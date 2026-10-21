// madvise(), which strict -std=c2x hides
#define _DEFAULT_SOURCE
#include "error.h"
#include "ty/ty_internal.h"
#include <math.h>
#include <sys/mman.h>
#include "lazy_type.h"
#include "program.h"
#include "scope.h"
#include "ast_alloc.h"
#include "source_file.h"
#include "ty/graph.h"
#include "ty/atom_table.h"

// Arena

#define ARENA_CHUNK_SIZE (8 * 1024 * 1024)

// Faults the next MB of a chunk in with one call rather than a trap per 4 KB page (vscode: names -4%, check -2.5%).
// Chunks are bumped through in order, so those pages are about to be written anyway
[[gnu::noinline]] static void arena_populate(ArenaChunk* chunk) {
    uintptr_t from = ((uintptr_t)chunk->data + chunk->populated) & ~(uintptr_t)4095;
    size_t to = chunk->used + ((size_t)1 << 20) < chunk->cap ? chunk->used + ((size_t)1 << 20) : chunk->cap;
    uintptr_t end = ((uintptr_t)chunk->data + to) & ~(uintptr_t)4095;
    if (end > from) madvise((void*)from, end - from, MADV_POPULATE_WRITE);
    chunk->populated = to;
}

#ifdef TTTT_ARENA_SITES
// Arena bytes and types by the code that allocates them, for scripts/arenasites.sh (a build with -DTTTT_ARENA_SITES; one
// table for the process, so run it with --threads 1). Sites are printed relative to arena_alloc()
#define ARENA_SITES 8192
static void* arena_sites[2][ARENA_SITES];
static u64 arena_site_bytes[2][ARENA_SITES];

static void arena_site_count(u32 table, void* site, size_t size) {
    u32 h = (u32)(((uintptr_t)site * 0x9e3779b97f4a7c15ull) >> 40) & (ARENA_SITES - 1);
    while (arena_sites[table][h] && arena_sites[table][h] != site) h = (h + 1) & (ARENA_SITES - 1);
    arena_sites[table][h] = site;
    arena_site_bytes[table][h] += size;
}

// The latest allocations, so that arena_pop_last() takes a popped one off its site's bytes (scope_release() pops most
// of the scopes the resolve walk makes)
#define ARENA_RECENT 64
static struct { void* ptr; void* site; } arena_recent[ARENA_RECENT];
static u32 arena_recent_next;

// Interned key bytes and entries by the key's first word (its KEY_* kind)
static u64 key_kind_bytes[256], key_kind_count[256];

void arena_sites_print(void) {
    for (u32 kind = 0; kind < 256; kind++) {
        if (key_kind_count[kind]) eprintln("key-kind ", kind, " ", key_kind_bytes[kind], " ", key_kind_count[kind]);
    }
    for (u32 table = 0; table < 2; table++) {
        for (u32 i = 0; i < ARENA_SITES; i++) {
            if (!arena_sites[table][i]) continue;
            eprintln(table ? "type-site " : "arena-site ", (i64)((uintptr_t)arena_sites[table][i] - (uintptr_t)&arena_alloc),
                " ", arena_site_bytes[table][i]);
        }
    }
}
#endif

// An Arena.ast_backed's allocation, out of the thread's current materialization (plan 78 stage 2). A chunked one's
// chunks are 1 KB, then twice the last one up to 16 KB (the scope arena: what a body keeps of its scopes is small),
// never mapped or populated of their own
[[gnu::noinline]] static void* arena_alloc_materialized(Arena* me, size_t size) {
    if (my ast_backed == ARENA_AST_DIRECT) return ast_materialize_alloc(size, false);
    size_t cap = my head ? my head->cap * 2 : 1024 - sizeof(ArenaChunk);
    if (cap > ((size_t)16 << 10)) cap = (size_t)16 << 10;
    if (cap < size) cap = size;
    ArenaChunk* chunk = ast_materialize_alloc(sizeof(ArenaChunk) + cap, true);
    *chunk = (ArenaChunk){ .next = my head, .used = size, .cap = cap, .populated = cap };
    my head = chunk;
    return chunk->data;
}

// The allocation itself (`size` rounded up to 8), counted in Arena.total by the caller
static void* arena_alloc_chunk(Arena* me, size_t size) {
    if (!my head || my head->used + size > my head->cap) {
        if (my ast_backed) return arena_alloc_materialized(me, size);
        size_t cap = size > ARENA_CHUNK_SIZE ? size : ARENA_CHUNK_SIZE;
        // Straight from mmap: the kernel's pages are zero already (calloc() would memset them), and the ones the
        // arena never reaches are never touched
        size_t mapped = sizeof(ArenaChunk) + cap;
        void* memory = mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        panic_if(memory == MAP_FAILED, "out of memory");
        // Transparent huge pages while the system has them free: a few hundred MB of types page faulted 4 KB at a
        // time is a tenth of a big project's time
        bool huge = huge_pages_take(mapped);
        if (huge) madvise(memory, mapped, MADV_HUGEPAGE);
        ArenaChunk* chunk = memory;
        chunk->huge = huge;
        chunk->cap = cap;
        chunk->mapped = mapped;
        if (my head && size > ARENA_CHUNK_SIZE) {
            // A big one-off allocation, keep filling the current chunk
            chunk->next = my head->next;
            my head->next = chunk;
        } else {
            chunk->next = my head;
            my head = chunk;
        }
        chunk->used = size;
        if (chunk == my head) arena_populate(chunk);
        return chunk->data;
    }
    void* ret = my head->data + my head->used;
    my head->used += size;
    if (my head->used > my head->populated) arena_populate(my head);
    return ret;
}

void* arena_alloc_slow(Arena* me, size_t size) {
    my total += size;
    return arena_alloc_chunk(me, size);
}

#if defined(TTTT_ARENA_SITES) || defined(TTTT_SURVIVORS)
void* arena_alloc(Arena* me, size_t size) {
    size = (size + 7) & ~(size_t)7;
    my total += size;
#ifdef TTTT_ARENA_SITES
    arena_site_count(0, __builtin_return_address(0), size);
#endif
    void* ret = arena_alloc_chunk(me, size);
#ifdef TTTT_ARENA_SITES
    arena_recent[arena_recent_next++ % ARENA_RECENT] = (typeof(arena_recent[0])){ ret, __builtin_return_address(0) };
#endif
#ifdef TTTT_SURVIVORS
    survivors_record(me, ret, size, __builtin_return_address(0));
#endif
    return ret;
}
#endif

bool arena_pop_last(Arena* me, void* ptr, size_t size) {
    size = (size + 7) & ~(size_t)7;
    if (!my head || my head->used < size || (char*)ptr != my head->data + my head->used - size) return false;
    memset(ptr, 0, size);
#ifdef TTTT_SURVIVORS
    survivors_unrecord(me, ptr);
#endif
#ifdef TTTT_ARENA_SITES
    for (u32 i = 0; i < ARENA_RECENT; i++) {
        if (arena_recent[i].ptr == ptr) {
            arena_site_count(0, arena_recent[i].site, -size);
            arena_recent[i].ptr = nullptr;
            break;
        }
    }
#endif
    my head->used -= size;
    my total -= size;
    return true;
}

void arena_drop(Arena* me) {
    bool ast_backed = my ast_backed;
    // (The materialization's memory: dropped with it)
    if (ast_backed) my head = nullptr;
    ArenaChunk* i = my head;
    while (i) {
        ArenaChunk* next = i->next;
        if (i->huge) huge_pages_give(i->mapped);
        munmap(i, i->mapped);
        i = next;
    }
    *me = (Arena){ .ast_backed = ast_backed };
}

// Intern table

#define INTERN_LONG_KEY 0xffffu

// The hash with the word count in the top 16 bits (see InternEntry)
static u64 hash_words(u64 const* words, u32 count) {
    // (A word at a time two operations, multiplying by an odd constant: keys differing in one word differ. Then
    // murmur3's finalizer: the product's high bits into the low ones table_index() reads)
    u64 h = 0x9e3779b97f4a7c15ull ^ count;
    for (u32 i = 0; i < count; i++) h = (h ^ words[i]) * 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 29;
    // (Bit 47 keeps it from being 0 without touching the bits that index the table)
    return (h & 0x00007fffffffffffull) | 1ull << 47 | (u64)(count < INTERN_LONG_KEY ? count : INTERN_LONG_KEY) << 48;
}

// Tables of any capacity: the slot where a hash starts probing, and the next one. Intern tables double; node links
// (cheap to probe: the key is in the entry) grow by half, a third fuller on average (growing intern tables by half cost
// zod 1.2% more instructions in probes and rehashes for a few MB either way)
static u32 table_index(u64 hash, u32 cap) {
    return (u32)(((u64)(u32)hash * cap) >> 32);
}

static u32 table_next(u32 i, u32 cap) {
    return i + 1 == cap ? 0 : i + 1;
}

static u32 table_grown(u32 cap, u32 initial) {
    return cap ? cap / 2 * 3 : initial;
}

// Probing from `start` (where the key's probe sequence starts, or a slot it has reached)
__attribute__((always_inline)) static inline InternEntry* intern_find_from(
    InternTable const* me, u64 hash, u64 const* words, u32 count, u32 start
) {
    // (The hash has the count: the same hash is the same count, but a long key's count is in its block)
    bool long_key = count >= INTERN_LONG_KEY;
    for (u32 i = start;; i = table_next(i, my cap)) {
        InternEntry* entry = &my entries[i];
        if (!entry->hash) return entry;
        if (entry->hash != hash) continue;
        u64 const* key = (u64 const*)(entry->block + 1);
        if (long_key && *key++ != count) continue;
        // (Keys are a few words: a loop beats memcmp()'s call)
        u32 k = 0;
        while (k < count && key[k] == words[k]) k++;
        if (k == count) return entry;
    }
}

__attribute__((always_inline)) static inline InternEntry* intern_find(InternTable const* me, u64 hash, u64 const* words, u32 count) {
    return my cap ? intern_find_from(me, hash, words, count, table_index(hash, my cap)) : nullptr;
}

static void intern_grow(InternTable* me) {
    InternTable bigger = { .cap = my cap ? my cap * 2 : 64, .len = my len, .shared = my shared };
    bigger.entries = zeroed_table(bigger.cap, sizeof(InternEntry));
    panic_if(!bigger.entries, "out of memory");
    for (u32 i = 0; i < my cap; i++) {
        InternEntry* entry = &my entries[i];
        if (entry->hash) {
            // (Keys are distinct: the first empty slot)
            u32 j = table_index(entry->hash, bigger.cap);
            while (bigger.entries[j].hash) j = table_next(j, bigger.cap);
            bigger.entries[j] = *entry;
        }
    }
    free(my entries);
    *me = bigger;
}

// Puts a new key at `entry` (empty)
static void const** intern_put(Checker* me, InternTable* table, InternEntry* entry, u64 hash, u64 const* words, u32 count) {
    bool long_key = count >= INTERN_LONG_KEY;
    u64* block = arena_new_n(&my arena, u64, 1 + long_key + count);
#ifdef TTTT_ARENA_SITES
    key_kind_bytes[count ? words[0] & 255 : 0] += sizeof(u64) * (1 + long_key + count);
    key_kind_count[count ? words[0] & 255 : 0]++;
#endif
    if (long_key) block[1] = count;
    memcpy(block + 1 + long_key, words, sizeof(u64) * count);
    entry->hash = hash;
    entry->block = (void const**)block;
    table->len++;
#ifdef TTTT_TAINT
    // (The hooks can intern: the entry may move, not the block)
    void const** taint_block = entry->block;
    taint_intern_put(table, hash, taint_block, count ? words[0] & 255 : 0);
    return taint_block;
#else
    void const** ret = entry->block;
    // (--shared-graph: whether its computation is clean, see graph.c)
#ifdef TTTT_GRAPH_TAINT
    if (table->shared) graph_taint_put(table, hash, ret, count ? (u32)(words[0] & 255) : 0);
#endif
    return ret;
#endif
}

// A private entry hit. (--shared-graph's taint doesn't count reads of a tainted private entry: a test on every hit
// cost the default path ~1% of its instructions. What a tainted memo entry feeds is published when its own computation
// saw no event, see graph.c)
static inline void const** intern_hit(InternTable const* table, void const** block) {
    (void)table;
    return block;
}

static bool intern_needs_growth(InternTable const* table) {
    return (table->len + 1) * 4 > table->cap * 3;
}

// --shared-graph: a key the checker's own table doesn't have, in the graph's (ty/graph.h). Null if none
[[gnu::noinline]] static void const** intern_shared_find(InternTable const* table, u64 hash, u64 const* words, u32 count) {
    InternEntry* entry = intern_find(table->shared, hash, words, count);
    if (!entry || !entry->hash) {
        // (Its value's computation may start here: graph_taint_put() takes the counter from then)
#ifdef TTTT_GRAPH_TAINT
        graph_taint_miss(table, hash);
#endif
        return nullptr;
    }
    graph_count_shared_hit(table, words, count);
    return entry->block;
}

// The returned slot is null if the key is new. It is in the key's arena block, so it stays valid (callers that intern
// while computing the value look it up again all the same, from when the slots were in the table).
void const** intern_slot(Checker* me, InternTable* table, u64 const* words, u32 count) {
    if (intern_needs_growth(table)) {
        intern_grow(table);
    }
    u64 hash = hash_words(words, count);
    InternEntry* entry = intern_find(table, hash, words, count);
    if (!entry->hash) {
        void const** shared = table->shared ? intern_shared_find(table, hash, words, count) : nullptr;
        return shared ? shared : intern_put(me, table, entry, hash, words, count);
    }
#ifdef TTTT_TAINT
    void const** taint_block = entry->block;
    taint_intern_hit(taint_block);
    return taint_block;
#else
    return intern_hit(table, entry->block);
#endif
}

void const* intern_lookup(InternTable const* table, u64 const* words, u32 count, InternMiss* miss) {
    u64 hash = hash_words(words, count);
    InternEntry* entry = intern_find(table, hash, words, count);
    *miss = (InternMiss){ .hash = hash, .cap = table->cap, .index = entry ? (u32)(entry - table->entries) : 0 };
    if ((!entry || !entry->hash) && table->shared) {
        void const** shared = intern_shared_find(table, hash, words, count);
        if (shared) return shared[0];
    }
#ifdef TTTT_TAINT
    void const** taint_block = entry && entry->hash ? entry->block : nullptr;
    if (taint_block) taint_intern_hit(taint_block);
    if (!taint_block || !taint_block[0]) taint_intern_miss(table, hash);
    return taint_block ? taint_block[0] : nullptr;
#else
    return entry && entry->hash ? intern_hit(table, entry->block)[0] : nullptr;
#endif
}

void const** intern_insert(Checker* me, InternTable* table, InternMiss const* miss, u64 const* words, u32 count) {
    // (Same table, nothing removed since: whatever was inserted meanwhile went into the key's probe sequence, so it's
    // probed from where the lookup stopped)
    u32 start = miss->index;
    if (miss->cap != table->cap || intern_needs_growth(table)) {
        if (intern_needs_growth(table)) intern_grow(table);
        start = table_index(miss->hash, table->cap);
    }
    InternEntry* entry = intern_find_from(table, miss->hash, words, count, start);
    if (!entry->hash) return intern_put(me, table, entry, miss->hash, words, count);
#ifdef TTTT_TAINT
    void const** taint_block = entry->block;
    taint_intern_hit(taint_block);
    return taint_block;
#else
    return intern_hit(table, entry->block);
#endif
}

// --shared-graph's publication (graph.c): whether a table has a private table's entry's key, and putting the entry (its
// block, which stays where it is) into one that doesn't
bool intern_table_has(InternTable const* table, InternEntry const* entry, u32 count) {
    u64 const* words = (u64 const*)(entry->block + 1) + (count >= INTERN_LONG_KEY);
    InternEntry const* found = intern_find(table, entry->hash, words, count);
    return found && found->hash;
}

// Grows the table so that `more` keys go in without growing again: publishing walks a private table in the order of
// its slots, which is the order of the keys' hashes, and growing on the way would cluster them (every key put past the
// growing run of the ones before it, quadratic: zod 30 s)
void intern_table_reserve(InternTable* table, u32 more) {
    while ((u64)(table->len + more + 1) * 4 > (u64)table->cap * 3) intern_grow(table);
}

static u32 link_index(u64 key, u32 cap);

void link_table_reserve(LinkTable* table, u32 more) {
    while ((u64)(table->len + more + 1) * 4 > (u64)table->cap * 3) {
        LinkTable bigger = { .cap = table_grown(table->cap, 1024), .len = table->len };
        bigger.entries = zeroed_table(bigger.cap, sizeof(LinkEntry));
        panic_if(!bigger.entries, "out of memory");
        for (u32 i = 0; i < table->cap; i++) {
            LinkEntry entry = table->entries[i];
            if (!entry.key) continue;
            u32 j = link_index(entry.key, bigger.cap);
            while (bigger.entries[j].key) j = table_next(j, bigger.cap);
            bigger.entries[j] = entry;
        }
        free(table->entries);
        *table = bigger;
    }
}

void intern_table_publish(InternTable* shared, InternEntry const* entry, u32 count) {
    (void)count;
    if (intern_needs_growth(shared)) intern_grow(shared);
    u32 i = table_index(entry->hash, shared->cap);
    while (shared->entries[i].hash) i = table_next(i, shared->cap);
    shared->entries[i] = *entry;
    shared->len++;
}

// The key's words in a block intern_slot() returned
static u64 const* intern_key_words(void const** block, u32 count) {
    return (u64 const*)block + 1 + (count >= INTERN_LONG_KEY);
}

// A list of types that is part of an interned key as pointers (`at` words in): the type's list can be the key's
// instead of a copy (drizzle 29, effect 34, vscode 44 MB of copies)
static TyList intern_key_list(void const** block, u32 count, u32 at, u32 len) {
    if (!len) return (TyList){0};
    return (TyList){ .data = (Ty const* const*)(intern_key_words(block, count) + at), .len = len };
}

void const* intern_get(InternTable const* table, u64 const* words, u32 count) {
#ifdef TTTT_TAINT
    u64 hash = hash_words(words, count);
    InternEntry* entry = intern_find(table, hash, words, count);
    void const** taint_block = entry && entry->hash ? entry->block : nullptr;
    if (taint_block) taint_intern_hit(taint_block);
    if (!taint_block || !taint_block[0]) taint_intern_miss(table, hash);
    return taint_block ? taint_block[0] : nullptr;
#else
    u64 hash = hash_words(words, count);
    InternEntry* entry = intern_find(table, hash, words, count);
    if (entry && entry->hash) return intern_hit(table, entry->block)[0];
    void const** shared = table->shared ? intern_shared_find(table, hash, words, count) : nullptr;
    return shared ? shared[0] : nullptr;
#endif
}

static u64 link_key(u32 kind, void const* subject) {
    // (User space addresses are below 2^47)
    return (u64)(kind + 1) << 56 | (u64)(uintptr_t)subject;
}

static u32 link_index(u64 key, u32 cap) {
    return table_index((key * 0x9e3779b97f4a7c15ull) >> 32, cap);
}

void const* ty_link_get(Checker const* me, u32 kind, void const* subject) {
    LinkTable const* table = &my node_links;
    if (!table->cap) return my graph ? graph_link_get(my graph, kind, subject) : nullptr;
    u64 key = link_key(kind, subject);
    for (u32 i = link_index(key, table->cap);; i = table_next(i, table->cap)) {
        LinkEntry const* entry = &table->entries[i];
        if (entry->key == key) return entry->value;
        if (!entry->key) return my graph ? graph_link_get(my graph, kind, subject) : nullptr;
    }
}

// The node link entries of a table (graph.c's publication): calls `each` with every (kind, subject, value)
void link_table_each(LinkTable const* table, void (*each)(void* arg, u32 kind, void const* subject, void const* value), void* arg) {
    for (u32 i = 0; i < table->cap; i++) {
        LinkEntry const* entry = &table->entries[i];
        if (entry->key) each(arg, (u32)(entry->key >> 56) - 1, (void const*)(uintptr_t)(entry->key & ((1ull << 56) - 1)), entry->value);
    }
}

// The shared table's lookup (graph.c)
void const* link_table_get(LinkTable const* table, u32 kind, void const* subject) {
    if (!table->cap) return nullptr;
    u64 key = link_key(kind, subject);
    for (u32 i = link_index(key, table->cap);; i = table_next(i, table->cap)) {
        LinkEntry const* entry = &table->entries[i];
        if (entry->key == key) return entry->value;
        if (!entry->key) return nullptr;
    }
}

static inline void link_set(LinkTable* table, u32 kind, void const* subject, void const* value);

void ty_link_set(Checker* me, u32 kind, void const* subject, void const* value) {
    link_set(&my node_links, kind, subject, value);
}

void link_table_set(LinkTable* table, u32 kind, void const* subject, void const* value) {
    link_set(table, kind, subject, value);
}

static inline void link_set(LinkTable* table, u32 kind, void const* subject, void const* value) {
    if ((table->len + 1) * 4 > table->cap * 3) {
        LinkTable bigger = { .cap = table_grown(table->cap, 1024), .len = table->len };
        bigger.entries = zeroed_table(bigger.cap, sizeof(LinkEntry));
        panic_if(!bigger.entries, "out of memory");
        for (u32 i = 0; i < table->cap; i++) {
            LinkEntry entry = table->entries[i];
            if (!entry.key) continue;
            u32 j = link_index(entry.key, bigger.cap);
            while (bigger.entries[j].key) j = table_next(j, bigger.cap);
            bigger.entries[j] = entry;
        }
        free(table->entries);
        *table = bigger;
    }
    u64 key = link_key(kind, subject);
    for (u32 i = link_index(key, table->cap);; i = table_next(i, table->cap)) {
        LinkEntry* entry = &table->entries[i];
        if (entry->key == key) {
            entry->value = value;
            return;
        }
        if (!entry->key) {
            *entry = (LinkEntry){ key, value };
            table->len++;
            return;
        }
    }
}

Atom const* checker_atom(Checker* me, csview sv) {
    return atom_table_get(my atom_table, name_cooked(sv)); // (an identifier's name: by its value)
}

// Types

#define TY_VARIANT_END(field) ((offsetof(Ty, field) + sizeof(((Ty*)0)->field) + 7) & ~(size_t)7)
static uint8_t const ty_kind_sizes[] = {
    [TY_INTRINSIC] = TY_VARIANT_END(intrinsic),
    [TY_LITERAL] = TY_VARIANT_END(literal),
    [TY_TYPE_PARAM] = TY_VARIANT_END(param),
    [TY_DECLARED] = TY_VARIANT_END(declared),
    [TY_REFERENCE] = TY_VARIANT_END(reference),
    [TY_ANONYMOUS] = TY_VARIANT_END(anonymous),
    [TY_UNION] = TY_VARIANT_END(composite),
    [TY_INTERSECTION] = TY_VARIANT_END(composite),
    [TY_KEYOF] = TY_VARIANT_END(keyof),
    [TY_INDEXED] = TY_VARIANT_END(indexed),
    [TY_CONDITIONAL] = TY_VARIANT_END(conditional),
    [TY_SUBSTITUTION] = TY_VARIANT_END(substitution),
    [TY_TEMPLATE_LITERAL] = TY_VARIANT_END(template_literal),
    [TY_STRING_MAPPING] = TY_VARIANT_END(string_mapping),
    [TY_EVOLVING_ARRAY] = TY_VARIANT_END(evolving_array),
};
#undef TY_VARIANT_END
static_assert(c_arraylen(ty_kind_sizes) == TY_EVOLVING_ARRAY + 1);

size_t ty_size_of_kind(TyKind kind) {
    check(kind < c_arraylen(ty_kind_sizes) && ty_kind_sizes[kind]);
    return ty_kind_sizes[kind];
}

#ifdef TTTT_TY_POISON
// Every poisoned Ty, to check at checker_drop() that nothing wrote past its kind's variant either
#include <pthread.h>
static pthread_mutex_t poisoned_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { Checker const* checker; Ty const* ty; }* poisoned;
static size_t poisoned_len, poisoned_cap;

static void poison_record(Checker const* checker, Ty const* ty) {
    pthread_mutex_lock(&poisoned_lock);
    if (poisoned_len == poisoned_cap) {
        poisoned_cap = poisoned_cap ? poisoned_cap * 2 : 4096;
        poisoned = realloc(poisoned, sizeof(*poisoned) * poisoned_cap);
        panic_if(!poisoned, "out of memory");
    }
    poisoned[poisoned_len].checker = checker;
    poisoned[poisoned_len++].ty = ty;
    pthread_mutex_unlock(&poisoned_lock);
}

// Panics with the first Ty of the checker whose tail isn't all 0xa5 anymore, forgets the checker's
static void poison_verify(Checker const* checker) {
    pthread_mutex_lock(&poisoned_lock);
    size_t kept = 0;
    for (size_t i = 0; i < poisoned_len; i++) {
        if (poisoned[i].checker != checker) {
            poisoned[kept++] = poisoned[i];
            continue;
        }
        Ty const* ty = poisoned[i].ty;
        unsigned char const* bytes = (unsigned char const*)ty;
        for (size_t at = ty_size_of_kind((TyKind)ty->kind); at < sizeof(Ty); at++) {
            panic_if(bytes[at] != 0xa5, "Ty ", (i64)ty->id, " of kind ", (i64)ty->kind, ": byte ", (i64)at,
                " past its variant (", (i64)ty_size_of_kind((TyKind)ty->kind), " bytes) was written");
        }
    }
    poisoned_len = kept;
    pthread_mutex_unlock(&poisoned_lock);
}
#endif

#ifdef TTTT_ARENA_SITES
__attribute__((noinline))
#endif
Ty* ty_alloc(Checker* me, TyKind kind, u32 flags) {
#ifdef TTTT_ARENA_SITES
    arena_site_count(1, __builtin_return_address(0), ty_size_of_kind(kind));
#endif
#ifdef TTTT_TY_POISON
    // (Debugging: what reads past the kind's variant reads garbage)
    Ty* ret = arena_new(&my arena, Ty);
    memset((char*)ret + ty_size_of_kind(kind), 0xa5, sizeof(Ty) - ty_size_of_kind(kind));
    poison_record(me, ret);
#else
    Ty* ret = arena_alloc(&my arena, ty_size_of_kind(kind));
#endif
    ret->id = my next_type_id++;
    lazy_slots_add_type(&my lazy, ret);
    ret->kind = kind;
    ret->flags = flags;
    return ret;
}

