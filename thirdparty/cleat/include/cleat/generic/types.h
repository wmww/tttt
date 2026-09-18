#pragma once

#include "stc/types.h"

#define g_declare_arc(type, prefix) \
    declare_arc(arc_##prefix, type); \
    typedef arc_##prefix Arc##type

#define g_declare_box(type, prefix) \
    declare_box(box_##prefix, type); \
    typedef box_##prefix Box##type

#define g_declare_vec(type, prefix) \
    declare_vec(vec_##prefix, type); \
    typedef vec_##prefix Vec##type

#define g_declare_hmap_custom(full_type, full_prefix, key_type, val_type) \
    declare_hashmap(full_prefix, key_type, val_type); \
    typedef full_prefix full_type

#define g_declare_hmap(key_type, val_type, val_prefix) \
    g_declare_hmap_custom(HMap##val_type, hmap_##val_prefix, key_type, val_type)

#define g_declare_smap_custom(full_type, full_prefix, key_type, val_type) \
    declare_sortedmap(full_prefix, key_type, val_type); \
    typedef full_prefix full_type

#define g_declare_smap(key_type, val_type, val_prefix) \
    g_declare_smap_custom(SMap##val_type, smap_##val_prefix, key_type, val_type)

#define g_declare_hset(type, prefix) \
    declare_hashset(hset_##prefix, type); \
    typedef hset_##prefix HSet##type

#define g_declare_sset(type, prefix) \
    declare_sortedset(sset_##prefix, type); \
    typedef sset_##prefix SSet##type
