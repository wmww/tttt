// madvise(), which strict -std=c2x hides
#define _DEFAULT_SOURCE
#include "ast_alloc.h"
#include <pthread.h>
#include <sys/mman.h>
#include <stdatomic.h>

// Address space comes in 256 MB segments (reserved, untouched pages cost nothing), marked in ast_segment_map so that
// ast_owns() is one load. Threads take 64 MB slabs out of the segments and bump each file they parse out of their
// slab, so a thread's files lie end to end and small files share pages; a file's arena is the ranges of a slab it
// took. A dropped arena's whole pages go back to the system and its addresses are never handed out again.
#define AST_SEGMENT ((size_t)1 << AST_SEGMENT_SHIFT)
#define AST_SLAB ((size_t)64 << 20)
#define AST_PAGE ((uintptr_t)4096)
#define AST_POPULATE ((size_t)2 << 20)
// Freed blocks up to this size are kept by size class (bigger ones by power of two)
#define AST_FREE_MAX 1024
static_assert(AST_FREE_MAX / 8 + 1 <= 3 * 64);

uint8_t ast_segment_map[(size_t)1 << (47 - AST_SEGMENT_SHIFT)];

struct AstRange {
    char* start;
    char* end;
};

static pthread_mutex_t segment_lock = PTHREAD_MUTEX_INITIALIZER;
static char* segment_pos;
static char* segment_end;

typedef struct LargeBlock LargeBlock;
struct LargeBlock {
    LargeBlock* next;
    size_t size;
};

// Bytes (TTTT_AST_STATS=1 prints them at exit)
typedef struct AstStats {
    size_t bumped, reused, lost_small, lost_large;
} AstStats;
static AstStats all_stats;
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct AstSlab {
    char* pos;
    char* end;
    char* populated; // The slab's pages below this are faulted in
} AstSlab;

typedef struct AstThread {
    AstArena* arena; // The file being parsed or the bodies being materialized, or null
    char* pos; // The thread's slab
    char* end;
    char* range_start; // Where the arena's latest range starts in the slab
    char* populated; // The slab's pages below this are faulted in
    AstSlab other; // The slab not in use: materializations' while parsing files, files' while materializing
    AstSlab lasting; // Lasting materializations' (ast_materialize_begin()), swapped like `other`
    AstSlab* swapped; // Materializing: where the files' slab is meanwhile
    bool no_huge; // Materializing a lasting materialization: its pages are given back one by one
    char* cursor_chunk; // The materialization slab's chunk the next materialization starts in, which it holds
    void* free_lists[AST_FREE_MAX / 8 + 1];
    u64 small_mask[3]; // Which free_lists aren't empty (AST_FREE_MAX / 8 + 1 bits)
    // Bigger freed blocks (outgrown lists, mostly) by floor(log2(size)), split to serve any allocation
    LargeBlock* large[64];
    u64 large_mask;
    AstStats stats;
} AstThread;

static thread_local AstThread ast_thread;

static inline size_t round8(isize size) {
    return ((size_t)(size > 0 ? size : 1) + 7) & ~(size_t)7;
}

