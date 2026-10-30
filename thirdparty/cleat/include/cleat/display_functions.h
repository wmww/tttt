#pragma once

#include "display_minimal.h"
#include "types.h"
#include <inttypes.h>

DisplayCtx string_debug_display(csview sv, DisplayCtx d);
DisplayCtx fmt_display(DisplayCtx d, const char* fmt, ...) __attribute__((format (printf, 2, 3)));
#define lit_display(s, d) sv_display(c_sv(s), d)

static inline DisplayCtx color_display(csview color_code, DisplayCtx d) {
    if (d.color) cstr_append_sv(&d.target, color_code);
    return d;
}
static inline DisplayCtx sv_display(csview sv, DisplayCtx d) { cstr_append_sv(&d.target, sv); return d; }

static inline DisplayCtx debuggable_sv_display(csview sv, DisplayCtx d) {
    if (d.debug_mode) return string_debug_display(sv, d);
    else              return sv_display          (sv, d);
}

static inline DisplayCtx cstr_raw_display(char const* s, DisplayCtx d) {
    return debuggable_sv_display(s ? csview_from(s) : (csview){0}, d);
}

impl_display_class(cstr, cstr, static inline) { return debuggable_sv_display(cstr_sv(me), d); }

static inline DisplayCtx i64_display     (i64    i, DisplayCtx d) { cstr_append_fmt(&d.target, "%" PRId64, i); return d; }
static inline DisplayCtx u64_display     (u64    u, DisplayCtx d) { cstr_append_fmt(&d.target, "%" PRIu64, u); return d; }
static inline DisplayCtx i32_display     (i32    i, DisplayCtx d) { return i64_display(i, d); }
static inline DisplayCtx u32_display     (u32    u, DisplayCtx d) { return u64_display(u, d); }
static inline DisplayCtx f64_display     (f64    f, DisplayCtx d) { cstr_append_fmt(&d.target, "%.12g", f); return d; }
static inline DisplayCtx void_ptr_display(void*  v, DisplayCtx d) { cstr_append_fmt(&d.target, "%p", v); return d; }
static inline DisplayCtx bool_display    (bool   b, DisplayCtx d) { return sv_display(b ? c_sv("true") : c_sv("false"), d); }
static inline DisplayCtx char_display    (char   c, DisplayCtx d) { return sv_display((csview){.buf = &c, .size = 1}, d); }
static inline DisplayCtx display_ctx_display(DisplayCtx me, DisplayCtx d) { return sv_display(cstr_sv(&d.target), me); }

static inline DisplayCtx repeated_display(csview sv, u64 n, DisplayCtx d) {
    for (u64 i = 0; i < n; i++) d = sv_display(sv, d);
    return d;
}

#ifndef DISPLAY_INDENT
#define DISPLAY_INDENT c_sv("  ")
#endif

typedef struct DisplayNewline {
    csview fallback;
} DisplayNewline;
static inline DisplayCtx display_newline_display(DisplayNewline me, DisplayCtx d) {
    if (d.multiline) {
        d = lit_display("\n", d);
        return repeated_display(DISPLAY_INDENT, d.indent, d);
    } else {
        return sv_display(me.fallback, d);
    }
}
static inline DisplayCtx newline_display(const char* fallback, DisplayCtx d) {
    return display_newline_display((DisplayNewline){
        .fallback = fallback ? csview_from(fallback) : (csview){0}
    }, d);
}

typedef struct DisplaySetProp {
    char prop; // manual enum, see display_set_prop_display() for values
    bool val;
} DisplaySetProp;
static inline DisplayCtx display_set_prop_display(DisplaySetProp me, DisplayCtx d) {
    switch (me.prop) {
        case 1: d.drop_after_display = me.val; break;
        case 2: d.debug_mode         = me.val; break;
    }
    return d;
}
#define consume_d(item) ((DisplaySetProp){1, true}), item, ((DisplaySetProp){1, false})
#define debug_d(item)   ((DisplaySetProp){2, true}), item, ((DisplaySetProp){2, false})
