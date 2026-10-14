#pragma once
#include "prelude.h"

// An index from names to the first of several items that has each, for duplicate checks over a declaration's members
// (scanning the earlier items for each item is quadratic on large literals and interfaces): open addressing over item
// indices, -1 for an empty slot. The caller probes from name_index_slot() with its own hash, compares the items
// itself, and fills an empty slot it stops at
typedef struct NameIndex {
    isize* slots;
    u64 mask;
} NameIndex;

static inline NameIndex name_index_make(isize count) {
    u64 capacity = 16;
    while (capacity < (u64)count * 2) capacity <<= 1;
    NameIndex ret = { malloc(sizeof(isize) * capacity), capacity - 1 };
    panic_if(!ret.slots, "out of memory");
    memset(ret.slots, 0xff, sizeof(isize) * capacity);
    return ret;
}

static inline u64 name_index_slot(NameIndex const* me, u64 hash) {
    return (hash ^ (hash >> 29)) * 0x9e3779b97f4a7c15ull >> 20 & my mask;
}

static inline u64 name_index_next(NameIndex const* me, u64 slot) {
    return (slot + 1) & my mask;
}

static inline void name_index_drop(NameIndex* me) {
    free(my slots);
    my slots = nullptr;
}
