#pragma once

#include "prelude.h"

// Bump allocator: nothing in it is freed individually. Implemented in ty/checker.c
typedef struct ArenaChunk ArenaChunk;
typedef struct Arena {
    ArenaChunk* head;
    size_t total; // bytes handed out, for measurements
    // Allocates in the thread's current materialization of deferred bodies (ast_materialize_alloc(), plan 78), which
    // owns the memory: arena_drop() only forgets it. ARENA_AST_DIRECT: each allocation (no chunk tails, no
    // arena_pop_last()), ARENA_AST_CHUNKED: in small chunks of it
    uint8_t ast_backed;
    bool checker; // A checker's: its chunks are named `checker` under TTTT_VMA_NAMES (vma_name()), others `arena`
#ifdef TTTT_SURVIVORS
    struct SurvivorRec* rec; // ty/survivors.c: its allocations, recorded (a checker's arena only)
#endif
} Arena;
enum { ARENA_AST_DIRECT = 1, ARENA_AST_CHUNKED = 2 };

struct ArenaChunk {
    ArenaChunk* next;
    size_t used;
    size_t cap;
    size_t mapped; // Bytes mapped for the chunk, header included
    size_t populated; // data's pages below this offset are faulted in (arena_populate()), at most cap
    alignas(16) char data[];
};

// arena_alloc() past the current chunk's populated pages (`size` rounded up to 8)
void* arena_alloc_slow(Arena* me, size_t size);
#if defined(TTTT_ARENA_SITES) || defined(TTTT_SURVIVORS)
void* arena_alloc(Arena* me, size_t size); // (Out of line: they record the caller)
#else
// Zeroed, 8 byte aligned. The bump inline, the rest out of line: as one function every call paid the slow path's
// frame (36 instructions a call, 6M calls on zod: ~3% of its instructions)
static inline void* arena_alloc(Arena* me, size_t size) {
    size = (size + 7) & ~(size_t)7;
    ArenaChunk* head = my head;
    if (__builtin_expect(head && head->used + size <= head->populated, 1)) {
        void* ret = head->data + head->used;
        head->used += size;
        my total += size;
        return ret;
    }
    return arena_alloc_slow(me, size);
}
#endif
void arena_drop(Arena* me);
// Empties it for reuse, keeping its latest chunk (zeroed again, its pages still mapped)
void arena_reset(Arena* me);
// Gives back the latest allocation (zeroed again) if `ptr` of `size` bytes is it, else false and nothing happens
bool arena_pop_last(Arena* me, void* ptr, size_t size);
#ifdef TTTT_ARENA_SITES
void arena_sites_print(void);
#endif
#ifdef TTTT_SURVIVORS
// ty/survivors.c (plan 79's measurement): the allocations of one checker's arena
void survivors_record(Arena* arena, void* ptr, size_t size, void* site);
void survivors_unrecord(Arena* arena, void* ptr);
void survivors_reset(Arena* arena);
void survivors_free(Arena* arena);
// The file whose results allocations are being made for (the checker's sequence number of its check, 0: none, the
// warm-up), see survivors_file_census()
extern thread_local u32 survivors_owner;
// A later look at a memo or intern entry (0: an intern block, 1: a node link key, 2: a LazyMemo)
void survivors_hit(void const* what, u32 kind);
#endif
#define arena_new(arena, T) ((T*)arena_alloc(arena, sizeof(T)))
#define arena_new_n(arena, T, n) ((T*)arena_alloc(arena, sizeof(T) * (size_t)(n)))
