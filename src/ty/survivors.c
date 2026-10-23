// Plan 79's measurement: at a checker recycle, what a copying recycle would keep. A build with -DTTTT_SURVIVORS
// (scripts/survivors.sh) records every allocation of the current checker's arena; survivors_report() marks
// conservatively (every word that points into a recorded allocation is a reference) from declaration-level roots and
// prints what is reachable, by type kind and by key kind. One checker on one thread (--checkers 1 --threads 1)
#ifdef TTTT_SURVIVORS

#include "ty/ty_internal.h"
#include "scope.h"

typedef struct SurvivorAlloc {
    uintptr_t at;
    u32 size;
    u32 site; // Return address relative to arena_alloc()
    u32 owner; // survivors_owner when it was made
    u32 unused;
} SurvivorAlloc;

// One checker arena's allocations (Arena.rec), and the file census' bookkeeping
typedef struct SurvivorRec {
    SurvivorAlloc* allocs;
    u32 count, cap;
    u32 id; // In creation order
    u32 seq; // Files checked (survivors_file_start())
    u32* at_owner; // Index among the checked files -> its sequence number
    u32 at_cap;
    // Node link keys -> survivors_owner of their last write (open addressing, 0: empty)
    u64* link_keys;
    u32* link_owner;
    u32 link_cap, link_len;
    // Looks at memos and intern entries (survivors_hit()), deduplicated by a direct-mapped cache
    struct SurvivorHit* hits;
    u64 hit_count, hit_cap;
} SurvivorRec;

typedef struct SurvivorHit {
    uintptr_t what;
    u32 owner;
    u32 kind;
} SurvivorHit;
static thread_local SurvivorHit hit_cache[1 << 16];

thread_local u32 survivors_owner;

static SurvivorAlloc* allocs;
static u32 alloc_count;

void survivors_record(Arena* arena, void* ptr, size_t size, void* site) {
    SurvivorRec* rec = arena->rec;
    if (!rec) return;
    if (rec->count == rec->cap) {
        rec->cap = rec->cap ? rec->cap * 2 : 1 << 16;
        rec->allocs = realloc(rec->allocs, sizeof(SurvivorAlloc) * rec->cap);
        panic_if(!rec->allocs, "out of memory");
    }
    rec->allocs[rec->count++] = (SurvivorAlloc){ (uintptr_t)ptr, (u32)size, (u32)((uintptr_t)site - (uintptr_t)&arena_alloc), survivors_owner, 0 };
}

void survivors_unrecord(Arena* arena, void* ptr) {
    SurvivorRec* rec = arena->rec;
    if (!rec || !rec->count || rec->allocs[rec->count - 1].at != (uintptr_t)ptr) return;
    rec->count--;
}

void survivors_free(Arena* arena) {
    SurvivorRec* rec = arena->rec;
    if (!rec) return;
    free(rec->allocs);
    free(rec->at_owner);
    free(rec->link_keys);
    free(rec->link_owner);
    free(rec->hits);
    free(rec);
    arena->rec = nullptr;
}

void survivors_reset(Arena* arena) {
    static _Atomic u32 next_id;
    survivors_free(arena);
    arena->rec = calloc(1, sizeof(SurvivorRec));
    arena->rec->id = next_id++;
}

u32 survivors_file_start(Checker const* me, u32 at) {
    SurvivorRec* rec = my arena.rec;
    if (!rec) return 0;
    u32 seq = ++rec->seq;
    if (at == UINT32_MAX) return seq;
    if (at >= rec->at_cap) {
        u32 cap = rec->at_cap ? rec->at_cap : 1024;
        while (cap <= at) cap *= 2;
        rec->at_owner = realloc(rec->at_owner, sizeof(u32) * cap);
        memset(rec->at_owner + rec->at_cap, 0, sizeof(u32) * (cap - rec->at_cap));
        rec->at_cap = cap;
    }
    rec->at_owner[at] = seq;
    return seq;
}

u32 survivors_owner_of(Checker const* me, u32 at) {
    SurvivorRec* rec = my arena.rec;
    return rec && at < rec->at_cap ? rec->at_owner[at] : 0;
}

static u32 link_slot(SurvivorRec const* rec, u64 key) {
    u32 i = (u32)((key * 0x9e3779b97f4a7c15ull) >> 40) & (rec->link_cap - 1);
    while (rec->link_keys[i] && rec->link_keys[i] != key) i = (i + 1) & (rec->link_cap - 1);
    return i;
}

void survivors_link_set(LinkTable const* table, u64 key) {
    Checker* me = current_checker;
    if (!me || table != &my node_links || !my arena.rec) return;
    SurvivorRec* rec = my arena.rec;
    if ((rec->link_len + 1) * 2 > rec->link_cap) {
        SurvivorRec bigger = { .link_cap = rec->link_cap ? rec->link_cap * 2 : 1 << 12 };
        bigger.link_keys = calloc(bigger.link_cap, sizeof(u64));
        bigger.link_owner = calloc(bigger.link_cap, sizeof(u32));
        for (u32 i = 0; i < rec->link_cap; i++) {
            if (!rec->link_keys[i]) continue;
            u32 j = link_slot(&bigger, rec->link_keys[i]);
            bigger.link_keys[j] = rec->link_keys[i];
            bigger.link_owner[j] = rec->link_owner[i];
        }
        free(rec->link_keys);
        free(rec->link_owner);
        rec->link_keys = bigger.link_keys;
        rec->link_owner = bigger.link_owner;
        rec->link_cap = bigger.link_cap;
    }
    u32 i = link_slot(rec, key);
    if (!rec->link_keys[i]) {
        rec->link_keys[i] = key;
        rec->link_len++;
    }
    rec->link_owner[i] = survivors_owner;
}

