#include "parser.h"
#include "block.h"
#include "interface.h"
#include "interface_member.h"
#include "span.h"
#include "modifier_list.h"
#include "statement.h"
#include "port_statement.h"
#include "several_types.h"
#include "generic.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "interface_decl.h"
#include "callable_type.h"
#include "type.h"
#include "error.h"
#include "source_file.h"
#include "math_helpers.h"
#include "string_helpers.h"

#define g_type VecU8, vec_u8
#define g_key uint8_t, u8
#include <cleat/generic/vec.h>

typedef struct JsDocRange {
    const char* start; // The `/**`
    const char* end; // Past the `*/`
} JsDocRange;
#define g_key JsDocRange, jsdoc_range
#include <cleat/generic/vec.h>

#define MAX_ERRORS 500 // Must be less than the max value storable in Parser.error_count

typedef struct ParserShared {
    SourceFile const* source;
    const char* fail_cache_start;
    VecU8 fail_cache;
    VecError errors;
    // What kind of function the parser is currently inside, decides if `await` and `yield` are operators
    bool in_async;
    bool in_generator;
    int function_depth;
    bool in_static_block; // Directly in a class `static {}` block, not in a function inside it
    bool in_parameters; // In the parameter list of the current function: `yield`/`await` there are TS2523/TS2524
    int jsx_children_depth; // Inside the children of a JSX element (at any depth): see parse_jsx_attributes()
    // What moduleDetection looks at (SourceFile.has_import_meta, has_jsx): set by attempts that backtrack too
    bool saw_import_meta;
    bool saw_jsx;
    Span module_exports_span; // See SourceFile.module_exports_span
    bool saw_commonjs; // A `require()` call, an assignment to `exports.x` or `module.exports` (a JavaScript file's)
    // JavaScript files: every `/** */` comment hopped so far (not `/**/`), sorted by start. Hosts find theirs by looking
    // back from their first token (jsdoc_before())
    VecJsDocRange jsdoc_comments;
    // Parsing a type inside a JSDoc comment: a line's leading `*` is whitespace, `*/` ends everything, the JSDoc-only
    // type forms (`?T`, `T=`, `*`, `function(...)`...) are allowed
    bool in_jsdoc;
    const char* jsdoc_end; // The `*/` of the comment being parsed, with in_jsdoc
} ParserShared;

typedef struct FunctionFlags {
    bool is_async;
    bool is_generator;
} FunctionFlags;

static const FunctionFlags function_flags_null = {0};

typedef struct FunctionCtx {
    bool in_async;
    bool in_generator;
    bool in_static_block;
    bool in_parameters;
} FunctionCtx;

void parser_shared_drop(ParserShared* me) {
    vec_u8_drop(&my fail_cache);
    vec_jsdoc_range_drop(&my jsdoc_comments);
    vec_error_drop(&my errors);
}

typedef enum WhitespaceType {
    WHITESPACE_NONE = 0,
    WHITESPACE_PARSED,
    WHITESPACE_NEWLINE,
} WhitespaceType;

typedef struct ExprCtx {
    bool reject_in_operator;
    bool reject_operators;
    bool entity_name; // The operand of a `typeof` type: `a.b.c<T>`, no calls and no `[i]` (that is an indexed access type)
    bool decorator; // `@dec [x]`: the `[` starts a computed member name, not an element access (tsc's inDecoratorContext)
    bool is_parsing_ternary;
    int min_precedence;
} ExprCtx;

static const ExprCtx expr_ctx_null = {0};

typedef enum FailCacheFlag {
    FAIL_CACHE_EXPR = 1 << 0,
    FAIL_CACHE_ARROW_FUNCTION = 1 << 1,
    FAIL_CACHE_TYPE = 1 << 2,
} FailCacheFlag;

typedef struct Parser Parser;
typedef struct Parser {
    ParserShared* shared; // State that is shared across all parsers parsing a single document
    Parser* parent; // the parser this parser was created from (and will merge it's result into on success)
    const char* pos; // current position in the code
    WhitespaceType whitespace; // the type of whitespace detected in the most recent call to hop_whitespace()
    const char* whitespace_end; // the end of the most recent whitespace (whitespace type is retained if same as pos)
    uint8_t fail_cache_flag;
    uint16_t error_count; // Number of errors added to this parser, includes children only after they succeed
#ifdef DEBUG_BUILD
    bool has_panding_child;
#endif
} Parser;

typedef enum TypePrecedence {
    TYPE_PRECEDENCE_NULL = 0,
    TYPE_PRECEDENCE_TERNARY,
    TYPE_PRECEDENCE_UNION,
    TYPE_PRECEDENCE_INTERSECTION,
    TYPE_PRECEDENCE_OPERATOR, // The operand of `keyof`: `keyof A & B` is `(keyof A) & B`, `keyof A[]` is `keyof (A[])`
} TypePrecedence;

static bool hop_token(Parser* parent);

// Takes path. The file owns it: what a failed parse attempt leaves behind is merely one more file looked for
static ModuleRef* add_module_ref(Parser* p, cstr path, Span span) {
    return source_file_add_module_ref((SourceFile*)p->shared->source, path, span);
}

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void append_code_point(cstr* str, u32 code) {
    char buf[8];
    int n = cutf8_encode(buf, code);
    cstr_append_n(str, buf, n);
}

// The escape sequence after a backslash (p is on the character after it): appends its value and moves past it.
// False for a malformed \x or \u escape (nothing consumed beyond the letter, the caller reports)
static bool parse_string_escape(Parser* p, cstr* str) {
    char c = *p->pos;
    p->pos++;
    switch (c) {
        case 'n': cstr_append_lit(str, "\n"); return true;
        case 'r': cstr_append_lit(str, "\r"); return true;
        case 't': cstr_append_lit(str, "\t"); return true;
        case 'b': cstr_append_lit(str, "\b"); return true;
        case 'f': cstr_append_lit(str, "\f"); return true;
        case 'v': cstr_append_lit(str, "\v"); return true;
        case '0':
            if (*p->pos >= '0' && *p->pos <= '9') break; // Legacy octal, tsc reports it; the value is the digits
            cstr_append_n(str, "\0", 1);
            return true;
        case '\r':
            if (*p->pos == '\n') p->pos++;
            return true; // Line continuation
        case '\n':
            return true;
        case 'x': {
            int hi = hex_digit_value(p->pos[0]);
            int lo = hi < 0 ? -1 : hex_digit_value(p->pos[1]);
            if (lo < 0) return false;
            p->pos += 2;
            append_code_point(str, (u32)(hi * 16 + lo));
            return true;
        }
        case 'u': {
            u32 code = 0;
            if (*p->pos == '{') {
                const char* q = p->pos + 1;
                int digits = 0;
                for (; hex_digit_value(*q) >= 0; q++, digits++) code = code * 16 + (u32)hex_digit_value(*q);
                if (!digits || *q != '}' || code > 0x10FFFF) return false;
                p->pos = q + 1;
            } else {
                for (int i = 0; i < 4; i++) {
                    int v = hex_digit_value(p->pos[i]);
                    if (v < 0) return false;
                    code = code * 16 + (u32)v;
                }
                p->pos += 4;
                // A surrogate pair written as two \u escapes is one code point
                if (code >= 0xD800 && code <= 0xDBFF && p->pos[0] == '\\' && p->pos[1] == 'u') {
                    u32 low = 0;
                    bool valid = true;
                    for (int i = 0; i < 4 && valid; i++) {
                        int v = hex_digit_value(p->pos[2 + i]);
                        if (v < 0) valid = false; else low = low * 16 + (u32)v;
                    }
                    if (valid && low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        p->pos += 6;
                    }
                }
            }
            append_code_point(str, code);
            return true;
        }
        default: break;
    }
    // Any other character stands for itself (`\'`, `\"`, `\\`, `\a`); a multi-byte one is copied whole
    int size = cutf8_chr_size(p->pos - 1);
    cstr_append_n(str, p->pos - 1, size > 0 ? size : 1);
    p->pos += (size > 1 ? size - 1 : 0);
    return true;
}

static bool parse_literal(Parser* parent, Value* ret);
static bool parse_binary_op(Parser* parent, BinaryOp* op);
static bool parse_left_unary_op(Parser* parent, UnaryOp* op);
static bool parse_right_unary_op(Parser* parent, UnaryOp* op);
static bool parse_block(Parser* parent, bool toplevel, Block* ret);
static bool parse_interface(Parser* parent, Interface* ret);
static bool parse_class_item(Parser* parent, bool is_type, bool is_abstract, ClassItem* ret);
static bool parse_statement(Parser* parent, bool toplevel, Statement* ret);
static inline bool expect_statement(Parser* parent, bool toplevel, Statement* ret);
static bool parse_type(Parser* parent, TypePrecedence min_precedence, TypeNode* ret);
static inline bool parse_type_or_set_to_default(Parser* parent, TypePrecedence min_precedence, TypeNode* ret);
static inline bool expect_type(Parser* parent, TypePrecedence min_precedence, TypeNode* ret);
static bool parse_function_args(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_function_args,
    FunctionImpl* ret
);
static bool parse_number(Parser* parent, double* ret);
static bool parse_numeric_value(Parser* parent, Value* ret);
static bool parse_string(Parser* parent, cstr* ret);
static bool parse_template(Parser* parent, VecExpr* ret, VecCStr* texts);
static bool parse_comma_ops_right_side(Parser* parent, Expr* left_and_ret);
static bool parse_decorators(Parser* parent, VecExpr* ret);
static bool parse_type_compat_literal(Parser* parent, Value* ret);
static bool parse_template_type(Parser* parent, TypeNode* ret);
static bool parse_expr(Parser* parent, ExprCtx ctx, Expr* ret);
static inline bool expect_expr(Parser* parent, ExprCtx ctx, Expr* ret);
static bool jsx_generic_arrow_start(Parser* parent);
static bool parse_binding_content(Parser* parent, Binding* ret);
static void parse_import_attributes_body(Parser* parent, ImportAttributes* ret);
static bool parse_binding_element(Parser* parent, Binding* ret);
static bool parse_binding_type_or_set_to_default(Parser* parent, Binding* ret);
static bool parse_initialization_or_set_to_default(Parser* parent, ExprCtx ctx, Expr* ret);

// tsc's isNamedEvaluationSource(): `value` gets its name from where it is (`x = class {}`). An anonymous class there
// is named by the emitter at runtime (ClassDecl.named_evaluation)
static void mark_named_evaluation(Expr* value, bool computed_key) {
    while (true) {
        if (value->tag == EXPR_AS_TYPE) value = &value->as_type->expr;
        else if (value->tag == EXPR_UNARY_OP && value->unary_op->op == UNARY_OP_RIGHT_BANG) value = &value->unary_op->expr;
        else break;
    }
    if (value->tag == EXPR_CLASS_DECL && !span_valid(value->class_decl->name)) {
        value->class_decl->named_evaluation = computed_key ? CLASS_NAMED_BY_COMPUTED_KEY : CLASS_NAMED_BY_CONTEXT;
    }
}

// tsc's getAssignedName(): an anonymous class expression that is directly (no parentheses) the value of a variable,
// property assignment, binding element or the right operand of a binary expression is called by that name
static void mark_assigned_name(Expr* value, Span name) {
    if (value->tag != EXPR_CLASS_DECL || span_valid(value->class_decl->name)) return;
    if (value->outer_span.sv.size != value->inner_span.sv.size) return;
    value->class_decl->assigned_name = name;
}

static inline Parser parser(Parser* parent) {
#ifdef DEBUG_BUILD
    check(!parent->has_panding_child);
    parent->has_panding_child = true;
#endif
    return (Parser){
        .shared = parent->shared,
        .parent = parent,
        .pos = parent->pos,
        .whitespace = parent->whitespace,
        .whitespace_end = parent->whitespace_end,
        .error_count = 0,
    };
}

/// Must not be used on the root parser, always returns true, drops parser
static inline bool parse_success(Parser* me) {
#ifdef DEBUG_BUILD
    check(my parent->has_panding_child);
    my parent->has_panding_child = false;
#endif
    my parent->pos = my pos;
    my parent->whitespace = my whitespace;
    my parent->whitespace_end = my whitespace_end;
    my parent->error_count += my error_count;
    return true;
}

/// Must not be used on the root parser, always returns false, drops parser
static inline bool parse_fail(Parser* me) {
#ifdef DEBUG_BUILD
    check(my parent->has_panding_child);
    my parent->has_panding_child = false;
#endif
    if (my error_count) {
        vec_error_erase_n(
            &my shared->errors,
            vec_error_size(&my shared->errors) - my error_count,
            my error_count
        );
    }
    if (my fail_cache_flag &&
        my shared->fail_cache_start &&
        my parent->pos >= my shared->fail_cache_start
    ) {
        intptr_t index = my parent->pos - my shared->fail_cache_start;
        if (vec_u8_size(&my shared->fail_cache) <= index) {
            vec_u8_resize(&my shared->fail_cache, index + 1, 0);
        }
        *vec_u8_at_mut(&my shared->fail_cache, index) |= my fail_cache_flag;
    }
    return false;
}

// Returns true if this position has already failed for this flag
static bool fail_cache_register_and_check(Parser* parser, FailCacheFlag flag) {
    // PERF: only activate fail cache once a threashold number of failures has been hit
    if (!parser->shared->fail_cache_start) {
        parser->shared->fail_cache_start = parser->pos;
    }
    if (parser->pos >= parser->shared->fail_cache_start &&
        parser->pos - parser->shared->fail_cache_start < vec_u8_size(&parser->shared->fail_cache) &&
        *vec_u8_at(&parser->shared->fail_cache, parser->pos - parser->shared->fail_cache_start) & flag
    ) {
        return true;
    } else {
        parser->fail_cache_flag = flag;
        return false;
    }
}
#ifdef DEBUG_BUILD
#define FAIL_CACHE_HOOK(parser, flag) if (fail_cache_register_and_check(parser, flag)) return parse_fail(parser)
#else
#define FAIL_CACHE_HOOK(parser, flag) if (fail_cache_register_and_check(parser, flag)) return false
#endif

static void reset_fail_cache(Parser* me) {
    my shared->fail_cache_start = nullptr;
    // _clear() loops in order to drop. there's no drop work, so the loop is not required. this should be safe.
    my shared->fail_cache.size = 0;
}

// Should only be called for the current active parser
static inline Error* add_error_to_parser(Parser* me, Error error) {
#ifdef DEBUG_BUILD
    check(!my has_panding_child);
#endif
    my error_count++;
    return vec_error_push(&my shared->errors, error);
}

static Error* parse_error_s(Parser* me, Span span, unsigned ts_code, cstr message) {
    if (vec_error_size(&my shared->errors) >= MAX_ERRORS) {
        cstr_drop(&message);
        return nullptr;
    } else if (vec_error_size(&my shared->errors) == MAX_ERRORS - 1) {
        cstr_drop(&message);
        ts_code = TS_CODE_NONE;
        message = cstr_lit("Too many errors, additional errors not shown.");
    }
    return add_error_to_parser(me, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code,
        .phase = ERROR_PHASE_PARSE,
    });
}

/// Builds the message from a display sequence
#define parse_error(me, span, ts_code, ...) parse_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

typedef struct Anchor {
    Parser* parser;
    const char* start;
} Anchor;

static inline Anchor anchor(Parser* parser) {
    return (Anchor) {
        .parser = parser,
        .start = parser->pos,
    };
}

static inline Anchor span_starting_at(Parser* parser, Span start) {
    return (Anchor) {
        .parser = parser,
        .start = span_start_ptr(start),
    };
}

static inline Span span_from(Anchor spanner) {
    return span_make(
        spanner.parser->shared->source,
        spanner.start,
        spanner.parser->pos
    );
}

static inline Span span_from_pos(Parser* me) {
    return span_make(my shared->source, my pos, my pos);
}

static inline Span span_from_previous_char(Parser* me) {
    return span_make(my shared->source, my pos - 1, my pos);
}

static Span span_from_token(Parser* me) {
    Parser p = parser(me);
    Anchor a = anchor(&p);
    hop_token(&p);
    parse_fail(&p);
    return span_from(a);
}

// TODO: unicode support: https://util.unicode.org/UnicodeJsps/list-unicodeset.jsp?a=%5Cp%7BID_Start%7D
static inline bool is_identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

// TODO: unicode support: https://util.unicode.org/UnicodeJsps/list-unicodeset.jsp?a=%5Cp%7BID_Continue%7D
static inline bool is_identifier_start_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

static inline bool is_number_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

static inline bool is_number_start_char(char c) {
    return (c >= '0' && c <= '9') || c == '.';
}

// ret can be null
static inline bool parse_identifier_or_keyword(Parser* parent, Span* ret) {
    Anchor a = anchor(parent);
    if (is_identifier_start_char(*parent->pos)) {
        parent->pos++;
        while (is_identifier_char(*parent->pos)) parent->pos++;
        if (ret) {
            *ret = span_from(a);
        }
        return true;
    } else {
        return false;
    }
}

// ret can be null
static bool parse_identifier(Parser* parent, Span* ret) {
    Anchor a = anchor(parent);
    if (!parse_identifier_or_keyword(parent, nullptr)) {
        return false;
    }
    Span s = span_from(a);
    if (span_equals(s, "return") ||
        span_equals(s, "throw") ||
        span_equals(s, "case") ||
        span_equals(s, "in") ||
        span_equals(s, "typeof") ||
        span_equals(s, "case") ||
        span_equals(s, "this") ||
        span_equals(s, "super") ||
        span_equals(s, "default")
    ) {
        parent->pos = a.start;
        return false; // PERF: check for keywords efficiently
    }
    if (ret) {
        *ret = s;
    }
    return true;
}

static inline bool expect_identifier(Parser* parent, Span* ret) {
    if (parse_identifier(parent, ret)) {
        return true;
    } else {
        parse_error(parent, span_from_token(parent), 1003, "Identifier expected.");
        *ret = span_from_pos(parent);
        return false;
    }
}

static inline bool is_js_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n';
}

// `at` is the `@` a comment directive would start with (tsc's commentDirectiveRegExSingleLine/MultiLine matched up to
// there): records a `@ts-expect-error`/`@ts-ignore` (no word boundary after it in tsc either)
static void note_comment_directive(Parser* parent, const char* start, const char* at, const char* end) {
    csview rest = csview_from_range(at + 1, end);
    bool expect_error = csview_starts_with(rest, "ts-expect-error");
    if (!expect_error && !csview_starts_with(rest, "ts-ignore")) return;
    source_file_add_comment_directive((SourceFile*)parent->shared->source, start, end, expect_error);
}

// Backtracking hops comments repeatedly: only what is past the last one recorded is new
static void note_jsdoc_comment(Parser* parent, const char* start, const char* end) {
    VecJsDocRange* comments = &parent->shared->jsdoc_comments;
    if (comments->size && vec_jsdoc_range_back(comments)->start >= start) return;
    vec_jsdoc_range_push(comments, (JsDocRange){ .start = start, .end = end });
}

static inline bool hop_comment(Parser* parent) {
    if (*parent->pos != '/') {
        return false;
    }
    const char* start = parent->pos;
    switch (*(parent->pos + 1)) {
        case '/': {
            (parent->pos) += 2;
            while (*parent->pos && *parent->pos != '\n') {
                (parent->pos)++;
            }
            if (parent->whitespace == WHITESPACE_NONE) {
                parent->whitespace = WHITESPACE_PARSED;
            }
            // /^\/\/\/?\s*@(ts-expect-error|ts-ignore)/
            const char* c = start + 2;
            if (*c == '/') c++;
            while (c < parent->pos && is_js_ascii_space(*c)) c++;
            if (*c == '@' && c < parent->pos) note_comment_directive(parent, start, c, parent->pos);
        }   break;

        case '*': {
            if (parent->whitespace == WHITESPACE_NONE) {
                parent->whitespace = WHITESPACE_PARSED;
            }
            const char* pos = parent->pos;
            const char* last_line_start = pos;
            pos += 2;
            while (*(pos - 1) != '*' || *pos != '/') {
                switch (*pos) {
                    case '\n': parent->whitespace = WHITESPACE_NEWLINE; last_line_start = pos + 1; break;
                    case '\0': return false;
                    default: break;
                }
                pos++;
            }
            pos++;
            parent->pos = pos;
            if (start[2] == '*' && start[3] != '/' && parent->shared->source->is_js) note_jsdoc_comment(parent, start, pos);
            // The last line, trimmed, against /^(?:\/|\*)*\s*@(ts-expect-error|ts-ignore)/
            const char* c = last_line_start;
            while (is_js_ascii_space(*c)) c++;
            while (*c == '/' || *c == '*') c++;
            while (c < pos && is_js_ascii_space(*c)) c++;
            if (*c == '@' && c < pos) note_comment_directive(parent, last_line_start, c, pos);
        }   break;

        default: return false;
    }
    return true;
}

// The byte length of the non-ASCII whitespace (JavaScript's WhiteSpace: NBSP, BOM and category Zs) at c, negated for
// a line terminator (U+2028, U+2029), 0 if there is none
static int unicode_whitespace_size(const char* c) {
    uint8_t b0 = (uint8_t)c[0];
    if (b0 < 0xC2) return 0;
    if (b0 == 0xC2) return (uint8_t)c[1] == 0xA0 ? 2 : 0;
    uint8_t b1 = (uint8_t)c[1];
    uint8_t b2 = b1 ? (uint8_t)c[2] : 0;
    if (b0 == 0xE1) return b1 == 0x9A && b2 == 0x80 ? 3 : 0; // U+1680
    if (b0 == 0xE2 && b1 == 0x80) {
        if (b2 >= 0x80 && b2 <= 0x8A) return 3; // U+2000..U+200A
        if (b2 == 0xA8 || b2 == 0xA9) return -3; // U+2028, U+2029
        if (b2 == 0xAF) return 3; // U+202F
        return 0;
    }
    if (b0 == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3; // U+205F
    if (b0 == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3; // U+3000
    if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) return 3; // U+FEFF
    return 0;
}

static inline void hop_whitespace_full(Parser* parent, bool hop_comments, bool hop_newlines) {
    if (parent->pos == parent->whitespace_end) {
        return;
    }
    parent->whitespace = WHITESPACE_NONE;
    while (true) {
        switch (*parent->pos) {
            case ' ':
            case '\t':
            case '\r':
                parent->pos++;
                break;

            case '\n':
                if (!hop_newlines) {
                    goto ws_end;
                }
                parent->whitespace = WHITESPACE_NEWLINE;
                parent->pos++;
                if (parent->shared->in_jsdoc) {
                    // tsc's skipJsDocLeadingAsterisks: the `*` a comment's line starts with isn't part of the type
                    while (*parent->pos == ' ' || *parent->pos == '\t') parent->pos++;
                    if (*parent->pos == '*' && parent->pos[1] != '/') parent->pos++;
                }
                break;

            case '/':
                if (!hop_comments || !hop_comment(parent)) {
                    goto ws_end;
                }
                break;

            case '\v':
            case '\f':
                parent->pos++;
                break;

            default: {
                int size = unicode_whitespace_size(parent->pos);
                if (!size) goto ws_end;
                if (size < 0) {
                    // U+2028/U+2029 are line terminators
                    if (!hop_newlines) goto ws_end;
                    parent->whitespace = WHITESPACE_NEWLINE;
                    size = -size;
                }
                parent->pos += size;
                break;
            }
        }
        if (parent->whitespace == WHITESPACE_NONE) {
            parent->whitespace = WHITESPACE_PARSED;
        }
    }

ws_end:
    parent->whitespace_end = parent->pos;
}

static inline void hop_whitespace(Parser* parent) {
    hop_whitespace_full(parent, true, true);
}

static inline bool hop_start(Parser* parent, const char* prefix) {
    const char* local_pos = parent->pos;
    while (*prefix) {
        if (!*local_pos || *prefix != *local_pos) {
            return false;
        }
        prefix++;
        local_pos++;
    }
    parent->pos = local_pos;
    return true;
}

static inline bool hop_char(Parser* parent, char c) {
    if (*parent->pos == c) {
        parent->pos++;
        return true;
    } else {
        return false;
    }
}

static inline bool expect_char(Parser* parent, char c) {
    if (hop_char(parent, c)) {
        return true;
    } else {
        parse_error(parent, span_from_token(parent), 1005, "'", c, "' expected.");
        return false;
    }
}

static Error* expected_token_error(Parser* parent, const char* expected) {
    return parse_error(parent, span_from_token(parent), 1005, "'", expected, "' expected.");
}

// The closing bracket of a bracket opened at `open` (tsc's parseExpectedMatchingBrackets: a missing one points at it)
static bool expect_closing(Parser* parent, char close, const char* open) {
    if (hop_char(parent, close)) return true;
    Error* error = parse_error(parent, span_from_token(parent), 1005, "'", close, "' expected.");
    error_add_related_d(error, span_make(parent->shared->source, open, open + 1), 1007,
        "The parser expected to find a '", close, "' to match the '", *open, "' token here.");
    return false;
}

static Error* expected_expression_error(Parser* parent) {
    return parse_error(parent, span_from_token(parent), 1109, "Expression expected.");
}

static inline bool hop_start_if_ends_on_word_break(Parser* parent, const char* prefix) {
    Parser p = parser(parent);
    if (hop_start(&p, prefix)) {
        if (!is_identifier_char(*p.pos)) {
            return parse_success(&p);
        }
    }
    return parse_fail(&p);
}

static inline bool starts_with_followed_by_word_break(Parser* parent, const char* prefix) {
    Parser p = parser(parent);
    bool ret = hop_start_if_ends_on_word_break(&p, prefix);
    parse_fail(&p);
    return ret;
}

static bool hop_token(Parser* parent) {
    switch (*parent->pos) {
        case '\0':
            return false;

        case '(':
        case ')':
        case '{':
        case '}':
        case '[':
        case ']':
            parent->pos++;
            return true;

        case '.':
            if (is_number_char(*(parent->pos + 1))) {
                break;
            } else {
                parent->pos++;
                return true;
            }

        default: break;
    }

    union {
        Value value;
        BinaryOp bin_op;
        UnaryOp un_op;
    } parsed;

    if (parse_literal(parent, &parsed.value)) {
        value_drop(&parsed.value);
        return true;
    } else if (parse_identifier_or_keyword(parent, nullptr)) {
        return true;
    } else if (hop_start(parent, "=>")) {
        return true;
    } else if (parse_binary_op(parent, &parsed.bin_op)) {
        return true;
    } else if (parse_left_unary_op(parent, &parsed.un_op)) {
        return true;
    } else if (parse_right_unary_op(parent, &parsed.un_op)) {
        return true;
    } else {
        parent->pos++;
        return true;
    }
}

static Span hop_token_get_span(Parser* parent) {
    Anchor a = anchor(parent);
    hop_token(parent);
    return span_from(a);
}

static bool parse_modifier_list(Parser* parent, bool is_member, ModifierList* ret) {
    Parser p = parser(parent);
    const char* end_pos = p.pos;
    bool has_static = false;
    bool prev_was_export = false;

    *ret = (ModifierList){0};

    while (true) {
        SpannedModifier mod = {
            .tag = MODIFIER_ENUM_CAP,
            .span = {0},
        };
        if (!parse_identifier_or_keyword(&p, &mod.span)) {
            goto loop_end;
        }

        hop_whitespace(&p);
        // Only `static` can be followed by a line break (tsc's nextTokenCanFollowModifier): `static\n x` is static
        if (p.whitespace == WHITESPACE_NEWLINE && !(is_member && span_equals(mod.span, "static"))) {
            goto loop_end;
        }

        if (!is_identifier_char(*p.pos)) {
            switch (*p.pos) {
                case '[': case '"': case '\'': break;

                case '#': case '*':
                    if (!is_member) {
                        goto loop_end;
                    }
                    break;

                case '@': // `export @decorator class`
                    if (is_member) {
                        goto loop_end;
                    }
                    break;

                case '(':
                case '{':
                case '<': // `export default <div />`
                    if (is_member) {
                        goto loop_end;
                    }
                    break;

                default:
                    // If the current parse is going to succeed at all, what we just parsed must be an identifier not a
                    // modifier. Break without handling it
                    goto loop_end;
            }
        }

        if (span_equals(mod.span, "readonly")) {
            mod.tag = MODIFIER_READONLY;
        } else if (is_member && span_equals(mod.span, "abstract")) {
            mod.tag = MODIFIER_ABSTRACT;
        } else if (span_equals(mod.span, "static")) {
            mod.tag = MODIFIER_STATIC;
            if (has_static) {
                // Why is the logic for static different? I don't know
                break;
            }
            has_static = true;
        } else if (span_equals(mod.span, "accessor")) {
            mod.tag = MODIFIER_ACCESSOR;
        } else if (span_equals(mod.span, "export")) {
            mod.tag = MODIFIER_EXPORT;
        } else if (prev_was_export && span_equals(mod.span, "default")) {
            mod.tag = MODIFIER_EXPORT_DEFAULT;
        } else if (span_equals(mod.span, "async")) {
            mod.tag = MODIFIER_ASYNC;
        } else if (span_equals(mod.span, "declare")) {
            mod.tag = MODIFIER_DECLARE;
        } else if (span_equals(mod.span, "override")) {
            mod.tag = MODIFIER_OVERRIDE;
        } else if (span_equals(mod.span, "private")) {
            mod.tag = MODIFIER_PRIVATE;
        } else if (span_equals(mod.span, "protected")) {
            mod.tag = MODIFIER_PROTECTED;
        } else if (span_equals(mod.span, "public")) {
            mod.tag = MODIFIER_PUBLIC;
        } else if (is_member && span_equals(mod.span, "const")) {
            mod.tag = MODIFIER_CONST;
        } else {
            break;
        }

        vec_spanned_modifier_push(&ret->spanned, mod);
        ret->includes[mod.tag] = true;
        end_pos = span_end_ptr(mod.span);

        prev_was_export = false;
        switch (mod.tag) {
            case MODIFIER_CONST:
                goto loop_end;

            case MODIFIER_EXPORT:
                prev_was_export = true;
                break;

            default:;
        }
    }
    loop_end:

    p.pos = end_pos;
    return parse_success(&p);
}

// tsc's checkGrammarAsyncModifier(): `async` on what isn't a function or a method
static void report_misplaced_async(Parser* p, ModifierList const* modifiers) {
    for_each (i, vec_spanned_modifier, &modifiers->spanned) {
        if (i->tag != MODIFIER_ASYNC) continue;
        parse_error(p, i->span, 1042, "'async' modifier cannot be used here.");
        return;
    }
}

static void modifier_list_validate_during_parse(Parser* parent, ModifierList const* me, Span name, ModifierValidationFlags flags) {
    ErrorCtx errs = { .phase = ERROR_PHASE_PARSE };
    modifier_list_validate(me, name, flags, &errs);
    for_each (i, vec_error, &errs.errors) {
        add_error_to_parser(parent, *i);
        *i = (Error){0};
    }
    error_ctx_drop(&errs);
}

static inline bool expect_binary_op(Parser* parent, BinaryOp expected) {
    Parser p = parser(parent);
    BinaryOp actual;
    if (parse_binary_op(&p, &actual) && actual == expected) {
        parse_success(&p);
        return true;
    } else {
        parse_fail(&p);
        expected_token_error(parent, binary_op_get_zv(expected).str);
        return false;
    }
}

static bool parse_generic_decl_item(Parser* parent, GenericItem* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    // Modifiers: `const T`, `in T`, `out T`, `in out T`. TODO: variance annotations are parsed and dropped
    bool is_const = false;
    while (true) {
        Parser modifier_p = parser(&p);
        bool modifier_const = false;
        if ((hop_start_if_ends_on_word_break(&modifier_p, "in") ||
             hop_start_if_ends_on_word_break(&modifier_p, "out") ||
             (modifier_const = hop_start_if_ends_on_word_break(&modifier_p, "const"))) &&
            (hop_whitespace(&modifier_p), is_identifier_start_char(*modifier_p.pos))
        ) {
            is_const |= modifier_const;
            parse_success(&modifier_p);
        } else {
            parse_fail(&modifier_p);
            break;
        }
    }

    Span name;
    if (!parse_identifier(&p, &name)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    TypeNode extends = type_any;
    bool extends_explicit = false;
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        extends_explicit = true;
        hop_whitespace(&p);
        parse_type_or_set_to_default(&p, TYPE_PRECEDENCE_NULL, &extends);
        hop_whitespace(&p);
    }

    TypeNode fallback = type_any;
    bool fallback_explicit = false;
    if (hop_start(&p, "=")) {
        hop_whitespace(&p);
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &fallback)) {
            fallback_explicit = true;
        } else {
            fallback = type_any;
        }
    }

    *ret = (GenericItem){
        .name = name,
        .full_span = span_from(a),
        .is_const = is_const,
        .extends_explicit = extends_explicit,
        .extends = extends,
        .fallback_explicit = fallback_explicit,
        .fallback = fallback,
    };
    return parse_success(&p);
}

