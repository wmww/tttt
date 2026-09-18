#include "stc/common.h"

#define NAMES_TYPE(a, b) a
#define NAMES_PREFIX(a, b) b
#define NAMES_GET(macro, ...) macro(__VA_ARGS__)
#define NAMES_MEMBER_INTERNAL(type, prefix, member) prefix##_##member
#define NAMES_MEMBER(...) NAMES_MEMBER_INTERNAL(__VA_ARGS__)

// Workaround for https://github.com/stclib/STC/pull/176
#undef _i_new_n
#define _i_new_n(T, n) ((T*)(n ? i_malloc((n)*c_sizeof(T)) : nullptr))

#ifdef g_no_clone
    #define i_no_clone
#endif

#ifdef g_no_hash
    #define i_no_hash
#endif

#ifdef g_declared
    #define i_declared
#endif

#ifdef g_header
    #define i_header
#endif

#ifdef g_eq
    #define i_eq g_eq
#endif

#ifdef g_cmp
    #define i_cmp g_cmp
#endif

#ifdef g_less
    #define i_less g_less
#endif

#ifdef g_hash
    #define i_hash g_hash
#endif

#ifdef g_keyclone
    #define i_keyclone g_keyclone
#endif

#ifdef g_keydrop
    #define i_keydrop g_keydrop
#endif

#ifdef g_keyraw
    #define i_keyraw g_keyraw
#endif

#ifdef g_keyfrom
    #define i_keyfrom g_keyfrom
#endif

#ifdef g_keytoraw
    #define i_keytoraw g_keytoraw
#endif

#ifdef g_valclone
    #define i_valclone g_valclone
#endif

#ifdef g_valdrop
    #define i_valdrop g_valdrop
#endif

#ifdef g_valraw
    #define i_valraw g_valraw
#endif

#ifdef g_valfrom
    #define i_valfrom g_valfrom
#endif

#ifdef g_valtoraw
    #define i_valtoraw g_valtoraw
#endif

#ifdef g_compare_key
    #define i_compare_key g_compare_key
#endif

#ifdef g_capacity
    #define i_capacity g_capacity
#endif

#ifdef g_allocator
    #define i_allocator g_allocator
#endif

#ifdef g_aux
    #define i_aux g_aux
#endif

#ifdef g_max_load_factor
    #define i_max_load_factor g_max_load_factor
#endif

#ifdef g_implement
    #define i_implement
#endif

#ifdef g_static
    #define i_static
#endif

#ifdef g_import
    #define i_import
#endif

#ifdef g_opt
    #define i_opt g_opt
#endif

// Support deprecated names
#ifdef g_keyclass
    #define g_class_key g_keyclass
#endif
#ifdef g_keypro
    #define g_pro_key g_keypro
#endif
#ifdef g_valclass
    #define g_class_val g_valclass
#endif
#ifdef g_valpro
    #define g_pro_val g_valpro
#endif

#ifdef g_pro_key
    #define g_class_key g_pro_key
    #define i_compare_key c_JOIN(NAMES_GET(NAMES_PREFIX, g_pro_key), _raw)
    #define i_keyraw i_compare_key
#endif

#ifdef g_class_key
    #ifndef g_key
        #define g_key g_class_key
    #endif
    #if !defined i_keyclone && !defined i_no_clone
        #define i_keyclone c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_key), _clone)
    #endif
    #ifndef i_keydrop
        #define i_keydrop c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_key), _drop)
    #endif
    #if !defined i_keyfrom && defined i_keyraw
        #define i_keyfrom c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_key), _from)
    #endif
    #if !defined i_keytoraw && defined i_keyraw
        #define i_keytoraw c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_key), _toraw)
    #endif
#endif

#if defined g_class_key && !defined i_keyraw && !defined i_compare_key
    #ifdef TEMPLATE_IS_HASHED
        #ifndef i_eq
            #define i_eq c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_key), _eq)
        #endif
        #if !defined i_hash && !defined i_no_hash
            #define i_hash c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_key), _hash)
        #endif
    #endif
    #ifdef TEMPLATE_IS_SORTED
        #if !defined i_cmp && !defined i_less
            #define i_cmp c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_key), _cmp)
        #endif
    #endif
#endif

#ifdef g_key
    #undef i_key
    #define i_key NAMES_GET(NAMES_TYPE, g_key)
#else
    #error You must specify g_key, g_class_key or g_pro_key
    // Prevent massive error messages
    #define g_key ,
#endif

#ifdef g_pro_val
    #define g_class_val g_pro_val
    #define i_valraw c_JOIN(NAMES_GET(NAMES_PREFIX, g_pro_val), _raw)
#endif

#ifdef g_class_val
    #ifndef g_val
        #define g_val g_class_val
    #endif
    #if !defined i_valclone && !defined i_no_clone
        #define i_valclone c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_val), _clone)
    #endif
    #ifndef i_valdrop
        #define i_valdrop c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_val), _drop)
    #endif
    #if !defined i_valfrom && defined i_valraw
        #define i_valfrom c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_val), _from)
    #endif
    #if !defined i_valtoraw && defined i_valraw
        #define i_valtoraw c_JOIN(NAMES_GET(NAMES_PREFIX, g_class_val), _toraw)
    #endif
#endif

#ifdef g_val
    #undef i_val
    #define i_val NAMES_GET(NAMES_TYPE, g_val)
#elifdef TEMPLATE_HAS_VAL
    #error You must specify g_val, g_class_val or g_pro_val
    // Prevent massive error messages
    #define g_val ,
#endif

#ifdef i_type
    #error i_type cannot be used with the cleat generic wrapper. use g_type Foo, foo instead
#endif

#ifndef g_type
    #ifdef TEMPLATE_HAS_VAL
        #ifdef g_include_key
            #define g_type \
                c_JOIN(TEMPLATE_TYPE, c_JOIN(NAMES_GET(NAMES_TYPE, g_key), NAMES_GET(NAMES_TYPE, g_val))), \
                c_JOIN(TEMPLATE_PREFIX, c_JOIN(NAMES_GET(NAMES_PREFIX, g_key), c_JOIN(_, NAMES_GET(NAMES_PREFIX, g_val))))
        #else
            #define g_type \
                c_JOIN(TEMPLATE_TYPE, NAMES_GET(NAMES_TYPE, g_val)), \
                c_JOIN(TEMPLATE_PREFIX, NAMES_GET(NAMES_PREFIX, g_val))
        #endif
    #else
        #define g_type \
            c_JOIN(TEMPLATE_TYPE, NAMES_GET(NAMES_TYPE, g_key)), \
            c_JOIN(TEMPLATE_PREFIX, NAMES_GET(NAMES_PREFIX, g_key))
    #endif
#endif

#define i_type NAMES_GET(NAMES_PREFIX, g_type)

typedef
#ifdef TEMPLATE_IS_UNION
    union
#else
    struct
#endif
        i_type NAMES_GET(NAMES_TYPE, g_type);
