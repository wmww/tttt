#pragma once

#include "span.h"

typedef struct Type Type;

typedef enum DisplayFlags {
    DISPLAY_FLAGS_NONE,
    // For correctly generating the .types file in the baseline tests. Implies d.target_with_underline is valid.
    DISPLAY_FLAGS_UNDERLINE_TYPES = 1 << 0,
} DisplayFlags;

// Used for underlining type with ^^^s for the tests
typedef struct StringWithUnderline {
    cstr main_str;
    cstr underline_str;
    // How much longer or shorter than underline is than the main string
    intptr_t underline_offset;
    intptr_t inhibit_underline_count; // '^' if 0, ' ' if >0
    bool underline_any;
    bool underlines_have_been_added;
} StringWithUnderline;

typedef struct Display {
    union {
        // The first element of all other union members is a cstr, so it is safe and well-defined to access target in
        // all cases.
        cstr* target;
        StringWithUnderline* target_with_underline;
    };
    DisplayFlags flags;
} Display;

static inline Display display_error_msg(cstr* target) {
    return (Display) {
        .target = target,
        .flags = DISPLAY_FLAGS_NONE,
    };
}
static inline Display display_with_underline_types(StringWithUnderline* target) {
    return (Display) {
        .target_with_underline = target,
        .flags = DISPLAY_FLAGS_UNDERLINE_TYPES,
    };
}
static inline Display display_add_flags(Display d, DisplayFlags to_add) {
    d.flags |= to_add;
    return d;
}
static inline Display display_remove_flags(Display d, DisplayFlags flags) {
    d.flags &= ~flags;
    return d;
}

static inline void display_str  (Display d, const char* s)  { cstr_append   (d.target, s); }
static inline void display_s    (Display d, cstr        s)  { cstr_append_s (d.target, s); }
static inline void display_sv   (Display d, csview      s)  { cstr_append_sv(d.target, s); }
static inline void display_span (Display d, Span        s)  { cstr_append_sv(d.target, span_sv(&s)); }
#define display_lit(d, s) display_sv(d, c_sv(s))

void display_type(Display d, Type const* me);
