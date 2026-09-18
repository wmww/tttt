#define TEMPLATE_TYPE HMap
#define TEMPLATE_PREFIX hmap_
#define TEMPLATE_HAS_VAL
#define TEMPLATE_HAS_ITER
#define TEMPLATE_IS_HASHED
#define TEMPLATE_DISPLAY_OPEN "{"
#define TEMPLATE_DISPLAY_CLOSE "}"

// One of the functions in hmap uses a variabled named ok internally, so undef the macro
#undef ok

#include "before.h"
#include "stc/hmap.h"
#include "after.h"

// And re-def ok here if the failable.h header has been included
#ifdef err
#define ok *(_OUTPUT_PARAM) = _OK_INTERNAL
#endif
