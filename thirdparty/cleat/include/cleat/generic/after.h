#ifdef TEMPLATE_HAS_ITER
static inline NAMES_MEMBER(g_type, value)* NAMES_MEMBER(g_type, iter_deref)(NAMES_MEMBER(g_type, iter)* it) { \
    return it->ref;
}

#ifdef g_display
#include "../display_functions.h"

impl_display_class(NAMES_GET(NAMES_TYPE, g_type), NAMES_GET(NAMES_PREFIX, g_type), static inline) {
    bool prev_debug = d.debug_mode;
    d.debug_mode = true;
    d = lit_display(TEMPLATE_DISPLAY_OPEN, d);
    if (!NAMES_MEMBER(g_type, is_empty)(me)) {
        d.indent++;
        d = newline_display(nullptr, d);
        NAMES_MEMBER(g_type, iter) iter = NAMES_MEMBER(g_type, begin)(me);
        while (iter.ref) {
            #ifdef TEMPLATE_HAS_VAL
            d = NAMES_MEMBER(g_key, display)(iter.ref->first, d);
            d = lit_display(": ", d);
            d = NAMES_MEMBER(g_val, display)(iter.ref->second, d);
            #else
            d = NAMES_MEMBER(g_key, display)(*iter.ref, d);
            #endif
            NAMES_MEMBER(g_type, next)(&iter);
            if (!iter.ref) break;
            d = lit_display(",", d);
            d = newline_display(" ", d);
        }
        if (d.multiline) d = lit_display(",", d);
        d.indent--;
        d = newline_display(nullptr, d);
    }
    d = lit_display(TEMPLATE_DISPLAY_CLOSE, d);
    d.debug_mode = prev_debug;
    return d;
}

#endif

#endif

#undef g_allocator
#undef g_aux
#undef g_capacity
#undef g_class_key
#undef g_class_val
#undef g_cmp
#undef g_compare_key
#undef g_declared
#undef g_eq
#undef g_hash
#undef g_header
#undef g_import
#undef g_include_key
#undef g_key
#undef g_keyclass
#undef g_keyclone
#undef g_keydrop
#undef g_keyfrom
#undef g_keypro
#undef g_keyraw
#undef g_keytoraw
#undef g_less
#undef g_max_load_factor
#undef g_no_clone
#undef g_no_hash
#undef g_opt
#undef g_pro_key
#undef g_pro_val
#undef g_static
#undef g_type
#undef g_val
#undef g_valclass
#undef g_valclone
#undef g_valdrop
#undef g_valfrom
#undef g_valpro
#undef g_valraw
#undef g_valtoraw

#undef g_display
#undef NAMES_PREFIX
#undef TEMPLATE_PREFIX
#undef TEMPLATE_HAS_VAL
#undef TEMPLATE_HAS_ITER
#undef TEMPLATE_DISPLAY_OPEN
#undef TEMPLATE_DISPLAY_CLOSE
#undef NAMES_TYPE
#undef NAMES_GET
#undef NAMES_MEMBER_INTERNAL
#undef NAMES_MEMBER
#undef TEMPLATE_TYPE
#undef TEMPLATE_IS_UNION
#undef TEMPLATE_IS_SORTED
#undef TEMPLATE_IS_HASHED
