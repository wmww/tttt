#pragma once

// This API was broken out of type.c, and is a bit clunky. It's perf-critical, which is why it's header-only and
// everything's inline.

#include "prelude.h"
#include "display.h"

typedef struct Type Type;

#define g_pro_key SvStr, svstr
#define g_type HMapBool, hmap_bool
#define g_val bool, bool
#include <cleat/generic/hmap.h>

typedef struct TypePairCache {
    HMapBool hmap;
} TypePairCache;

static inline void type_pair_cache_drop(TypePairCache* me) {
    hmap_bool_drop(&my hmap);
}

typedef struct TypePairCacheEntry {
    TypePairCache* cache;
    bool result;
    bool key_needs_drop;
    cstr key;
    bool serialized_types_eq;
} TypePairCacheEntry;

static inline TypePairCacheEntry type_pair_cache_entry_make(
    TypePairCache* cache,
    Type const* a,
    Type const* b,
    bool default_result
) {
    cstr key = {0};
    display_type(hash_key_display(&key), a);
    intptr_t sub_end = cstr_size(&key);
    cstr_append_lit(&key, " # ");
    intptr_t super_start = cstr_size(&key);
    display_type(hash_key_display(&key), b);
    return (TypePairCacheEntry) {
        .cache = cache,
        .result = default_result,
        .key_needs_drop = true,
        .key = key,
        .serialized_types_eq = csview_equals_sv(
            csview_slice(cstr_sv(&key), 0, sub_end),
            csview_slice(cstr_sv(&key), super_start, cstr_size(&key))
        ),
    };
}

static inline bool type_pair_cache_entry_insert(TypePairCacheEntry* me) {
    // Owndership of the key is passed in here (which means key may point to freed memory if result.inserted is false)
    auto result = hmap_bool_insert(&my cache->hmap, my key, my result);
    my key_needs_drop = false;
    if (!result.inserted) {
        my result = result.ref->second;
    }
    return result.inserted;
}

static inline void type_pair_cache_entry_update(TypePairCacheEntry* me, bool result) {
    if (result == my result) {
        return;
    }
    my result = result;
    // key may not have ownership over its data anymore, but can still be used. This is hacky but it works
    auto value = hmap_bool_get_mut(&my cache->hmap, cstr_sv(&my key));
    ASSERT(value);
    value->second = result;
}

static inline void type_pair_cache_entry_drop(TypePairCacheEntry* me) {
    if (my key_needs_drop) {
        cstr_drop(&my key);
    }
}
