#pragma once

#include "span.h"

typedef struct Value Value;
typedef struct Type Type;
typedef struct Expr Expr;
typedef struct Statement Statement;
typedef struct Block Block;

extern const char* color_normal;
extern const char* color_error;
extern const char* color_good;
extern const char* color_highlighted_error;
extern const char* color_highlighted_good;

extern const char* color_path;
extern const char* color_status;
extern const char* color_line_number;
extern const char* color_run_percent;
extern const char* color_implicit;

extern const char* color_name;
extern const char* color_value;
extern const char* color_string;
extern const char* color_number;
extern const char* color_type;

extern const csview indent_string;

// TODO: can these be converted to bitfield bools?
typedef enum DisplayFlags {
    DISPLAY_FLAGS_NONE,
    DISPLAY_FLAGS_COLOR = 1 << 0,
    DISPLAY_FLAGS_DEBUG = 1 << 1,
} DisplayFlags;

typedef struct Display {
    union {
        // The first element of all other union members is a cstr, so it is safe and well-defined to access target in
        // all cases.
        cstr* target;
        // It's unclear if we will in fact need other union members, if not the union can be removed
    };
    DisplayFlags flags;
    unsigned char indent;
} Display;

static inline Display error_display(cstr* target) {
    return (Display) {
        .target = target,
        .flags = DISPLAY_FLAGS_NONE,
    };
}

static inline Display debug_display(cstr* target) {
    return (Display) {
        .target = target,
        .flags = DISPLAY_FLAGS_DEBUG | DISPLAY_FLAGS_COLOR,
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

#define display_lit(d, s) display_sv(d, c_sv(s))
#define display_colored(d, c, s) display_color(d, c); display_lit(d, s); display_normal(d)

static inline void display_str  (Display d, const char* s)  { cstr_append   (d.target, s); }
static inline void display_s    (Display d, cstr        s)  { cstr_append_s (d.target, s); }
static inline void display_sv   (Display d, csview      s)  { cstr_append_sv(d.target, s); }
static inline void display_span (Display d, Span        s)  { cstr_append_sv(d.target, span_sv(&s)); }

static inline void display_newline(Display d) {
    if (d.flags & DISPLAY_FLAGS_DEBUG) {
        display_lit(d, "\n");
        for (int i = 0; i < d.indent; i++) {
            cstr_append_sv(d.target, indent_string);
        }
    } else {
        display_lit(d, " ");
    }
}

static inline void display_color(Display d, const char* code) {
    if (d.flags & DISPLAY_FLAGS_COLOR) display_str(d, code);
}

static inline void display_normal(Display d) {
    display_color(d, color_normal);
}

static inline void display_colored_span(Display d, const char* code, Span s) {
    display_color(d, code);
    display_span(d, s);
    display_normal(d);
}

void display_fmt(Display d, const char* fmt, ...) __attribute__ ((format (printf, 2, 3)));
void display_value(Display d, Value const* me);
void display_type(Display d, Type const* me);
void display_expr(Display d, Expr const* me);
void display_statement(Display d, Statement const* me);
void display_block(Display d, Block const* me);