TyList ty_list_copy(Checker* me, Ty const* const* data, u32 len) {
    if (!len) return (TyList){0};
    Ty const** copy = arena_new_n(&my arena, Ty const*, len);
    memcpy(copy, data, sizeof(Ty const*) * len);
    return (TyList){ .data = copy, .len = len };
}

static u32 ty_list_flags(TyList list) {
    u32 flags = 0;
    for (u32 i = 0; i < list.len; i++) flags |= list.data[i]->flags;
    return flags;
}

static Ty const* make_intrinsic(Checker* me, char const* name, u32 flags) {
    Ty* ret = ty_alloc(me, TY_INTRINSIC, flags);
    ret->intrinsic.name = csview_from(name);
    return ret;
}

Checker* checker_new(Program* program) {
    Checker* me = calloc(1, sizeof(Checker));
    panic_if(!me, "out of memory");
    my program = program;
    my binder = program_get_binder(program);
    my errs = program_errors(program);
    my next_type_id = 1;
    my atom_table = program_atoms(program);
    // (--shared-graph: the graph's first checker made the intrinsics, the rest share them)
    TyGraph* graph = program_graph(program);
    if (graph) {
        graph_adopt(graph, me);
        return me;
    }
#ifdef TTTT_TAINT
    taint_checker_new(me);
#endif
#ifdef TTTT_SURVIVORS
    survivors_reset(&my arena);
#endif
    my intrinsics[TY_ANY] = make_intrinsic(me, "any", TYF_ANY);
    my intrinsics[TY_AUTO] = make_intrinsic(me, "any", TYF_ANY | TYF_NON_INFERRABLE);
    my intrinsics[TY_WILDCARD] = make_intrinsic(me, "any", TYF_ANY);
    my intrinsics[TY_ERROR] = make_intrinsic(me, "any", TYF_ANY | TYF_ERROR);
    my intrinsics[TY_CIRCULAR] = make_intrinsic(me, "any", TYF_ANY);
    my intrinsics[TY_UNKNOWN_] = make_intrinsic(me, "unknown", TYF_UNKNOWN);
    my intrinsics[TY_UNDEFINED] = make_intrinsic(me, "undefined", TYF_UNDEFINED);
    my intrinsics[TY_UNDEFINED_WIDENING] = my intrinsics[TY_UNDEFINED];
    if (!compiler_options()->strict_null_checks) {
        Ty* widening = (Ty*)make_intrinsic(me, "undefined", TYF_UNDEFINED);
        widening->xflags = TYX_CONTAINS_WIDENING | TYX_WIDENS;
        my intrinsics[TY_UNDEFINED_WIDENING] = widening;
    }
    my intrinsics[TY_MISSING] = make_intrinsic(me, "undefined", TYF_UNDEFINED);
    my intrinsics[TY_OPTIONAL] = make_intrinsic(me, "undefined", TYF_UNDEFINED);
    my intrinsics[TY_NULL] = make_intrinsic(me, "null", TYF_NULL);
    my intrinsics[TY_NULL_WIDENING] = my intrinsics[TY_NULL];
    if (!compiler_options()->strict_null_checks) {
        Ty* widening = (Ty*)make_intrinsic(me, "null", TYF_NULL);
        widening->xflags = TYX_CONTAINS_WIDENING | TYX_WIDENS;
        my intrinsics[TY_NULL_WIDENING] = widening;
    }
    my intrinsics[TY_STRING] = make_intrinsic(me, "string", TYF_STRING);
    my intrinsics[TY_NUMBER] = make_intrinsic(me, "number", TYF_NUMBER);
    my intrinsics[TY_BIGINT] = make_intrinsic(me, "bigint", TYF_BIGINT);
    Value false_value = { .tag = VALUE_BOOLEAN, .boolean = false };
    Value true_value = { .tag = VALUE_BOOLEAN, .boolean = true };
    my intrinsics[TY_FALSE] = ty_literal(me, &false_value);
    my intrinsics[TY_TRUE] = ty_literal(me, &true_value);
    my intrinsics[TY_BOOLEAN] = ty_union(me, &my intrinsics[TY_FALSE], 2, nullptr, (TyList){0});
    my intrinsics[TY_ES_SYMBOL] = make_intrinsic(me, "symbol", TYF_ES_SYMBOL);
    my intrinsics[TY_VOID] = make_intrinsic(me, "void", TYF_VOID);
    my intrinsics[TY_NEVER] = make_intrinsic(me, "never", TYF_NEVER);
    my intrinsics[TY_SILENT_NEVER] = make_intrinsic(me, "never", TYF_NEVER | TYF_NON_INFERRABLE);
    my intrinsics[TY_IMPLICIT_NEVER] = make_intrinsic(me, "never", TYF_NEVER);
    my intrinsics[TY_UNREACHABLE_NEVER] = make_intrinsic(me, "never", TYF_NEVER);
    my intrinsics[TY_NON_PRIMITIVE] = make_intrinsic(me, "object", TYF_NON_PRIMITIVE);
    {
        // tsc's zeroBigIntType comes first: compareTypes orders bigint literals by type id (`0n | 1n`)
        Value zero_bigint = value_bigint("0", 1, false);
        ty_literal(me, &zero_bigint);
    }
    {
        // tsc's typeofType (createTypeofType) is made with the checker's global types, before any written literal
        static char const* const names[] = { "string", "number", "bigint", "boolean", "symbol", "undefined", "object", "function" };
        Ty const* types[c_arraylen(names)];
        for (u32 i = 0; i < c_arraylen(names); i++) types[i] = ty_string_literal(me, csview_from(names[i]));
        my typeof_type = ty_union(me, types, c_arraylen(names), nullptr, (TyList){0});
    }
    my atom_constructor = checker_atom(me, c_sv("constructor"));
    my atom_prototype = checker_atom(me, c_sv("prototype"));
    my atom_bind = checker_atom(me, c_sv("bind"));
    return me;
}

// The atom of an index's decimal name (a tuple element's, an array pattern's): tuples ask for millions on type-fest,
// formatting and hashing each again
Atom const* ty_index_atom(Checker* me, u32 index) {
    if (index >= 4096) {
        char digits[16];
        int n = snprintf(digits, sizeof digits, "%u", index);
        return checker_atom(me, csview_from_n(digits, n));
    }
    if (index >= my index_atoms_len) {
        u32 len = my index_atoms_len ? my index_atoms_len : 16;
        while (len <= index) len *= 2;
        my index_atoms = realloc(my index_atoms, sizeof(Atom const*) * len);
        panic_if(!my index_atoms, "out of memory");
        memset(my index_atoms + my index_atoms_len, 0, sizeof(Atom const*) * (len - my index_atoms_len));
        my index_atoms_len = len;
    }
    if (!my index_atoms[index]) {
        char digits[16];
        int n = snprintf(digits, sizeof digits, "%u", index);
        my index_atoms[index] = checker_atom(me, csview_from_n(digits, n));
    }
    return my index_atoms[index];
}

thread_local Checker* current_checker;

void checker_make_current(Checker* me) {
    // (--shared-graph: the atoms, the type ids and the lib singletons go to the current checker)
    TyGraph* graph = me ? my graph : current_checker ? current_checker->graph : nullptr;
    if (graph) graph_enter(graph, me);
    current_checker = me;
    current_lazy_slots = me ? &my lazy : nullptr;
}

void checker_defer(Checker* me, DeferredCheckKind kind, void const* node) {
    if (my deferred_count == my deferred_cap) {
        my deferred_cap = my deferred_cap ? my deferred_cap * 2 : 64;
        my deferred = realloc(my deferred, sizeof(DeferredCheck) * my deferred_cap);
        panic_if(!my deferred, "out of memory");
    }
    my deferred[my deferred_count++] = (DeferredCheck){ kind, node };
}

void checker_defer_function_body(Checker* me, FunctionImpl const* function) {
    if (ty_link_get(me, KEY_DEFERRED_BODY, function)) return;
    ty_link_set(me, KEY_DEFERRED_BODY, function, function);
    checker_defer(me, DEFERRED_FUNCTION_BODY, function);
}

void checker_drop(Checker* me) {
    if (!me) return;
#ifdef TTTT_TAINT
    taint_checker_drop(me);
#endif
#ifdef TTTT_TY_POISON
    poison_verify(me);
#endif
    free(my deferred);
    free(my painted_kept);
    // Not current while dropping: memos contain LazyTypes of their own, which mustn't look for slots in here anymore
    if (current_checker == me) checker_make_current(nullptr);
    if (my graph) graph_release(my graph, me);
    // (The materializations of deferred bodies it held may go now: nothing else of it points into them, plan 78)
    deferred_bodies_release(my held, my held_count);
    free(my held);
    free(my held_files);
    free(my cross_files);
    lazy_slots_drop(&my lazy);
    free(my index_atoms);
    free(my index_atoms_by_name);
    free(my types.entries);
    free(my links.entries);
    free(my node_links.entries);
    free(my intersection_props.entries);
    for (u32 i = 0; i < TY_RELATION_COUNT; i++) relation_cache_drop(&my relations[i]);
    ty_active_mappers_drop(me);
    free(my flow_loops);
    free(my resolving);
    free(my barriers);
    free(my members_in_flight);
    free(my flow_shared);
    for (u32 i = 0; i < my call_errors_len; i++) error_ctx_drop(my call_errors[i]);
    free(my call_errors);
    arena_drop(&my arena);
    free(me);
}

u64 checker_memory(Checker const* me) {
    u64 bytes = my arena.total;
    bytes += ((u64)my types.cap + my intersection_props.cap + my links.cap) * sizeof(InternEntry);
    bytes += (u64)my node_links.cap * sizeof(LinkEntry);
    for (u32 i = 0; i < TY_RELATION_COUNT; i++) bytes += (u64)my relations[i].cap * (sizeof(u64) + 1);
    // (lazy_slots_add_type()'s chunks)
    u64 chunk = sizeof(Ty const*) << LAZY_TYPE_CHUNK_BITS;
    return bytes + ((u64)my next_type_id + chunk / sizeof(Ty const*) - 1) / (chunk / sizeof(Ty const*)) * chunk;
}

// A bigint keyed by its decimal text, which the type keeps (the value's text may not live as long): an atom's.
// Out of line: literal_type() is hot and bigints are rare
[[gnu::noinline]] static Value bigint_normalized(Checker* me, Value const* value) {
    cstr text = cstr_init();
    bigint_append_decimal(&text, value->bigint);
    csview digits = cstr_sv(&text);
    bool negative = digits.size && digits.buf[0] == '-';
    if (negative) digits = csview_slice(digits, 1, digits.size);
    Atom const* atom = checker_atom(me, digits);
    cstr_drop(&text);
    return value_bigint(atom->str, atom->len, negative);
}

static Ty const* literal_type(Checker* me, Value const* value, Symbol const* enum_decl, Symbol const* enum_member) {
    // An enum's literal types are per (value, enum): members of one enum with the same value share a type, as in tsc
    u64 key[4] = { KEY_LITERAL, value->tag, 0, (u64)(uintptr_t)enum_decl };
    Atom const* string = nullptr;
    u32 flags = 0;
    Value normalized;
    switch (value->tag) {
        case VALUE_UNDEFINED: return my intrinsics[TY_UNDEFINED];
        case VALUE_NULL: return my intrinsics[TY_NULL];
        case VALUE_BOOLEAN: key[2] = value->boolean; flags = TYF_BOOLEAN_LITERAL; break;
        case VALUE_NUMBER: {
            // tsc keys number literal types by their string: -0 is 0, and every NaN is one type
            double number = value->number == 0 ? 0.0 : isnan(value->number) ? NAN : value->number;
            normalized = (Value){ .tag = VALUE_NUMBER, .number = number };
            value = &normalized;
            memcpy(&key[2], &number, sizeof(double));
            flags = TYF_NUMBER_LITERAL;
            break;
        }
        case VALUE_BIGINT:
            normalized = bigint_normalized(me, value);
            value = &normalized;
            key[2] = (u64)(uintptr_t)normalized.bigint.text << 1 | normalized.bigint.negative;
            flags = TYF_BIGINT_LITERAL;
            break;
        case VALUE_STRING:
            string = checker_atom(me, cstr_sv(&value->string));
            key[2] = (u64)(uintptr_t)string;
            flags = TYF_STRING_LITERAL;
            break;
        default:
            // Objects, arrays, functions and symbols aren't literal types; the legacy checker shouldn't produce them
            return my intrinsics[TY_ERROR];
    }
    if (enum_decl) flags |= TYF_ENUM_LITERAL;
    void const** slot = intern_slot(me, &my types, key, 4);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_LITERAL, flags);
        ret->literal.value = *value;
        if (string) {
            // Borrow the atom's storage, so the literal doesn't depend on the lifetime of the Value it came from
            ret->literal.value.string = cstr_null;
        }
        ret->literal.string = string;
        ret->literal.enum_decl = enum_decl;
        ret->literal.enum_member = enum_member;
        *slot = ret;
    }
    return *slot;
}

// The type of a type name that didn't resolve: the error type, but it prints as the name with its type arguments'
// types, one per (name, arguments), like tsc's errorTypes for an unresolved symbol (getTypeFromTypeAliasReference)
Ty const* ty_unresolved(Checker* me, csview name, TyList args) {
    Atom const* atom = checker_atom(me, name);
    u64 stack_key[16];
    u32 count = 3 + args.len;
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    panic_if(!key, "out of memory");
    key[0] = KEY_LITERAL;
    key[1] = UINT32_MAX - 1;
    key[2] = (u64)(uintptr_t)atom;
    for (u32 i = 0; i < args.len; i++) key[3 + i] = args.data[i]->id;
    void const** slot = intern_slot(me, &my types, key, count);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_INTRINSIC, TYF_ANY | TYF_ERROR);
        ret->intrinsic.name = atom_sv(atom);
        ret->intrinsic.unresolved_args = ty_list_copy(me, args.data, args.len);
        *slot = ret;
    }
    if (key != stack_key) free(key);
    return *slot;
}

Ty const* ty_literal(Checker* me, Value const* value) {
    return literal_type(me, value, nullptr, nullptr);
}

Ty const* ty_enum_literal(Checker* me, Value const* value, Symbol const* enum_decl, Symbol const* member) {
    return literal_type(me, value, enum_decl, member);
}

// The type of an enum member whose value isn't a compile time constant (tsc's computed enum type, TypeFlags.Enum).
// Also the type of an enum without members, with the enum as its own member
Ty const* ty_enum_computed_member(Checker* me, Symbol const* enum_decl, Symbol const* member) {
    u64 key[4] = { KEY_LITERAL, UINT32_MAX, (u64)(uintptr_t)member, (u64)(uintptr_t)enum_decl };
    void const** slot = intern_slot(me, &my types, key, 4);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_LITERAL, TYF_ENUM_LITERAL | TYF_ENUM);
        ret->literal.value = (Value){ .tag = VALUE_NUMBER, .number = NAN };
        ret->literal.enum_decl = enum_decl;
        ret->literal.enum_member = member;
        *slot = ret;
    }
    return *slot;
}

Ty const* ty_unique_symbol(Checker* me, UniqueSymbolDecl const* decl) {
    if (!decl->key) return my intrinsics[TY_ES_SYMBOL];
    u64 key[2] = { KEY_UNIQUE_SYMBOL, (u64)(uintptr_t)decl->key };
    void const** slot = intern_slot(me, &my types, key, 2);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_LITERAL, TYF_UNIQUE_ES_SYMBOL);
        ret->literal.value = (Value){ .tag = VALUE_UNDEFINED };
        ret->literal.unique = decl;
        csview name = span_valid(decl->member) ? span_sv(decl->member) : decl->owner ? decl->owner->name : c_sv("");
        cstr escaped = cstr_d("__@", name, "@", ret->id);
        ret->literal.string = checker_atom(me, cstr_sv(&escaped));
        cstr_drop(&escaped);
        *slot = ret;
        u64 name_key[2] = { KEY_UNIQUE_SYMBOL_NAME, (u64)(uintptr_t)ret->literal.string };
        *intern_slot(me, &my links, name_key, 2) = ret;
    }
    return *slot;
}

Ty const* ty_widened_unique_symbol(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_UNIQUE_ES_SYMBOL) return my intrinsics[TY_ES_SYMBOL];
    if (ty->kind == TY_UNION) {
        bool any = false;
        for (u32 i = 0; i < ty->composite.types.len; i++) any |= (ty->composite.types.data[i]->flags & TYF_UNIQUE_ES_SYMBOL) != 0;
        if (!any) return ty;
        TyBuf buf = {0};
        for (u32 i = 0; i < ty->composite.types.len; i++) ty_buf_push(&buf, ty_widened_unique_symbol(me, ty->composite.types.data[i]));
        Ty const* ret = ty_union(me, buf.data, buf.len, nullptr, (TyList){0});
        ty_buf_drop(&buf);
        return ret;
    }
    return ty;
}

Ty const* ty_fresh_literal(Checker* me, Ty const* ty) {
    if (ty->kind != TY_LITERAL || (ty->flags & (TYF_FRESH_LITERAL | TYF_UNIQUE_ES_SYMBOL))) return ty;
    Ty const* fresh = ty_link_get(me, KEY_FRESH_LITERAL, ty);
    if (!fresh) {
        Ty* ret = ty_alloc(me, TY_LITERAL, ty->flags | TYF_FRESH_LITERAL);
        ret->literal = ty->literal;
        ret->literal.regular = ty;
        ty_link_set(me, KEY_FRESH_LITERAL, ty, ret);
        fresh = ret;
    }
    return fresh;
}

Ty const* ty_regular(Checker* me, Ty const* ty) {
    if (!(ty->flags & TYF_FRESH_LITERAL)) return ty;
    if (ty->kind == TY_LITERAL) return ty->literal.regular;
    check(ty->kind == TY_UNION);
    u64 key[2] = { KEY_REGULAR_UNION, (u64)(uintptr_t)ty };
    void const* known = intern_get(&my types, key, 2);
    if (known) return known;
    TyList members = ty->composite.types;
    TyBuf buf = {0};
    for (u32 i = 0; i < members.len; i++) ty_buf_push(&buf, ty_regular(me, members.data[i]));
    Ty const* ret = ty_union(me, buf.data, buf.len, nullptr, (TyList){0});
    ty_buf_drop(&buf);
    *intern_slot(me, &my types, key, 2) = ret;
    return ret;
}

static bool sorted_contains(TyBuf const* buf, u32 len, Ty const* t) {
    u32 lo = 0, hi = len;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        int c = ty_compare(buf->data[mid], t);
        if (c == 0) return true;
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return false;
}

Ty const* ty_string_literal(Checker* me, csview sv) {
    Value value = { .tag = VALUE_STRING, .string = cstr_from_sv(name_cooked(sv)) }; // (maybe an identifier's name)
    Ty const* ret = ty_literal(me, &value);
    value_drop(&value);
    return ret;
}

// Unions and intersections

// Sorts types in union order (ty_compare(); stable). The input is usually a few sorted runs back to back (the members
// of the unions being combined, plus a few more), so this is a natural merge sort: it merges the runs that are there
// instead of sorting from scratch
static void sort_by_id(Ty const** data, u32 len) {
    if (len <= 8) {
        for (u32 i = 1; i < len; i++) {
            Ty const* t = data[i];
            u32 j = i;
            for (; j && ty_compare(data[j - 1], t) > 0; j--) data[j] = data[j - 1];
            data[j] = t;
        }
        return;
    }
    // Run boundaries: run k is [bounds[k], bounds[k + 1])
    u32* bounds = malloc(sizeof(u32) * (len + 1));
    Ty const** tmp = malloc(sizeof(Ty const*) * len);
    panic_if(!bounds || !tmp, "out of memory");
    u32 runs = 0;
    bounds[runs++] = 0;
    for (u32 i = 1; i < len; i++) if (ty_compare(data[i - 1], data[i]) > 0) bounds[runs++] = i;
    bounds[runs] = len;
    Ty const** src = data;
    Ty const** dst = tmp;
    while (runs > 1) {
        u32 merged = 0;
        for (u32 k = 0; k < runs; k += 2) {
            u32 lo = bounds[k], mid = bounds[k + 1], hi = k + 2 <= runs ? bounds[k + 2] : mid;
            u32 a = lo, b = mid, o = lo;
            while (a < mid && b < hi) dst[o++] = ty_compare(src[b], src[a]) < 0 ? src[b++] : src[a++];
            while (a < mid) dst[o++] = src[a++];
            while (b < hi) dst[o++] = src[b++];
            bounds[merged++] = lo;
        }
        bounds[merged] = len;
        runs = merged;
        Ty const** swap = src;
        src = dst;
        dst = swap;
    }
    if (src != data) memcpy(data, src, sizeof(Ty const*) * len);
    free(tmp);
    free(bounds);
}

static void ty_buf_add_flattened(TyBuf* me, TyKind kind, Ty const* const* types, u32 len) {
    for (u32 i = 0; i < len; i++) {
        // (The same type again right after itself adds nothing: tsc's addTypesToUnion skips it, lastType. A union
        // over a thousand keys whose properties are one big union is a thousand copies of it)
        if (i && types[i] == types[i - 1]) continue;
        if (types[i]->kind == kind) {
            ty_buf_add_flattened(me, kind, types[i]->composite.types.data, types[i]->composite.types.len);
        } else {
            ty_buf_push(me, types[i]);
        }
    }
}

// What a union is built as (Ty.composite.origin) before it is interned: a union or intersection of `types`, or
// (TY_KEYOF) `keyof types[0]`. The origin Ty is only made when the union is new
typedef struct {
    TyKind kind;
    Ty const* const* types;
    u32 len;
} OriginSpec;

// `types`: the spec's types, the interned key's (see intern_key_list())
static Ty const* make_origin(Checker* me, OriginSpec const* spec, TyList types) {
#ifdef TTTT_TY_POISON
    Ty* ret = arena_new(&my arena, Ty);
    memset((char*)ret + ty_size_of_kind(spec->kind), 0xa5, sizeof(Ty) - ty_size_of_kind(spec->kind));
    ret->kind = spec->kind; // (For poison_verify(), which may see it before the kind is set below)
    poison_record(me, ret);
#else
    Ty* ret = arena_alloc(&my arena, ty_size_of_kind(spec->kind));
#endif
    ret->kind = spec->kind;
    if (spec->kind == TY_KEYOF) {
        ret->keyof.operand = spec->types[0];
    } else {
        ret->flags = spec->kind == TY_UNION ? TYF_UNION : TYF_INTERSECTION;
        ret->composite.types = types;
    }
    return ret;
}

static Ty const* intern_composite_origin(
    Checker* me, TyKind kind, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args,
    OriginSpec const* origin
) {
    // tsc's getUnionTypeFromSortedList: an origin is part of the identity. It goes last, after a marker no type id
    // can equal, so keys with and without one can't collide
    u32 count = 3 + alias_args.len + len + (origin ? 1 + origin->len : 0);
    u64 stack_key[64];
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    key[0] = kind == TY_UNION ? KEY_UNION : KEY_INTERSECTION;
    key[1] = (u64)(uintptr_t)alias;
    key[2] = alias_args.len;
    // (The lists by pointer, which are as unique as ids: the type's lists are the key's, see intern_key_list(). No
    // pointer equals the marker, they're 8-byte aligned)
    for (u32 i = 0; i < alias_args.len; i++) key[3 + i] = (u64)(uintptr_t)alias_args.data[i];
    for (u32 i = 0; i < len; i++) key[3 + alias_args.len + i] = (u64)(uintptr_t)types[i];
    if (origin) {
        u64* okey = key + 3 + alias_args.len + len;
        okey[0] = ((u64)origin->kind << 32) | UINT32_MAX;
        for (u32 i = 0; i < origin->len; i++) okey[1 + i] = (u64)(uintptr_t)origin->types[i];
    }
    void const** slot = intern_slot(me, &my types, key, count);
    if (key != stack_key) free(key);
    if (!*slot) {
        u32 flags = 0, xflags = 0;
        for (u32 i = 0; i < len; i++) {
            flags |= types[i]->flags;
            xflags |= types[i]->xflags;
        }
        u32 member_flags = flags;
        flags = (flags | ty_list_flags(alias_args)) & TYF_COULD_CONTAIN_TYPE_VARIABLES;
        flags |= kind == TY_UNION ? TYF_UNION | (member_flags & (TYF_FRESH_LITERAL | TYF_FRESH_OBJECT)) : TYF_INTERSECTION;
        flags |= member_flags & TYF_NON_INFERRABLE;
        // Any two boolean literals, fresh or not, are `boolean` (tsc's getUnionTypeFromSortedList)
        if (kind == TY_UNION && len == 2 && (types[0]->flags & types[1]->flags & TYF_BOOLEAN_LITERAL)) {
            flags |= TYF_BOOLEAN;
        }
        if (kind == TY_UNION && (member_flags & TYF_UNDEFINED) && (member_flags & TYF_NULL)) xflags |= TYX_UNDEFINED_AND_NULL;
        if (kind == TY_UNION) {
            // (Only intersections that may reduce: two object members without type variables, or one and a type
            // literal with them or a type variable, see never_reduced_prop(): `(A | L1 | P1) & V` with V constrained to
            // `A | L1 | L2` loses `P1 & V`)
            for (u32 i = 0; i < len && !(xflags & TYX_CONTAINS_INTERSECTIONS); i++) {
                if (types[i]->kind != TY_INTERSECTION) continue;
                u32 objects = 0, literals = 0;
                TyList parts = types[i]->composite.types;
                for (u32 j = 0; j < parts.len; j++) {
                    objects += (parts.data[j]->flags & TYF_OBJECT) && !(parts.data[j]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES);
                    literals += (parts.data[j]->kind == TY_ANONYMOUS && (parts.data[j]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) ||
                        parts.data[j]->kind == TY_TYPE_PARAM || parts.data[j]->kind == TY_INDEXED;
                }
                if (objects >= 2 || (objects && literals)) xflags |= TYX_CONTAINS_INTERSECTIONS;
            }
        }
        Ty* ret = ty_alloc(me, kind, flags);
        ret->xflags = xflags;
        ret->alias = alias;
        ret->alias_args = intern_key_list(slot, count, 3, alias_args.len);
        ret->composite.types = intern_key_list(slot, count, 3 + alias_args.len, len);
        if (origin) ret->composite.origin = make_origin(me, origin, intern_key_list(slot, count, 4 + alias_args.len + len, origin->len));
        if (kind == TY_INTERSECTION) ret->composite.state = arena_new(&my arena, TyObjectState);
        *slot = ret;
    }
    return *slot;
}

static Ty const* intern_composite(
    Checker* me, TyKind kind, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args
) {
    return intern_composite_origin(me, kind, types, len, alias, alias_args, nullptr);
}

// tsc's getConstituentCount
static u32 constituent_count_of(Ty const* const* types, u32 len);
static u32 constituent_count(Ty const* t) {
    if ((t->kind != TY_UNION && t->kind != TY_INTERSECTION) || t->alias) return 1;
    if (t->kind == TY_UNION && t->composite.origin) {
        Ty const* o = t->composite.origin;
        return o->kind == TY_KEYOF ? 1 : constituent_count_of(o->composite.types.data, o->composite.types.len);
    }
    return constituent_count_of(t->composite.types.data, t->composite.types.len);
}
static u32 constituent_count_of(Ty const* const* types, u32 len) {
    u32 n = 0;
    for (u32 i = 0; i < len; i++) n += constituent_count(types[i]);
    return n;
}

// tsc's isNamedUnionType/addNamedUnions
static void add_named_unions(TyBuf* named, Ty const* const* types, u32 len) {
    for (u32 i = 0; i < len; i++) {
        Ty const* t = types[i];
        if (t->kind != TY_UNION) continue;
        Ty const* origin = t->composite.origin;
        if (t->alias || (origin && origin->kind != TY_UNION)) {
            bool seen = false;
            for (u32 j = 0; j < named->len && !seen; j++) seen = named->data[j] == t;
            if (!seen) ty_buf_push(named, t);
        } else if (origin) {
            add_named_unions(named, origin->composite.types.data, origin->composite.types.len);
        }
    }
}

static bool sorted_list_contains(TyList list, Ty const* t) {
    if (list.len <= 8) {
        for (u32 i = 0; i < list.len; i++) {
            if (list.data[i] == t) return true;
        }
        return false;
    }
    u32 lo = 0, hi = list.len;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (list.data[mid] == t) return true;
        if (ty_compare(list.data[mid], t) < 0) lo = mid + 1; else hi = mid;
    }
    return false;
}

