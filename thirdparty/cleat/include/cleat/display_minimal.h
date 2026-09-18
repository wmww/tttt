#pragma once

#include <stc/cstr.h>
#include <stc/csview.h>

typedef struct DisplayCtx DisplayCtx;
struct DisplayCtx {
    cstr target;
    unsigned char indent;
    bool color : 1;
    bool multiline : 1;
    bool drop_after_display : 1;
    bool debug_mode : 1; // Put quotes around strings and such
};

static inline void display_ctx_drop(DisplayCtx* d) {
    cstr_drop(&d->target);
}

static inline void display_ctx_reserve(DisplayCtx* d, isize extra_capacity) {
    cstr_reserve(&d->target, cstr_size(&d->target) + extra_capacity);
}

static inline cstr display_into_cstr(DisplayCtx d) {
    return d.target;
}

#define declare_display(type, prefix, ...) \
    __VA_ARGS__ DisplayCtx c_JOIN(prefix, _display)(type, DisplayCtx)

#define impl_display_no_drop(type, prefix, ...) \
    __VA_ARGS__ DisplayCtx c_JOIN(prefix, _display)(type me, DisplayCtx d)

#define impl_display_class(type, prefix, ...) \
    static inline DisplayCtx c_JOIN(prefix, _display_internal)(type const* me, DisplayCtx d); \
    impl_display_no_drop(type, prefix, __VA_ARGS__) { \
        bool drop_after_display = d.drop_after_display; \
        d.drop_after_display = false; \
        d = c_JOIN(prefix, _display_internal)(&me, d); \
        if (drop_after_display) c_JOIN(prefix, _drop)(&me); \
        return d; \
    } \
    static inline DisplayCtx c_JOIN(prefix, _display_internal)(type const* me, DisplayCtx d)
