#pragma once

#include "span.h"

typedef struct Value Value;
typedef struct TypeNode TypeNode;
typedef struct InterfaceMember InterfaceMember;
typedef struct Expr Expr;
typedef struct Statement Statement;
typedef struct Block Block;
typedef struct Error Error;
typedef struct Ty Ty;

// An ANSI escape code. Displayable through both tttt's Display (below) and cleat's DisplayCtx; in either case nothing
// is emitted unless the target has color enabled.
typedef struct AnsiColor {
    csview code;
} AnsiColor;

impl_display_no_drop(AnsiColor, ansi_color, static inline) { return color_display(me.code, d); }

extern AnsiColor color_none; // Emits nothing, for `cond ? color_x : color_none`
extern AnsiColor color_normal;
extern AnsiColor color_error;
extern AnsiColor color_good;
extern AnsiColor color_highlighted_error;
extern AnsiColor color_highlighted_good;

extern AnsiColor color_path;
extern AnsiColor color_status;
extern AnsiColor color_line_number;
extern AnsiColor color_run_percent;
extern AnsiColor color_implicit;

extern AnsiColor color_name;
extern AnsiColor color_value;
extern AnsiColor color_string;
extern AnsiColor color_number;
extern AnsiColor color_type;

extern const csview indent_string;

// A string repeated n times, for use in a cleat display sequence: println(repeated_d("-", 20))
typedef struct RepeatedD {
    csview sv;
    u64 count;
} RepeatedD;

#define repeated_d(str, n) ((RepeatedD){ .sv = c_sv(str), .count = (u64)(n) })
impl_display_no_drop(RepeatedD, repeated_d, static inline) { return repeated_display(me.sv, me.count, d); }

// Moves target's contents into ctx, so a display sequence seeded with it appends rather than replaces
static inline DisplayCtx display_ctx_take(DisplayCtx ctx, cstr* target) {
    ctx.target = *target;
    *target = cstr_null;
    return ctx;
}

// Appends a display sequence to an existing cstr, the counterpart to cstr_d() for incremental building. ctx seeds the
// formatting options: DEFAULT_DISPLAY for plain text, PRINT_DISPLAY to keep colors.
#define cstr_append_d(target, ctx, ...) \
    (*(target) = display_into_cstr(display_sequence(display_ctx_take(ctx, target), __VA_ARGS__)))

// tttt's own display context. It sits on the typechecker's hot path (every type comparison that produces a message
// builds one of these), so it stays a thin wrapper around a borrowed cstr rather than an owning cleat DisplayCtx.
// The declare_ast_display() bridge below is what connects it to cleat's display sequences.
typedef struct Display {
    cstr* target;
    unsigned char indent;
    bool color : 1;
    bool debug : 1;
    bool brief : 1;
    bool parsed : 1;
    bool nested : 1;
} Display;

static inline Display debug_display(cstr* target) {
    return (Display) {
        .target = target,
        .color = true,
        .debug = true,
    };
}

// Borrows a cleat DisplayCtx as a tttt Display. The caller keeps ownership of d->target; d.debug_mode selects the
// debug rendering, so a plain sequence gets the error-message form and debug_d(x) gets the developer form.
static inline Display display_borrow_ctx(DisplayCtx* d) {
    return (Display) {
        .target = &d->target,
        .indent = d->indent,
        .color = d->color,
        .debug = d->debug_mode,
    };
}

#define display_lit(d, s) display_sv(d, c_sv(s))
#define display_colored(d, c, s) do { display_color(d, c); display_lit(d, s); display_normal(d); } while(false)

static inline void display_str  (Display d, const char* s)  { cstr_append   (d.target, s); }
static inline void display_s    (Display d, cstr        s)  { cstr_append_s (d.target, s); }
static inline void display_sv   (Display d, csview      s)  { cstr_append_sv(d.target, s); }
static inline void display_span (Display d, Span        s)  { cstr_append_span(d.target, s); }
static inline void display_i64  (Display d, i64         i)  { cstr_append_fmt(d.target, "%" PRId64, i); }