static void insert_by_id(TyBuf* buf, Ty const* t);

// tsc's getUnionTypeWorker after a subtype reduction (`members`, sorted, what is left of the inputs' members): the
// inputs' only named union if it has every member left (a reduced `Basic | undefined` is still Basic, though Function
// went for object), else, if the named unions (aliased, or with an origin) and the members outside them don't overlap
// and are all left, the union with those as its origin (`Exit | Primitive` of return types, a narrowed subtype of
// both dropped). Null otherwise: the plain union
Ty const* ty_named_union_of_reduced(Checker* me, Ty const* const* inputs, u32 len, Ty const* const* members, u32 count) {
    TyBuf named = {0};
    add_named_unions(&named, inputs, len);
    Ty const* ret = nullptr;
    if (named.len) {
        TyBuf reduced = {0};
        for (u32 i = 0; i < count; i++) {
            bool in_named = false;
            for (u32 j = 0; j < named.len && !in_named; j++) in_named = sorted_list_contains(named.data[j]->composite.types, members[i]);
            if (!in_named) ty_buf_push(&reduced, members[i]);
        }
        if (named.len == 1 && reduced.len == 0) {
            ret = named.data[0];
        } else {
            u32 named_count = 0;
            for (u32 j = 0; j < named.len; j++) named_count += named.data[j]->composite.types.len;
            if (named_count + reduced.len == count) {
                for (u32 j = 0; j < named.len; j++) insert_by_id(&reduced, named.data[j]);
                OriginSpec spec = { TY_UNION, reduced.data, reduced.len };
                ret = intern_composite_origin(me, TY_UNION, members, count, nullptr, (TyList){0}, &spec);
            }
        }
        ty_buf_drop(&reduced);
    }
    ty_buf_drop(&named);
    return ret;
}

static void insert_by_id(TyBuf* buf, Ty const* t) {
    ty_buf_push(buf, t);
    u32 j = buf->len - 1;
    for (; j && ty_compare(buf->data[j - 1], t) > 0; j--) buf->data[j] = buf->data[j - 1];
    buf->data[j] = t;
}

// String literals that a pattern in the same union already has (tsc's removeStringLiteralsMatchedByTemplateLiterals):
// `"ab" | `a${string}`` is `a${string}`. Returns the new length
// Whether a string starts with a template's first text and ends with its last: what a match needs first
static bool template_ends_fit(Atom const* string, Ty const* pattern) {
    Atom const* head = pattern->template_literal.texts[0];
    Atom const* tail = pattern->template_literal.texts[pattern->template_literal.types.len];
    if (head->len + tail->len > string->len) return false;
    return memcmp(string->str, head->str, head->len) == 0 &&
        memcmp(string->str + string->len - tail->len, tail->str, tail->len) == 0;
}

static u32 remove_literals_matched_by_patterns(Checker* me, Ty const** types, u32 len) {
    // (The patterns once, up front: a union of many literals with a few patterns is typical, `${K}.${P}` paths)
    TyBuf patterns = {0};
    for (u32 i = 0; i < len; i++) if (ty_is_pattern_literal(types[i])) ty_buf_push(&patterns, types[i]);
    if (!patterns.len) return len;
    u32 out = 0;
    for (u32 i = 0; i < len; i++) {
        Ty const* t = types[i];
        bool matched = false;
        if (t->kind == TY_LITERAL && (t->flags & TYF_STRING_LITERAL)) {
            for (u32 j = 0; j < patterns.len && !matched; j++) {
                Ty const* pattern = patterns.data[j];
                matched = pattern->kind == TY_TEMPLATE_LITERAL ?
                    template_ends_fit(t->literal.string, pattern) && ty_template_matches(me, t, pattern) :
                    ty_string_mapping_contains(me, t, pattern);
            }
        }
        if (!matched) types[out++] = t;
    }
    ty_buf_drop(&patterns);
    return out;
}

// Only the cheap reductions: flatten, dedupe, absorb any/unknown/never, drop literals next to their primitive.
// Subtype reduction is a separate operation (TODO, the legacy type_composite() does its own through the relation).
// `origin` is what the union was built as (see Ty.composite.origin); without one, a union made of named unions gets
// them as its origin
static u32 remove_constrained_type_variables(Checker* me, Ty const** types, u32 len);

static Ty const* union_with_origin(
    Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args, OriginSpec const* origin
) {
    if (len == 1) return types[0];
    for (u32 i = 0; i < len; i++) {
        if (types[i] == my intrinsics[TY_WILDCARD]) return types[i];
    }
    TyBuf buf = {0};
    ty_buf_add_flattened(&buf, TY_UNION, types, len);

    u32 includes = 0, xincludes = 0;
    bool includes_error = false, includes_non_widening = false;
    for (u32 i = 0; i < buf.len; i++) {
        includes |= buf.data[i]->flags;
        xincludes |= buf.data[i]->xflags;
        if ((buf.data[i]->flags & TYF_NULLABLE) && !(buf.data[i]->xflags & TYX_CONTAINS_WIDENING)) includes_non_widening = true;
        // (An unresolved name's any too: tsc's isErrorType, IncludesError)
        includes_error |= (buf.data[i]->flags & TYF_ERROR) != 0;
    }
    Ty const* ret = nullptr;
    if (includes & TYF_ANY) {
        ret = my intrinsics[includes_error ? TY_ERROR : TY_ANY];
    } else if (includes & TYF_UNKNOWN) {
        ret = my intrinsics[TY_UNKNOWN_];
    } else {
        // The members of a union are sorted: a filtered union (narrowing) comes in sorted already
        bool sorted = true;
        for (u32 i = 1; i < buf.len && sorted; i++) sorted = ty_compare(buf.data[i - 1], buf.data[i]) <= 0;
        if (!sorted) sort_by_id(buf.data, buf.len);
        bool strict_null_checks = compiler_options()->strict_null_checks;
        if (includes & TYF_FRESH_LITERAL) {
            // Drop fresh literals whose regular twin is here too, before the rest compacts the (sorted) buffer
            // (tsc's removeRedundantLiteralTypes)
            bool* drop = calloc(buf.len, sizeof(bool));
            panic_if(!drop, "out of memory");
            for (u32 i = 0; i < buf.len; i++) {
                drop[i] = ty_is_fresh(buf.data[i]) && sorted_contains(&buf, buf.len, buf.data[i]->literal.regular);
            }
            u32 kept = 0;
            for (u32 i = 0; i < buf.len; i++) if (!drop[i]) buf.data[kept++] = buf.data[i];
            buf.len = kept;
            free(drop);
        }
        u32 out = 0;
        for (u32 i = 0; i < buf.len; i++) {
            Ty const* t = buf.data[i];
            if (out && buf.data[out - 1] == t) continue;
            // undefined and missing: undefined (tsc's getUnionType removes missingType then)
            if (t == my intrinsics[TY_MISSING] && out && buf.data[out - 1] == my intrinsics[TY_UNDEFINED]) continue;
            if (t->kind == TY_INTRINSIC && (t->flags & TYF_NEVER)) continue;
            // Without strictNullChecks null and undefined are in every type, so a union doesn't keep them
            if (!strict_null_checks && (t->flags & TYF_NULLABLE)) continue;
            if ((t->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) && (includes & TYF_STRING)) continue;
            if (t->kind == TY_LITERAL && (
                ((t->flags & TYF_STRING_LITERAL) && (includes & TYF_STRING)) ||
                ((t->flags & TYF_NUMBER_LITERAL) && (includes & TYF_NUMBER)) ||
                ((t->flags & TYF_BIGINT_LITERAL) && (includes & TYF_BIGINT)) ||
                ((t->flags & TYF_UNIQUE_ES_SYMBOL) && (includes & TYF_ES_SYMBOL))
            )) continue;
            buf.data[out++] = t;
        }
        if ((includes & TYF_STRING_LITERAL) && (includes & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING))) {
            out = remove_literals_matched_by_patterns(me, buf.data, out);
        }
        if (xincludes & TYX_CONSTRAINED_TYPE_VARIABLE) out = remove_constrained_type_variables(me, buf.data, out);
        if (out == 0) {
            // Nothing but null and undefined (without strictNullChecks): null wins, like in tsc's getUnionType
            TyIntrinsic only = TY_NEVER;
            if (!strict_null_checks) {
                only = includes & TYF_NULL ? (includes_non_widening ? TY_NULL : TY_NULL_WIDENING) :
                    includes & TYF_UNDEFINED ? (includes_non_widening ? TY_UNDEFINED : TY_UNDEFINED_WIDENING) : TY_NEVER;
            }
            ret = my intrinsics[only];
        } else if (out == 1) {
            ret = buf.data[0];
        } else {
            bool has_named = false;
            for (u32 i = 0; i < len && !has_named; i++) {
                has_named = types[i]->kind == TY_UNION && (types[i]->alias || types[i]->composite.origin);
            }
            if (!origin && has_named) {
                // tsc's getUnionTypeWorker: made of named unions (aliased, or with an origin themselves)
                TyBuf named = {0};
                add_named_unions(&named, types, len);
                if (named.len) {
                    TyBuf reduced = {0};
                    // (Which members a named union has: both lists are sorted, a merge walk)
                    bool* in_named = calloc(out, sizeof(bool));
                    panic_if(!in_named, "out of memory");
                    for (u32 j = 0; j < named.len; j++) {
                        TyList members = named.data[j]->composite.types;
                        u32 i = 0, k = 0;
                        while (i < out && k < members.len) {
                            if (buf.data[i] == members.data[k]) {
                                in_named[i++] = true;
                                k++;
                            } else if (ty_compare(buf.data[i], members.data[k]) < 0) {
                                i++;
                            } else {
                                k++;
                            }
                        }
                    }
                    for (u32 i = 0; i < out; i++) {
                        if (!in_named[i]) ty_buf_push(&reduced, buf.data[i]);
                    }
                    free(in_named);
                    if (!alias && named.len == 1 && reduced.len == 0) {
                        ret = named.data[0];
                    } else {
                        u32 named_count = 0;
                        for (u32 j = 0; j < named.len; j++) named_count += named.data[j]->composite.types.len;
                        if (named_count + reduced.len == out) {
                            for (u32 j = 0; j < named.len; j++) insert_by_id(&reduced, named.data[j]);
                            OriginSpec spec = { TY_UNION, reduced.data, reduced.len };
                            ret = intern_composite_origin(me, TY_UNION, buf.data, out, alias, alias_args, &spec);
                        }
                    }
                    ty_buf_drop(&reduced);
                }
                ty_buf_drop(&named);
            }
            if (!ret) ret = intern_composite_origin(me, TY_UNION, buf.data, out, alias, alias_args, origin);
        }
    }
    ty_buf_drop(&buf);
    return ret;
}

Ty const* ty_union(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args) {
    // Two-member unions are mostly `T | undefined` and `T | U` of the same few types over and over (flow, optional
    // properties, element accesses), where T is often a big union: union_with_origin() would flatten, sort and hash it
    // every time
    if (len == 2 && !alias && types[0] != types[1]) {
        u64 key[3] = { KEY_UNION_PAIR, types[0]->id, types[1]->id };
        InternMiss miss;
        Ty const* cached = intern_lookup(&my links, key, 3, &miss);
        if (cached) return cached;
        Ty const* ret = union_with_origin(me, types, len, alias, alias_args, nullptr);
        *intern_insert(me, &my links, &miss, key, 3) = ret;
        return ret;
    }
    return union_with_origin(me, types, len, alias, alias_args, nullptr);
}

// tsc's getUnionType with UnionReduction.None: flattened, sorted and deduplicated, never dropped, nothing absorbed
// (`unknown | (() => T)` stays a union). Only contextual types are built this way (instantiateInstantiableTypes), so a
// contextual signature is still found next to an `unknown` member
Ty const* ty_union_unreduced(Checker* me, Ty const* const* types, u32 len) {
    if (len == 1) return types[0];
    TyBuf buf = {0};
    ty_buf_add_flattened(&buf, TY_UNION, types, len);
    sort_by_id(buf.data, buf.len);
    u32 out = 0;
    for (u32 i = 0; i < buf.len; i++) {
        Ty const* t = buf.data[i];
        if ((out && buf.data[out - 1] == t) || (t->kind == TY_INTRINSIC && (t->flags & TYF_NEVER))) continue;
        buf.data[out++] = t;
    }
    Ty const* ret = out == 0 ? my intrinsics[TY_NEVER] : out == 1 ? buf.data[0] :
        intern_composite(me, TY_UNION, buf.data, out, nullptr, (TyList){0});
    ty_buf_drop(&buf);
    return ret;
}

// Some members of `of`, in its order (narrowing's filters): they are sorted and reduced already, so there's only the
// interning left to do. Rebuilding a filtered 300-member enum union through ty_union() is most of what narrowing an
// enum by `===` costs
Ty const* ty_union_filtered(Checker* me, Ty const* of, Ty const* const* kept, u32 len) {
    check(of->kind == TY_UNION);
    if (len == of->composite.types.len) return of;
    if (len == 0) return my intrinsics[TY_NEVER];
    if (len == 1) return kept[0];
    Ty const* origin = of->composite.origin;
    if (!origin || origin->kind != TY_UNION) return intern_composite(me, TY_UNION, kept, len, nullptr, (TyList){0});
    // tsc's filterType: the origin loses the same members, if it can (named unions in it stay)
    TyList from = origin->composite.types;
    TyList kept_list = { .data = kept, .len = len };
    TyBuf filtered = {0};
    for (u32 i = 0; i < from.len; i++) {
        if (from.data[i]->kind == TY_UNION || sorted_list_contains(kept_list, from.data[i])) {
            ty_buf_push(&filtered, from.data[i]);
        }
    }
    Ty const* ret;
    if (from.len - filtered.len != of->composite.types.len - len) {
        ret = intern_composite(me, TY_UNION, kept, len, nullptr, (TyList){0});
    } else if (filtered.len == 1) {
        ret = filtered.data[0];
    } else {
        OriginSpec spec = { TY_UNION, filtered.data, filtered.len };
        ret = intern_composite_origin(me, TY_UNION, kept, len, nullptr, (TyList){0}, &spec);
    }
    ty_buf_drop(&filtered);
    return ret;
}

