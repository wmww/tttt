#define TYPENAME_TYPE(a, b) a
#define TYPENAME_PREFIX(a, b) b
#define TYPENAME_GET(macro, ...) macro(__VA_ARGS__)

#if defined i_key_classnames && ! defined i_key
    #ifndef i_type
        #define i_type c_JOIN(vec_, TYPENAME_GET(TYPENAME_PREFIX, i_key_classnames))
    #endif

    typedef struct i_type c_JOIN(Vec, TYPENAME_GET(TYPENAME_TYPE, i_key_classnames));

    #define i_key TYPENAME_GET(TYPENAME_TYPE, i_key_classnames)

    #define i_key_drop TYPENAME_PREFIX(i_key_classnames)##_drop

    #ifndef i_no_clone
        #define i_key_clone TYPENAME_PREFIX(i_key_classnames)##_clone
    #endif
#elif defined i_key
    #error "Cannot use both i_key_classnames and i_key"
#endif

#include "stc/vec.h"

#undef i_key_classnames
#undef GET_TYPENAME
#undef GET_TYPEPREFIX
#undef TYPENAME_GET
