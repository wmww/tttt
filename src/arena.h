#pragma once

#include "prelude.h"

// Bump allocator: nothing in it is freed individually. Implemented in ty/checker.c
typedef struct ArenaChunk ArenaChunk;
typedef struct Arena {
    ArenaChunk* head;
    size_t total; // bytes handed out, for measurements
} Arena;

void* arena_alloc(Arena* me, size_t size); // zeroed, 8 byte aligned
void arena_drop(Arena* me);
#define arena_new(arena, T) ((T*)arena_alloc(arena, sizeof(T)))
#define arena_new_n(arena, T, n) ((T*)arena_alloc(arena, sizeof(T) * (size_t)(n)))