// A slab of at least `size` bytes (the caller holds nothing)
static char* take_slab(size_t size, char** end) {
    size = (size + AST_POPULATE - 1) & ~(AST_POPULATE - 1); // (Slabs stay 2 MB aligned)
    pthread_mutex_lock(&segment_lock);
    if ((size_t)(segment_end - segment_pos) < size) {
        size_t segment = size > AST_SEGMENT ? (size + AST_SEGMENT - 1) & ~(AST_SEGMENT - 1) : AST_SEGMENT;
        // Aligned to the segment size: map twice as much and cut the ends off
        char* raw = mmap(nullptr, segment + AST_SEGMENT, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        panic_if(raw == MAP_FAILED, "Couldn't reserve address space for the AST");
        char* start = (char*)(((uintptr_t)raw + AST_SEGMENT - 1) & ~(uintptr_t)(AST_SEGMENT - 1));
        if (start > raw) munmap(raw, (size_t)(start - raw));
        if (start + segment < raw + segment + AST_SEGMENT) munmap(start + segment, (size_t)(raw + segment + AST_SEGMENT - (start + segment)));
        for (uintptr_t i = (uintptr_t)start >> AST_SEGMENT_SHIFT; i < ((uintptr_t)start + segment) >> AST_SEGMENT_SHIFT; i++) {
            check(i < sizeof(ast_segment_map));
            ast_segment_map[i] = 1;
        }
        // (The rest of the previous segment, less than a slab, is lost)
        segment_pos = start;
        segment_end = start + segment;
    }
    char* ret = segment_pos;
    segment_pos += size;
    *end = segment_pos;
    pthread_mutex_unlock(&segment_lock);
    return ret;
}

// A materialization slab's 2 MB chunks are given back whole, when the last materialization in one is dropped, so that
// they can be huge pages: dropping part of a huge page (MADV_DONTNEED) splits it, and the kernel frees its memory only
// at its deferred split, under memory pressure. Per segment, a count per chunk: the materializations with a range in it,
// plus one while a thread's next materialization may start in it (AstThread.cursor_chunk); CHUNK_HUGE when madvised
#define CHUNK_HUGE ((u32)1 << 31)
static _Atomic(u32)* _Atomic chunk_counts[(size_t)1 << (47 - AST_SEGMENT_SHIFT)];

static inline char* chunk_of(char const* p) {
    return (char*)((uintptr_t)p & ~(uintptr_t)(AST_POPULATE - 1));
}

static _Atomic(u32)* chunk_count(char const* chunk) {
    uintptr_t segment = (uintptr_t)chunk >> AST_SEGMENT_SHIFT;
    _Atomic(u32)* counts = atomic_load_explicit(&chunk_counts[segment], memory_order_acquire);
    if (!counts) {
        _Atomic(u32)* fresh = calloc(AST_SEGMENT / AST_POPULATE, sizeof(u32));
        check(fresh);
        if (atomic_compare_exchange_strong(&chunk_counts[segment], &counts, fresh)) counts = fresh;
        else free(fresh);
    }
    return &counts[((uintptr_t)chunk & (AST_SEGMENT - 1)) / AST_POPULATE];
}

static void chunk_mark_huge(char const* chunk) {
    atomic_fetch_or_explicit(chunk_count(chunk), CHUNK_HUGE, memory_order_relaxed);
}

// Adds delta to the count of every chunk [start, end) touches; a chunk whose count drops to zero is given back
static void chunks_ref(char const* start, char const* end, int delta) {
    if (end <= start) return;
    for (char* chunk = chunk_of(start); chunk < end; chunk += AST_POPULATE) {
        _Atomic(u32)* count = chunk_count(chunk);
        u32 old = atomic_fetch_add_explicit(count, (u32)delta, memory_order_acq_rel);
        if (delta > 0 || (old & ~CHUNK_HUGE) != 1) continue;
        if (!debug_env.ast_poison) madvise(chunk, AST_POPULATE, MADV_DONTNEED);
        if (old & CHUNK_HUGE) huge_pages_give(AST_POPULATE);
    }
}

static void add_range(AstArena* arena, char* start, char* end) {
    if (start == end) return;
    if (arena->range_count > arena->ranges_counted && arena->ranges[arena->range_count - 1].end == start) {
        arena->ranges[arena->range_count - 1].end = end;
        return;
    }
    if (arena->range_count == arena->range_cap) {
        arena->range_cap = arena->range_cap ? arena->range_cap * 2 : 2;
        arena->ranges = realloc(arena->ranges, sizeof(AstRange) * arena->range_cap);
        check(arena->ranges);
    }
    arena->ranges[arena->range_count++] = (AstRange){ start, end };
}

void ast_arena_begin(AstArena* arena) {
    AstThread* t = &ast_thread;
    if (debug_env.ast_malloc) return;
    check(!t->arena);
    t->arena = arena;
    t->range_start = t->pos;
}

// (The free lists of a file or a materialization's parse are its own blocks)
static void clear_free_lists(AstThread* t) {
    if (debug_env.ast_stats) {
        for (u32 i = 0; i <= AST_FREE_MAX / 8; i++) {
            for (void* b = t->free_lists[i]; b; b = *(void**)b) t->stats.lost_small += i * 8;
        }
        for (u32 i = 0; i < 64; i++) {
            for (LargeBlock* b = t->large[i]; b; b = b->next) t->stats.lost_large += b->size;
        }
    }
    memset(t->free_lists, 0, sizeof(t->free_lists));
    memset(t->small_mask, 0, sizeof(t->small_mask));
    memset(t->large, 0, sizeof(t->large));
    t->large_mask = 0;
}

static void swap_slabs(AstThread* t, AstSlab* other) {
    AstSlab current = { t->pos, t->end, t->populated };
    t->pos = other->pos;
    t->end = other->end;
    t->populated = other->populated;
    *other = current;
}

void ast_materialize_begin(AstArena* arena, bool lasting) {
    AstThread* t = &ast_thread;
    if (debug_env.ast_malloc) return;
    check(!t->arena);
    t->swapped = lasting ? &t->lasting : &t->other;
    t->no_huge = lasting;
    swap_slabs(t, t->swapped);
    arena->materialization = true;
    arena->lasting = lasting;
    // (A page of its own: ast_arena_drop() gives back every page it touched)
    char* aligned = (char*)(((uintptr_t)t->pos + AST_PAGE - 1) & ~(AST_PAGE - 1));
    t->pos = aligned <= t->end ? aligned : t->end;
    t->arena = arena;
    t->range_start = t->pos;
}

void ast_materialize_end(void) {
    AstThread* t = &ast_thread;
    if (debug_env.ast_malloc) return;
    check(t->arena && t->arena->materialization);
    add_range(t->arena, t->range_start, t->pos);
    clear_free_lists(t);
    if (debug_env.ast_stats) {
        pthread_mutex_lock(&stats_lock);
        all_stats.bumped += t->arena->total;
        pthread_mutex_unlock(&stats_lock);
    }
    for (u32 i = t->arena->ranges_counted; i < t->arena->range_count && !t->no_huge; i++) {
        chunks_ref(t->arena->ranges[i].start, t->arena->ranges[i].end, 1);
    }
    t->arena->ranges_counted = t->arena->range_count;
    // (The chunk the slab continues in stays until the thread's next materialization is past it)
    char* cursor = t->pos < t->end ? chunk_of(t->pos) : nullptr;
    if (!t->no_huge && cursor != t->cursor_chunk) {
        if (cursor) chunks_ref(cursor, cursor + 1, 1);
        if (t->cursor_chunk) chunks_ref(t->cursor_chunk, t->cursor_chunk + 1, -1);
        t->cursor_chunk = cursor;
    }
    t->arena = nullptr;
    t->no_huge = false;
    swap_slabs(t, t->swapped);
}

void ast_arena_end(void) {
    AstThread* t = &ast_thread;
    if (debug_env.ast_malloc) return;
    check(t->arena);
    add_range(t->arena, t->range_start, t->pos);
    // (What is still on the free lists is lost: the next file mustn't get blocks of this one)
    if (debug_env.ast_stats) {
        for (u32 i = 0; i <= AST_FREE_MAX / 8; i++) {
            for (void* b = t->free_lists[i]; b; b = *(void**)b) t->stats.lost_small += i * 8;
        }
        for (u32 i = 0; i < 64; i++) {
            for (LargeBlock* b = t->large[i]; b; b = b->next) t->stats.lost_large += b->size;
        }
        pthread_mutex_lock(&stats_lock);
        all_stats.bumped += t->arena->total;
        all_stats.reused += t->stats.reused;
        all_stats.lost_small += t->stats.lost_small;
        all_stats.lost_large += t->stats.lost_large;
        pthread_mutex_unlock(&stats_lock);
        t->stats = (AstStats){0};
    }
    memset(t->free_lists, 0, sizeof(t->free_lists));
    memset(t->small_mask, 0, sizeof(t->small_mask));
    memset(t->large, 0, sizeof(t->large));
    t->large_mask = 0;
    t->arena = nullptr;
}

void ast_arena_absorb(AstArena* into, AstArena* from) {
    check(!into->materialization && !from->materialization);
    for (u32 i = 0; i < from->range_count; i++) {
        if (into->range_count == into->range_cap) {
            into->range_cap = into->range_cap ? into->range_cap * 2 : 2;
            into->ranges = realloc(into->ranges, sizeof(AstRange) * into->range_cap);
            check(into->ranges);
        }
        into->ranges[into->range_count++] = from->ranges[i];
    }
    into->total += from->total;
    free(from->ranges);
    *from = (AstArena){0};
}

void ast_arena_drop(AstArena* arena) {
    for (u32 i = 0; i < arena->range_count; i++) {
        if (arena->materialization && !arena->lasting) {
            if (debug_env.ast_poison) {
                uintptr_t start = (uintptr_t)arena->ranges[i].start, end = ((uintptr_t)arena->ranges[i].end + AST_PAGE - 1) & ~(AST_PAGE - 1);
                mprotect((void*)start, end - start, PROT_NONE);
            }
            chunks_ref(arena->ranges[i].start, arena->ranges[i].end, -1);
            continue;
        }
        uintptr_t start = ((uintptr_t)arena->ranges[i].start + AST_PAGE - 1) & ~(AST_PAGE - 1);
        // (A materialization's last page is its own too)
        uintptr_t end = ((uintptr_t)arena->ranges[i].end + (arena->materialization ? AST_PAGE - 1 : 0)) & ~(AST_PAGE - 1);
        if (end <= start) continue;
        if (debug_env.ast_poison) mprotect((void*)start, end - start, PROT_NONE);
        else madvise((void*)start, end - start, MADV_DONTNEED);
    }
    free(arena->ranges);
    *arena = (AstArena){0};
}

// Faults the slab's pages in up to past pos, 2 MB at a time with one call rather than a trap per page (a tenth of
// vscode's parse), as huge pages while the system has them free (slabs are 2 MB aligned)
[[gnu::noinline]] static void populate(AstThread* t, char* pos) {
    while (t->populated < pos && t->populated < t->end) {
        char* from = t->populated;
        char* to = (size_t)(t->end - from) > AST_POPULATE ? from + AST_POPULATE : t->end;
        if (to - from == AST_POPULATE && !t->no_huge && huge_pages_take(AST_POPULATE)) {
            madvise(from, AST_POPULATE, MADV_HUGEPAGE);
            if (t->arena && t->arena->materialization) chunk_mark_huge(from);
        }
        madvise(from, (size_t)(to - from), MADV_POPULATE_WRITE);
        t->populated = to;
    }
}

// Moves the slab's end of the arena to pos
static inline void advance(AstThread* t, char* pos) {
    t->arena->total += (size_t)(pos - t->pos);
    t->pos = pos;
    if (__builtin_expect(pos > t->populated, 0)) populate(t, pos);
}

static inline void* bump(AstThread* t, size_t size) {
    if (__builtin_expect((size_t)(t->end - t->pos) < size, 0)) {
        add_range(t->arena, t->range_start, t->pos);
        t->pos = take_slab(size > AST_SLAB / 4 ? size : AST_SLAB, &t->end);
        t->range_start = t->pos;
        t->populated = t->pos;
    }
    void* ret = t->pos;
    advance(t, t->pos + size);
    return ret;
}

// Whether p is the current arena's: only its own blocks may be reused while parsing
static bool in_current_arena(AstThread const* t, char const* p) {
    if (p >= t->range_start && p < t->pos) return true;
    for (u32 i = 0; i < t->arena->range_count; i++) {
        if (p >= t->arena->ranges[i].start && p < t->arena->ranges[i].end) return true;
    }
    return false;
}

static void give_back(AstThread* t, char* p, size_t n);

static char* small_pop(AstThread* t, u32 cls) {
    char* ret = t->free_lists[cls];
    t->free_lists[cls] = *(void**)ret;
    if (!t->free_lists[cls]) t->small_mask[cls / 64] &= ~((u64)1 << (cls % 64));
    return ret;
}

// Serves n bytes out of a large free block, giving back the rest
static void* split(AstThread* t, LargeBlock* block, size_t n) {
    size_t size = block->size;
    t->stats.reused += n;
    if (size > n) give_back(t, (char*)block + n, size - n);
    return block;
}

static void* take_large(AstThread* t, u32 bin, size_t n) {
    LargeBlock* block = t->large[bin];
    t->large[bin] = block->next;
    if (!t->large[bin]) t->large_mask &= ~((u64)1 << bin);
    return split(t, block, n);
}

// A free block of at least n bytes split, else a bump
[[gnu::noinline]] static void* reuse_or_bump(AstThread* t, size_t n) {
    if (n <= AST_FREE_MAX) {
        // The smallest bigger free block
        u32 cls = (u32)(n / 8);
        for (u32 w = cls / 64; w < sizeof(t->small_mask) / 8; w++) {
            u64 bits = t->small_mask[w] & (w == cls / 64 ? ~(u64)0 << (cls % 64) : ~(u64)0);
            if (!bits) continue;
            u32 found = w * 64 + (u32)__builtin_ctzll(bits);
            char* ret = small_pop(t, found);
            t->stats.reused += n;
            if (found > cls) give_back(t, ret + n, (found - cls) * 8);
            return ret;
        }
        if (t->large_mask) return take_large(t, (u32)__builtin_ctzll(t->large_mask), n);
        return bump(t, n);
    }
    u32 bin = 63 - (u32)__builtin_clzll(n);
    // Any block of a higher bin is big enough, the first one of n's own bin that is
    u64 higher = bin < 63 ? t->large_mask & (~(u64)0 << (bin + 1)) : 0;
    if (higher) return take_large(t, (u32)__builtin_ctzll(higher), n);
    for (LargeBlock** b = &t->large[bin]; *b; b = &(*b)->next) {
        if ((*b)->size >= n) {
            LargeBlock* found = *b;
            *b = found->next;
            if (!t->large[bin]) t->large_mask &= ~((u64)1 << bin);
            return split(t, found, n);
        }
    }
    return bump(t, n);
}

void* ast_malloc(isize size) {
    AstThread* t = &ast_thread;
    if (!t->arena) {
        void* ret = malloc((size_t)size);
        check(ret || !size);
        return ret;
    }
    size_t n = round8(size);
    if (n <= AST_FREE_MAX && t->free_lists[n / 8]) {
        t->stats.reused += n;
        return small_pop(t, (u32)(n / 8));
    }
    if (t->small_mask[0] | t->small_mask[1] | t->small_mask[2] | t->large_mask) return reuse_or_bump(t, n);
    return bump(t, n);
}

void* ast_calloc(isize count, isize size) {
    void* ret = ast_malloc(count * size);
    if (ret) memset(ret, 0, (size_t)(count * size));
    return ret;
}

// Gives back n bytes (a multiple of 8) at p, of the current arena
static void give_back(AstThread* t, char* p, size_t n) {
    if (p + n == t->pos && p >= t->range_start) {
        t->arena->total -= n;
        t->pos = p;
    } else if (n <= AST_FREE_MAX) {
        u32 cls = (u32)(n / 8);
        *(void**)p = t->free_lists[cls];
        t->free_lists[cls] = p;
        t->small_mask[cls / 64] |= (u64)1 << (cls % 64);
    } else {
        u32 bin = 63 - (u32)__builtin_clzll(n);
        LargeBlock* block = (LargeBlock*)p;
        *block = (LargeBlock){ t->large[bin], n };
        t->large[bin] = block;
        t->large_mask |= (u64)1 << bin;
    }
}

void ast_free(void* p, isize size) {
    if (!p) return;
    if (!ast_owns(p)) {
        free(p);
        return;
    }
    AstThread* t = &ast_thread;
    if (t->arena && in_current_arena(t, p)) give_back(t, p, round8(size));
}

void* ast_realloc(void* p, isize old_size, isize size) {
    if (!p) return ast_malloc(size);
    if (!ast_owns(p)) {
        void* ret = realloc(p, (size_t)size);
        check(ret || !size);
        return ret;
    }
    AstThread* t = &ast_thread;
    size_t old_n = round8(old_size), n = round8(size);
    bool own = t->arena && in_current_arena(t, p);
    if (own && (char*)p + old_n == t->pos && (char*)p >= t->range_start && (char*)p + n <= t->end) {
        // The latest allocation: grows or shrinks in place
        advance(t, (char*)p + n);
        return p;
    }
    if (n <= old_n) {
        if (own && n < old_n) give_back(t, (char*)p + n, old_n - n);
        return p;
    }
    // (Another arena's: copied out to malloc, also by a materialization, whose memory goes before the node's)
    void* ret = own ? ast_malloc(size) : malloc((size_t)size);
    check(ret || !size);
    memcpy(ret, p, (size_t)old_size);
    ast_free(p, old_size);
    return ret;
}

void ast_stats_print(void) {
    if (!debug_env.ast_stats) return;
    AstStats s = all_stats;
    eprintln("ast arenas: ", s.bumped >> 20, " MB bumped, ", s.reused >> 20, " MB of freed blocks reused, still free at the files' ends ",
        s.lost_small >> 20, " MB small and ", s.lost_large >> 20, " MB large");
}

void ast_cstr_seal(cstr* s) {
    if (!ast_thread.arena || !cstr_is_long(s) || ast_owns(cstr_l_data(s))) return;
    isize size = cstr_l_size(s);
    char* data = ast_malloc(size + 1);
    memcpy(data, cstr_l_data(s), (size_t)size + 1);
    cstr_drop(s);
    s->lon.data = data;
    s->lon.size = (uintptr_t)size;
    cstr_l_set_cap(s, size);
}

void* ast_materialize_alloc(size_t size, bool align16) {
    AstThread* t = &ast_thread;
    size_t n = round8((isize)size);
    if (debug_env.ast_malloc) {
        // (Never freed: a debugging mode)
        void* ret = calloc(1, n);
        check(ret);
        return ret;
    }
    check(t->arena && t->arena->materialization);
    if (align16 && (uintptr_t)t->pos & 15 && t->end - t->pos >= 8) advance(t, t->pos + 8);
    void* ret = bump(t, n);
    // (Bumped memory is fresh but where ast_materialize_pop() or the parse gave some back)
    memset(ret, 0, n);
    return ret;
}