static bool parse_generic_decl(Parser* parent, Generic* ret) {
    Parser p = parser(parent);
    *ret = (Generic){0};

    if (!hop_char(&p, '<')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    while (*p.pos && *p.pos != '>') {
        GenericItem item;
        if (parse_generic_decl_item(&p, &item)) {
            vec_generic_item_push(&ret->items, item);
            hop_whitespace(&p);
        } else if (*p.pos == ',') {
            parse_error(&p, span_from_token(&p), 1110, "Type expected.");
        } else {
            break;
        }

        ignore( *p.pos == '>' || expect_char(&p, ',') );
        hop_whitespace(&p);
    }

    if (generic_empty(ret) && *p.pos != '>') {
        generic_drop(ret);
        return parse_fail(&p);
    }

    expect_char(&p, '>');

    return parse_success(&p);
}

static bool parse_generic_params(Parser* parent, bool fail_on_error, VecSpannedType* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    *ret = vec_spanned_type_init();

    if (!hop_char(&p, '<')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span trailing_comma_span = span_null;
    while (*p.pos && *p.pos != '>') {
        trailing_comma_span = span_null;
        TypeNode t;
        Anchor type_a = anchor(&p);
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
            vec_spanned_type_push(ret, spanned_type(span_from(type_a), t));
            hop_whitespace(&p);
        } else if (*p.pos == ',') {
            vec_spanned_type_push(ret, spanned_type(span_from_pos(&p), (TypeNode){
                .tag = TYPE_ANY,
                .any.unkown_symbol_span = span_from_pos(&p),
            }));
            parse_error(&p, span_from_token(&p), 1110, "Type expected.");
        } else {
            break;
        }

        if (*p.pos != '>') {
            if (expect_char(&p, ',')) {
                trailing_comma_span = span_from_previous_char(&p);
            } else {
                if (fail_on_error) { goto fail; }
                hop_token(&p);
            }
        }
        hop_whitespace(&p);
    }

    if (span_valid(trailing_comma_span)) {
        parse_error(&p, trailing_comma_span, 1009, "Trailing comma not allowed.");
    }

    if (!expect_char(&p, '>')) {
        if (fail_on_error) { goto fail; }
    }

    if (vec_spanned_type_is_empty(ret)) {
        parse_error(&p, span_from(a), 1099, "Type argument list cannot be empty.");
    }

    return parse_success(&p);

fail:
    vec_spanned_type_drop(ret);
    return parse_fail(&p);
}

static bool parse_ternary_type(Parser* parent, TypeNode* left_and_ret) {
    Parser p = parser(parent);
    TypeNode maybe_super = {0};
    TypeNode if_true = {0};
    TypeNode if_false = {0};

    if (!hop_start_if_ends_on_word_break(&p, "extends")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    parse_type(&p, TYPE_PRECEDENCE_TERNARY, &maybe_super);
    hop_whitespace(&p);

    expect_char(&p, '?');
    hop_whitespace(&p);

    parse_type(&p, TYPE_PRECEDENCE_TERNARY, &if_true);
    hop_whitespace(&p);

    expect_char(&p, ':');
    hop_whitespace(&p);

    parse_type(&p, TYPE_PRECEDENCE_TERNARY, &if_false);

    *left_and_ret = type_ternary(c_new(TernaryType, {
        .maybe_sub = *left_and_ret,
        .maybe_super = maybe_super,
        .if_true = if_true,
        .if_false = if_false,
    }));
    return parse_success(&p);
}

static bool parse_tuple(Parser* parent, VecTupleElement* ret) {
    Parser p = parser(parent);
    *ret = vec_tuple_element_init();

    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    while (*p.pos && *p.pos != ']') {
        TupleElement elem = {0};

        if (hop_start(&p, "...")) {
            elem.rest = true;
        }
        hop_whitespace(&p);

        if (hop_start_if_ends_on_word_break(&p, "readonly")) {
            elem.readonly = true;
        }
        hop_whitespace(&p);

        // `name: T` or `name?: T`
        Parser name_p = parser(&p);
        Span name;
        bool named = parse_identifier_or_keyword(&name_p, &name);
        if (named) {
            hop_whitespace(&name_p);
            if (hop_char(&name_p, '?')) {
                elem.optional = true;
                hop_whitespace(&name_p);
            }
            named = hop_char(&name_p, ':');
        }
        if (named) {
            parse_success(&name_p);
            elem.maybe_name = name;
            hop_whitespace(&p);
        } else {
            parse_fail(&name_p);
            elem.optional = false;
        }

        expect_type(&p, TYPE_PRECEDENCE_NULL, &elem.type);
        hop_whitespace(&p);

        if (!named && hop_char(&p, '?')) {
            elem.optional = true;
            hop_whitespace(&p);
        }

        vec_tuple_element_push(ret, elem);

        if (*p.pos != ']' && !expect_char(&p, ',')) {
            return parse_success(&p);
        }
        hop_whitespace(&p);
    }
    expect_char(&p, ']');

    return parse_success(&p);

}

// tsc's nextTokenIsStartOfType, roughly: after a postfix `?`, a type means a conditional type's true branch
static bool is_type_start_char(char c) {
    return is_identifier_start_char(c) || (c >= '0' && c <= '9') || (uint8_t)c >= 0x80 || c == '"' || c == '\'' ||
        c == '`' || c == '{' || c == '[' || c == '(' || c == '<' || c == '-' || c == '*' || c == '?' || c == '!' ||
        c == '|' || c == '&' || c == '.';
}

static TypeNode jsdoc_synthesized(TypeNode node) {
    node.parsed = true;
    node.synthesized = true;
    return node;
}

// JSDoc's type forms that come first: `*` (any), `?` alone (any), `?T` (T | null), `!T` (T)
static bool parse_jsdoc_prefix_type(Parser* parent, TypeNode* ret) {
    Parser p = parser(parent);
    if (hop_char(&p, '*')) {
        *ret = type_any;
        return parse_success(&p);
    }
    if (hop_char(&p, '?')) {
        hop_whitespace(&p);
        char c = *p.pos;
        if (c == ',' || c == '}' || c == ')' || c == '>' || c == '=' || c == '|' || c == ']' || !c) {
            *ret = type_any;
            return parse_success(&p);
        }
        TypeNode inner;
        expect_type(&p, TYPE_PRECEDENCE_NULL, &inner);
        *ret = type_composite_without_simplify(TYPE_UNION, inner, jsdoc_synthesized(type_null));
        return parse_success(&p);
    }
    if (hop_char(&p, '!')) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_OPERATOR, ret);
        return parse_success(&p);
    }
    return parse_fail(&p);
}

// JSDoc's postfix type forms: `T?` (T | null), `T!` (T), `T=` (optional: T | undefined)
static bool parse_jsdoc_postfix_type(Parser* parent, TypeNode* left_and_ret) {
    Parser p = parser(parent);
    if (hop_char(&p, '=')) {
        *left_and_ret = type_composite_without_simplify(TYPE_UNION, *left_and_ret, jsdoc_synthesized(type_undefined));
        left_and_ret->jsdoc_optional = true;
        return parse_success(&p);
    }
    if (hop_char(&p, '!')) {
        return parse_success(&p);
    }
    if (hop_char(&p, '?')) {
        Parser next = parser(&p);
        hop_whitespace(&next);
        bool type_follows = is_type_start_char(*next.pos) && !(next.pos[0] == '*' && next.pos[1] == '/');
        parse_fail(&next);
        if (!type_follows) {
            *left_and_ret = type_composite_without_simplify(TYPE_UNION, *left_and_ret, jsdoc_synthesized(type_null));
            return parse_success(&p);
        }
    }
    return parse_fail(&p);
}

static bool parse_type_right_side(Parser* parent, TypePrecedence min_precedence, TypeNode* left_and_ret) {
    Parser p = parser(parent);
    TypeNode type;

    hop_whitespace(&p);

    if (p.shared->in_jsdoc && parse_jsdoc_postfix_type(&p, left_and_ret)) {
        return parse_success(&p);
    }

    if (p.whitespace != WHITESPACE_NEWLINE && hop_char(&p, '[')) {
        hop_whitespace(&p);
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
            hop_whitespace(&p);
            expect_char(&p, ']');
            *left_and_ret = type_indexed(c_new(IndexedType, {
                .interface = *left_and_ret,
                .index = type,
            }));
            return parse_success(&p);
        } else {
            expect_char(&p, ']');
            *left_and_ret = type_array(c_new(TypeNode, *left_and_ret), false);
            return parse_success(&p);
        }
    }

    if (TYPE_PRECEDENCE_TERNARY >= min_precedence) {
        if (parse_ternary_type(&p, left_and_ret)) {
            return parse_success(&p);
        }
    }

    if (TYPE_PRECEDENCE_INTERSECTION >= min_precedence) {
        if (hop_char(&p, '&') && *p.pos != '&' && *p.pos != '=') {
            hop_whitespace(&p);

            // One level tighter: the caller's loop takes the next `&`, so a long chain doesn't recurse per member
            expect_type(&p, TYPE_PRECEDENCE_OPERATOR, &type);
            *left_and_ret = type_composite_without_simplify(TYPE_INTERSECTION, *left_and_ret, type);
            return parse_success(&p);
        }
    }

    if (TYPE_PRECEDENCE_UNION >= min_precedence) {
        if (hop_char(&p, '|') && *p.pos != '|' && *p.pos != '=') {
            hop_whitespace(&p);

            // Same: generated unions of thousands of members exist (icon names)
            expect_type(&p, TYPE_PRECEDENCE_INTERSECTION, &type);
            *left_and_ret = type_composite_without_simplify(TYPE_UNION, *left_and_ret, type);
            return parse_success(&p);
        }
    }

    return parse_fail(&p);
}

static bool parse_identifier_type(Parser* parent, IdentifierType* ret) {
    Parser p = parser(parent);
    VecSpannedType generic_params = {0};

    Span name;
    VecSpan namespaces = vec_span_init();
    if (!parse_identifier(&p, &name)) {
        return parse_fail(&p);
    }
    const char* end = p.pos;

    hop_whitespace(&p);
    while (hop_char(&p, '.')) {
        if (p.shared->in_jsdoc && *p.pos == '<') break; // `Array.<T>`
        vec_span_push(&namespaces, name);
        hop_whitespace(&p);
        // After the dot any identifier name goes, keywords too (`AtRule.default`)
        Anchor member = anchor(&p);
        if (parse_identifier_or_keyword(&p, nullptr)) {
            name = span_from(member);
        } else {
            expect_identifier(&p, &name);
        }
        end = p.pos;
        hop_whitespace(&p);
    }

    if (p.whitespace != WHITESPACE_NEWLINE) {
        if (parse_generic_params(&p, false, &generic_params)) {
            end = p.pos;
        } else {
            generic_params = vec_spanned_type_init();
        }
    }

    p.pos = end;
    *ret = (IdentifierType){
        .namespaces = namespaces,
        .name = name,
        .generic_params = generic_params,
    };
    return parse_success(&p);
}

static bool parse_inferred_type(Parser* parent, InferredType* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "infer")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span name;
    expect_identifier(&p, &name);
    hop_whitespace(&p);

    TypeNode extends;
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        hop_whitespace(&p);
        if (expect_type(&p, TYPE_PRECEDENCE_NULL, &extends)) {
        }
    } else {
        extends = type_any;
    }

    *ret = (InferredType){
        .name = name,
        .full_span = span_from(a),
        .extends = extends,
    };
    return parse_success(&p);
}

// `import("./a")`, `import("./a").B.C<T>`, `typeof import("./a")`: an IdentifierType whose names are looked up in the
// module instead of the scope
static bool parse_import_type(Parser* parent, TypeNode* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    IdentifierType identifier = {0};
    if (hop_start_if_ends_on_word_break(&p, "typeof")) {
        identifier.import_typeof = true;
        hop_whitespace(&p);
    }
    if (!hop_start_if_ends_on_word_break(&p, "import")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (!hop_char(&p, '(')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    cstr path = cstr_null;
    Anchor path_anchor = anchor(&p);
    bool has_path = parse_string(&p, &path);
    Span path_span = span_from(path_anchor);
    if (!has_path) {
        parse_error(&p, span_from_token(&p), 1141, "String literal expected.");
    }
    hop_whitespace(&p);
    if (hop_char(&p, ',')) {
        // Import attributes: `{ with: { "resolution-mode": "import" } }` (tsc's parseImportType)
        hop_whitespace(&p);
        if (expect_char(&p, '{')) {
            hop_whitespace(&p);
            Anchor keyword = anchor(&p);
            bool is_assert = hop_start_if_ends_on_word_break(&p, "assert");
            if (!is_assert && !hop_start_if_ends_on_word_break(&p, "with")) expected_token_error(&p, "with");
            ImportAttributes* attributes = c_new(ImportAttributes, {
                .keyword = span_from(keyword), .is_assert = is_assert, .elements = vec_import_attribute_init(),
            });
            hop_whitespace(&p);
            expect_char(&p, ':');
            hop_whitespace(&p);
            Anchor body = anchor(&p);
            parse_import_attributes_body(&p, attributes);
            attributes->span = span_from(body);
            identifier.import_attributes = attributes;
            hop_whitespace(&p);
            expect_char(&p, '}');
        }
        hop_whitespace(&p);
    }
    expect_char(&p, ')');

    const char* end = p.pos;
    hop_whitespace(&p);
    while (hop_char(&p, '.')) {
        hop_whitespace(&p);
        Span name;
        if (!parse_identifier_or_keyword(&p, &name)) {
            expect_identifier(&p, &name);
        }
        if (span_valid(identifier.name)) vec_span_push(&identifier.namespaces, identifier.name);
        identifier.name = name;
        end = p.pos;
        hop_whitespace(&p);
    }
    if (p.whitespace != WHITESPACE_NEWLINE && parse_generic_params(&p, false, &identifier.generic_params)) {
        end = p.pos;
    }
    p.pos = end;

    if (has_path) {
        identifier.import_ref = add_module_ref(&p, path, path_span);
        identifier.import_ref->resolution_override = import_attributes_resolution_mode(identifier.import_attributes, nullptr);
    } else {
        cstr_drop(&path);
    }
    identifier.import_span = span_from(a);
    *ret = type_identifier(c_new(IdentifierType, identifier));
    return parse_success(&p);
}

// `new.target` (tsc's MetaProperty), as one keyword identifier
static bool parse_new_target(Parser* parent, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    if (!hop_start_if_ends_on_word_break(&p, "new")) return parse_fail(&p);
    hop_whitespace(&p);
    if (!hop_char(&p, '.')) return parse_fail(&p);
    hop_whitespace(&p);
    if (!hop_start_if_ends_on_word_break(&p, "target")) return parse_fail(&p);
    *ret = expr_identifier((IdentifierExpr){ .name = span_from(a), .keyword = KEYWORD_IDENTIFIER_NEW_TARGET });
    return parse_success(&p);
}

// The `import` of `import("./a")` and `import.meta`, as a keyword identifier (the call and the property access are
// parsed like any other)
static bool parse_import_keyword_expr(Parser* parent, IdentifierExpr* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    if (!hop_start_if_ends_on_word_break(&p, "import")) {
        return parse_fail(&p);
    }
    *ret = (IdentifierExpr){ .name = span_from(a), .keyword = KEYWORD_IDENTIFIER_IMPORT };
    Parser ahead = parser(&p);
    hop_whitespace(&ahead);
    if (*ahead.pos == '.') {
        parse_fail(&ahead);
        p.shared->saw_import_meta = true;
        return parse_success(&p);
    }
    if (!hop_char(&ahead, '(')) {
        parse_fail(&ahead);
        return parse_fail(&p);
    }
    hop_whitespace(&ahead);
    cstr path = cstr_null;
    Anchor path_anchor = anchor(&ahead);
    if (parse_string(&ahead, &path)) {
        Span path_span = span_from(path_anchor);
        hop_whitespace(&ahead);
        // (a template with substitutions isn't a module name anyone can resolve)
        bool is_constant = csview_find_sv(path_span.sv, c_sv("${")) == c_NPOS || *path_span.sv.buf != '`';
        if (is_constant && (*ahead.pos == ')' || *ahead.pos == ',')) {
            ret->module_ref = add_module_ref(&p, path, path_span);
            ret->module_ref->syntax = MODULE_REF_DYNAMIC;
        } else {
            cstr_drop(&path);
        }
    }
    parse_fail(&ahead);
    return parse_success(&p);
}

static bool parse_unique_symbol_type(Parser* parent) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "unique")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!hop_start_if_ends_on_word_break(&p, "symbol")) {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_type(Parser* parent, TypePrecedence min_precedence, TypeNode* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    union {
        IdentifierType identifier;
        InferredType inferred;
        FunctionImpl func;
        TypeNode type;
        VecTupleElement tuple_elements;
    } parsed;

    bool parenthesized = false;
    bool placeholder = false;
    Span readonly_span = span_null;
    if (hop_start_if_ends_on_word_break(&p, "readonly")) {
        readonly_span = span_from(a);
        hop_whitespace(&p);
    }

    Anchor value_anchor = anchor(&p);
    if (p.shared->in_jsdoc && parse_jsdoc_prefix_type(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "any")) {
        *ret = type_any;
    } else if (hop_start_if_ends_on_word_break(&p, "unknown")) {
        *ret = type_unknown;
    } else if (hop_start_if_ends_on_word_break(&p, "never")) {
        *ret = type_never;
    } else if (hop_start_if_ends_on_word_break(&p, "void")) {
        *ret = type_void;
    } else if (hop_start_if_ends_on_word_break(&p, "boolean")) {
        *ret = type_boolean;
    } else if (hop_start_if_ends_on_word_break(&p, "number")) {
        *ret = type_number;
    } else if (hop_start_if_ends_on_word_break(&p, "string")) {
        *ret = type_string;
    } else if (hop_start_if_ends_on_word_break(&p, "symbol")) {
        *ret = type_symbol;
    } else if (hop_start_if_ends_on_word_break(&p, "bigint")) {
        *ret = type_bigint;
    } else if (hop_start_if_ends_on_word_break(&p, "this")) {
        *ret = type_identifier(c_new(IdentifierType, {
            .name = span_from(a),
            .keyword = KEYWORD_IDENTIFIER_THIS,
        }));
    } else if (hop_start_if_ends_on_word_break(&p, "super")) {
        *ret = type_identifier(c_new(IdentifierType, {
            .name = span_from(a),
            .keyword = KEYWORD_IDENTIFIER_THIS,
        }));
    } else if (hop_start_if_ends_on_word_break(&p, "readonly")) {
        *ret = type_string;
    } else if (hop_start_if_ends_on_word_break(&p, "keyof")) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_OPERATOR, &parsed.type);
        *ret = type_keyof(c_new(KeyofType, {
            .interface = parsed.type,
        }));
    } else if (parse_import_type(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "typeof")) {
        hop_whitespace(&p);
        Expr expr;
        ExprCtx expr_ctx = {
            .reject_operators = true,
            .entity_name = true,
        };
        if (!parse_expr(&p, expr_ctx, &expr)) {
            parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
            expr = expr_implicit_any(span_from_pos(&p));
        }
        *ret = type_typeof(c_new(TypeofType, {
            .expr = c_new(Expr, expr),
            .owns_expr = true,
        }));
    } else if (parse_template_type(&p, ret)) {
    } else if (parse_type_compat_literal(&p, &ret->value)) {
        ret->tag = TYPE_VALUE;
        ret->value_span = span_from(value_anchor);
    } else if (hop_start(&p, "-")) {
        hop_whitespace(&p);
        if (parse_numeric_value(&p, &ret->value)) {
            ret->tag = TYPE_VALUE;
            if (ret->value.tag == VALUE_NUMBER) ret->value.number = -ret->value.number;
            else ret->value.bigint = -ret->value.bigint;
            ret->value_span = span_from(value_anchor);
        } else {
            return parse_fail(&p);
        }
    } else if (parse_function_args(&p, true, true, true, &parsed.func)) {
        parsed.func.full_span = span_from(value_anchor);
        *ret = function_impl_into_type(parsed.func);
    } else if (parse_inferred_type(&p, &parsed.inferred)) {
        *ret = type_inferred(c_new(InferredType, parsed.inferred));
    } else if (parse_unique_symbol_type(&p)) {
        *ret = (TypeNode){ .tag = TYPE_UNIQUE_SYMBOL };
    } else if (parse_identifier_type(&p, &parsed.identifier)) {
        parsed.identifier.jsdoc = p.shared->in_jsdoc;
        *ret = type_identifier(c_new(IdentifierType, parsed.identifier));
    } else if (parse_tuple(&p, &parsed.tuple_elements)){
        *ret = type_tuple(parsed.tuple_elements, false);
    } else if (hop_char(&p, '(')) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, ret);
        hop_whitespace(&p);
        expect_char(&p, ')');
        parenthesized = true;
    } else if (parse_interface(&p, &ret->interface)) {
        ret->tag = TYPE_INTERFACE;
    } else if (*p.pos == '|' || *p.pos == '&') {
        *ret = *p.pos == '|' ? type_never : type_any;
        placeholder = true;
    } else {
        return parse_fail(&p);
    }

    ret->parsed = true;
    // (Some branches above only write part of `ret`, over what a failed attempt may have left)
    ret->parenthesized = parenthesized;
    ret->placeholder = placeholder;
    if (!p.shared->in_jsdoc || ret->tag != TYPE_UNION) ret->jsdoc_optional = false;
    ret->from_jsdoc = false;
    ret->synthesized = false;

    if (span_valid(readonly_span)) {
        // `readonly` applies to the whole postfix chain: `readonly T[][]` is a readonly array of `T[]`
        while (parse_type_right_side(&p, TYPE_PRECEDENCE_OPERATOR, ret)) {}
        if (ret->tag == TYPE_ARRAY) {
            ret->array.readonly = true;
            readonly_span = span_null;
        } else if (ret->tag == TYPE_TUPLE) {
            ret->tuple.readonly = true;
            readonly_span = span_null;
        }
    }
    while (parse_type_right_side(&p, min_precedence, ret)) {}
    ret->parsed = true; // (Also what the right side made)

    if (span_valid(readonly_span)) {
        parse_error(
            &p,
            readonly_span,
            1354,
            "'readonly' type modifier is only permitted on array and tuple literal types."
        );
    }

    return parse_success(&p);
}

static inline bool parse_type_or_set_to_default(Parser* parent, TypePrecedence min_precedence, TypeNode* ret) {
    if (parse_type(parent, min_precedence, ret)) {
        return true;
    } else {
        *ret = (TypeNode){0};
        return false;
    }
}

// JSDoc (JavaScript files only). tsc parses the `/** */` comments in a node's leading trivia and attaches them to it;
// here whoever parses a host looks back from its first token for the comment that ends there (jsdoc_before()) and
// moves what the tags say into the node's own slots (an `@type` is the annotation, an `@param` a parameter's...), so
// the checker can't tell a JSDoc annotation from a written one. Types in tags are parsed by the type parser, pointed
// into the comment with `in_jsdoc` set

typedef enum JsDocTagKind {
    JSDOC_TAG_OTHER = 0,
    JSDOC_TAG_TYPE,
    JSDOC_TAG_PARAM, // Also `@arg`, `@argument`
    JSDOC_TAG_RETURNS, // Also `@return`
    JSDOC_TAG_TEMPLATE,
    JSDOC_TAG_TYPEDEF,
    JSDOC_TAG_CALLBACK,
    JSDOC_TAG_PROPERTY, // Also `@prop`
    JSDOC_TAG_ENUM,
    JSDOC_TAG_THIS,
    JSDOC_TAG_EXTENDS, // Also `@augments`
    JSDOC_TAG_IMPLEMENTS,
    JSDOC_TAG_CLASS, // Also `@constructor`
    JSDOC_TAG_OVERLOAD,
    JSDOC_TAG_SATISFIES,
    JSDOC_TAG_IMPORT,
    JSDOC_TAG_THROWS,
    JSDOC_TAG_PRIVATE,
    JSDOC_TAG_PROTECTED,
    JSDOC_TAG_PUBLIC,
    JSDOC_TAG_READONLY,
    JSDOC_TAG_OVERRIDE,
} JsDocTagKind;

typedef struct JsDocTag {
    JsDocTagKind kind;
    Span tag_name; // `param` of `@param`
    bool has_type;
    TypeNode type; // Of the `{T}`, owned while has_type
    Span type_span; // Inside the braces
    bool variadic; // `{...T}`
    Span name; // `@param {T} name`, `@typedef {T} Name`, `@template T`: the name (maybe dotted: `o.a`)
    bool bracketed; // `[name]`, `[name=default]`: optional
    // A `@param`/`@returns` of a `@callback`, a `@property` of a `@typedef`: part of that, not about the host
    bool nested;
} JsDocTag;

static void jsdoc_tag_drop(JsDocTag* me) {
    if (my has_type) type_drop(&my type);
}

#define g_class_key JsDocTag, jsdoc_tag
#define g_no_clone
#include <cleat/generic/vec.h>

static inline bool is_jsdoc_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Past whitespace, newlines and the leading `*`s of lines, not past `end`
static const char* jsdoc_skip_space(const char* c, const char* end) {
    while (c < end) {
        if (is_jsdoc_space(*c)) {
            c++;
            continue;
        }
        if (*c != '*') break;
        const char* line = c;
        while (line[-1] == ' ' || line[-1] == '\t') line--;
        if (line[-1] != '\n') break;
        c++;
    }
    return c;
}

// Parses `{T}` at `*pos` (which must be the `{`) into `tag`, moves `*pos` past it. The parse errors of JSDoc types are
// dropped
static void parse_jsdoc_type_expression(Parser* parent, const char** pos, const char* end, JsDocTag* tag) {
    Parser p = parser(parent);
    p.pos = *pos + 1;
    p.whitespace_end = nullptr;
    bool outer_in_jsdoc = p.shared->in_jsdoc;
    const char* outer_end = p.shared->jsdoc_end;
    p.shared->in_jsdoc = true;
    p.shared->jsdoc_end = end;
    hop_whitespace(&p);
    bool variadic = hop_start(&p, "...");
    Anchor a = anchor(&p);
    TypeNode type;
    if (parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
        Span type_span = span_from(a);
        hop_whitespace(&p);
        if (p.pos < end) {
            if (tag->has_type) type_drop(&tag->type);
            tag->has_type = true;
            tag->type = type;
            tag->type_span = type_span;
            tag->variadic = variadic;
            if (*p.pos == '}') p.pos++;
            *pos = p.pos;
        } else {
            type_drop(&type);
        }
    }
    // Past the braces even when the type didn't parse (tsc: `{` starts a type expression that ends at `}`)
    if (!tag->has_type) {
        const char* c = *pos;
        while (c < end && *c != '}' && *c != '\n') c++;
        *pos = c < end && *c == '}' ? c + 1 : c;
    }
    p.shared->in_jsdoc = outer_in_jsdoc;
    p.shared->jsdoc_end = outer_end;
    parse_fail(&p);
}

static const char* jsdoc_parse_name(const char* c, const char* end, Span* ret, SourceFile const* source) {
    const char* start = c;
    while (c < end && (is_identifier_char(*c) || *c == '.' || (uint8_t)*c >= 0x80 ||
        (*c == '[' && c[1] == ']'))
    ) {
        c += *c == '[' ? 2 : 1;
    }
    *ret = c > start ? span_make(source, start, c) : span_null;
    return c;
}

// `@param {T} name`, `@param name {T}`, `@param {T} [name=default]`
static const char* parse_jsdoc_param_like(Parser* parent, const char* c, const char* end, JsDocTag* tag) {
    SourceFile const* source = parent->shared->source;
    c = jsdoc_skip_space(c, end);
    if (c < end && *c == '{') {
        parse_jsdoc_type_expression(parent, &c, end, tag);
        c = jsdoc_skip_space(c, end);
    }
    if (c < end && *c == '[') {
        tag->bracketed = true;
        c = jsdoc_skip_space(c + 1, end);
        c = jsdoc_parse_name(c, end, &tag->name, source);
        // The default, up to the closing bracket
        int depth = 0;
        while (c < end && (*c != ']' || depth)) {
            if (*c == '[') depth++;
            else if (*c == ']') depth--;
            c++;
        }
        if (c < end) c++;
    } else {
        c = jsdoc_parse_name(c, end, &tag->name, source);
    }
    if (!tag->has_type) {
        const char* after = jsdoc_skip_space(c, end);
        if (after < end && *after == '{') {
            parse_jsdoc_type_expression(parent, &after, end, tag);
            c = after;
        }
    }
    return c;
}

static JsDocTagKind jsdoc_tag_kind(csview name) {
    static const struct { const char* name; JsDocTagKind kind; } kinds[] = {
        {"type", JSDOC_TAG_TYPE}, {"param", JSDOC_TAG_PARAM}, {"arg", JSDOC_TAG_PARAM}, {"argument", JSDOC_TAG_PARAM},
        {"returns", JSDOC_TAG_RETURNS}, {"return", JSDOC_TAG_RETURNS}, {"template", JSDOC_TAG_TEMPLATE},
        {"typedef", JSDOC_TAG_TYPEDEF}, {"callback", JSDOC_TAG_CALLBACK}, {"property", JSDOC_TAG_PROPERTY},
        {"prop", JSDOC_TAG_PROPERTY}, {"enum", JSDOC_TAG_ENUM}, {"this", JSDOC_TAG_THIS},
        {"extends", JSDOC_TAG_EXTENDS}, {"augments", JSDOC_TAG_EXTENDS}, {"implements", JSDOC_TAG_IMPLEMENTS},
        {"class", JSDOC_TAG_CLASS}, {"constructor", JSDOC_TAG_CLASS}, {"overload", JSDOC_TAG_OVERLOAD},
        {"satisfies", JSDOC_TAG_SATISFIES}, {"import", JSDOC_TAG_IMPORT}, {"throws", JSDOC_TAG_THROWS},
        {"exception", JSDOC_TAG_THROWS}, {"private", JSDOC_TAG_PRIVATE}, {"protected", JSDOC_TAG_PROTECTED},
        {"public", JSDOC_TAG_PUBLIC}, {"readonly", JSDOC_TAG_READONLY}, {"override", JSDOC_TAG_OVERRIDE},
    };
    for (u32 i = 0; i < c_arraylen(kinds); i++) {
        if (csview_equals(name, kinds[i].name)) return kinds[i].kind;
    }
    return JSDOC_TAG_OTHER;
}