// Which disjoint family of primitives a type belongs to, 0 if none
static u32 primitive_family(Ty const* t) {
    if (t->kind != TY_INTRINSIC && t->kind != TY_LITERAL) return 0;
    if (t->flags & (TYF_STRING | TYF_STRING_LITERAL)) return TYF_STRING;
    // (A computed enum member, or an enum without members, is number-like: tsc's TypeFlags.Enum in NumberLike)
    if (t->flags & (TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) return TYF_NUMBER;
    if (t->flags & (TYF_BIGINT | TYF_BIGINT_LITERAL)) return TYF_BIGINT;
    if (t->flags & TYF_BOOLEAN_LITERAL) return TYF_BOOLEAN_LITERAL;
    if (t->flags & TYF_UNIQUE_ES_SYMBOL) return TYF_ES_SYMBOL;
    return t->flags & (TYF_ES_SYMBOL | TYF_VOID | TYF_UNDEFINED | TYF_NULL);
}

static bool is_primitive_union(Ty const* t) {
    if (t->kind != TY_UNION) return false;
    for (u32 i = 0; i < t->composite.types.len; i++) {
        if (!primitive_family(t->composite.types.data[i])) return false;
    }
    return true;
}

static bool union_contains(Ty const* u, Ty const* t) {
    return sorted_list_contains(u->composite.types, t);
}

// `("a" | "b") & ("b" | "c")` is `"b"`: unions of primitives are intersected as sets rather than distributed over, which
// would be a cross product (`keyof (A | B | C | ...)` is such an intersection). All but the first of them leave `buf`,
// the first is replaced by the result
static void intersect_unions_of_primitives(Checker* me, TyBuf* buf) {
    u32 first = buf->len;
    u32 count = 0;
    for (u32 i = 0; i < buf->len; i++) {
        if (!is_primitive_union(buf->data[i])) continue;
        if (!count++) first = i;
    }
    if (count < 2) return;

    Ty const** unions = malloc(sizeof(Ty const*) * count);
    u32 out = 0;
    count = 0;
    for (u32 i = 0; i < buf->len; i++) {
        if (is_primitive_union(buf->data[i])) {
            unions[count++] = buf->data[i];
            if (i != first) continue;
        }
        buf->data[out++] = buf->data[i];
    }
    buf->len = out;

    // Each member once (tsc's `checked` set): `keyof (A | B | ...)` of many types sharing their keys is otherwise
    // quadratic in the number of unions (the result is a union, which orders it anyway)
    TyBuf candidates = {0};
    for (u32 u = 0; u < count; u++) {
        TyList members = unions[u]->composite.types;
        for (u32 i = 0; i < members.len; i++) ty_buf_push(&candidates, members.data[i]);
    }
    sort_by_id(candidates.data, candidates.len);
    TyBuf result = {0};
    for (u32 c = 0; c < candidates.len; c++) {
        Ty const* t = candidates.data[c];
        if (c && candidates.data[c - 1] == t) continue;
        bool everywhere = true;
        for (u32 other = 0; other < count && everywhere; other++) {
            if (union_contains(unions[other], t)) continue;
            // undefined and exactOptionalPropertyTypes' missing type match each other (tsc's eachUnionContains)
            if (t == my intrinsics[TY_MISSING] || t == my intrinsics[TY_UNDEFINED]) {
                everywhere = union_contains(unions[other], my intrinsics[t == my intrinsics[TY_MISSING] ? TY_UNDEFINED : TY_MISSING]);
                continue;
            }
            // A literal is also matched by its primitive: `("a" | 1) & (string | 2)` keeps "a"
            u32 family = t->kind == TY_LITERAL ? primitive_family(t) : 0;
            Ty const* primitive =
                family == TYF_STRING ? my intrinsics[TY_STRING] :
                family == TYF_NUMBER ? my intrinsics[TY_NUMBER] :
                family == TYF_BIGINT ? my intrinsics[TY_BIGINT] : nullptr;
            everywhere = primitive && union_contains(unions[other], primitive);
        }
        if (everywhere) ty_buf_push(&result, t);
    }
    ty_buf_drop(&candidates);
    // (Both: the missing type, which a union with undefined would drop: tsc's intersectUnionsOfPrimitiveTypes)
    bool has_missing = false;
    for (u32 i = 0; i < result.len; i++) has_missing |= result.data[i] == my intrinsics[TY_MISSING];
    if (has_missing) {
        u32 kept = 0;
        for (u32 i = 0; i < result.len; i++) {
            if (result.data[i] != my intrinsics[TY_UNDEFINED]) result.data[kept++] = result.data[i];
        }
        result.len = kept;
    }
    buf->data[first] = ty_union(me, result.data, result.len, nullptr, (TyList){0});
    ty_buf_drop(&result);
    free(unions);
}

// tsc's isLiteralType: what a property can discriminate by
static bool is_literal_like(Ty const* t) {
    if (t->flags & (TYF_BOOLEAN | TYF_ENUM_LITERAL)) return true;
    if (t->kind == TY_UNION) {
        for (u32 i = 0; i < t->composite.types.len; i++) {
            if (!is_literal_like(t->composite.types.data[i])) return false;
        }
        return true;
    }
    return t->kind == TY_LITERAL || (t->kind == TY_INTRINSIC && (t->flags & TYF_NULLABLE));
}

static Prop const* root_declaration(Prop const* prop) {
    while (prop->target) prop = prop->target;
    return prop;
}

// An object type whose properties are known: not generic (TYF_COULD_CONTAIN_TYPE_VARIABLES says too little: a type
// literal with a `typeof` in it has it)
static bool is_known_object(Checker* me, Ty const* ty) {
    return (ty->flags & TYF_OBJECT) && (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || !ty_is_generic_object(me, ty));
}

// The same, cheap enough for every intersection's never-reduction (getReducedType): an object with the flag counts only
// if it is a type literal as written (not an instantiation) that has it from a `typeof` alone, mentioning no type
// parameter. Resolving the members of every generic instantiation in an intersection cost valibot 39% instructions
static bool is_known_object_for_reduction(Checker* me, Ty const* ty) {
    if (!(ty->flags & TYF_OBJECT)) return false;
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return true;
    if (ty->kind != TY_ANONYMOUS) return false;
    // (An instantiation of one, by a mapper whose types have no type variables: the literal instantiated inside a
    // generic signature)
    Ty const* root = ty->anonymous.target ? ty->anonymous.target : ty;
    if (root->kind == TY_ANONYMOUS && !root->anonymous.target && root->anonymous.block_params_only) return true;
    if (root->kind != TY_ANONYMOUS || root->anonymous.target || !root->anonymous.free_params_unknown || root->anonymous.free_params.len) return false;
    TyMapper const* mapper = ty->anonymous.target ? ty->anonymous.mapper : nullptr;
    for (u32 i = 0; mapper && i < mapper->len; i++) {
        if (mapper->targets[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) return false;
    }
    return !ty_is_generic_object(me, ty);
}

// A property of types[i] that a later member declares separately, one of them privately (tsc's
// isConflictingPrivateProperty: a synthetic property with no single value declaration that contains a private one)
static bool has_conflicting_private(Checker* me, Prop const* prop, Ty const* const* types, u32 i, u32 len) {
    for (u32 j = i + 1; j < len; j++) {
        Ty const* b = types[j];
        if (!is_known_object_for_reduction(me, b)) continue;
        Prop const* other = ty_get_prop(me, b, prop->name);
        // (Only declarations count: a mapped type's property, `Record<"m", unknown>`'s, has none, so with a class's
        // private `m` the property has that one value declaration and isn't conflicting, tsc's
        // createUnionOrIntersectionProperty hasNonUniformValueDeclaration)
        Prop const* a = root_declaration(prop);
        Prop const* c = other ? root_declaration(other) : nullptr;
        if (other && ((prop->flags | other->flags) & PROP_PRIVATE) && a != c && span_valid(a->decl->name_span) && span_valid(c->decl->name_span)) {
            return true;
        }
    }
    return false;
}

// Why an intersection of object types is never (tsc's isNeverReducedProperty over getPropertiesOfUnionOrIntersectionType):
// a required property whose types are disjoint, one of them a literal (`{ kind: "a" } & { kind: "b" }`,
// isDiscriminantWithNeverType), or one declared separately by two members, privately by one (isConflictingPrivateProperty).
// Null if it isn't; `*private_conflict` says which
// A discriminant conflict between a known object member and a type literal (or an instantiation of one) that mentions
// type variables elsewhere: `{ type: "next"; value: T } & { type: "error" }` is never (tsc's getReducedType doesn't care
// whether the members are generic, only isGenericObjectType). Only the literal-like properties of the known members are
// looked up in the others: comparing every property of every generic instantiation is what made valibot +39% (c2146ada)
// The type of a required property of a known object type, or of every member of a union of them (tsc's union
// property); null if some member lacks it or has it optional, or a member isn't known
static Ty const* apparent_union_prop_type(Checker* me, Ty const* apparent, Atom const* name) {
    TyList members = apparent->kind == TY_UNION ? apparent->composite.types : (TyList){ .data = &apparent, .len = 1 };
    if (members.len > 64) return nullptr;
    Ty const* parts[64];
    for (u32 i = 0; i < members.len; i++) {
        Ty const* m = members.data[i];
        if (!(m->flags & (TYF_OBJECT | TYF_INTERSECTION)) || (m->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return nullptr;
        Prop const* prop = ty_property_of_type(me, m, name);
        if (!prop || (prop->flags & PROP_OPTIONAL)) return nullptr;
        parts[i] = prop_type(me, prop);
    }
    return members.len == 1 ? parts[0] : ty_union(me, parts, members.len, nullptr, (TyList){0});
}

// A type parameter's constraint (known already: resolving it from wherever an intersection is reduced finds
// circularities tsc doesn't) or an indexed access's apparent type (`Definitions[EndpointName]`, a union of
// definitions), when it is known object types or a union of them; else null
static Ty const* known_objects_constraint(Checker* me, Ty const* ty) {
    Ty const* constraint = nullptr;
    if (ty->kind == TY_TYPE_PARAM) {
        TyParamState const* param = ty_non_distributed(ty)->param.state;
        if (TAINT_IF(TAINT_PEEK, param->constraint_state != LAZY_STATE_DONE)) return nullptr;
        constraint = param->constraint;
    } else if (ty->kind == TY_INDEXED) {
        constraint = ty_apparent_type(me, ty);
    }
    if (!constraint) return nullptr;
    TyList members = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
    for (u32 i = 0; i < members.len; i++) {
        Ty const* m = members.data[i];
        if (!(m->flags & (TYF_OBJECT | TYF_INTERSECTION)) || (m->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return nullptr;
    }
    return constraint;
}

// Whether a constituent's property `name` is never (tsc's CheckFlags.HasNeverType of the intersection's property): it
// isn't reduced then. A known object's, or a type variable's through a constraint of known objects
static bool some_prop_is_never(Checker* me, Atom const* name, Ty const* const* types, u32 len) {
    for (u32 k = 0; k < len; k++) {
        if (types[k]->kind == TY_TYPE_PARAM || types[k]->kind == TY_INDEXED) {
            Ty const* constraint = known_objects_constraint(me, types[k]);
            Ty const* type = constraint ? apparent_union_prop_type(me, constraint, name) : nullptr;
            if (type && (type->flags & TYF_NEVER)) return true;
            continue;
        }
        if (!is_known_object_for_reduction(me, types[k])) continue;
        Prop const* prop = ty_get_prop(me, types[k], name);
        if (prop && (prop_type(me, prop)->flags & TYF_NEVER)) return true;
    }
    return false;
}

static Atom const* never_reduced_literal_prop(Checker* me, Ty const* const* types, u32 len) {
    // (A type variable whose constraint is known objects, which a type literal's literal property can conflict with:
    // see below. Looked for first, it is rare: resolving the members of every type literal next to a type variable
    // cost zod 1% instructions)
    bool ready_param = false;
    for (u32 i = 0; i < len && !ready_param; i++) {
        Ty const* constraint = known_objects_constraint(me, types[i]);
        ready_param = constraint != nullptr;
    }
    for (u32 i = 0; i < len; i++) {
        Ty const* known = types[i];
        // (A type literal that may mention type parameters still has literal properties that don't, which a type
        // parameter's constraint can conflict with. Only then: its property types are resolved for it)
        bool known_object = is_known_object_for_reduction(me, known);
        bool literal_object = !known_object && ready_param && known->kind == TY_ANONYMOUS && (known->flags & TYF_OBJECT) &&
            !ty_is_generic_object(me, known);
        if (!literal_object && !known_object) continue;
        Members const* members = ty_members(me, known);
        for (u32 p = 0; p < members->prop_count; p++) {
            Prop const* prop = members->props[p];
            Ty const* a_type = nullptr;
            for (u32 j = 0; j < len; j++) {
                Ty const* other_type = types[j];
                if (j == i) continue;
                bool variable = other_type->kind == TY_TYPE_PARAM || other_type->kind == TY_INDEXED;
                if (literal_object && !variable) continue;
                if ((other_type->kind != TY_ANONYMOUS && other_type->kind != TY_REFERENCE && !variable) ||
                    !(other_type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)
                ) {
                    continue;
                }
                if (variable) {
                    // A type parameter takes part through its constraint (tsc's getPropertiesOfUnionOrIntersectionType,
                    // its apparent type): `D & { type: "query" }` with D constrained to definitions whose `type` is an
                    // enum member is never. Only a constraint known already and made of known object types: resolving
                    // one here, from wherever an intersection is reduced, finds circularities tsc doesn't. (Its side
                    // first: the property's own type is resolved only for a name the constraint has)
                    Ty const* constraint = ready_param ? known_objects_constraint(me, other_type) : nullptr;
                    if (!constraint) continue;
                    Ty const* b_type = apparent_union_prop_type(me, constraint, prop->name);
                    if (!b_type || (b_type->flags & TYF_NEVER)) continue;
                    if (!a_type) {
                        a_type = prop_type(me, prop);
                        if (!is_literal_like(a_type) || (a_type->flags & TYF_NEVER)) break;
                    }
                    Ty const* both[2] = { a_type, b_type };
                    if (ty_intersection(me, both, 2, nullptr, (TyList){0})->flags & TYF_NEVER) {
                        // (Not when a third constituent's is never, see never_reduced_prop())
                        if (some_prop_is_never(me, prop->name, types, len)) break;
                        return prop->name;
                    }
                    continue;
                }
                if (!a_type) {
                    a_type = prop_type(me, prop);
                    if (!is_literal_like(a_type) || (a_type->flags & TYF_NEVER)) break;
                }
                if (ty_is_generic_object(me, other_type)) continue;
                Prop const* other = ty_get_prop(me, other_type, prop->name);
                if (!other || (prop->flags & other->flags & PROP_OPTIONAL)) continue;
                Ty const* b_type = prop_type(me, other);
                // (A literal part on one side is enough, tsc's CheckFlags.HasLiteralPart)
                if (b_type->flags & TYF_NEVER) continue;
                Ty const* both[2] = { a_type, b_type };
                if (ty_intersection(me, both, 2, nullptr, (TyList){0})->flags & TYF_NEVER) {
                    if (some_prop_is_never(me, prop->name, types, len)) break;
                    return prop->name;
                }
            }
        }
    }
    return nullptr;
}

static Atom const* never_reduced_prop(Checker* me, Ty const* const* types, u32 len, bool* private_conflict) {
    u32 objects = 0;
    for (u32 i = 0; i < len; i++) {
        if (is_known_object_for_reduction(me, types[i])) objects++;
    }
    if (objects < 2) {
        *private_conflict = false;
        return never_reduced_literal_prop(me, types, len);
    }
    Atom const* private_name = nullptr;
    for (u32 i = 0; i < len; i++) {
        Ty const* a = types[i];
        if (!is_known_object_for_reduction(me, a)) continue;
        Members const* members = ty_members(me, a);
        for (u32 p = 0; p < members->prop_count; p++) {
            Prop const* prop = members->props[p];
            if (!private_name && has_conflicting_private(me, prop, types, i, len)) private_name = prop->name;
            // (The intersection's property is required if one of them is: an optional one is that type or undefined
            // then, `{ a: string } & Partial<{ a: never }>` is never, string & undefined)
            Ty const* a_type = nullptr;
            for (u32 j = i + 1; j < len; j++) {
                Ty const* b = types[j];
                if (!is_known_object_for_reduction(me, b)) continue;
                Prop const* other = ty_get_prop(me, b, prop->name);
                if (!other || (prop->flags & other->flags & PROP_OPTIONAL)) continue;
                if (!a_type) {
                    a_type = prop_type(me, prop);
                }
                Ty const* b_type = prop_type(me, other);
                if (!is_literal_like(a_type) && !is_literal_like(b_type)) continue;
                // (Not when one of them is never: tsc's isDiscriminantWithNeverType, CheckFlags.HasNeverType)
                if ((a_type->flags | b_type->flags) & TYF_NEVER) continue;
                Ty const* both[2] = { a_type, b_type };
                if (ty_intersection(me, both, 2, nullptr, (TyList){0})->flags & TYF_NEVER) {
                    // (Nor when a third constituent's is never: HasNeverType is the whole property's, `{ a: {} } &
                    // { a?: never } & { a: never }` stays, apollo's options with `TVariables = never`)
                    if (some_prop_is_never(me, prop->name, types, len)) break;
                    *private_conflict = false;
                    return prop->name;
                }
            }
        }
    }
    *private_conflict = private_name != nullptr;
    return private_name;
}

// tsc's getReducedType: an intersection of object types whose properties conflict is never, a union of such
// intersections lacks them (getReducedUnionType). Asked where tsc asks it (property lookup through the apparent type,
// relations, keyof, inference, printing...), not when the intersection is made
Ty const* ty_reduced_slow(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) {
        TyObjectState* state = ty->composite.state;
        if (!state) return ty; // (A union's origin)
        if (state->reduced_state == REDUCED_NOT_STARTED) {
            state->reduced_state = REDUCED_IN_FLIGHT;
            bool private_conflict;
            bool never = never_reduced_prop(me, ty->composite.types.data, ty->composite.types.len, &private_conflict) != nullptr;
            state->reduced_state = never ? REDUCED_NEVER : REDUCED_ITSELF;
        }
        return state->reduced_state == REDUCED_NEVER ? my intrinsics[TY_NEVER] : ty;
    }
    if (ty->kind != TY_UNION || !(ty->xflags & TYX_CONTAINS_INTERSECTIONS)) return ty;
    u64 key[2] = { KEY_REDUCED_UNION, ty->id };
    void const* cached = intern_get(&my links, key, 2);
    if (cached) return cached;
    TyList types = ty->composite.types;
    Ty const* stack_buf[32];
    Ty const** reduced = types.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * types.len);
    panic_if(!reduced, "out of memory");
    bool changed = false;
    for (u32 i = 0; i < types.len; i++) {
        reduced[i] = ty_reduced(me, types.data[i]);
        changed |= reduced[i] != types.data[i];
    }
    Ty const* ret = changed ? ty_union(me, reduced, types.len, nullptr, (TyList){0}) : ty;
    if (reduced != stack_buf) free(reduced);
    *intern_slot(me, &my links, key, 2) = ret;
    if (ret != ty && ret->kind == TY_UNION) *intern_slot(me, &my links, (u64[]){ KEY_REDUCED_UNION, ret->id }, 2) = ret;
    return ret;
}

// tsc's elaborateNeverIntersection: why `ty` is never, the intersection printed unreduced. False if it isn't one
bool ty_never_intersection_elaboration(Checker* me, Ty const* ty, unsigned* ts_code, cstr* message) {
    if (ty->kind != TY_INTERSECTION || ty_reduced(me, ty) == ty) return false;
    bool private_conflict;
    Atom const* name = never_reduced_prop(me, ty->composite.types.data, ty->composite.types.len, &private_conflict);
    if (!name) return false;
    cstr ty_str = cstr_null;
    ty_display(me, (TyDisplay){ .target = &ty_str, .no_reduction = true }, ty);
    cstr name_str = ty_prop_name_cstr(me, name);
    *ts_code = private_conflict ? 18032 : 18031;
    *message = cstr_d("The intersection '", ty_str, "' was reduced to 'never' because property '", name_str,
        private_conflict ? "' exists in multiple constituents and is private in some." : "' has conflicting types in some constituents.");
    c_drop(cstr, &ty_str, &name_str);
    return true;
}

// The templates and mappings of an intersection with the string literal `literal` go if the literal is one of theirs,
// true if a pattern doesn't have it (the intersection is empty)
static bool extract_redundant_templates(Checker* me, Ty const** types, u32* len, Ty const* literal) {
    u32 out = 0;
    for (u32 i = 0; i < *len; i++) {
        Ty const* t = types[i];
        if (t->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) {
            if (ty_is_related(me, literal, t, TY_RELATION_SUBTYPE)) continue;
            if (ty_is_pattern_literal(t)) return true;
        }
        types[out++] = t;
    }
    *len = out;
    return false;
}

// tsc's isEmptyAnonymousObjectType: `{}` (a type literal or anonymous object without members)
bool ty_is_empty_anonymous_object(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS) return false;
    if (ty->anonymous.empty_symbol) return true;
    // A type literal goes by its syntax (tsc's isEmptyTypeLiteralSymbol): its members may not be resolvable yet, `type
    // Foo = { x: string } & { [x: number]: Foo }` (the same for its instantiations)
    Ty const* root = ty;
    while (root->anonymous.target && !root->anonymous.applies_type_args) root = root->anonymous.target;
    if (root->kind == TY_ANONYMOUS && root->anonymous.node && root->anonymous.node->tag == TYPE_INTERFACE) {
        return !vec_interface_member_size(&root->anonymous.node->interface.members);
    }
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->call_count && !members->construct_count && !members->index_count &&
        !members->mapped;
}

// Options of intersection(): keep `{}` next to non-nullable members; no reduction of `T & P` with T's primitive
// constraint (tsc's IntersectionFlags.NoConstraintReduction, which the members it distributes into inherit)
enum { INTERSECT_KEEP_EMPTY_OBJECT = 1, INTERSECT_NO_CONSTRAINT_REDUCTION = 2 };
static Ty const* intersection(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args, u32 opts);

// Intersections without an alias are cached by their members (tsc's intersectionTypes by type list id, which it
// looks up before distributing over unions, as this does): distributing `SyntaxKind & T` over a big union again at
// every relation that asks was ~4% of the tsc corpus
// (Of any length: a big one that is too complex, TS2590, is the error type from then on, reported once like tsc's whose
// divide and conquer caches the error type under the outer key: base-ui's `<Element ref={r} {...props} />` twice)
static Ty const* intersection_cached(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args, u32 opts) {
    if (alias || len < 2) return intersection(me, types, len, alias, alias_args, opts);
    // (A long one only if it distributes over a union, where the work and TS2590 are: the rest are cheaper to make
    // again than to key, type-fest's long intersections of objects)
    if (len > 14) {
        bool has_union = false;
        for (u32 i = 0; i < len && !has_union; i++) has_union = types[i]->kind == TY_UNION;
        if (!has_union) return intersection(me, types, len, alias, alias_args, opts);
    }
    u64 small[16];
    u64* key = len <= 14 ? small : malloc(sizeof(u64) * (len + 2));
    panic_if(!key, "out of memory");
    key[0] = KEY_INTERSECTION_OF;
    key[1] = opts;
    for (u32 i = 0; i < len; i++) key[2 + i] = types[i]->id;
    Ty const* ret = intern_get(&my links, key, len + 2);
    if (!ret) {
        ret = intersection(me, types, len, alias, alias_args, opts);
        *intern_slot(me, &my links, key, len + 2) = ret;
    }
    if (key != small) free(key);
    return ret;
}

Ty const* ty_intersection(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args) {
    return intersection_cached(me, types, len, alias, alias_args, false);
}

// An intersection made by an alias as the same intersection without it (tsc's getIntersectionType of the one type
// flattens it into its members, which are reduced already: nothing to compute again, and resolving their members here
// could reach a declaration being resolved, a spurious TS2502)
Ty const* ty_intersection_unaliased(Checker* me, Ty const* ty) {
    if (ty->kind != TY_INTERSECTION || !ty->alias) return ty;
    return intern_composite(me, TY_INTERSECTION, ty->composite.types.data, ty->composite.types.len, nullptr, (TyList){0});
}

// Whether tsc's getTypeWithThisArgument(t, this, needApparentType) makes a member of an intersection anew: a class or
// interface with a `this` type (a reference gets the this-argument), a nested intersection with such a member, or
// what has an apparent type of its own (a primitive, a type parameter)
static bool apparent_member_changes(Checker* me, Ty const* ty, u32 depth) {
    if (ty->kind == TY_DECLARED) return ty->declared.this_param != nullptr;
    if (ty->kind == TY_REFERENCE) return !ty->reference.this_arg && ty->reference.target->declared.this_param;
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len && depth < 8; i++) {
            if (apparent_member_changes(me, ty->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    return ty_apparent_type(me, ty) != ty;
}

// The apparent type of an intersection as it prints (tsc's getApparentTypeOfIntersectionType): made again without its
// alias when a member changes (`Loaded<User>` is `User & {...}`, a class), itself otherwise (an alias of an
// intersection of object literal types stays `RecoveredSignatureType`)
Ty const* ty_intersection_apparent_unaliased(Checker* me, Ty const* ty) {
    if (ty->kind != TY_INTERSECTION || !ty->alias) return ty;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (apparent_member_changes(me, ty->composite.types.data[i], 0)) return ty_intersection_unaliased(me, ty);
    }
    return ty;
}

Ty const* ty_intersection_keeping_empty_object(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args) {
    return intersection_cached(me, types, len, alias, alias_args, INTERSECT_KEEP_EMPTY_OBJECT);
}

Ty const* ty_intersection_no_constraint_reduction(Checker* me, Ty const* const* types, u32 len) {
    return intersection_cached(me, types, len, nullptr, (TyList){0}, INTERSECT_NO_CONSTRAINT_REDUCTION);
}

static bool union_has(Ty const* ty, u32 flag) {
    if (ty->kind != TY_UNION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (ty->composite.types.data[i]->flags & flag) return true;
    }
    return false;
}

// tsc's getIntersectionType with unions among `buf` (not yet distributed), before the cross product: when every member
// is a union with undefined (or null) in it, that is taken out, `(A | undefined) & (B | undefined)` being
// `(A & B) | undefined`; three or more members are intersected in halves, which usually makes far fewer combinations
// than the whole cross product; one of 100000 or more combinations is the error type (tsc's TS2590, reported at the
// current node, once per intersection asked for: see intersection_cached()). Null if the plain cross product it is. `len`: how many types the intersection was asked for
static Ty const* intersect_unions(Checker* me, TyBuf* buf, u32 len, Symbol const* alias, TyList alias_args, u32 opts) {
    u32 flags[2] = { TYF_UNDEFINED, TYF_NULL };
    for (u32 f = 0; f < 2; f++) {
        bool all = true;
        for (u32 i = 0; i < buf->len && all; i++) all = union_has(buf->data[i], flags[f]);
        if (!all) continue;
        Ty const** rest = calloc(buf->len, sizeof(Ty const*));
        panic_if(!rest, "out of memory");
        for (u32 i = 0; i < buf->len; i++) {
            TyList members = buf->data[i]->composite.types;
            Ty const** kept = malloc(sizeof(Ty const*) * members.len);
            panic_if(!kept, "out of memory");
            u32 kept_len = 0;
            for (u32 j = 0; j < members.len; j++) {
                if (!(members.data[j]->flags & flags[f])) kept[kept_len++] = members.data[j];
            }
            rest[i] = ty_union(me, kept, kept_len, nullptr, (TyList){0});
            free(kept);
        }
        // (The missing type if any has it: `(X | undefined) & (X | missing)` is `X | missing`)
        bool missing = false;
        for (u32 i = 0; f == 0 && i < buf->len && !missing; i++) {
            TyList members = buf->data[i]->composite.types;
            for (u32 j = 0; j < members.len && !missing; j++) missing = members.data[j] == my intrinsics[TY_MISSING];
        }
        Ty const* both[2] = { intersection_cached(me, rest, buf->len, nullptr, (TyList){0}, opts & INTERSECT_NO_CONSTRAINT_REDUCTION),
            my intrinsics[missing ? TY_MISSING : f == 0 ? TY_UNDEFINED : TY_NULL] };
        free(rest);
        return ty_union(me, both, 2, alias, alias_args);
    }
    if (buf->len >= 3 && len > 2) {
        u32 middle = buf->len / 2;
        u32 inherited = opts & INTERSECT_NO_CONSTRAINT_REDUCTION;
        Ty const* halves[2] = {
            intersection_cached(me, buf->data, middle, nullptr, (TyList){0}, inherited),
            intersection_cached(me, buf->data + middle, buf->len - middle, nullptr, (TyList){0}, inherited),
        };
        return intersection(me, halves, 2, alias, alias_args, inherited);
    }
    // (tsc's checkCrossProductUnion)
    double size = 1;
    for (u32 i = 0; i < buf->len; i++) {
        if (buf->data[i]->kind == TY_UNION) size *= buf->data[i]->composite.types.len;
    }
    if (size < 100000) return nullptr;
    ty_error_at_current_node_d(me, 2590, "Expression produces a union type that is too complex to represent.");
    return my intrinsics[TY_ERROR];
}

// tsc's TypeFlags.TypeVariable: type parameters and indexed accesses
static bool is_type_variable(Ty const* t) { return t->kind == TY_TYPE_PARAM || t->kind == TY_INDEXED; }

// A primitive, `object` or `{}`: what a constrained type variable intersection is made of (tsc's getIntersectionType)
static bool is_primitive_or_empty_object(Checker* me, Ty const* t) {
    return primitive_family(t) || (t->flags & TYF_NON_PRIMITIVE) || ty_is_empty_anonymous_object(me, t);
}

// tsc's getIntersectionType for `T & P`, T a type variable, P a primitive, `object` or `{}`, when T's constraint is
// made of those too: T if the constraint is a strict subtype of P, never if they are unrelated. Otherwise null, and
// *constrained says whether it's a constrained type variable
static Ty const* compute_type_variable_reduction(Checker* me, Ty const* variable, Ty const* primitive, bool* constrained);

static Ty const* reduce_type_variable_intersection(Checker* me, Ty const* a, Ty const* b, bool* constrained) {
    *constrained = false;
    Ty const* variable = is_type_variable(a) ? a : b;
    Ty const* primitive = variable == a ? b : a;
    if (!is_type_variable(variable) || is_type_variable(primitive)) return nullptr;
    // (Not asking whether P is `{}` before the constraint says it could matter: that resolves an object's members)
    if (!primitive_family(primitive) && !(primitive->flags & (TYF_NON_PRIMITIVE | TYF_OBJECT))) return nullptr;
    // (Memoized: `T & {}` is made over and over, NonNullable<T>. 1: nothing to reduce, 2: constrained)
    u64 key[3] = { KEY_TYPE_VARIABLE_REDUCTION, variable->id, primitive->id };
    void const* known = intern_get(&my links, key, 3);
    if (known) {
        *constrained = (uintptr_t)known == 2;
        return (uintptr_t)known <= 2 ? nullptr : known;
    }
    Ty const* ret = compute_type_variable_reduction(me, variable, primitive, constrained);
    *intern_slot(me, &my links, key, 3) = ret ? (void const*)ret : (void const*)(uintptr_t)(*constrained ? 2 : 1);
    return ret;
}

// The base constraint of a type variable if it's made of primitives, `object` and `{}`, else null. Once per variable:
// a conditional type's constraint is costly, and `T & P` is asked for with many P (expect-type's `T & string`, `T &
// number`...)
static Ty const* primitive_constraint_of(Checker* me, Ty const* variable) {
    u64 key[3] = { KEY_TYPE_VARIABLE_REDUCTION, variable->id, 0 };
    void const* known = intern_get(&my links, key, 3);
    if (known) return (uintptr_t)known == 1 ? nullptr : known;
    // (Re-entered while its base constraint is computed: none, like tsc's circular base constraint. The claim says it's
    // in flight, the links only ever hold the answer)
    if (ty_claim_find(me, CLAIM_PRIMITIVE_CONSTRAINT, variable, 0)) return nullptr;
    Claim claim;
    ty_claim_push(me, CLAIM_PRIMITIVE_CONSTRAINT, &claim, variable, 0);
    Ty const* ret = nullptr;
    // (The declared constraint tells first, cheaply: an object type in it is the end of it. An indexed access has
    // none: its base constraint is all there is)
    Ty const* declared = variable->kind == TY_INDEXED ? nullptr : ty_param_constraint(me, variable);
    TyList declared_parts = !declared ? (TyList){0} :
        declared->kind == TY_UNION ? declared->composite.types : (TyList){ .data = &declared, .len = 1 };
    bool possible = declared != nullptr;
    if (variable->kind == TY_INDEXED) {
        // HACK (perf, like the declared constraint's below): an access into a conditional type's result whose branches
        // are generic (`NonNullable<ComponentPropsWithRef<C>["ref"]>` in @types/react's ElementRef) is taken to be no
        // primitive. tsc asks its base constraint, which distributes the conditional type over C's constraint: every
        // intrinsic element's props resolved, react +15% instructions. Plain branches stay (arktype's
        // `(kind extends "required" ? Required.Declaration : Optional.Declaration)["kind"]`)
        Ty const* object = variable->indexed.object;
        while (object->kind == TY_INDEXED) object = object->indexed.object;
        possible = true;
        if (object->kind == TY_CONDITIONAL) {
            Ty const* root = object->conditional.root ? object->conditional.root : object;
            possible = !((ty_conditional_branch(me, root, true)->flags | ty_conditional_branch(me, root, false)->flags) &
                TYF_COULD_CONTAIN_TYPE_VARIABLES);
        }
    }
    for (u32 i = 0; i < declared_parts.len && possible; i++) {
        Ty const* part = declared_parts.data[i];
        // HACK: a deferred type in the declared constraint (expect-type's `Expected extends Cond<...> ? ... : ...`) is
        // taken to be no primitive: tsc asks its base constraint, which costs tttt 5% of valibot
        possible = part->kind == TY_TYPE_PARAM || is_primitive_or_empty_object(me, part);
    }
    Ty const* constraint = possible ? ty_base_constraint(me, variable) : nullptr;
    if (constraint && constraint != variable) {
        TyList parts = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
        bool all = true;
        for (u32 i = 0; i < parts.len && all; i++) all = is_primitive_or_empty_object(me, parts.data[i]);
        if (all) ret = constraint;
    }
    ty_claim_pop(me, CLAIM_PRIMITIVE_CONSTRAINT, &claim);
    *intern_slot(me, &my links, key, 3) = ret ? (void const*)ret : (void const*)(uintptr_t)1;
    return ret;
}

static Ty const* compute_type_variable_reduction(Checker* me, Ty const* variable, Ty const* primitive, bool* constrained) {
    Ty const* constraint = primitive_constraint_of(me, variable);
    if (!constraint || !is_primitive_or_empty_object(me, primitive)) return nullptr;
    TyList parts = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
    if (ty_is_related(me, constraint, primitive, TY_RELATION_STRICT_SUBTYPE)) return variable;
    bool some = false;
    for (u32 i = 0; constraint->kind == TY_UNION && i < parts.len && !some; i++) {
        some = ty_is_related(me, parts.data[i], primitive, TY_RELATION_STRICT_SUBTYPE);
    }
    if (!some && !ty_is_related(me, primitive, constraint, TY_RELATION_STRICT_SUBTYPE)) return my intrinsics[TY_NEVER];
    *constrained = true;
    return nullptr;
}

// tsc's removeConstrainedTypeVariables: `(T & "a") | (T & "b")` with `T extends "a" | "b"` is T. `types` is sorted
// by id, and stays so; returns the new length
static u32 remove_constrained_type_variables(Checker* me, Ty const** types, u32 len) {
    for (u32 v = 0; v < len; v++) {
        Ty const* t = types[v];
        if (t->kind != TY_INTERSECTION || !(t->xflags & TYX_CONSTRAINED_TYPE_VARIABLE) || t->composite.types.len != 2) continue;
        Ty const* variable = is_type_variable(t->composite.types.data[0]) ? t->composite.types.data[0] : t->composite.types.data[1];
        // The primitives it's intersected with in this union
        TyBuf primitives = {0};
        for (u32 i = 0; i < len; i++) {
            Ty const* u = types[i];
            if (u->kind != TY_INTERSECTION || !(u->xflags & TYX_CONSTRAINED_TYPE_VARIABLE) || u->composite.types.len != 2) continue;
            TyList pair = u->composite.types;
            if (pair.data[0] == variable) ty_buf_push(&primitives, pair.data[1]);
            else if (pair.data[1] == variable) ty_buf_push(&primitives, pair.data[0]);
        }
        Ty const* constraint = ty_base_constraint(me, variable);
        // (TYX_CONSTRAINED_TYPE_VARIABLE says it has one, unless that turned out circular since)
        if (!constraint) {
            ty_buf_drop(&primitives);
            continue;
        }
        TyList parts = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
        bool covered = true;
        for (u32 i = 0; i < parts.len && covered; i++) {
            bool found = false;
            for (u32 j = 0; j < primitives.len && !found; j++) found = primitives.data[j] == parts.data[i];
            covered = found;
        }
        if (covered) {
            u32 out = 0;
            for (u32 i = 0; i < len; i++) {
                Ty const* u = types[i];
                bool drop = u->kind == TY_INTERSECTION && (u->xflags & TYX_CONSTRAINED_TYPE_VARIABLE) &&
                    u->composite.types.len == 2 && (u->composite.types.data[0] == variable || u->composite.types.data[1] == variable);
                if (!drop && u != variable) types[out++] = u;
            }
            // (Sorted insertion)
            u32 at = out;
            while (at > 0 && ty_compare(types[at - 1], variable) > 0) {
                types[at] = types[at - 1];
                at--;
            }
            types[at] = variable;
            len = out + 1;
            v = (u32)-1; // Start over: the list changed
        }
        ty_buf_drop(&primitives);
    }
    return len;
}

static bool non_union_nullable(TyBuf const* buf) {
    for (u32 i = 0; i < buf->len; i++) if (buf->data[i]->kind != TY_UNION && (buf->data[i]->flags & TYF_NULLABLE)) return true;
    return false;
}

// What tsc's getIntersectionType finds never before it lets any decide: an object type next to null or undefined, two
// disjoint primitive domains (`string & number & any` is never). A union is in no domain there, but for boolean
static bool any_intersection_never(Checker* me, TyBuf const* buf) {
    enum { OBJECT = 1, NULLABLE = 2 };
    u32 domains = 0, kinds = 0;
    for (u32 i = 0; i < buf->len; i++) {
        Ty const* t = buf->data[i];
        if (t->kind == TY_UNION) {
            bool boolean = t->composite.types.len == 2 && (t->composite.types.data[0]->flags & TYF_BOOLEAN_LITERAL) &&
                (t->composite.types.data[1]->flags & TYF_BOOLEAN_LITERAL);
            if (boolean) domains |= TYF_BOOLEAN_LITERAL;
            continue;
        }
        if (t->flags & (TYF_OBJECT | TYF_NON_PRIMITIVE) || ty_is_empty_anonymous_object(me, t)) kinds |= OBJECT;
        if (t->flags & (TYF_UNDEFINED | TYF_NULL)) kinds |= NULLABLE;
        if (t->flags & TYF_NON_PRIMITIVE) domains |= TYF_NON_PRIMITIVE;
        if (t->flags & TYF_STRING_LIKE) domains |= TYF_STRING;
        else if (t->flags & (TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) domains |= TYF_NUMBER;
        else if (t->flags & (TYF_BIGINT | TYF_BIGINT_LITERAL)) domains |= TYF_BIGINT;
        else if (t->flags & TYF_BOOLEAN_LITERAL) domains |= TYF_BOOLEAN_LITERAL;
        else if (t->flags & TYF_ES_SYMBOL_LIKE) domains |= TYF_ES_SYMBOL;
        else if (t->flags & (TYF_VOID | TYF_UNDEFINED)) domains |= TYF_VOID;
        else if (t->flags & TYF_NULL) domains |= TYF_NULL;
    }
    if (compiler_options()->strict_null_checks && kinds == (OBJECT | NULLABLE)) return true;
    // (tsc's DisjointDomains: Null among them, but null & boolean only with one of the others named)
    u32 named = TYF_NON_PRIMITIVE | TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL | TYF_VOID;
    return (domains & named) && (domains & (domains - 1));
}

static Ty const* intersection(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args, u32 opts) {
    bool keep_empty_object = opts & INTERSECT_KEEP_EMPTY_OBJECT;
    u32 inherited = opts & INTERSECT_NO_CONSTRAINT_REDUCTION;
    if (len == 1) return types[0];
    for (u32 i = 0; i < len; i++) {
        if (types[i] == my intrinsics[TY_WILDCARD]) return types[i];
    }
    TyBuf buf = {0};
    ty_buf_add_flattened(&buf, TY_INTERSECTION, types, len);
    // The members of an intersection are regular (freshLiteralTypesInIntersections)
    for (u32 i = 0; i < buf.len; i++) buf.data[i] = ty_regular(me, buf.data[i]);
    // The missing type is undefined in an intersection, and what it leaves of undefined is missing again (tsc's
    // addTypeToIntersection with IncludesMissingType)
    bool has_missing = false;
    for (u32 i = 0; i < buf.len; i++) {
        if (buf.data[i] != my intrinsics[TY_MISSING]) continue;
        buf.data[i] = my intrinsics[TY_UNDEFINED];
        has_missing = true;
    }
    // (The same union twice is one member, before unions of primitives are intersected as sets, which would lose
    // its alias: tsc's addTypeToIntersection dedupes by identity. Unions told apart by their origin or alias only are
    // two members there, distributed: `U1 & U2` with two aliases of `A | B` is `A | B | (A & B)`, printed as its origin)
    u32 distinct = 0;
    for (u32 i = 0; i < buf.len; i++) {
        bool seen = false;
        for (u32 j = 0; j < distinct && !seen && buf.data[i]->kind == TY_UNION; j++) seen = buf.data[j] == buf.data[i];
        if (!seen) buf.data[distinct++] = buf.data[i];
    }
    buf.len = distinct;
    // (Not when never or any decides: tsc's getIntersectionType returns those before it looks at the unions,
    // `boolean & any & (string | undefined)` is any)
    bool decided = false;
    for (u32 i = 0; i < buf.len; i++) decided |= buf.data[i]->kind != TY_UNION && (buf.data[i]->flags & (TYF_NEVER | TYF_ANY));
    if (!decided) intersect_unions_of_primitives(me, &buf);
    if (buf.len == 1) {
        Ty const* only = buf.data[0] == my intrinsics[TY_UNDEFINED] && has_missing ? my intrinsics[TY_MISSING] : buf.data[0];
        ty_buf_drop(&buf);
        return only;
    }

    u32 includes = 0;
    bool includes_error = false;
    Ty const* first_union = nullptr;
    for (u32 i = 0; i < buf.len; i++) {
        Ty const* t = buf.data[i];
        includes |= t->flags;
        includes_error |= (t->flags & TYF_ERROR) != 0;
        if (t->kind == TY_UNION && !first_union) first_union = t;
    }

    // `{}` next to boolean (a union that is BooleanLike to tsc's includes, DefinitelyNonNullable) goes before the
    // unions are distributed: NonNullable<boolean> is boolean itself, no alias (tsc's getIntersectionType)
    if (first_union && !(includes & (TYF_NEVER | TYF_ANY)) && (compiler_options()->strict_null_checks || !(includes & TYF_NULLABLE))) {
        bool boolean = false;
        u32 empty = buf.len;
        for (u32 i = 0; i < buf.len; i++) {
            boolean |= buf.data[i]->kind == TY_UNION && (buf.data[i]->flags & TYF_BOOLEAN);
            if (empty == buf.len && buf.data[i]->kind != TY_UNION && ty_is_empty_anonymous_object(me, buf.data[i])) empty = i;
        }
        if (boolean && empty < buf.len && !keep_empty_object) {
            for (u32 i = empty; i + 1 < buf.len; i++) buf.data[i] = buf.data[i + 1];
            buf.len--;
            if (buf.len == 1) {
                Ty const* only = buf.data[0];
                ty_buf_drop(&buf);
                return only;
            }
        }
    }

    Ty const* ret = nullptr;
    if (includes & TYF_NEVER) {
        // (The silent never, what inference hasn't found yet, survives: tsc's getIntersectionType)
        bool silent = false;
        for (u32 i = 0; i < buf.len; i++) silent |= buf.data[i] == my intrinsics[TY_SILENT_NEVER];
        ret = my intrinsics[silent ? TY_SILENT_NEVER : TY_NEVER];
    } else if (includes & TYF_ANY) {
        ret = my intrinsics[any_intersection_never(me, &buf) ? TY_NEVER : includes_error ? TY_ERROR : TY_ANY];
    } else if (first_union && !compiler_options()->strict_null_checks && non_union_nullable(&buf)) {
        // Without strictNullChecks null or undefined decides before the unions are distributed: `undefined & ("a" |
        // "b")` is undefined, not never (tsc's getIntersectionType sees a union only as a union in its includes)
        // (boolean is a union that is BooleanLike to those includes, disjoint from a void-like undefined: `boolean &
        // undefined` is never)
        bool boolean = false, void_like = false;
        for (u32 i = 0; i < buf.len; i++) {
            boolean |= buf.data[i]->kind == TY_UNION && (buf.data[i]->flags & TYF_BOOLEAN);
            void_like |= buf.data[i]->kind != TY_UNION && (buf.data[i]->flags & (TYF_UNDEFINED | TYF_VOID));
        }
        u32 kept = 0;
        for (u32 i = 0; i < buf.len; i++) if (buf.data[i]->kind != TY_UNION) buf.data[kept++] = buf.data[i];
        ret = boolean && void_like ? my intrinsics[TY_NEVER] : intersection_cached(me, buf.data, kept, nullptr, (TyList){0}, inherited);
    } else if (first_union && (ret = intersect_unions(me, &buf, len, alias, alias_args, opts))) {
        // (Handled)
    } else if (first_union) {
        // tsc's getCrossProductIntersections: A & (B | C) & (D | E) is A & B & D | A & B & E | A & C & D | A & C & E,
        // remembering the intersection it came from when that is shorter (the origin)
        u32 combos = 1;
        for (u32 j = 0; j < buf.len; j++) if (buf.data[j]->kind == TY_UNION) combos *= buf.data[j]->composite.types.len;
        TyBuf results = {0};
        TyBuf parts = {0};
        bool any_intersection = false;
        for (u32 i = 0; i < combos; i++) {
            parts.len = 0;
            for (u32 j = 0; j < buf.len; j++) ty_buf_push(&parts, buf.data[j]);
            u32 n = i;
            for (u32 j = buf.len; j-- > 0;) {
                Ty const* t = buf.data[j];
                if (t->kind != TY_UNION) continue;
                u32 l = t->composite.types.len;
                parts.data[j] = t->composite.types.data[n % l];
                n /= l;
            }
            Ty const* r = intersection_cached(me, parts.data, parts.len, nullptr, (TyList){0}, inherited);
            if (r->flags & TYF_NEVER) continue;
            any_intersection |= r->kind == TY_INTERSECTION;
            ty_buf_push(&results, r);
        }
        ty_buf_drop(&parts);
        OriginSpec spec = { TY_INTERSECTION, buf.data, buf.len };
        bool use_origin = any_intersection &&
            constituent_count_of(results.data, results.len) > constituent_count_of(buf.data, buf.len);
        ret = results.len == 0 ? my intrinsics[TY_NEVER] :
            union_with_origin(me, results.data, results.len, alias, alias_args, use_origin ? &spec : nullptr);
        ty_buf_drop(&results);
    } else {
        // Insertion order is kept (it's observable in how tsc prints), duplicates and `unknown` go
        u32 out = 0;
        u32 family = 0;
        Ty const* literal = nullptr;
        bool is_never = false;
        bool seen_empty = false;
        // (Duplicates by a set of ids past a few members: type-fest intersects hundreds)
        u32 set_cap = 0;
        u32* set = nullptr;
        if (buf.len > 16) {
            set_cap = 32;
            while (set_cap < buf.len * 2) set_cap *= 2;
            set = calloc(set_cap, sizeof(u32));
            panic_if(!set, "out of memory");
        }
        for (u32 i = 0; i < buf.len && !is_never; i++) {
            Ty const* t = buf.data[i];
            if (t->flags & TYF_UNKNOWN) continue;
            bool duplicate = false;
            if (set) {
                u32 slot = (u32)(t->id * 0x9E3779B1u) & (set_cap - 1);
                while (set[slot] && set[slot] != t->id) slot = (slot + 1) & (set_cap - 1);
                duplicate = set[slot] == t->id;
                set[slot] = t->id;
            } else {
                for (u32 j = 0; j < out; j++) duplicate |= buf.data[j] == t;
            }
            if (duplicate) continue;
            // (Only the first empty object type: tsc's addTypeToIntersection, `C & {}` with `type C = {}` is C)
            if (ty_is_empty_anonymous_object(me, t)) {
                if (seen_empty) continue;
                seen_empty = true;
            }
            u32 t_family = primitive_family(t);
            // (void and undefined are both void-like, which aren't disjoint: `undefined & void` is undefined)
            if (t_family == TYF_UNDEFINED) t_family = TYF_VOID;
            if (t_family) {
                if (family && family != t_family) is_never = true;
                family = t_family;
                if (t->kind == TY_LITERAL) {
                    if (literal && literal != t) is_never = true;
                    literal = t;
                }
            }
            buf.data[out++] = t;
        }
        free(set);
        // `object` and a primitive (null and undefined included) are disjoint domains (tsc's getIntersectionType)
        if ((includes & TYF_NON_PRIMITIVE) && (family || (includes & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)))) {
            is_never = true;
        }
        // (A computed enum type is a unit type, but not a literal: `number & E` for an empty enum stays, tsc's
        // removeRedundantSupertypes only drops `number` next to a number literal)
        if (literal && !(literal->flags & TYF_ENUM)) {
            // "a" & string is "a"
            u32 kept = 0;
            for (u32 i = 0; i < out; i++) {
                if (buf.data[i]->kind == TY_INTRINSIC && primitive_family(buf.data[i])) continue;
                buf.data[kept++] = buf.data[i];
            }
            out = kept;
        }
        if (!is_never && (includes & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING))) {
            // A template or mapping and a primitive of another family is never, and one next to `string` is itself
            u32 kept = 0;
            for (u32 i = 0; i < out && !is_never; i++) {
                u32 f = primitive_family(buf.data[i]);
                if (f && f != TYF_STRING) is_never = true;
                if (buf.data[i]->kind == TY_INTRINSIC && f == TYF_STRING) continue;
                buf.data[kept++] = buf.data[i];
            }
            out = kept;
            // `get${string}` & "getX" is "getX", and with "setX" never (tsc's extractRedundantTemplateLiterals)
            if (literal && !is_never) is_never = extract_redundant_templates(me, buf.data, &out, literal);
        }
        // void next to undefined adds nothing (tsc's removeRedundantSupertypes)
        if (!is_never && family == TYF_VOID && (includes & TYF_UNDEFINED) && (includes & TYF_VOID)) {
            u32 kept = 0;
            for (u32 i = 0; i < out; i++) {
                if (!(buf.data[i]->kind == TY_INTRINSIC && (buf.data[i]->flags & TYF_VOID))) buf.data[kept++] = buf.data[i];
            }
            out = kept;
        }
        // Nothing is both an object and null or undefined
        bool strict = compiler_options()->strict_null_checks;
        if (strict && (includes & TYF_NULLABLE) && (includes & (TYF_OBJECT | TYF_NON_PRIMITIVE))) {
            is_never = true;
        }
        // `{}` next to something that is never null or undefined adds nothing (tsc's removeRedundantSupertypes):
        // NonNullable<1> is 1
        u32 empty_objects = 0, non_nullable = 0;
        for (u32 i = 0; i < out; i++) {
            if (ty_is_empty_anonymous_object(me, buf.data[i])) {
                empty_objects++;
            } else if (buf.data[i]->flags & (TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT |
                TYF_BIGINT_LITERAL | TYF_BOOLEAN | TYF_BOOLEAN_LITERAL | TYF_ENUM | TYF_ES_SYMBOL_LIKE | TYF_OBJECT |
                TYF_NON_PRIMITIVE)
            ) {
                non_nullable++;
            }
        }
        if (!is_never && empty_objects && non_nullable && !keep_empty_object) {
            u32 kept = 0;
            for (u32 i = 0; i < out; i++) {
                if (!ty_is_empty_anonymous_object(me, buf.data[i])) buf.data[kept++] = buf.data[i];
            }
            out = kept;
        }
        // Without strictNullChecks null and undefined are in every type: `T[] & null` is null (`{} & null` never)
        if (!strict && !is_never && (includes & TYF_NULLABLE)) {
            ret = my intrinsics[empty_objects ? TY_NEVER : includes & TYF_UNDEFINED ? TY_UNDEFINED : TY_NULL];
            ty_buf_drop(&buf);
            return ret;
        }
        if (is_never) {
            ret = my intrinsics[TY_NEVER];
        } else if (out == 0) {
            ret = my intrinsics[TY_UNKNOWN_];
        } else if (out == 1) {
            ret = buf.data[0] == my intrinsics[TY_UNDEFINED] && has_missing ? my intrinsics[TY_MISSING] : buf.data[0];
        } else {
            bool constrained = false;
            Ty const* reduced = out == 2 && !inherited ? reduce_type_variable_intersection(me, buf.data[0], buf.data[1], &constrained) : nullptr;
            if (reduced) {
                ret = reduced;
            } else {
                ret = intern_composite(me, TY_INTERSECTION, buf.data, out, alias, alias_args);
                if (constrained) ((Ty*)ret)->xflags |= TYX_CONSTRAINED_TYPE_VARIABLE;
            }
        }
    }
    ty_buf_drop(&buf);
    return ret;
}

// Type parameters

static Ty const* make_type_param(Checker* me, Symbol const* decl, Atom const* name) {
    Ty* ret = ty_alloc(me, TY_TYPE_PARAM, TYF_TYPE_PARAM | TYF_COULD_CONTAIN_TYPE_VARIABLES);
    ret->param.symbol = decl;
    ret->param.name = name;
    ret->param.state = arena_new(&my arena, TyParamState);
    return ret;
}

Ty const* ty_type_param(Checker* me, Symbol const* decl) {
    u64 key[2] = { KEY_TYPE_PARAM, (u64)(uintptr_t)decl };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) {
        Ty const* ret = make_type_param(me, decl, checker_atom(me, span_sv(decl->name_span)));
        // `infer U extends X`. Declared type parameters read their GenericItem (TyParamState.item), mapped type keys
        // get their constraint from whoever lowers the mapped type
        InferredType const* inferred = symbol_find_node(decl, SYMBOL_DECL_INFER);
        // (Of several `infer U`, the first that has one: tsc's getConstraintDeclaration)
        for (u32 i = 0; inferred && i < decl->decl_count; i++) {
            InferredType const* other = decl->decls[i].kind == SYMBOL_DECL_INFER ? decl->decls[i].node : nullptr;
            if (other && other->extends.tag != TYPE_ANY) {
                ret->param.state->constraint_node = &other->extends;
                break;
            }
        }
        // The key of a mapped type is constrained by what it iterates over (as declared: instantiations share it)
        InterfaceMember const* mapped = symbol_find_node(decl, SYMBOL_DECL_MAPPED_KEY);
        if (mapped) ret->param.state->constraint_node = &mapped->key.mapped->in_type;
        ret->param.state->is_infer = inferred != nullptr;
        ret->param.state->item = symbol_find_node(decl, SYMBOL_DECL_TYPE_PARAMETER);
        *slot = ret;
    }
    return *slot;
}

// tsgo's getDistributedTypeFromTypeParameter: a type parameter of its own, constrained to the original
Ty const* ty_distributed_type_param(Checker* me, Ty const* param) {
    param = ty_non_distributed(param);
    TyParamState* state = param->param.state;
    if (!state->distributed) {
        Ty* twin = (Ty*)make_type_param(me, param->param.symbol, param->param.name);
        twin->param.state->distributed_of = param;
        twin->param.state->constraint = param;
        twin->param.state->constraint_state = LAZY_STATE_DONE;
        twin->param.state->is_infer = state->is_infer;
        twin->param.state->circular = 1;
        state->distributed = twin;
    }
    return state->distributed;
}

Ty const* ty_synthetic_type_param(Checker* me, csview name) {
    return make_type_param(me, nullptr, checker_atom(me, name));
}

Ty* ty_clone_type_param(Checker* me, Ty const* param, TyMapper const* mapper, Atom const* name) {
    Ty* ret = (Ty*)make_type_param(me, param->param.symbol, name ? name : param->param.name);
    ret->param.state->clone_of = param;
    ret->param.state->clone_mapper = mapper;
    ret->param.state->is_infer = param->param.state->is_infer;
    return ret;
}

bool ty_param_is_bounded(Ty const* param) {
    TyParamState const* state = param->param.state;
    if (state->clone_of || state->is_this) return true;
    if (state->item) return state->item->extends_explicit || state->item->fallback_explicit;
    return state->constraint_node || state->constraint || state->fallback;
}

// tsc's isConstTypeVariable: a `const` type parameter, or a type built on one that stays generic, where an argument's
// literals are checked as if under `as const`
bool ty_is_const_type_variable(Checker* me, Ty const* ty, u32 depth) {
    if (!ty || depth >= 5) return false;
    switch (ty->kind) {
        case TY_TYPE_PARAM: {
            while (ty->param.state->clone_of) ty = ty->param.state->clone_of;
            return ty->param.state->item && ty->param.state->item->is_const;
        }
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (ty_is_const_type_variable(me, ty->composite.types.data[i], depth)) return true;
            }
            return false;
        case TY_INDEXED:
            return ty_is_const_type_variable(me, ty->indexed.object, depth + 1);
        case TY_CONDITIONAL:
            // `[T] extends [never] ? T : X` (of its constraint: the branches, or the distributive constraint)
            return ty_is_const_type_variable(me, ty_conditional_constraint(me, ty), depth + 1);
        case TY_SUBSTITUTION:
            return ty_is_const_type_variable(me, ty->substitution.base, depth);
        case TY_ANONYMOUS: {
            // A homomorphic mapped type over one (tsc's isConstMappedType). (Syntax first, as members are lazy)
            if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return false;
            Ty const* root = ty->anonymous.target ? ty->anonymous.target : ty;
            // (An instantiation expression's target is any object type)
            TypeNode const* node = root->kind == TY_ANONYMOUS ? root->anonymous.node : nullptr;
            if (!node || node->tag != TYPE_INTERFACE || vec_interface_member_size(&node->interface.members) != 1) return false;
            if (vec_interface_member_at(&node->interface.members, 0)->key.tag != INTERFACE_KEY_MAPPED) return false;
            MappedInfo const* mapped = ty_members(me, ty)->mapped;
            if (!mapped || mapped->constraint->kind != TY_KEYOF) return false;
            Ty const* variable = ty_actual_type_variable(me, mapped->constraint->keyof.operand);
            return variable->kind == TY_TYPE_PARAM && ty_is_const_type_variable(me, variable, depth);
        }
        case TY_REFERENCE: {
            // A generic tuple with a variadic element of one: `[...T]`
            if (!ty_is_generic_tuple(me, ty)) return false;
            TupleShape const* shape = ty_tuple_shape(ty);
            TyList args = ty_reference_args(me, ty);
            for (u32 i = 0; i < shape->arity && i < args.len; i++) {
                if ((shape->flags[i] & TUPLE_ELEMENT_REST) && args.data[i]->kind == TY_TYPE_PARAM &&
                    ty_is_const_type_variable(me, args.data[i], depth)) return true;
            }
            return false;
        }
        default:
            return false;
    }
}

// tsc's someType(ty, isMutableArrayLikeType): whether some member is assignable to `any[]` (null, undefined and any:
// false, tsc 6: `[...] as const` returned into an `any` context stays readonly)
static bool ty_some_mutable_array_like_at(Checker* me, Ty const* ty, u32 depth) {
    if (!ty) return false;
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (ty_some_mutable_array_like_at(me, ty->composite.types.data[i], depth)) return true;
        }
        return false;
    }
    if (ty->flags & (TYF_NULL | TYF_UNDEFINED | TYF_ANY | TYF_NEVER)) return false; // (never too: tsgo 7.1)
    // (Of the apparent type: a type parameter's constraint)
    if (ty->kind == TY_TYPE_PARAM) return depth < 8 && ty_some_mutable_array_like_at(me, ty_param_constraint(me, ty), depth + 1);
    return ty_is_related(me, ty, ty_array(me, ty_intrinsic(me, TY_ANY), false), TY_RELATION_ASSIGNABLE);
}

