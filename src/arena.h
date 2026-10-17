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
} Arena;
enum { ARENA_AST_DIRECT = 1, ARENA_AST_CHUNKED = 2 };

void* arena_alloc(Arena* me, size_t size); // zeroed, 8 byte aligned
void arena_drop(Arena* me);
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
#endif
#define arena_new(arena, T) ((T*)arena_alloc(arena, sizeof(T)))
#define arena_new_n(arena, T, n) ((T*)arena_alloc(arena, sizeof(T) * (size_t)(n)))