void survivors_hit(void const* what, u32 kind) {
    u32 owner = survivors_owner;
    Checker* me = current_checker;
    if (!owner || !me || !my arena.rec) return;
    SurvivorRec* rec = my arena.rec;
    u32 h = (u32)((((uintptr_t)what ^ kind) * 0x9e3779b97f4a7c15ull) >> 48);
    SurvivorHit* c = &hit_cache[h];
    if (c->what == (uintptr_t)what && c->owner == owner && c->kind == kind) return;
    *c = (SurvivorHit){ (uintptr_t)what, owner, kind };
    if (rec->hit_count == rec->hit_cap) {
        rec->hit_cap = rec->hit_cap ? rec->hit_cap * 2 : 1 << 16;
        rec->hits = realloc(rec->hits, sizeof(SurvivorHit) * rec->hit_cap);
        panic_if(!rec->hits, "out of memory");
    }
    rec->hits[rec->hit_count++] = *c;
}

static int hit_cmp(void const* a, void const* b) {
    SurvivorHit const* x = a;
    SurvivorHit const* y = b;
    if (x->what != y->what) return x->what < y->what ? -1 : 1;
    if (x->kind != y->kind) return x->kind < y->kind ? -1 : 1;
    return x->owner < y->owner ? -1 : x->owner > y->owner;
}

static u32 link_owner_of(SurvivorRec const* rec, u64 key) {
    if (!rec->link_cap) return 0;
    u32 i = link_slot(rec, key);
    return rec->link_keys[i] ? rec->link_owner[i] : 0;
}

// How a key kind's entries fare at a copy
typedef enum {
    FATE_EXPR,     // Expression-level: dropped
    FATE_ROOT,     // Declaration-level: kept (a Symbol subject only when not a local of a function)
    FATE_IDENTITY, // The value's identity: kept when the value survives (else a second copy could be made)
    FATE_CACHE,    // A cache: may go, worth keeping when every part of it survives
} Fate;

enum { KEY_INTERSECTION_PROPS = 254 }; // (The intersection_props table's keys aren't tagged)

static Fate kind_fate(u32 kind) {
    switch (kind) {
    case KEY_INTERSECTION_PROPS:
        return FATE_CACHE;
    case KEY_TYPE_PARAM: case KEY_SYMBOL_LINKS: case KEY_GENERIC_PARAMS: case KEY_NAMESPACE: case KEY_TYPE_ONLY_EXPORT:
    case KEY_MERGED_VALUE: case KEY_ALIAS_VARIANCES: case KEY_DECL_GROUP: case KEY_DECL_GROUP_OF: case KEY_PRIVATE_NAME:
    case KEY_PRIVATE_NAME_CLASS: case KEY_NAMESPACE_IMPORT: case KEY_CLASS_MEMBER_SYMBOLS:
        return FATE_ROOT;
    case KEY_LITERAL: case KEY_UNION: case KEY_INTERSECTION: case KEY_REFERENCE: case KEY_TUPLE_TARGET: case KEY_KEYOF:
    case KEY_INDEXED: case KEY_CONDITIONAL: case KEY_SUBSTITUTION: case KEY_ANONYMOUS_INSTANCE: case KEY_UNIQUE_SYMBOL:
    case KEY_TEMPLATE_LITERAL: case KEY_STRING_MAPPING: case KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT:
    case KEY_CONDITIONAL_INSTANCE: case KEY_ALIAS_INSTANCE: case KEY_NODE: case KEY_DEFERRED_OUTER:
    case KEY_DEFERRED_REFERENCE: case KEY_STRUCTURAL: case KEY_RESTRICTIVE_PARAM: case KEY_MARKER: case KEY_CHECK_MARKER:
    case KEY_FUNCTION: case KEY_UNIQUE_SYMBOL_NAME: case KEY_UNDEFINED_PROPERTY: case KEY_FUNCTION_RETURN:
    case KEY_IMPLEMENTATION_SIGNATURE: case KEY_SIGNATURE_INSTANCE: case KEY_SIGNATURE_TYPE: case KEY_UNION_NODE:
    case KEY_MODULE_CLONE: case KEY_FRESH_LITERAL:
        return FATE_IDENTITY;
    case KEY_REGULAR_UNION: case KEY_WIDENED_OBJECTS: case KEY_REGULAR_OBJECT_LITERAL: case KEY_MAPPED_KEY_PARAM:
    case KEY_KEYOF_PROPERTIES: case KEY_SIMPLIFIED: case KEY_NORMALIZED_INTERSECTION: case KEY_MAPPED_FILTERING:
    case KEY_PROP_MAPPED_TYPE: case KEY_PROP_WRITE_TYPE: case KEY_UNION_ACCESS: case KEY_BASE_CONSTRAINT:
    case KEY_INFERRED_TRUE_BRANCH: case KEY_DISTRIBUTION_DEPENDENT: case KEY_DISTRIBUTIVE_CONSTRAINT:
    case KEY_IMPLIED_CONSTRAINT: case KEY_TYPE_VARIABLE_REDUCTION: case KEY_DEFINITELY_RELATED:
    case KEY_CANONICAL_SIGNATURE: case KEY_BASE_SIGNATURE: case KEY_VARIANCES: case KEY_ALIAS_MARKER:
    case KEY_CHECK_MARKER_REF: case KEY_GENERIC_RELATION: case KEY_ITERATION_TYPES: case KEY_WIDENED_NULLABLE:
    case KEY_INTERSECTION_PROP_CONTEXT: case KEY_SYNTHETIC_PROP_PARTS: case KEY_UNION_PAIR:
    case KEY_INTERSECTION_OF: case KEY_REVERSE_MAPPED: case KEY_UNION_KEY_NAME: case KEY_UNION_KEY_MEMBER: case KEY_FLOW_NOT_UNIT:
    case KEY_EVOLVING_ARRAY: case KEY_REDUCED_UNION: case KEY_FUNCTION_FREE_PARAMS: case KEY_FUNCTION_TYPE_MENTIONS:
    case KEY_ARRAY_THIS_PROPS: case KEY_TUPLE_NAME_ORDER: case KEY_INDEX_NAME_ORDER: case KEY_DECIMAL_ORDER: case KEY_THISLESS_SIGNATURES: case KEY_INSTANTIATION_QUERY:
        return FATE_CACHE;
    default:
        return FATE_EXPR;
    }
}