bool ty_some_mutable_array_like(Checker* me, Ty const* ty) {
    return ty_some_mutable_array_like_at(me, ty, 0);
}

// The `this` type of a class or interface: constrained to the type as seen from inside (applied to its own type
// parameters)
Ty const* ty_this_type_param(Checker* me, Ty const* declared) {
    Ty const* ret = make_type_param(me, nullptr, checker_atom(me, c_sv("this")));
    TyParamState* state = ret->param.state;
    state->is_this = true;
    state->constraint = declared->declared.type_params.len ?
        ty_reference(me, declared, declared->declared.type_params) : declared;
    state->constraint_state = LAZY_STATE_DONE;
    return ret;
}

// References

static Ty const* normalized_tuple(Checker* me, Ty const* target, TyList args);

Ty const* ty_reference_with_this(Checker* me, Ty const* target, TyList args, Ty const* this_arg) {
    check(target->kind == TY_DECLARED);
    // (A tuple's target has a this-type in tsc too: a tuple in an intersection sees the intersection as its `this`)
    bool tuple = target->declared.kind == TY_DECLARED_TUPLE_TARGET;
    if (!target->declared.this_param && !tuple) this_arg = nullptr;
    if (!target->declared.type_params.len && !this_arg) return target;
    check(args.len == target->declared.type_params.len);
    if (tuple) {
        // An optional element's type has its undefined (tsc's TupleNormalizer.add, addOptionality as a property's)
        TupleShape const* shape = target->declared.tuple;
        for (u32 i = 0; i < args.len; i++) {
            if (!(shape->flags[i] & TUPLE_ELEMENT_OPTIONAL)) continue;
            Ty const* added = ty_optional_prop_type(me, args.data[i]);
            if (added == args.data[i]) continue;
            Ty const* stack_args[16];
            Ty const** copy = args.len <= c_arraylen(stack_args) ? stack_args : malloc(sizeof(Ty const*) * args.len);
            panic_if(!copy, "out of memory");
            memcpy(copy, args.data, sizeof(Ty const*) * args.len);
            for (u32 j = i; j < args.len; j++) {
                if (shape->flags[j] & TUPLE_ELEMENT_OPTIONAL) copy[j] = ty_optional_prop_type(me, copy[j]);
            }
            Ty const* ret = ty_reference_with_this(me, target, (TyList){ .data = copy, .len = args.len }, this_arg);
            if (copy != stack_args) free(copy);
            return ret;
        }
        Ty const* normalized = normalized_tuple(me, target, args);
        if (normalized && this_arg && normalized->kind == TY_REFERENCE && ty_is_tuple(normalized)) {
            return ty_reference_with_this(me, normalized->reference.target, ty_reference_args(me, normalized), this_arg);
        }
        if (normalized) return normalized;
    }
    u64 stack_key[32];
    u32 count = 3 + args.len;
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    key[0] = KEY_REFERENCE;
    key[1] = target->id;
    key[2] = this_arg ? this_arg->id : 0;
    for (u32 i = 0; i < args.len; i++) key[3 + i] = (u64)(uintptr_t)args.data[i]; // (See intern_key_list())
    void const** slot = intern_slot(me, &my types, key, count);
    if (key != stack_key) free(key);
    if (!*slot) {
        u32 flags = ty_list_flags(args) | (this_arg ? this_arg->flags : 0);
        // Arrays and tuples of fresh object literals widen element-wise (tsc's getWidenedType)
        bool widens = target == my array_target || target == my readonly_array_target ||
            target->declared.kind == TY_DECLARED_TUPLE_TARGET;
        u32 kept = TYF_COULD_CONTAIN_TYPE_VARIABLES | TYF_NON_INFERRABLE | (widens ? TYF_FRESH_OBJECT : 0);
        Ty* ret = ty_alloc(me, TY_REFERENCE, TYF_OBJECT | (flags & kept));
        ret->reference.target = target;
        ret->reference.args = intern_key_list(slot, count, 3, args.len);
        ret->reference.this_arg = this_arg;
        ret->reference.from_type_node = my creating_from_type_node;
        if (!compiler_options()->strict_null_checks) {
            for (u32 i = 0; i < args.len; i++) ret->xflags |= args.data[i]->xflags;
        }
        ret->reference.state = arena_new(&my arena, TyObjectState);
        *slot = ret;
    }
    return *slot;
}

