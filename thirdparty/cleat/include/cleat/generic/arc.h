#define TEMPLATE_TYPE Arc
#define TEMPLATE_PREFIX arc_
#define TEMPLATE_IS_UNION

#include "before.h"
#include "stc/arc.h"

#ifdef g_display
#include "../display_minimal.h"

#define DISPLAY_STRINGIFY_ARGS(...) #__VA_ARGS__
#define DISPLAY_APPLY_MACRO(name, ...) name(__VA_ARGS__)
impl_display_class(NAMES_GET(NAMES_TYPE, g_type), NAMES_GET(NAMES_PREFIX, g_type), static inline) {
    bool prev_debug = d.debug_mode;
    d.debug_mode = true;
    d = lit_display(DISPLAY_APPLY_MACRO(DISPLAY_STRINGIFY_ARGS, NAMES_GET(NAMES_TYPE, g_type)), d);
    d = lit_display("(", d);
    d = void_ptr_display((void*)me->get, d);
    d = lit_display(")", d);
    d.debug_mode = prev_debug;
    return d;
}
#undef DISPLAY_STRINGIFY_ARGS
#undef DISPLAY_APPLY_MACRO
#undef g_display
#endif

#include "after.h"