// The tag at `c` (its `@`): appends it, returns where its own syntax ends (what follows is its comment)
static const char* parse_jsdoc_tag(Parser* parent, const char* c, const char* end, VecJsDocTag* tags) {
    SourceFile const* source = parent->shared->source;
    const char* name_start = ++c;
    while (c < end && (is_identifier_char(*c) || *c == '-')) c++;
    JsDocTag tag = {
        .kind = jsdoc_tag_kind(csview_from_range(name_start, c)),
        .tag_name = span_make(source, name_start, c),
    };
    switch (tag.kind) {
        case JSDOC_TAG_PARAM:
        case JSDOC_TAG_PROPERTY:
            c = parse_jsdoc_param_like(parent, c, end, &tag);
            break;
        case JSDOC_TAG_TYPEDEF:
        case JSDOC_TAG_CALLBACK:
        case JSDOC_TAG_TEMPLATE:
            c = jsdoc_skip_space(c, end);
            if (tag.kind != JSDOC_TAG_CALLBACK && c < end && *c == '{') {
                parse_jsdoc_type_expression(parent, &c, end, &tag);
                c = jsdoc_skip_space(c, end);
            }
            c = jsdoc_parse_name(c, end, &tag.name, source);
            break;
        case JSDOC_TAG_TYPE:
        case JSDOC_TAG_RETURNS:
        case JSDOC_TAG_ENUM:
        case JSDOC_TAG_THIS:
        case JSDOC_TAG_SATISFIES:
        case JSDOC_TAG_THROWS:
        case JSDOC_TAG_EXTENDS:
        case JSDOC_TAG_IMPLEMENTS: {
            const char* after = jsdoc_skip_space(c, end);
            if (after < end && *after == '{') {
                parse_jsdoc_type_expression(parent, &after, end, &tag);
                c = after;
            }
        }   break;
        default:
            break;
    }
    vec_jsdoc_tag_push(tags, tag);
    return c;
}

// The tags of the comment `range` (tsc's parseJSDocCommentWorker: a tag starts at an `@` at the start of a line, after
// whitespace and at most one `*`, or after whitespace and before something else anywhere; nothing in backticks)
static void parse_jsdoc_comment(Parser* parent, JsDocRange range, VecJsDocTag* tags) {
    const char* c = range.start + 3;
    const char* end = range.end - 2;
    bool line_start = true;
    bool saw_asterisk = false;
    bool in_backticks = false;
    JsDocTagKind owner = JSDOC_TAG_OTHER; // The @typedef or @callback the tags are part of, if any
    while (c < end) {
        char ch = *c;
        if (ch == '\n') {
            line_start = true;
            saw_asterisk = false;
            c++;
        } else if (ch == ' ' || ch == '\t' || ch == '\r') {
            c++;
        } else if (ch == '*' && line_start && !saw_asterisk) {
            saw_asterisk = true;
            c++;
        } else if (ch == '@' && !in_backticks && (line_start || ((c[-1] == ' ' || c[-1] == '\t') && !is_jsdoc_space(c[1])))) {
            c = parse_jsdoc_tag(parent, c, end, tags);
            line_start = false;
            // tsc's parseCallbackTag/parseTypedefTag take the tags that follow as their children
            JsDocTag* tag = vec_jsdoc_tag_back_mut(tags);
            if (tag->kind == JSDOC_TAG_TYPEDEF || tag->kind == JSDOC_TAG_CALLBACK) {
                owner = tag->kind;
            } else if (owner == JSDOC_TAG_CALLBACK && (tag->kind == JSDOC_TAG_PARAM || tag->kind == JSDOC_TAG_RETURNS ||
                tag->kind == JSDOC_TAG_TEMPLATE)
            ) {
                tag->nested = true;
            } else if (owner == JSDOC_TAG_TYPEDEF && (tag->kind == JSDOC_TAG_PROPERTY || tag->kind == JSDOC_TAG_TEMPLATE)) {
                tag->nested = true;
            } else {
                owner = JSDOC_TAG_OTHER;
            }
        } else {
            if (ch == '`' && !line_start) in_backticks = !in_backticks;
            line_start = false;
            c++;
        }
    }
}

// The JSDoc comment that ends right before `pos` (only whitespace between), parsed into `tags`: false if there is
// none. tsc attaches every JSDoc comment of the leading trivia, but takes the tags of the last one only
// (filterOwnedJSDocTags; the others' `@overload` tags aside)
static bool jsdoc_before(Parser* parent, const char* pos, VecJsDocTag* tags) {
    VecJsDocRange const* comments = &parent->shared->jsdoc_comments;
    if (!comments->size) return false;
    const char* file_start = source_file_start(parent->shared->source);
    const char* c = pos;
    while (c > file_start && is_js_ascii_space(c[-1])) c--;
    if (c - file_start < 4 || c[-1] != '/' || c[-2] != '*') return false;
    // Binary search for the comment ending at c
    isize lo = 0, hi = comments->size;
    while (lo < hi) {
        isize mid = (lo + hi) / 2;
        if (comments->data[mid].end < c) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= comments->size || comments->data[lo].end != c) return false;
    parse_jsdoc_comment(parent, comments->data[lo], tags);
    return true;
}

// The first tag of a kind that has a type, null if there is none
static JsDocTag* jsdoc_find_typed(VecJsDocTag* tags, JsDocTagKind kind) {
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind == kind && i->has_type && !i->nested) return i;
    }
    return nullptr;
}

// Moves a tag's type into an annotation slot
static TypeNode jsdoc_take_type(JsDocTag* tag, Span* span_ret) {
    tag->has_type = false;
    if (span_ret) *span_ret = tag->type_span;
    tag->type.from_jsdoc = true;
    return tag->type;
}

// `@type` on a declaration with no annotation of its own
static void jsdoc_apply_type_to_binding(VecJsDocTag* tags, Binding* binding) {
    if (binding->has_explicit_type) return;
    JsDocTag* tag = jsdoc_find_typed(tags, JSDOC_TAG_TYPE);
    if (!tag) return;
    binding->explicit_type = jsdoc_take_type(tag, &binding->explicit_type_span);
    binding->has_explicit_type = true;
}

// The `@param` tag for a function's parameter (tsc's getJSDocParameterTags): by name, or for a pattern by position
static JsDocTag* jsdoc_param_tag(VecJsDocTag* tags, FunctionImpl const* function, FunctionArg const* arg) {
    Binding const* binding = &arg->initializer.binding;
    if (binding->tag == BINDING_IDENTIFIER) {
        for_each (i, vec_jsdoc_tag, tags) {
            if (i->kind == JSDOC_TAG_PARAM && !i->nested && span_valid(i->name) && csview_equals_sv(i->name.sv, binding->content_span.sv)) {
                return i->has_type ? i : nullptr;
            }
        }
        return nullptr;
    }
    isize index = arg - function->args.data;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_PARAM || i->nested) continue;
        if (!index--) return i->has_type ? i : nullptr;
    }
    return nullptr;
}

// The function an expression is, directly (tsc: a parenthesized one has no JSDoc host above it)
static FunctionImpl* jsdoc_function_of(Expr* expr) {
    if (expr->tag != EXPR_FUNCTION || expr->outer_span.sv.size != expr->inner_span.sv.size) return nullptr;
    return expr->function;
}

// What a function's JSDoc says about its parameters and return type: `@param`, `@returns`
static void jsdoc_apply_function(VecJsDocTag* tags, FunctionImpl* function) {
    for_each (i, vec_function_arg, &function->args) {
        Binding* binding = &i->initializer.binding;
        if (binding->has_explicit_type) continue;
        JsDocTag* tag = jsdoc_param_tag(tags, function, i);
        if (!tag) continue;
        bool variadic = tag->variadic;
        bool bracketed = tag->bracketed;
        binding->explicit_type = jsdoc_take_type(tag, &binding->explicit_type_span);
        binding->has_explicit_type = true;
        if (binding->explicit_type.jsdoc_optional || bracketed) {
            // `{T=}` (of type `T | undefined`, which tsc prints) and `[name]` are optional parameters
            if (i->initializer.value.tag == EXPR_IMPLICIT_ANY) i->explicit_optional = true;
        }
        if (variadic && i->rest) {
            binding->explicit_type = type_array(c_new(TypeNode, binding->explicit_type), false);
            binding->explicit_type.parsed = true;
        }
        binding->explicit_type.from_jsdoc = true;
    }
    if (!function->ret_explicit && !function->is_constructor) {
        JsDocTag* tag = jsdoc_find_typed(tags, JSDOC_TAG_RETURNS);
        if (tag) {
            function->ret = jsdoc_take_type(tag, &function->ret_span);
            function->ret_explicit = true;
        }
    }
}

static inline bool expect_type(Parser* parent, TypePrecedence min_precedence, TypeNode* ret) {
    if (parse_type(parent, min_precedence, ret)) {
        return true;
    } else {
        parse_error(parent, span_from_token(parent), 1110, "Type expected.");
        *ret = (TypeNode){
            .tag = TYPE_ANY,
            .any.unkown_symbol_span = span_from_pos(parent),
        };
        return false;
    }
}

static bool parse_interface(Parser* parent, Interface* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_char(&p, '{')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    VecInterfaceMember members = {0};
    const char* prev_s = nullptr;

    while (*p.pos && *p.pos != '}' && p.pos != prev_s) {
        prev_s = p.pos;
        hop_whitespace(&p);

        ClassItem item;
        if (parse_class_item(&p, true, true, &item)) {
            switch (item.tag) {
                case CLASS_ITEM_METHOD: {
                    if (item.method.accessor) {
                        // `get x(): T` / `set x(v: T)` in a type is a property. The getter's type wins
                        bool is_getter = item.method.accessor == CLASS_ACCESSOR_GET;
                        InterfaceMember* existing = nullptr;
                        for_each (other, vec_interface_member, &members) {
                            if (other->key.tag == INTERFACE_KEY_NAME &&
                                cstr_eq(&other->key.name_string, &item.method.name_string)
                            ) {
                                existing = other;
                            }
                        }
                        TypeNode type = type_any;
                        if (is_getter) {
                            type = type_move(&item.method.func.ret);
                        } else if (vec_function_arg_size(&item.method.func.args)) {
                            type = type_move(&item.method.func.args.data[0].initializer.binding.explicit_type);
                        }
                        if (existing) {
                            vec_span_push(&existing->declarations, item.span);
                            if (is_getter) {
                                type_drop(&existing->type);
                                existing->type = type;
                            } else {
                                type_drop(&type);
                                existing->readonly = false;
                            }
                        } else {
                            InterfaceMember member = interface_member_make(
                                item.span,
                                nullptr,
                                is_getter,
                                false,
                                cstr_move(&item.method.name_string),
                                item.method.name_span,
                                item.method.name_is_value,
                                type
                            );
                            vec_interface_member_push(&members, member);
                        }
                        break;
                    }
                    InterfaceMember member = interface_member_make(
                        item.span,
                        nullptr,
                        false,
                        item.method.is_optional,
                        cstr_move(&item.method.name_string),
                        item.method.name_span,
                        item.method.name_is_value,
                        function_impl_into_type(item.method.func)
                    );
                    check(member.key.tag == INTERFACE_KEY_NAME);
                    member.key.name_is_member_function = true;
                    vec_interface_member_push(&members, member);
                    item.method.func = (FunctionImpl){0};
                }   break;

                case CLASS_ITEM_FIELD:
                    vec_interface_member_push(&members, interface_member_make(
                        item.span,
                        nullptr,
                        item.field.is_readonly,
                        item.field.is_optional,
                        cstr_move(&item.field.name_string),
                        item.field.name_span,
                        item.field.name_is_value,
                        type_move(&item.field.explicit_type)
                    ));
                    break;

                case CLASS_ITEM_INTERFACE_MEMBER:
                    vec_interface_member_push(&members, item.interface_memeber);
                    item.interface_memeber = (InterfaceMember){0};
                    break;

                default:
                    parse_error(&p, span_from_pos(&p), TS_CODE_NONE, "Invalid class item for interface.");
                    break;
            }
            class_item_drop(&item);
        } else {
            parse_error(&p, hop_token_get_span(&p), 1131, "Property or signature expected.");
        }
        hop_whitespace(&p);

        // Typescript accepts EITHER , ; or newlines in interfaces. You can MIX them!
        if (!hop_char(&p, ',') &&
            *p.pos != '}' &&
            p.whitespace != WHITESPACE_NEWLINE
        ) {
            expect_char(&p, ';');
        }
        hop_whitespace(&p);
    }

    expect_char(&p, '}');

    *ret = (Interface) {
        .members = members,
        .parsed = true,
        .span = span_from(a),
    };
    return parse_success(&p);
}

static bool parse_type_decl(Parser* parent, TypeDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "type")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }

    Span name;
    if (!parse_identifier(&p, &name)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Generic generic;
    if (!parse_generic_decl(&p, &generic)) {
        generic = (Generic){0};
    }
    hop_whitespace(&p);

    TypeNode type;
    if (expect_char(&p, '=')) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, &type);
    } else {
        type = type_any;
    }

    *ret = (TypeDecl) {
        .name = name,
        .full_span = span_from(a),
        .generic = generic,
        .type = type,
    };
    return parse_success(&p);
}

static bool parse_interface_decl(Parser* parent, InterfaceDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "interface")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span name;
    if (!parse_identifier(&p, &name)) {
        parse_error(&p, span_from_pos(&p), 1438, "Interface must be given a name.");
        name = span_null;
    }
    hop_whitespace(&p);

    Generic generic;
    if (!parse_generic_decl(&p, &generic)) {
        generic = (Generic){0};
    }
    hop_whitespace(&p);

    vec_spanned_type extends = {0};
    Interface interface;
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        hop_whitespace(&p);

        Span last_comma_span = span_null;
        Span second_last_comma_span = span_null;
        TypeNode last_type = type_any;
        Span last_type_span = span_null;

        while (*p.pos) {
            TypeNode t;
            Anchor type_start = anchor(&p);
            if (parse_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
                if (span_valid(last_type_span)) {
                    vec_spanned_type_push(&extends, spanned_type(last_type_span, last_type));
                }
                last_type_span = span_from(type_start);
                last_type = t;
                if (!span_valid(last_comma_span) && last_type.tag == TYPE_INTERFACE) {
                    second_last_comma_span = span_null;
                    break;
                }
                hop_whitespace(&p);
            } else {
                if (!span_valid(last_type_span) || span_valid(last_comma_span)) {
                    hop_whitespace(&p);
                    expected_expression_error(&p);
                } else {
                    break;
                }
            }

            second_last_comma_span = last_comma_span;

            if (*p.pos == '{' || !*p.pos) {
                last_comma_span = span_null;
            } else if (expect_char(&p, ',')) {
                last_comma_span = span_from_previous_char(&p);
            } else {
                last_comma_span = hop_token_get_span(&p);
            }
            hop_whitespace(&p);
        }

        if (last_type.tag == TYPE_INTERFACE) {
            interface = last_type.interface;
            if (span_valid(second_last_comma_span)) {
                parse_error(&p, second_last_comma_span, 1009, "Trailing comma not allowed.");
            }
        } else {
            vec_spanned_type_push(&extends, spanned_type(last_type_span, last_type));
            interface = (Interface){0};
            expected_token_error(&p, "{");
        }
    } else {
        if (!parse_interface(&p, &interface)) {
            interface = (Interface){0};
            expected_token_error(&p, "{");
            hop_token(&p);
        }
    }

    *ret = (InterfaceDecl) {
        .name = name,
        .full_span = span_from(a),
        .generic = generic,
        .extends = extends,
        .type = type_interface(interface),
    };
    return parse_success(&p);
}

static bool parse_enum_decl(Parser* parent, EnumDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    bool is_const = false;
    if (hop_start_if_ends_on_word_break(&p, "const")) {
        is_const = true;
        hop_whitespace(&p);
    }
    if (!hop_start_if_ends_on_word_break(&p, "enum")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span name;
    expect_identifier(&p, &name);
    hop_whitespace(&p);

    expect_char(&p, '{');
    hop_whitespace(&p);

    VecEnumCase cases = vec_enum_case_init();
    while (*p.pos && *p.pos != '}') {
        EnumCase enum_case = {0};
        cstr string_name;
        Anchor name_anchor = anchor(&p);
        if ((*p.pos == '"' || *p.pos == '\'') && parse_string(&p, &string_name)) {
            // The name is taken from the source, so escape sequences in it aren't processed
            cstr_drop(&string_name);
            enum_case.full_name = span_from(name_anchor);
            enum_case.name = span_make(
                enum_case.full_name.source,
                span_start_ptr(enum_case.full_name) + 1,
                span_end_ptr(enum_case.full_name) - 1
            );
            enum_case.name_is_value = true;
        } else if (parse_identifier_or_keyword(&p, &enum_case.name)) {
            enum_case.full_name = enum_case.name;
        } else {
            expect_identifier(&p, &enum_case.name);
            enum_case.full_name = enum_case.name;
            hop_token(&p);
        }
        hop_whitespace(&p);
        if (hop_char(&p, '=')) {
            hop_whitespace(&p);
            expect_expr(&p, expr_ctx_null, &enum_case.value);
            hop_whitespace(&p);
        } else {
            enum_case.value = expr_implicit_any_no_span;
        }
        ignore( *p.pos == '}' || hop_char(&p, ';') || expect_char(&p, ',') );
        hop_whitespace(&p);
        vec_enum_case_push(&cases, enum_case);
    }

    expect_char(&p, '}');

    *ret = (EnumDecl) {
        .name = name,
        .full_span = span_from(a),
        .is_const = is_const,
        .cases = cases,
    };
    return parse_success(&p);
}

// `x is T`, `this is T`, `asserts x`, `asserts x is T` as a return type (`FunctionImpl.predicate`)
static bool parse_type_predicate(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);

    Span name;
    if (!parse_identifier_or_keyword(&p, &name)) {
        return parse_fail(&p);
    }
    hop_whitespace_full(&p, true, false);
    bool asserts = false;
    if (span_equals(name, "asserts") && !starts_with_followed_by_word_break(&p, "is")) {
        if (!parse_identifier_or_keyword(&p, &name)) {
            return parse_fail(&p);
        }
        asserts = true;
        hop_whitespace_full(&p, true, false);
    }
    TypePredicateNode predicate = { .name = name, .is_this = span_equals(name, "this"), .asserts = asserts, .param_index = -1 };
    if (hop_start_if_ends_on_word_break(&p, "is")) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, &predicate.type);
        predicate.has_type = true;
    } else if (!asserts) {
        return parse_fail(&p);
    }

    ret->ret = asserts ? type_void : type_boolean;
    ret->predicate = c_new(TypePredicateNode, predicate);
    return parse_success(&p);
}

static bool parse_function_args_inner(
    Parser* parent, bool parse_abstract_and_new, bool parse_type_return_syntax, bool parse_ret_type, FunctionImpl* ret
);

static bool parse_function_args(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_ret_type,
    FunctionImpl* ret
) {
    bool outer = parent->shared->in_parameters;
    parent->shared->in_parameters = true;
    bool success = parse_function_args_inner(parent, parse_abstract_and_new, parse_type_return_syntax, parse_ret_type, ret);
    parent->shared->in_parameters = outer;
    return success;
}

static bool parse_function_args_inner(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_ret_type,
    FunctionImpl* ret
) {
    Parser p = parser(parent);
    bool is_definitely_function = false;

    *ret = (FunctionImpl){0};

    if (parse_abstract_and_new) {
        if (hop_start_if_ends_on_word_break(&p, "abstract")) {
            ret->is_abstract_constructor = true;
            hop_whitespace(&p);
        }

        if (hop_start_if_ends_on_word_break(&p, "new")) {
            hop_whitespace(&p);
            ret->is_constructor = true;
        } else if (ret->is_abstract_constructor) {
            return parse_fail(&p);
        }
    }

    if (parse_generic_decl(&p, &ret->generic)) {
        hop_whitespace(&p);
        is_definitely_function = true;
    } else {
        ret->generic = (Generic){0};
    }

    Anchor args_start = anchor(&p);

    if (hop_char(&p, '(')) {
        hop_whitespace(&p);

        while (*p.pos && *p.pos != ')') {
            if (hop_char(&p, ',')) {
                parse_error(&p, span_from_previous_char(&p), 1138, "Parameter declaration expected.");
            } else {
                InitializedBinding initializer = {0};
                const char* before_param = p.pos;
                bool explicit_optional = false;
                bool rest = false;
                Span rest_span = span_null;
                Anchor arg_start = anchor(&p);
                VecExpr decorators = {0};
                ignore parse_decorators(&p, &decorators);

                ModifierList modifier_list;
                parse_modifier_list(&p, true, &modifier_list);
                bool is_readonly_property = modifier_list.includes[MODIFIER_READONLY];
                bool is_parameter_property = is_readonly_property || modifier_list.includes[MODIFIER_PUBLIC] ||
                    modifier_list.includes[MODIFIER_PROTECTED] || modifier_list.includes[MODIFIER_PRIVATE] ||
                    modifier_list.includes[MODIFIER_OVERRIDE];
                uint8_t access = modifier_list.includes[MODIFIER_PRIVATE] ? ACCESS_MODIFIER_PRIVATE :
                    modifier_list.includes[MODIFIER_PROTECTED] ? ACCESS_MODIFIER_PROTECTED : ACCESS_MODIFIER_PUBLIC;
                modifier_list_drop(&modifier_list);
                hop_whitespace(&p);

                if (hop_start(&p, "...")) {
                    hop_whitespace(&p);
                    rest = true;
                    rest_span = span_from(arg_start);
                }

                if (parse_binding_content(&p, &initializer.binding)) {
                } else if (hop_start_if_ends_on_word_break(&p, "this")) {
                    initializer.binding.tag = BINDING_IDENTIFIER;
                    initializer.binding.content_span = span_make(p.shared->source, p.pos - 4, p.pos);
                } else {
                    vec_expr_drop(&decorators);
                    goto drop_and_fail;
                }
                hop_whitespace(&p);

                if (hop_char(&p, '?')) {
                    explicit_optional = true;
                    hop_whitespace(&p);
                }

                parse_binding_type_or_set_to_default(&p, &initializer.binding);
                hop_whitespace(&p);
                if (p.shared->source->is_js && !initializer.binding.has_explicit_type) {
                    // `function f(/** @type {T} */ a)`
                    VecJsDocTag tags = {0};
                    if (jsdoc_before(&p, arg_start.start, &tags)) jsdoc_apply_type_to_binding(&tags, &initializer.binding);
                    vec_jsdoc_tag_drop(&tags);
                }

                parse_initialization_or_set_to_default(&p, expr_ctx_null, &initializer.value);
                if (initializer.binding.tag == BINDING_IDENTIFIER && !rest) mark_named_evaluation(&initializer.value, false);
                hop_whitespace(&p);

                if (vec_function_arg_is_empty(&ret->args) && initializer.binding.has_explicit_type) {
                    is_definitely_function = true;
                }

                initializer.is_rest_parameter = rest;
                vec_function_arg_push(&ret->args, (FunctionArg){
                    .span = span_from(arg_start),
                    .rest_span = rest_span,
                    .initializer = initializer,
                    .explicit_optional = explicit_optional,
                    .rest = rest,
                    .decorators = decorators,
                    .is_parameter_property = is_parameter_property,
                    .is_readonly_property = is_readonly_property,
                    .access = access,
                });

                if (p.pos == before_param) {
                    if (is_definitely_function) {
                        // Error was already added? hopefully?
                        hop_token(&p);
                    } else {
                        goto drop_and_fail;
                    }
                } else {
                    hop_whitespace(&p);
                    ignore( *p.pos == ')' || expect_char(&p, ',') );
                }
            }

            hop_whitespace(&p);
        }

        if (!expect_char(&p, ')')) {
            function_impl_drop(ret);
            *ret = (FunctionImpl){0};
            return parse_success(&p);
        }
    } else if (is_definitely_function) {
        expected_token_error(&p, "(");
        //goto drop_and_fail;
    } else {
        goto drop_and_fail;
    }

    ret->args_span = span_from(args_start);

    if (parse_type_return_syntax) {
        hop_whitespace(&p);

        if (!hop_start(&p, "=>")){
            if (is_definitely_function) {
                expected_token_error(&p, "=>");
            } else {
                goto drop_and_fail;
            }
        }
    } else {
        Parser type_p = parser(&p);
        hop_whitespace(&type_p);

        if (hop_start(&type_p, ":")) {
            parse_success(&type_p);
        } else {
            parse_fail(&type_p);
            parse_ret_type = false;
        }
    }
    hop_whitespace(&p);

    if (parse_ret_type) {
        Anchor ret_a = anchor(&p);
        if (parse_type_predicate(&p, ret)) {
            ret->ret_span = span_from(ret_a);
            ret->ret_explicit = true;
        } else if (parse_type(&p, TYPE_PRECEDENCE_NULL, &ret->ret)) {
            ret->ret_span = span_from(ret_a);
            ret->ret_explicit = true;
            hop_whitespace(&p);
        } else {
            goto drop_and_fail;
        }
    }

    return parse_success(&p);

drop_and_fail:
    function_impl_drop(ret);
    return parse_fail(&p);
}

// Call before parsing a function's arguments and body, and function_ctx_leave() after (on success and failure)
static FunctionCtx function_ctx_enter(Parser* p, FunctionFlags flags) {
    FunctionCtx saved = {
        .in_async = p->shared->in_async, .in_generator = p->shared->in_generator,
        .in_static_block = p->shared->in_static_block, .in_parameters = p->shared->in_parameters,
    };
    p->shared->in_static_block = false;
    p->shared->in_parameters = false;
    p->shared->in_async = flags.is_async;
    p->shared->in_generator = flags.is_generator;
    p->shared->function_depth++;
    return saved;
}

static void function_ctx_leave(Parser* p, FunctionCtx saved) {
    p->shared->in_async = saved.in_async;
    p->shared->in_generator = saved.in_generator;
    p->shared->in_static_block = saved.in_static_block;
    p->shared->in_parameters = saved.in_parameters;
    p->shared->function_depth--;
}

static bool parse_arrow_function_inner(Parser* parent, ExprCtx ctx, FunctionImpl* ret);

static bool parse_arrow_function(Parser* parent, ExprCtx ctx, FunctionFlags flags, FunctionImpl* ret) {
    FunctionCtx saved = function_ctx_enter(parent, flags);
    bool success = parse_arrow_function_inner(parent, ctx, ret);
    function_ctx_leave(parent, saved);
    if (success) {
        ret->is_async = flags.is_async;
    }
    return success;
}

