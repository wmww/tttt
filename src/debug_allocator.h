#pragma once

// Set so code that needs the allocation counter (prelude.c defines it) knows the allocator is in play
#define DEBUG_ALLOCATOR_ACTIVE

// Counts live allocations so leaks can be attributed to whatever was running at the time. At time of writing it's
// only useful for identifying which test of many has a leak.
//
// This works by macro-replacing malloc/calloc/realloc/free, which STC's c_malloc/c_free/etc. bottom out in. It only
// affects allocations made from our own translation units; cleat and STC's out-of-line code keeps using the real
// allocator. That's fine as long as no allocation crosses the boundary, and the offset below makes sure we crash
// loudly rather than corrupt the heap if one ever does.
//
// Must be included before any STC or cleat header, so nothing has already been compiled against the real functions.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern intptr_t debug_allocation_count;

// Seems to cause problems (especially in release mode) if not a word multiple
#define DEBUG_ALLOCATOR_OFFSET (sizeof(intptr_t))

// A zero-size allocation returns null rather than a real block, which the standard permits. STC's container _drop()
// functions skip freeing when capacity is zero, so anything that allocates zero bytes and records a zero capacity
// (vec_X_with_capacity(0) used to) leaks the block forever. Cleat works that specific case around in
// generic/before.h, but keeping the rule here means our allocation count can't be thrown off by another one.
static inline void* debug_malloc(size_t size) {
    if (size == 0) return nullptr;
    void* ret = (malloc)(size + DEBUG_ALLOCATOR_OFFSET);
    if (!ret) return nullptr;
    debug_allocation_count++;
    return (char*)ret + DEBUG_ALLOCATOR_OFFSET;
}

static inline void* debug_calloc(size_t num, size_t size) {
    if (num == 0 || size == 0) return nullptr;
    void* ret = (calloc)(1, num * size + DEBUG_ALLOCATOR_OFFSET);
    if (!ret) return nullptr;
    debug_allocation_count++;
    return (char*)ret + DEBUG_ALLOCATOR_OFFSET;
}

// Deliberately does NOT mirror debug_malloc()'s zero-size special case: STC's _reserve() treats a null return as
// failure and leaves its now-freed data pointer in place, so returning null here would be a use-after-free. A
// resize to zero therefore keeps a one-word block alive, which STC's _drop() will skip. Nothing calls it that way
// at present (it would need shrink_to_fit() on an empty container).
static inline void* debug_realloc(void* ptr, size_t new_size) {
    if (!ptr) return debug_malloc(new_size);
    void* ret = (realloc)((char*)ptr - DEBUG_ALLOCATOR_OFFSET, new_size + DEBUG_ALLOCATOR_OFFSET);
    if (!ret) return nullptr;
    return (char*)ret + DEBUG_ALLOCATOR_OFFSET;
}

static inline void debug_free(void* ptr) {
    if (!ptr) return;
    (free)((char*)ptr - DEBUG_ALLOCATOR_OFFSET);
    debug_allocation_count--;
}

#define malloc(size) debug_malloc(size)
#define calloc(num, size) debug_calloc(num, size)
#define realloc(ptr, size) debug_realloc(ptr, size)
#define free(ptr) debug_free(ptr)