static inline void display_repeated(Display d, csview sv, intptr_t n) {
    for (intptr_t i = 0; i < n; i++) display_sv(d, sv);
}

static inline void display_newline(Display d) {
    if (d.debug) {
        display_lit(d, "\n");
        display_repeated(d, indent_string, d.indent);
    } else if (d.parsed) {
        display_repeated(d, indent_string, d.indent);
    } else {
        display_lit(d, " ");
    }
}

static inline void display_color(Display d, AnsiColor color) {
    if (d.color) display_sv(d, color.code);
}

static inline void display_normal(Display d) {
    display_color(d, color_normal);
}

static inline void display_colored_span(Display d, AnsiColor color, Span s) {
    display_color(d, color);
    display_span(d, s);
    display_normal(d);
}

void display_value(Display d, Value const* me);
void display_interface_member(Display d, InterfaceMember const* me);
void display_type(Display d, TypeNode const* me);
void display_expr(Display d, Expr const* me);
void display_statement(Display d, Statement const* me);
void display_block(Display d, Block const* me);
void display_error(Display d, Error const* me);

// Makes an AST node displayable in cleat display sequences by wrapping its display_*() function above. Both the const
// and non-const pointer types get an entry, since _Generic only ever matches exactly.
#define declare_ast_display(T, prefix) \
    declare_display(T const*, prefix); \
    static inline DisplayCtx c_JOIN(prefix, _mut_display)(T* me, DisplayCtx d) { \
        return c_JOIN(prefix, _display)(me, d); \
    }

#define AST_DISPLAY_ENTRIES(T, prefix) \
    T const*: c_JOIN(prefix, _display), \
    T*: c_JOIN(prefix, _mut_display)

// The matching definition, one per AST node in display.c (error.c for Error)
#define impl_ast_display(T, prefix) \
    impl_display_no_drop(T const*, prefix) { \
        c_JOIN(display_, prefix)(display_borrow_ctx(&d), me); \
        return d; \
    }

declare_ast_display(Value, value)
declare_ast_display(InterfaceMember, interface_member)
declare_ast_display(TypeNode, type)
declare_ast_display(Expr, expr)
declare_ast_display(Statement, statement)
declare_ast_display(Block, block)
declare_ast_display(Error, error)

declare_display(Span, span);
// A semantic type as tsc prints it in messages (by the current checker)
declare_display(Ty const*, ty_d);

// Wrap a TypeNode in a display sequence to get the truncated debug form: brief_d() is to debug_d() what a summary is to
// the whole tree, handy for logging a type that could be enormous
typedef struct BriefType {
    TypeNode const* type;
} BriefType;

#define brief_d(t) ((BriefType){ .type = (t) })
declare_display(BriefType, brief_type);

// Redefine DISPLAYABLES as `TTTT_DISPLAYABLES, MyType: my_type_display` in a .c file to add a local displayable
#undef DISPLAYABLES
#define TTTT_DISPLAYABLES \
    CORE_DISPLAYABLES, \
    STRING_COLLECTION_DISPLAYABLES, \
    AnsiColor: ansi_color_display, \
    RepeatedD: repeated_d_display, \
    Span: span_display, \
    Ty const*: ty_d_display, \
    BriefType: brief_type_display, \
    AST_DISPLAY_ENTRIES(Value, value), \
    AST_DISPLAY_ENTRIES(InterfaceMember, interface_member), \
    AST_DISPLAY_ENTRIES(TypeNode, type), \
    AST_DISPLAY_ENTRIES(Expr, expr), \
    AST_DISPLAY_ENTRIES(Statement, statement), \
    AST_DISPLAY_ENTRIES(Block, block), \
    AST_DISPLAY_ENTRIES(Error, error)

#define DISPLAYABLES TTTT_DISPLAYABLES

// Useful for calling from GDB
void print_type_dbg(TypeNode const* me);