static bool parse_arrow_function_inner(Parser* parent, ExprCtx ctx, FunctionImpl* ret) {
    Parser p = parser(parent);
    FAIL_CACHE_HOOK(&p, FAIL_CACHE_ARROW_FUNCTION);
    Anchor a = anchor(&p);
    bool is_definitely_function = false;
    Span lone_arg_name;

    // In a JSX file `<T>(x) => x` is an element: a generic arrow function is written `<T,>` or `<T extends X>`
    if (*p.pos == '<' && p.shared->source->is_jsx && !jsx_generic_arrow_start(&p)) {
        return parse_fail(&p);
    }

    if (parse_function_args(&p, false, false, !ctx.is_parsing_ternary, ret)) {

        is_definitely_function =
            !vec_function_arg_is_empty(&ret->args) &&
            vec_function_arg_at(&ret->args, 0)->initializer.binding.has_explicit_type;

        if (ret->ret_explicit) {
            is_definitely_function = true;
        }

    } else if (parse_identifier(&p, &lone_arg_name)) {
        *ret = (FunctionImpl){0};

        ret->args_span = lone_arg_name;
        Binding binding = {
            .tag = BINDING_IDENTIFIER,
            .content_span = lone_arg_name,
        };
        vec_function_arg_push(&ret->args, (FunctionArg){
            .span = lone_arg_name,
            .initializer.binding = binding
        });

    } else {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    bool newline_after_args = p.whitespace == WHITESPACE_NEWLINE;

    Anchor arrow_a = anchor(&p);
    if (!hop_start(&p, "=>")) {
        if (is_definitely_function) {
            expected_token_error(&p, "=>");
        } else {
            function_impl_drop(ret);
            return parse_fail(&p);
        }
    }
    ret->arrow_span = span_from(arrow_a);

    if (newline_after_args) {
        // This will generate an error in the typecheck phase, why it's not a parse error I have no idea.
        ret->newline_before_arrow = true;
    }
    hop_whitespace(&p);

    if (!parse_block(&p, false, &ret->body)) {
        Expr expr;
        if (!expect_expr(&p, ctx, &expr)) {
            p.pos = span_end_ptr(ret->arrow_span);
        }
        ret->body = (Block){0};
        vec_statement_push(&ret->body.statements, (Statement){
            .tag = STATEMENT_RETURN_EXPR,
            .expr = expr,
        });
        ret->expression_body = true;
    }

    ret->has_body = true;
    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_function_header(Parser* parent, FunctionFlags flags, FunctionImpl* ret) {
    ignore flags;
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!parse_function_args(&p, false, false, true, ret)) {
        return parse_fail(&p);
    }

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_non_arrow_function_inner(Parser* parent, FunctionImpl* ret);

static bool parse_non_arrow_function(Parser* parent, FunctionFlags flags, FunctionImpl* ret) {
    FunctionCtx saved = function_ctx_enter(parent, flags);
    bool success = parse_non_arrow_function_inner(parent, ret);
    function_ctx_leave(parent, saved);
    if (success) {
        ret->is_async = flags.is_async;
        ret->is_generator = flags.is_generator;
    }
    return success;
}

static bool parse_non_arrow_function_inner(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!parse_function_args(&p, false, false, true, ret)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (parse_block(&p, false, &ret->body)) {
        ret->has_body = true;
    } else if (*p.pos == ';' || *p.pos == '}' || !*p.pos || p.whitespace == WHITESPACE_NEWLINE) {
        ret->body = (Block){0};
    } else {
        parse_error(&p, span_from_token(&p), 1144, "'{' or ';' expected.");
    }

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_function_keyword_function(Parser* parent, bool is_async, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "function")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    FunctionFlags flags = { .is_async = is_async };
    if (hop_char(&p, '*')) {
        flags.is_generator = true;
        hop_whitespace(&p);
    }

    Span name;
    if (!parse_identifier(&p, &name)) {
        name = span_null;
    }
    hop_whitespace(&p);

    if (!parse_non_arrow_function(&p, flags, ret)) {
        parse_error(&p, span_from_token(&p), TS_CODE_NONE, "Expected function.");
        *ret = (FunctionImpl){0};
    }

    ret->name = name;
    ret->full_span = span_from(a);
    return parse_success(&p);
}

static inline bool parse_name_or_value(Parser* parent, Span* span_ret, cstr* name_ret, bool* name_is_value_ret) {
    Value value;
    Anchor a = anchor(parent);
    if (parse_identifier_or_keyword(parent, span_ret)) {
        *name_ret = cstr_from_span(*span_ret);
        *name_is_value_ret = false;
        return true;
    } else if (parse_literal(parent, &value)) {
        if (value.tag == VALUE_STRING) {
            *name_ret = value.string;
            // Now value shouldn't be dropped
        } else {
            *name_ret = cstr_null;
            cstr_append_value_js(name_ret, &value);
            value_drop(&value);
        }
        *span_ret = span_from(a);
        *name_is_value_ret = true;
        return true;
    } else {
        return false;
    }
}

// Hops a contextual member keyword (`get`, `set`, `async`) if it is followed by a member name on the same line,
// rather than being the member name itself (`{ get: 1 }`, `{ get() {} }`, `{ get }`)
static bool hop_member_keyword(Parser* parent, const char* keyword, bool allow_newline) {
    Parser p = parser(parent);
    if (!hop_start_if_ends_on_word_break(&p, keyword)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (!allow_newline && p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }
    if (is_identifier_char(*p.pos) || *p.pos == '[' || *p.pos == '"' || *p.pos == '\'' || *p.pos == '#' ||
        (*p.pos == '*' && strcmp(keyword, "async") == 0)
    ) {
        return parse_success(&p);
    }
    return parse_fail(&p);
}

static bool parse_object_item(Parser* parent, ObjectExprItem* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (hop_start(&p, "...")) {
        hop_whitespace(&p);
        Expr expr;
        expect_expr(&p, expr_ctx_null, &expr);
        *ret = (ObjectExprItem){
            .tag = OBJECT_ITEM_SPREAD,
            .span = span_from(a),
            .value = expr,
        };
        return parse_success(&p);
    }

    ObjectExprItem item = { .tag = OBJECT_ITEM_PROPERTY };
    bool must_be_method = false;
    FunctionFlags flags = {0};
    if (hop_member_keyword(&p, "async", false)) {
        must_be_method = true;
        flags.is_async = true;
    }
    if (hop_char(&p, '*')) {
        hop_whitespace(&p);
        must_be_method = true;
        flags.is_generator = true;
    }
    if (!must_be_method) {
        if (hop_member_keyword(&p, "get", true)) {
            item.tag = OBJECT_ITEM_GETTER;
            must_be_method = true;
        } else if (hop_member_keyword(&p, "set", true)) {
            item.tag = OBJECT_ITEM_SETTER;
            must_be_method = true;
        }
    }

    bool name_is_value = false;
    bool name_is_identifier = false;
    if (*p.pos == '[') {
        Anchor key_anchor = anchor(&p);
        hop_char(&p, '[');
        hop_whitespace(&p);
        Expr key;
        if (!parse_expr(&p, expr_ctx_null, &key)) {
            return parse_fail(&p);
        }
        hop_whitespace(&p);
        if (!hop_char(&p, ']')) {
            expr_drop(&key);
            return parse_fail(&p);
        }
        item.computed_key = c_new(Expr, key);
        item.name_span = span_from(key_anchor);
    } else {
        const char* name_start = p.pos;
        if (!parse_name_or_value(&p, &item.name_span, &item.name_string, &name_is_value)) {
            return parse_fail(&p);
        }
        item.name_is_value = name_is_value;
        name_is_identifier = !name_is_value;
        if (name_is_identifier) {
            // Keywords are fine as property names, but only real identifiers can be shorthand properties
            Parser probe = parser(&p);
            probe.pos = name_start;
            Span ignored;
            name_is_identifier = parse_identifier(&probe, &ignored);
            parse_fail(&probe);
        }
    }
    hop_whitespace(&p);

    // Tolerated here, the checker doesn't report TS1162 (object member cannot be declared optional) yet
    if ((*p.pos == '?' || *p.pos == '!') && (p.pos[1] == '(' || p.pos[1] == ':' || p.pos[1] == '<')) {
        p.pos++;
        hop_whitespace(&p);
    }

    FunctionImpl func;
    if ((*p.pos == '(' || *p.pos == '<') && parse_non_arrow_function(&p, flags, &func)) {
        if (item.tag == OBJECT_ITEM_PROPERTY) {
            item.tag = OBJECT_ITEM_METHOD;
        }
        func.full_span = span_from(a);
        item.value = expr_function(span_from(a), c_new(FunctionImpl, func));
    } else if (must_be_method) {
        object_expr_item_drop(&item);
        return parse_fail(&p);
    } else if (hop_char(&p, ':')) {
        hop_whitespace(&p);
        expect_expr(&p, expr_ctx_null, &item.value);
        // Not `__proto__: class {}`, which sets the prototype
        if (item.computed_key || !csview_equals(cstr_sv(&item.name_string), "__proto__")) {
            mark_named_evaluation(&item.value, item.computed_key);
        }
        mark_assigned_name(&item.value, item.name_span);
    } else if (name_is_identifier && !item.computed_key && (*p.pos == ',' || *p.pos == '}' || *p.pos == '=')) {
        item.tag = OBJECT_ITEM_SHORTHAND;
        item.value = expr_identifier((IdentifierExpr){ .name = item.name_span, .shorthand = true });
        if (*p.pos == '=' && p.pos[1] != '=' && p.pos[1] != '>') {
            // `{ a = 1 }` is only valid as a destructuring assignment target
            hop_char(&p, '=');
            hop_whitespace(&p);
            Expr default_value;
            if (expect_expr(&p, expr_ctx_null, &default_value)) {
                mark_named_evaluation(&default_value, false);
                item.shorthand_default = c_new(Expr, default_value);
            } else {
                expr_drop(&default_value);
            }
        }
    } else {
        object_expr_item_drop(&item);
        return parse_fail(&p);
    }

    item.span = span_from(a);
    if (p.shared->source->is_js && item.tag != OBJECT_ITEM_SHORTHAND) {
        // The tags of a method, or of a property whose value is a function, are the function's
        VecJsDocTag tags = {0};
        FunctionImpl* function = jsdoc_function_of(&item.value);
        if (function && jsdoc_before(&p, a.start, &tags)) jsdoc_apply_function(&tags, function);
        vec_jsdoc_tag_drop(&tags);
    }
    *ret = item;
    return parse_success(&p);
}

static bool parse_object(Parser* parent, ObjectExpr* ret) {
    Parser p = parser(parent);
    *ret = vec_object_expr_item_init();

    const char* open = p.pos;
    if (!hop_char(&p, '{')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    while (*p.pos && *p.pos != '}') {
        ObjectExprItem item;
        if (parse_object_item(&p, &item)) {
            vec_object_expr_item_push(ret, item);
        } else if (*p.pos == ',' || *p.pos == ';') {
            parse_error(&p, span_from_token(&p), 1136, "Property assignment expected.");
        } else {
            break;
        }
        hop_whitespace(&p);

        ignore( *p.pos == '}' || expect_char(&p, ',') || hop_char(&p, ';') );
        hop_whitespace(&p);
    }

    expect_closing(&p, '}', open);

    return parse_success(&p);
}

// An expression that may be a spread (`...a`), for array literals and call arguments
static bool parse_expr_or_spread(Parser* parent, Expr* ret) {
    if (!(parent->pos[0] == '.' && parent->pos[1] == '.' && parent->pos[2] == '.')) {
        return parse_expr(parent, expr_ctx_null, ret);
    }
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    p.pos += 3;
    hop_whitespace(&p);
    Expr operand;
    if (!expect_expr(&p, expr_ctx_null, &operand)) {
        return parse_fail(&p);
    }
    *ret = expr_spread(span_from(a), c_new(Expr, operand));
    return parse_success(&p);
}

static bool parse_array(Parser* parent, VecExpr* ret) {
    Parser p = parser(parent);
    *ret = vec_expr_init();

    const char* open = p.pos;
    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    while (*p.pos && *p.pos != ']') {
        Expr expr;
        if (parse_expr_or_spread(&p, &expr)) {
            vec_expr_push(ret, expr);
        } else if (*p.pos == ',') {
            vec_expr_push(ret, expr_undefined(span_from_pos(&p)));
        } else {
            break;
        }
        hop_whitespace(&p);

        ignore( *p.pos == ']' || expect_char(&p, ',') );
        hop_whitespace(&p);
    }

    expect_closing(&p, ']', open);

    return parse_success(&p);
}

static bool parse_mapped_interface_member(Parser* parent, InterfaceMember* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    *ret = (InterfaceMember){
        .key.tag = INTERFACE_KEY_MAPPED,
    };

    if (hop_char(&p, '+')) {
        ret->key.mapped.readonly = 1;
        hop_whitespace(&p);
    } else if (hop_char(&p, '-')) {
        ret->key.mapped.readonly = -1;
        hop_whitespace(&p);
    }

    if (hop_start_if_ends_on_word_break(&p, "readonly")) {
        if (ret->key.mapped.readonly == 0) {
            ret->key.mapped.readonly = 1;
        }
        hop_whitespace(&p);
    } else if (ret->key.mapped.readonly != 0) {
        return parse_fail(&p);
    }

    if (!expect_char(&p, '[') && ret->key.mapped.readonly == 0) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!expect_identifier(&p, &ret->key.mapped.type_name) && ret->key.mapped.readonly == 0) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!hop_start_if_ends_on_word_break(&p, "in")) {
        if (ret->key.mapped.readonly == 0) {
            return parse_fail(&p);
        } else {
            expected_token_error(&p, "in");
        }
    }
    hop_whitespace(&p);

    expect_type(&p, TYPE_PRECEDENCE_NULL, &ret->key.mapped.in_type);
    hop_whitespace(&p);

    if (hop_start_if_ends_on_word_break(&p, "as")) {
        hop_whitespace(&p);
        ret->key.mapped.has_as_type = true;

        expect_type(&p, TYPE_PRECEDENCE_NULL, &ret->key.mapped.as_type);
        hop_whitespace(&p);
    }

    expect_char(&p, ']');
    ret->key.span = span_from(a);
    hop_whitespace(&p);

   if (hop_char(&p, '+')) {
        ret->key.mapped.optional = 1;
        ret->key.span = span_from(a);
        hop_whitespace(&p);
    } else if (hop_char(&p, '-')) {
        ret->key.mapped.optional = -1;
        ret->key.span = span_from(a);
        hop_whitespace(&p);
    }

    if (ret->key.mapped.optional == 0) {
        if (hop_char(&p, '?')) {
            ret->key.mapped.optional = 1;
        }
    } else {
        expect_char(&p, '?');
    }
    ret->key.span = span_from(a);
    hop_whitespace(&p);

    expect_char(&p, ':');
    hop_whitespace(&p);

    expect_type(&p, TYPE_PRECEDENCE_NULL, &ret->type);

    return parse_success(&p);
}

static bool parse_index_or_expr_interface_member(
    Parser* parent,
    bool is_type,
    bool is_index,
    FunctionFlags flags,
    InterfaceMember* ret
) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    *ret = (InterfaceMember){
        .key.tag = is_index ? INTERFACE_KEY_INDEX : INTERFACE_KEY_EXPR,
    };

    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    bool show_errors = true;
    if (is_index) {
        bool fail_if_no_colon = true;

        if (!parse_identifier(&p, &ret->key.index.name)) {
            return parse_fail(&p);
        }
        hop_whitespace(&p);
        if (hop_char(&p, '?')) {
            parse_error(
                &p,
                span_from_previous_char(&p),
                1019,
                "An index signature parameter cannot have a question mark."
            );
            hop_whitespace(&p);
            fail_if_no_colon = false;
            show_errors = false;
        }

        if (!hop_char(&p, ':')) {
            if (fail_if_no_colon) {
                return parse_fail(&p);
            }
        }
        hop_whitespace(&p);

        (show_errors ? expect_type : parse_type_or_set_to_default)(&p, TYPE_PRECEDENCE_NULL, &ret->key.index.type);
    } else {
        Expr expr;
        if (!parse_expr(&p, expr_ctx_null, &expr)) {
            return parse_fail(&p);
        }
        ret->key.expr.owned = c_new(Expr, expr);
        ret->key.expr.ref = ret->key.expr.owned;
    }
    hop_whitespace(&p);

    (show_errors ? expect_char : hop_char)(&p, ']');
    ret->key.span = span_from(a);
    hop_whitespace(&p);

    if (hop_char(&p, '?')) {
        ret->optional = true;
        hop_whitespace(&p);
    }

    bool (*parse_function_fn)(Parser* parent, FunctionFlags flags, FunctionImpl* ret) =
        is_type ? parse_function_header : parse_non_arrow_function;

    FunctionImpl func;
    if (parse_function_fn(&p, flags, &func)) {
        ret->type = function_impl_into_type(func);
        ret->is_method = true;
    } else if (hop_char(&p, ':')) {
        hop_whitespace(&p);
        (show_errors ? expect_type : parse_type_or_set_to_default)(&p, TYPE_PRECEDENCE_NULL, &ret->type);
    }

    return parse_success(&p);
}

static bool parse_class_item_inner(Parser* parent, bool is_type, bool is_abstract, ClassItem* ret);

// In a type: `new` directly followed by a parameter or type parameter list
static bool is_construct_signature_start(const char* pos) {
    if (strncmp(pos, "new", 3) != 0) return false;
    pos += 3;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
    return *pos == '(' || *pos == '<';
}

// tsc's isUnambiguouslyIndexSignature: `[x:`, `[x,`, `[x?:`, `[x?,`, `[x?]`
static bool is_index_signature_start(const char* pos) {
    if (*pos++ != '[') return false;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
    if (!is_identifier_start_char(*pos)) return false;
    while (is_identifier_char(*pos)) pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
    if (*pos == ':' || *pos == ',') return true;
    if (*pos != '?') return false;
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
    return *pos == ':' || *pos == ',' || *pos == ']';
}

// The `[k]` of a class member
static bool parse_class_computed_key(Parser* parent, Expr** ret) {
    Parser p = parser(parent);
    if (*p.pos != '[' || is_index_signature_start(p.pos)) return parse_fail(&p);
    p.pos++;
    hop_whitespace(&p);
    Expr key;
    if (!parse_expr(&p, expr_ctx_null, &key)) return parse_fail(&p);
    hop_whitespace(&p);
    if (!hop_char(&p, ']')) {
        expr_drop(&key);
        return parse_fail(&p);
    }
    *ret = c_new(Expr, key);
    return parse_success(&p);
}

// A class member's JSDoc: a property's `@type`, a method's (or a function-valued property's) `@param`s and `@returns`
static void jsdoc_apply_class_item(Parser* p, const char* start, ClassItem* item) {
    VecJsDocTag tags = {0};
    if (!jsdoc_before(p, start, &tags)) return;
    switch (item->tag) {
        case CLASS_ITEM_CONSTRUCTOR: jsdoc_apply_function(&tags, &item->constructor.func); break;
        case CLASS_ITEM_METHOD: jsdoc_apply_function(&tags, &item->method.func); break;
        case CLASS_ITEM_FIELD: {
            if (!item->field.has_explicit_type) {
                JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
                if (tag) {
                    item->field.explicit_type = jsdoc_take_type(tag, &item->field.explicit_type_span);
                    item->field.has_explicit_type = true;
                }
            }
            FunctionImpl* function = jsdoc_function_of(&item->field.value);
            if (function) jsdoc_apply_function(&tags, function);
        }   break;
        default: break;
    }
    vec_jsdoc_tag_drop(&tags);
}

static bool parse_class_item(Parser* parent, bool is_type, bool is_abstract, ClassItem* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    VecExpr decorators = {0};
    if (!is_type) {
        ignore parse_decorators(&p, &decorators);
    }
    if (!parse_class_item_inner(&p, is_type, is_abstract, ret)) {
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }
    ret->decorators = decorators;
    ret->span = span_from(a);
    if (p.shared->source->is_js) jsdoc_apply_class_item(&p, a.start, ret);
    return parse_success(&p);
}

static bool parse_class_item_inner(Parser* parent, bool is_type, bool is_abstract, ClassItem* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    ModifierList modifiers;
    if (!parse_modifier_list(&p, true, &modifiers)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    FunctionFlags function_flags = { .is_async = modifiers.includes[MODIFIER_ASYNC] };
    bool is_getter = false, is_setter = false;
    if (hop_member_keyword(&p, "get", true)) {
        is_getter = true;
    } else if (hop_member_keyword(&p, "set", true)) {
        is_setter = true;
    } else if (!is_type && hop_char(&p, '*')) {
        function_flags.is_generator = true;
        hop_whitespace(&p);
    }

    Span name_span;
    cstr name_string;
    bool name_is_value;
    Expr* computed_key = nullptr;
    Anchor name_anchor = anchor(&p);
    if (!is_type && *p.pos == '#' && is_identifier_start_char(p.pos[1])) {
        // Private name, the `#` is part of the name
        p.pos++;
        Span ignored;
        parse_identifier_or_keyword(&p, &ignored);
        name_span = span_from(name_anchor);
        name_string = cstr_from_span(name_span);
        name_is_value = false;
    } else if (is_type && is_construct_signature_start(p.pos)) {
        // `new (...): T` / `new <T>(...): T` is a construct signature, not a method called `new`
        name_span = span_null;
        name_string = cstr_null;
        name_is_value = false;
    } else if (!is_type && parse_class_computed_key(&p, &computed_key)) {
        // `[k]: T = x`, `[k]() {}`: the name is the key's source text, see ClassItem
        name_span = span_from(name_anchor);
        name_string = cstr_from_span(name_span);
        name_is_value = false;
    } else if (!parse_name_or_value(&p, &name_span, &name_string, &name_is_value)) {
        name_span = span_null;
        name_string = cstr_null;
        name_is_value = false;
    }
    hop_whitespace(&p);

    bool (*parse_function_fn)(Parser* parent, FunctionFlags flags, FunctionImpl* ret) =
        is_type ? parse_function_header : parse_non_arrow_function;

    bool outer_static_block = p.shared->in_static_block;
    p.shared->in_static_block = true;
    bool static_block = !is_type && !name_is_value && span_equals(name_span, "static") &&
        parse_block(&p, false, &ret->static_block);
    p.shared->in_static_block = outer_static_block;
    if (static_block) {
        modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_MODIFIERS_NOT_ALLOWED);
        ret->tag = CLASS_ITEM_STATIC_BLOCK;
    } else if (span_valid(name_span)) {
        if (span_valid(name_span) && cstr_is_empty(&name_string)) {
            name_string = cstr_from_span(name_span);
        }

        bool optional = false;
        if (hop_char(&p, '?')) {
            hop_whitespace(&p);
            optional = true;
        } else if (!is_type && *p.pos == '!' && p.pos[1] != '=') {
            // Definite assignment assertion, only matters once definite assignment is checked
            p.pos++;
            hop_whitespace(&p);
        }

        FunctionImpl func;
        ModifierValidationFlags flags =
            (is_type ? MODIFIER_VALIDATION_FLAG_TYPE_MEMEBER : 0) |
            (is_abstract ? MODIFIER_VALIDATION_FLAG_ABSTRACT : 0);
        if (!is_type && !name_is_value && span_equals(name_span, "constructor")) {
            modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_METHOD | flags);
            name_span = span_from(a);
            if (!parse_function_fn(&p, function_flags_null, &func)) {
                parse_error(&p, span_from_token(&p), TS_CODE_NONE, "constructor expected");
                func = (FunctionImpl){0};
            }
            func.full_span = span_from(a);
            func.is_constructor = true;
            *ret = (ClassItem){
                .tag = CLASS_ITEM_CONSTRUCTOR,
                .constructor = {
                    .name_span = name_span,
                    .func = func,
                }
            };
        } else if (parse_function_fn(&p, function_flags, &func)) {
            modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_METHOD | flags);
            if (is_getter || is_setter) report_misplaced_async(&p, &modifiers);
            *ret = (ClassItem){
                .tag = CLASS_ITEM_METHOD,
                .method = {
                    .is_static = modifiers.includes[MODIFIER_STATIC],
                    .is_abstract = modifiers.includes[MODIFIER_ABSTRACT],
                    .name_string = cstr_move(&name_string),
                    .name_span = name_span,
                    .is_optional = optional,
                    .name_is_value = name_is_value,
                    .computed_key = computed_key,
                    .func = func,
                    .accessor = is_getter ? CLASS_ACCESSOR_GET : (is_setter ? CLASS_ACCESSOR_SET : CLASS_ACCESSOR_NONE),
                }
            };
        } else {
            bool is_definitely_field = false;

            modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_FIELD | flags);
            report_misplaced_async(&p, &modifiers);
            *ret = (ClassItem){
                .tag = CLASS_ITEM_FIELD,
                .field = {
                    .is_readonly = modifiers.includes[MODIFIER_READONLY],
                    .is_declare = modifiers.includes[MODIFIER_DECLARE],
                    .is_abstract = modifiers.includes[MODIFIER_ABSTRACT],
                    .is_auto_accessor = modifiers.includes[MODIFIER_ACCESSOR],
                    .is_static = modifiers.includes[MODIFIER_STATIC],
                    .is_optional = optional,
                    .name_string = cstr_move(&name_string),
                    .name_span = name_span,
                    .name_is_value = name_is_value,
                    .computed_key = computed_key,
                }
            };

            Binding binding;
            if (parse_binding_type_or_set_to_default(&p, &binding)) {
                ret->field.explicit_type = binding.explicit_type;
                ret->field.explicit_type_span = binding.explicit_type_span;
                ret->field.has_explicit_type = binding.has_explicit_type;
                is_definitely_field = true;
                hop_whitespace(&p);
            }

            if (parse_initialization_or_set_to_default(&p, expr_ctx_null, &ret->field.value)) {
                mark_named_evaluation(&ret->field.value, ret->field.computed_key);
                is_definitely_field = true;
                hop_whitespace(&p);
            }

            if (p.whitespace != WHITESPACE_NEWLINE) {
                switch (*p.pos) {
                    case ';': case ',': case '}': case '\0': break;
                    default:
                        if (is_definitely_field) {
                            if (is_type) {
                                expect_char(&p, ';');
                            } else {
                                if (!hop_char(&p, ';')) {
                                    parse_error(&p, span_from_token(&p), 1442, "Expected '=' for property initializer.");
                                }
                            }
                        } else if (span_equals(name_span, "function")) {
                            modifier_list_drop(&modifiers);
                            class_item_drop(ret);
                            return parse_fail(&p);
                        } else if (span_equals(name_span, "let") || span_equals(name_span, "var")) {
                            if (vec_spanned_modifier_size(&modifiers.spanned)) {
                                // If we have modifiers
                                parse_error(&p, name_span, 1440, "Variable declaration not allowed at this  location.");
                            } else {
                                modifier_list_drop(&modifiers);
                                return parse_fail(&p);
                            }
                        } else {
                            parse_error(&p, name_span, 1434, "Unexpected keyword or identifier.");
                        }
                }
            }
        }
        ret->access = modifiers.includes[MODIFIER_PRIVATE] ?
            ACCESS_MODIFIER_PRIVATE :
            (modifiers.includes[MODIFIER_PROTECTED] ?
                ACCESS_MODIFIER_PROTECTED :
                ACCESS_MODIFIER_PUBLIC);
    } else {
        FunctionImpl func;
        if (parse_mapped_interface_member(&p, &ret->interface_memeber) ||
            parse_index_or_expr_interface_member(&p, is_type, true, function_flags, &ret->interface_memeber) ||
            parse_index_or_expr_interface_member(&p, is_type, false, function_flags, &ret->interface_memeber)
        ) {
            modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_TYPE_MEMEBER | MODIFIER_VALIDATION_FLAG_FIELD);
            ret->tag = CLASS_ITEM_INTERFACE_MEMBER;
            ret->interface_memeber.declarations = vec_span_single(span_from(a));
            ret->interface_memeber.readonly = modifiers.includes[MODIFIER_READONLY];
            ret->interface_member_static = modifiers.includes[MODIFIER_STATIC];
            // `readonly [K in T]: X` (the modifier went to the list before the key was known to be mapped)
            InterfaceMember* member = &ret->interface_memeber;
            if (member->key.tag == INTERFACE_KEY_MAPPED && member->readonly && !member->key.mapped.readonly) {
                member->key.mapped.readonly = 1;
            }
        } else if (parse_function_args(&p, true, false, true, &func)) {
            modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_TYPE_MEMEBER | MODIFIER_VALIDATION_FLAG_METHOD);
            ret->tag = CLASS_ITEM_INTERFACE_MEMBER;
            ret->interface_memeber = (InterfaceMember){
                .declarations = vec_span_single(span_from(a)),
                .readonly = modifiers.includes[MODIFIER_READONLY],
                .key.tag = INTERFACE_KEY_CALLABLE,
                .type = function_impl_into_type(func),
            };
        } else {
            modifier_list_drop(&modifiers);
            cstr_drop(&name_string);
            return parse_fail(&p);
        }

        hop_whitespace(&p);
        if (p.whitespace != WHITESPACE_NEWLINE && *p.pos != ';' && *p.pos != ',' && *p.pos != '}') {
            expect_char(&p, ';');
        }
    }
    cstr_drop(&name_string);

    modifier_list_drop(&modifiers);
    ret->span = span_from(a);
    return parse_success(&p);
}