static bool symbol_keyed(u32 kind) {
    switch (kind) {
    case KEY_TYPE_PARAM: case KEY_SYMBOL_LINKS: case KEY_NAMESPACE: case KEY_TYPE_ONLY_EXPORT: case KEY_MERGED_VALUE:
    case KEY_ALIAS_VARIANCES: case KEY_DECL_GROUP_OF: case KEY_PRIVATE_NAME: case KEY_NAMESPACE_IMPORT:
        return true;
    default:
        return false;
    }
}

static uint8_t* marks;
static u32* work;
static u32 work_len;

static i64 alloc_find(uintptr_t p) {
    if (!alloc_count || p < allocs[0].at) return -1;
    u32 lo = 0, hi = alloc_count; // allocs[lo].at <= p
    while (hi - lo > 1) {
        u32 mid = lo + (hi - lo) / 2;
        if (allocs[mid].at <= p) lo = mid; else hi = mid;
    }
    return p < allocs[lo].at + allocs[lo].size ? (i64)lo : -1;
}

static void mark_ptr(uintptr_t p) {
    i64 i = alloc_find(p);
    if (i < 0 || marks[i]) return;
    marks[i] = 1;
    work[work_len++] = (u32)i;
}

static bool is_marked(uintptr_t p) {
    i64 i = alloc_find(p);
    return i < 0 || marks[i]; // (Outside the arena: the AST, the binder, malloc: always there)
}

static void mark_words(void const* from, size_t bytes) {
    uintptr_t const* words = from;
    for (size_t w = 0; w < bytes / 8; w++) mark_ptr(words[w]);
}

static void drain(void) {
    while (work_len) {
        SurvivorAlloc const* a = &allocs[work[--work_len]];
        mark_words((void const*)a->at, a->size);
    }
}

// An entry the copy is to decide on: its arena block (0: in the entry), the value, its kind and key words
typedef struct Pending {
    uintptr_t block;
    uintptr_t value;
    u64 const* key; // Its words after the kind, null for a node link
    u32 key_len;
    uintptr_t subject; // A node link's
    u32 kind;
    bool kept;
} Pending;

static Pending* pending;
static u32 pending_len, pending_cap;

static void pend(Pending p) {
    if (pending_len == pending_cap) {
        pending_cap = pending_cap ? pending_cap * 2 : 1 << 16;
        pending = realloc(pending, sizeof(Pending) * pending_cap);
    }
    pending[pending_len++] = p;
}

static int alloc_cmp(void const* a, void const* b) {
    uintptr_t x = ((SurvivorAlloc const*)a)->at, y = ((SurvivorAlloc const*)b)->at;
    return x < y ? -1 : x > y;
}

// A key word of a cache is a type id, an address in the arena (both must survive) or something else (the AST, flags)
static bool key_survives(Checker const* me, u64 const* key, u32 len) {
    for (u32 k = 0; k < len; k++) {
        u64 w = key[k];
        if (alloc_find((uintptr_t)w) >= 0) {
            if (!is_marked((uintptr_t)w)) return false;
        } else if (w && w < my next_type_id) {
            Ty const* ty = lazy_slots_type(&my lazy, (u32)w);
            if (ty && !is_marked((uintptr_t)ty)) return false;
        } else if ((w >> 32) && (w >> 32) < my next_type_id && (u32)w < my next_type_id && (w >> 32) < 0x5000) {
            // (ty_id_pair())
            Ty const* a = lazy_slots_type(&my lazy, (u32)(w >> 32));
            Ty const* b = lazy_slots_type(&my lazy, (u32)w);
            if ((a && !is_marked((uintptr_t)a)) || (b && !is_marked((uintptr_t)b))) return false;
        }
    }
    return true;
}

