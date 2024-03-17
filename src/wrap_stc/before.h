#define NAMES_TYPE(a, b) a
#define NAMES_PREFIX(a, b) b
#define NAMES_GET(macro, ...) macro(__VA_ARGS__)

#ifdef i_key_class_names
    #define i_keydrop c_JOIN(NAMES_GET(NAMES_PREFIX, i_key_class_names), _drop)

    #ifndef i_no_clone
        #define i_keyclone c_JOIN(NAMES_GET(NAMES_PREFIX, i_key_class_names), _clone)
    #endif

    #define i_key_names i_key_class_names
#endif
#ifdef i_key_names
    #undef i_key
    #define i_key NAMES_GET(NAMES_TYPE, i_key_names)
#elifdef i_key_str
    #define i_key_names Str, str
#elifdef i_key_ssv
    #define i_key_names Str, str
#elifdef i_key_arcbox_names
    #define i_key_names i_key_arcbox_names
#else
    #define i_key_names ,
#endif

#ifdef i_val_class_names
    #define i_valdrop c_JOIN(NAMES_GET(NAMES_PREFIX, i_val_class_names), _drop)

    #ifndef i_no_clone
        #define i_valclone c_JOIN(NAMES_GET(NAMES_PREFIX, i_val_class_names), _clone)
    #endif

    #define i_val_names i_val_class_names
#endif
#ifdef i_val_names
    #undef i_val
    #define i_val NAMES_GET(NAMES_TYPE, i_val_names)
#elifdef i_val_str
    #define i_val_names Str, str
#elifdef i_val_ssv
    #define i_val_names Str, str
#elifdef i_val_arcbox_names
    #define i_val_names i_val_arcbox_names
#else
    #define i_val_names ,
#endif


#ifndef i_type
    #ifdef TEMPLATE_HAS_VAL
        #define i_type c_JOIN(TEMPLATE_PREFIX, c_JOIN(NAMES_GET(NAMES_PREFIX, i_key_names), c_JOIN(_, NAMES_GET(NAMES_PREFIX, i_val_names))))
    #else
        #define i_type c_JOIN(TEMPLATE_PREFIX, NAMES_GET(NAMES_PREFIX, i_key_names))
    #endif
#endif

#ifdef i_more
#define I_MORE_INCLUDES
#endif

typedef struct i_type c_JOIN(TEMPLATE_TYPE, c_JOIN(NAMES_GET(NAMES_TYPE, i_key_names), NAMES_GET(NAMES_TYPE, i_val_names)));