static bool parse_class_decl(Parser* parent, ClassDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    VecExpr decorators = {0};
    ignore parse_decorators(&p, &decorators);

    bool is_abstract = false;
    if (hop_start_if_ends_on_word_break(&p, "abstract")) {
        hop_whitespace(&p);
        is_abstract = true;
    }

    if (!hop_start_if_ends_on_word_break(&p, "class")) {
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    *ret = (ClassDecl){
        .decorators = decorators,
        .is_abstract = is_abstract,
        .extends = expr_implicit_any_no_span,
    };
    // `class extends A {}` and `class implements A {}` are anonymous, but `class implements {}` is called implements
    bool starts_with_implements_clause = false;
    if (starts_with_followed_by_word_break(&p, "implements")) {
        const char* c = p.pos + strlen("implements");
        while (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') {
            c++;
        }
        starts_with_implements_clause = is_identifier_start_char(*c);
    }
    if (starts_with_followed_by_word_break(&p, "extends") ||
        starts_with_implements_clause ||
        !parse_identifier(&p, &ret->name)
    ) {
        ret->name = span_null;
    }
    hop_whitespace(&p);

    if (parse_generic_decl(&p, &ret->generic)) {
        hop_whitespace(&p);
    } else {
        ret->generic = (Generic){0};
    }

    Anchor extends_start = anchor(&p);
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        hop_whitespace(&p);

        ExprCtx ctx = expr_ctx_null;
        ctx.reject_operators = true;
        expect_expr(&p, ctx, &ret->extends);
        ret->extends_clause = span_from(extends_start);
        hop_whitespace(&p);

        if (hop_char(&p, ',')) {
            // TODO consume and give correct error when multiple classes are extended
            parse_error(&p, span_from_previous_char(&p), 1009, "Trailing comma not allowed.");
            hop_whitespace(&p);
        }
    }

    if (hop_start_if_ends_on_word_break(&p, "implements")) {
        hop_whitespace(&p);

        while (true) {
            TypeNode t;
            if (!expect_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
                break;
            }

            vec_type_push(&ret->implements, t);

            hop_whitespace(&p);
            if (!hop_char(&p, ',')) {
                break;
            }
            hop_whitespace(&p);
        }
    }

    if (!expect_char(&p, '{')) {
        ret->full_span = span_from(a);
        return parse_success(&p);
    }
    hop_whitespace(&p);

    ClassItem item;
    while (*p.pos && *p.pos != '}') {
        if (hop_char(&p, ';')) {
            hop_whitespace(&p);
            continue;
        }
        if (!parse_class_item(&p, false, is_abstract, &item)) {
            parse_error(
                &p,
                span_from_token(&p),
                1068,
                "Unexpected token. A constructor, method, accessor, or property was expected."
            );
            ret->full_span = span_from(a);
            return parse_success(&p);
        }
        if (item.tag == CLASS_ITEM_CONSTRUCTOR) {
            if (!generic_empty(&item.constructor.func.generic)) {
                Span generics_span = span_between(
                    item.constructor.func.generic.items.data->full_span,
                    vec_generic_item_back(&item.constructor.func.generic.items)->full_span
                );
                parse_error(&p, generics_span, 1092, "Type parameters cannot appear on a constructor declaration.");
            }
            if (is_abstract) {
                item.constructor.func.is_abstract_constructor = true;
            }
        }
        vec_class_item_push(&ret->items, item);
        hop_whitespace(&p);

        if (!*p.pos || *p.pos == '}') {
            break;
        }

        do {
            hop_whitespace(&p);
        } while (hop_char(&p, ';'));
    }
    expect_char(&p, '}');

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_call_args(Parser* parent, VecExpr* args) {
    Parser p = parser(parent);

    if (!hop_start(&p, "(")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    *args = vec_expr_init();
    while (*p.pos && *p.pos != ')') {
        Expr arg = {0};
        if (parse_expr_or_spread(&p, &arg) || expect_expr(&p, expr_ctx_null, &arg)) {
            vec_expr_push(args, arg);
        } else {
            while (*p.pos && *p.pos != ')' && *p.pos != ',') {
                p.pos++;
            }
        }
        hop_whitespace(&p);

        ignore( *p.pos == ')' || (expect_char(&p, ',') && p.pos--) );

        bool first_comma = true;
        while (*p.pos == ',') {
            p.pos++;
            hop_whitespace(&p);
            if (!first_comma) {
                parse_error(&p, span_from_token(&p), 1135, "Argument expression expected.");
            }
            first_comma = false;
        }
    }
    expect_char(&p, ')');

    return parse_success(&p);
}

static bool parse_right_unary_op(Parser* parent, UnaryOp* op) {
    Parser p = parser(parent);

    if (hop_start(&p, "++")) {
        *op = UNARY_OP_RIGHT_PLUS_PLUS;
    } else if (hop_start(&p, "--")) {
        *op = UNARY_OP_RIGHT_MINUS_MINUS;
    } else if (hop_start(&p, "!")) {
        if (*p.pos == '=') {
            return parse_fail(&p);
        } else {
            *op = UNARY_OP_RIGHT_BANG;
        }
    } else {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_dot_prop(Parser* parent, bool* optional_chain_ret, Expr* ret) {
    Parser p = parser(parent);

    bool optional_chain = hop_char(&p, '?');

    if (!hop_char(&p, '.')) {
        return parse_fail(&p);
    }
    if (optional_chain && *p.pos >= '0' && *p.pos <= '9') {
        return parse_fail(&p); // `a ? .5 : b`
    }
    hop_whitespace(&p);

    Span prop;
    if (*p.pos == '#' && is_identifier_start_char(p.pos[1])) {
        // Private name, the `#` is part of the member's name
        Anchor name_anchor = anchor(&p);
        p.pos++;
        parse_identifier_or_keyword(&p, &prop);
        prop = span_from(name_anchor);
    } else if (!parse_identifier_or_keyword(&p, &prop)) {
        return parse_fail(&p);
    }

    *ret = expr_literal(prop, value_string(cstr_from_span(prop)));
    *optional_chain_ret = optional_chain;
    return parse_success(&p);
}

static bool parse_ternary_expr(Parser* parent, ExprCtx ctx, Expr* ret_yes, Expr* ret_no) {
    Parser p = parser(parent);
    ctx.min_precedence = ternary_precedence - 1;
    ctx.is_parsing_ternary = true;

    if (!hop_char(&p, '?')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!expect_expr(&p, ctx, ret_yes)) {
        *ret_no = expr_undefined(span_from_pos(&p));
        return parse_success(&p);
    }
    hop_whitespace(&p);

    if (!expect_char(&p, ':')) {
        *ret_no = expr_undefined(span_from_pos(&p));
        return parse_success(&p);
    }
    hop_whitespace(&p);

    if (!expect_expr(&p, ctx, ret_no)) {
        return parse_success(&p);
    }

    return parse_success(&p);
}

static bool parse_bracket_prop(Parser* parent, Expr* ret) {
    Parser p = parser(parent);

    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!expect_expr(&p, expr_ctx_null, ret)) {
        return parse_success(&p);
    }
    hop_whitespace(&p);

    if (!expect_char(&p, ']')) {
        return parse_success(&p);
    }

    return parse_success(&p);
}

static bool parse_as_type(Parser* parent, TypeNode* ret, Span* keyword_span_ret, bool* is_satisfies_ret) {
    Parser p = parser(parent);

    // Neither keyword can follow a line break
    hop_whitespace(&p);
    if (p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }
    Anchor a = anchor(&p);
    if (hop_start_if_ends_on_word_break(&p, "as")) {
        *is_satisfies_ret = false;
    } else if (hop_start_if_ends_on_word_break(&p, "satisfies")) {
        *is_satisfies_ret = true;
    } else {
        return parse_fail(&p);
    }
    *keyword_span_ret = span_from(a);
    hop_whitespace(&p);

    expect_type(&p, TYPE_PRECEDENCE_NULL, ret);

    return parse_success(&p);
}

// Longest match first within each first character
static bool parse_binary_op(Parser* parent, BinaryOp* op) {
    Parser p = parser(parent);
    bool matched = true;
    switch (*p.pos) {
        case '>':
            if (hop_start(&p, ">>>=")) *op = BINARY_OP_GREATER_GREATER_GREATER_EQUAL;
            else if (hop_start(&p, ">>>")) *op = BINARY_OP_GREATER_GREATER_GREATER;
            else if (hop_start(&p, ">>=")) *op = BINARY_OP_GREATER_GREATER_EQUAL;
            else if (hop_start(&p, ">>")) *op = BINARY_OP_GREATER_GREATER;
            else if (hop_start(&p, ">=")) *op = BINARY_OP_GREATER_EQUAL;
            else { p.pos++; *op = BINARY_OP_GREATER; }
            break;
        case '<':
            if (hop_start(&p, "<<=")) *op = BINARY_OP_LESS_LESS_EQUAL;
            else if (hop_start(&p, "<<")) *op = BINARY_OP_LESS_LESS;
            else if (hop_start(&p, "<=")) *op = BINARY_OP_LESS_EQUAL;
            else { p.pos++; *op = BINARY_OP_LESS; }
            break;
        case '=':
            if (hop_start(&p, "===")) *op = BINARY_OP_TRIPLE_EQUAL;
            else if (hop_start(&p, "==")) *op = BINARY_OP_DOUBLE_EQUAL;
            else if (hop_start(&p, "=>")) matched = false;
            else { p.pos++; *op = BINARY_OP_EQUAL; }
            break;
        case '!':
            if (hop_start(&p, "!==")) *op = BINARY_OP_TRIPLE_NOT_EQUAL;
            else if (hop_start(&p, "!=")) *op = BINARY_OP_NOT_EQUAL;
            else matched = false;
            break;
        case '&':
            if (hop_start(&p, "&&=")) *op = BINARY_OP_AND_AND_EQUAL;
            else if (hop_start(&p, "&&")) *op = BINARY_OP_AND_AND;
            else if (hop_start(&p, "&=")) *op = BINARY_OP_AND_EQUAL;
            else { p.pos++; *op = BINARY_OP_AND; }
            break;
        case '|':
            if (hop_start(&p, "||=")) *op = BINARY_OP_OR_OR_EQUAL;
            else if (hop_start(&p, "||")) *op = BINARY_OP_OR_OR;
            else if (hop_start(&p, "|=")) *op = BINARY_OP_OR_EQUAL;
            else { p.pos++; *op = BINARY_OP_OR; }
            break;
        case '?':
            if (hop_start(&p, "\?\?=")) *op = BINARY_OP_EH_EH_EQUAL;
            else if (hop_start(&p, "\?\?")) *op = BINARY_OP_EH_EH;
            else matched = false;
            break;
        case '*':
            if (hop_start(&p, "**=")) *op = BINARY_OP_STAR_STAR_EQUAL;
            else if (hop_start(&p, "**")) *op = BINARY_OP_STAR_STAR;
            else if (hop_start(&p, "*=")) *op = BINARY_OP_STAR_EQUAL;
            else { p.pos++; *op = BINARY_OP_STAR; }
            break;
        case '+':
            if (hop_start(&p, "++")) matched = false;
            else if (hop_start(&p, "+=")) *op = BINARY_OP_PLUS_EQUAL;
            else { p.pos++; *op = BINARY_OP_PLUS; }
            break;
        case '-':
            if (hop_start(&p, "--")) matched = false;
            else if (hop_start(&p, "-=")) *op = BINARY_OP_MINUS_EQUAL;
            else { p.pos++; *op = BINARY_OP_MINUS; }
            break;
        case '/':
            if (hop_start(&p, "/=")) *op = BINARY_OP_SLASH_EQUAL;
            else { p.pos++; *op = BINARY_OP_SLASH; }
            break;
        case '%':
            if (hop_start(&p, "%=")) *op = BINARY_OP_MOD_EQUAL;
            else { p.pos++; *op = BINARY_OP_MOD; }
            break;
        case '^':
            if (hop_start(&p, "^=")) *op = BINARY_OP_XOR_EQUAL;
            else { p.pos++; *op = BINARY_OP_XOR; }
            break;
        case 'i':
            if (hop_start_if_ends_on_word_break(&p, "in")) *op = BINARY_OP_IN;
            else if (hop_start_if_ends_on_word_break(&p, "instanceof")) *op = BINARY_OP_INSTANCEOF;
            else matched = false;
            break;
        // ',' is handled in parse_comma_ops_right_side()
        default:
            matched = false;
    }
    return matched ? parse_success(&p) : parse_fail(&p);
}

static bool parse_binary_op_expr(Parser* parent, ExprCtx ctx, Expr* left_and_ret) {
    Parser p = parser(parent);

    BinaryOp op;
    if (!parse_binary_op(&p, &op)) {
        return parse_fail(&p);
    }
    int precedence = binary_op_get_precedence(op);
    if (precedence <= ctx.min_precedence || (ctx.reject_in_operator && op == BINARY_OP_IN)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Expr right;
    // Assignments and `**` are right associative: `a = b ??= c`
    bool right_associative = precedence == binary_op_get_precedence(BINARY_OP_EQUAL) || op == BINARY_OP_STAR_STAR;
    ctx.min_precedence = right_associative ? precedence - 1 : precedence;
    expect_expr(&p, ctx, &right);

    bool logical_or_plain = op == BINARY_OP_EQUAL || op == BINARY_OP_AND_AND_EQUAL || op == BINARY_OP_OR_OR_EQUAL ||
        op == BINARY_OP_EH_EH_EQUAL;
    if (logical_or_plain && left_and_ret->tag == EXPR_IDENTIFIER) mark_named_evaluation(&right, false);
    bool left_parenthesized = left_and_ret->outer_span.sv.size != left_and_ret->inner_span.sv.size;
    if (left_and_ret->tag == EXPR_IDENTIFIER && !left_parenthesized) {
        mark_assigned_name(&right, left_and_ret->identifier.name);
    } else if (left_and_ret->tag == EXPR_PROPERTY && !left_parenthesized) {
        // `a.C = class {}`: C. `a["C"] = class {}`: "C". `a[k] = class {}`: a[k]
        Expr const* prop = &left_and_ret->property->prop;
        bool named = left_and_ret->property->form == PROPERTY_EXPR_DOT || prop->tag == EXPR_LITERAL;
        mark_assigned_name(&right, named ? prop->inner_span : left_and_ret->inner_span);
    }
    if (op == BINARY_OP_EQUAL && p.shared->source->is_js && expr_commonjs_target(left_and_ret, nullptr)) {
        p.shared->saw_commonjs = true;
    }
    expr_mark_binary_operand(left_and_ret);
    expr_mark_binary_operand(&right);
    *left_and_ret = expr_binary_op(span_between(left_and_ret->outer_span, right.outer_span), c_new(BinaryOpExpr, {
        .left = *left_and_ret,
        .op = op,
        .right = right,
    }));
    return parse_success(&p);
}

// `await` is only a keyword in async functions and at the top level of modules. Elsewhere tsc still reads it as an
// operator (and then complains) if an operand obviously follows
static bool await_is_operator(Parser* parent) {
    if (!starts_with_followed_by_word_break(parent, "await")) {
        return false;
    } else if (parent->shared->in_async) {
        return true;
    }
    const char* c = parent->pos + strlen("await");
    while (*c == ' ' || *c == '\t') {
        c++;
    }
    if (is_identifier_start_char(*c) || is_number_start_char(*c) || *c == '"' || *c == '\'' || *c == '`') {
        return true;
    }
    // At the top level an operand can start with anything that can't continue an expression that began with an
    // identifier called await
    return parent->shared->function_depth == 0 && (*c == '(' || *c == '[' || *c == '{' || *c == '!' || *c == '~');
}

static bool parse_left_unary_op(Parser* parent, UnaryOp* op) {
    if (hop_start(parent, "!")) {
        *op = UNARY_OP_LEFT_BANG;
    } else if (hop_start_if_ends_on_word_break(parent, "delete")) {
        *op = UNARY_OP_DELETE;
    } else if (await_is_operator(parent) && hop_start_if_ends_on_word_break(parent, "await")) {
        *op = UNARY_OP_AWAIT;
    } else if (parent->shared->in_generator && hop_start_if_ends_on_word_break(parent, "yield")) {
        *op = UNARY_OP_YIELD;
        const char* after_yield = parent->pos;
        hop_whitespace_full(parent, true, false);
        if (hop_char(parent, '*')) {
            *op = UNARY_OP_YIELD_STAR;
        } else {
            parent->pos = after_yield;
        }
    } else if (hop_start_if_ends_on_word_break(parent, "typeof")) {
        *op = UNARY_OP_TYPEOF;
    } else if (hop_start_if_ends_on_word_break(parent, "void")) {
        *op = UNARY_OP_VOID;
    } else if (hop_start(parent, "~")) {
        *op = UNARY_OP_TILDE;
    } else if (hop_start(parent, "++")) {
        *op = UNARY_OP_LEFT_PLUS_PLUS;
    } else if (hop_start(parent, "--")) {
        *op = UNARY_OP_LEFT_MINUS_MINUS;
    } else if (hop_start(parent, "+")) {
        *op = UNARY_OP_PLUS;
    } else if (hop_start(parent, "-")) {
        *op = UNARY_OP_MINUS;
    } else {
        return false;
    }
    return true;
}

static bool parse_left_unary_op_expr(Parser* parent, ExprCtx ctx, Expr* ret) {
    Parser p = parser(parent);

    Anchor a = anchor(&p);
    UnaryOp op = 0;
    if (!parse_left_unary_op(&p, &op) || unary_op_get_precedence(op) < ctx.min_precedence) {
        return parse_fail(&p);
    }
    Span op_span = span_from(a);
    const char* op_end = p.pos;
    hop_whitespace(&p);

    if (p.shared->in_parameters && (op == UNARY_OP_YIELD || op == UNARY_OP_YIELD_STAR)) {
        parse_error(&p, op_span, 2523, "'yield' expressions cannot be used in a parameter initializer.");
    } else if (p.shared->in_parameters && op == UNARY_OP_AWAIT) {
        parse_error(&p, op_span, 2524, "'await' expressions cannot be used in a parameter initializer.");
    } else if (op == UNARY_OP_AWAIT && !p.shared->in_async && p.shared->function_depth > 0) {
        parse_error(&p, op_span, 1308,
            "'await' expressions are only allowed within async functions and at the top levels of modules.");
    }

    Expr expr;
    ctx.min_precedence = unary_op_get_precedence(op);
    bool bare_yield = false;
    if (op == UNARY_OP_YIELD) {
        // tsc's parseYieldExpression(): an operand if one starts on the same line (isStartOfExpression)
        char c = *p.pos;
        bool starts_expression = is_identifier_start_char(c) || is_number_start_char(c) || (unsigned char)c >= 0x80 ||
            strchr("\"'`([{!~+-/<@#", c) != nullptr;
        bare_yield = !c || !starts_expression || p.whitespace == WHITESPACE_NEWLINE;
    }
    if (bare_yield) {
        p.pos = op_end;
        expr = expr_implicit_any_no_span;
    } else {
        expect_expr(&p, ctx, &expr);
    }

    // tsc's parseUnaryExpressionOrHigher: `-x ** 2` is ambiguous, so an error (still read as `(-x) ** 2`)
    bool simple_unary = op == UNARY_OP_PLUS || op == UNARY_OP_MINUS || op == UNARY_OP_TILDE || op == UNARY_OP_LEFT_BANG ||
        op == UNARY_OP_DELETE || op == UNARY_OP_TYPEOF || op == UNARY_OP_VOID || op == UNARY_OP_AWAIT;
    if (simple_unary) {
        Parser lookahead = parser(&p);
        hop_whitespace(&lookahead);
        bool exponent = lookahead.pos[0] == '*' && lookahead.pos[1] == '*' && lookahead.pos[2] != '=';
        parse_fail(&lookahead);
        if (exponent) {
            csview op_text = csview_trim(zsview_sv(unary_op_get_zv(op)));
            parse_error(&p, span_between(op_span, expr.outer_span), 17006, "An unary expression with the '",
                op_text, "' operator is not allowed in the left-hand side of an exponentiation expression. "
                "Consider enclosing the expression in parentheses.");
        }
    }

    *ret = expr_unary_op(
        op_span,
        c_new(UnaryOpExpr, {
            .op = op,
            .expr = expr,
        })
    );
    return parse_success(&p);
}

static bool parse_number(Parser* parent, double* ret) {
    if (!is_number_start_char(*parent->pos)) {
        return false;
    }
    char const* start = parent->pos;
    const char* end = string_to_double(parent->pos, nullptr, true, true, true, ret);
    if (end) {
        parent->pos = end;
        // tsc's scanNumber: a leading zero before more digits is a legacy octal (TS1121) or a decimal with a leading
        // zero (TS1489)
        if (start[0] == '0' && end - start > 1 && start[1] >= '0' && start[1] <= '9') {
            bool octal = true;
            char const* digits_end = start + 1;
            while (digits_end < end && *digits_end >= '0' && *digits_end <= '9') {
                if (*digits_end >= '8') octal = false;
                digits_end++;
            }
            if (octal) {
                double value = 0;
                for (char const* i = start + 1; i < digits_end; i++) value = value * 8 + (*i - '0');
                *ret = value;
                parent->pos = end = digits_end;
                bool minus = start > cstr_str(&parent->shared->source->contents) && start[-1] == '-';
                char octal_text[64];
                snprintf(octal_text, sizeof(octal_text), "%s0o%llo", minus ? "-" : "", (unsigned long long)value);
                Span span = span_make(parent->shared->source, minus ? start - 1 : start, end);
                parse_error(parent, span, 1121, "Octal literals are not allowed. Use the syntax '", octal_text, "'.");
                return true;
            }
            char const* decimal_end = end;
            while (decimal_end > start && (*(decimal_end - 1) == 'n')) decimal_end--;
            parse_error(parent, span_make(parent->shared->source, start, decimal_end), 1489,
                "Decimals with leading zeros are not allowed.");
        }
        if (*parent->pos == 'e' || *parent->pos == 'E') {
            parent->pos++;
            parse_error(parent, span_from_pos(parent), 1124, "Digit expected.");
        }
        return true;
    } else {
        return false;
    }
}

// A number or, with an `n` suffix, a bigint literal (its value is kept as a double, see Value)
static bool parse_numeric_value(Parser* parent, Value* ret) {
    double number;
    if (!parse_number(parent, &number)) return false;
    if (*parent->pos == 'n' && !is_identifier_char(parent->pos[1])) {
        parent->pos++;
        *ret = (Value){ .tag = VALUE_BIGINT, .bigint = number };
    } else {
        *ret = (Value){ .tag = VALUE_NUMBER, .number = number };
    }
    return true;
}

static bool parse_string(Parser* parent, cstr* ret) {
    Parser p = parser(parent);

    char quote = *p.pos;
    switch (quote) {
        case '"': case '\'': case '`': break;
        default: return parse_fail(&p);
    }
    p.pos++;
    cstr str = cstr_null;
    while (true) {
        if (!*p.pos || *p.pos == quote) {
            break;
        } else if (*p.pos == '\n' && quote != '`') {
            break;
        } else if (*p.pos == '\\' && *(p.pos + 1)) {
            p.pos++;
            if (!parse_string_escape(&p, &str)) {
                parse_error(&p, span_from_pos(&p), 1125, "Hexadecimal digit expected.");
            }
            continue;
        } else if (*p.pos == '\r') {
            // Ignore?
        } else {
            cstr_append_n(&str, p.pos, 1);
        }
        p.pos++;
    }
    *ret = str;
    if (*p.pos == quote) {
        p.pos++;
    } else {
        parse_error(&p, span_from_pos(&p), 1002, "Unterminated string literal.");
    }

    return parse_success(&p);
}

// A regular expression literal. Only called where an expression may start, which is what makes the `/` not a division
// (tsc rescans the slash token in the same situation)
static bool parse_regex(Parser* parent) {
    Parser p = parser(parent);

    if (*p.pos != '/' || p.pos[1] == '/' || p.pos[1] == '*') {
        return parse_fail(&p);
    }
    p.pos++;
    bool in_class = false;
    while (in_class || *p.pos != '/') {
        switch (*p.pos) {
            case '\0': case '\n': case '\r':
                return parse_fail(&p); // TODO: TS1161 Unterminated regular expression literal
            case '\\':
                if (p.pos[1] && p.pos[1] != '\n') {
                    p.pos++;
                }
                break;
            case '[': in_class = true; break;
            case ']': in_class = false; break;
            default: break;
        }
        p.pos++;
    }
    p.pos++;
    while (is_identifier_char(*p.pos)) {
        p.pos++;
    }

    return parse_success(&p);
}

static bool parse_type_compat_literal(Parser* parent, Value* ret) {
    Parser p = parser(parent);

    if (hop_start_if_ends_on_word_break(&p, "undefined")) {
        *ret = value_undefined;
    } else if (hop_start_if_ends_on_word_break(&p, "null")) {
        *ret = value_null;
    } else if (hop_start_if_ends_on_word_break(&p, "true")) {
        *ret = value_boolean(true);
    } else if (hop_start_if_ends_on_word_break(&p, "false")) {
        *ret = value_boolean(false);
    } else if (parse_numeric_value(&p, ret)) {
    } else if (parse_string(&p, &ret->string)) {
        ret->tag = VALUE_STRING;
    } else {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

// A template literal type with substitutions (`a${B}c`): the texts are cooked like a string's, the substitutions are
// types (which can be templates again). Fails if there are none, a plain template is a string literal type
static bool parse_template_type(Parser* parent, TypeNode* ret) {
    Parser p = parser(parent);

    if (*p.pos != '`') {
        return parse_fail(&p);
    }
    p.pos++;

    TemplateType template = { .texts = vec_cstr_init(), .types = vec_type_init(), .type_spans = vec_span_init() };
    cstr text = cstr_null;
    while (*p.pos && *p.pos != '`') {
        if (*p.pos == '\\' && p.pos[1]) {
            p.pos++;
            if (!parse_string_escape(&p, &text)) {
                parse_error(&p, span_from_pos(&p), 1125, "Hexadecimal digit expected.");
            }
        } else if (p.pos[0] == '$' && p.pos[1] == '{') {
            p.pos += 2;
            vec_cstr_push(&template.texts, text);
            text = cstr_null;
            hop_whitespace(&p);
            Anchor type_a = anchor(&p);
            TypeNode type;
            expect_type(&p, TYPE_PRECEDENCE_NULL, &type);
            vec_type_push(&template.types, type);
            vec_span_push(&template.type_spans, span_from(type_a));
            hop_whitespace(&p);
            if (!expect_char(&p, '}')) {
                break;
            }
        } else if (*p.pos == '\r') {
            p.pos++; // CRLF is LF in a template
        } else {
            cstr_append_n(&text, p.pos, 1);
            p.pos++;
        }
    }
    vec_cstr_push(&template.texts, text);

    if (vec_type_is_empty(&template.types)) {
        template_type_drop(&template);
        return parse_fail(&p);
    }

    if (*p.pos == '`') {
        p.pos++;
    } else {
        parse_error(&p, span_from_pos(&p), 1160, "Unterminated template literal.");
    }

    *ret = (TypeNode){ .tag = TYPE_TEMPLATE, .template_ = c_new(TemplateType, template) };
    return parse_success(&p);
}

static bool parse_literal(Parser* parent, Value* ret) {
    return parse_type_compat_literal(parent, ret);
}

static bool parse_expr_right_side(Parser* parent, ExprCtx ctx, Expr* left_and_ret) {
    Parser p = parser(parent);
    Anchor a = span_starting_at(&p, left_and_ret->outer_span);

    hop_whitespace(&p);
    bool hit_newline = p.whitespace == WHITESPACE_NEWLINE;

    union {
        VecExpr args;
        struct {
            bool optional_chain;
            Expr expr_a;
            Expr expr_b;
        };
        TypeNode type;
        UnaryOp unary;
        struct {
            Expr expr;
            BinaryOp op;
        } binary;
        FunctionImpl function;
        VecSpannedType generic_params;
    } parsed;

    // `f?.(x)` and `a?.[i]`. `a?.b` is parse_dot_prop()'s, and `a ? .5 : b` is a ternary
    bool optional_chain = false;
    if (p.pos[0] == '?' && p.pos[1] == '.' && (p.pos[2] == '(' || p.pos[2] == '[')) {
        optional_chain = true;
        p.pos += 2;
    }

    if (!ctx.entity_name && new_keyword_precedence > ctx.min_precedence && parse_call_args(&p, &parsed.args)) {
        *left_and_ret = expr_call(
            span_from(a),
            c_new(CallExpr, {
                .uses_new = false,
                .optional_chain = optional_chain,
                .callable = *left_and_ret,
                .args = parsed.args,
            })
        );
        if (p.shared->source->is_js && !optional_chain && expr_is_require_call(left_and_ret)) {
            // tsc's collectRequireCalls: `require("m")` names a module in a JavaScript file, wherever it is
            p.shared->saw_commonjs = true;
            left_and_ret->call->callable.identifier.require_call = true;
            Expr const* arg = vec_expr_at(&left_and_ret->call->args, 0);
            if (arg->tag == EXPR_LITERAL && arg->literal.tag == VALUE_STRING) {
                ModuleRef* ref = add_module_ref(&p, cstr_clone(arg->literal.string), arg->inner_span);
                ref->syntax = MODULE_REF_REQUIRE;
                left_and_ret->call->callable.identifier.module_ref = ref;
            }
        }
        return parse_success(&p);
    }

    if (!ctx.entity_name && new_keyword_precedence > ctx.min_precedence && *p.pos == '`') {
        Anchor template_a = anchor(&p);
        cstr plain_string = cstr_null;
        if (!parse_template(&p, &parsed.args, nullptr)) {
            parsed.args = vec_expr_init();
            parse_string(&p, &plain_string);
        }
        *left_and_ret = expr_template(
            span_from(a),
            c_new(TemplateExpr, {
                .tag = c_new(Expr, *left_and_ret),
                .template_span = span_from(template_a),
                .substitutions = parsed.args,
                .plain_string = plain_string,
            })
        );
        return parse_success(&p);
    }

    if (parse_dot_prop(&p, &parsed.optional_chain, &parsed.expr_a)) {
        *left_and_ret = expr_property(
            span_from(a),
            c_new(PropertyExpr, {
                .expr = *left_and_ret,
                .prop = parsed.expr_a,
                .form = PROPERTY_EXPR_DOT,
                .optional_chain = parsed.optional_chain,
            })
        );
        if (p.shared->source->is_js && expr_is_module_exports(left_and_ret)) {
            Span module = left_and_ret->property->expr.inner_span;
            Span* first = &p.shared->module_exports_span;
            if (!span_valid(*first) || span_start_ptr(module) < span_start_ptr(*first)) *first = module;
        }
        return parse_success(&p);
    }

    if (!ctx.entity_name && !(ctx.decorator && !optional_chain) && parse_bracket_prop(&p, &parsed.expr_a)) {
        *left_and_ret = expr_property(
            span_from(a),
            c_new(PropertyExpr, {
                .expr = *left_and_ret,
                .prop = parsed.expr_a,
                .form = PROPERTY_EXPR_BRACKET,
                .optional_chain = optional_chain,
            })
        );
        return parse_success(&p);
    }
    if (optional_chain) {
        return parse_fail(&p);
    }

    Span as_type_span;
    bool is_satisfies = false;
    if (as_type_precedence > ctx.min_precedence && parse_as_type(&p, &parsed.type, &as_type_span, &is_satisfies)) {
        *left_and_ret = expr_as_type(
            span_from(a),
            c_new(AsTypeExpr, {
                .expr = *left_and_ret,
                .type = parsed.type,
                .keyword_span = as_type_span,
                .angle_bracket_syntax = false,
                .is_satisfies = is_satisfies,
            })
        );
        return parse_success(&p);
    }

    if (left_and_ret->tag == EXPR_IDENTIFIER && *p.pos == '=' && *(p.pos + 1) == '>') {
        Parser sub_p = parser(&p);
        sub_p.pos = span_start_ptr(left_and_ret->outer_span);

        if (parse_arrow_function(&sub_p, ctx, function_flags_null, &parsed.function)) {
            parse_success(&sub_p);
            expr_drop(left_and_ret);
            *left_and_ret = expr_function(span_from(a), c_new(FunctionImpl, parsed.function));
            return parse_success(&p);
        } else {
            parse_fail(&sub_p);
        }
    }

    Parser generic_p = parser(&p);
    // Never in JavaScript files, to avoid the ambiguity with `<` and `>` (tsc's parseTypeArgumentsInExpression)
    if (!p.shared->source->is_js && parse_generic_params(&generic_p, true, &parsed.generic_params)) {
        bool success = false;
        if (ctx.reject_operators) {
            success = true;
        } else if (*generic_p.pos == '>' || *generic_p.pos == '=') {
            // Fail
        } else if (true) {
            Expr expr;
            Parser next_expr_p = parser(&generic_p);
            hop_whitespace(&next_expr_p);
            ExprCtx next_expr_ctx = ctx;
            next_expr_ctx.min_precedence = imax(ctx.min_precedence, binary_op_get_precedence(BINARY_OP_GREATER));
            if (next_expr_p.whitespace != WHITESPACE_NEWLINE &&
                *next_expr_p.pos != '(' &&
                parse_expr(&next_expr_p, next_expr_ctx, &expr)
            ) {
                expr_drop(&expr);
            } else {
                success = true;
            }
            parse_fail(&next_expr_p);
        }
        if (success) {
            parse_success(&generic_p);
            if (vec_spanned_type_size(&parsed.generic_params)) {
                *left_and_ret = expr_generic_params(
                    span_from(a),
                    c_new(GenericParamsExpr, {
                        .expr = *left_and_ret,
                        .params = parsed.generic_params,
                    })
                );
            }
            return parse_success(&p);
        } else {
            parse_fail(&generic_p);
            vec_spanned_type_drop(&parsed.generic_params);
        }
    } else {
        parse_fail(&generic_p);
    }

    if (!ctx.reject_operators && !hit_newline) {
        if (parse_right_unary_op(&p, &parsed.unary)) {
            *left_and_ret = expr_unary_op(
                span_from(a),
                c_new(UnaryOpExpr, {
                    .op = parsed.unary,
                    .expr = *left_and_ret,
                })
            );
            return parse_success(&p);
        }
    }

    if (!ctx.reject_operators && parse_binary_op_expr(&p, ctx, left_and_ret)) {
        return parse_success(&p);
    }

    if (!ctx.reject_operators && ternary_precedence >= ctx.min_precedence && parse_ternary_expr(&p, ctx, &parsed.expr_a, &parsed.expr_b)) {
        *left_and_ret = expr_ternary(
            span_from(a),
            c_new(TernaryExpr, {
                .condition = *left_and_ret,
                .yes = parsed.expr_a,
                .no = parsed.expr_b,
            })
        );
        return parse_success(&p);
    }

    return parse_fail(&p);
}

static bool parse_comma_ops_right_side(Parser* parent, Expr* left_and_ret) {
    Parser p = parser(parent);
    Anchor a = span_starting_at(&p, left_and_ret->outer_span);
    Expr expr;
    bool parsed_commas = false;

    hop_whitespace(&p);

    while (hop_char(&p, ',')) {
        parsed_commas = true;
        hop_whitespace(&p);

        if (!expect_expr(&p, expr_ctx_null, &expr)) {
            return parse_success(&p);
        }

        expr_mark_binary_operand(left_and_ret);
        expr_mark_binary_operand(&expr);
        *left_and_ret = expr_binary_op(
            span_from(a),
            c_new(BinaryOpExpr, {
                .left = *left_and_ret,
                .op = BINARY_OP_COMMA,
                .right = expr,
            })
        );
    }
    return parsed_commas ? parse_success(&p) : parse_fail(&p);
}

// A template literal with substitutions, and if `texts` isn't null the cooked texts around them (one more than there
// are substitutions). Fails if there are no substitutions, a plain template is a string literal (see parse_string())
static bool parse_template(Parser* parent, VecExpr* ret, VecCStr* texts) {
    Parser p = parser(parent);

    if (*p.pos != '`') {
        return parse_fail(&p);
    }
    p.pos++;

    VecExpr substitutions = vec_expr_init();
    VecCStr cooked = vec_cstr_init();
    cstr text = cstr_null;
    while (*p.pos && *p.pos != '`') {
        if (*p.pos == '\\' && p.pos[1]) {
            p.pos++;
            parse_string_escape(&p, &text); // (An invalid escape is fine in a tagged template, TODO: TS1125 otherwise)
        } else if (*p.pos == '\r') {
            p.pos++;
        } else if (p.pos[0] == '$' && p.pos[1] == '{') {
            vec_cstr_push(&cooked, text);
            text = cstr_null;
            p.pos += 2;
            hop_whitespace(&p);
            Expr expr;
            if (expect_expr(&p, expr_ctx_null, &expr)) {
                parse_comma_ops_right_side(&p, &expr);
                vec_expr_push(&substitutions, expr);
            }
            hop_whitespace(&p);
            if (!expect_char(&p, '}')) {
                break;
            }
        } else {
            cstr_append_n(&text, p.pos, 1);
            p.pos++;
        }
    }
    vec_cstr_push(&cooked, text);

    if (vec_expr_is_empty(&substitutions)) {
        vec_expr_drop(&substitutions);
        vec_cstr_drop(&cooked);
        return parse_fail(&p);
    }
    if (texts) *texts = cooked; else vec_cstr_drop(&cooked);

    if (*p.pos == '`') {
        p.pos++;
    } else {
        parse_error(&p, span_from_pos(&p), 1160, "Unterminated template literal.");
    }

    *ret = substitutions;
    return parse_success(&p);
}

// Zero or more decorators (`@a @b.c(d)`), fails if there are none. Whitespace after them is hopped
static bool parse_decorators(Parser* parent, VecExpr* ret) {
    if (*parent->pos != '@') {
        return false;
    }
    Parser p = parser(parent);
    *ret = vec_expr_init();
    while (hop_char(&p, '@')) {
        hop_whitespace(&p);
        // Member accesses and calls, anything more has to be in parentheses
        ExprCtx ctx = expr_ctx_null;
        ctx.reject_operators = true;
        ctx.decorator = true;
        Expr expr;
        if (expect_expr(&p, ctx, &expr)) {
            vec_expr_push(ret, expr);
        }
        hop_whitespace(&p);
    }
    return parse_success(&p);
}

// `async function () {}`, `async () => {}`, `async x => {}`
static bool parse_async_function_expr(Parser* parent, ExprCtx ctx, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "async")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }

    if (!parse_function_keyword_function(&p, true, ret) &&
        !parse_arrow_function(&p, ctx, (FunctionFlags){ .is_async = true }, ret)
    ) {
        return parse_fail(&p);
    }

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_identifier_expr(Parser* parent, IdentifierExpr* ret) {
    Parser p = parser(parent);

    Span name;
    if (!parse_identifier(&p, &name)) {
        return parse_fail(&p);
    }
    *ret = (IdentifierExpr){ .name = name };

    return parse_success(&p);
}

// JSX (tsc's parseJsxElementOrSelfClosingElementOrFragment and friends). There is no JSX scanner mode: text between
// tags is read straight from the source, where `//` isn't a comment and `'` isn't a string

// Like parse_error(), except that it is dropped when the last error starts at the same position (tsc's
// parseErrorAtPosition only reports the first error at a position; the JSX recovery relies on that)
#define parse_error_once(me, span, ts_code, ...) \
    (last_error_starts_at(me, span) ? nullptr : parse_error(me, span, ts_code, __VA_ARGS__))

static bool last_error_starts_at(Parser* me, Span span) {
    if (!vec_error_size(&my shared->errors)) return false;
    Error const* last = vec_error_back(&my shared->errors);
    return last->span.source == span.source && span_start_ptr(last->span) == span_start_ptr(span);
}

// A unicode escape in an identifier (`\u0061`, `\u{61}`) at c: its length, 0 if there is none
static int identifier_escape_size(const char* c) {
    if (c[0] != '\\' || c[1] != 'u') return 0;
    if (c[2] == '{') {
        int i = 3;
        while (hex_digit_value(c[i]) >= 0) i++;
        return i > 3 && c[i] == '}' ? i + 1 : 0;
    }
    for (int i = 2; i < 6; i++) {
        if (hex_digit_value(c[i]) < 0) return 0;
    }
    return 6;
}

// A JSX identifier: an identifier name (keywords too) that may go on with hyphens, `data-x`, `my-elem` (not after a
// dot: `hyphens`). Unicode escapes are read, and are TS17021
static bool parse_jsx_identifier_hyphens(Parser* parent, bool hyphens, Span* ret) {
    Anchor a = anchor(parent);
    bool escaped = false;
    int escape = identifier_escape_size(parent->pos);
    if (escape) {
        parent->pos += escape;
        escaped = true;
    } else if (!parse_identifier_or_keyword(parent, nullptr)) {
        return false;
    }
    while (true) {
        if ((hyphens && *parent->pos == '-') || is_identifier_char(*parent->pos)) {
            parent->pos++;
        } else if ((escape = identifier_escape_size(parent->pos))) {
            parent->pos += escape;
            escaped = true;
        } else {
            break;
        }
    }
    *ret = span_from(a);
    if (escaped) parse_error(parent, *ret, 17021, "Unicode escape sequence cannot appear here.");
    return true;
}

static bool parse_jsx_identifier(Parser* parent, Span* ret) {
    return parse_jsx_identifier_hyphens(parent, true, ret);
}

// `ns:name` after the first part of a tag or attribute name (at `:`, maybe after whitespace). False if there's no `:`
static bool parse_jsx_namespace_rest(Parser* parent) {
    Parser p = parser(parent);
    hop_whitespace(&p);
    if (!hop_char(&p, ':')) return parse_fail(&p);
    hop_whitespace(&p);
    Span local;
    if (!parse_jsx_identifier(&p, &local)) {
        parse_error_once(&p, span_from_token(&p), 1003, "Identifier expected.");
    }
    return parse_success(&p);
}

// The token at p, where `</` is one token in a JSX file
static Span jsx_span_from_token(Parser* p) {
    if (p->pos[0] == '<' && p->pos[1] == '/') return span_make(p->shared->source, p->pos, p->pos + 2);
    return span_from_token(p);
}

// tsc's parseJsxElementName: an identifier, `this`, a namespaced name `a:b`, or property accesses on the first two
static cstr jsx_name_value(csview name);

static void parse_jsx_tag_name(Parser* p, Expr* ret, bool* namespaced, bool* broken) {
    Anchor a = anchor(p);
    *namespaced = false;
    Span name;
    if (!parse_jsx_identifier(p, &name)) {
        parse_error_once(p, jsx_span_from_token(p), 1003, "Identifier expected.");
        *ret = expr_identifier((IdentifierExpr){ .name = span_from_pos(p) });
        return;
    }
    if (parse_jsx_namespace_rest(p)) {
        *namespaced = true;
        *ret = expr_identifier((IdentifierExpr){ .name = span_from(a) });
        return;
    }
    if (span_equals(name, "this")) {
        *ret = expr_identifier((IdentifierExpr){ .name = name, .keyword = KEYWORD_IDENTIFIER_THIS });
    } else {
        *ret = expr_identifier((IdentifierExpr){ .name = name });
    }
    while (true) {
        Parser dot = parser(p);
        hop_whitespace(&dot);
        if (!hop_char(&dot, '.')) {
            parse_fail(&dot);
            break;
        }
        hop_whitespace(&dot);
        Span prop;
        if (!parse_jsx_identifier_hyphens(&dot, false, &prop)) {
            *broken = true;
            // (a private name is read and dropped, tsc's parseRightSideOfDot, which reports after it)
            if (*dot.pos == '#' && is_identifier_start_char(dot.pos[1])) {
                dot.pos++;
                parse_identifier_or_keyword(&dot, nullptr);
                parse_error_once(&dot, span_from_pos(&dot), 1003, "Identifier expected.");
            } else {
                parse_error_once(&dot, span_from_token(&dot), 1003, "Identifier expected.");
            }
            parse_success(&dot);
            break;
        }
        parse_success(&dot);
        *ret = expr_property(span_from(a), c_new(PropertyExpr, {
            .expr = *ret,
            .prop = expr_literal(prop, value_string(jsx_name_value(prop.sv))),
            .form = PROPERTY_EXPR_DOT,
        }));
    }
}

// A JSX name with its unicode escapes decoded
static cstr jsx_name_value(csview name) {
    cstr ret = cstr_null;
    for (isize i = 0; i < name.size;) {
        int escape = identifier_escape_size(name.buf + i);
        if (!escape) {
            cstr_append_n(&ret, name.buf + i, 1);
            i++;
            continue;
        }
        u32 code = 0;
        for (int j = 2; j < escape; j++) {
            int v = hex_digit_value(name.buf[i + j]);
            if (v >= 0) code = code * 16 + (u32)v;
        }
        append_code_point(&ret, code);
        i += escape;
    }
    return ret;
}

// tsc's tagNamesAreEquivalent
static bool jsx_tag_names_equal(Expr const* a, Expr const* b) {
    if (a->tag != b->tag) return false;
    switch (a->tag) {
        case EXPR_IDENTIFIER: {
            if (a->identifier.keyword != b->identifier.keyword) return false;
            csview x = a->identifier.name.sv, y = b->identifier.name.sv;
            if (!csview_contains(x, "\\") && !csview_contains(y, "\\")) return csview_equals_sv(x, y);
            // (by the names' values: `<\u0061></a>` is fine)
            cstr cx = jsx_name_value(x), cy = jsx_name_value(y);
            bool ret = cstr_eq(&cx, &cy);
            cstr_drop(&cx);
            cstr_drop(&cy);
            return ret;
        }
        case EXPR_PROPERTY:
            return csview_equals_sv((a->property->prop.inner_span).sv, (b->property->prop.inner_span).sv) &&
                jsx_tag_names_equal(&a->property->expr, &b->property->expr);
        default:
            return false;
    }
}

// `{`, the expression (or nothing) and `}` of an attribute value or a child. False if there was no expression
static bool parse_jsx_braced_expr(Parser* p, bool allow_spread, bool* spread, Expr* ret) {
    p->pos++; // `{`
    hop_whitespace(p);
    bool has_expr = false;
    if (*p->pos != '}') {
        if (!allow_spread && !strncmp(p->pos, "...", 3)) {
            // `x={...a}`: no spread in an attribute value, the `...` is left for the attribute list to skip
            parse_error(p, span_make(p->shared->source, p->pos, p->pos + 3), 1109, "Expression expected.");
            parse_error_once(p, span_from_pos(p), 1005, "'}' expected.");
            return false;
        }
        if (allow_spread && hop_start(p, "...")) {
            *spread = true;
            hop_whitespace(p);
        }
        // Only an assignment expression is valid, but tsc parses (and later reports) a comma sequence
        has_expr = expect_expr(p, expr_ctx_null, ret);
        if (has_expr) parse_comma_ops_right_side(p, ret);
        hop_whitespace(p);
    }
    if (!hop_char(p, '}')) {
        parse_error_once(p, span_from_token(p), 1005, "'}' expected.");
    }
    return has_expr;
}

static bool parse_jsx_element(Parser* parent, bool in_expression_context, JsxElement const* enclosing,
    bool must_be_unary, const char* top_invalid_pos, Expr* ret);

// tsc's parseJsxAttributes: until `>` or `/`
static void parse_jsx_attributes(Parser* p, VecJsxAttribute* ret) {
    while (true) {
        hop_whitespace(p);
        char c = *p->pos;
        if (!c || c == '>' || c == '/') break;
        Anchor a = anchor(p);
        JsxAttribute attr = {0};
        if (c == '{') {
            // tsc's parseJsxSpreadAttribute
            p->pos++;
            hop_whitespace(p);
            attr.tag = JSX_ATTRIBUTE_SPREAD;
            if (!hop_start(p, "...")) parse_error_once(p, span_from_token(p), 1005, "'...' expected.");
            hop_whitespace(p);
            if (expect_expr(p, expr_ctx_null, &attr.value)) parse_comma_ops_right_side(p, &attr.value);
            hop_whitespace(p);
            if (!hop_char(p, '}')) parse_error_once(p, span_from_token(p), 1005, "'}' expected.");
            attr.span = span_from(a);
            vec_jsx_attribute_push(ret, attr);
            continue;
        }
        Span name;
        if (!parse_jsx_identifier(p, &name)) {
            // tsc's parseList: skips what can't start an attribute, unless something around could use it
            // (isInSomeParsingContext: anything is a JSX child; elsewhere, roughly, whatever starts a statement ends
            // the tag: `<C<T> />` in a JS file)
            parse_error_once(p, jsx_span_from_token(p), 1003, "Identifier expected.");
            if (p->shared->jsx_children_depth || strchr("<([\"'`!-+~@#", c) || (c >= '0' && c <= '9')) break;
            if (!hop_start(p, "...")) hop_token(p);
            continue;
        }
        parse_jsx_namespace_rest(p);
        attr.name = span_from(a);
        attr.tag = JSX_ATTRIBUTE_NO_VALUE;
        Parser eq = parser(p);
        hop_whitespace(&eq);
        if (!hop_char(&eq, '=')) {
            parse_fail(&eq);
        } else {
            parse_success(&eq);
            hop_whitespace(p);
            Anchor value_a = anchor(p);
            char quote = *p->pos;
            if (quote == '"' || quote == '\'') {
                // A JSX string: no escapes, line breaks allowed
                p->pos++;
                const char* start = p->pos;
                while (*p->pos && *p->pos != quote) p->pos++;
                cstr value = cstr_from_sv(csview_from_range(start, p->pos));
                if (!hop_char(p, quote)) parse_error(p, span_from_pos(p), 1002, "Unterminated string literal.");
                attr.tag = JSX_ATTRIBUTE_STRING;
                attr.value = expr_literal(span_from(value_a), value_string(value));
            } else if (quote == '{') {
                bool spread = false;
                attr.tag = parse_jsx_braced_expr(p, false, &spread, &attr.value) ?
                    JSX_ATTRIBUTE_EXPR : JSX_ATTRIBUTE_EMPTY_EXPR;
                if (attr.tag == JSX_ATTRIBUTE_EMPTY_EXPR) attr.value = expr_implicit_any(span_from(value_a));
            } else if (quote == '<') {
                attr.tag = JSX_ATTRIBUTE_ELEMENT;
                parse_jsx_element(p, true, nullptr, false, nullptr, &attr.value);
            } else {
                parse_error_once(p, span_from_token(p), 1145, "'{' or JSX element expected.");
            }
        }
        attr.span = span_from(a);
        vec_jsx_attribute_push(ret, attr);
    }
}

static bool jsx_is_line_break(char c) {
    return c == '\n' || c == '\r';
}

// tsc's parseJsxChildren: until `</` (left for the caller) or the end of the file
static void parse_jsx_children_inner(Parser* p, JsxElement* el, bool is_fragment);

static void parse_jsx_children(Parser* p, JsxElement* el, bool is_fragment) {
    p->shared->jsx_children_depth++;
    parse_jsx_children_inner(p, el, is_fragment);
    p->shared->jsx_children_depth--;
}

static void parse_jsx_children_inner(Parser* p, JsxElement* el, bool is_fragment) {
    while (true) {
        Anchor a = anchor(p);
        char c = *p->pos;
        if (!c) {
            // At the tag that lacks the closing element rather than at the end of the file
            if (is_fragment) {
                parse_error_once(p, el->opening_span, 17014, "JSX fragment has no corresponding closing tag.");
            } else {
                parse_error_once(p, el->tag_name.outer_span, 17008, "JSX element '", el->tag_name.outer_span,
                    "' has no corresponding closing tag.");
            }
            return;
        }
        if (c == '<') {
            if (p->pos[1] == '/') return;
            JsxChild child = { .tag = JSX_CHILD_ELEMENT };
            parse_jsx_element(p, false, is_fragment ? nullptr : el, false, nullptr, &child.expr);
            child.span = span_from(a);
            vec_jsx_child_push(&el->children, child);
            // `<div>...(<span>...</div>)`: the `</div>` goes to the parent, stop here
            JsxElement const* inner = child.expr.tag == EXPR_JSX_ELEMENT ? child.expr.jsx : nullptr;
            if (!is_fragment && inner && !inner->self_closing && span_valid(inner->closing_span) &&
                !jsx_tag_names_equal(&inner->tag_name, &inner->closing_name) &&
                jsx_tag_names_equal(&el->tag_name, &inner->closing_name)
            ) {
                return;
            }
            continue;
        }
        if (c == '{') {
            JsxChild child = { .tag = JSX_CHILD_EXPR };
            if (!parse_jsx_braced_expr(p, true, &child.spread, &child.expr)) {
                child.tag = JSX_CHILD_EMPTY_EXPR;
                child.expr = expr_implicit_any(span_from(a));
            }
            child.span = span_from(a);
            vec_jsx_child_push(&el->children, child);
            continue;
        }
        // Text (tsc's scanJsxToken). Whitespace only with a line break before anything else is JsxTextAllWhiteSpaces
        int first_non_whitespace = 0;
        while (*p->pos && *p->pos != '{' && *p->pos != '<') {
            char ch = *p->pos;
            if (ch == '>') {
                parse_error(p, span_make(p->shared->source, p->pos, p->pos + 1), 1382,
                    "Unexpected token. Did you mean `{'>'}` or `&gt;`?");
            } else if (ch == '}') {
                parse_error(p, span_make(p->shared->source, p->pos, p->pos + 1), 1381,
                    "Unexpected token. Did you mean `{'}'}` or `&rbrace;`?");
            }
            if (jsx_is_line_break(ch) && first_non_whitespace == 0) {
                first_non_whitespace = -1;
            } else if (!is_js_ascii_space(ch) && !unicode_whitespace_size(p->pos)) {
                first_non_whitespace = 1;
            }
            p->pos++;
        }
        vec_jsx_child_push(&el->children, (JsxChild){
            .tag = JSX_CHILD_TEXT,
            .span = span_from(a),
            .whitespace_only = first_non_whitespace == -1,
        });
    }
}

// tsc's parseJsxElementOrSelfClosingElementOrFragment. `enclosing` is the element whose children this is in, if any.
// Not inlined: parse_expr()'s frame is what deeply nested expressions pay for (binderBinaryExpressionStress)
__attribute__((noinline)) static bool parse_jsx_element(Parser* parent, bool in_expression_context, JsxElement const* enclosing,
    bool must_be_unary, const char* top_invalid_pos, Expr* ret
) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    if (!hop_char(&p, '<')) return parse_fail(&p);
    hop_whitespace(&p);

    JsxElement* el = c_new(JsxElement, {
        .tag_name = expr_implicit_any_no_span,
        .closing_name = expr_implicit_any_no_span,
    });
    bool is_fragment = hop_char(&p, '>');
    Anchor attributes_a = anchor(&p);
    VecJsxAttribute attributes = {0};
    if (!is_fragment) {
        parse_jsx_tag_name(&p, &el->tag_name, &el->namespaced, &el->tag_name_broken);
        hop_whitespace(&p);
        attributes_a = anchor(&p);
        if (!p.shared->source->is_js && *p.pos == '<' && p.pos[1] != '/') {
            // tsc's parseBracketedList(TypeArguments): types separated by commas, anything else ends the list
            Anchor type_args_a = anchor(&p);
            p.pos++;
            el->has_type_args = true;
            el->type_args = vec_spanned_type_init();
            hop_whitespace(&p);
            if (*p.pos == '>') {
                p.pos++;
                el->type_args_empty = span_from(type_args_a);
            } else while (true) {
                hop_whitespace(&p);
                Anchor type_a = anchor(&p);
                TypeNode t;
                if (!parse_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
                    parse_error_once(&p, jsx_span_from_token(&p), 1110, "Type expected.");
                    break;
                }
                vec_spanned_type_push(&el->type_args, spanned_type(span_from(type_a), t));
                hop_whitespace(&p);
                Anchor comma_a = anchor(&p);
                if (!hop_char(&p, ',')) {
                    if (!hop_char(&p, '>')) parse_error_once(&p, jsx_span_from_token(&p), 1005, "'>' expected.");
                    break;
                }
                el->type_args_trailing_comma = span_from(comma_a);
                hop_whitespace(&p);
                if (hop_char(&p, '>')) break;
                el->type_args_trailing_comma = span_null;
            }
            hop_whitespace(&p);
            attributes_a = anchor(&p);
        }
        parse_jsx_attributes(&p, &attributes);
        if (!hop_char(&p, '>')) {
            if (!hop_char(&p, '/')) parse_error_once(&p, span_from_token(&p), 1005, "'/' expected.");
            hop_whitespace(&p);
            if (!hop_char(&p, '>')) parse_error_once(&p, span_from_token(&p), 1005, "'>' expected.");
            el->self_closing = true;
        }
    }
    el->attributes = (Expr){
        .tag = EXPR_JSX_ATTRIBUTES,
        .jsx_attributes = attributes,
        .inner_span = span_from(attributes_a),
        .outer_span = span_from(attributes_a),
    };
    el->opening_span = span_from(a);

    if (!el->self_closing) {
        parse_jsx_children(&p, el, is_fragment);
        JsxChild* last = vec_jsx_child_size(&el->children) ? vec_jsx_child_back_mut(&el->children) : nullptr;
        JsxElement* inner = last && last->expr.tag == EXPR_JSX_ELEMENT ? last->expr.jsx : nullptr;
        if (!is_fragment && inner && !inner->self_closing && span_valid(inner->closing_span) &&
            !jsx_tag_names_equal(&inner->tag_name, &inner->closing_name) &&
            jsx_tag_names_equal(&el->tag_name, &inner->closing_name)
        ) {
            // An unclosed child took our closing tag: `<div>(...<span>...</div>)` is `<div>(...<span>...</>)</div>`
            // (the child reported it)
            el->closing_name = inner->closing_name;
            el->closing_span = inner->closing_span;
            inner->closing_name = expr_identifier((IdentifierExpr){ .name = span_from_pos(&p) });
            inner->closing_span = span_from_pos(&p);
        } else {
            Anchor closing_a = anchor(&p);
            if (!hop_start(&p, "</")) {
                parse_error_once(&p, span_from_pos(&p), 1005, "'</' expected.");
            } else if (is_fragment) {
                hop_whitespace(&p);
                if (!hop_char(&p, '>')) {
                    parse_error_once(&p, span_from_token(&p), 17015, "Expected corresponding closing tag for JSX fragment.");
                }
                el->closing_span = span_from(closing_a);
            } else {
                hop_whitespace(&p);
                bool namespaced, broken = false;
                parse_jsx_tag_name(&p, &el->closing_name, &namespaced, &broken);
                hop_whitespace(&p);
                if (!hop_char(&p, '>')) parse_error_once(&p, span_from_token(&p), 1005, "'>' expected.");
                el->closing_span = span_from(closing_a);
                if (!jsx_tag_names_equal(&el->tag_name, &el->closing_name)) {
                    if (enclosing && jsx_tag_names_equal(&el->closing_name, &enclosing->tag_name)) {
                        // Our parent's closing tag: the error is ours
                        parse_error_once(&p, el->tag_name.outer_span, 17008, "JSX element '", el->tag_name.outer_span,
                            "' has no corresponding closing tag.");
                    } else {
                        parse_error_once(&p, el->closing_name.outer_span, 17002,
                            "Expected corresponding JSX closing tag for '", el->tag_name.outer_span, "'.");
                    }
                }
            }
        }
    }

    p.shared->saw_jsx = true;
    *ret = (Expr){
        .tag = is_fragment ? EXPR_JSX_FRAGMENT : EXPR_JSX_ELEMENT,
        .jsx = el,
        .inner_span = span_from(a),
        .outer_span = span_from(a),
    };

    // `<a></a><b></b>` where an expression is expected: the second element is parsed too, and the two are a comma
    // expression (tsc's recovery, so that the error is about both)
    if (!must_be_unary && in_expression_context) {
        Parser look = parser(&p);
        hop_whitespace(&look);
        bool another = *look.pos == '<';
        parse_fail(&look);
        if (another) {
            const char* top = top_invalid_pos ? top_invalid_pos : a.start;
            hop_whitespace(&p);
            Expr invalid;
            parse_jsx_element(&p, true, nullptr, false, top, &invalid);
            parse_error_once(&p, span_make(p.shared->source, top, span_end_ptr(invalid.outer_span)), 2657,
                "JSX expressions must have one parent element.");
            *ret = expr_binary_op(span_from(a), c_new(BinaryOpExpr, {
                .op = BINARY_OP_COMMA,
                .left = *ret,
                .right = invalid,
                .jsx_siblings = true,
            }));
        }
    }
    return parse_success(&p);
}

// tsc's parseUpdateExpression: in a JSX file, `<` followed by an identifier, keyword or `>` starts an element
__attribute__((noinline)) static bool at_jsx_element_start(Parser* parent) {
    if (!parent->shared->source->is_jsx || *parent->pos != '<') return false;
    Parser p = parser(parent);
    p.pos++;
    hop_whitespace(&p);
    bool ret = *p.pos == '>' || is_identifier_start_char(*p.pos) || identifier_escape_size(p.pos);
    parse_fail(&p);
    return ret;
}

// tsc's isParenthesizedArrowFunctionExpressionWorker in a JSX file: `<T,>` and `<T extends X>` start a generic arrow
// function, anything else after `<` is an element
static bool jsx_generic_arrow_start(Parser* parent) {
    Parser p = parser(parent);
    p.pos++;
    hop_whitespace(&p);
    bool ret = false;
    Span name;
    if (parse_identifier_or_keyword(&p, &name)) {
        hop_whitespace(&p);
        if (span_equals(name, "const") && parse_identifier_or_keyword(&p, &name)) hop_whitespace(&p);
        if (hop_start_if_ends_on_word_break(&p, "extends")) {
            hop_whitespace(&p);
            ret = *p.pos != '=' && *p.pos != '>' && *p.pos != '/';
        } else {
            ret = *p.pos == ',' || *p.pos == '=';
        }
    }
    parse_fail(&p);
    return ret;
}

// `/** @type {T} */ (x)` is a type assertion, `/** @satisfies {T} */ (x)` a satisfies expression (tsc: a parenthesized
// expression with those tags). `span` is the parenthesized expression's
static void jsdoc_apply_parenthesized(Parser* p, const char* start, Span span, Expr* ret) {
    VecJsDocTag tags = {0};
    if (!jsdoc_before(p, start, &tags)) return;
    JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
    bool is_satisfies = !tag;
    if (!tag) tag = jsdoc_find_typed(&tags, JSDOC_TAG_SATISFIES);
    if (tag) {
        // (The parentheses are the assertion's: tsc's ParenthesizedExpression has the asserted type)
        Expr inner = *ret;
        inner.outer_span = inner.inner_span;
        *ret = expr_as_type(span, c_new(AsTypeExpr, {
            .expr = inner,
            .keyword_span = tag->type_span, // (Where tsc reports what's wrong with a JSDoc assertion: its type)
            .type = jsdoc_take_type(tag, nullptr),
            .is_satisfies = is_satisfies,
        }));
    }
    vec_jsdoc_tag_drop(&tags);
}

static bool parse_expr(Parser* parent, ExprCtx ctx, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    FAIL_CACHE_HOOK(&p, FAIL_CACHE_EXPR);

    union {
        FunctionImpl function;
        ClassDecl class_decl;
    } parsed;
    bool inner_span_set = false;

    VecExpr substitutions;
    VecCStr texts;
    if (parse_template(&p, &substitutions, &texts)) {
        *ret = expr_template(span_from(a), c_new(TemplateExpr, {
            .template_span = span_from(a),
            .substitutions = substitutions,
            .texts = texts,
        }));
    } else if (parse_literal(&p, &ret->literal)) {
        ret->tag = EXPR_LITERAL;
    } else if (parse_regex(&p)) {
        *ret = expr_regex(span_from(a));
    } else if (*p.pos == '#' && is_identifier_start_char(p.pos[1])) {
        p.pos++;
        Span ignored;
        parse_identifier_or_keyword(&p, &ignored);
        *ret = expr_private_name(span_from(a), c_new(PrivateNameExpr, {0}));
    } else if (parse_object(&p, &ret->object)) {
        ret->tag = EXPR_OBJECT;
    } else if (parse_array(&p, &ret->array)) {
        ret->tag = EXPR_ARRAY;
    } else if (parse_class_decl(&p, &parsed.class_decl)) {
        parsed.class_decl.is_expression = true;
        *ret = expr_class_decl(span_from(a), c_new(ClassDecl, parsed.class_decl));
    } else if (parse_new_target(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "new")) {
        hop_whitespace(&p);
        ExprCtx sub_ctx = ctx;
        sub_ctx.min_precedence = new_keyword_precedence;
        CallExpr call = { .uses_new = true };
        expect_expr(&p, sub_ctx, &call.callable);
        if (!parse_call_args(&p, &call.args)) {
            call.args = vec_expr_init();
        }
        *ret = expr_call(span_from(a), c_new(CallExpr, call));
    } else if (parse_left_unary_op_expr(&p, ctx, ret)) {
    } else if (parse_function_keyword_function(&p, false, &parsed.function)) {
        *ret = expr_function(span_from(a), c_new(FunctionImpl, parsed.function));
    } else if (parse_async_function_expr(&p, ctx, &parsed.function)) {
        *ret = expr_function(span_from(a), c_new(FunctionImpl, parsed.function));
    } else if (hop_start_if_ends_on_word_break(&p, "this")) {
        *ret = expr_identifier((IdentifierExpr){
            .name = span_from(a),
            .keyword = KEYWORD_IDENTIFIER_THIS,
        });
    } else if (hop_start_if_ends_on_word_break(&p, "super")) {
        *ret = expr_identifier((IdentifierExpr){
            .name = span_from(a),
            .keyword = KEYWORD_IDENTIFIER_SUPER,
        });
    } else if (parse_import_keyword_expr(&p, &ret->identifier)) {
        *ret = expr_identifier(ret->identifier);
    } else if (parse_identifier_expr(&p, &ret->identifier)) {
        *ret = expr_identifier(ret->identifier);
        inner_span_set = true;
    } else if (parse_arrow_function(&p, ctx, function_flags_null, &parsed.function)) {
        *ret = expr_function(span_from(a), c_new(FunctionImpl, parsed.function));
    } else if (hop_char(&p, '(')) {
        hop_whitespace(&p);
        if (!parse_expr(&p, expr_ctx_null, ret)) {
            return parse_fail(&p);
        }
        parse_comma_ops_right_side(&p, ret);
        hop_whitespace(&p);
        expect_char(&p, ')');
        inner_span_set = true;
        if (p.shared->source->is_js) jsdoc_apply_parenthesized(&p, a.start, span_from(a), ret);
    } else if (at_jsx_element_start(&p)) {
        // The operand of a unary operator is a unary expression, which a comma expression isn't (tsc's mustBeUnary)
        bool must_be_unary = ctx.min_precedence >= unary_op_get_precedence(UNARY_OP_LEFT_BANG);
        parse_jsx_element(&p, true, nullptr, must_be_unary, nullptr, ret);
        inner_span_set = true;
    } else if (!p.shared->source->is_jsx && hop_char(&p, '<')) {
        hop_whitespace(&p);

        TypeNode t;
        expect_type(&p, TYPE_PRECEDENCE_NULL, &t);
        hop_whitespace(&p);

        expect_char(&p, '>');
        hop_whitespace(&p);

        // The operand is a unary expression: `<T>a || b` is `(<T>a) || b`
        Expr e;
        ExprCtx operand_ctx = ctx;
        operand_ctx.min_precedence = unary_op_get_precedence(UNARY_OP_LEFT_BANG);
        expect_expr(&p, operand_ctx, &e);
        *ret = expr_as_type(span_from(a), c_new(AsTypeExpr, {
            .expr = e,
            .type = t,
            .angle_bracket_syntax = true,
        }));
    } else {
        return parse_fail(&p);
    }

    if (!inner_span_set) {
        ret->inner_span = span_from(a);
    }
    ret->outer_span = span_from(a);

    ret->lazy_type = (LazyType){0};

    // An arrow function is a whole assignment expression: nothing continues it (`() => {} || a` is missing a `;`)
    bool is_arrow = ret->tag == EXPR_FUNCTION && span_valid(ret->function->arrow_span) && !expr_is_perenthisized(ret);
    while (!is_arrow && parse_expr_right_side(&p, ctx, ret)) {}

    return parse_success(&p);
}

static inline bool expect_expr(Parser* parent, ExprCtx ctx, Expr* ret) {
    if (parse_expr(parent, ctx, ret)) {
        return true;
    } else {
        expected_expression_error(parent);
        *ret = expr_implicit_any(span_from_pos(parent));
        return false;
    }
}

static bool parse_source_directive(Parser* parent, SourceDirectiveStatement* ret) {
    Parser p = parser(parent);
    if (!hop_start(&p, "///")) {
        return parse_fail(&p);
    }
    hop_whitespace_full(&p, false, false);
    if (!hop_char(&p, '<')) {
        return parse_fail(&p);
    }
    hop_whitespace_full(&p, false, false);
    Span xml_tag;
    if (!parse_identifier_or_keyword(&p, &xml_tag)) {
        return parse_fail(&p);
    }
    hop_whitespace_full(&p, false, false);
    bool found_reference_attr = false;
    typeof(ret->tag) reference_attr;
    cstr reference_path = cstr_null;
    Span reference_path_span;
    bool reference_preserve = false;
    while (true) {
        if (hop_start(&p, "/>")) {
            break;
        }

        Span attr;
        if (!parse_identifier_or_keyword(&p, &attr)) {
            cstr_drop(&reference_path);
            return parse_fail(&p);
        }
        hop_whitespace_full(&p, false, false);

        cstr value = cstr_null;
        Anchor value_a = anchor(&p);
        if (hop_char(&p, '=')) {
            hop_whitespace_full(&p, false, false);
            if (*p.pos == '\'' || *p.pos == '"') {
                if (!parse_string(&p, &value)) {
                    value = cstr_null;
                }
            } else {
                Span value_span;
                if (parse_identifier_or_keyword(&p, &value_span)) {
                    value = cstr_from_span(value_span);
                }
            }
        }

        if (span_equals(xml_tag, "reference")) {
            if (!found_reference_attr) {
                if ((span_equals(attr, "path")  && (reference_attr = SOURCE_DIRECTIVE_REFERENCE_PATH,  true)) ||
                    (span_equals(attr, "lib")   && (reference_attr = SOURCE_DIRECTIVE_REFERENCE_LIB,   true)) ||
                    (span_equals(attr, "types") && (reference_attr = SOURCE_DIRECTIVE_REFERENCE_TYPES, true))
                ) {
                    found_reference_attr = true;
                    reference_path = cstr_move(&value);
                    reference_path_span = span_from(value_a);
                }
            }
            if (span_equals(attr, "preserve") && cstr_equals(&value, "true")) {
                reference_preserve = true;
            }
        }

        cstr_drop(&value);
        hop_whitespace_full(&p, false, false);
    }

    if (found_reference_attr) {
        *ret = (SourceDirectiveStatement){
            .tag = reference_attr,
            .path = reference_path,
            .path_span = reference_path_span,
            .preserve = reference_preserve,
        };
        return parse_success(&p);
    } else {
        return parse_fail(&p);
    }
}

static void parse_source_directives(Parser* parent, VecStatement* ret) {
    while (true) {
        hop_whitespace_full(parent, false, true);

        Parser p = parser(parent);
        if (!hop_comment(&p)) {
            parse_fail(&p);
            return;
        }
        const char* comment_end = p.pos;
        parse_fail(&p);

        SourceDirectiveStatement directive;
        if (parse_source_directive(parent, &directive)) {
            vec_statement_push(ret, (Statement){
                .tag = STATEMENT_SOURCE_DIRECTIVE,
                .source_directive = c_new(SourceDirectiveStatement, directive),
            });
        }
        parent->pos = comment_end;
    }
}

// An import/export specifier's name (tsc's parseModuleExportName): any identifier name, or a string literal (kept with
// its quotes, see port_item_name())
static bool parse_module_export_name(Parser* parent, Span* ret) {
    if (parse_identifier_or_keyword(parent, ret)) return true;
    if (*parent->pos != '"' && *parent->pos != '\'') return false;
    Anchor a = anchor(parent);
    cstr ignored = cstr_null;
    bool parsed = parse_string(parent, &ignored);
    cstr_drop(&ignored);
    if (parsed) *ret = span_from(a);
    return parsed;
}

static bool parse_import_item_list(Parser* parent, PortStatement* ret) {
    Parser p = parser(parent);
    ret->items = vec_port_item_init();

    if (!hop_char(&p, '{')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    while (*p.pos && *p.pos != '}') {
        PortItem item = {
            .type_only = ret->main.type_only,
        };
        Anchor a = anchor(&p);

        // `type` is a modifier unless it is the name: `{ type }`, `{ type, a }`, `{ type as x }` and `{ type as as }`
        // rename or take something called type, `{ type as }` and `{ type as as x }` are type-only (tsc's
        // parseImportOrExportSpecifier)
        Parser type_p = parser(&p);
        if (hop_start_if_ends_on_word_break(&type_p, "type")) {
            hop_whitespace(&type_p);
            Parser as_p = parser(&type_p);
            bool type_is_name = *type_p.pos == ',' || *type_p.pos == '}';
            if (hop_start_if_ends_on_word_break(&as_p, "as")) {
                hop_whitespace(&as_p);
                bool ends = *as_p.pos == ',' || *as_p.pos == '}';
                if (hop_start_if_ends_on_word_break(&as_p, "as")) {
                    hop_whitespace(&as_p);
                    type_is_name = *as_p.pos == ',' || *as_p.pos == '}';
                } else {
                    type_is_name = !ends;
                }
            }
            parse_fail(&as_p);
            if (type_is_name) {
                parse_fail(&type_p);
            } else {
                parse_success(&type_p);
                item.type_only = true;
            }
        } else {
            parse_fail(&type_p);
        }

        // Any identifier name will do: `default as a`, `a as default`
        if (parse_module_export_name(&p, &item.source_name) || expect_identifier(&p, &item.source_name)) {
            hop_whitespace(&p);

            if (hop_start_if_ends_on_word_break(&p, "as")) {
                hop_whitespace(&p);
                if (!parse_module_export_name(&p, &item.decl_name)) {
                    expect_identifier(&p, &item.decl_name);
                }
            } else {
                item.decl_name = item.source_name;
            }

            item.full_span = span_from(a);
            vec_port_item_push(&ret->items, item);
        } else {
            hop_token(&p);
        }
        hop_whitespace(&p);

        ignore( *p.pos == '}' || expect_char(&p, ',') );
        hop_whitespace(&p);
    }
    expect_char(&p, '}');

    return parse_success(&p);
}

// Parses what comes after the import keyword in `import a = require("b")` and `import a = b.c`
static bool parse_import_equals(Parser* parent, Anchor statement_start, PortStatement* statement) {
    Parser p = parser(parent);

    bool type_only = false;
    Span name;
    if (!parse_identifier(&p, &name)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (span_equals(name, "type") && *p.pos != '=') {
        type_only = true;
        if (!parse_identifier(&p, &name)) {
            return parse_fail(&p);
        }
        hop_whitespace(&p);
    }
    if (!hop_char(&p, '=') || *p.pos == '=' || *p.pos == '>') {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    statement->main.decl_name = name;
    statement->main.type_only = type_only;

    Parser require_p = parser(&p);
    bool is_require = false;
    if (hop_start_if_ends_on_word_break(&require_p, "require")) {
        hop_whitespace(&require_p);
        if (hop_char(&require_p, '(')) {
            is_require = true;
            hop_whitespace(&require_p);
            Anchor path_anchor = anchor(&require_p);
            Value path;
            if (parse_literal(&require_p, &path)) {
                if (path.tag == VALUE_STRING) {
                    statement->target_path = cstr_move(&path.string);
                    statement->target_path_span = span_from(path_anchor);
                } else {
                    parse_error(&require_p, span_from(path_anchor), 1141, "String literal expected.");
                }
                value_drop(&path);
            } else {
                parse_error(&require_p, span_from_token(&require_p), 1141, "String literal expected.");
            }
            hop_whitespace(&require_p);
            expect_char(&require_p, ')');
        }
    }

    if (is_require) {
        parse_success(&require_p);
        statement->main.tag = PORT_IMPORT_GATHER;
        statement->requires_target = true;
        statement->is_import_require = true;
        statement->main.is_require = true;
    } else {
        parse_fail(&require_p);
        statement->main.tag = PORT_IMPORT_ALIAS;
        statement->main.alias_path = vec_span_init();
        while (true) {
            Span part;
            if (!parse_identifier(&p, &part)) {
                parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
                break;
            }
            vec_span_push(&statement->main.alias_path, part);
            Parser dot = parser(&p);
            hop_whitespace(&dot);
            if (!hop_char(&dot, '.')) {
                parse_fail(&dot);
                break;
            }
            hop_whitespace(&dot);
            parse_success(&dot);
        }
    }

    statement->main.full_span = span_from(statement_start);
    return parse_success(&p);
}

// The `{ type: "json" }` of import attributes, into ret->elements (tsc's parseImportAttributes)
static void parse_import_attributes_body(Parser* parent, ImportAttributes* ret) {
    Parser* p = parent;
    if (!expect_char(p, '{')) return;
    hop_whitespace(p);
    while (*p->pos && *p->pos != '}') {
        ImportAttribute attribute = {0};
        Anchor name_anchor = anchor(p);
        cstr ignored = cstr_null;
        if (*p->pos == '"' || *p->pos == '\'') {
            parse_string(p, &ignored);
            cstr_drop(&ignored);
        } else if (!parse_identifier_or_keyword(p, nullptr)) {
            parse_error(p, span_from_token(p), 1003, "Identifier expected.");
            break;
        }
        attribute.name = span_from(name_anchor);
        hop_whitespace(p);
        expect_char(p, ':');
        hop_whitespace(p);
        expect_expr(p, expr_ctx_null, &attribute.value);
        vec_import_attribute_push(&ret->elements, attribute);
        hop_whitespace(p);
        if (*p->pos == '}' || !expect_char(p, ',')) break;
        hop_whitespace(p);
    }
    expect_char(p, '}');
}

// `with { type: "json" }` / `assert { ... }` after a module specifier, on the same line
static ImportAttributes* parse_import_attributes(Parser* parent) {
    Parser p = parser(parent);
    hop_whitespace(&p);
    Anchor a = anchor(&p);
    bool is_assert = false;
    if (p.whitespace == WHITESPACE_NEWLINE) {
        parse_fail(&p);
        return nullptr;
    } else if (hop_start_if_ends_on_word_break(&p, "assert")) {
        is_assert = true;
    } else if (!hop_start_if_ends_on_word_break(&p, "with")) {
        parse_fail(&p);
        return nullptr;
    }
    ImportAttributes* ret = c_new(ImportAttributes, { .keyword = span_from(a), .is_assert = is_assert });
    ret->elements = vec_import_attribute_init();
    hop_whitespace(&p);
    parse_import_attributes_body(&p, ret);
    ret->span = span_from(a);
    parse_success(&p);
    return ret;
}

static bool parse_import_export_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    Anchor statement_start = a;
    PortStatement statement = {0};

    bool is_export = false;
    if (hop_start_if_ends_on_word_break(&p, "import")) {
    } else if (hop_start_if_ends_on_word_break(&p, "export")) {
        is_export = true;
    } else {
        return parse_fail(&p);
    }
    statement.keyword_span = span_from(a);
    hop_whitespace(&p);

    if (is_export && *p.pos == '=' && p.pos[1] != '=' && p.pos[1] != '>') {
        // `export = x`
        p.pos++;
        hop_whitespace(&p);
        Expr expr;
        expect_expr(&p, expr_ctx_null, &expr);
        mark_named_evaluation(&expr, false);
        *ret = (Statement){
            .tag = STATEMENT_EXPORT_DEFAULT_EXPR,
            .expr = expr,
            .export_equals = true,
        };
        return parse_success(&p);
    }

    if (is_export) {
        // `export import a = b.c`
        Parser import_p = parser(&p);
        if (hop_start_if_ends_on_word_break(&import_p, "import")) {
            hop_whitespace(&import_p);
            if (parse_import_equals(&import_p, a, &statement)) {
                statement.main.export_import = true;
                parse_success(&import_p);
                goto done;
            }
        }
        parse_fail(&import_p);
        if (starts_with_followed_by_word_break(&p, "import")) {
            // `export import x from "m"`: an import declaration with a modifier (tsc's checkGrammarModifiers)
            parse_error(&p, statement.keyword_span, 1191, "An import declaration cannot have modifiers.");
            if (parse_import_export_statement(&p, ret)) return parse_success(&p);
            return parse_fail(&p);
        }
    } else if (parse_import_equals(&p, a, &statement)) {
        goto done;
    } else if (*p.pos == '"' || *p.pos == '\'') {
        // `import "./side-effects"`
        Anchor path_anchor = anchor(&p);
        Value path;
        if (parse_literal(&p, &path)) {
            if (path.tag == VALUE_STRING) {
                statement.main.tag = PORT_NOOP;
                statement.side_effect_only = true;
                statement.requires_target = true;
                statement.target_path = cstr_move(&path.string);
                statement.target_path_span = span_from(path_anchor);
                statement.attributes = parse_import_attributes(&p);
                statement.main.full_span = span_from(a);
                value_drop(&path);
                goto done;
            }
            value_drop(&path);
        }
        return parse_fail(&p);
    }

    a = anchor(&p);
    if (hop_start_if_ends_on_word_break(&p, "type")) {
        statement.main.type_only = true;
        hop_whitespace(&p);
    }

    Anchor star = anchor(&p);
    if (hop_char(&p, '*')) {
        a = star; // (tsc's NamespaceImport/NamespaceExport starts at the `*`, after `type`)
        hop_whitespace(&p);
        statement.main.tag = is_export ? PORT_REEXPORT_GATHER : PORT_IMPORT_GATHER;

        if (is_export) {
            if (hop_start_if_ends_on_word_break(&p, "as")) {
                hop_whitespace(&p);
                // `export * as default from`, `export * as "x" from`
                if (!parse_module_export_name(&p, &statement.main.decl_name)) {
                    expect_identifier(&p, &statement.main.decl_name);
                }
            } else {
                statement.main.tag = PORT_REEXPORT_SPLAT;
            }
        } else {
            if (!hop_start_if_ends_on_word_break(&p, "as")) {
                expected_token_error(&p, "as");
            }
            hop_whitespace(&p);

            expect_identifier(&p, &statement.main.decl_name);
        }
    } else if (is_export && hop_start_if_ends_on_word_break(&p, "as")) {
        hop_whitespace(&p);
        if (!hop_start_if_ends_on_word_break(&p, "namespace")) {
            expected_token_error(&p, "namespace");
        }
        hop_whitespace(&p);
        expect_identifier(&p, &statement.main.decl_name);
        statement.main.tag = PORT_EXPORT_AS_NAMESPACE;
        a = statement_start; // (the statement declares the alias, `export` included)
    } else if (!is_export && parse_identifier(&p, &statement.main.decl_name)) {
        hop_whitespace(&p);
        statement.main.tag = PORT_IMPORT_DEFAULT;

        if (hop_char(&p, ',')) {
            hop_whitespace(&p);

            Anchor gather = anchor(&p);
            if (hop_char(&p, '*')) {
                // `import d, * as ns from "m"`: the namespace import rides along with the (named) items
                hop_whitespace(&p);
                if (!hop_start_if_ends_on_word_break(&p, "as")) expected_token_error(&p, "as");
                hop_whitespace(&p);
                PortItem item = { .tag = PORT_IMPORT_GATHER, .type_only = statement.main.type_only };
                expect_identifier(&p, &item.decl_name);
                item.full_span = span_from(gather);
                statement.items = vec_port_item_init();
                vec_port_item_push(&statement.items, item);
            } else if (!parse_import_item_list(&p, &statement)) {
                statement.items = vec_port_item_init();
                expected_token_error(&p, "{");
            }
        }
    } else if (parse_import_item_list(&p, &statement)) {
        statement.main.tag = PORT_NOOP; // Only the item list is imported, no main import
    } else {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    statement.requires_target = true;
    if (!hop_start_if_ends_on_word_break(&p, "from")) {
        if (is_export) {
            statement.requires_target = false;
        } else {
            expected_token_error(&p, "from");
        }
    }
    hop_whitespace(&p);

    if (statement.requires_target) {
        Expr path_expr;
        // The module specifier is a string literal token (`from "a"\n<div />` is two statements in a JSX file)
        Anchor path_a = anchor(&p);
        bool parsed = (*p.pos == '"' || *p.pos == '\'') && parse_literal(&p, &path_expr.literal);
        if (parsed) path_expr = expr_literal(span_from(path_a), path_expr.literal);
        if (parsed || expect_expr(&p, expr_ctx_null, &path_expr)) {
            if (path_expr.tag == EXPR_LITERAL && path_expr.literal.tag == VALUE_STRING) {
                statement.target_path = cstr_move(&path_expr.literal.string);
                statement.target_path_span = path_expr.inner_span;
            } else {
                parse_error(&p, path_expr.inner_span, 1141, "String literal expected.");
            }
            expr_drop(&path_expr);
            statement.attributes = parse_import_attributes(&p);
        }
    }

    auto item_tag = is_export ? (
        statement.requires_target ? PORT_REEXPORT_SPECIFIC : PORT_EXPORT_SPECIFIC
    ) : PORT_IMPORT_SPECIFIC;
    for_each (i, vec_port_item, &statement.items) {
        if (i->tag != PORT_IMPORT_GATHER) i->tag = item_tag;
    }

    statement.main.full_span = span_from(a);

done:
    if (span_valid(statement.target_path_span)) {
        statement.module_ref = add_module_ref(&p, cstr_clone(statement.target_path), statement.target_path_span);
        if (statement.is_import_require) statement.module_ref->syntax = MODULE_REF_REQUIRE;
        statement.main.module_ref = statement.module_ref;
        if (statement.main.type_only) {
            statement.module_ref->resolution_override = import_attributes_resolution_mode(statement.attributes, nullptr);
        }
        for_each (i, vec_port_item, &statement.items) {
            i->module_ref = statement.module_ref;
        }
    }
    *ret = (Statement){
        .tag = STATEMENT_IMPORT_EXPORT,
        .import_export = c_new(PortStatement, statement),
    };
    return parse_success(&p);
}

// Only parses content, sets type and value to defaults
// An element of an array or object pattern: `x`, `...x`, `x = 1`, `[a, b] = c`
static bool parse_binding_element(Parser* parent, Binding* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    bool rest = hop_start(&p, "...");
    if (rest) {
        hop_whitespace(&p);
    }
    if (!parse_binding_content(&p, ret)) {
        return parse_fail(&p);
    }
    ret->rest = rest;

    Parser default_p = parser(&p);
    hop_whitespace(&default_p);
    if (*default_p.pos == '=' && default_p.pos[1] != '=' && default_p.pos[1] != '>') {
        default_p.pos++;
        hop_whitespace(&default_p);
        Expr default_value;
        expect_expr(&default_p, expr_ctx_null, &default_value);
        if (ret->tag == BINDING_IDENTIFIER && !rest) mark_named_evaluation(&default_value, false);
        if (ret->tag == BINDING_IDENTIFIER) mark_assigned_name(&default_value, ret->content_span);
        ret->default_value = c_new(Expr, default_value);
        parse_success(&default_p);
    } else {
        parse_fail(&default_p);
    }

    ret->element_span = span_from(a);
    return parse_success(&p);
}

static bool parse_binding_content(Parser* parent, Binding* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    Binding binding;

    if (parse_identifier(&p, nullptr)) {
        *ret = (Binding){ .tag = BINDING_IDENTIFIER };
        // Name is accessed via content_span, which is set at the end
    } else if (hop_char(&p, '[')) {
        hop_whitespace(&p);
        ret->tag = BINDING_ARRAY;
        ret->array = vec_binding_init();
        while (*p.pos && *p.pos != ']') {
            binding = (Binding){0};
            if (parse_binding_element(&p, &binding)) {
                vec_binding_push(&ret->array, binding);
            } else if (*p.pos == ',') {
                vec_binding_push(&ret->array, (Binding){
                    .content_span = span_from_pos(&p),
                    .tag = BINDING_NOOP,
                });
            } else {
                break;
            }
            hop_whitespace(&p);

            ignore( *p.pos == ']' || expect_char(&p, ',') );
            hop_whitespace(&p);
        }
        expect_char(&p, ']');
    } else if (hop_char(&p, '{')) {
        hop_whitespace(&p);
        ret->tag = BINDING_OBJECT;
        ret->object = vec_object_binding_item_init();
        while (*p.pos && *p.pos != '}') {
            Anchor key_anchor = anchor(&p);
            Span name;
            Value key;
            Expr* computed_key = nullptr;
            bool shorthand_allowed = false;
            binding = (Binding){0};
            if (*p.pos == '.' && parse_binding_element(&p, &binding)) {
                // `...rest`
                vec_object_binding_item_push(&ret->object, (ObjectBindingItem){
                    .source_span = binding.content_span,
                    .target = binding,
                });
                goto object_item_done;
            } else if (parse_identifier_or_keyword(&p, &name)) {
                key = value_string(cstr_from_span(name));
                shorthand_allowed = true;
            } else if (*p.pos == '[') {
                p.pos++;
                hop_whitespace(&p);
                Expr key_expr;
                expect_expr(&p, expr_ctx_null, &key_expr);
                hop_whitespace(&p);
                expect_char(&p, ']');
                computed_key = c_new(Expr, key_expr);
                key = value_undefined;
                name = span_from(key_anchor);
            } else if (parse_type_compat_literal(&p, &key) && (key.tag == VALUE_STRING || key.tag == VALUE_NUMBER)) {
                name = span_from(key_anchor);
            } else {
                break;
            }
            hop_whitespace(&p);

            if (hop_char(&p, ':')) {
                hop_whitespace(&p);
                if (!parse_binding_element(&p, &binding)) {
                    parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
                    binding = (Binding){ .tag = BINDING_NOOP, .content_span = span_from_pos(&p) };
                }
            } else {
                if (!shorthand_allowed) {
                    expected_token_error(&p, ":");
                }
                binding = (Binding){
                    .tag = shorthand_allowed ? BINDING_IDENTIFIER : BINDING_NOOP,
                    .content_span = name,
                };
                if (*p.pos == '=' && p.pos[1] != '=' && p.pos[1] != '>') {
                    p.pos++;
                    hop_whitespace(&p);
                    Expr default_value;
                    expect_expr(&p, expr_ctx_null, &default_value);
                    binding.default_value = c_new(Expr, default_value);
                }
            }
            vec_object_binding_item_push(&ret->object, (ObjectBindingItem){
                .source = key,
                .source_span = name,
                .computed_key = computed_key,
                .target = binding,
            });
        object_item_done:
            hop_whitespace(&p);

            ignore( *p.pos == '}' || expect_char(&p, ',') );
            hop_whitespace(&p);
        }
        expect_char(&p, '}');
    } else {
        return parse_fail(&p);
    }

    ret->content_span = span_from(a);
    return parse_success(&p);
}

static bool parse_binding_type(Parser* parent, Binding* ret) {
    if (!hop_char(parent, ':')) {
        return false;
    }
    hop_whitespace(parent);

    Anchor a = anchor(parent);

    TypeNode type;
    expect_type(parent, TYPE_PRECEDENCE_NULL, &type);
    ret->has_explicit_type = true;
    ret->explicit_type = type;
    ret->explicit_type_span = span_from(a);

    return true;
}

static bool parse_binding_type_or_set_to_default(Parser* parent, Binding* ret) {
    if (parse_binding_type(parent, ret)) {
        return true;
    } else {
        ret->explicit_type_span = span_null;
        ret->has_explicit_type = false;
        ret->explicit_type = (TypeNode){0};
            return false;
    }
}

static bool parse_initialization(Parser* parent, ExprCtx ctx, Expr* ret) {
    if (!hop_char(parent, '=')) {
        return false;
    }
    hop_whitespace(parent);

    expect_expr(parent, ctx, ret);

    return true;
}

static bool parse_initialization_or_set_to_default(Parser* parent, ExprCtx ctx, Expr* ret) {
    if (parse_initialization(parent, ctx, ret)) {
        return true;
    } else {
        *ret = expr_implicit_any(span_from_pos(parent));
        return false;
    }
}

// ECMAScript's reserved words: never a binding identifier (strict mode ones are, with a grammar error)
static bool is_reserved_word(csview word) {
    static const char* const words[] = {
        "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "enum",
        "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof", "new", "null",
        "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with",
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(*words); i++) {
        if (csview_equals(word, words[i])) return true;
    }
    return false;
}

// After `using`: a declaration if a binding identifier or `{` follows on the same line (tsc's
// nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine; `[` would be an element access)
static bool using_declaration_follows(Parser const* parent, bool disallow_of) {
    Parser p = parser((Parser*)parent);
    hop_whitespace(&p);
    bool ret = false;
    Span word;
    if (p.whitespace == WHITESPACE_NEWLINE) {
        ret = false;
    } else if (*p.pos == '{') {
        ret = true;
    } else if (parse_identifier_or_keyword(&p, &word)) {
        ret = !is_reserved_word(word.sv) && !(disallow_of && span_equals(word, "of"));
    }
    parse_fail(&p);
    return ret;
}

// tsc's isVariableDeclarationInitializedToBareOrAccessedRequire: in a JavaScript file `const x = require("m")`,
// `const { a, b: c } = require("m")` and `const a = require("m").a` declare aliases, like imports
static void note_require_declaration(InitializedBinding* initializer) {
    if (initializer->binding.has_explicit_type) return;
    Expr* value = &initializer->value;
    Span accessed = span_null;
    if (value->tag == EXPR_PROPERTY && !expr_is_perenthisized(value) && value->property->form == PROPERTY_EXPR_DOT &&
        !value->property->optional_chain
    ) {
        accessed = value->property->prop.inner_span;
        value = &value->property->expr;
    }
    if (!expr_is_require_call(value) || expr_is_perenthisized(value)) return;
    ModuleRef* ref = value->call->callable.identifier.module_ref;
    if (!ref) return;
    Binding const* binding = &initializer->binding;
    if (binding->tag == BINDING_OBJECT && span_valid(accessed)) return;
    for_each (i, vec_object_binding_item, binding->tag == BINDING_OBJECT ? &binding->object : &(VecObjectBindingItem){0}) {
        if (i->computed_key || i->target.tag != BINDING_IDENTIFIER || i->target.rest || i->target.default_value ||
            !span_valid(i->source_span)
        ) {
            return;
        }
    }
    if (binding->tag != BINDING_IDENTIFIER && (binding->tag != BINDING_OBJECT || !vec_object_binding_item_size(&binding->object))) return;
    ((Expr*)value)->call->callable.identifier.require_call = 2;
    if (binding->tag == BINDING_IDENTIFIER) {
        initializer->require_ports = c_new(PortItem, {
            .tag = span_valid(accessed) ? PORT_IMPORT_SPECIFIC : PORT_IMPORT_GATHER,
            .decl_name = binding->content_span,
            .full_span = binding->content_span,
            .source_name = accessed,
            .is_require = !span_valid(accessed),
            .module_ref = ref,
        });
        initializer->require_port_count = 1;
        return;
    }
    u32 count = (u32)vec_object_binding_item_size(&binding->object);
    initializer->require_ports = calloc(count, sizeof(PortItem));
    initializer->require_port_count = count;
    for_each (i, vec_object_binding_item, &binding->object) {
        Span name = i->target.content_span;
        initializer->require_ports[i_index] = (PortItem){
            .tag = PORT_IMPORT_SPECIFIC,
            .decl_name = name,
            .full_span = span_start_ptr(i->source_span) < span_start_ptr(name) ? span_between(i->source_span, name) : name,
            .source_name = i->source_span,
            .module_ref = ref,
        };
    }
}

// in_for: the declaration of a `for` loop, where `using of` is `for (using of x)`
static bool parse_variable_statement(Parser* parent, ExprCtx ctx, bool in_for, VariableStatement* ret) {
    Parser p = parser(parent);

    Anchor start = anchor(&p);
    if (hop_start_if_ends_on_word_break(&p, "var")) {
        ret->keyword = VAR_INIT_VAR;
    } else if (hop_start_if_ends_on_word_break(&p, "let")) {
        ret->keyword = VAR_INIT_LET;
    } else if (hop_start_if_ends_on_word_break(&p, "const")) {
        ret->keyword = VAR_INIT_CONST;
    } else if (hop_start_if_ends_on_word_break(&p, "using") && using_declaration_follows(&p, in_for)) {
        ret->keyword = VAR_INIT_USING;
    } else if (hop_start_if_ends_on_word_break(&p, "await")) {
        hop_whitespace(&p);
        // Unlike `for (using of x)`, `for (await using of x)` declares nothing (tsc: TS1123)
        if (!hop_start_if_ends_on_word_break(&p, "using") || !using_declaration_follows(&p, false)) {
            return parse_fail(&p);
        }
        ret->keyword = VAR_INIT_AWAIT_USING;
        ret->await_span = (Span){ .source = p.shared->source, .sv = csview_from_n(start.start, 5) };
        ret->at_top_level = p.shared->function_depth == 0;
        ret->in_static_block = p.shared->in_static_block;
        if (!p.shared->in_async && p.shared->function_depth > 0) {
            parse_error(&p, ret->await_span, 2852,
                "'await using' statements are only allowed within async functions and at the top levels of modules.");
        }
    } else {
        return parse_fail(&p);
    }
    Anchor end_of_keyword = anchor(&p);
    hop_whitespace(&p);

    if (ret->keyword == VAR_INIT_CONST && starts_with_followed_by_word_break(&p, "enum")) {
        // `const enum E {}`
        return parse_fail(&p);
    }

    Span comma_span = span_null;
    InitializedBinding initializer = {0};
    ret->initializers = vec_initialized_binding_init();

    // `for (await using of x)`: `of` is the loop's (tsc's canFollowContextualOfKeyword)
    bool empty_for_of = false;
    if (in_for && ret->keyword == VAR_INIT_AWAIT_USING && starts_with_followed_by_word_break(&p, "of")) {
        Parser ahead = parser(&p);
        hop_start(&ahead, "of");
        hop_whitespace(&ahead);
        empty_for_of = parse_identifier(&ahead, nullptr);
        hop_whitespace(&ahead);
        empty_for_of = empty_for_of && *ahead.pos == ')';
        parse_fail(&ahead);
    }

    while (true) {
        if (empty_for_of || !parse_binding_content(&p, &initializer.binding)) {
            if (span_valid(comma_span)) {
                parse_error(&p, comma_span, 1009, "Trailing comma not allowed.");
                p.pos = span_end_ptr(comma_span);
            } else if (vec_initialized_binding_is_empty(&ret->initializers)) {
                if (ret->keyword == VAR_INIT_LET) {
                    // let can also be a variable name, fail so we then attempt to parse it as such
                    variable_statement_drop(ret);
                    return parse_fail(&p);
                } else {
                    Span s = span_from(end_of_keyword);
                    s.sv.size = 0;
                    parse_error(&p, s, 1123, "Variable declaration list cannot be empty.");
                }
            } else {
                parse_error(&p, span_from_token(&p), 1134, "Variable declaration expected.");
            }
            break;
        }
        const char* end_of_initialization = p.pos;
        hop_whitespace(&p);

        // `let x!: T`. TODO: TS1263/TS1264 when it's misplaced
        if (*p.pos == '!' && p.whitespace != WHITESPACE_NEWLINE) {
            Parser bang_p = parser(&p);
            bang_p.pos++;
            hop_whitespace(&bang_p);
            if (*bang_p.pos == ':') {
                initializer.binding.definitely_assigned = true;
                parse_success(&bang_p);
            } else {
                parse_fail(&bang_p);
            }
        }

        if (parse_binding_type_or_set_to_default(&p, &initializer.binding)) {
            end_of_initialization = p.pos;
            hop_whitespace(&p);
        }

        if (parse_initialization_or_set_to_default(&p, ctx, &initializer.value)) {
            if (initializer.binding.tag == BINDING_IDENTIFIER) {
                mark_named_evaluation(&initializer.value, false);
                mark_assigned_name(&initializer.value, initializer.binding.content_span);
            }
            end_of_initialization = p.pos;
            hop_whitespace(&p);
        }

        if (p.shared->source->is_js) note_require_declaration(&initializer);
        vec_initialized_binding_push(&ret->initializers, initializer);

        if (*p.pos == ';' ||
            *p.pos == ')' ||
            *p.pos == '}' ||
            *p.pos == '\0' ||
            hop_start_if_ends_on_word_break(&p, "in") ||
            hop_start_if_ends_on_word_break(&p, "of")
        ) {
            p.pos = end_of_initialization;
            break;
        }

        if (p.whitespace == WHITESPACE_NEWLINE) {
            if (hop_char(&p, ',')) {
                comma_span = span_from_previous_char(&p);
            } else {
                comma_span = span_null;
                break;
            }
        } else if (expect_char(&p, ',')) {
            comma_span = span_from_previous_char(&p);
        } else {
            comma_span = span_null;
        }
        hop_whitespace(&p);
    }

    ret->span = span_from(start);
    while (ret->span.sv.size && is_js_ascii_space(ret->span.sv.buf[ret->span.sv.size - 1])) ret->span.sv.size--;
    return parse_success(&p);
}

static bool parse_if_arm(Parser* parent, Expr* condition, Statement* statement) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "if")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span open_token;
    if (expect_char(&p, '(')) {
        open_token = span_from_previous_char(&p);
    } else {
        open_token = span_null;
    }
    hop_whitespace(&p);

    if (expect_expr(&p, expr_ctx_null, condition)) {
        parse_comma_ops_right_side(&p, condition);
    }
    hop_whitespace(&p);

    if (!hop_char(&p, ')')) {
        Error* error = expected_token_error(&p, ")");
        error_sub_error_d(error, open_token, 1007, "The parser expected to find a ')' to match the '(' token here.");
    }
    hop_whitespace(&p);

    expect_statement(&p, false, statement);
    return parse_success(&p);
}

static bool parse_if_else(Parser* parent, IfStatement* ret) {
    Parser p = parser(parent);

    VecExpr conditions = {0};
    VecStatement statements = {0};
    while (true) {
        Expr condition = {0};
        Statement statement = {0};
        if (!parse_if_arm(&p, &condition, &statement)) {
            if (vec_expr_is_empty(&conditions)) {
                return parse_fail(&p);
            } else if (expect_statement(&p, false, &statement)) {
                vec_statement_push(&statements, statement);
                break;
            }
        }

        vec_expr_push(&conditions, condition);
        vec_statement_push(&statements, statement);

        // `if (a) b; else c;`: the `;` that ends the arm's statement doesn't end the if (statement terminators are
        // otherwise left to parse_append_statement_sequence())
        Parser else_p = parser(&p);
        hop_whitespace(&else_p);
        if (hop_char(&else_p, ';')) {
            hop_whitespace(&else_p);
        }
        if (!hop_start_if_ends_on_word_break(&else_p, "else")) {
            parse_fail(&else_p);
            break;
        }
        parse_success(&else_p);
        hop_whitespace(&p);
    }

    *ret = (IfStatement){
        .conditions = conditions,
        .statements = statements,
    };
    return parse_success(&p);
}

static bool parse_for_loop_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "for")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    // `for await (x of y)`. TODO: TS1103/TS1431/TS1432 where it isn't allowed
    bool is_await = hop_start_if_ends_on_word_break(&p, "await");
    if (is_await) hop_whitespace(&p);

    expect_char(&p, '(');
    hop_whitespace(&p);

    bool is_for_each = false;
    Statement statement = {0};
    Expr left_expr;
    Expr right_expr;
    Statement body;
    bool keyword_is_in = false;

    if (parse_variable_statement(&p, (ExprCtx){.reject_in_operator = true}, true, &statement.decl.variable)) {
        statement.decl.props.exported = DECL_EXPORTED_NO;
        statement.tag = STATEMENT_DECL_VARIABLE;
    } else if (parse_expr(&p, (ExprCtx){.reject_in_operator = true}, &statement.expr)) {
        parse_comma_ops_right_side(&p, &statement.expr);
        statement.tag = STATEMENT_EXPR;
    } else if (*p.pos == ';') {
        statement = statement_noop;
    } else {
        expected_expression_error(&p);
        statement = statement_noop;
    }
    hop_whitespace(&p);

    if (hop_start(&p, ";")) {
        hop_whitespace(&p);

        if (parse_expr(&p, expr_ctx_null, &left_expr)) {
            parse_comma_ops_right_side(&p, &left_expr);
        } else {
            left_expr = expr_literal(span_from_pos(&p), value_boolean(true));
        }
        hop_whitespace(&p);

        if (expect_char(&p, ';')) {
            hop_whitespace(&p);
            if (parse_expr(&p, expr_ctx_null, &right_expr)) {
                parse_comma_ops_right_side(&p, &right_expr);
            } else {
                right_expr = expr_undefined(span_from_pos(&p));
            }
            hop_whitespace(&p);
        } else {
            right_expr = expr_undefined(span_from_pos(&p));
            hop_token(&p);
        }

    } else if ((statement.tag == STATEMENT_EXPR || statement.tag == STATEMENT_DECL_VARIABLE) && (
        hop_start_if_ends_on_word_break(&p, "of") ||
        (hop_start_if_ends_on_word_break(&p, "in") && (keyword_is_in = true)) // control flow is my passion
    )) {
        is_for_each = true;

        hop_whitespace(&p);

        if (expect_expr(&p, expr_ctx_null, &right_expr)) {
            parse_comma_ops_right_side(&p, &right_expr);
        }
        hop_whitespace(&p);
    } else {
        // Unclear why "',' expected" is the error message in this case
        expected_token_error(&p, ",");
        left_expr = expr_undefined(span_from_pos(&p));
        right_expr = expr_undefined(span_from_pos(&p));
    }
    hop_whitespace(&p);

    if (expect_char(&p, ')')) {
        hop_whitespace(&p);
        expect_statement(&p, false, &body);
    } else {
        body = statement_noop;
    }

    if (is_for_each) {
        *ret = (Statement){
            .tag = STATEMENT_FOR_EACH_LOOP,
            .for_each_loop = c_new(ForEachLoopStatement, {
                .keyword = keyword_is_in ? FOR_EACH_LOOP_IN : FOR_EACH_LOOP_OF,
                .is_await = is_await && !keyword_is_in,
                .iterable = right_expr,
                .body = body,
            })
        };

        switch (statement.tag) {
            case STATEMENT_DECL_VARIABLE:
                ret->for_each_loop->variable.tag = FOR_EACH_LOOP_STATEMENT;
                ret->for_each_loop->variable.statement = statement.decl.variable;
                break;

            case STATEMENT_EXPR:
                ret->for_each_loop->variable.tag = FOR_EACH_LOOP_EXPR;
                ret->for_each_loop->variable.expr = statement.expr;
                break;

            default: panic("Invalid statement type for for-each loop");
        }
    } else {
        *ret = (Statement){
            .tag = STATEMENT_FOR_LOOP,
            .for_loop = c_new(ForLoopStatement, {
                .initialization = statement,
                .condition = left_expr,
                .afterthought = right_expr,
                .body = body,
            }),
        };
    }

    return parse_success(&p);
}

static bool parse_while_loop_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "while")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    const char* open = p.pos;
    bool opened = expect_char(&p, '(');
    hop_whitespace(&p);

    Expr condition = expr_literal(span_from_pos(&p), value_boolean(false));
    Statement body = statement_noop;
    if (expect_expr(&p, expr_ctx_null, &condition)) {
        parse_comma_ops_right_side(&p, &condition);
    }
    hop_whitespace(&p);

    if (opened) expect_closing(&p, ')', open);
    else expect_char(&p, ')');
    hop_whitespace(&p);

    expect_statement(&p, false, &body);

    *ret = (Statement){
        .tag = STATEMENT_FOR_LOOP,
        .for_loop = c_new(ForLoopStatement, {
            .initialization = statement_noop,
            .condition = condition,
            .afterthought = expr_undefined(span_from_pos(&p)),
            .body = body,
        }),
    };
    return parse_success(&p);
}

static bool parse_do_while_loop_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "do")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Statement body = statement_noop;
    expect_statement(&p, false, &body);
    hop_whitespace(&p);
    ignore(hop_char(&p, ';'));
    hop_whitespace(&p);

    Expr condition = expr_literal(span_from_pos(&p), value_boolean(false));
    if (hop_start_if_ends_on_word_break(&p, "while")) {
        hop_whitespace(&p);
        const char* open = p.pos;
        bool opened = expect_char(&p, '(');
        hop_whitespace(&p);
        Expr parsed;
        if (expect_expr(&p, expr_ctx_null, &parsed)) {
            expr_drop(&condition);
            condition = parsed;
            parse_comma_ops_right_side(&p, &condition);
        }
        hop_whitespace(&p);
        if (opened) expect_closing(&p, ')', open);
        else expect_char(&p, ')');
    } else {
        expected_token_error(&p, "while");
    }

    *ret = (Statement){
        .tag = STATEMENT_FOR_LOOP,
        .for_loop = c_new(ForLoopStatement, {
            .initialization = statement_noop,
            .condition = condition,
            .afterthought = expr_undefined(span_from_pos(&p)),
            .body = body,
            .is_do_while = true,
        }),
    };
    return parse_success(&p);
}

