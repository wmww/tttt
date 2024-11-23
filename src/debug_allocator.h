#pragma once

#define DEBUG_ALLOCATOR
// See STC's linkage.h
#define STC_ALLOCATOR debug
extern intptr_t debug_allocation_count;

// If this is non-zero, allocations made by us and freed some other wasy (or vice versa) will crash the program (good)
// Seems to cause problems (especially in release mode) if not a word multiple
#define DEBUG_ALLOCATOR_OFFSET ((intptr_t)sizeof(intptr_t))

// The extra space makes sure we crash if a memory block is allocated through our allocator and freed some other way, or
// vice versa. Could
static inline void* debug_malloc(intptr_t size) {
    void* ret = c_malloc(size + DEBUG_ALLOCATOR_OFFSET);
    if (ret) debug_allocation_count++;
    return (char*)ret + DEBUG_ALLOCATOR_OFFSET;
}

static inline void* debug_calloc(intptr_t num, intptr_t size) {
    void* ret = c_calloc(num, size + DEBUG_ALLOCATOR_OFFSET);
    if (ret) debug_allocation_count++;
    return (char*)ret + DEBUG_ALLOCATOR_OFFSET;
}

static inline void* debug_realloc(void* ptr, intptr_t old_size, intptr_t new_size) {
    if (ptr) {
        return (char*)c_realloc(
            (char*)ptr - DEBUG_ALLOCATOR_OFFSET,
            old_size + DEBUG_ALLOCATOR_OFFSET,
            new_size + DEBUG_ALLOCATOR_OFFSET
        ) + DEBUG_ALLOCATOR_OFFSET;
    } else {
        return debug_malloc(new_size);
    }
}

static inline void debug_free(void *ptr, intptr_t size) {
    if (ptr) {
        c_free((char*)ptr - DEBUG_ALLOCATOR_OFFSET, size + DEBUG_ALLOCATOR_OFFSET);
        debug_allocation_count--;
    }
}
// Replace STC common.h's c_new with one that uses our allocator (not sure why this isn't default)
#undef c_new
#define c_new(T, ...) ((T*)memcpy(debug_malloc(sizeof(T)), ((T[]){__VA_ARGS__}), sizeof(T)))
#define free(ptr) debug_free(ptr, 0)
