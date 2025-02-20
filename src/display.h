#pragma once

#include "span.h"

typedef struct Value Value;
typedef struct Type Type;
typedef struct InterfaceMember InterfaceMember;
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

typedef struct Declaration Declaration;
typedef Declaration const* DeclarationCPtr;

#define i_key_names DeclarationCPtr, declaration_cptr
#include "wrap_stc/hset.h"

typedef struct HashKeyTarget {
    cstr target;
    HsetDeclarationCPtr declarations;
} HashKeyTarget;

typedef struct Display {
    union {
        // The first element of all other union members is a cstr, so it is safe and well-defined to access target in
        // all cases.
        cstr* target;
        HashKeyTarget* hash_key_target;
    };
    unsigned char indent;
    bool color : 1;
    bool debug : 1;
    bool hash_key : 1;
} Display;

static inline Display error_display(cstr* target) {
    return (Display) {
        .target = target,
    };
}

static inline Display hash_key_display(HashKeyTarget* target) {
    return (Display) {
        .hash_key_target = target,
        .hash_key = true,
    };
}

static inline Display debug_display(cstr* target) {
    return (Display) {
        .target = target,
        .color = true,
        .debug = true,
    };
}

#define display_lit(d, s) display_sv(d, c_sv(s))
#define display_colored(d, c, s) do { display_color(d, c); display_lit(d, s); display_normal(d); } while(false)

static inline void display_str  (Display d, const char* s)  { cstr_append   (d.target, s); }
static inline void display_s    (Display d, cstr        s)  { cstr_append_s (d.target, s); }
static inline void display_sv   (Display d, csview      s)  { cstr_append_sv(d.target, s); }
static inline void display_span (Display d, Span        s)  { cstr_append_span(d.target, s); }

static inline void display_newline(Display d) {
    if (d.debug) {
        display_lit(d, "\n");
        for (int i = 0; i < d.indent; i++) {
            cstr_append_sv(d.target, indent_string);
        }
    } else {
        display_lit(d, " ");
    }
}

static inline void display_color(Display d, const char* code) {
    if (d.color) display_str(d, code);
}

static inline void display_normal(Display d) {
    display_color(d, color_normal);
}

static inline void display_colored_span(Display d, const char* code, Span s) {
    display_color(d, code);
    display_span(d, s);
    display_normal(d);
}

void display_fmt(Display d, const char* fmt, ...) __attribute__((format (printf, 2, 3)));
void display_value(Display d, Value const* me);
void display_interface_member(Display d, InterfaceMember const* me);
void display_type(Display d, Type const* me);
void display_expr(Display d, Expr const* me);
void display_statement(Display d, Statement const* me);
void display_block(Display d, Block const* me);

// Useful for calling from GDB
void print_type_dbg(Type const* me);