// `label:`, the statement that follows is parsed by the caller
static bool parse_label(Parser* parent, Span* ret) {
    Parser p = parser(parent);
    Span label;
    if (!parse_identifier(&p, &label)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (!hop_char(&p, ':')) {
        return parse_fail(&p);
    }
    *ret = label;
    return parse_success(&p);
}

// The optional label after break and continue
static void parse_jump_target(Parser* parent, Span* ret) {
    Parser p = parser(parent);
    hop_whitespace_full(&p, true, false);
    Span label;
    if (parse_identifier(&p, &label)) {
        *ret = label;
        parse_success(&p);
    } else {
        parse_fail(&p);
    }
}

static bool parse_append_statement_sequence(Parser* parent, bool toplevel, VecStatement* ret) {
    Parser p = parser(parent);

    while(
        *p.pos &&
        *p.pos != '}' &&
        !starts_with_followed_by_word_break(&p, "default") &&
        !starts_with_followed_by_word_break(&p, "case")
    ) {
        Statement statement = {0};
        if (*p.pos == ';') {
            p.pos++;
        } else if (parse_statement(&p, toplevel, &statement)) {
            vec_statement_push(ret, statement);
            bool ended_on_brace = p.pos > source_file_start(p.shared->source) && *(p.pos - 1) == '}';
            hop_whitespace(&p);
            if (p.whitespace == WHITESPACE_NEWLINE ||
                statement.tag == STATEMENT_NOOP ||
                !*p.pos ||
                ended_on_brace ||
                *p.pos == '}'
            ) {
            } else if (hop_char(&p, ';')) {
                Statement* pushed = vec_statement_back_mut(ret);
                pushed->span = span_make(p.shared->source, span_start_ptr(pushed->span), p.pos);
            } else if (*p.pos) {
                if (statement.tag == STATEMENT_EXPR &&
                    statement.expr.tag == EXPR_IDENTIFIER &&
                    !expr_is_perenthisized(&statement.expr)
                ) {
                    parse_error(&p, statement.expr.inner_span, 1434, "Unexpected keyword or identifier.");
                } else {
                    expected_token_error(&p, ";");
                }
            }
        } else if (*p.pos == '\\') {
            parse_error(&p, span_from_pos(&p), 1127, "Invalid character.");
            p.pos++;
        } else {
            parse_error(&p, hop_token_get_span(&p), 1128, "Declaration or statement expected.");
        }
        hop_whitespace(&p); // TODO: is this needed?
    }

    return parse_success(&p);
}

static bool parse_switch_case(Parser* parent, SwitchCase* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (hop_start_if_ends_on_word_break(&p, "case")) {
        hop_whitespace(&p);

        *ret = (SwitchCase){
            .is_pattern = true,
            .pattern = {0},
            .statements = {0},
        };

        if (expect_expr(&p, expr_ctx_null, &ret->pattern)) {
            parse_comma_ops_right_side(&p, &ret->pattern);
        }
        hop_whitespace(&p);

        expect_char(&p, ':');
    } else if (hop_start_if_ends_on_word_break(&p, "default")) {
        hop_whitespace(&p);

        expect_char(&p, ':');

        *ret = (SwitchCase){
            .is_pattern = false,
            .default_span = span_from(a),
            .statements = {0},
        };
    } else {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_append_statement_sequence(&p, false, &ret->statements)) {
        switch_case_drop(ret);
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_switch_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);
    SwitchStatement switch_ = {0};

    if (!hop_start_if_ends_on_word_break(&p, "switch")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    expect_char(&p, '(');
    hop_whitespace(&p);

    if (expect_expr(&p, expr_ctx_null, &switch_.needle)) {
        parse_comma_ops_right_side(&p, &switch_.needle);
    }
    hop_whitespace(&p);

    expect_char(&p, ')');
    hop_whitespace(&p);

    expect_char(&p, '{');
    hop_whitespace(&p);

    while(*p.pos && *p.pos != '}') {
        SwitchCase case_;
        if (parse_switch_case(&p, &case_)) {
            vec_switch_case_push(&switch_.cases, case_);
        } else {
            break;
        }
        hop_whitespace(&p);
    }

    if (!hop_char(&p, '}')) {
        parse_error(&p, span_from_token(&p), 1130, "'case' or 'default' expected.");
    }

    *ret = (Statement){
        .tag = STATEMENT_SWITCH,
        .switch_ = c_new(SwitchStatement, switch_),
    };
    return parse_success(&p);
}

static bool parse_try_catch_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "try")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    TryCatchStatement try_catch = {0};
    bool has_catch = false;
    bool has_finally = false;

    if (parse_block(&p, false, &try_catch.try_block)) {
        hop_whitespace(&p);

        if (hop_start_if_ends_on_word_break(&p, "catch")) {
            has_catch = true;
            hop_whitespace(&p);
            if (hop_char(&p, '(')) {
                hop_whitespace(&p);

                if (!parse_binding_content(&p, &try_catch.catch_var)) {
                    parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
                    try_catch.catch_var = (Binding){0};
                }
                hop_whitespace(&p);
                if (parse_binding_type(&p, &try_catch.catch_var)) hop_whitespace(&p);

                expect_char(&p, ')');
                hop_whitespace(&p);
            }
            if (!parse_block(&p, false, &try_catch.catch_block)) {
                expected_token_error(&p, "{");
                try_catch.catch_block = (Block){0};
            }
            hop_whitespace(&p);
        } else {
            try_catch.catch_block = (Block){0};
        }

        if (hop_start_if_ends_on_word_break(&p, "finally")) {
            has_finally = true;
            hop_whitespace(&p);
            if (!parse_block(&p, false, &try_catch.finally_block)) {
                expected_token_error(&p, "{");
                try_catch.finally_block = (Block){0};
            }
        } else {
            try_catch.finally_block = (Block){0};
        }

        if (!has_catch && !has_finally) {
            parse_error(&p, span_from_token(&p), 1472, "'catch' or 'finally' expected.");
        }
    } else {
        expected_token_error(&p, "{");
        try_catch.try_block = (Block){0};
    }

    try_catch.has_catch = has_catch;
    try_catch.has_finally = has_finally;
    *ret = (Statement){
        .tag = STATEMENT_TRY_CATCH,
        .try_catch = c_new(TryCatchStatement, try_catch),
    };
    return parse_success(&p);
}