static void table_pend(InternTable const* table, bool atoms, u32 forced_kind) {
    for (u32 i = 0; i < table->cap; i++) {
        InternEntry const* entry = &table->entries[i];
        if (!entry->hash) continue;
        if (atoms) {
            pend((Pending){ .block = (uintptr_t)entry->block, .value = (uintptr_t)entry->block, .kind = 255 });
            continue;
        }
        u64 const* block = (u64 const*)entry->block;
        u32 count = (u32)(entry->hash >> 48);
        bool long_key = count == 0xffff;
        if (long_key) count = (u32)block[1];
        u64 const* key = block + 1 + long_key;
        u32 kind = forced_kind ? forced_kind : count ? (u32)(key[0] & 255) : 0;
        pend((Pending){ .block = (uintptr_t)block, .value = (uintptr_t)block[0], .key = key + 1, .key_len = count ? count - 1 : 0, .kind = kind });
    }
}

void survivors_report(Checker const* me) {
    allocs = my arena.rec ? my arena.rec->allocs : nullptr;
    alloc_count = my arena.rec ? my arena.rec->count : 0;
    qsort(allocs, alloc_count, sizeof(SurvivorAlloc), alloc_cmp);
    marks = calloc(alloc_count ? alloc_count : 1, 1);
    work = malloc(sizeof(u32) * (alloc_count ? alloc_count : 1));
    work_len = 0;
    pending_len = 0;
    u64 total = 0;
    for (u32 i = 0; i < alloc_count; i++) total += allocs[i].size;
    bool lazy_roots = getenv("TTTT_SURVIVORS_LAZY") != nullptr;

    // Roots: the checker's own fields (intrinsics, global types, array targets...)
    mark_words(me, sizeof(Checker));
    table_pend(&my types, false, 0);
    table_pend(&my links, false, 0);
    table_pend(&my intersection_props, false, KEY_INTERSECTION_PROPS);
    for (u32 i = 0; i < my node_links.cap; i++) {
        LinkEntry const* entry = &my node_links.entries[i];
        if (!entry->key) continue;
        pend((Pending){ .value = (uintptr_t)entry->value, .subject = entry->key & ((1ull << 56) - 1), .kind = (u32)(entry->key >> 56) - 1 });
    }
    u64 root_entries = 0;
    for (u32 i = 0; i < pending_len; i++) {
        Pending* p = &pending[i];
        if (p->kind == 255 || kind_fate(p->kind) != FATE_ROOT) continue;
        uintptr_t subject = p->key ? (p->key_len ? p->key[0] : 0) : p->subject;
        if (symbol_keyed(p->kind) && subject && ((Symbol const*)subject)->function) continue;
        p->kept = true;
        root_entries++;
        mark_ptr(p->block);
        mark_ptr(p->value);
    }
    u64 lazy_memos = 0;
    if (lazy_roots) {
        for (size_t i = 0; i < c_arraylen(my lazy.top); i++) {
            if (!my lazy.top[i]) continue;
            for (size_t j = 0; j < (1 << LAZY_DIR_BITS); j++) {
                LazyMemo const* page = my lazy.top[i][j];
                if (!page) continue;
                for (size_t k = 0; k < (1 << LAZY_PAGE_BITS); k++) {
                    if (!page[k].evaluated) continue;
                    lazy_memos++;
                    mark_ptr((uintptr_t)lazy_slots_type(&my lazy, page[k].evaluated));
                }
            }
        }
    }
    drain();
    // Identities: kept while their value survives, which keeps their keys; to a fixpoint
    for (bool changed = true; changed;) {
        changed = false;
        for (u32 i = 0; i < pending_len; i++) {
            Pending* p = &pending[i];
            if (p->kept || !(p->kind == 255 || kind_fate(p->kind) == FATE_IDENTITY)) continue;
            if (!p->value || !is_marked(p->value) || alloc_find(p->value) < 0) continue;
            p->kept = true;
            changed = true;
            mark_ptr(p->block);
        }
        drain();
    }
    u64 graph = 0;
    for (u32 i = 0; i < alloc_count; i++) graph += marks[i] ? allocs[i].size : 0;
    // Caches over survivors: what keeping them would add (their blocks and what only they reach)
    u64 cache_entries = 0, cache_total = 0;
    for (u32 i = 0; i < pending_len; i++) {
        Pending* p = &pending[i];
        if (p->kept || p->kind == 255 || kind_fate(p->kind) != FATE_CACHE) continue;
        cache_total++;
        if (p->key && !key_survives(me, p->key, p->key_len)) continue;
        if (!p->key && !is_marked(p->subject)) continue;
        if (p->value && alloc_find(p->value) >= 0 && !is_marked(p->value)) continue;
        p->kept = true;
        cache_entries++;
        mark_ptr(p->block);
    }
    drain();
    u64 with_caches = 0;
    for (u32 i = 0; i < alloc_count; i++) with_caches += marks[i] ? allocs[i].size : 0;

    // By type kind (the type directory names which allocations are types) and by key kind (intern blocks)
    u64 kind_bytes[32][2] = {0}, kind_count[32][2] = {0};
    uint8_t* is_type = calloc(alloc_count ? alloc_count : 1, 1);
    for (u32 id = 1; id < my next_type_id; id++) {
        Ty const* ty = lazy_slots_type(&my lazy, id);
        i64 a = ty ? alloc_find((uintptr_t)ty) : -1;
        if (a < 0) continue;
        is_type[a] = 1;
        u32 k = ty->kind < 32 ? ty->kind : 31;
        kind_bytes[k][marks[a]] += allocs[a].size;
        kind_count[k][marks[a]]++;
    }
    u64 key_bytes[256][2] = {0}, key_count[256][2] = {0};
    uint8_t* is_block = calloc(alloc_count ? alloc_count : 1, 1);
    for (u32 i = 0; i < pending_len; i++) {
        Pending const* p = &pending[i];
        i64 a = p->block ? alloc_find(p->block) : -1;
        u32 k = p->kind & 255;
        if (a >= 0 && !is_type[a]) {
            is_block[a] = 1;
            key_bytes[k][marks[a]] += allocs[a].size;
        }
        key_count[k][p->kept]++;
    }
    // The rest by allocating site
    typedef struct { u32 site; u64 bytes[2]; } SiteBytes;
    SiteBytes* sites = calloc(4096, sizeof(SiteBytes));
    for (u32 i = 0; i < alloc_count; i++) {
        if (is_type[i] || is_block[i]) continue;
        u32 h = (allocs[i].site * 2654435761u) & 4095;
        while (sites[h].site && sites[h].site != allocs[i].site) h = (h + 1) & 4095;
        sites[h].site = allocs[i].site;
        sites[h].bytes[marks[i]] += allocs[i].size;
    }
    eprintln("survivors-total ", (i64)total, " ", (i64)alloc_count, " memory ", (i64)checker_memory(me), " types ", (i64)my next_type_id);
    eprintln("survivors-graph ", (i64)graph, " with-caches ", (i64)with_caches, " roots ", (i64)root_entries,
        " caches-kept ", (i64)cache_entries, " of ", (i64)cache_total, " lazy-memos ", (i64)lazy_memos);
    for (u32 k = 0; k < 32; k++) {
        if (kind_count[k][0] + kind_count[k][1]) {
            eprintln("survivors-ty ", (i64)k, " ", (i64)kind_bytes[k][1], " ", (i64)kind_bytes[k][0], " ", (i64)kind_count[k][1], " ", (i64)kind_count[k][0]);
        }
    }
    for (u32 k = 0; k < 256; k++) {
        if (key_count[k][0] + key_count[k][1]) {
            eprintln("survivors-key ", (i64)k, " ", (i64)key_bytes[k][1], " ", (i64)key_bytes[k][0], " ", (i64)key_count[k][1], " ", (i64)key_count[k][0]);
        }
    }
    for (u32 h = 0; h < 4096; h++) {
        if (sites[h].site) eprintln("survivors-site ", (i64)sites[h].site, " ", (i64)sites[h].bytes[1], " ", (i64)sites[h].bytes[0]);
    }
    free(sites);
    free(is_type);
    free(is_block);
    free(marks);
    free(work);
}