// A spread element whose type isn't known yet stays a spread (tsc's Variadic element)
static bool is_generic_spread(Checker* me, Ty const* ty) {
    if (ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED)) return true;
    return ty->kind == TY_ANONYMOUS && (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && ty_generic_mapped(me, ty);
}

typedef struct TupleBuilder {
    Ty const** types;
    unsigned char* flags;
    Atom const** names;
    void const** labels; // Null until some element has a label
    u32 len, cap;
    bool any_name, labels_any;
    i32 last_required, first_rest, last_optional_or_rest;
} TupleBuilder;

// Room for `cap` elements in all
static void tuple_builder_reserve(TupleBuilder* b, u32 cap) {
    if (cap <= b->cap) return;
    b->cap = cap;
    b->types = realloc(b->types, sizeof(Ty const*) * b->cap);
    b->flags = realloc(b->flags, b->cap);
    b->names = realloc(b->names, sizeof(Atom const*) * b->cap);
    if (b->labels) b->labels = realloc(b->labels, sizeof(void const*) * b->cap);
    panic_if(!b->types || !b->flags || !b->names || (b->len && b->labels_any && !b->labels), "out of memory");
}

static void tuple_builder_add(Checker* me, TupleBuilder* b, Ty const* type, unsigned char flags, Atom const* name, void const* label) {
    if (b->len == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->types = realloc(b->types, sizeof(Ty const*) * b->cap);
        b->flags = realloc(b->flags, b->cap);
        b->names = realloc(b->names, sizeof(Atom const*) * b->cap);
        if (b->labels) b->labels = realloc(b->labels, sizeof(void const*) * b->cap);
        panic_if(!b->types || !b->flags || !b->names || (b->len && b->labels_any && !b->labels), "out of memory");
    }
    bool rest = (flags & TUPLE_ELEMENT_REST) && !is_generic_spread(me, type);
    if (!(flags & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST))) b->last_required = (i32)b->len;
    if (rest && b->first_rest < 0) b->first_rest = (i32)b->len;
    if ((flags & TUPLE_ELEMENT_OPTIONAL) || rest) b->last_optional_or_rest = (i32)b->len;
    b->types[b->len] = type;
    b->flags[b->len] = flags;
    // (Labels only once one is known: most tuples have none)
    if (label && !b->labels) {
        b->labels = calloc(b->cap, sizeof(void const*));
        panic_if(!b->labels, "out of memory");
        b->labels_any = true;
    }
    if (b->labels) b->labels[b->len] = label;
    b->names[b->len++] = name;
    b->any_name |= name != nullptr;
}

// tuple_builder_add() of each element of a tuple: copied whole unless it has a rest element or labels a builder without
// any doesn't have room for (type-fest builds tuples of hundreds of elements one spread at a time)
static void tuple_builder_splice(Checker* me, TupleBuilder* b, Ty const* const* types, TupleShape const* inner) {
    u32 n = inner->arity;
    tuple_builder_reserve(b, b->len + n);
    bool simple = true;
    for (u32 j = 0; j < n && simple; j++) simple = !(inner->flags[j] & TUPLE_ELEMENT_REST);
    for (u32 j = 0; simple && !b->labels && inner->labels && j < n; j++) simple = !inner->labels[j];
    if (!simple) {
        for (u32 j = 0; j < n; j++) {
            tuple_builder_add(me, b, types[j], inner->flags[j], inner->names ? inner->names[j] : nullptr,
                inner->labels ? inner->labels[j] : nullptr);
        }
        return;
    }
    memcpy(b->types + b->len, types, sizeof(Ty const*) * n);
    memcpy(b->flags + b->len, inner->flags, n);
    if (inner->names) {
        memcpy(b->names + b->len, inner->names, sizeof(Atom const*) * n);
        for (u32 j = 0; j < n && !b->any_name; j++) b->any_name = inner->names[j] != nullptr;
    } else {
        memset(b->names + b->len, 0, sizeof(Atom const*) * n);
    }
    if (b->labels) {
        if (inner->labels) memcpy(b->labels + b->len, inner->labels, sizeof(void const*) * n);
        else memset(b->labels + b->len, 0, sizeof(void const*) * n);
    }
    for (u32 j = n; j-- > 0;) {
        if (!(inner->flags[j] & TUPLE_ELEMENT_OPTIONAL)) {
            b->last_required = (i32)(b->len + j);
            break;
        }
    }
    for (u32 j = n; j-- > 0;) {
        if (inner->flags[j] & TUPLE_ELEMENT_OPTIONAL) {
            b->last_optional_or_rest = (i32)(b->len + j);
            break;
        }
    }
    b->len += n;
}

// tsc's createNormalizedTupleType: spreads of tuples are spliced in, a spread of a union is a union of tuples, and
// everything from the first rest element to the last optional or rest one is one rest element. Null if there's
// nothing to do
static bool is_readonly_array(Checker* me, Ty const* ty) {
    return ty->kind == TY_REFERENCE && ty->reference.target == my readonly_array_target;
}

static Ty const* normalized_tuple(Checker* me, Ty const* target, TyList args) {
    TupleShape const* shape = target->declared.tuple;
    // A lone rest element of an array is that array (tsc's getTupleTargetType): `[...xs]` in a tuple context
    if (shape->arity == 1 && (shape->flags[0] & TUPLE_ELEMENT_REST) && (ty_is_array(me, args.data[0]) || is_readonly_array(me, args.data[0]))) {
        Ty const* element = ty_reference_args(me, args.data[0]).data[0];
        return ty_array(me, element, shape->readonly);
    }
    // A spread of any or unknown is a rest element of it (tsc's createNormalizedTupleType): `[...any]` is `any[]`
    if (shape->arity == 1 && (shape->flags[0] & TUPLE_ELEMENT_REST) && (args.data[0]->flags & (TYF_ANY | TYF_UNKNOWN))) {
        return ty_array(me, args.data[0], shape->readonly);
    }
    bool needed = false;
    bool past_rest = false; // Past a (non-generic) rest element: an optional or rest element after it merges into it
    for (u32 i = 0; i < shape->arity && !needed; i++) {
        if (past_rest && (shape->flags[i] & TUPLE_ELEMENT_OPTIONAL)) needed = true;
        if (!(shape->flags[i] & TUPLE_ELEMENT_REST)) continue;
        Ty const* arg = args.data[i];
        // (A rest element keeps only the element type of the array it spreads: `...readonly T[]` is `...T[]`)
        needed = ty_is_tuple(arg) || (arg->kind == TY_DECLARED && arg->declared.kind == TY_DECLARED_TUPLE_TARGET) ||
            (arg->flags & (TYF_ANY | TYF_UNKNOWN)) ||
            (arg->kind == TY_UNION && !(arg->flags & TYF_BOOLEAN)) || (arg->flags & TYF_NEVER) || is_readonly_array(me, arg);
        if (!is_generic_spread(me, arg)) {
            needed |= past_rest;
            past_rest = true;
        }
    }
    if (!needed) return nullptr;
    // (Distributed only while the cross product of the spread unions stays under 100000, else TS2590 and the union
    // stays spread: tsc's createNormalizedTupleType with checkCrossProductUnion)
    double cross = 1;
    for (u32 i = 0; i < shape->arity; i++) {
        Ty const* arg = args.data[i];
        if (!(shape->flags[i] & TUPLE_ELEMENT_REST)) continue;
        if (arg->flags & TYF_NEVER) cross = 0;
        else if (arg->kind == TY_UNION) cross *= arg->composite.types.len;
    }
    bool distribute = cross < 100000;
    if (!distribute) ty_error_at_current_node_d(me, 2590, "Expression produces a union type that is too complex to represent.");
    for (u32 i = 0; i < shape->arity && distribute; i++) {
        Ty const* arg = args.data[i];
        if (!(shape->flags[i] & TUPLE_ELEMENT_REST) || !((arg->kind == TY_UNION && !(arg->flags & TYF_BOOLEAN)) || (arg->flags & TYF_NEVER))) {
            continue;
        }
        // [A, ...(X | Y)] is [A, ...X] | [A, ...Y]
        if (arg->flags & TYF_NEVER) return arg;
        Ty const** replaced = malloc(sizeof(Ty const*) * args.len);
        panic_if(!replaced, "out of memory");
        memcpy(replaced, args.data, sizeof(Ty const*) * args.len);
        TyBuf results = {0};
        bool changed = false;
        for (u32 m = 0; m < arg->composite.types.len; m++) {
            replaced[i] = arg->composite.types.data[m];
            ty_buf_push(&results, ty_reference(me, target, (TyList){ .data = replaced, .len = args.len }));
            changed |= results.data[m] != arg->composite.types.data[m];
        }
        // (A union each of whose members is the tuple it makes is itself, alias and all: tsc's mapType)
        Ty const* ret = changed ? ty_union(me, results.data, results.len, nullptr, (TyList){0}) : arg;
        ty_buf_drop(&results);
        free(replaced);
        return ret;
    }
    TupleBuilder b = { .last_required = -1, .first_rest = -1, .last_optional_or_rest = -1 };
    tuple_builder_reserve(&b, shape->arity);
    for (u32 i = 0; i < shape->arity; i++) {
        Ty const* arg = args.data[i];
        Atom const* name = shape->names ? shape->names[i] : nullptr;
        void const* label = shape->labels ? shape->labels[i] : nullptr;
        if (!(shape->flags[i] & TUPLE_ELEMENT_REST)) {
            tuple_builder_add(me, &b, arg, shape->flags[i], name, label);
        } else if (ty_is_tuple(arg)) {
            TupleShape const* inner = arg->reference.target->declared.tuple;
            if (b.len + inner->arity >= 10000) {
                // (tsc's createNormalizedTupleType: at the current node, a type or an expression)
                if (my current_node.expr) {
                    ty_error_at_current_node_d(me, 2800, "Expression produces a tuple type that is too large to represent.");
                } else {
                    ty_error_at_current_node_d(me, 2799, "Type produces a tuple type that is too large to represent.");
                }
                free(b.types);
                free(b.flags);
                free(b.names);
                free(b.labels);
                return ty_intrinsic(me, TY_ERROR);
            }
            TyList elements = ty_reference_args(me, arg);
            tuple_builder_splice(me, &b, elements.data, inner);
        } else if (is_readonly_array(me, arg)) {
            tuple_builder_add(me, &b, ty_array(me, ty_reference_args(me, arg).data[0], false), TUPLE_ELEMENT_REST, name, label);
        } else if (arg->flags & (TYF_ANY | TYF_UNKNOWN)) {
            tuple_builder_add(me, &b, ty_array(me, arg, false), TUPLE_ELEMENT_REST, name, label);
        } else if (arg->kind == TY_UNION && !(arg->flags & TYF_BOOLEAN)) {
            // A union spread that wasn't distributed (TS2590): a rest element of its elements (tsc's
            // createNormalizedTupleType, getIndexedAccessType(t, number) of an array-like type)
            TyBuf elements = {0};
            for (u32 m = 0; m < arg->composite.types.len; m++) ty_buf_push(&elements, ty_rest_element_type(me, arg->composite.types.data[m]));
            Ty const* element = ty_union(me, elements.data, elements.len, nullptr, (TyList){0});
            ty_buf_drop(&elements);
            tuple_builder_add(me, &b, ty_array(me, element, false), TUPLE_ELEMENT_REST, name, label);
        } else if (!(arg->kind == TY_DECLARED && arg->declared.kind == TY_DECLARED_TUPLE_TARGET)) {
            tuple_builder_add(me, &b, arg, TUPLE_ELEMENT_REST, name, label);
        }
    }
    for (i32 i = 0; i < b.last_required; i++) b.flags[i] &= ~TUPLE_ELEMENT_OPTIONAL;
    if (b.first_rest >= 0 && b.first_rest < b.last_optional_or_rest) {
        TyBuf merged = {0};
        for (i32 i = b.first_rest; i <= b.last_optional_or_rest; i++) {
            Ty const* t = b.types[i];
            if (b.flags[i] & TUPLE_ELEMENT_REST) {
                t = is_generic_spread(me, t) ? ty_indexed(me, t, ty_intrinsic(me, TY_NUMBER)) : ty_rest_element_type(me, t);
            }
            ty_buf_push(&merged, t);
        }
        b.types[b.first_rest] = ty_array(me, ty_union(me, merged.data, merged.len, nullptr, (TyList){0}), false);
        b.flags[b.first_rest] = TUPLE_ELEMENT_REST;
        ty_buf_drop(&merged);
        u32 removed = (u32)(b.last_optional_or_rest - b.first_rest);
        for (u32 i = (u32)b.first_rest + 1; i + removed < b.len; i++) {
            b.types[i] = b.types[i + removed];
            b.flags[i] = b.flags[i + removed];
            b.names[i] = b.names[i + removed];
            if (b.labels) b.labels[i] = b.labels[i + removed];
        }
        b.len -= removed;
    }
    if (b.len == 1 && b.flags[0] == TUPLE_ELEMENT_REST && ty_is_array(me, b.types[0])) {
        // What's left is a lone rest element: the array (tsc's getTupleTargetType)
        Ty const* array = b.types[0];
        free(b.types);
        free(b.flags);
        free(b.names);
        free(b.labels);
        return shape->readonly ? ty_array(me, ty_reference_args(me, array).data[0], true) : array;
    }
    TupleShape normalized = {
        .arity = b.len,
        .readonly = shape->readonly,
        .flags = b.flags,
        .names = b.any_name ? b.names : nullptr,
        .labels = b.any_name ? b.labels : nullptr,
    };
    Ty const* new_target = ty_tuple_target(me, &normalized);
    Ty const* ret = b.len ? ty_reference(me, new_target, (TyList){ .data = b.types, .len = b.len }) : new_target;
    free(b.types);
    free(b.flags);
    free(b.names);
    free(b.labels);
    return ret;
}

Ty const* ty_reference(Checker* me, Ty const* target, TyList args) {
    return ty_reference_with_this(me, target, args, nullptr);
}

Ty const* ty_with_this_arg(Checker* me, Ty const* ty, Ty const* this_arg) {
    if (ty == this_arg) return ty;
    if (ty->kind == TY_INTERSECTION) {
        TyBuf parts = {0};
        bool changed = false;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* part = ty_with_this_arg(me, ty->composite.types.data[i], this_arg);
            changed |= part != ty->composite.types.data[i];
            ty_buf_push(&parts, part);
        }
        // (Itself, alias and all, when no member changes: tsc's sameMap)
        Ty const* ret = changed ? ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0}) : ty;
        ty_buf_drop(&parts);
        return ret;
    }
    if (ty->kind == TY_DECLARED && ty->declared.this_param) {
        return ty_reference_with_this(me, ty, ty->declared.type_params, this_arg);
    }
    // (A deferred reference, the body of an alias `type B<N> = Builder<{ name: N }>`, too: tsc's
    // getTypeWithThisArgument takes any reference's type arguments)
    if (ty->kind == TY_REFERENCE && ty->reference.target->declared.this_param) {
        if (ty->reference.this_arg == this_arg) return ty;
        return ty_reference_with_this(me, ty->reference.target, ty_reference_args(me, ty), this_arg);
    }
    // (A tuple's target has a this-type in tsc too: the reference it makes has no alias, `[string, number]`)
    if (ty->kind == TY_REFERENCE && ty->reference.target->declared.tuple) {
        if (ty->reference.this_arg == this_arg && !ty->alias) return ty;
        return ty_reference_with_this(me, ty->reference.target, ty_reference_args(me, ty), this_arg);
    }
    return ty;
}

static Ty const* make_synthetic_target(Checker* me, char const* name) {
    Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
    ret->declared.kind = TY_DECLARED_SYNTHETIC;
    ret->declared.name = checker_atom(me, csview_from(name));
    Ty const* param = ty_synthetic_type_param(me, c_sv("T"));
    ret->declared.type_params = ty_list_copy(me, &param, 1);
    ret->declared.state = arena_new(&my arena, TyObjectState);
    return ret;
}

static Ty const* global_generic_target(Checker* me, char const* name) {
    Symbol const* decl = program_get_global(my program, csview_from(name), SYMBOL_TYPE);
    if (decl && decl->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) {
        Ty const* declared = ty_declared_type_of(me, decl);
        if (declared->kind == TY_DECLARED && declared->declared.type_params.len == 1) {
            return declared;
        }
    }
    return make_synthetic_target(me, name);
}

// ReadonlyArray's target, looked up if no `readonly T[]` did yet: a written `ReadonlyArray<T>` is a readonly array
// (tsc looks the global types up when it starts)
Ty const* ty_readonly_array_target(Checker* me) {
    if (!my readonly_array_target) my readonly_array_target = global_generic_target(me, "ReadonlyArray");
    return my readonly_array_target;
}

Ty const* ty_array(Checker* me, Ty const* element, bool readonly) {
    Ty const** target = readonly ? &my readonly_array_target : &my array_target;
    if (!*target) {
        *target = global_generic_target(me, readonly ? "ReadonlyArray" : "Array");
        // tsc makes autoArrayType first thing: of the arrays it has the lowest id, so it's the `any[]` that survives
        // subtype reduction
        if (!readonly) {
            Ty const* auto_element = ty_intrinsic(me, TY_AUTO);
            my auto_array = ty_reference(me, *target, (TyList){ .data = &auto_element, .len = 1 });
        }
    }
    // A lib without ReadonlyArray reads it as Array (tsc's getGlobalReadonlyArrayType)
    if (readonly && (*target)->declared.kind == TY_DECLARED_SYNTHETIC) return ty_array(me, element, false);
    // Without a global Array (noLib) an array is the empty object type (tsc's createTypeFromGenericGlobalType of
    // emptyGenericType). (The auto array stays a reference: it is compared by identity)
    if ((*target)->declared.kind == TY_DECLARED_SYNTHETIC && element != ty_intrinsic(me, TY_AUTO)) return ty_empty_object_type(me);
    return ty_reference(me, *target, (TyList){ .data = &element, .len = 1 });
}

Ty const* ty_auto_array(Checker* me) {
    if (!my auto_array) ty_array(me, ty_intrinsic(me, TY_AUTO), false); // (Which makes it)
    return my auto_array;
}

Ty const* ty_tuple_target(Checker* me, TupleShape const* shape) {
    u64 stack_key[64];
    bool labels = false;
    for (u32 i = 0; shape->labels && shape->names && i < shape->arity && !labels; i++) labels = shape->labels[i] && shape->names[i];
    bool named = false;
    for (u32 i = 0; shape->names && i < shape->arity && !named; i++) named = shape->names[i] != nullptr;
    // (The flags 8 to a word, then the names and labels if there are any: type-fest's tuples have hundreds of elements)
    u32 flag_words = (shape->arity + 7) / 8;
    u32 count = 3 + flag_words + (named ? shape->arity : 0) + (labels ? shape->arity : 0);
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    key[0] = KEY_TUPLE_TARGET;
    key[1] = shape->arity | (u64)labels << 32 | (u64)named << 33;
    key[2] = shape->readonly;
    u64* at = key + 3;
    memset(at, 0, sizeof(u64) * flag_words);
    for (u32 i = 0; i < shape->arity; i++) at[i / 8] |= (u64)shape->flags[i] << (i % 8 * 8);
    at += flag_words;
    for (u32 i = 0; named && i < shape->arity; i++) *at++ = (u64)(uintptr_t)shape->names[i];
    for (u32 i = 0; labels && i < shape->arity; i++) *at++ = (u64)(uintptr_t)(shape->names[i] ? shape->labels[i] : nullptr);
    void const** slot = intern_slot(me, &my types, key, count);
    if (key != stack_key) free(key);
    if (!*slot) {
        if (my tuple_params.len < shape->arity) {
            u32 len = my tuple_params.len * 2 > shape->arity ? my tuple_params.len * 2 : shape->arity;
            Ty const** params = arena_new_n(&my arena, Ty const*, len);
            if (my tuple_params.len) memcpy(params, my tuple_params.data, sizeof(Ty const*) * my tuple_params.len);
            for (u32 i = my tuple_params.len; i < len; i++) params[i] = ty_synthetic_type_param(me, c_sv("T"));
            my tuple_params = (TyList){ .data = params, .len = len };
        }
        unsigned char* flags = arena_new_n(&my arena, unsigned char, shape->arity);
        Atom const** names = shape->names ? arena_new_n(&my arena, Atom const*, shape->arity) : nullptr;
        void const** target_labels = labels ? arena_new_n(&my arena, void const*, shape->arity) : nullptr;
        for (u32 i = 0; i < shape->arity; i++) {
            flags[i] = shape->flags[i];
            if (names) names[i] = shape->names[i];
            if (target_labels) target_labels[i] = shape->names[i] ? shape->labels[i] : nullptr;
        }
        Ty* ret = ty_alloc(me, TY_DECLARED, TYF_OBJECT);
        ret->declared.kind = TY_DECLARED_TUPLE_TARGET;
        ret->declared.state = arena_new(&my arena, TyObjectState);
        TupleShape* copy = arena_new(&my arena, TupleShape);
        copy->arity = shape->arity;
        copy->readonly = shape->readonly;
        copy->flags = flags;
        copy->names = names;
        copy->labels = target_labels;
        ret->declared.tuple = copy;
        ret->declared.type_params = (TyList){ .data = my tuple_params.data, .len = shape->arity };
        *slot = ret;
    }
    return *slot;
}

// Deferred types. The constructors resolve what they can and only make a deferred type for the rest

static Ty const* intern_deferred(Checker* me, u64 const* key, u32 count, TyKind kind, u32 operand_flags, Ty** fresh) {
    void const** slot = intern_slot(me, &my types, key, count);
    *fresh = nullptr;
    if (!*slot) {
        *fresh = ty_alloc(me, kind, TYF_DEFERRED | (operand_flags & TYF_COULD_CONTAIN_TYPE_VARIABLES));
        *slot = *fresh;
    }
    return *slot;
}

// tsc's getLiteralTypeFromProperty: a property declared with a numeric literal name (`{ 1: x }`) is keyed by the number
// literal, others by the name's string literal (or unique symbol)
Ty const* ty_prop_key_literal(Checker* me, Prop const* prop) {
    Prop const* declared = prop;
    while (declared->target && !declared->decl->decl_span_count && !prop_decl_rare(declared->decl)->name_type) declared = declared->target;
    if (prop_decl_rare(declared->decl)->name_type) return prop_decl_rare(declared->decl)->name_type; // (tsc's nameType, fresh if it was)
    csview text = span_sv(declared->decl->name_span);
    if (declared->decl->name_is_value && span_valid(declared->decl->name_span) && text.size &&
        ((text.buf[0] >= '0' && text.buf[0] <= '9') || text.buf[0] == '.' || text.buf[0] == '[')
    ) {
        // (A computed name is a value when its type is a number literal: tsc's nameType)
        csview name = atom_sv(prop->name);
        char buf[64];
        if (name.size > 0 && name.size < (isize_t)sizeof(buf)) {
            memcpy(buf, name.buf, (size_t)name.size);
            buf[name.size] = 0;
            char* end = nullptr;
            double n = strtod(buf, &end);
            if (end == buf + name.size) {
                Value value = value_number(n);
                return ty_literal(me, &value);
            }
        }
    }
    return ty_prop_name_literal(me, prop->name);
}