static bool parse_return(Parser* parent, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "return")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (parse_expr(&p, expr_ctx_null, ret)) {
        parse_comma_ops_right_side(&p, ret);
    } else {
        *ret = expr_undefined(span_from(a));
    }

    return parse_success(&p);
}

static bool parse_throw(Parser* parent, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "throw")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (parse_expr(&p, expr_ctx_null, ret)) {
        parse_comma_ops_right_side(&p, ret);
    } else {
        *ret = expr_undefined(span_from(a));
    }

    return parse_success(&p);
}

static bool parse_block(Parser* parent, bool toplevel, Block* ret);

// Handles `namespace A {}`, the legacy spelling `module A {}`, dotted names (`namespace A.B {}`, which become nested
// exported namespaces) and ambient external modules (`declare module "a" {}` and the body-less `declare module "a";`)
static bool parse_namespace(Parser* parent, NamespaceDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    bool is_module_keyword = false;
    if (hop_start_if_ends_on_word_break(&p, "module")) {
        is_module_keyword = true;
    } else if (!hop_start_if_ends_on_word_break(&p, "namespace")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }

    *ret = (NamespaceDecl){0};

    VecSpan names = vec_span_init();
    Span name;
    if (parse_identifier(&p, &name)) {
        vec_span_push(&names, name);
        while (true) {
            Parser dot = parser(&p);
            hop_whitespace(&dot);
            if (!hop_char(&dot, '.')) {
                parse_fail(&dot);
                break;
            }
            hop_whitespace(&dot);
            if (!parse_identifier(&dot, &name)) {
                parse_error(&dot, span_from_token(&dot), 1003, "Identifier expected.");
                parse_success(&dot);
                break;
            }
            vec_span_push(&names, name);
            parse_success(&dot);
        }
    } else {
        vec_span_drop(&names);
        Value val;
        Anchor name_anchor = anchor(&p);
        if (is_module_keyword && *p.pos != '"' && *p.pos != '\'') {
            // `module` is only a contextual keyword, tsc parses `module {}` as an expression followed by a block
            return parse_fail(&p);
        } else if (*p.pos == '{') {
            parse_error(&p, span_from_token(&p), 1437, "Namespace must be given a name.");
            ret->name = span_from(name_anchor);
        } else if (parse_literal(&p, &val)) {
            bool is_string = val.tag == VALUE_STRING;
            if (is_string && is_module_keyword) {
                ret->is_ambient_module = true;
                ret->name = span_from(name_anchor);
                ret->module_ref = add_module_ref(&p, cstr_move(&val.string), ret->name);
                ret->module_ref->is_declaration = true;
                value_drop(&val);
                hop_whitespace(&p);
                if (!parse_block(&p, true, &ret->contents)) {
                    // `declare module "a";` is a shorthand ambient module
                    ret->contents = (Block){0};
                    ret->is_shorthand = true;
                }
                ret->full_span = span_from(a);
                return parse_success(&p);
            } else if (is_string) {
                parse_error(&p, span_from(name_anchor), 1003, "Identifier expected.");
            } else {
                parse_error(&p, span_from(name_anchor), 2819, "Namespace name cannot be '", &val, "'.");
            }
            value_drop(&val);
            return parse_success(&p);
        } else {
            return parse_fail(&p);
        }
        names = vec_span_init();
    }
    hop_whitespace(&p);

    Block contents;
    if (!parse_block(&p, true, &contents)) {
        if (is_module_keyword && vec_span_size(&names) == 1 && *p.pos != '.') {
            // `module` is only a contextual keyword, this is probably an expression such as `module = 5`
            vec_span_drop(&names);
            return parse_fail(&p);
        }
        contents = (Block){0};
        expected_token_error(&p, "{");
    }

    // Build dotted names inside out
    for (isize i = vec_span_size(&names) - 1; i > 0; i--) {
        Span inner_name = *vec_span_at(&names, i);
        NamespaceDecl inner = {
            .name = inner_name,
            .full_span = span_make(p.shared->source, span_start_ptr(inner_name), p.pos),
            .contents = contents,
        };
        contents = (Block){0};
        vec_statement_push(&contents.statements, (Statement){
            .tag = STATEMENT_DECL_NAMESPACE,
            .decl = {
                .props = { .exported = DECL_EXPORTED_YES },
                .namespace = inner,
            },
        });
    }
    if (vec_span_size(&names)) {
        ret->name = *vec_span_at(&names, 0);
    }
    ret->contents = contents;
    vec_span_drop(&names);

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_global_block(Parser* parent, NamespaceDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "global")) {
        return parse_fail(&p);
    }

    *ret = (NamespaceDecl) {
        .name = span_from(a),
        .is_global = true,
    };

    hop_whitespace(&p);
    if (!parse_block(&p, false, &ret->contents)) {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_decl_statement(Parser* parent, bool toplevel, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    union {
        VariableStatement variable;
        FunctionImpl function;
        EnumDecl enum_decl;
        ClassDecl class;
        TypeDecl type_decl;
        InterfaceDecl interface_decl;
        Expr expr;
        Block block;
    } parsed;

    // Decorators can come before `export`, the ones after it are handled by parse_class_decl()
    VecExpr decorators = {0};
    Span decorators_span = span_null;
    if (parse_decorators(&p, &decorators)) {
        decorators_span = span_from(a);
    }

    ModifierList modifiers;
    if (!parse_modifier_list(&p, false, &modifiers)) {
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    DeclStatementProps decl_props = {
        .exported = modifiers.includes[MODIFIER_EXPORT_DEFAULT] ?
            DECL_EXPORTED_YES_DEFAULT :
            (modifiers.includes[MODIFIER_EXPORT] ?
                DECL_EXPORTED_YES :
                DECL_EXPORTED_NO
            ),
        .declared = modifiers.includes[MODIFIER_DECLARE] || p.shared->source->is_declaration,
    };

    modifier_list_validate_during_parse(
        &p,
        &modifiers,
        span_from(a),
        toplevel ? MODIFIER_VALIDATION_FLAG_TOPLEVEL_STATEMENT : MODIFIER_VALIDATION_FLAG_MODIFIERS_NOT_ALLOWED
    );

    if (decl_props.exported != DECL_EXPORTED_YES_DEFAULT &&
        parse_variable_statement(&p, expr_ctx_null, false, &parsed.variable)
    ) {
        bool is_using = parsed.variable.keyword == VAR_INIT_USING || parsed.variable.keyword == VAR_INIT_AWAIT_USING;
        for_each (i, vec_spanned_modifier, &modifiers.spanned) {
            if (!is_using || (i->tag != MODIFIER_EXPORT && i->tag != MODIFIER_DECLARE)) continue;
            // tsc's checkGrammarModifiers(): the first one
            if (parsed.variable.keyword == VAR_INIT_USING) {
                parse_error(&p, i->span, 1491, "'", i->span, "' modifier cannot appear on a 'using' declaration.");
            } else {
                parse_error(&p, i->span, 1495, "'", i->span, "' modifier cannot appear on an 'await using' declaration.");
            }
            break;
        }
        *ret = (Statement) {
            .tag = STATEMENT_DECL_VARIABLE,
            .decl = {
                .props = decl_props,
                .variable = parsed.variable,
            },
        };
    } else if (parse_function_keyword_function(&p, modifiers.includes[MODIFIER_ASYNC], &parsed.function)) {
        parsed.function.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_FUNCTION,
            .decl = {
                .props = decl_props,
                .function = {
                    .impl = c_new(FunctionImpl, parsed.function),
                },
            },
        };
    } else if (parse_class_decl(&p, &parsed.class)) {
        report_misplaced_async(&p, &modifiers);
        parsed.class.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_CLASS,
            .decl = {
                .props = decl_props,
                .class = c_new(ClassDecl, parsed.class),
            },
        };
    } else if (parse_type_decl(&p, &parsed.type_decl)) {
        report_misplaced_async(&p, &modifiers);
        parsed.type_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_TYPE,
            .decl = {
                .props = decl_props,
                .type = parsed.type_decl,
            },
        };
    } else if (parse_interface_decl(&p, &parsed.interface_decl)) {
        report_misplaced_async(&p, &modifiers);
        parsed.interface_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_INTERFACE,
            .decl = {
                .props = decl_props,
                .interface = c_new(InterfaceDecl, parsed.interface_decl),
            },
        };
    } else if (parse_enum_decl(&p, &parsed.enum_decl)) {
        report_misplaced_async(&p, &modifiers);
        parsed.enum_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_ENUM,
            .decl = {
                .props = decl_props,
                .enum_ = c_new(EnumDecl, parsed.enum_decl),
            },
        };
    } else if (parse_global_block(&p, &ret->decl.namespace)) {
        if (decl_props.declared) {
            for_each (i, vec_statement, &ret->decl.namespace.contents.statements) {
                if (statement_tag_is_decl(i->tag)) {
                    i->decl.props.declared = true;
                }
            }
        }
        ret->decl.namespace.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_NAMESPACE,
            .decl = {
                .props = decl_props,
                .namespace = ret->decl.namespace,
            },
        };
    } else if (decl_props.exported == DECL_EXPORTED_YES_DEFAULT && parse_expr(&p, expr_ctx_null, &parsed.expr)) {
        mark_named_evaluation(&parsed.expr, false);
        *ret = (Statement){
            .tag = STATEMENT_EXPORT_DEFAULT_EXPR,
            .expr = parsed.expr,
        };
    } else if (decl_props.exported != DECL_EXPORTED_YES_DEFAULT && parse_namespace(&p, &ret->decl.namespace)) {
        report_misplaced_async(&p, &modifiers);
        ret->decl.namespace.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_NAMESPACE,
            .decl = {
                .props = decl_props,
                .namespace = ret->decl.namespace,
            },
        };
    } else {
        modifier_list_drop(&modifiers);
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }

    if (!vec_expr_is_empty(&decorators)) {
        if (ret->tag == STATEMENT_DECL_CLASS) {
            // These come before any that parse_class_decl() found after `export`
            for_each (i, vec_expr, &ret->decl.class->decorators) {
                vec_expr_push(&decorators, *i);
            }
            ret->decl.class->decorators.size = 0; // The elements were moved
            vec_expr_drop(&ret->decl.class->decorators);
            ret->decl.class->decorators = decorators;
        } else {
            parse_error(&p, decorators_span, 1206, "Decorators are not valid here.");
            vec_expr_drop(&decorators);
        }
    }

    modifier_list_drop(&modifiers);
    return parse_success(&p);
}

