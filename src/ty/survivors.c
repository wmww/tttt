// Plan 79's measurement: at a checker recycle, what a copying recycle would keep. A build with -DTTTT_SURVIVORS
// (scripts/survivors.sh) records every allocation of the current checker's arena; survivors_report() marks
// conservatively (every word that points into a recorded allocation is a reference) from declaration-level roots and
// prints what is reachable, by type kind and by key kind. One checker on one thread (--checkers 1 --threads 1)
#ifdef TTTT_SURVIVORS

#include "ty/ty_internal.h"
#include "scope.h"

Arena* survivors_arena;

typedef struct SurvivorAlloc {
    uintptr_t at;
    u32 size;
    u32 site; // Return address relative to arena_alloc()
} SurvivorAlloc;

static SurvivorAlloc* allocs;
static u32 alloc_count, alloc_cap;

void survivors_record(Arena* arena, void* ptr, size_t size, void* site) {
    if (arena != survivors_arena) return;
    if (alloc_count == alloc_cap) {
        alloc_cap = alloc_cap ? alloc_cap * 2 : 1 << 20;
        allocs = realloc(allocs, sizeof(SurvivorAlloc) * alloc_cap);
    }
    allocs[alloc_count++] = (SurvivorAlloc){ (uintptr_t)ptr, (u32)size, (u32)((uintptr_t)site - (uintptr_t)&arena_alloc) };
}

void survivors_unrecord(Arena* arena, void* ptr) {
    if (arena != survivors_arena || !alloc_count || allocs[alloc_count - 1].at != (uintptr_t)ptr) return;
    alloc_count--;
}

void survivors_reset(Arena* arena) {
    survivors_arena = arena;
    alloc_count = 0;
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
    case KEY_INTERSECTION_PROP_CONTEXT: case KEY_SYNTHETIC_PROP_PARTS: case KEY_OPTIONAL_PARAM: case KEY_UNION_PAIR:
    case KEY_INTERSECTION_OF: case KEY_REVERSE_MAPPED: case KEY_UNION_KEY_NAME: case KEY_UNION_KEY_MEMBER:
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
    table_pend(&my atoms, true, 0);
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

#else
typedef int survivors_unused; // (ISO C: a translation unit declares something)
#endif