bool ty_prop_is_key(Checker* me, Prop const* prop) {
    return !(prop->flags & (PROP_PRIVATE | PROP_PROTECTED)) && !ty_private_name_class(me, prop->name);
}

Ty const* ty_prop_name_literal(Checker* me, Atom const* name) {
    Ty const* unique = ty_unique_symbol_of_name(me, name);
    if (unique) return unique;
    // (Keyed like literal_type() by the atom: a hit without cooking and hashing the text again)
    u64 key[4] = { KEY_LITERAL, VALUE_STRING, (u64)(uintptr_t)name, 0 };
    Ty const* known = intern_get(&my types, key, 4);
    if (known) return known;
    // (A private name's atom isn't the plain one of its text, also one no class declares, see ty_access_key_type())
    if (!name->len || name->str[0] != '#' || name == checker_atom(me, atom_sv(name))) return ty_string_literal(me, atom_sv(name));
    // A string literal of the private atom (keyed like literal_type(): the atom is none of its)
    void const** slot = intern_slot(me, &my types, key, 4);
    if (!*slot) {
        Ty* ret = ty_alloc(me, TY_LITERAL, TYF_STRING_LITERAL);
        ret->literal.value.tag = VALUE_STRING;
        ret->literal.value.string = cstr_null;
        ret->literal.string = name;
        *slot = ret;
    }
    return *slot;
}

bool ty_member_is_referenced(Checker* me, Span declaration) {
    u64 key[2] = { KEY_REFERENCED_MEMBER, (u64)(uintptr_t)declaration.buf };
    return intern_get(&my links, key, 2) != nullptr;
}

void ty_mark_member_referenced(Checker* me, Span declaration) {
    if (!span_valid(declaration)) return;
    u64 key[2] = { KEY_REFERENCED_MEMBER, (u64)(uintptr_t)declaration.buf };
    *intern_slot(me, &my links, key, 2) = me;
}

bool ty_first_check(Checker* me, void const* node) {
    u64 key[2] = { KEY_CHECKED_ONCE, (u64)(uintptr_t)node };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (*slot) return false;
    *slot = me;
    return true;
}

Atom const* ty_private_name(Checker* me, Symbol const* class_symbol, csview text) {
    Atom const* plain = checker_atom(me, text);
    u64 key[3] = { KEY_PRIVATE_NAME, (u64)(uintptr_t)class_symbol, (u64)(uintptr_t)plain };
    Atom const* found = intern_get(&my links, key, 3);
    if (found) return found;
    Atom* atom = arena_alloc(&my arena, sizeof(Atom) + (size_t)plain->len + 1);
    atom->len = plain->len;
    memcpy(atom->str, plain->str, plain->len);
    atom->str[plain->len] = 0;
    *intern_slot(me, &my links, key, 3) = atom;
    u64 back[2] = { KEY_PRIVATE_NAME_CLASS, (u64)(uintptr_t)atom };
    *intern_slot(me, &my links, back, 2) = class_symbol;
    return atom;
}

Symbol const* ty_private_name_class(Checker* me, Atom const* name) {
    if (!name->len || name->str[0] != '#') return nullptr;
    u64 key[2] = { KEY_PRIVATE_NAME_CLASS, (u64)(uintptr_t)name };
    return intern_get(&my links, key, 2);
}

Ty const* ty_unique_symbol_of_name(Checker* me, Atom const* name) {
    if (name->len < 4 || memcmp(name->str, "__@", 3) != 0) return nullptr;
    u64 key[2] = { KEY_UNIQUE_SYMBOL_NAME, (u64)(uintptr_t)name };
    return intern_get(&my links, key, 2);
}

// `keyof` without the key types of index signatures (tsc's getIndexType with IndexFlags.NoIndexSignatures): the
// property names of an object type. Anything generic, or a mapped type, is its ordinary keyof
Ty const* ty_keyof_without_index_signatures(Checker* me, Ty const* operand) {
    if (operand->kind == TY_UNION || operand->kind == TY_INTERSECTION) {
        if (operand->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) return ty_keyof(me, operand);
        TyBuf parts = {0};
        for (u32 i = 0; i < operand->composite.types.len; i++) {
            ty_buf_push(&parts, ty_keyof_without_index_signatures(me, operand->composite.types.data[i]));
        }
        Ty const* ret = operand->kind == TY_UNION ? ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0}) :
            ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    if (!(operand->flags & TYF_OBJECT) || ty_is_generic_tuple(me, operand)) {
        return ty_keyof(me, operand);
    }
    Members const* members = ty_members(me, operand);
    if (members->mapped || members_rare(members)->expanded_from) {
        // A mapped type's keys, less those that are index signatures, `string` (tsc's getIndexTypeForMappedType with
        // IndexFlags.NoIndexSignatures): `{ [P in string]: X }` has none but `number` (its addMemberForKeyType makes a
        // string key `string | number` before the filter)
        Ty const* keys = ty_keyof(me, operand);
        if (keys->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) return keys;
        TyList all = keys->kind == TY_UNION ? keys->composite.types : (TyList){ .data = &keys, .len = 1 };
        TyBuf kept = {0};
        bool changed = false;
        for (u32 i = 0; i < all.len; i++) {
            Ty const* key = all.data[i];
            if (key->kind == TY_INTRINSIC && (key->flags & (TYF_ANY | TYF_STRING))) {
                changed = true;
                if (key->flags & TYF_STRING) ty_buf_push(&kept, ty_intrinsic(me, TY_NUMBER));
            } else {
                ty_buf_push(&kept, key);
            }
        }
        Ty const* ret = !changed ? keys : ty_union(me, kept.data, kept.len, nullptr, (TyList){0});
        ty_buf_drop(&kept);
        return ret;
    }
    TyBuf parts = {0};
    for (u32 i = 0; i < members->prop_count; i++) {
        if (!ty_prop_is_key(me, members->props[i])) continue;
        ty_buf_push(&parts, ty_prop_key_literal(me, members->props[i]));
    }
    Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