// The file census (TTTT_FILE_CENSUS=1, a -DTTTT_SURVIVORS build): which of the checker's results could have been
// dropped when the file they were made for was done. Every allocation, node link entry and LazyType memo has an owner:
// the checker's sequence number of the file whose check (or after-check pass) made it, 0 for the warm-up. An allocation
// is needed after its file when a declaration-level root reaches it (the checker's fields, the warm-up's allocations,
// FATE_ROOT entries, declarations' memos) or when anything made for a *later* file points to it (that file obtained it,
// from an intern table or a memo, after its own file was done), and then so is what it reaches; intern identities are
// kept while their value is, caches while their key and value are. The rest of a file's allocations could go at its end
static pthread_mutex_t census_lock = PTHREAD_MUTEX_INITIALIZER;
static u32* owners; // Of allocs, by index
static uint8_t* skip; // Lazy memo pages and directories: not scanned as words (they hold ids)
static uint8_t* referenced; // Something other than itself points to it

static void census_seed(i64 j) {
    if (j < 0 || marks[j]) return;
    marks[j] = 1;
    work[work_len++] = (u32)j;
}

// `from` (owner `owner`, 0: a root) points at word `w`
static void census_ref(u32 owner, uintptr_t w, i64 self) {
    i64 j = alloc_find(w);
    if (j < 0 || j == self || skip[j]) return;
    referenced[j] = 1;
    if (!owner || (owners[j] && owner > owners[j])) census_seed(j);
}

static void census_drain(void) {
    while (work_len) {
        u32 i = work[--work_len];
        if (skip[i]) continue;
        SurvivorAlloc const* a = &allocs[i];
        uintptr_t const* words = (uintptr_t const*)a->at;
        for (u32 w = 0; w < a->size / 8; w++) {
            i64 j = alloc_find(words[w]);
            if (j >= 0 && !skip[j]) census_seed(j);
        }
    }
}

static i64 census_type(Checker const* me, u32 id) {
    if (!id) return -1;
    if (!my lazy.types[id >> LAZY_TYPE_CHUNK_BITS]) return -1;
    Ty const* ty = lazy_slots_type(&my lazy, id);
    return ty ? alloc_find((uintptr_t)ty) : -1;
}

enum { CAT_TYPE, CAT_KEY_IDENTITY, CAT_KEY_CACHE, CAT_KEY_MEMO, CAT_KEY_ROOT, CAT_LAZY, CAT_IPROPS, CAT_OTHER, CAT_COUNT };
static char const* const cat_names[] = { "type", "key-identity", "key-cache", "key-memo", "key-root", "lazy-pages", "iprops", "other" };
// warm, kept, dropped (referenced), dropped (nothing points to it)
enum { ST_WARM, ST_KEPT, ST_DROP, ST_GARBAGE, ST_COUNT };

