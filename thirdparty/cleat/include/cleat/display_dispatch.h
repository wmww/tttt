#pragma once

#include "display_minimal.h"
#include "types.h"
typedef struct ErrorBase ErrorBase;
DisplayCtx error_base_display(ErrorBase const* error, DisplayCtx d);

#define nl ((DisplayNewline){})
#define nl_or_space ((DisplayNewline){ .fallback = c_sv(" ") })

#ifndef DEFAULT_DISPLAY
#define DEFAULT_DISPLAY ((DisplayCtx){})
#endif

#ifndef PRINT_DISPLAY
#define PRINT_DISPLAY ((DisplayCtx){.color = true, .multiline = true})
#endif

#ifndef EPRINT_DISPLAY
#define EPRINT_DISPLAY PRINT_DISPLAY
#endif

static inline DisplayCtx display_ctx_clear_debug(DisplayCtx d) { d.debug_mode = false; return d; }

#define DISPLAY_GENERIC_1(d) d
#define DISPLAY_GENERIC_2(d, value) _Generic((value), DISPLAYABLES)((value), display_ctx_clear_debug(d))
#define DISPLAY_GENERIC_3(_1, _2, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_2(_1, _2))
#define DISPLAY_GENERIC_4(_1, _2, _3, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_3(_1, _2, _3))
#define DISPLAY_GENERIC_5(_1, _2, _3, _4, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_4(_1, _2, _3, _4))
#define DISPLAY_GENERIC_6(_1, _2, _3, _4, _5, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_5(_1, _2, _3, _4, _5))
#define DISPLAY_GENERIC_7(_1, _2, _3, _4, _5, _6, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_6(_1, _2, _3, _4, _5, _6))
#define DISPLAY_GENERIC_8(_1, _2, _3, _4, _5, _6, _7, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_7(_1, _2, _3, _4, _5, _6, _7))
#define DISPLAY_GENERIC_9(_1, _2, _3, _4, _5, _6, _7, _8, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_8(_1, _2, _3, _4, _5, _6, _7, _8))
#define DISPLAY_GENERIC_10(_1, _2, _3, _4, _5, _6, _7, _8, _9, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_9(_1, _2, _3, _4, _5, _6, _7, _8, _9))
#define DISPLAY_GENERIC_11(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_10(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10))
#define DISPLAY_GENERIC_12(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_11(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11))
#define DISPLAY_GENERIC_13(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_12(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12))
#define DISPLAY_GENERIC_14(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_13(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13))
#define DISPLAY_GENERIC_15(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_14(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14))
#define DISPLAY_GENERIC_16(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_15(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15))
#define DISPLAY_GENERIC_17(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_16(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16))
#define DISPLAY_GENERIC_18(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_17(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17))
#define DISPLAY_GENERIC_19(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_18(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18))
#define DISPLAY_GENERIC_20(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, value) _Generic((value), DISPLAYABLES)((value), DISPLAY_GENERIC_19(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19))

#define display_sequence(...) c_MACRO_OVERLOAD(DISPLAY_GENERIC __VA_OPT__(, __VA_ARGS__))

static inline void print_display(FILE* f, DisplayCtx d) {
    fprintf(f, "%s", cstr_str(&d.target));
    display_ctx_drop(&d);
}
#define print(...)    print_display(stdout, display_sequence(PRINT_DISPLAY,   __VA_ARGS__))
#define println(...)  print_display(stdout, display_sequence(PRINT_DISPLAY,   __VA_OPT__(__VA_ARGS__,) "\n"))
#define eprint(...)   print_display(stderr, display_sequence(EPRINT_DISPLAY,  __VA_ARGS__))
#define eprintln(...) print_display(stderr, display_sequence(EPRINT_DISPLAY,  __VA_OPT__(__VA_ARGS__,) "\n"))
#define cstr_d(...)   display_into_cstr(    display_sequence(DEFAULT_DISPLAY, __VA_ARGS__))

__attribute__((noreturn))
void default_log_fatal_abort(csview file, i64 line, csview label, DisplayCtx d);
#define panic(...) default_log_fatal_abort( \
    c_sv(__FILE__), \
    __LINE__, \
    csview_null, \
    display_sequence(logger_config.display_ctx, __VA_ARGS__) \
)
#define panic_if(cond, ...) if (cond) panic(__VA_ARGS__)

// Patched (cleat-int64-patch.txt): long and long long rather than i64 and u64, so every 64-bit integer type displays
// whichever of them int64_t is (long on Linux, long long on macOS; size_t and ptrdiff_t are long on both)
#define CORE_DISPLAYABLES \
    char*:          cstr_raw_display, \
    char const*:    cstr_raw_display, \
    void*:          void_ptr_display, \
    cstr:           cstr_display, \
    csview:         debuggable_sv_display, \
    long:           i64_display, \
    unsigned long:  u64_display, \
    long long:      i64_display, \
    unsigned long long: u64_display, \
    i32:            i64_display, \
    u32:            i64_display, \
    char:           char_display, \
    f64:            f64_display, \
    f32:            f64_display, \
    bool:           bool_display, \
    DisplayNewline: display_newline_display, \
    DisplaySetProp: display_set_prop_display, \
    ErrorBase const*: error_base_display, \
    DisplayCtx:     display_ctx_display

#define DISPLAYABLES CORE_DISPLAYABLES