// keyof `keyof T`: the names of the properties `string | number | symbol` has, which are those all of String, Number
// and Symbol have, Object's included (tsc's getLiteralTypeFromProperties of the apparent union; its index signatures,
// which only String has, aren't common)
static Ty const* keyof_keyof(Checker* me) {
    Ty const* apparent[3] = {
        ty_apparent_type(me, my intrinsics[TY_STRING]), ty_apparent_type(me, my intrinsics[TY_NUMBER]),
        ty_apparent_type(me, my intrinsics[TY_ES_SYMBOL]),
    };
    TyBuf parts = {0};
    for (u32 i = 0; i < 3; i++) {
        if (!(apparent[i]->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, apparent[i]);
        for (u32 j = 0; j < members->prop_count; j++) {
            Prop const* prop = members->props[j];
            if (ty_private_name_class(me, prop->name)) continue;
            bool common = true;
            for (u32 k = 0; k < 3 && common; k++) {
                common = k == i || !(apparent[k]->flags & TYF_OBJECT) || ty_get_object_prop(me, apparent[k], prop->name);
            }
            if (common) ty_buf_push(&parts, ty_prop_key_literal(me, prop));
        }
    }
    Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

static bool is_generic_reducible(Checker* me, Ty const* ty);
Ty const* ty_keyof(Checker* me, Ty const* operand) {
    if (operand->kind == TY_UNION || operand->kind == TY_INTERSECTION) operand = ty_reduced(me, operand); // (tsc's getIndexType)
    if (ty_is_no_infer(operand)) return ty_no_infer(me, ty_keyof(me, operand->substitution.base));
    if (operand == my intrinsics[TY_WILDCARD]) return operand; // (tsc's getIndexType)
    if (operand->flags & TYF_ANY) {
        Ty const* all[3] = { my intrinsics[TY_STRING], my intrinsics[TY_NUMBER], my intrinsics[TY_ES_SYMBOL] };
        return ty_union(me, all, 3, nullptr, (TyList){0});
    }
    // (A union an instantiation can take members out of stays `keyof` it: tsc's shouldDeferIndexType with
    // isGenericReducibleType, `keyof Gen<T>` with `Gen<T> = { v: T } & ({ v: A; a: X } | { v: B; b: Y })`)
    if (operand->kind == TY_UNION && is_generic_reducible(me, operand)) {
        u64 key[2] = { KEY_KEYOF, operand->id };
        Ty* fresh;
        Ty const* ret = intern_deferred(me, key, 2, TY_KEYOF, operand->flags, &fresh);
        if (fresh) fresh->keyof.operand = operand;
        return ret;
    }
    if (operand->kind == TY_UNION) {
        // keyof (A | B) is keyof A & keyof B
        TyBuf parts = {0};
        for (u32 i = 0; i < operand->composite.types.len; i++) {
            ty_buf_push(&parts, ty_keyof(me, operand->composite.types.data[i]));
        }
        Ty const* ret = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    // `keyof (T & {})` (NonNullable<T>, LowInfer<T>) stays as it is, like in tsc's shouldDeferIndexType
    bool defer = false;
    if (operand->kind == TY_INTERSECTION && (operand->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        bool has_variable = false, has_empty_object = false;
        for (u32 i = 0; i < operand->composite.types.len; i++) {
            Ty const* t = operand->composite.types.data[i];
            has_variable |= t->kind == TY_TYPE_PARAM || (t->flags & TYF_DEFERRED);
            if (t->kind == TY_ANONYMOUS) {
                Members const* members = ty_members(me, t);
                has_empty_object |= !members->prop_count && !members->call_count && !members->construct_count &&
                    !members->index_count && !members->mapped;
            }
        }
        defer = has_variable && has_empty_object;
    }
    if (operand->kind == TY_INTERSECTION && !defer) {
        TyBuf parts = {0};
        for (u32 i = 0; i < operand->composite.types.len; i++) {
            ty_buf_push(&parts, ty_keyof(me, operand->composite.types.data[i]));
        }
        Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        return ret;
    }
    // The property names of `List<T>` or `{ a: T }` don't depend on T: only type variables and generic mapped types
    // (members->mapped) defer, like tsc's isGenericObjectType
    bool known_names = !ty_is_generic_tuple(me, operand); // (tsc's shouldDeferIndexType: isGenericTupleType)
    if (!defer && (operand->flags & TYF_OBJECT) && known_names) {
        // A generic mapped type's keys are its constraint, read from the declaration without expanding it (tsc's
        // getIndexTypeForMappedType)
        if (operand->kind == TY_ANONYMOUS) {
            Ty const* root = operand->anonymous.target ? operand->anonymous.target : operand;
            TypeNode const* node = root->kind == TY_ANONYMOUS ? root->anonymous.node : nullptr;
            if (node && node->tag == TYPE_INTERFACE && vec_interface_member_size(&node->interface.members) == 1 &&
                vec_interface_member_at(&node->interface.members, 0)->key.tag == INTERFACE_KEY_MAPPED) {
                MappedInfo const* mapped = ty_mapped_info_unexpanded(me, operand);
                if (mapped && ty_is_generic_index(mapped->constraint)) {
                    // (With an `as` clause it stays `keyof` it, tsc 6.0's shouldDeferIndexType, see ty_simplified():
                    // expanding it to find that out relates its `as` clause, which may reach this very `keyof`)
                    if (!mapped->name_type) return mapped->constraint;
                    goto deferred;
                }
            }
        }
        Members const* members = ty_members(me, operand);
        // Of a mapped type without an `as` clause, its constraint (tsc's getIndexTypeForMappedType):
        // `keyof { [P in string]: X }` is string, `keyof Pick<T, unknown>` unknown
        if (members_rare(members)->expanded_from && !members_rare(members)->expanded_from->name_type) return members_rare(members)->expanded_from->constraint;
        if (members->mapped && !members->mapped->name_type) return members->mapped->constraint;
        // (Over no keys there are none, whatever the `as` clause: tsc's getIndexTypeForMappedType iterates the never
        // constraint, `keyof { [K in never as F<K, C>]: X }` with F generic in C is never)
        if (members->mapped && (members->mapped->constraint->flags & TYF_NEVER)) return my intrinsics[TY_NEVER];
        // (A generic mapped type with an `as` clause stays `keyof` it: tsc 6.0's shouldDeferIndexType, see ty_simplified())
        if (!members->mapped) {
            // Of a mapped type with an `as` clause over `keyof X`, the names X's keys map to (tsc's
            // getIndexTypeForMappedType), not the expanded members' keys: a name that is any makes it any, where the
            // members have a string index signature. Its constraint if that has the same members
            MappedInfo const* expanded = members_rare(members)->expanded_from;
            Ty const* names = expanded && expanded->name_type ? ty_mapped_type_name_keys(me, expanded) : nullptr;
            if (names) {
                if (names->kind == TY_UNION && expanded->constraint->kind == TY_UNION &&
                    names->composite.types.len == expanded->constraint->composite.types.len
                ) {
                    bool same = true;
                    for (u32 i = 0; i < names->composite.types.len && same; i++) {
                        same = names->composite.types.data[i] == expanded->constraint->composite.types.data[i];
                    }
                    if (same) return expanded->constraint;
                }
                return names;
            }
            // (Remembered per type: tsc builds it again each time, instantiations of `keyof X` over one big X ask
            // thousands of times, i18next's key paths)
            u64 cache_key[2] = { KEY_KEYOF_PROPERTIES, operand->id };
            Ty const* cached = intern_get(&my links, cache_key, 2);
            if (cached) return cached;
            TyBuf parts = {0};
            u32 entries = 0; // (tsc's list of one type per property and per index info: getUnionType returns one alone as it is)
            for (u32 i = 0; i < members->prop_count; i++) {
                if (!ty_prop_is_key(me, members->props[i])) continue;
                ty_buf_push(&parts, ty_prop_key_literal(me, members->props[i]));
                entries++;
            }
            // (An enum object's reverse mapping `[x: number]: string` is not a key: tsc's getLiteralTypeFromProperties
            // skips enumNumberIndexInfo)
            bool enum_object = operand->kind == TY_DECLARED && operand->declared.kind == TY_DECLARED_ENUM_OBJECT;
            for (u32 i = 0; i < members->index_count && !enum_object; i++) {
                entries++;
                ty_buf_push(&parts, members->index[i].key);
                if (members->index[i].key->flags & TYF_STRING) {
                    // Anything indexable by string is indexable by number
                    ty_buf_push(&parts, my intrinsics[TY_NUMBER]);
                }
            }
            // tsc's getLiteralTypeFromProperties: keyof a class, interface or reference, or of an aliased type,
            // remembers it (prints `keyof X`)
            // Of a mapped type with an `as` clause, the union of the names (tsc's getIndexTypeForMappedType), or its
            // constraint if that has the same members
            MappedInfo const* renamed = members_rare(members)->expanded_from && members_rare(members)->expanded_from->name_type ? members_rare(members)->expanded_from : nullptr;
            bool named = !renamed && entries != 1 && (operand->alias || operand->kind == TY_REFERENCE || (operand->kind == TY_DECLARED &&
                (operand->declared.kind == TY_DECLARED_INTERFACE || operand->declared.kind == TY_DECLARED_CLASS_INSTANCE)));
            OriginSpec spec = { TY_KEYOF, &operand, 1 };
            Ty const* ret = union_with_origin(me, parts.data, parts.len, nullptr, (TyList){0}, named ? &spec : nullptr);
            ty_buf_drop(&parts);
            if (renamed && ret->kind == TY_UNION && renamed->constraint->kind == TY_UNION &&
                ret->composite.types.len == renamed->constraint->composite.types.len
            ) {
                bool same = true;
                for (u32 i = 0; i < ret->composite.types.len && same; i++) {
                    same = ret->composite.types.data[i] == renamed->constraint->composite.types.data[i];
                }
                if (same) ret = renamed->constraint;
            }
            if (ty_members_done(operand)) *intern_slot(me, &my links, cache_key, 2) = ret;
            return ret;
        }
    }
    if (operand->flags & (TYF_NEVER)) {
        Ty const* all[3] = { my intrinsics[TY_STRING], my intrinsics[TY_NUMBER], my intrinsics[TY_ES_SYMBOL] };
        return ty_union(me, all, 3, nullptr, (TyList){0});
    }
    if (operand->flags & (TYF_UNKNOWN | TYF_NON_PRIMITIVE | TYF_VOID | TYF_NULLABLE)) {
        return my intrinsics[TY_NEVER];
    }
    // tsc only defers keyof an InstantiableNonPrimitive (shouldDeferIndexType): keyof `keyof T` is the keys its apparent
    // type `string | number | symbol` has (`"toString" | "valueOf" | "toLocaleString"`). Template literals and string
    // mappings are Strings below
    if (operand->kind == TY_KEYOF) return keyof_keyof(me);
    // keyof a primitive is the keys of its apparent type (`String`, `Number`...), without the `keyof String` origin
    if (operand->kind == TY_INTRINSIC || operand->kind == TY_LITERAL || operand->kind == TY_TEMPLATE_LITERAL ||
        operand->kind == TY_STRING_MAPPING
    ) {
        Ty const* apparent = ty_apparent_type(me, operand);
        if (apparent != operand && (apparent->flags & TYF_OBJECT)) {
            Ty const* keys = ty_keyof(me, apparent);
            if (keys->kind != TY_UNION) return keys;
            return ty_union(me, keys->composite.types.data, keys->composite.types.len, nullptr, (TyList){0});
        }
    }
deferred:;
    u64 key[2] = { KEY_KEYOF, operand->id };
    Ty* fresh;
    Ty const* ret = intern_deferred(me, key, 2, TY_KEYOF, operand->flags, &fresh);
    if (fresh) fresh->keyof.operand = operand;
    return ret;
}

// tsc's isGenericTupleType: a tuple with a variadic element, a spread of something generic (`[...T]`, not `[...T[]]`).
// A deferred reference whose arguments aren't resolved yet goes by its syntax, like tsc's tuple target does
bool ty_is_generic_tuple(Checker* me, Ty const* ty) {
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || !ty_is_tuple(ty)) return false;
    TyDeferredRef const* deferred = ty->reference.deferred;
    if (deferred && deferred->state != LAZY_STATE_DONE) {
        return deferred->node->tag == TYPE_TUPLE && ty_tuple_node_has_variadic(deferred->node);
    }
    TupleShape const* shape = ty_tuple_shape(ty);
    TyList args = ty_reference_args(me, ty);
    for (u32 i = 0; i < shape->arity && i < args.len; i++) {
        if (!(shape->flags[i] & TUPLE_ELEMENT_REST)) continue;
        Ty const* arg = args.data[i];
        if (arg->kind == TY_TYPE_PARAM || (arg->flags & TYF_DEFERRED)) return true;
        // (A generic mapped type is variadic too, tsc's createNormalizedTupleType with isGenericMappedType)
        if (arg->kind == TY_ANONYMOUS && (arg->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && ty_generic_mapped(me, arg)) return true;
    }
    return false;
}

// An intersection that some instantiation reduces to never: a property declared in two of its object members, a
// literal in one and a type parameter in the other (`{ src: "child" } & { src: TSrc }`). tsc's isReducibleIntersection
// instantiates the type parameters with a never that counts as a literal and asks getReducedType; this looks for the
// shape that makes that never
static bool is_reducible_intersection(Checker* me, Ty const* ty) {
    TyList parts = ty->composite.types;
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* a = parts.data[i];
        if (!is_known_object(me, a)) continue;
        Members const* members = ty_members(me, a);
        for (u32 p = 0; p < members->prop_count; p++) {
            Prop const* prop = members->props[p];
            Ty const* a_type = nullptr;
            for (u32 j = 0; j < parts.len; j++) {
                if (j == i || !is_known_object(me, parts.data[j])) continue;
                Prop const* other = ty_get_prop(me, parts.data[j], prop->name);
                if (!other || ((prop->flags & other->flags) & PROP_OPTIONAL)) continue;
                // (Without an optional one's undefined, as before plan 82; tsc's getReducedType of the never-filled
                // instantiation would see `"a" | undefined` reduce too)
                if (!a_type) a_type = (prop->flags & PROP_OPTIONAL) ? ty_without_undefined(me, prop_type(me, prop)) : prop_type(me, prop);
                if (!is_literal_like(a_type) || (a_type->flags & TYF_NULLABLE)) break;
                if (prop_type(me, other)->kind == TY_TYPE_PARAM) return true;
            }
        }
    }
    return false;
}

// tsc's isGenericReducibleType: a union an instantiation can take members out of stays generic, so that an indexed
// access of it is deferred and reduced once instantiated: `(({ src: "a"; v: A } | { src: "b"; v: B }) & { src: S })["v"]`
// with S = "a" is A, not A | B. An intersection is if it is reducible itself: `({ type: T } & { type: "P"; v: X })["v"]`
static bool is_generic_reducible(Checker* me, Ty const* ty) {
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return false;
    if (ty->kind == TY_INTERSECTION) return is_reducible_intersection(me, ty);
    if (ty->kind != TY_UNION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        Ty const* member = ty->composite.types.data[i];
        if (member->kind == TY_INTERSECTION && (member->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
            is_reducible_intersection(me, member)
        ) {
            return true;
        }
    }
    return false;
}

bool ty_is_generic_reducible(Checker* me, Ty const* ty) {
    return is_generic_reducible(me, ty);
}

// Returns null if the new core can't tell yet
// Generic as far as deferring an indexed access goes: a type variable at the top level (tsc's isGenericObjectType /
// isGenericIndexType). `{ a: T }["a"]` isn't, it's `T`
static bool is_generic_operand(Checker* me, Ty const* ty, u32 depth) {
    if (ty->kind == TY_SUBSTITUTION) {
        return is_generic_operand(me, ty->substitution.base, depth + 1) || is_generic_operand(me, ty->substitution.constraint, depth + 1);
    }
    if (ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED)) return true;
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || depth > 8) return false;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_generic_operand(me, ty->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    // A mapped type whose keys aren't known yet (from its declaration when it says so, as tsc's isGenericMappedType:
    // expanding a homomorphic one resolves its type variable's apparent type, typesaurus's
    // `AllRequired<AllRequired<Model>[K1]>[K2]` chains made 2x tsgo's types in date-fns/pkgs/docs)
    if (ty->kind == TY_ANONYMOUS) return ty_generic_mapped(me, ty) != nullptr;
    return ty_is_generic_tuple(me, ty);
}

// Every number the index can be names one of the leading fixed elements of a tuple
static bool index_less_than_fixed(Checker* me, Ty const* tuple, Ty const* index) {
    if (tuple->reference.deferred && tuple->reference.deferred->state != LAZY_STATE_DONE) return false;
    // (The leading and trailing fixed elements: tsc's getTotalFixedElementCount)
    TupleShape const* shape = ty_tuple_shape(tuple);
    u32 fixed = 0;
    while (fixed < shape->arity && !(shape->flags[fixed] & TUPLE_ELEMENT_REST)) fixed++;
    for (u32 i = shape->arity; i > fixed && !(shape->flags[i - 1] & TUPLE_ELEMENT_REST); i--) fixed++;
    Ty const* const* parts = &index;
    u32 count = 1;
    if (index->kind == TY_UNION) {
        parts = index->composite.types.data;
        count = index->composite.types.len;
    }
    for (u32 i = 0; i < count; i++) {
        double n;
        if (parts[i]->flags & TYF_NUMBER_LITERAL) {
            n = parts[i]->literal.value.number;
        } else if (parts[i]->flags & TYF_STRING_LITERAL) {
            // A string that is a numeric literal name counts too (tsc's indexTypeLessThan): a mapped type's key "0"
            Atom const* name = ty_literal_name(me, parts[i]);
            if (!name || !ty_is_numeric_name(name)) return false;
            csview text = atom_sv(name);
            char buf[64];
            if (text.size >= (isize)sizeof(buf)) return false;
            memcpy(buf, text.buf, (size_t)text.size);
            buf[text.size] = 0;
            n = strtod(buf, nullptr);
        } else {
            return false;
        }
        if (!(n >= 0 && n < fixed && n == (u32)n)) return false;
    }
    return true;
}

// Whether a mapped type's keys `constraint` make index signatures: string, number or symbol among them
static bool mapped_keys_have_index_signature(Ty const* constraint) {
    TyList keys = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
    for (u32 i = 0; i < keys.len; i++) {
        if (keys.data[i]->kind == TY_INTRINSIC && (keys.data[i]->flags & (TYF_STRING | TYF_NUMBER | TYF_ES_SYMBOL))) return true;
    }
    return false;
}

// What a never index reads before the general access (out of line: try_resolve_indexed is hot). Null: no shortcut
[[gnu::noinline, gnu::cold]] static Ty const* never_index_read(Checker* me, Ty const* object, Ty const* index) {
    // A never index on a tuple reads its number index signature, the union of its elements, without resolving the
    // tuple's (Array's) members: resolving them for every `[never][never]` made enough extra instantiations to push
    // xstate's recursive StateId into TS5115 without strictNullChecks (flagflip)
    // (Nor a mapped type over no keys, `{ [K in never]: StateId<...> }`: tsc never instantiates its template, which
    // resolving its members here would, xstate's recursive StateId then ran into TS5115 without strictNullChecks)
    // HACK: an anonymous object indexed by never is never, its members unresolved. tsc reads an index signature there
    // (`{ [k: string]: X }[never]` is X), but resolving the members of xstate's `{ [K in never]: StateId<...> }` (and
    // the other `{}`s on the way) instantiated enough to push its recursive StateId into TS5115 without
    // strictNullChecks (flagflip nosnc/sloppy: +7); tsc never instantiates a keyless mapped type's template. Only a
    // mapped type reads its index signature, its keys known from its declaration (instantiated, unexpanded):
    // `Record<string, T>[never]` is T (es-toolkit's sampleSize), `Partial<Record<string, T>>[never]` T | undefined
    // (Nothing read is the plain never, also for a silent never index, inference_mapper_no_default()'s nothing: tsc's
    // getPropertyTypeForIndexType returns neverType, which infers)
    Ty const* never = my intrinsics[TY_NEVER];
    if (object->kind == TY_ANONYMOUS) {
        if (!object->anonymous.mapped) return never;
        MappedInfo const* mapped = ty_mapped_info_unexpanded(me, object);
        if (!mapped || mapped->name_type || (mapped->constraint->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return never;
        return mapped_keys_have_index_signature(mapped->constraint) ? nullptr : never;
    }
    if (ty_is_tuple(object)) {
        TyList elements = ty_reference_args(me, object);
        TupleShape const* shape = ty_tuple_shape(object);
        TyBuf parts = {0};
        for (u32 i = 0; i < shape->arity && i < elements.len; i++) {
            Ty const* element = elements.data[i];
            if ((shape->flags[i] & TUPLE_ELEMENT_REST) && (ty_is_array(me, element) || ty_is_tuple(element))) {
                element = ty_is_array(me, element) ? ty_reference_args(me, element).data[0] : ty_indexed(me, element, ty_intrinsic(me, TY_NUMBER));
            } else if (shape->flags[i] & TUPLE_ELEMENT_REST) {
                ty_buf_drop(&parts);
                return ty_access(me, object, index);
            }
            ty_buf_push(&parts, element);
        }
        Ty const* ret = parts.len ? ty_union(me, parts.data, parts.len, nullptr, (TyList){0}) : never;
        ty_buf_drop(&parts);
        return ret;
    }
    return nullptr;
}

// The access itself is ty_access() (access.c), the same lookup property access expressions get
static Ty const* try_resolve_indexed(Checker* me, Ty const* object, Ty const* index, bool written, bool include_undefined) {
    // (An error index is any to tsgo's getIndexedAccessType: the index signatures say, `keyof Record<Unresolved, X>`)
    if (object->flags & TYF_ANY) return object;
    // (No shortcut for a never index: a generic object defers first, `(T | Record<string, T> | null)[never]`, and an
    // index signature takes never, `Record<string, T>[never]` is T: tsc's getIndexedAccessTypeOrUndefined and
    // getPropertyTypeForIndexType, which access() follows)
    // (Before the generic index check: `{ [k: string]: V }[K]` with K extends string is V, tsc's
    // getIndexedAccessTypeOrUndefined. Only an object, union or intersection can be one: this runs for every deferred
    // `T[K]`, the call alone cost react 0.3%)
    if ((object->flags & TYF_OBJECT) || object->kind == TY_UNION || object->kind == TY_INTERSECTION) {
        index = ty_string_index_only_key(me, object, index);
    }
    // An element before the variadic part of a generic tuple is known (`[S, ...T][0]` is S, tsc's indexTypeLessThan
    // the fixed elements)
    if (ty_is_tuple(object) && ty_is_generic_tuple(me, object) && index_less_than_fixed(me, object, index)) {
        return ty_access(me, object, index);
    }
    if (is_generic_operand(me, object, 0) || is_generic_operand(me, index, 0) || is_generic_reducible(me, object)) {
        // (`unknown[K]` with a generic K is unknown, like any's: tsc's getIndexedAccessTypeOrUndefined)
        return object->flags & TYF_UNKNOWN ? object : nullptr;
    }
    if (index->flags & TYF_NEVER) {
        Ty const* read = never_index_read(me, object, index);
        if (read) return read;
    }
    // A negative index of a tuple without a rest element written as a type is undefined (TS2514, tsc's
    // getPropertyTypeForIndexType with an access node)
    if (written && (index->flags & TYF_NUMBER_LITERAL) && index->literal.value.number < 0 && ty_is_tuple(object)) {
        TupleShape const* shape = object->reference.target->declared.tuple;
        if (ty_tuple_fixed_length(shape) == shape->arity) return my intrinsics[TY_UNDEFINED];
    }
    // Written as a type, a union with null or undefined has nothing to index: TS2537/TS2339 (check_indexed_access_part)
    // and the error type (tsc's getPropertyTypeForIndexType on the union, which ty_access() reads past)
    if (written && object->kind == TY_UNION) {
        for (u32 i = 0; i < object->composite.types.len; i++) {
            if (object->composite.types.data[i]->flags & TYF_NULLABLE) return my intrinsics[TY_ERROR];
        }
    }
    Ty const* ret = ty_access_without_node_include_undefined(me, object, index, include_undefined); // (A symbol key reads a string index signature, tsc reports TS2538 separately)
    // Past the end of tuples (TS2493 where written) is undefined (tsc's getTupleElementTypeOutOfStartCount)
    if (!ret && (((index->flags & TYF_NUMBER_LITERAL) && index->literal.value.number >= 0) ||
        ((index->flags & TYF_STRING_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) && index->literal.string &&
            ty_is_numeric_name(index->literal.string) && atom_sv(index->literal.string).buf[0] != '-'))
    ) {
        TyList parts = object->kind == TY_UNION ? object->composite.types : (TyList){ .data = &object, .len = 1 };
        bool tuples = parts.len > 0;
        for (u32 i = 0; i < parts.len && tuples; i++) tuples = ty_tuple_shape(parts.data[i]) != nullptr;
        if (tuples) return my intrinsics[TY_UNDEFINED];
    }
    // What isn't there is unknown, or the error type where it's written (tsc's getIndexedAccessType: the access node
    // reported it)
    return ret ? ret : my intrinsics[written ? TY_ERROR : TY_UNKNOWN_];
}

static Ty const* indexed(Checker* me, Ty const* object, Ty const* index, bool written, bool include_undefined) {
    if (object == my intrinsics[TY_WILDCARD] || index == my intrinsics[TY_WILDCARD]) return my intrinsics[TY_WILDCARD];
    index = ty_regular(me, index);
    Ty const* resolved = try_resolve_indexed(me, object, index, written, include_undefined);
    if (resolved) return resolved;
    u64 key[4] = { KEY_INDEXED, object->id, index->id, 1 };
    Ty* fresh;
    Ty const* ret = intern_deferred(me, key, include_undefined ? 4 : 3, TY_INDEXED, object->flags | index->flags, &fresh);
    if (fresh) {
        fresh->indexed.object = object;
        fresh->indexed.index = index;
        fresh->indexed.include_undefined = include_undefined;
    }
    return ret;
}

Ty const* ty_indexed(Checker* me, Ty const* object, Ty const* index) { return indexed(me, object, index, false, false); }

// tsc's getIndexedAccessTypeOrUndefined: null where the access finds nothing (ty_indexed() has unknown there)
Ty const* ty_indexed_or_null(Checker* me, Ty const* object, Ty const* index, bool include_undefined) {
    Ty const* ret = indexed(me, object, index, true, include_undefined);
    if (ret == my intrinsics[TY_ERROR] && !((object->flags | index->flags) & TYF_ERROR)) return nullptr;
    return ret;
}
Ty const* ty_indexed_written(Checker* me, Ty const* object, Ty const* index) { return indexed(me, object, index, true, false); }
Ty const* ty_indexed_include_undefined(Checker* me, Ty const* object, Ty const* index, bool include_undefined) {
    return indexed(me, object, index, false, include_undefined);
}

static Ty const* make_substitution(Checker* me, Ty const* base, Ty const* constraint) {
    u64 key[3] = { KEY_SUBSTITUTION, base->id, constraint->id };
    Ty* fresh;
    Ty const* ret = intern_deferred(me, key, 3, TY_SUBSTITUTION, base->flags | constraint->flags, &fresh);
    if (fresh) {
        fresh->substitution.base = base;
        fresh->substitution.constraint = constraint;
    }
    return ret;
}

Ty const* ty_substitution(Checker* me, Ty const* base, Ty const* constraint) {
    if ((constraint->flags & (TYF_ANY | TYF_UNKNOWN)) || constraint == base || (base->flags & TYF_ANY)) return base;
    return make_substitution(me, base, constraint);
}

// tsc's isNoInferTargetType: what could mention a type variable, without looking inside object types
static bool is_no_infer_target(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_no_infer_target(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    if (ty->kind == TY_SUBSTITUTION) return !ty_is_no_infer(ty) && is_no_infer_target(me, ty->substitution.base);
    if (ty->flags & TYF_OBJECT) return !ty_is_empty_anonymous_object(me, ty);
    if (ty->kind == TY_TYPE_PARAM || ty->kind == TY_KEYOF || ty->kind == TY_INDEXED || ty->kind == TY_CONDITIONAL) return true;
    if (ty->kind == TY_TEMPLATE_LITERAL || ty->kind == TY_STRING_MAPPING) return !ty_is_pattern_literal(ty);
    return false;
}

Ty const* ty_no_infer(Checker* me, Ty const* ty) {
    return is_no_infer_target(me, ty) ? make_substitution(me, ty, my intrinsics[TY_UNKNOWN_]) : ty;
}

Ty const* ty_substitution_intersection(Checker* me, Ty const* ty) {
    if (ty_is_no_infer(ty)) return ty->substitution.base;
    Ty const* both[2] = { ty->substitution.constraint, ty->substitution.base };
    return ty_intersection(me, both, 2, nullptr, (TyList){0});
}

Ty const* ty_actual_type_variable(Checker* me, Ty const* ty) {
    if (ty->kind == TY_SUBSTITUTION) return ty_actual_type_variable(me, ty->substitution.base);
    if (ty->kind == TY_INDEXED) {
        Ty const* object = ty_actual_type_variable(me, ty->indexed.object);
        Ty const* index = ty_actual_type_variable(me, ty->indexed.index);
        if (object == ty->indexed.object && index == ty->indexed.index) return ty;
        return ty_indexed(me, object, index);
    }
    return ty_non_distributed(ty); // (tsgo 7.1: a distributed type parameter is its original)
}

// What an instantiation makes of `O[K]`: like ty_indexed(), but a non-generic access that finds nothing is unknown
// (tsc's getIndexedAccessType without an access node; the error was the declaration's to report): falso's
// `O['length'] extends number ? T[] : T` with `O = { withAccents: false }` takes the false branch
Ty const* ty_indexed_instantiated(Checker* me, Ty const* object, Ty const* index, bool include_undefined) {
    Ty const* ret = indexed(me, object, index, false, include_undefined);
    if (ret->kind != TY_INDEXED || is_generic_operand(me, object, 0) || is_generic_operand(me, index, 0) ||
        is_generic_reducible(me, object)
    ) {
        return ret;
    }
    // (Only if it is this access that stayed deferred: a property whose type is an indexed access, `{ a: U["x"] }["a"]`,
    // resolved fine)
    if (is_generic_operand(me, ret->indexed.object, 0) || is_generic_operand(me, ret->indexed.index, 0) ||
        is_generic_reducible(me, ret->indexed.object)
    ) {
        return ret;
    }
    return my intrinsics[TY_UNKNOWN_];
}

// `x!`: the type without null and undefined. TODO: tsc intersects a type variable with `{}` under strictNullChecks
// (void isn't IsUndefined to tsc's getTypeFacts: a void callee has no call signatures, TS2349)
u32 ty_nullable_facts(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_UNDEFINED) return 1;
    if (ty->flags & TYF_NULL) return 2;
    if (ty->kind == TY_UNION) {
        u32 facts = 0;
        for (u32 i = 0; i < ty->composite.types.len; i++) facts |= ty_nullable_facts(me, ty->composite.types.data[i]);
        return facts;
    }
    if (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
        Ty const* constraint = ty_base_constraint(me, ty);
        return constraint && constraint != ty ? ty_nullable_facts(me, constraint) : 0;
    }
    return 0;
}

// (Without strictNullChecks only the null and undefined types themselves, tsc's getTypeFacts: `o.f()` where `f` is an
// index signature's `never` that noUncheckedIndexedAccess made undefined)
Ty const* ty_check_non_null_callee(Checker* me, Ty const* callee, Span span, ErrorCtx* errs) {
    if (!compiler_options()->strict_null_checks && !(callee->flags & (TYF_NULL | TYF_UNDEFINED))) return callee;
    u32 facts = ty_nullable_facts(me, callee);
    if (!facts) return callee;
    if (facts == 3) error_ctx_error_d(errs, span, 2723, "Cannot invoke an object which is possibly 'null' or 'undefined'.");
    else if (facts == 1) error_ctx_error_d(errs, span, 2722, "Cannot invoke an object which is possibly 'undefined'.");
    else error_ctx_error_d(errs, span, 2721, "Cannot invoke an object which is possibly 'null'.");
    // (void goes too: tsc's getNonNullableType removes what IsUndefinedOrNull says)
    Ty const* rest = ty_non_nullable(me, callee);
    return rest->flags & (TYF_NULLABLE | TYF_NEVER | TYF_VOID) ? ty_intrinsic(me, TY_ERROR) : rest;
}

// Whether a type may have the facts of null or undefined (and not be only null, undefined or void), by its flags
// alone: a plain object or primitive type can't
static bool may_be_nullish(Ty const* ty) {
    return ty->kind == TY_INTERSECTION ||
        (ty->flags & (TYF_UNKNOWN | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_INTERSECTION));
}

// tsc's getNonNullableType: without strictNullChecks nothing is nullable; with it, null, undefined and void go and a
// generic that may still be nullish is `NonNullable<T>` (`T & {}`)
Ty const* ty_non_nullable(Checker* me, Ty const* ty) {
    if (!compiler_options()->strict_null_checks) return ty;
    // (What plainly can or can't be nullish by its flags goes without the type facts, which resolve an object's
    // members: most callers pass an object type or an optional one's union)
    u32 const nullish = TYF_NULLABLE | TYF_VOID;
    if (ty->kind != TY_UNION) {
        if (may_be_nullish(ty)) return ty_non_nullish_part(me, ty);
        return ty->flags & nullish ? ty_intrinsic(me, TY_NEVER) : ty;
    }
    TyList members = ty->composite.types;
    for (u32 i = 0; i < members.len; i++) if (may_be_nullish(members.data[i])) return ty_non_nullish_part(me, ty);
    // (calloc: with ty_union_filtered inlined, gcc's -Wmaybe-uninitialized can't tell kept[0] is written when len is 1)
    Ty const** kept = calloc(members.len, sizeof(Ty const*));
    u32 len = 0;
    for (u32 i = 0; i < members.len; i++) if (!(members.data[i]->flags & nullish)) kept[len++] = members.data[i];
    Ty const* ret = ty_union_filtered(me, ty, kept, len);
    free(kept);
    return ret;
}

// Nothing but null and undefined, or never (tsc's `filterType(t, t => !!(t.flags & ~TypeFlags.Nullable)) === neverType`,
// what JavaScript widens to any)
bool ty_only_nullable(Ty const* ty) {
    if (ty->flags & (TYF_NULLABLE | TYF_NEVER)) return true;
    if (ty->kind != TY_UNION) return false;
    TyList members = ty->composite.types;
    for (u32 i = 0; i < members.len; i++) if (!(members.data[i]->flags & TYF_NULLABLE)) return false;
    return true;
}

Ty const* ty_without_undefined(Checker* me, Ty const* type) {
    if (type->kind != TY_UNION) return type->flags & TYF_UNDEFINED ? ty_intrinsic(me, TY_NEVER) : type;
    TyList members = type->composite.types;
    Ty const** kept = malloc(sizeof(Ty const*) * members.len);
    panic_if(!kept, "out of memory");
    u32 len = 0;
    for (u32 i = 0; i < members.len; i++) {
        if (!(members.data[i]->flags & TYF_UNDEFINED)) kept[len++] = members.data[i];
    }
    Ty const* ret = ty_union_filtered(me, type, kept, len);
    free(kept);
    return ret;
}

Ty const* ty_undefined_or_missing(Checker* me) {
    return my intrinsics[compiler_options()->exact_optional_property_types ? TY_MISSING : TY_UNDEFINED];
}

bool ty_contains_missing(Checker* me, Ty const* ty) {
    Ty const* missing = my intrinsics[TY_MISSING];
    return ty == missing || (ty->kind == TY_UNION && ty->composite.types.data[0] == missing);
}

Ty const* ty_remove_optional_marker(Checker* me, Ty const* ty) {
    Ty const* marker = my intrinsics[TY_OPTIONAL];
    if (ty == marker) return my intrinsics[TY_NEVER];
    if (ty->kind != TY_UNION) return ty;
    TyList members = ty->composite.types;
    // (The marker is an undefined: it sorts among the first members)
    u64 marker_key = ty_order_key(marker);
    for (u32 i = 0; i < members.len && ty_order_key(members.data[i]) <= marker_key; i++) {
        if (members.data[i] != marker) continue;
        Ty const** kept = malloc(sizeof(Ty const*) * members.len);
        panic_if(!kept, "out of memory");
        u32 len = 0;
        for (u32 j = 0; j < members.len; j++) if (j != i) kept[len++] = members.data[j];
        Ty const* ret = ty_union_filtered(me, ty, kept, len);
        free(kept);
        return ret;
    }
    return ty;
}

Ty const* ty_remove_missing(Checker* me, Ty const* ty, bool optional) {
    if (!optional || !compiler_options()->exact_optional_property_types) return ty;
    Ty const* missing = my intrinsics[TY_MISSING];
    if (ty == missing) return my intrinsics[TY_NEVER];
    if (ty->kind != TY_UNION || !ty_contains_missing(me, ty)) return ty;
    return ty_union_filtered(me, ty, ty->composite.types.data + 1, ty->composite.types.len - 1);
}

Ty const* ty_optional_prop_type(Checker* me, Ty const* ty) {
    if (!compiler_options()->strict_null_checks) return ty;
    Ty const* add = ty_undefined_or_missing(me);
    if (ty == add || (ty->kind == TY_UNION && ty->composite.types.data[0] == add)) return ty;
    return ty_union(me, (Ty const*[]){ ty, add }, 2, nullptr, (TyList){0});
}

Ty const* ty_cond_branch_type(Checker* me, TyCondBranch* branch) {
    if ((!branch->node && !branch->source) || branch->state == LAZY_STATE_DONE) return branch->type;
    if (branch->state == LAZY_STATE_IN_FLIGHT) {
        TAINT_EVENT(TAINT_IN_FLIGHT_OTHER);
        return ty_intrinsic(me, TY_ERROR);
    }
    branch->state = LAZY_STATE_IN_FLIGHT;
    Ty const* source = branch->node ? ty_from_node(me, branch->node, branch->ctx) : branch->source;
    branch->type = ty_instantiate_tail(me, source, branch->mapper);
    branch->state = LAZY_STATE_DONE;
    return branch->type;
}

static u32 cond_branch_key_len(TyCondBranch const* branch) {
    if (!branch->node && !branch->source) return 2;
    return 2 + (branch->mapper ? branch->mapper->len : 0);
}

static u32 cond_branch_key(u64* key, TyCondBranch const* branch) {
    if (!branch->node && !branch->source) {
        key[0] = branch->type->id;
        key[1] = 0;
        return 2;
    }
    u32 count = 0;
    key[count++] = branch->node ? (u64)(uintptr_t)branch->node : (u64)(uintptr_t)branch->source;
    key[count++] = branch->mapper ? branch->mapper->len : 0;
    for (u32 i = 0; branch->mapper && i < branch->mapper->len; i++) {
        key[count++] = ty_id_pair(branch->mapper->sources[i], branch->mapper->targets[i]);
    }
    return count;
}

// Resolves right away unless the check type is generic (conditional.c)
Ty const* ty_conditional(
    Checker* me, Ty const* check_type, Ty const* extends, TyCondBranch const* if_true, TyCondBranch const* if_false,
    Ty const* root, TyMapper const* mapper, bool distributive, bool check_tuples, TyList infers
) {
    if (root) check_tuples = root->conditional.check_tuples;
    if (check_type == my intrinsics[TY_WILDCARD] || extends == my intrinsics[TY_WILDCARD]) return my intrinsics[TY_WILDCARD];
    // (Only this resolution is for a constraint: what its branches instantiate isn't, tsc's forConstraint parameter)
    Ty const* constraint_root = root && root == my for_constraint_root ? root : nullptr;
    Ty const* saved_constraint_root = my for_constraint_root;
    my for_constraint_root = nullptr;
    Ty const* resolved = ty_conditional_resolve(me, ty_actual_type_variable(me, check_type), extends, infers, if_true, if_false, mapper,
        constraint_root, check_tuples);
    my for_constraint_root = saved_constraint_root;
    if (resolved) return resolved;
    u64 stack_key[64];
    u32 key_len = 3 + cond_branch_key_len(if_true) + cond_branch_key_len(if_false);
    u64* key = key_len <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * key_len);
    panic_if(!key, "out of memory");
    key[0] = KEY_CONDITIONAL;
    key[1] = ty_id_pair(check_type, extends);
    key[2] = (u64)(root ? root->id : 0) << 2 | (u64)check_tuples << 1 | distributive;
    u32 count = 3;
    count += cond_branch_key(key + count, if_true);
    count += cond_branch_key(key + count, if_false);
    u32 flags = check_type->flags | extends->flags | TYF_COULD_CONTAIN_TYPE_VARIABLES;
    Ty* fresh;
    Ty const* ret = intern_deferred(me, key, count, TY_CONDITIONAL, flags, &fresh);
    if (key != stack_key) free(key);
    if (fresh) {
        fresh->conditional.check = check_type;
        fresh->conditional.extends = extends;
        TyCondBranch* branches = arena_new_n(&my arena, TyCondBranch, 2);
        branches[0] = *if_false;
        branches[1] = *if_true;
        fresh->conditional.branches = branches;
        fresh->conditional.distributive = distributive;
        fresh->conditional.check_tuples = check_tuples;
        fresh->conditional.root = root ? root : fresh;
        fresh->conditional.mapper = root ? mapper : nullptr;
    }
    return ret;
}

void ty_resolving_grow(Checker* me) {
    my resolving_cap = my resolving_cap ? my resolving_cap * 2 : 64;
    my resolving = realloc(my resolving, sizeof(Resolving) * my resolving_cap);
    panic_if(!my resolving, "out of memory");
}

void ty_barrier_push(Checker* me, BarrierKind kind) {
    if (my barriers_len == my barriers_cap) {
        my barriers_cap = my barriers_cap ? my barriers_cap * 2 : 16;
        my barriers = realloc(my barriers, sizeof(Barrier) * my barriers_cap);
        panic_if(!my barriers, "out of memory");
    }
    Barrier barrier = { .resolving_len = my resolving_len, .outer_start = my resolving_start, .kind = kind };
    if (kind == BARRIER_NESTED_CALL) barrier.resolving_len = my resolving_start;
    if (kind != BARRIER_VARIANCE) my resolving_start = barrier.resolving_len;
    my barriers[my barriers_len++] = barrier;
}

void ty_barrier_pop(Checker* me) {
    Barrier const* barrier = &my barriers[--my barriers_len];
    if (barrier->kind != BARRIER_VARIANCE) my resolving_start = barrier->outer_start;
}

u32 ty_barrier_outer_call_start(Checker* me) {
    for (u32 i = my barriers_len; i-- > 0;) {
        if (my barriers[i].kind != BARRIER_VARIANCE) return my barriers[i].outer_start;
    }
    return 0;
}

bool ty_resolving_reenters(Checker* me, ResolvingKind kind, void const* key) {
    u32 start = my resolving_start;
    if (kind == RESOLVING_ALIAS) {
        u32 i = my barriers_len;
        while (i-- > 0 && my barriers[i].kind != BARRIER_VARIANCE) {}
        if (i == UINT32_MAX) return false;
        start = my barriers[i].resolving_len;
    }
    for (u32 i = my resolving_len; i-- > start;) {
        if (my resolving[i].key == key && my resolving[i].kind == kind) return false;
    }
    return true;
}

Resolving* ty_in_flight_entry(Checker* me, ResolvingKind kind, void const* key) {
    for (u32 i = my resolving_len; i-- > 0;) {
        if (my resolving[i].key == key && my resolving[i].kind == kind) return &my resolving[i];
    }
    return nullptr;
}

void ty_debug_reentry(Checker* me, char const* event, Span at) {
    if (!span_valid(at)) at = my current_node.span;
    if (!span_valid(at)) {
        fprintf(stderr, "TTTT %s\n", event);
        return;
    }
    cstr const* path = &span_source(at)->canonical_path;
    fprintf(stderr, "TTTT %s %s:%u\n", event, cstr_str(path), span_line_index(at) + 1);
}

void ty_resolving_cycle(Checker* me, ResolvingKind kind, void const* key) {
    for (u32 i = my resolving_len; i-- > my resolving_start;) {
        if (my resolving[i].key == key && my resolving[i].kind == kind) {
            TAINT_EVENT(TAINT_CYCLE);
            for (u32 j = i; j < my resolving_len; j++) my resolving[j].circular = true;
            return;
        }
    }
}

void ty_resolving_cycle_across_calls(Checker* me, ResolvingKind kind, void const* key) {
    for (u32 i = my resolving_len; i-- > 0;) {
        if (my resolving[i].key == key && my resolving[i].kind == kind) {
            TAINT_EVENT(TAINT_CYCLE);
            for (u32 j = i; j < my resolving_len; j++) my resolving[j].circular = true;
            return;
        }
    }
}

void ty_resolving_pattern_cycle(Checker* me, void const* key, Binding const* pattern) {
    for (u32 i = my resolving_len; i-- > my resolving_start;) {
        if (my resolving[i].key != key || my resolving[i].kind != RESOLVING_PATTERN) continue;
        // From the first of its names resolved since: tsc re-enters that one (the pattern's type isn't memoized, nor
        // pushed: without one of its names in between, there's no cycle)
        for (u32 j = i + 1; j < my resolving_len; j++) {
            if (my resolving[j].kind != RESOLVING_BINDING) continue;
            Binding const* root = my resolving[j].key;
            while (root->parent) root = root->parent;
            if (root != pattern) continue;
            TAINT_EVENT(TAINT_CYCLE);
            for (u32 k = j; k < my resolving_len; k++) my resolving[k].circular = true;
            return;
        }
        return;
    }
}