void survivors_file_census(Checker const* me, char const* when) {
    SurvivorRec* rec = my arena.rec;
    if (!rec) return;
    pthread_mutex_lock(&census_lock);
    allocs = rec->allocs;
    alloc_count = rec->count;
    qsort(allocs, alloc_count, sizeof(SurvivorAlloc), alloc_cmp);
    u32 n = alloc_count ? alloc_count : 1;
    marks = calloc(n, 1);
    skip = calloc(n, 1);
    referenced = calloc(n, 1);
    owners = malloc(sizeof(u32) * n);
    work = malloc(sizeof(u32) * n);
    work_len = 0;
    pending_len = 0;
    uint8_t* cat = calloc(n, 1);
    for (u32 i = 0; i < alloc_count; i++) {
        owners[i] = allocs[i].owner;
        cat[i] = CAT_OTHER;
    }
    // Memo pages and directories
    u64 memo_count[8][3] = {0}; // by census tag: warm, file-made, of which with a type
    for (size_t t = 0; t < c_arraylen(my lazy.top); t++) {
        if (!my lazy.top[t]) continue;
        i64 d = alloc_find((uintptr_t)my lazy.top[t]);
        if (d >= 0) { skip[d] = 1; cat[d] = CAT_LAZY; }
        for (size_t p = 0; p < (1 << LAZY_DIR_BITS); p++) {
            LazyMemo const* page = my lazy.top[t][p];
            if (!page) continue;
            i64 a = alloc_find((uintptr_t)page);
            if (a >= 0) { skip[a] = 1; cat[a] = CAT_LAZY; }
        }
    }
    // The types (by the directory)
    uint8_t* is_type = calloc(n, 1);
    for (u32 b = 0; b < my lazy.own_len; b++) {
        u32 chunk = my lazy.own[b];
        Ty const** types = my lazy.types[chunk];
        if (!types) continue;
        for (u32 k = 0; k < (1u << LAZY_TYPE_CHUNK_BITS); k++) {
            Ty const* ty = types[k];
            i64 a = ty ? alloc_find((uintptr_t)ty) : -1;
            if (a >= 0 && (uintptr_t)ty == allocs[a].at) { is_type[a] = 1; cat[a] = CAT_TYPE; }
        }
    }
    // Roots: the checker's fields and the warm-up's allocations
    mark_words(me, sizeof(Checker));
    for (u32 i = 0; i < alloc_count; i++) if (!owners[i] && !skip[i]) census_seed(i);
    // Every allocation as a referrer
    for (u32 i = 0; i < alloc_count; i++) {
        if (skip[i]) continue;
        uintptr_t const* words = (uintptr_t const*)allocs[i].at;
        for (u32 w = 0; w < allocs[i].size / 8; w++) census_ref(owners[i] ? owners[i] : 0, words[w], i);
    }
    // Memos as referrers (declarations' are roots)
    for (size_t t = 0; t < c_arraylen(my lazy.top); t++) {
        if (!my lazy.top[t]) continue;
        for (size_t p = 0; p < (1 << LAZY_DIR_BITS); p++) {
            LazyMemo const* page = my lazy.top[t][p];
            if (!page) continue;
            for (size_t k = 0; k < (1 << LAZY_PAGE_BITS); k++) {
                LazyMemo const* m = &page[k];
                if (!m->census_tag) continue;
                u32 tag = m->census_tag < 8 ? m->census_tag : 7;
                memo_count[tag][m->census_owner ? 1 : 0]++;
                if (m->evaluated) memo_count[tag][2]++;
                bool root = !m->census_owner || tag == LAZY_CENSUS_DECL || tag == LAZY_CENSUS_FIELD || tag == LAZY_CENSUS_OTHER;
                u32 ids[2] = { m->evaluated, m->context };
                for (u32 x = 0; x < 2; x++) {
                    i64 j = census_type(me, ids[x]);
                    if (j < 0) continue;
                    referenced[j] = 1;
                    if (root || (owners[j] && m->census_owner > owners[j])) census_seed(j);
                }
            }
        }
    }
    // Intern tables and node links: entries to decide on
    table_pend(&my types, false, 0);
    table_pend(&my links, false, 0);
    u32 iprops_from = pending_len;
    table_pend(&my intersection_props, false, KEY_INTERSECTION_PROPS);
    u32 links_from = pending_len;
    u64 link_kind[256][2] = {0}; // node link entries by kind: kept, dropped
    uint8_t* block_kind = calloc(n, 1);
    for (u32 i = 0; i < my node_links.cap; i++) {
        LinkEntry const* entry = &my node_links.entries[i];
        if (!entry->key) continue;
        pend((Pending){ .value = (uintptr_t)entry->value, .subject = entry->key & ((1ull << 56) - 1),
            .kind = (u32)(entry->key >> 56) - 1, .key_len = link_owner_of(rec, entry->key) });
    }
    for (u32 i = 0; i < pending_len; i++) {
        Pending* p = &pending[i];
        i64 b = p->block ? alloc_find(p->block) : -1;
        Fate fate = p->kind == 255 ? FATE_IDENTITY : kind_fate(p->kind);
        if (b >= 0 && (uintptr_t)allocs[b].at == p->block) block_kind[b] = (uint8_t)(p->kind & 255);
        if (b >= 0 && !is_type[b] && (uintptr_t)allocs[b].at == p->block) {
            cat[b] = i >= iprops_from ? CAT_IPROPS : fate == FATE_ROOT ? CAT_KEY_ROOT : fate == FATE_IDENTITY ? CAT_KEY_IDENTITY :
                fate == FATE_CACHE ? CAT_KEY_CACHE : CAT_KEY_MEMO;
        }
        if (fate != FATE_ROOT) continue;
        uintptr_t subject = i >= links_from ? p->subject : p->key && p->key_len ? p->key[0] : 0;
        if (symbol_keyed(p->kind) && subject && ((Symbol const*)subject)->function) continue;
        p->kept = true;
        census_seed(b);
        if (i >= links_from) {
            census_seed(alloc_find(p->value));
            census_seed(alloc_find(p->subject));
        }
    }
    // Node links as referrers (an entry's owner is in key_len)
    for (u32 i = links_from; i < pending_len; i++) {
        Pending* p = &pending[i];
        if (p->kept) continue;
        census_ref(p->key_len, p->value, -1);
        census_ref(p->key_len, p->subject, -1);
    }
    census_drain();
    // Identities, to a fixpoint (a node link identity keeps its subject too)
    for (bool changed = true; changed;) {
        changed = false;
        for (u32 i = 0; i < pending_len; i++) {
            Pending* p = &pending[i];
            if (p->kept || !(p->kind == 255 || kind_fate(p->kind) == FATE_IDENTITY)) continue;
            i64 v = alloc_find(p->value);
            bool keep;
            if (v >= 0) keep = marks[v];
            else if (i < links_from) keep = true; // (A value outside the arena: kept)
            else {
                i64 s2 = alloc_find(p->subject);
                keep = s2 < 0 || marks[s2] || !owners[s2];
            }
            if (!keep) continue;
            p->kept = true;
            changed = true;
            census_seed(p->block ? alloc_find(p->block) : -1);
            if (i >= links_from) census_seed(alloc_find(p->subject));
        }
        census_drain();
    }
    // Caches whose parts are kept stay (their own blocks are counted kept)
    for (u32 i = 0; i < pending_len; i++) {
        Pending* p = &pending[i];
        if (p->kept || p->kind == 255 || kind_fate(p->kind) != FATE_CACHE) continue;
        if (i < links_from && p->key && !key_survives(me, p->key, p->key_len)) continue;
        if (i >= links_from && alloc_find(p->subject) >= 0 && !marks[alloc_find(p->subject)]) continue;
        if (p->value && alloc_find(p->value) >= 0 && !marks[alloc_find(p->value)]) continue;
        p->kept = true;
        census_seed(p->block ? alloc_find(p->block) : -1);
    }
    census_drain();
    // Node link entries: kept if decided so, or made in the warm-up, or keyed and valued by what is kept
    u64 links_kept = 0, links_dropped = 0;
    for (u32 i = links_from; i < pending_len; i++) {
        Pending* p = &pending[i];
        bool kept = p->kept || !p->key_len;
        if (!kept) {
            i64 v = alloc_find(p->value), s2 = alloc_find(p->subject);
            Fate fate = kind_fate(p->kind);
            // (A memo of a file's expression goes with its file; anything else stays while its parts do)
            kept = fate != FATE_EXPR && (v < 0 || marks[v] || !owners[v]) && (s2 < 0 || marks[s2] || !owners[s2]);
        }
        link_kind[p->kind & 255][kept ? 0 : 1]++;
        if (!kept && rec->link_cap) {
            u64 key = (u64)(p->kind + 1) << 56 | p->subject;
            u32 slot = link_slot(rec, key);
            if (rec->link_keys[slot]) rec->link_owner[slot] |= 1u << 31;
        }
        if (kept) links_kept++; else links_dropped++;
    }
    // Tally
    u64 st_bytes[ST_COUNT] = {0}, st_count[ST_COUNT] = {0}, cat_bytes[CAT_COUNT][ST_COUNT] = {0};
    u64 kind_bytes[32][ST_COUNT] = {0};
    typedef struct { u32 site; u64 bytes[ST_COUNT]; } SiteBytes;
    SiteBytes* sites = calloc(8192, sizeof(SiteBytes));
    u64 total = 0;
    for (u32 i = 0; i < alloc_count; i++) {
        u32 st = !owners[i] ? ST_WARM : marks[i] ? ST_KEPT : referenced[i] ? ST_DROP : ST_GARBAGE;
        u64 size = allocs[i].size;
        if (cat[i] == CAT_LAZY) size = size * 12 / sizeof(LazyMemo); // (Without the census' own fields)
        total += size;
        st_bytes[st] += size;
        st_count[st]++;
        cat_bytes[cat[i]][st] += size;
        if (is_type[i]) {
            u32 k = ((Ty const*)allocs[i].at)->kind;
            kind_bytes[k < 32 ? k : 31][st] += size;
        }
        if (cat[i] != CAT_OTHER) continue;
        u32 h = (allocs[i].site * 2654435761u) & 8191;
        while (sites[h].site && sites[h].site != allocs[i].site) h = (h + 1) & 8191;
        sites[h].site = allocs[i].site;
        sites[h].bytes[st] += size;
    }
    // Relation cache entries naming a type that goes
    u64 rel_len = 0, rel_cap_bytes = 0, rel_dropped = 0, rel_unknown = 0;
    for (u32 r = 0; r < TY_RELATION_COUNT; r++) {
        RelationCache const* cache = &my relations[r];
        rel_len += cache->len;
        rel_cap_bytes += (u64)cache->cap * 9;
        for (u32 i = 0; i < cache->cap; i++) {
            u64 key = cache->keys[i];
            if (!key) continue;
            u32 a = (u32)(key >> 32) & 0x3fffffff, b = (u32)key;
            i64 x = census_type(me, a), y = census_type(me, b);
            if (x < 0 || y < 0) { rel_unknown++; continue; }
            if ((owners[x] && !marks[x]) || (owners[y] && !marks[y])) rel_dropped++;
        }
    }
    // Later files' looks at what goes at its file's end: what they would compute again
    u64 reuse[3][256] = {0}, looks[3] = {0};
    qsort(rec->hits, rec->hit_count, sizeof(SurvivorHit), hit_cmp);
    for (u64 i = 0; i < rec->hit_count; i++) {
        SurvivorHit const* h = &rec->hits[i];
        if (i && h->what == h[-1].what && h->kind == h[-1].kind && h->owner == h[-1].owner) continue;
        looks[h->kind]++;
        if (h->kind == 0) {
            i64 b = alloc_find(h->what);
            if (b >= 0 && owners[b] && owners[b] < h->owner && !marks[b]) reuse[0][block_kind[b]]++;
        } else if (h->kind == 1) {
            if (!rec->link_cap) continue;
            u32 slot = link_slot(rec, h->what);
            u32 o = rec->link_owner[slot];
            if (rec->link_keys[slot] && (o >> 31) && (o & 0x7fffffff) && (o & 0x7fffffff) < h->owner) reuse[1][(h->what >> 56) - 1]++;
        } else {
            LazyMemo const* m = (LazyMemo const*)h->what;
            if (m->census_owner && m->census_owner < h->owner) reuse[2][m->census_tag]++;
        }
    }
    u32 id = rec->id;
    for (u32 k = 0; k < 3; k++) {
        eprintln("fcensus-looks ", (i64)id, " ", (i64)k, " ", (i64)looks[k]);
        for (u32 j = 0; j < 256; j++) if (reuse[k][j]) eprintln("fcensus-reuse ", (i64)id, " ", (i64)k, " ", (i64)j, " ", (i64)reuse[k][j]);
    }
    eprintln("fcensus-total ", (i64)id, " ", when, " arena ", (i64)total, " allocs ", (i64)alloc_count, " memory ",
        (i64)checker_memory(me), " files ", (i64)rec->seq, " types ", (i64)checker_type_count(me));
    for (u32 st = 0; st < ST_COUNT; st++) eprintln("fcensus-state ", (i64)id, " ", (i64)st, " ", (i64)st_bytes[st], " ", (i64)st_count[st]);
    for (u32 c = 0; c < CAT_COUNT; c++) {
        eprintln("fcensus-cat ", (i64)id, " ", cat_names[c], " ", (i64)cat_bytes[c][0], " ", (i64)cat_bytes[c][1], " ",
            (i64)cat_bytes[c][2], " ", (i64)cat_bytes[c][3]);
    }
    for (u32 k = 0; k < 32; k++) {
        u64 sum = kind_bytes[k][0] + kind_bytes[k][1] + kind_bytes[k][2] + kind_bytes[k][3];
        if (sum) eprintln("fcensus-tykind ", (i64)id, " ", (i64)k, " ", (i64)kind_bytes[k][0], " ", (i64)kind_bytes[k][1], " ",
            (i64)kind_bytes[k][2], " ", (i64)kind_bytes[k][3]);
    }
    for (u32 t = 1; t < 8; t++) {
        if (memo_count[t][0] + memo_count[t][1]) eprintln("fcensus-memo ", (i64)id, " ", (i64)t, " ", (i64)memo_count[t][0], " ",
            (i64)memo_count[t][1], " ", (i64)memo_count[t][2]);
    }
    for (u32 k = 0; k < 256; k++) {
        if (link_kind[k][0] + link_kind[k][1]) eprintln("fcensus-link ", (i64)id, " ", (i64)k, " ", (i64)link_kind[k][0], " ", (i64)link_kind[k][1]);
    }
    eprintln("fcensus-tables ", (i64)id, " types ", (i64)my types.len, " ", (i64)my types.cap, " links ", (i64)my links.len, " ",
        (i64)my links.cap, " iprops ", (i64)my intersection_props.len, " ", (i64)my intersection_props.cap);
    eprintln("fcensus-nodelinks ", (i64)id, " ", (i64)my node_links.len, " ", (i64)my node_links.cap, " kept ", (i64)links_kept,
        " dropped ", (i64)links_dropped);
    eprintln("fcensus-relations ", (i64)id, " ", (i64)rel_len, " ", (i64)rel_cap_bytes, " dropped ", (i64)rel_dropped,
        " unknown ", (i64)rel_unknown, " type_chunks ", (i64)my lazy.own_len);
    for (u32 h = 0; h < 8192; h++) {
        if (sites[h].site) eprintln("fcensus-site ", (i64)id, " ", (i64)sites[h].site, " ", (i64)sites[h].bytes[0], " ",
            (i64)sites[h].bytes[1], " ", (i64)sites[h].bytes[2], " ", (i64)sites[h].bytes[3]);
    }
    free(sites);
    free(block_kind);
    free(cat);
    free(is_type);
    free(owners);
    free(skip);
    free(referenced);
    free(marks);
    free(work);
    pthread_mutex_unlock(&census_lock);
}

#else
typedef int survivors_unused; // (ISO C: a translation unit declares something)
#endif
