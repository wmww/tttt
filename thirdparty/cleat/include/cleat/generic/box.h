#define TEMPLATE_TYPE Box
#define TEMPLATE_PREFIX box_

#include "before.h"
#include "stc/box.h"

#ifdef g_display
#include "../display_minimal.h"

impl_display_class(NAMES_GET(NAMES_TYPE, g_type), NAMES_GET(NAMES_PREFIX, g_type), static inline) {
    bool prev_debug = d.debug_mode;
    d.debug_mode = true;
    d = lit_display("-> ", d);
    d = NAMES_MEMBER(g_key, display)(*me->get, d);
    d.debug_mode = prev_debug;
    return d;
}
#undef g_display
#endif

#include "after.h"
