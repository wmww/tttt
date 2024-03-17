#pragma once

#include "stc/types.h"

#define wrap_forward_arc(type, prefix) \
    forward_arc(arc_##prefix, type); \
    typedef arc_##prefix Arc##type

#define wrap_forward_box(type, prefix) \
    forward_box(box_##prefix, type); \
    typedef box_##prefix Box##type

#define wrap_forward_vec(type, prefix) \
    forward_vec(vec_##prefix, type); \
    typedef vec_##prefix Vec##type

#define wrap_forward_hmap_full(full_type, full_prefix, key_type, val_type) \
    forward_hmap(full_prefix, key_type, val_type); \
    typedef full_prefix full_type

#define wrap_forward_hmap_str(val_type, val_prefix) \
    wrap_forward_hmap_full(HmapStr##val_type, hmap_str_##val_prefix, cstr, val_type)

#define wrap_forward_hmap(key_type, key_prefix, val_type, val_prefix) \
    wrap_forward_hmap_full(Hmap##key_type##val_type, hmap_##key_prefix##_##val_prefix, key_type, val_type)