// What a statement's JSDoc says (tsc's getJSDocCommentsAndTags / getNextJSDocCommentLocation, from the host down):
// a variable statement's `@type` is its single declaration's annotation, and like the tags of an assignment statement
// or `return` its `@param`s/`@returns` are for the function it gives
static void jsdoc_apply_statement(Parser* p, const char* start, Statement* statement) {
    VecJsDocTag tags = {0};
    if (!jsdoc_before(p, start, &tags)) return;
    switch (statement->tag) {
        case STATEMENT_DECL_VARIABLE: {
            VecInitializedBinding* list = &statement->decl.variable.initializers;
            if (vec_initialized_binding_size(list) != 1) break;
            InitializedBinding* initializer = list->data;
            jsdoc_apply_type_to_binding(&tags, &initializer->binding);
            FunctionImpl* function = jsdoc_function_of(&initializer->value);
            if (function) jsdoc_apply_function(&tags, function);
        }   break;
        case STATEMENT_DECL_FUNCTION:
            jsdoc_apply_function(&tags, statement->decl.function.impl);
            break;
        case STATEMENT_EXPR:
            if (statement->expr.tag == EXPR_BINARY_OP && statement->expr.binary_op->op == BINARY_OP_EQUAL) {
                FunctionImpl* function = jsdoc_function_of(&statement->expr.binary_op->right);
                if (function) jsdoc_apply_function(&tags, function);
            }
            break;
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR: {
            FunctionImpl* function = jsdoc_function_of(&statement->expr);
            if (function) jsdoc_apply_function(&tags, function);
        }   break;
        default:
            break;
    }
    vec_jsdoc_tag_drop(&tags);
}

static bool parse_statement(Parser* parent, bool toplevel, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    Span label = span_null;
    ret->jump_label = span_null;
    ret->flow = nullptr;
    if (*p.pos == ';') {
        *ret = statement_noop;
    } else if (hop_start_if_ends_on_word_break(&p, "debugger")) {
        *ret = statement_noop;
    } else if (parse_label(&p, &label)) {
        hop_whitespace(&p);
        Statement body;
        expect_statement(&p, false, &body);
        *ret = (Statement){
            .tag = STATEMENT_LABELED,
            .labeled = {
                .name = label,
                .body = c_new(Statement, body),
            },
        };
        label = span_null;
    } else if (parse_import_export_statement(&p, ret)) {
    } else if (parse_decl_statement(&p, toplevel, ret)) {
    } else if (parse_block(&p, false, &ret->block)) {
        ret->tag = STATEMENT_BLOCK;
    } else if (parse_if_else(&p, &ret->if_else)) {
        ret->tag = STATEMENT_IF_ELSE;
    } else if (parse_for_loop_statement(&p, ret)) {
    } else if (parse_while_loop_statement(&p, ret)) {
    } else if (parse_do_while_loop_statement(&p, ret)) {
    } else if (parse_switch_statement(&p, ret)) {
    } else if (parse_try_catch_statement(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "break")) {
        ret->tag = STATEMENT_BREAK;
        ret->keyword_span = span_from(a);
        parse_jump_target(&p, &label);
    } else if (hop_start_if_ends_on_word_break(&p, "continue")) {
        ret->tag = STATEMENT_CONTINUE;
        ret->keyword_span = span_from(a);
        parse_jump_target(&p, &label);
    } else if (parse_return(&p, &ret->expr)) {
        ret->tag = STATEMENT_RETURN_EXPR;
    } else if (parse_throw(&p, &ret->expr)) {
        ret->tag = STATEMENT_THROW_EXPR;
    } else if (parse_expr(&p, expr_ctx_null, &ret->expr)) {
        ret->tag = STATEMENT_EXPR;
        parse_comma_ops_right_side(&p, &ret->expr);
        if (ret->tag == STATEMENT_EXPR &&
            ret->expr.tag == EXPR_IDENTIFIER &&
            span_equals(ret->expr.identifier.name, "static")
        ) {
            hop_whitespace(&p);
            if (p.whitespace != WHITESPACE_NEWLINE && *p.pos != ';' && *p.pos != '}') {
                statement_drop(ret);
                return parse_fail(&p);
            }
        }
    } else {
        return parse_fail(&p);
    }

    if ((ret->tag == STATEMENT_BREAK || ret->tag == STATEMENT_CONTINUE) && span_valid(label)) {
        ret->jump_label = label;
    }
    ret->span = span_from(a);
    if (p.shared->source->is_js) jsdoc_apply_statement(&p, a.start, ret);

    reset_fail_cache(&p);
    return parse_success(&p);
}

static inline bool expect_statement(Parser* parent, bool toplevel, Statement* ret) {
    if (parse_statement(parent, toplevel, ret)) {
        return true;
    } else {
        expected_expression_error(parent);
        *ret = (Statement){
            .tag = STATEMENT_EXPR,
            .expr = expr_implicit_any(span_from_pos(parent)),
        };
        return false;
    }
}

static bool parse_block(Parser* parent, bool toplevel, Block* ret) {
    Parser p = parser(parent);

    const char* open = p.pos;
    if (!hop_char(&p, '{')) return parse_fail(&p);
    hop_whitespace(&p);

    *ret = (Block){0};
    if (!parse_append_statement_sequence(&p, toplevel, &ret->statements)) {
        *ret = (Block){0};
    }
    hop_whitespace(&p);

    expect_closing(&p, '}', open);
    return parse_success(&p);
}

void parse_file(SourceFile* source, ErrorCtx* errs) {
    ParserShared shared = {
        .source = source,
        .fail_cache_start = source_file_start(source),
        .errors = vec_error_init(),
    };

    Parser p = {
        .shared = &shared,
        .parent = nullptr,
        .pos = source_file_start(source),
        .error_count = 0,
    };

    p.pos = file_start_excluding_bom(p.pos);
    if (p.pos[0] == '#' && p.pos[1] == '!') {
        // A shebang line is trivia (tsc's scanShebangTrivia)
        while (*p.pos && *p.pos != '\n') p.pos++;
    }
    parse_source_directives(&p, &source->ast.statements);
    hop_whitespace(&p);

    while (true) {
        parse_append_statement_sequence(&p, true, &source->ast.statements);
        if (*p.pos) {
            parse_error(&p, hop_token_get_span(&p), 1128, "Declaration or statement expected.");
            hop_whitespace(&p);
        } else {
            break;
        }
    }

    // tsc checks modifiers in the checker (checkGrammarModifiers): those don't count as parse errors, which quiet the
    // checker's grammar errors
    static unsigned const modifier_codes[] = { 1024, 1028, 1029, 1030, 1031, 1044, 1070, 1184, 1248, 1253 };
    source->has_import_meta = shared.saw_import_meta;
    source->has_commonjs = shared.saw_commonjs;
    source->module_exports_span = shared.module_exports_span;
    source->has_jsx = shared.saw_jsx;
    source->has_parse_errors = false;
    for_each (i, vec_error, &shared.errors) {
        bool modifier = false;
        for (u32 c = 0; c < c_arraylen(modifier_codes); c++) modifier |= i->ts_code == modifier_codes[c];
        source->has_parse_errors |= !modifier;
    }
    for_each (i, vec_error, &shared.errors) {
        error_ctx_append_error(errs, *i);
        *i = (Error){0};
    }

    parser_shared_drop(&shared);
}
