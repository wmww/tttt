#include "parser.h"
#include "unicode_identifiers.h"
#include "scope.h"
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
#include "flow.h"
#include "source_file.h"
#include "math_helpers.h"
#include <math.h>
#include "string_helpers.h"

#define g_type VecU8, vec_u8
#define g_key uint8_t, u8
#include <cleat/generic/vec.h>

typedef struct JsDocRange {
    const char* start; // The `/**`
    const char* end; // Past the `*/`
    // Its declaration tags (`@typedef`, `@callback`, `@import`) were bound by the innermost statement list containing
    // it (jsdoc_bind_comments())
    bool claimed;
} JsDocRange;
#define g_key JsDocRange, jsdoc_range
#include <cleat/generic/vec.h>

#define MAX_ERRORS 500 // Must be less than the max value storable in Parser.error_count

typedef enum WhitespaceType {
    WHITESPACE_NONE = 0,
    WHITESPACE_PARSED,
    WHITESPACE_NEWLINE,
} WhitespaceType;

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
    // Every `module.exports` of a JavaScript file (malloc'd, attempts that backtrack too), and where the first thing
    // that makes it CommonJS starts: SourceFile.module_exports_span is the first of them from there on
    Span* module_exports_accesses;
    u32 module_exports_access_count;
    const char* commonjs_start;
    bool saw_commonjs; // A `require()` call, an assignment to `exports.x` or `module.exports` (a JavaScript file's)
    // Top-level variables of a JavaScript file initialized to `module.exports`, `exports` or another of them (malloc'd):
    // `M.x = v` with one of them makes the file CommonJS too (tsc's isExportsOrModuleExportsOrAlias)
    csview* exports_aliases;
    u32 exports_alias_count;
    u32 exports_alias_cap;
    // JavaScript files: every `/** */` comment hopped so far (not `/**/`), sorted by start. Hosts find theirs by looking
    // back from their first token (jsdoc_before())
    VecJsDocRange jsdoc_comments;
    // Parsing a type inside a JSDoc comment: a line's leading `*` is whitespace, `*/` ends everything, the JSDoc-only
    // type forms (`?T`, `T=`, `*`, `function(...)`...) are allowed
    bool in_jsdoc;
    const char* jsdoc_end; // The `*/` of the comment being parsed, with in_jsdoc
    // The last postfix `T?` parsed in JSDoc, from T's start to after the `?`: a tuple element that is exactly this is
    // an optional element (tsc's parseTupleElementType)
    const char* jsdoc_postfix_nullable_start;
    const char* jsdoc_postfix_nullable_end;
    // The type alias an `@enum` on the statement just parsed declares, for the statement list to append after it
    Statement* jsdoc_enum;
    u32 parsing_context; // The lists being parsed, a bit per ParsingContext
    // Where the statements being parsed are, for the modifier checks (modifier_list_check_grammar())
    bool in_ambient; // tsc's NodeFlags.Ambient context: a declaration file, a `declare` declaration's insides
    ModifiedParent statement_parent;
    bool in_namespace_body; // The module block is a `namespace N {}`'s, not an ambient module's
    // The end of the file's last token (tsc's full start of the end-of-file token): where a missing expression at the
    // end of the file is reported. The smallest position trivia was skipped from up to the end
    const char* eof_full_start;
    u32 errors_attempted; // Every parse_error() call, the ones dropped too (a position that already has one)
    // The parse errors of JSDoc comments (tsc's jsDocDiagnostics): kept whether or not the attempt that parsed the
    // comment succeeds (a comment is parsed by every attempt that parses its host), deduplicated at the end, and only
    // reported in checked JavaScript
    VecError jsdoc_errors;
} ParserShared;

typedef struct FunctionFlags {
    bool is_async;
    bool is_generator;
    Span asterisk; // The `*` of a generator
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
    if (my jsdoc_enum) {
        statement_drop(my jsdoc_enum);
        free(my jsdoc_enum);
    }
    vec_error_drop(&my errors);
    vec_error_drop(&my jsdoc_errors);
}

typedef struct ExprCtx {
    bool reject_in_operator;
    bool reject_operators;
    bool entity_name; // The operand of a `typeof` type: `a.b.c<T>`, no calls and no `[i]` (that is an indexed access type)
    bool decorator; // `@dec [x]`: the `[` starts a computed member name, not an element access (tsc's inDecoratorContext)
    // A conditional's true branch (tsc's allowReturnTypeInArrowFunction false): an arrow function there only has a
    // return type if a `:` follows it, `a ? (b) : c => d` is `a ? b : (c => d)`
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
// The errors of an invalid escape (tsc's scanEscapeSequence), which the caller reports unless it's in a tagged template
typedef struct EscapeErrors {
    u32 count;
    struct { u32 code; const char* at; u32 len; u32 value; } errors[2];
} EscapeErrors;

static void escape_error(EscapeErrors* errors, u32 code, const char* at, u32 len) {
    if (errors->count < c_arraylen(errors->errors)) errors->errors[errors->count++] = (typeof(errors->errors[0])){ code, at, len, 0 };
}

// After the backslash. An invalid escape stands for its own text (`\u{` in "\u{-1}", like tsc's) and fills `errors`
static bool parse_string_escape(Parser* p, cstr* str, EscapeErrors* errors) {
    const char* start = p->pos - 1;
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
            if (!(*p->pos >= '0' && *p->pos <= '9')) {
                cstr_append_n(str, "\0", 1);
                return true;
            }
            // (`\08` is `\0` and `8`, but still a legacy octal escape)
            [[fallthrough]];
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
            // A legacy octal escape (TS1487): `\1`, `\17`, `\177`, but `\47` not `\477`
            u32 value = (u32)(c - '0');
            if (c <= '3' && *p->pos >= '0' && *p->pos <= '7') value = value * 8 + (u32)(*p->pos++ - '0');
            if (*p->pos >= '0' && *p->pos <= '7' && p->pos - start < 4) value = value * 8 + (u32)(*p->pos++ - '0');
            escape_error(errors, 1487, start, (u32)(p->pos - start));
            errors->errors[errors->count - 1].value = value;
            append_code_point(str, value);
            return false;
        }
        case '8': case '9':
            escape_error(errors, 1488, start, 2);
            cstr_append_n(str, &c, 1);
            return false;
        case '\r':
            if (*p->pos == '\n') p->pos++;
            return true; // Line continuation
        case '\n':
            return true;
        case 'x': {
            int hi = hex_digit_value(p->pos[0]);
            int lo = hi < 0 ? -1 : hex_digit_value(p->pos[1]);
            if (lo < 0) {
                if (hi >= 0) p->pos++;
                escape_error(errors, 1125, p->pos, 0);
                cstr_append_n(str, start, p->pos - start);
                return false;
            }
            p->pos += 2;
            append_code_point(str, (u32)(hi * 16 + lo));
            return true;
        }
        case 'u': {
            u32 code = 0;
            if (*p->pos == '{') {
                const char* digits_start = p->pos + 1;
                const char* q = digits_start;
                bool too_big = false;
                for (; hex_digit_value(*q) >= 0; q++) {
                    code = code * 16 + (u32)hex_digit_value(*q);
                    too_big |= code > 0x10FFFF;
                }
                bool valid = true;
                if (q == digits_start) {
                    escape_error(errors, 1125, q, 0);
                    valid = false;
                } else if (too_big) {
                    escape_error(errors, 1198, digits_start, (u32)(q - digits_start));
                    valid = false;
                }
                if (!*q) {
                    escape_error(errors, 1126, q, 0);
                    valid = false;
                } else if (*q == '}') {
                    q++;
                } else {
                    escape_error(errors, 1199, q, 0);
                    valid = false;
                }
                p->pos = q;
                if (!valid) {
                    cstr_append_n(str, start, p->pos - start);
                    return false;
                }
            } else {
                for (int i = 0; i < 4; i++) {
                    int v = hex_digit_value(p->pos[i]);
                    if (v < 0) {
                        p->pos += i;
                        escape_error(errors, 1125, p->pos, 0);
                        cstr_append_n(str, start, p->pos - start);
                        return false;
                    }
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
        case '\xE2':
            if ((unsigned char)p->pos[0] == 0x80 && ((unsigned char)p->pos[1] & 0xFE) == 0xA8) {
                p->pos += 2;
                return true; // Line continuation by U+2028/U+2029
            }
            break;
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

// The first push reserves room for 4, and these elements are big (a parameter is ~570 bytes): lists of one or two were
// most of the AST's memory. A finished list gives back what it doesn't use
#define shrink_list(vec_kind, vec) do { \
        if ((vec)->size && (vec)->size < (vec)->capacity) vec_kind##_shrink_to_fit(vec); \
    } while (0)
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
    bool commit_on_paren,
    FunctionImpl* ret
);
static bool parse_numeric_value(Parser* parent, Value* ret);
static bool parse_string(Parser* parent, cstr* ret);
static bool parse_template(Parser* parent, VecExpr* ret, VecCStr* texts);
static bool parse_comma_ops_right_side(Parser* parent, Expr* left_and_ret);
static bool parse_decorators(Parser* parent, VecExpr* ret);
static bool parse_type_compat_literal(Parser* parent, Value* ret);
static bool parse_template_type(Parser* parent, TypeNode* ret);
static bool parse_expr(Parser* parent, ExprCtx ctx, Expr* ret);
static inline bool expect_expr(Parser* parent, ExprCtx ctx, Expr* ret);
static bool expr_ends_with_arrow(Expr const* e);
static bool jsx_generic_arrow_start(Parser* parent);
static bool parse_binding_content(Parser* parent, Binding* ret);
static bool is_reserved_word(csview word);
static bool parse_binding_identifier(Parser* parent);
static bool parse_binding_content_or_private_name(Parser* parent, Binding* ret, u32 code, char const* message);
static void parse_import_attributes_body(Parser* parent, ImportAttributes* ret);
static bool parse_binding_element(Parser* parent, Binding* ret);
static bool parse_binding_type_or_set_to_default(Parser* parent, Binding* ret);
static bool parse_binding_type_ex(Parser* parent, Binding* ret, bool report_js);
static bool parse_initialization(Parser* parent, ExprCtx ctx, Expr* ret);
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

static bool last_error_starts_at(Parser* me, Span span);
static int invalid_char_size(const char* c);

// tsc's parseErrorAtPosition: an error that starts where the previous one does is dropped (the first error at a
// position is the one that explains it; whatever recovery reports there next is noise). An error at a character no
// token starts with is TS1127, which tsc's scanner reports before the parser gets to say anything there
static Error* parse_error_inner(Parser* me, Span span, unsigned ts_code, cstr message, bool in_literal) {
    my shared->errors_attempted++;
    int invalid = span.source && !in_literal ? invalid_char_size(span_start_ptr(span)) : 0;
    if (invalid && ts_code != 1127) {
        cstr_drop(&message);
        return parse_error_inner(me, span_make(my shared->source, span_start_ptr(span), span_start_ptr(span) + invalid),
            1127, cstr_lit("Invalid character."), false);
    }
    // Likewise a `#!` anywhere but at the start of the file (the scanner's TS18026 comes first)
    const char* at = span_start_ptr(span);
    if (span.source && !in_literal && ts_code != 18026 && at && at[0] == '#' && at[1] == '!' &&
        at != cstr_str(&my shared->source->contents)
    ) {
        cstr_drop(&message);
        return parse_error_inner(me, span_make(my shared->source, at, at + 2), 18026,
            cstr_lit("'#!' can only be used at the start of a file."), false);
    }
    if (last_error_starts_at(me, span)) {
        cstr_drop(&message);
        return nullptr;
    } else if (vec_error_size(&my shared->errors) >= MAX_ERRORS) {
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

static Error* parse_error_s(Parser* me, Span span, unsigned ts_code, cstr message) {
    return parse_error_inner(me, span, ts_code, message, false);
}

/// Builds the message from a display sequence
#define parse_error(me, span, ts_code, ...) parse_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))
// An error inside a literal (an escape sequence): whatever character it starts at, it's not a token
#define parse_error_in_literal(me, span, ts_code, ...) parse_error_inner(me, span, ts_code, cstr_d(__VA_ARGS__), true)

// TypeScript-only syntax in a JavaScript file (tsc's getJSSyntacticDiagnosticsForFile: TS8002-TS8017, TS8037). They're
// syntactic diagnostics of their own: not parse errors (grammar errors are still reported), reported whether or not
// the file is checked, never deduplicated with parse errors
static bool is_js_syntax_code(unsigned code) {
    return (code >= 8002 && code <= 8017) || code == 8037;
}

static bool reports_js_syntax(Parser const* me) {
    return my shared->source && my shared->source->is_js && !my shared->in_jsdoc;
}

static void js_syntax_error_s(Parser* me, Span span, unsigned ts_code, cstr message) {
    if (!reports_js_syntax(me)) {
        cstr_drop(&message);
        return;
    }
    add_error_to_parser(me, (Error){ .message = message, .span = span, .ts_code = ts_code, .phase = ERROR_PHASE_PARSE });
}

#define js_syntax_error(me, span, ts_code, ...) js_syntax_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

// The TypeScript-only syntax errors inside what one of them is about are not reported (tsc's walk returns "skip"):
// drops those reported since `start` (a size of the error list)
static void js_syntax_skip_since(Parser* me, intptr_t start) {
    if (!reports_js_syntax(me)) return;
    VecError* errors = &my shared->errors;
    intptr_t kept = start;
    for (intptr_t i = start; i < vec_error_size(errors); i++) {
        Error* error = vec_error_at_mut(errors, i);
        if (is_js_syntax_code(error->ts_code)) {
            error_drop(error);
            my error_count--;
        } else {
            if (kept != i) errors->data[kept] = *error;
            kept++;
        }
    }
    errors->size = kept;
}

// Moves the errors a JSDoc parser `me` reported to the JSDoc errors, before it fails
static void jsdoc_keep_errors(Parser* me) {
    VecError* errors = &my shared->errors;
    for (intptr_t i = vec_error_size(errors) - my error_count; i < vec_error_size(errors); i++) {
        Error error = errors->data[i];
        if (my shared->source->is_js) {
            error.phase = ERROR_PHASE_RESOLVE_NAMES;
            vec_error_push(&my shared->jsdoc_errors, error);
        } else {
            error_drop(&error);
        }
    }
    errors->size -= my error_count;
    my error_count = 0;
}

// An error about a JSDoc comment that isn't a parse error of its text (tsc's checker reports it about the tags), kept
// like those
static void jsdoc_error_s(Parser* me, Span span, unsigned ts_code, cstr message) {
    vec_error_push(&my shared->jsdoc_errors, (Error){
        .message = message, .span = span, .ts_code = ts_code, .phase = ERROR_PHASE_RESOLVE_NAMES,
    });
}

#define jsdoc_error(me, span, ts_code, ...) jsdoc_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

static intptr_t error_mark(Parser const* me) {
    return vec_error_size(&my shared->errors);
}

// A function's type parameters (`type_params`: of a declaration, not of a signature in a type) and return type in
// JavaScript
static void js_syntax_function(Parser* me, FunctionImpl const* function, bool type_params) {
    if (!reports_js_syntax(me)) return;
    VecGenericItem const* items = &function->generic.items;
    if (type_params && !vec_generic_item_is_empty(items) && !vec_generic_item_front(items)->jsdoc) {
        Span span = span_between(vec_generic_item_front(items)->full_span, vec_generic_item_back(items)->full_span);
        js_syntax_error(me, span, 8004, "Type parameter declarations can only be used in TypeScript files.");
    }
    if (function->ret_explicit && span_valid(function->ret_span)) {
        js_syntax_error(me, function->ret_span, 8010, "Type annotations can only be used in TypeScript files.");
    }
}

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

static void report_escape_errors(Parser* me, EscapeErrors const* errors) {
    for (u32 i = 0; i < errors->count; i++) {
        Span at = span_make(my shared->source, errors->errors[i].at, errors->errors[i].at + errors->errors[i].len);
        switch (errors->errors[i].code) {
            case 1125: parse_error_in_literal(me, at, 1125, "Hexadecimal digit expected."); break;
            case 1126: parse_error_in_literal(me, at, 1126, "Unexpected end of text."); break;
            case 1198:
                parse_error_in_literal(me, at, 1198, "An extended Unicode escape value must be between 0x0 and 0x10FFFF inclusive.");
                break;
            case 1199: parse_error_in_literal(me, at, 1199, "Unterminated Unicode escape sequence."); break;
            case 1487: {
                char hex[8];
                snprintf(hex, sizeof(hex), "\\x%02x", errors->errors[i].value);
                parse_error_in_literal(me, at, 1487, "Octal escape sequences are not allowed. Use the syntax '", hex, "'.");
            }   break;
            case 1488: parse_error_in_literal(me, at, 1488, "Escape sequence '", at.sv, "' is not allowed."); break;
            default: impossible;
        }
    }
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

// Of each ASCII character: bit 1 if it can start an identifier, bit 2 if it can continue one
static const uint8_t identifier_char_class[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0,
    0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 3,
    0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
};

// ASCII only: identifier_char_size() knows the rest
static inline bool is_identifier_char(char c) {
    return identifier_char_class[(uint8_t)c] & 2;
}

static inline bool is_identifier_start_char(char c) {
    return identifier_char_class[(uint8_t)c] & 1;
}

static int identifier_char_size_slow(const char* c, bool start);

// The length of the character at c if it can start (`start`) or continue an identifier, else 0: ASCII, non-ASCII
// (tsc's unicode tables for the target) and unicode escapes of either (`\u0061`, `\u{61}`)
static inline int identifier_char_size(const char* c, bool start) {
    uint8_t b = (uint8_t)*c;
    if (identifier_char_class[b] & (start ? 1 : 2)) return 1;
    if (b < 0x80 && b != '\\') return 0;
    return identifier_char_size_slow(c, start);
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
    int size = identifier_char_size(parent->pos, true);
    if (size) {
        parent->pos += size;
        while (true) {
            while (is_identifier_char(*parent->pos)) parent->pos++;
            if ((uint8_t)*parent->pos < 0x80 && *parent->pos != '\\') break;
            if (!(size = identifier_char_size_slow(parent->pos, false))) break;
            parent->pos += size;
        }
        if (ret) {
            *ret = span_from(a);
        }
        return true;
    } else {
        return false;
    }
}

// The words parse_identifier() refuses
static inline bool is_reserved_identifier(const char* word, intptr_t len) {
#define WORD_IS(lit) (len == sizeof(lit) - 1 && memcmp(word, lit, sizeof(lit) - 1) == 0)
    switch (word[0]) {
        case 'r': return WORD_IS("return");
        case 't': return WORD_IS("throw") || WORD_IS("typeof") || WORD_IS("this");
        case 'c': return WORD_IS("case");
        case 'i': return WORD_IS("in");
        case 's': return WORD_IS("super");
        case 'd': return WORD_IS("default");
        default: return false;
    }
#undef WORD_IS
}

// ret can be null
static bool parse_identifier(Parser* parent, Span* ret) {
    Anchor a = anchor(parent);
    if (!parse_identifier_or_keyword(parent, nullptr)) {
        return false;
    }
    Span s = span_from(a);
    if (is_reserved_identifier(a.start, parent->pos - a.start)) {
        parent->pos = a.start;
        return false;
    }
    if (ret) {
        *ret = s;
    }
    return true;
}

// A private name `#x` (its span is the name), or a lone `#`: tsc's scanner makes that a PrivateIdentifier `#` too,
// reporting TS1127 (not `#!`)
static bool hop_private_name(Parser* parent) {
    if (parent->pos[0] != '#' || parent->pos[1] == '!') return false;
    Anchor a = anchor(parent);
    parent->pos++;
    if (!parse_identifier_or_keyword(parent, nullptr)) {
        parse_error(parent, span_from(a), 1127, "Invalid character.");
    } else if (span_equals(span_from(a), "#constructor")) {
        // (tsc's binder, checkPrivateIdentifier: unless the file has parse errors, see is_grammar_code())
        parse_error(parent, span_from(a), 18012, "'#constructor' is a reserved word.");
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
            const char* newline = strchr(parent->pos + 2, '\n');
            parent->pos = newline ? newline : parent->pos + 2 + strlen(parent->pos + 2);
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
            // The `*/` (not the `*` of the `/*`), found with the libc scans
            const char* pos = start + 2;
            const char* slash;
            while (true) {
                slash = strchr(pos, '/');
                if (!slash) {
                    if (strchr(start, '\n')) parent->whitespace = WHITESPACE_NEWLINE;
                    return false;
                }
                if (slash[-1] == '*' && slash - 1 >= start + 2) break;
                pos = slash + 1;
            }
            pos = slash + 1;
            const char* last_line_start = start;
            if (memchr(start, '\n', pos - start)) {
                parent->whitespace = WHITESPACE_NEWLINE;
                last_line_start = pos;
                while (last_line_start[-1] != '\n') last_line_start--;
            }
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

// The byte length of the non-ASCII whitespace (tsc's isWhiteSpaceSingleLine: NBSP, NEL, BOM, category Zs and U+200B)
// at c, negated for a line terminator (U+2028, U+2029), 0 if there is none
static int unicode_whitespace_size(const char* c) {
    uint8_t b0 = (uint8_t)c[0];
    if (b0 < 0xC2) return 0;
    if (b0 == 0xC2) return (uint8_t)c[1] == 0xA0 || (uint8_t)c[1] == 0x85 ? 2 : 0;
    uint8_t b1 = (uint8_t)c[1];
    uint8_t b2 = b1 ? (uint8_t)c[2] : 0;
    if (b0 == 0xE1) return b1 == 0x9A && b2 == 0x80 ? 3 : 0; // U+1680
    if (b0 == 0xE2 && b1 == 0x80) {
        if (b2 >= 0x80 && b2 <= 0x8B) return 3; // U+2000..U+200B
        if (b2 == 0xA8 || b2 == 0xA9) return -3; // U+2028, U+2029
        if (b2 == 0xAF) return 3; // U+202F
        return 0;
    }
    if (b0 == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3; // U+205F
    if (b0 == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3; // U+3000
    if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) return 3; // U+FEFF
    return 0;
}

// The length of the UTF-8 character at c (1 for a stray continuation byte)
static int utf8_char_size(const char* c) {
    unsigned char b = (unsigned char)c[0];
    int size = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
    for (int i = 1; i < size; i++) {
        if (((unsigned char)c[i] & 0xC0) != 0x80) return i;
    }
    return size;
}

static int identifier_escape_size(const char* c);

// The code point of the (well-formed) UTF-8 character of `size` bytes at c
static u32 utf8_code_point(const char* c, int size) {
    const unsigned char* b = (const unsigned char*)c;
    switch (size) {
        case 2: return (u32)(b[0] & 0x1F) << 6 | (b[1] & 0x3F);
        case 3: return (u32)(b[0] & 0x0F) << 12 | (u32)(b[1] & 0x3F) << 6 | (b[2] & 0x3F);
        case 4: return (u32)(b[0] & 0x07) << 18 | (u32)(b[1] & 0x3F) << 12 | (u32)(b[2] & 0x3F) << 6 | (b[3] & 0x3F);
        default: return b[0];
    }
}

static int identifier_char_size_slow(const char* c, bool start) {
    u32 code;
    int size;
    if (*c == '\\') {
        size = identifier_escape_size(c);
        if (!size) return 0;
        code = 0;
        for (const char* d = c + 2; d < c + size; d++) {
            if (hex_digit_value(*d) >= 0) code = code * 16 + (u32)hex_digit_value(*d);
            if (code > 0x10FFFF) return 0;
        }
        if (code < 0x80) return (identifier_char_class[code] & (start ? 1 : 2)) ? size : 0;
    } else {
        size = utf8_char_size(c);
        if (size < 2) return 0;
        code = utf8_code_point(c, size);
    }
    bool esnext = compiler_options()->target >= SCRIPT_TARGET_ES2015;
    return (start ? unicode_is_identifier_start(code, esnext) : unicode_is_identifier_part(code, esnext)) ? size : 0;
}

// A character at c that starts no token (tsc's scanner: TS1127 Invalid character): its length, else 0
static int invalid_char_size(const char* c) {
    unsigned char b = (unsigned char)c[0];
    if (b >= 0x80) return unicode_whitespace_size(c) || identifier_char_size_slow(c, true) ? 0 : utf8_char_size(c);
    if (b == '\\') return identifier_char_size_slow(c, true) ? 0 : 1;
    if (b && b < 0x20 && !is_js_ascii_space((char)b)) return 1;
    // A `#` that starts no private name (tsc's scanner still makes it a PrivateIdentifier token, with this error).
    // `#!` is TS18026
    if (b == '#' && c[1] != '!' && !identifier_char_size(c + 1, true)) return 1;
    return 0;
}

static void hop_jsdoc_optional(Parser* parent, TypeNode* type);
static void hop_whitespace_slow(Parser* parent, bool hop_comments, bool hop_newlines);

// U+FFFD, which is also what bytes that aren't UTF-8 become when the file is loaded (text_decode())
static bool is_replacement_char(const char* c) {
    return (unsigned char)c[0] == 0xEF && (unsigned char)c[1] == 0xBF && (unsigned char)c[2] == 0xBD;
}

// tsc's isConflictMarkerTrivia: seven `<`, `=`, `>` or `|` at the start of a line, then a space (`=======` needs
// none)
static bool is_conflict_marker(Parser* parent, const char* c) {
    if (c[0] != '<' && c[0] != '=' && c[0] != '>' && c[0] != '|') return false;
    if (c != cstr_str(&parent->shared->source->contents) && c[-1] != '\n' && c[-1] != '\r') return false;
    for (int i = 1; i < 7; i++) {
        if (c[i] != c[0]) return false;
    }
    return c[7] && (c[0] == '=' || c[7] == ' ');
}

// tsc's scanConflictMarkerTrivia: TS1185, then the marker's line is trivia, and after a `=======` or `|||||||` the
// other side of the conflict too (up to the next `=======` or `>>>>>>>`)
static const char* hop_conflict_marker(Parser* parent, const char* c) {
    parse_error(parent, span_make(parent->shared->source, c, c + 7), 1185, "Merge conflict marker encountered.");
    char marker = c[0];
    if (marker == '<' || marker == '>') {
        while (*c && *c != '\n' && *c != '\r') c++;
        return c;
    }
    for (; *c; c++) {
        if ((*c == '=' || *c == '>') && *c != marker && is_conflict_marker(parent, c)) break;
    }
    return c;
}

static inline bool is_conflict_marker_char(char c) {
    return c == '<' || c == '=' || c == '>' || c == '|';
}

// At the start of a line: the conflict markers there (after a ======= the next one is at a line start too)
static void hop_conflict_markers(Parser* parent) {
    while (is_conflict_marker(parent, parent->pos)) parent->pos = hop_conflict_marker(parent, parent->pos);
}

// Most calls are at a token already: whitespace is ASCII at most ' ', and the rest starts with `/` or a non-ASCII byte
static inline void hop_whitespace_full(Parser* parent, bool hop_comments, bool hop_newlines) {
    if (parent->pos == parent->whitespace_end) {
        return;
    }
    char c = *parent->pos;
    if ((unsigned char)c > ' ' && (unsigned char)c < 0x80 && c != '/') {
        parent->whitespace = WHITESPACE_NONE;
        parent->whitespace_end = parent->pos;
        return;
    }
    hop_whitespace_slow(parent, hop_comments, hop_newlines);
}

static void hop_whitespace_slow(Parser* parent, bool hop_comments, bool hop_newlines) {
    const char* start = parent->pos;
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
                } else if (is_conflict_marker_char(*parent->pos)) {
                    // (Only a line's start can be a conflict marker, the file's start is parse_file()'s)
                    hop_conflict_markers(parent);
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
                if (!size && is_replacement_char(parent->pos)) {
                    // tsc's NonTextFileMarkerTrivia: the rest of the file is trivia
                    const char* file_start = cstr_str(&parent->shared->source->contents);
                    parse_error_inner(parent, span_make(parent->shared->source, file_start, file_start), 1490,
                        cstr_lit("File appears to be binary."), true);
                    parent->pos += strlen(parent->pos);
                    break;
                }
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
    if (!*parent->pos && !parent->shared->in_jsdoc &&
        (!parent->shared->eof_full_start || start < parent->shared->eof_full_start)) {
        parent->shared->eof_full_start = start;
    }
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

// At the end of the file tsc reports at the end of the last token (createIdentifier's reportAtCurrentPosition)
static Span span_from_token_or_eof(Parser* parent) {
    const char* eof_start = parent->shared->eof_full_start;
    if (!*parent->pos && eof_start && !parent->shared->in_jsdoc) return span_make(parent->shared->source, eof_start, eof_start);
    return span_from_token(parent);
}

static Error* expected_expression_error(Parser* parent) {
    return parse_error(parent, span_from_token_or_eof(parent), 1109, "Expression expected.");
}

// The keyword probe: no child parser (nothing to backtrack), and most probes fail on the first character
__attribute__((always_inline)) static inline bool hop_start_if_ends_on_word_break(Parser* parent, const char* prefix) {
#ifdef DEBUG_BUILD
    check(!parent->has_panding_child);
#endif
    const char* pos = parent->pos;
    if (*pos != *prefix) return false;
    while (*prefix) {
        if (*pos != *prefix) return false;
        prefix++;
        pos++;
    }
    if (identifier_char_size(pos, false)) return false;
    parent->pos = pos;
    return true;
}

static inline bool starts_with_followed_by_word_break(Parser* parent, const char* prefix) {
    Parser p = parser(parent);
    bool ret = hop_start_if_ends_on_word_break(&p, prefix);
    parse_fail(&p);
    return ret;
}

// tsc's isIdentifier: `yield` in a generator and `await` in an async function or a class static block aren't
// identifiers (where they aren't operators either, as the operand of a `++` or a type assertion, the expression is
// missing)
static bool yield_await_is_keyword_here(Parser* parent) {
    return (parent->shared->in_generator && starts_with_followed_by_word_break(parent, "yield")) ||
        ((parent->shared->in_async || parent->shared->in_static_block) && starts_with_followed_by_word_break(parent, "await"));
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
            if (parent->pos[1] == '.' && parent->pos[2] == '.') {
                parent->pos += 3;
                return true;
            } else if (is_number_char(*(parent->pos + 1))) {
                break;
            } else {
                parent->pos++;
                return true;
            }

        case '#':
            if (identifier_char_size(parent->pos + 1, true)) {
                parent->pos++;
                parse_identifier_or_keyword(parent, nullptr);
                return true;
            }
            break;

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
        parent->pos += utf8_char_size(parent->pos);
        return true;
    }
}

static Span hop_token_get_span(Parser* parent) {
    Anchor a = anchor(parent);
    hop_token(parent);
    return span_from(a);
}

// Token lookahead, tsc's isStartOf* predicates. Only list recovery uses these (see ParsingContext): the parser proper
// decides by trying to parse
typedef struct Tok {
    const char* start;
    const char* end;
    bool newline_before;
} Tok;

// The end of the token at c, tsc's scanner's idea of it but cheap: no values
static const char* tok_scan_end(const char* c) {
    if (!*c) return c;
    int size = identifier_char_size(c + (*c == '#'), true);
    if (size) {
        c += size + (*c == '#');
        while ((size = identifier_char_size(c, false))) c += size;
        return c;
    }
    if ((*c >= '0' && *c <= '9') || (*c == '.' && c[1] >= '0' && c[1] <= '9')) {
        c++;
        while (is_number_char(*c) || ((*c == '+' || *c == '-') && (c[-1] == 'e' || c[-1] == 'E'))) c++;
        return c;
    }
    if (*c == '"' || *c == '\'') {
        char quote = *c++;
        while (*c && *c != quote && *c != '\n') {
            if (*c == '\\' && c[1]) c++;
            c++;
        }
        return *c == quote ? c + 1 : c;
    }
    if (*c == '`') {
        // A template without substitutions, or a template head (up to its `${`)
        c++;
        while (*c && *c != '`' && !(c[0] == '$' && c[1] == '{')) {
            if (*c == '\\' && c[1]) c++;
            c++;
        }
        return *c == '`' ? c + 1 : *c ? c + 2 : c;
    }
    // Only these start a punctuator of more than one character
    if (!strchr(">.=!*<&|?+-/%^", *c)) return c + utf8_char_size(c);
    // Longest punctuator first
    static const char* const puncts[] = {
        ">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "&&=", "||=", "?\?=",
        "=>", "==", "!=", "<=", ">=", "&&", "||", "??", "?.", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
        "**", "<<", ">>", nullptr,
    };
    for (const char* const* punct = puncts; *punct; punct++) {
        if ((*punct)[0] != *c) continue;
        size_t n = strlen(*punct);
        if (strncmp(c, *punct, n) == 0) {
            // `?.5` is `?` then a number
            if (n == 2 && c[0] == '?' && c[1] == '.' && c[2] >= '0' && c[2] <= '9') break;
            return c + n;
        }
    }
    return c + utf8_char_size(c);
}

static Tok tok_at(Parser* parent, const char* pos, bool newline_before) {
    Parser p = parser(parent);
    p.pos = pos;
    p.whitespace = newline_before ? WHITESPACE_NEWLINE : WHITESPACE_NONE;
    p.whitespace_end = nullptr;
    hop_whitespace(&p);
    Tok t = { .start = p.pos, .end = tok_scan_end(p.pos), .newline_before = newline_before || p.whitespace == WHITESPACE_NEWLINE };
    parse_fail(&p);
    return t;
}

// The token at the parser's position (after whitespace, which may be hopped already)
static Tok tok_peek(Parser* parent) {
    return tok_at(parent, parent->pos, parent->pos == parent->whitespace_end && parent->whitespace == WHITESPACE_NEWLINE);
}

static Tok tok_next(Parser* parent, Tok t) {
    return tok_at(parent, t.end, false);
}

static bool tok_is(Tok t, const char* text) {
    size_t n = strlen(text);
    return (size_t)(t.end - t.start) == n && memcmp(t.start, text, n) == 0;
}

static bool tok_is_eof(Tok t) {
    return !*t.start;
}

// tsc's tokenIsIdentifierOrKeyword, which private names (`#x`) pass too
static bool tok_is_word(Tok t) {
    // (A lone `#` is a private name too, with TS1127: see hop_private_name())
    return identifier_char_size(t.start, true) || (*t.start == '#' && t.start[1] != '!');
}

static bool tok_is_one_of(Tok t, const char* const* words) {
    for (; *words; words++) {
        if (tok_is(t, *words)) return true;
    }
    return false;
}

static bool is_reserved_word(csview word);

// tsc's isIdentifier (reserved words and private names aren't; `yield`/`await` are, which only matters to the
// stricter predicates)
static bool tok_is_identifier(Tok t) {
    return identifier_char_size(t.start, true) && !is_reserved_word((csview){ t.start, t.end - t.start });
}

// Any keyword tsc's scanner knows, contextual ones included (tsc's textToKeywordObj)
static const char* const all_keywords[] = {
        "abstract", "accessor", "any", "as", "asserts", "assert", "bigint", "boolean", "break", "case", "catch",
        "class", "continue", "const", "debugger", "declare", "default", "delete", "do", "else", "enum", "export",
        "extends", "false", "finally", "for", "from", "function", "get", "if", "implements", "import", "in", "infer",
        "instanceof", "interface", "intrinsic", "is", "keyof", "let", "module", "namespace", "never", "new", "null",
        "number", "object", "package", "private", "protected", "public", "override", "out", "readonly", "require",
        "global", "return", "satisfies", "set", "static", "string", "super", "switch", "symbol", "this", "throw",
        "true", "try", "type", "typeof", "undefined", "unique", "unknown", "using", "var", "void", "while", "with",
        "yield", "async", "await", "of", nullptr,
};

// tsc's isKeyword
static bool tok_is_keyword(Tok t) {
    return tok_is_word(t) && tok_is_one_of(t, all_keywords);
}

static bool tok_is_modifier(Tok t) {
    static const char* const words[] = {
        "abstract", "accessor", "async", "const", "declare", "default", "export", "in", "public", "private",
        "protected", "readonly", "static", "out", "override", nullptr,
    };
    return tok_is_word(t) && tok_is_one_of(t, words);
}

static bool tok_is_class_member_modifier(Tok t) {
    static const char* const words[] = {
        "public", "private", "protected", "readonly", "static", "override", "accessor", nullptr,
    };
    return tok_is_word(t) && tok_is_one_of(t, words);
}

static bool tok_is_string(Tok t) {
    return *t.start == '"' || *t.start == '\'';
}

static bool tok_is_number(Tok t) {
    return (*t.start >= '0' && *t.start <= '9') || (*t.start == '.' && t.start[1] >= '0' && t.start[1] <= '9');
}

static bool tok_is_template(Tok t) {
    return *t.start == '`';
}

// tsc's isLiteralPropertyName
static bool tok_is_literal_property_name(Tok t) {
    return tok_is_word(t) || tok_is_string(t) || tok_is_number(t);
}

// tsc's canParseSemicolon at t
static bool tok_can_parse_semicolon(Tok t) {
    return tok_is(t, ";") || tok_is(t, "}") || tok_is_eof(t) || t.newline_before;
}

// Has a binary operator precedence in tsc (getBinaryOperatorPrecedence > 0): not assignments, `,`, `?`, `=>`
static bool tok_is_binary_operator(Tok t) {
    static const char* const ops[] = {
        "??", "||", "&&", "|", "^", "&", "==", "!=", "===", "!==", "<", ">", "<=", ">=", "<<", ">>", ">>>", "+", "-",
        "*", "/", "%", "**", nullptr,
    };
    static const char* const words[] = { "instanceof", "in", "as", "satisfies", nullptr };
    return tok_is_word(t) ? tok_is_one_of(t, words) : tok_is_one_of(t, ops);
}

static bool tok_is_start_of_left_hand_side_expression(Parser* p, Tok t) {
    static const char* const words[] = { "this", "super", "null", "true", "false", "function", "class", "new", nullptr };
    if (tok_is_number(t) || tok_is_string(t) || tok_is_template(t)) return true;
    switch (*t.start) {
        case '(': case '[': case '{': case '/': return true;
        default: break;
    }
    if (tok_is(t, "import")) {
        Tok next = tok_next(p, t);
        return tok_is(next, "(") || tok_is(next, "<") || tok_is(next, ".");
    }
    return tok_is_one_of(t, words) || tok_is_identifier(t);
}

// tsc's isStartOfExpression
static bool tok_is_start_of_expression(Parser* p, Tok t) {
    static const char* const ops[] = { "+", "-", "~", "!", "++", "--", "<", "@", nullptr };
    static const char* const words[] = { "delete", "typeof", "void", "await", "yield", nullptr };
    if (tok_is_start_of_left_hand_side_expression(p, t)) return true;
    if (*t.start == '#') return true;
    return tok_is_one_of(t, ops) || tok_is_one_of(t, words) || tok_is_binary_operator(t) || tok_is_identifier(t);
}

// tsc's isDeclaration (a lookahead from t)
static bool tok_is_start_of_declaration(Parser* p, Tok t) {
    static const char* const declarations[] = { "var", "let", "const", "function", "class", "enum", nullptr };
    static const char* const modifiers[] = {
        "abstract", "accessor", "async", "declare", "private", "protected", "public", "readonly", nullptr,
    };
    while (true) {
        if (!tok_is_word(t)) return false;
        if (tok_is_one_of(t, declarations)) return true;
        if (tok_is(t, "using") || tok_is(t, "await")) {
            Tok next = tok_next(p, t);
            if (tok_is(t, "await")) {
                if (!tok_is(next, "using") || next.newline_before) return false;
                next = tok_next(p, next);
            }
            return !next.newline_before && (tok_is(next, "{") || tok_is_identifier(next));
        }
        if (tok_is(t, "interface") || tok_is(t, "type")) {
            Tok next = tok_next(p, t);
            return !next.newline_before && tok_is_identifier(next);
        }
        if (tok_is(t, "module") || tok_is(t, "namespace")) {
            Tok next = tok_next(p, t);
            return !next.newline_before && (tok_is_identifier(next) || tok_is_string(next));
        }
        if (tok_is_one_of(t, modifiers)) {
            Tok next = tok_next(p, t);
            if (next.newline_before) return false;
            if (tok_is(t, "declare") && tok_is(next, "type")) return true;
            t = next;
            continue;
        }
        if (tok_is(t, "global")) {
            Tok next = tok_next(p, t);
            return tok_is(next, "{") || tok_is_identifier(next) || tok_is(next, "export");
        }
        if (tok_is(t, "import")) {
            Tok next = tok_next(p, t);
            return tok_is_string(next) || tok_is(next, "*") || tok_is(next, "{") || tok_is_word(next);
        }
        if (tok_is(t, "export")) {
            Tok next = tok_next(p, t);
            Tok after = tok_is(next, "type") ? tok_next(p, next) : next;
            if (tok_is(after, "=") || tok_is(after, "*") || tok_is(after, "{") || tok_is(after, "default") ||
                tok_is(after, "as") || tok_is(after, "@")
            ) {
                return true;
            }
            t = next;
            continue;
        }
        if (tok_is(t, "static")) {
            t = tok_next(p, t);
            continue;
        }
        return false;
    }
}

// tsc's isStartOfStatement
static bool tok_is_start_of_statement(Parser* p, Tok t) {
    static const char* const puncts[] = { "@", ";", "{", nullptr };
    static const char* const words[] = {
        "var", "let", "using", "function", "class", "enum", "if", "do", "while", "for", "continue", "break", "return",
        "with", "switch", "throw", "try", "debugger", "catch", "finally",
        "async", "declare", "interface", "module", "namespace", "type", "global", nullptr,
    };
    static const char* const member_modifiers[] = {
        "accessor", "public", "private", "protected", "static", "readonly", nullptr,
    };
    if (tok_is_one_of(t, puncts)) return true;
    if (!tok_is_word(t)) return tok_is_start_of_expression(p, t);
    if (tok_is_one_of(t, words)) return true;
    if (tok_is(t, "import")) {
        if (tok_is_start_of_declaration(p, t)) return true;
        Tok next = tok_next(p, t);
        return tok_is(next, "(") || tok_is(next, "<") || tok_is(next, ".");
    }
    if (tok_is(t, "const") || tok_is(t, "export")) return tok_is_start_of_declaration(p, t);
    if (tok_is_one_of(t, member_modifiers)) {
        if (tok_is_start_of_declaration(p, t)) return true;
        Tok next = tok_next(p, t);
        return !(tok_is_word(next) && !next.newline_before);
    }
    return tok_is_start_of_expression(p, t);
}

// tsc's isClassMemberStart
static bool tok_is_class_member_start(Parser* p, Tok t) {
    if (tok_is(t, "@")) return true;
    Tok id = {0};
    while (tok_is_modifier(t)) {
        if (tok_is_class_member_modifier(t)) return true;
        id = t;
        t = tok_next(p, t);
    }
    if (tok_is(t, "*")) return true;
    if (tok_is_literal_property_name(t)) {
        id = t;
        t = tok_next(p, t);
    }
    if (tok_is(t, "[")) return true;
    if (id.start) {
        if (!tok_is_keyword(id) || tok_is(id, "set") || tok_is(id, "get")) return true;
        static const char* const follows[] = { "(", "<", "!", ":", "=", "?", nullptr };
        return tok_is_one_of(t, follows) || tok_can_parse_semicolon(t);
    }
    return false;
}

// tsc's isTypeMemberStart
static bool tok_is_type_member_start(Parser* p, Tok t) {
    if (tok_is(t, "(") || tok_is(t, "<") || tok_is(t, "get") || tok_is(t, "set")) return true;
    bool id = false;
    while (tok_is_modifier(t)) {
        id = true;
        t = tok_next(p, t);
    }
    if (tok_is(t, "[")) return true;
    if (tok_is_literal_property_name(t)) {
        id = true;
        t = tok_next(p, t);
    }
    static const char* const follows[] = { "(", "<", "?", ":", ",", nullptr };
    return id && (tok_is_one_of(t, follows) || tok_can_parse_semicolon(t));
}

// Every list being parsed is a bit of ParserShared.parsing_context (tsc's ParsingContext, same names). When an element
// can't be parsed, the list reports its error and then gives up if the current token belongs to any list being parsed
// (it starts an element or ends the list), else skips one token and goes on (tsc's abortParsingListOrMoveToNextToken).
// The mask only matters to recovery: on valid code no list ever consults it
typedef enum ParsingContext {
    PARSING_SOURCE_ELEMENTS,
    PARSING_BLOCK_STATEMENTS,
    PARSING_SWITCH_CLAUSES,
    PARSING_SWITCH_CLAUSE_STATEMENTS,
    PARSING_TYPE_MEMBERS,
    PARSING_CLASS_MEMBERS,
    PARSING_ENUM_MEMBERS,
    PARSING_OBJECT_LITERAL_MEMBERS,
    PARSING_PARAMETERS,
    PARSING_ARGUMENT_EXPRESSIONS,
    PARSING_ARRAY_LITERAL_MEMBERS,
    PARSING_VARIABLE_DECLARATIONS,
    PARSING_HERITAGE_CLAUSES,
    PARSING_HERITAGE_CLAUSE_ELEMENT,
    PARSING_CONTEXT_COUNT,
} ParsingContext;

static bool parse_list_member(Parser* parent, ParsingContext ctx, bool is_type, bool is_abstract, ClassItem* ret);

// tsc's isStartOfType(inStartOfParameter = true)
static bool tok_is_start_of_parameter_type(Tok t) {
    static const char* const words[] = {
        "any", "unknown", "string", "number", "bigint", "boolean", "readonly", "symbol", "unique", "void",
        "undefined", "null", "this", "typeof", "never", "new", "true", "false", "object", "infer", "import", "asserts",
        nullptr,
    };
    static const char* const puncts[] = { "{", "[", "<", "|", "&", "*", "?", "!", "...", nullptr };
    return tok_is_string(t) || tok_is_number(t) || tok_is_template(t) || tok_is_one_of(t, puncts) ||
        tok_is_one_of(t, words) || tok_is_identifier(t);
}

// tsc's isStartOfParameter
static bool tok_is_start_of_parameter(Tok t) {
    return tok_is(t, "...") || tok_is(t, "{") || tok_is(t, "[") || tok_is(t, "@") || tok_is_modifier(t) ||
        (*t.start == '#' && tok_is_word(t)) || tok_is_identifier(t) || tok_is_start_of_parameter_type(t);
}

// tsc's isListElement(HeritageClauseElement): an expression, but `{}` only if it can't be the body, and not the keyword
// of the next clause (`extends A implements B`)
static bool tok_is_heritage_clause_element(Parser* p, Tok t, bool in_error_recovery) {
    if (tok_is(t, "{")) {
        // tsc's isValidHeritageClauseObjectLiteral: `extends {} {`, `extends {},`, `extends {} extends`...
        Tok next = tok_next(p, t);
        if (!tok_is(next, "}")) return true;
        Tok after = tok_next(p, next);
        return tok_is(after, ",") || tok_is(after, "{") || tok_is(after, "extends") || tok_is(after, "implements");
    }
    if ((tok_is(t, "extends") || tok_is(t, "implements")) && tok_is_start_of_expression(p, tok_next(p, t))) {
        return false;
    }
    return in_error_recovery ? tok_is_identifier(t) : tok_is_start_of_left_hand_side_expression(p, t);
}

// tsc's isListElement with inErrorRecovery (the stricter form, which is all the abort rule asks)
static bool list_is_element(Parser* p, ParsingContext ctx, Tok t) {
    switch (ctx) {
        case PARSING_SOURCE_ELEMENTS:
        case PARSING_BLOCK_STATEMENTS:
        case PARSING_SWITCH_CLAUSE_STATEMENTS:
            return !tok_is(t, ";") && tok_is_start_of_statement(p, t);
        case PARSING_SWITCH_CLAUSES:
            return tok_is(t, "case") || tok_is(t, "default");
        case PARSING_TYPE_MEMBERS:
            // `{ -readonly [K in T]: X }` is a mapped type, which tsc parses before it gets to any list
            return tok_is_type_member_start(p, t) ||
                ((tok_is(t, "-") || tok_is(t, "+")) && tok_is(tok_next(p, t), "readonly"));
        case PARSING_CLASS_MEMBERS:
            return tok_is_class_member_start(p, t);
        case PARSING_ENUM_MEMBERS:
            return tok_is(t, "[") || tok_is_literal_property_name(t);
        case PARSING_OBJECT_LITERAL_MEMBERS:
            return tok_is(t, "[") || tok_is(t, "*") || tok_is(t, "...") || tok_is(t, ".") ||
                tok_is_literal_property_name(t);
        case PARSING_PARAMETERS:
            return tok_is_start_of_parameter(t);
        case PARSING_ARRAY_LITERAL_MEMBERS:
            if (tok_is(t, ",") || tok_is(t, ".")) return true;
            return tok_is(t, "...") || tok_is_start_of_expression(p, t);
        case PARSING_ARGUMENT_EXPRESSIONS:
            return tok_is(t, "...") || tok_is_start_of_expression(p, t);
        case PARSING_VARIABLE_DECLARATIONS:
            return tok_is(t, "{") || tok_is(t, "[") || (*t.start == '#' && tok_is_word(t)) || tok_is_identifier(t);
        case PARSING_HERITAGE_CLAUSES:
            return tok_is(t, "extends") || tok_is(t, "implements");
        case PARSING_HERITAGE_CLAUSE_ELEMENT:
            return tok_is_heritage_clause_element(p, t, true);
        case PARSING_CONTEXT_COUNT: break;
    }
    impossible;
}

// tsc's isListTerminator
static bool list_is_terminator(ParsingContext ctx, Tok t) {
    if (tok_is_eof(t)) return true;
    switch (ctx) {
        case PARSING_SOURCE_ELEMENTS:
            return false;
        case PARSING_BLOCK_STATEMENTS:
        case PARSING_SWITCH_CLAUSES:
        case PARSING_TYPE_MEMBERS:
        case PARSING_CLASS_MEMBERS:
        case PARSING_ENUM_MEMBERS:
        case PARSING_OBJECT_LITERAL_MEMBERS:
            return tok_is(t, "}");
        case PARSING_SWITCH_CLAUSE_STATEMENTS:
            return tok_is(t, "}") || tok_is(t, "case") || tok_is(t, "default");
        case PARSING_PARAMETERS:
            return tok_is(t, ")") || tok_is(t, "]");
        case PARSING_ARGUMENT_EXPRESSIONS:
            return tok_is(t, ")") || tok_is(t, ";");
        case PARSING_ARRAY_LITERAL_MEMBERS:
            return tok_is(t, "]");
        case PARSING_VARIABLE_DECLARATIONS:
            // tsc's isVariableDeclaratorListTerminator
            return tok_can_parse_semicolon(t) || tok_is(t, "in") || tok_is(t, "of") || tok_is(t, "=>");
        case PARSING_HERITAGE_CLAUSES:
            return tok_is(t, "{") || tok_is(t, "}");
        case PARSING_HERITAGE_CLAUSE_ELEMENT:
            return tok_is(t, "{") || tok_is(t, "extends") || tok_is(t, "implements");
        case PARSING_CONTEXT_COUNT: break;
    }
    impossible;
}

// tsc's parsingContextErrors: reported at the current token
static void list_report_error(Parser* p, ParsingContext ctx, Tok t) {
    Span span = span_make(p->shared->source, t.start, t.end);
    switch (ctx) {
        case PARSING_SOURCE_ELEMENTS:
            if (tok_is(t, "default")) {
                parse_error(p, span, 1005, "'export' expected.");
            } else {
                parse_error(p, span, 1128, "Declaration or statement expected.");
            }
            break;
        case PARSING_BLOCK_STATEMENTS:
            parse_error(p, span, 1128, "Declaration or statement expected.");
            break;
        case PARSING_SWITCH_CLAUSES:
            parse_error(p, span, 1130, "'case' or 'default' expected.");
            break;
        case PARSING_SWITCH_CLAUSE_STATEMENTS:
            parse_error(p, span, 1129, "Statement expected.");
            break;
        case PARSING_TYPE_MEMBERS:
            parse_error(p, span, 1131, "Property or signature expected.");
            break;
        case PARSING_CLASS_MEMBERS:
            parse_error(p, span, 1068, "Unexpected token. A constructor, method, accessor, or property was expected.");
            break;
        case PARSING_ENUM_MEMBERS:
            parse_error(p, span, 1132, "Enum member expected.");
            break;
        case PARSING_OBJECT_LITERAL_MEMBERS:
            parse_error(p, span, 1136, "Property assignment expected.");
            break;
        case PARSING_PARAMETERS:
            if (tok_is_keyword(t)) {
                parse_error(p, span, 1390, "'", span, "' is not allowed as a parameter name.");
            } else {
                parse_error(p, span, 1138, "Parameter declaration expected.");
            }
            break;
        case PARSING_ARGUMENT_EXPRESSIONS:
            parse_error(p, span, 1135, "Argument expression expected.");
            break;
        case PARSING_ARRAY_LITERAL_MEMBERS:
            parse_error(p, span, 1137, "Expression or comma expected.");
            break;
        case PARSING_VARIABLE_DECLARATIONS:
            if (tok_is_keyword(t)) {
                parse_error(p, span, 1389, "'", span, "' is not allowed as a variable declaration name.");
            } else {
                parse_error(p, span, 1134, "Variable declaration expected.");
            }
            break;
        case PARSING_HERITAGE_CLAUSES:
            parse_error(p, span, 1179, "Unexpected token. '{' expected.");
            break;
        case PARSING_HERITAGE_CLAUSE_ELEMENT:
            parse_error(p, span, 1109, "Expression expected.");
            break;
        case PARSING_CONTEXT_COUNT: impossible;
    }
}

// Enters a list: returns the mask to restore with list_leave()
static u32 list_enter(Parser* p, ParsingContext ctx) {
    u32 saved = p->shared->parsing_context;
    p->shared->parsing_context |= 1u << ctx;
    return saved;
}

static void list_leave(Parser* p, u32 saved) {
    p->shared->parsing_context = saved;
}

// The current token can't start an element of the ctx list: reports it, then returns true if the list should end (the
// token belongs to some list being parsed), else skips the token and returns false (tsc's
// abortParsingListOrMoveToNextToken). Whitespace is hopped either way
static bool list_abort_or_skip(Parser* p, ParsingContext ctx) {
    hop_whitespace(p);
    Tok t = tok_peek(p);
    list_report_error(p, ctx, t);
    // Something that starts an element of this very list is skipped: the element parser just said no to it, where
    // tsc's would have committed and consumed it with errors
    if (list_is_terminator(ctx, t)) return true;
    if (!list_is_element(p, ctx, t)) {
        u32 mask = p->shared->parsing_context & ~(1u << ctx);
        for (u32 i = 0; i < PARSING_CONTEXT_COUNT; i++) {
            if ((mask & (1u << i)) &&
                (list_is_element(p, (ParsingContext)i, t) || list_is_terminator((ParsingContext)i, t))
            ) {
                return true;
            }
        }
    }
    p->pos = t.end;
    hop_whitespace(p);
    return false;
}

// tsc's nextTokenCanFollowDefaultKeyword, at the token after `default`
static bool can_follow_default_keyword(Parser* p) {
    if (*p->pos == '@') return true;
    if (starts_with_followed_by_word_break(p, "class") || starts_with_followed_by_word_break(p, "function") ||
        starts_with_followed_by_word_break(p, "interface")
    ) {
        return true;
    }
    Tok t = tok_peek(p);
    if (tok_is(t, "abstract")) return tok_is(tok_next(p, t), "class") && !tok_next(p, t).newline_before;
    if (tok_is(t, "async")) return tok_is(tok_next(p, t), "function") && !tok_next(p, t).newline_before;
    return false;
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
        // Only `static`, `export` and `default` can be followed by a line break (tsc's nextTokenCanFollowModifier):
        // `static\n x` is static, `export default\n@dec class C {}` a default export
        if (p.whitespace == WHITESPACE_NEWLINE &&
            !(is_member && span_equals(mod.span, "static")) &&
            !span_equals(mod.span, "export") &&
            !(prev_was_export && span_equals(mod.span, "default"))
        ) {
            goto loop_end;
        }

        // `export default` is followed by any expression: `-1`, `` `a` ``, `!x`
        bool export_default = prev_was_export && !is_member && span_equals(mod.span, "default");
        if (!is_identifier_char(*p.pos) && !export_default) {
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

                case '.': // `constructor(public ...rest)`
                    if (p.pos[1] != '.' || p.pos[2] != '.') goto loop_end;
                    break;

                case '{': // `constructor(public { x })`, `export {}`; `static {` is a static block
                    if (is_member && span_equals(mod.span, "static")) goto loop_end;
                    break;

                case '(':
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
        } else if (span_equals(mod.span, "abstract")) {
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
            // tsc's canFollowExportModifier: `export {`, `export *`, `export as namespace` are export declarations
            if (!is_member && (*p.pos == '{' || *p.pos == '*' || starts_with_followed_by_word_break(&p, "as"))) break;
            mod.tag = MODIFIER_EXPORT;
        } else if (span_equals(mod.span, "default") && (prev_was_export || (!is_member && can_follow_default_keyword(&p)))) {
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
        } else if (is_member && span_equals(mod.span, "in")) {
            mod.tag = MODIFIER_IN; // (Only on a type parameter: TS1274)
        } else if (is_member && span_equals(mod.span, "out")) {
            mod.tag = MODIFIER_OUT;
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

// TypeScript-only modifiers in JavaScript (tsc's getJSSyntacticDiagnosticsForFile: checkModifiers, and every modifier
// of a class property but `static` and `accessor`, TS8012 for a parameter's)
static void check_js_modifiers(Parser* parent, ModifierList const* me, ModifiedNode node) {
    if (!reports_js_syntax(parent) || node.parent == MODIFIED_IN_TYPE) return;
    switch (node.kind) {
        case MODIFIED_PARAMETER: {
            Span first = vec_spanned_modifier_front(&me->spanned)->span;
            Span last = vec_spanned_modifier_back(&me->spanned)->span;
            js_syntax_error(parent, span_between(first, last), 8012, "Parameter modifiers can only be used in TypeScript files.");
            return;
        }
        case MODIFIED_PROPERTY:
        case MODIFIED_METHOD:
        case MODIFIED_GET_ACCESSOR:
        case MODIFIED_SET_ACCESSOR:
        case MODIFIED_CONSTRUCTOR:
        case MODIFIED_VARIABLE_STATEMENT:
        case MODIFIED_FUNCTION:
        case MODIFIED_CLASS:
            break;
        default:
            return;
    }
    for_each (i, vec_spanned_modifier, &me->spanned) {
        bool reported;
        if (node.kind == MODIFIED_PROPERTY) {
            reported = i->tag != MODIFIER_STATIC && i->tag != MODIFIER_ACCESSOR;
        } else {
            switch (i->tag) {
                case MODIFIER_CONST: reported = node.kind != MODIFIED_VARIABLE_STATEMENT; break;
                case MODIFIER_PUBLIC:
                case MODIFIER_PRIVATE:
                case MODIFIER_PROTECTED:
                case MODIFIER_READONLY:
                case MODIFIER_DECLARE:
                case MODIFIER_ABSTRACT:
                case MODIFIER_OVERRIDE:
                    reported = true;
                    break;
                default: reported = false; break;
            }
        }
        if (reported) {
            js_syntax_error(parent, i->span, 8009, "The '", i->span, "' modifier can only be used in TypeScript files.");
        }
    }
}

static bool check_modifiers(Parser* parent, ModifierList const* me, ModifiedNode node) {
    if (vec_spanned_modifier_is_empty(&me->spanned)) return false;
    check_js_modifiers(parent, me, node);
    node.parent_ambient |= parent->shared->in_ambient;
    ErrorCtx errs = { .phase = ERROR_PHASE_PARSE };
    bool reported = modifier_list_check_grammar(me, &node, &errs);
    for_each (i, vec_error, &errs.errors) {
        add_error_to_parser(parent, *i);
        *i = (Error){0};
    }
    error_ctx_drop(&errs);
    return reported;
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

static bool parse_generic_decl_item(Parser* parent, TypeParamOwner owner, GenericItem* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    // Modifiers (tsc's parseModifiers with permitConstAsModifier): `const T`, `in T`, `out T`, `in out T`, and the
    // others, which the grammar check rejects. A modifier keyword is the name when nothing that can be a name follows
    // it on the same line
    static const char* const words[] = {
        "abstract", "accessor", "async", "const", "declare", "export", "in", "public", "private", "protected",
        "readonly", "static", "out", "override", nullptr,
    };
    ModifierList modifiers = {0};
    while (true) {
        Parser modifier_p = parser(&p);
        Span word;
        bool is_modifier = parse_identifier_or_keyword(&modifier_p, &word);
        if (is_modifier) {
            is_modifier = false;
            for (const char* const* w = words; *w && !is_modifier; w++) is_modifier = csview_equals(word.sv, *w);
        }
        if (is_modifier) {
            hop_whitespace(&modifier_p);
            is_modifier = (modifier_p.whitespace != WHITESPACE_NEWLINE || span_equals(word, "static")) &&
                identifier_char_size(modifier_p.pos, true);
        }
        if (!is_modifier) {
            parse_fail(&modifier_p);
            break;
        }
        SpannedModifier mod = { .span = word };
        mod.tag = span_equals(word, "in") ? MODIFIER_IN : span_equals(word, "out") ? MODIFIER_OUT :
            span_equals(word, "const") ? MODIFIER_CONST : span_equals(word, "readonly") ? MODIFIER_READONLY :
            span_equals(word, "abstract") ? MODIFIER_ABSTRACT : span_equals(word, "static") ? MODIFIER_STATIC :
            span_equals(word, "accessor") ? MODIFIER_ACCESSOR : span_equals(word, "export") ? MODIFIER_EXPORT :
            span_equals(word, "async") ? MODIFIER_ASYNC : span_equals(word, "declare") ? MODIFIER_DECLARE :
            span_equals(word, "override") ? MODIFIER_OVERRIDE : span_equals(word, "private") ? MODIFIER_PRIVATE :
            span_equals(word, "protected") ? MODIFIER_PROTECTED : MODIFIER_PUBLIC;
        vec_spanned_modifier_push(&modifiers.spanned, mod);
        modifiers.includes[mod.tag] = true;
        parse_success(&modifier_p);
    }
    bool is_const = modifiers.includes[MODIFIER_CONST];
    bool is_in = modifiers.includes[MODIFIER_IN];
    bool is_out = modifiers.includes[MODIFIER_OUT];
    check_modifiers(&p, &modifiers, (ModifiedNode){ .kind = MODIFIED_TYPE_PARAMETER, .parent = MODIFIED_IN_OTHER,
        .type_param_owner = owner });
    modifier_list_drop(&modifiers);

    Span name;
    if (!parse_identifier(&p, &name)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    TypeNode extends = type_any;
    bool extends_explicit = false;
    Span extends_span = span_null;
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        extends_explicit = true;
        hop_whitespace(&p);
        Anchor extends_a = anchor(&p);
        parse_type_or_set_to_default(&p, TYPE_PRECEDENCE_NULL, &extends);
        extends_span = span_from(extends_a);
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
        .is_in = is_in,
        .is_out = is_out,
        .extends_explicit = extends_explicit,
        .extends = extends,
        .extends_span = extends_span,
        .fallback_explicit = fallback_explicit,
        .fallback = fallback,
    };
    return parse_success(&p);
}

static bool parse_generic_decl(Parser* parent, TypeParamOwner owner, Generic* ret) {
    Parser p = parser(parent);
    *ret = (Generic){0};

    if (!hop_char(&p, '<')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    while (*p.pos && *p.pos != '>') {
        GenericItem item;
        if (parse_generic_decl_item(&p, owner, &item)) {
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

    // (tsc's checkGrammarTypeArguments, which doesn't see JSDoc)
    if (vec_spanned_type_is_empty(ret) && !p.shared->in_jsdoc) {
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

        char const* type_start = p.pos;
        expect_type(&p, TYPE_PRECEDENCE_NULL, &elem.type);
        // JSDoc `[T?]`: the postfix `?` that would make `T | null` marks an optional element instead
        if (!named && p.shared->in_jsdoc && p.shared->jsdoc_postfix_nullable_start == type_start &&
            p.shared->jsdoc_postfix_nullable_end == p.pos && elem.type.tag == TYPE_UNION) {
            TypeNode null_type = *vec_type_back(&elem.type.composite);
            check(null_type.synthesized);
            vec_type_pop(&elem.type.composite);
            if (vec_type_size(&elem.type.composite) == 1) {
                TypeNode inner = *vec_type_front(&elem.type.composite);
                elem.type.composite.size = 0;
                type_drop(&elem.type);
                elem.type = inner;
            }
            elem.optional = true;
        }
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

// `T | null` of `?T`, `T | undefined` of `T=`: a union of its own, not merged with a union T (tsc's JSDocNullableType
// and JSDocOptionalType, which print as a parenthesized union in a union)
static TypeNode jsdoc_union(TypeNode type, TypeNode added) {
    TypeNode ret = { .tag = TYPE_UNION, .parsed = true, .composite = vec_type_init() };
    vec_type_push(&ret.composite, type);
    vec_type_push(&ret.composite, added);
    return ret;
}

// The names tsc gives the parameters of a JSDoc function type (`arg0`...), which aren't written anywhere
static Span jsdoc_parameter_name(SourceFile const* source, u32 index) {
    static const char* const names[] = {
        "arg0", "arg1", "arg2", "arg3", "arg4", "arg5", "arg6", "arg7", "arg8", "arg9", "arg10", "arg11", "arg12",
        "arg13", "arg14", "arg15",
    };
    return (Span){ .source = source, .sv = csview_from(names[index < c_arraylen(names) ? index : c_arraylen(names) - 1]) };
}

// `function(string, number=, ...boolean): R`, `function(this: T, ...)`, `function(new: T, ...)` (tsc's
// parseJSDocFunctionType): p is past `function`
static bool parse_jsdoc_function_type(Parser* parent, Anchor a, TypeNode* ret) {
    Parser p = parser(parent);
    hop_whitespace(&p);
    if (!hop_char(&p, '(')) return parse_fail(&p);
    FunctionImpl function = { .args = vec_function_arg_init(), .jsdoc_type = true };
    Anchor args_start = { .parser = &p, .start = p.pos - 1 };
    hop_whitespace(&p);
    u32 index = 0;
    while (*p.pos && *p.pos != ')') {
        Anchor arg_start = anchor(&p);
        FunctionArg arg = { .initializer.value = expr_implicit_any(span_from_pos(&p)) };
        Span name = span_null;
        bool is_new = false;
        Parser word = parser(&p);
        Anchor word_a = anchor(&word);
        if ((hop_start_if_ends_on_word_break(&word, "this") || (is_new = hop_start_if_ends_on_word_break(&word, "new"))) &&
            (hop_whitespace(&word), hop_char(&word, ':'))
        ) {
            name = span_make(p.shared->source, word_a.start, word_a.start + (is_new ? 3 : 4));
            parse_success(&word);
            hop_whitespace(&p);
        } else {
            parse_fail(&word);
            is_new = false;
        }
        if (!span_valid(name) && hop_start(&p, "...")) {
            arg.rest = true;
            arg.initializer.is_rest_parameter = true;
            hop_whitespace(&p);
        }
        TypeNode type;
        if (!parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
            vec_function_arg_drop(&function.args);
            return parse_fail(&p);
        }
        if (!is_new) hop_jsdoc_optional(&p, &type);
        if (is_new) {
            // The type the construct signature makes
            function.is_constructor = true;
            function.ret = type;
            function.ret_explicit = true;
        } else {
            if (type.jsdoc_optional) arg.explicit_optional = true;
            if (arg.rest) type = type_array(c_new(TypeNode, type), false), type.parsed = true;
            arg.initializer.binding = (Binding){
                .tag = BINDING_IDENTIFIER,
                .content_span = span_valid(name) ? name : jsdoc_parameter_name(p.shared->source, index),
                .has_explicit_type = true,
                .explicit_type = type,
            };
            arg.span = span_from(arg_start);
            vec_function_arg_push(&function.args, arg);
        }
        index++;
        hop_whitespace(&p);
        if (!hop_char(&p, ',')) break;
        hop_whitespace(&p);
    }
    if (!hop_char(&p, ')')) {
        vec_function_arg_drop(&function.args);
        return parse_fail(&p);
    }
    function.args_span = span_from(args_start);
    Parser colon = parser(&p);
    hop_whitespace(&colon);
    if (hop_char(&colon, ':')) {
        hop_whitespace(&colon);
        Anchor ret_a = anchor(&colon);
        TypeNode written;
        if (parse_type(&colon, TYPE_PRECEDENCE_NULL, &written)) {
            // (A construct signature's is what `new:` says, the written one is ignored)
            if (function.is_constructor) {
                type_drop(&written);
            } else {
                function.ret = written;
                function.ret_span = span_from(ret_a);
                function.ret_explicit = true;
            }
            parse_success(&colon);
        } else {
            parse_fail(&colon);
        }
    } else {
        parse_fail(&colon);
    }
    function.full_span = span_make(p.shared->source, a.start, p.pos); // (`a` is the caller's, before `function`)
    *ret = function_impl_into_type(function);
    return parse_success(&p);
}

// JSDoc's type forms that come first: `*` (any), `?` alone (any), `?T` (T | null), `!T` (T), `function(...)`
static bool parse_jsdoc_prefix_type(Parser* parent, TypeNode* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    if (hop_start_if_ends_on_word_break(&p, "function") && parse_jsdoc_function_type(&p, a, ret)) {
        return parse_success(&p);
    }
    p.pos = a.start;
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
        *ret = jsdoc_union(inner, jsdoc_synthesized(type_null));
        return parse_success(&p);
    }
    if (hop_char(&p, '!')) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_OPERATOR, ret);
        return parse_success(&p);
    }
    return parse_fail(&p);
}

// JSDoc's `T=` (optional: T | undefined), which only ends a whole JSDoc type or a parameter of `function()` (tsc's
// parseJSDocType): `(a: A) => R=` is an optional function, not one returning `R | undefined`
static void hop_jsdoc_optional(Parser* parent, TypeNode* type) {
    Parser p = parser(parent);
    hop_whitespace(&p);
    if (p.pos[0] != '=' || p.pos[1] == '>' || p.pos[1] == '=') {
        parse_fail(&p);
        return;
    }
    p.pos++;
    *type = jsdoc_union(*type, jsdoc_synthesized(type_undefined));
    type->jsdoc_optional = true;
    type->parsed = true;
    parse_success(&p);
}

// JSDoc's postfix type forms: `T?` (T | null), `T!` (T)
static bool parse_jsdoc_postfix_type(Parser* parent, char const* left_start, TypeNode* left_and_ret) {
    Parser p = parser(parent);
    if (hop_char(&p, '!')) {
        return parse_success(&p);
    }
    if (hop_char(&p, '?')) {
        Parser next = parser(&p);
        hop_whitespace(&next);
        bool type_follows = is_type_start_char(*next.pos) && !(next.pos[0] == '*' && next.pos[1] == '/');
        parse_fail(&next);
        if (!type_follows) {
            *left_and_ret = jsdoc_union(*left_and_ret, jsdoc_synthesized(type_null));
            p.shared->jsdoc_postfix_nullable_start = left_start;
            p.shared->jsdoc_postfix_nullable_end = p.pos;
            return parse_success(&p);
        }
    }
    return parse_fail(&p);
}

// `left_start`: where the type on the left began (the span of `T[K]`)
static bool parse_type_right_side(Parser* parent, TypePrecedence min_precedence, char const* left_start, TypeNode* left_and_ret) {
    Parser p = parser(parent);
    TypeNode type;

    hop_whitespace(&p);

    if (p.shared->in_jsdoc && parse_jsdoc_postfix_type(&p, left_start, left_and_ret)) {
        return parse_success(&p);
    }

    if (p.whitespace != WHITESPACE_NEWLINE && hop_char(&p, '[')) {
        hop_whitespace(&p);
        Anchor index_anchor = anchor(&p);
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
            Span index_span = span_from(index_anchor);
            hop_whitespace(&p);
            expect_char(&p, ']');
            *left_and_ret = type_indexed(c_new(IndexedType, {
                .interface = *left_and_ret,
                .index = type,
                .index_span = index_span,
                .span = span_make(p.shared->source, left_start, p.pos),
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
    Span full_span = span_from(a);
    hop_whitespace(&p);

    TypeNode extends;
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        hop_whitespace(&p);
        if (expect_type(&p, TYPE_PRECEDENCE_NULL, &extends)) {
        }
        full_span = span_from(a);
    } else {
        extends = type_any;
    }

    *ret = (InferredType){
        .name = name,
        .full_span = full_span,
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
    identifier.jsdoc = p.shared->in_jsdoc;
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
    // (Any name: tsc's checkGrammarMetaProperty says TS17012 for another one)
    if (!parse_identifier_or_keyword(&p, nullptr)) return parse_fail(&p);
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

// A primitive type keyword not followed by `.` (tsc's parseKeywordAndNoDot): `string.x` names a namespace `string`
__attribute__((always_inline)) static inline bool hop_type_keyword(Parser* p, char const* keyword) {
    if (*p->pos != *keyword) return false;
    char const* start = p->pos;
    if (!hop_start_if_ends_on_word_break(p, keyword)) return false;
    char const* c = p->pos;
    while (*c == ' ' || *c == '\t') c++;
    // (Only a `.`, or line breaks and comments that may hide one, need the tokenizer)
    if ((*c == '.' || *c == '/' || *c == '\n' || *c == '\r' || (unsigned char)*c >= 0x80) && *tok_peek(p).start == '.') {
        p->pos = start;
        return false;
    }
    return true;
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
    char const* left_start = value_anchor.start;
    *ret = (TypeNode){0}; // (Some branches only fill in what they're about)
    // Every branch below needs a particular first character: test it before trying the branch
    char c = *p.pos;
    bool word = identifier_char_size(p.pos, true);
    if (p.shared->in_jsdoc && parse_jsdoc_prefix_type(&p, ret)) {
    } else if (hop_type_keyword(&p, "any")) {
        *ret = type_any;
    } else if (hop_type_keyword(&p, "unknown")) {
        *ret = type_unknown;
    } else if (hop_type_keyword(&p, "never")) {
        *ret = type_never;
    } else if (hop_start_if_ends_on_word_break(&p, "void")) {
        *ret = type_void;
    } else if (hop_type_keyword(&p, "boolean")) {
        *ret = type_boolean;
    } else if (hop_type_keyword(&p, "number")) {
        *ret = type_number;
    } else if (hop_type_keyword(&p, "string")) {
        *ret = type_string;
    } else if (hop_type_keyword(&p, "symbol")) {
        *ret = type_symbol;
    } else if (hop_type_keyword(&p, "bigint")) {
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
    } else if ((c == 't' || c == 'i') && parse_import_type(&p, ret)) {
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
    } else if (c == '`' && parse_template_type(&p, ret)) {
    } else if ((word || c == '"' || c == '\'' || c == '`' || is_number_start_char(c)) &&
        parse_type_compat_literal(&p, &ret->value)
    ) {
        ret->tag = TYPE_VALUE;
        ret->value_span = span_from(value_anchor);
    } else if (hop_char(&p, '-')) {
        hop_whitespace(&p);
        if (parse_numeric_value(&p, &ret->value)) {
            ret->tag = TYPE_VALUE;
            if (ret->value.tag == VALUE_NUMBER) ret->value.number = -ret->value.number;
            else ret->value.bigint = -ret->value.bigint;
            ret->value_span = span_from(value_anchor);
        } else {
            return parse_fail(&p);
        }
    } else if ((c == '(' || c == '<' || c == 'a' || c == 'n') &&
        parse_function_args(&p, true, true, true, false, &parsed.func)
    ) {
        parsed.func.full_span = span_from(value_anchor);
        *ret = function_impl_into_type(parsed.func);
    } else if (c == 'i' && parse_inferred_type(&p, &parsed.inferred)) {
        *ret = type_inferred(c_new(InferredType, parsed.inferred));
    } else if (c == 'u' && parse_unique_symbol_type(&p)) {
        *ret = (TypeNode){ .tag = TYPE_UNIQUE_SYMBOL };
    } else if (word && parse_identifier_type(&p, &parsed.identifier)) {
        parsed.identifier.jsdoc = p.shared->in_jsdoc;
        *ret = type_identifier(c_new(IdentifierType, parsed.identifier));
    } else if (c == '[' && parse_tuple(&p, &parsed.tuple_elements)) {
        *ret = type_tuple(parsed.tuple_elements, false);
    } else if (hop_char(&p, '(')) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, ret);
        hop_whitespace(&p);
        expect_char(&p, ')');
        parenthesized = true;
    } else if (c == '{' && parse_interface(&p, &ret->interface)) {
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
        while (parse_type_right_side(&p, TYPE_PRECEDENCE_OPERATOR, left_start, ret)) {}
        if (ret->tag == TYPE_ARRAY) {
            ret->array.readonly = true;
            readonly_span = span_null;
        } else if (ret->tag == TYPE_TUPLE) {
            ret->tuple.readonly = true;
            readonly_span = span_null;
        }
    }
    while (parse_type_right_side(&p, min_precedence, left_start, ret)) {}
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
    bool name_first; // `@param name {T}` or no type at all (tsc's isNameFirst)
    bool is_const; // `@template const T`
    bool is_in, is_out; // `@template in T`, `@template out T`
    // A `@param`/`@returns` of a `@callback`, a `@property` of a `@typedef`: part of that, not about the host
    bool nested;
    // `@returns {x is T}`, `{asserts x}`: owned, `type` is then the return type the predicate goes with
    TypePredicateNode* predicate;
    bool has_default;
    TypeNode default_type; // `@template [T=D]`, owned while has_default
    // From a JSDoc comment before the one right before the host: only its `@overload` tags (with their children and
    // the comment's `@template`s) are kept, nothing else about the host (tsc's filterOwnedJSDocTags)
    bool earlier;
    const char* comment; // The start of the comment the tag is in
} JsDocTag;

static void jsdoc_tag_drop(JsDocTag* me) {
    if (my has_type) type_drop(&my type);
    if (my has_default) type_drop(&my default_type);
    if (my predicate) {
        if (my predicate->has_type) type_drop(&my predicate->type);
        free(my predicate);
    }
}

static bool parse_type_predicate(Parser* parent, FunctionImpl* ret);

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

// Parses `{T}` at `*pos` (which must be the `{`) into `tag`, moves `*pos` past it. Its parse errors go to the JSDoc
// errors
static void parse_jsdoc_type_expression(Parser* parent, const char** pos, const char* end, JsDocTag* tag) {
    Parser p = parser(parent);
    p.pos = *pos + 1;
    p.whitespace_end = nullptr;
    bool outer_in_jsdoc = p.shared->in_jsdoc;
    const char* outer_end = p.shared->jsdoc_end;
    p.shared->in_jsdoc = true;
    p.shared->jsdoc_end = end;
    hop_whitespace(&p);
    Anchor module_start = anchor(&p);
    if (hop_start_if_ends_on_word_break(&p, "module")) {
        // `{module:foo/bar}` (tsc's parseJSDocType: a JSDocNamepathType, which is the error type): every token up to
        // a `}`, `,` or whitespace
        while (p.pos < end && *p.pos != '}' && *p.pos != ',' && !is_jsdoc_space(*p.pos)) p.pos++;
        if (tag->has_type) type_drop(&tag->type);
        tag->has_type = true;
        tag->type = type_any;
        tag->type_span = span_from(module_start);
        hop_whitespace(&p);
        if (p.pos < end && *p.pos == '}') p.pos++;
        *pos = p.pos;
        p.shared->in_jsdoc = outer_in_jsdoc;
        p.shared->jsdoc_end = outer_end;
        parse_fail(&p);
        return;
    }
    bool variadic = hop_start(&p, "...");
    Anchor a = anchor(&p);
    TypeNode type;
    FunctionImpl predicate = {0};
    if (tag->kind == JSDOC_TAG_RETURNS && parse_type_predicate(&p, &predicate)) {
        type = predicate.ret;
        if (tag->predicate) {
            if (tag->predicate->has_type) type_drop(&tag->predicate->type);
            free(tag->predicate);
        }
        tag->predicate = predicate.predicate;
        tag->predicate->jsdoc = true;
    }
    if (tag->predicate || parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
        if (!tag->predicate) hop_jsdoc_optional(&p, &type);
        Span type_span = span_from(a);
        hop_whitespace(&p);
        if (p.pos < end) {
            if (tag->has_type) type_drop(&tag->type);
            tag->has_type = true;
            tag->type = type;
            tag->type_span = type_span;
            tag->variadic = variadic;
            if (*p.pos == '}') p.pos++;
            else parse_error(&p, span_from_token(&p), 1005, "'}' expected.");
            *pos = p.pos;
        } else {
            type_drop(&type);
            parse_error(&p, span_make(p.shared->source, end, end), 1005, "'}' expected.");
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
    jsdoc_keep_errors(&p);
    parse_fail(&p);
}

// A type without braces at `*pos` (`@template [T=D]`'s default), moves `*pos` past it
static bool parse_jsdoc_bare_type(Parser* parent, const char** pos, const char* end, TypeNode* ret) {
    Parser p = parser(parent);
    p.pos = *pos;
    p.whitespace_end = nullptr;
    bool outer_in_jsdoc = p.shared->in_jsdoc;
    const char* outer_end = p.shared->jsdoc_end;
    p.shared->in_jsdoc = true;
    p.shared->jsdoc_end = end;
    hop_whitespace(&p);
    bool parsed = parse_type(&p, TYPE_PRECEDENCE_NULL, ret) && p.pos <= end;
    if (parsed) {
        *pos = p.pos;
    }
    p.shared->in_jsdoc = outer_in_jsdoc;
    p.shared->jsdoc_end = outer_end;
    parse_fail(&p);
    return parsed;
}

static int identifier_escape_size(const char* c);
static cstr jsx_name_value(csview name);

// A JSDoc name is a parameter's name, `\u0061` for `a` (tsc's scanner decodes escapes in identifiers)
static bool jsdoc_name_is(csview jsdoc_name, csview name) {
    if (!memchr(jsdoc_name.buf, '\\', (size_t)jsdoc_name.size)) return csview_equals_sv(jsdoc_name, name);
    cstr decoded = jsx_name_value(jsdoc_name);
    bool ret = csview_equals_sv(cstr_sv(&decoded), name);
    cstr_drop(&decoded);
    return ret;
}

static const char* jsdoc_parse_name(const char* c, const char* end, Span* ret, SourceFile const* source) {
    const char* start = c;
    while (c < end && (is_identifier_char(*c) || *c == '.' || (uint8_t)*c >= 0x80 ||
        (*c == '[' && c[1] == ']') || identifier_escape_size(c))
    ) {
        int escape = identifier_escape_size(c);
        c += escape ? escape : *c == '[' ? 2 : 1;
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
    tag->name_first = !tag->has_type;
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
            c = jsdoc_skip_space(c, end);
            if (tag.kind != JSDOC_TAG_CALLBACK && c < end && *c == '{') {
                parse_jsdoc_type_expression(parent, &c, end, &tag);
                c = jsdoc_skip_space(c, end);
            }
            c = jsdoc_parse_name(c, end, &tag.name, source);
            break;
        case JSDOC_TAG_TEMPLATE:
            // `@template {C} T, U`, `@template [T=D]` (tsc's parseTemplateTag): a tag per name, the constraint is the
            // first one's (getEffectiveConstraintOfTypeParameter). Defaults aren't read
            c = jsdoc_skip_space(c, end);
            if (c < end && *c == '{') {
                parse_jsdoc_type_expression(parent, &c, end, &tag);
                c = jsdoc_skip_space(c, end);
            }
            while (true) {
                bool bracketed = c < end && *c == '[';
                if (bracketed) c = jsdoc_skip_space(c + 1, end);
                // Modifiers (tsc's parseTemplateTagTypeParameter: parseModifiers): `const`, `in`, `out`
                bool is_const = false, is_in = false, is_out = false;
                while (true) {
                    const char* word = c;
                    while (c < end && (is_identifier_char(*c) || (uint8_t)*c >= 0x80)) c++;
                    csview w = { word, c - word };
                    bool modifier = csview_equals(w, "const") || csview_equals(w, "in") || csview_equals(w, "out");
                    if (!modifier || c >= end || (*c != ' ' && *c != '\t')) {
                        c = word;
                        break;
                    }
                    is_const |= csview_equals(w, "const");
                    is_in |= csview_equals(w, "in");
                    is_out |= csview_equals(w, "out");
                    c = jsdoc_skip_space(c, end);
                }
                const char* name_start = c;
                while (c < end && (is_identifier_char(*c) || (uint8_t)*c >= 0x80)) c++;
                if (c == name_start) break;
                if (span_valid(tag.name)) {
                    vec_jsdoc_tag_push(tags, tag);
                    tag = (JsDocTag){ .kind = JSDOC_TAG_TEMPLATE, .tag_name = tag.tag_name };
                }
                tag.name = span_make(source, name_start, c);
                tag.bracketed = bracketed;
                tag.is_const = is_const;
                tag.is_in = is_in;
                tag.is_out = is_out;
                if (bracketed) {
                    while (c < end && (*c == ' ' || *c == '\t')) c++;
                    if (c < end && *c == '=') {
                        c++;
                        TypeNode type;
                        if (parse_jsdoc_bare_type(parent, &c, end, &type)) {
                            if (tag.has_default) type_drop(&tag.default_type);
                            tag.default_type = type;
                            tag.has_default = true;
                        }
                    }
                    while (c < end && *c != ']' && *c != '\n') c++;
                    if (c < end && *c == ']') c++;
                }
                const char* after = c;
                while (after < end && (*after == ' ' || *after == '\t')) after++;
                if (after >= end || *after != ',') break;
                c = after + 1;
                while (c < end && (*c == ' ' || *c == '\t')) c++;
            }
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
            } else if (tag.kind == JSDOC_TAG_SATISFIES) {
                // Braces are required (tsc's parseSatisfiesTag: parseJSDocTypeExpression(mayOmitBraces: false))
                Parser e = parser(parent);
                e.pos = after;
                e.whitespace_end = nullptr;
                const char* token_end = after;
                while (token_end < end && (is_identifier_char(*token_end) || (uint8_t)*token_end >= 0x80)) token_end++;
                if (token_end == after && after < end) token_end++;
                parse_error(&e, span_make(source, after, token_end), 1005, "'{' expected.");
                const char* type_start = after;
                TypeNode type;
                if (after < end && parse_jsdoc_bare_type(parent, &after, end, &type)) {
                    tag.has_type = true;
                    tag.type = type;
                    tag.type_span = span_make(source, type_start, after);
                    c = after;
                }
                after = jsdoc_skip_space(after, end);
                if (after < end && *after == '}') {
                    c = after + 1;
                } else {
                    token_end = after;
                    while (token_end < end && (is_identifier_char(*token_end) || (uint8_t)*token_end >= 0x80)) token_end++;
                    if (token_end == after && after < end) token_end++;
                    parse_error(&e, span_make(source, after, token_end), 1005, "'}' expected.");
                }
                jsdoc_keep_errors(&e);
                parse_fail(&e);
            } else if ((tag.kind == JSDOC_TAG_TYPE || tag.kind == JSDOC_TAG_ENUM || tag.kind == JSDOC_TAG_EXTENDS ||
                    tag.kind == JSDOC_TAG_IMPLEMENTS) && after < end && after > c &&
                !memchr(c, '\n', (size_t)(after - c)) && *after != '@'
            ) {
                // Braces may be omitted (tsc's parseJSDocTypeExpression(mayOmitBraces)): `@type object`, `@enum number`
                const char* type_start = after;
                TypeNode type;
                if (parse_jsdoc_bare_type(parent, &after, end, &type)) {
                    tag.has_type = true;
                    tag.type = type;
                    tag.type_span = span_make(source, type_start, after);
                    while (tag.type_span.sv.size && is_jsdoc_space(tag.type_span.sv.buf[tag.type_span.sv.size - 1])) tag.type_span.sv.size--;
                    c = after;
                }
            }
        }   break;
        default:
            break;
    }
    vec_jsdoc_tag_push(tags, tag);
    return c;
}

// The tags of the comment `range` (tsc's parseJSDocCommentWorker: a tag starts at an `@` at the start of a line, after
// whitespace and at most one `*`, or after whitespace and before something else anywhere; not in backticks of a tag's
// comment)
static bool jsdoc_is_object_type(JsDocTag const* tag, bool* array);

static void parse_jsdoc_comment(Parser* parent, JsDocRange range, VecJsDocTag* tags) {
    const char* c = range.start + 3;
    const char* end = range.end - 2;
    bool line_start = true;
    bool saw_asterisk = false;
    // Backticks keep an `@` from starting a tag in a tag's comment, up to the end of the line; not in the description
    // (tsc's JSDocState.SavingBackticks, "only used when parsing tag comments")
    bool in_backticks = false;
    bool in_tag = false;
    JsDocTagKind owner = JSDOC_TAG_OTHER; // The @typedef or @callback the tags are part of, if any
    bool typedef_children = false; // The @typedef has no type or `{Object}`: it takes an `@type` child
    while (c < end) {
        char ch = *c;
        if (ch == '\n') {
            line_start = true;
            saw_asterisk = false;
            in_backticks = false;
            c++;
        } else if (ch == ' ' || ch == '\t' || ch == '\r') {
            c++;
        } else if (ch == '*' && line_start && !saw_asterisk) {
            saw_asterisk = true;
            c++;
        } else if (ch == '@' && !in_backticks && (line_start || ((c[-1] == ' ' || c[-1] == '\t') && !is_jsdoc_space(c[1])))) {
            isize before = tags->size;
            c = parse_jsdoc_tag(parent, c, end, tags);
            line_start = false;
            in_tag = true;
            for (isize i = before; i < tags->size; i++) tags->data[i].comment = range.start;
            // tsc's parseCallbackTag/parseTypedefTag/parseOverloadTag take the tags that follow as their children
            JsDocTag* tag = vec_jsdoc_tag_back_mut(tags);
            if (tag->kind == JSDOC_TAG_TYPEDEF || tag->kind == JSDOC_TAG_CALLBACK || tag->kind == JSDOC_TAG_OVERLOAD) {
                owner = tag->kind;
                bool array;
                typedef_children = tag->kind == JSDOC_TAG_TYPEDEF && jsdoc_is_object_type(tag, &array);
            } else if (owner == JSDOC_TAG_TYPEDEF && tag->kind == JSDOC_TAG_TYPE && typedef_children) {
                // (tsc's parseTypedefTag: an `@type` child of an untyped or `{Object}` typedef is its type)
                tag->nested = true;
            } else if (owner == JSDOC_TAG_CALLBACK && (tag->kind == JSDOC_TAG_PARAM || tag->kind == JSDOC_TAG_RETURNS ||
                tag->kind == JSDOC_TAG_TEMPLATE)
            ) {
                tag->nested = true;
            } else if (owner == JSDOC_TAG_OVERLOAD && (tag->kind == JSDOC_TAG_PARAM || tag->kind == JSDOC_TAG_RETURNS)) {
                // (tsc's parseJSDocSignature: parameters, then one `@returns`, which ends it)
                tag->nested = true;
                if (tag->kind == JSDOC_TAG_RETURNS) owner = JSDOC_TAG_OTHER;
            } else if (owner == JSDOC_TAG_TYPEDEF && (tag->kind == JSDOC_TAG_PROPERTY || tag->kind == JSDOC_TAG_TEMPLATE)) {
                tag->nested = true;
            } else {
                owner = JSDOC_TAG_OTHER;
            }
        } else {
            if (ch == '`' && in_tag) in_backticks = !in_backticks;
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
    // The last JSDoc comment among the host's leading comments (tsc's getJSDocCommentRanges): other comments between
    // it and the host (`// eslint-disable-next-line`) are skipped
    isize lo;
    while (true) {
        while (c > file_start && is_js_ascii_space(c[-1])) c--;
        if (c - file_start >= 4 && c[-1] == '/' && c[-2] == '*') {
            // Binary search for the comment ending at c
            lo = 0;
            isize hi = comments->size;
            while (lo < hi) {
                isize mid = (lo + hi) / 2;
                if (comments->data[mid].end < c) lo = mid + 1;
                else hi = mid;
            }
            if (lo < comments->size && comments->data[lo].end == c) break;
            // A plain block comment: before its `/*`
            const char* open = c - 2;
            while (open > file_start + 1 && !(open[-2] == '/' && open[-1] == '*')) open--;
            if (open <= file_start + 1) return false;
            c = open - 2;
            continue;
        }
        // A line that is a line comment
        const char* line = c;
        while (line > file_start && line[-1] != '\n' && line[-1] != '\r') line--;
        const char* text = line;
        while (text < c && is_js_ascii_space(*text)) text++;
        if (c - text >= 2 && text[0] == '/' && text[1] == '/' && line < c) {
            c = line;
            continue;
        }
        return false;
    }
    // The comments before it (only whitespace between) for their `@overload` tags (tsc's getJSDocOverloadTags)
    isize first = lo;
    while (first > 0) {
        const char* start = comments->data[first].start;
        while (start > file_start && is_js_ascii_space(start[-1])) start--;
        if (comments->data[first - 1].end != start) break;
        first--;
    }
    for (isize i = first; i < lo; i++) {
        JsDocRange range = comments->data[i];
        if (!csview_contains(csview_from_range(range.start, range.end), "@overload")) continue;
        VecJsDocTag earlier = {0};
        parse_jsdoc_comment(parent, range, &earlier);
        bool in_overload = false;
        for_each (t, vec_jsdoc_tag, &earlier) {
            if (t->kind == JSDOC_TAG_OVERLOAD) in_overload = true;
            else if (!t->nested) in_overload = false;
            if (in_overload || t->kind == JSDOC_TAG_TEMPLATE) {
                t->earlier = true;
                vec_jsdoc_tag_push(tags, *t);
                *t = (JsDocTag){0};
            }
        }
        vec_jsdoc_tag_drop(&earlier);
    }
    parse_jsdoc_comment(parent, comments->data[lo], tags);
    // (tsc's checkJSDocSatisfiesTag)
    bool satisfies = false;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_SATISFIES || i->earlier) continue;
        if (satisfies) jsdoc_error(parent, i->tag_name, 1223, "'satisfies' tag already specified.");
        satisfies = true;
    }
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
            if (i->kind == JSDOC_TAG_PARAM && !i->nested && span_valid(i->name) && jsdoc_name_is(i->name.sv, binding->content_span.sv)) {
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

// `@type {(a: A) => R}` (or `{{(a: A): R}}`) on a function declares its signature (tsc's getSignatureOfTypeTag): the
// written signature's parameter and return types move into the function's slots
// The one signature a written function type or `{(...): R}` literal is, null for anything else
static FunctionImpl* jsdoc_single_signature(TypeNode* type) {
    if (type->tag == TYPE_CALLABLE) return type->callable->function;
    if (type->tag == TYPE_INTERFACE && vec_interface_member_size(&type->interface.members) == 1) {
        InterfaceMember* member = vec_interface_member_at_mut(&type->interface.members, 0);
        if (member->key.tag == INTERFACE_KEY_CALLABLE && member->type.tag == TYPE_CALLABLE) return member->type.callable->function;
    }
    return nullptr;
}

static void jsdoc_apply_type_signature(VecJsDocTag* tags, FunctionImpl* function) {
    JsDocTag* tag = jsdoc_find_typed(tags, JSDOC_TAG_TYPE);
    if (!tag) return;
    FunctionImpl* signature = jsdoc_single_signature(&tag->type);
    if (!signature || signature->is_constructor) return;
    // `(this: T) => R`: the function's `this` parameter, unless it has one (or is an arrow function), and not a
    // position to match the parameters by
    FunctionArg* signature_this = vec_function_arg_size(&signature->args) ? signature->args.data : nullptr;
    if (signature_this && !(signature_this->initializer.binding.tag == BINDING_IDENTIFIER &&
        span_equals(signature_this->initializer.binding.content_span, "this"))
    ) {
        signature_this = nullptr;
    }
    FunctionArg const* own_first = vec_function_arg_size(&function->args) ? function->args.data : nullptr;
    bool has_this = own_first && own_first->initializer.binding.tag == BINDING_IDENTIFIER &&
        span_equals(own_first->initializer.binding.content_span, "this");
    if (signature_this && !has_this && !span_valid(function->arrow_span) && signature_this->initializer.binding.has_explicit_type) {
        FunctionArg arg = {
            .initializer.value = expr_implicit_any(signature_this->initializer.binding.content_span),
            .jsdoc_this = true,
        };
        arg.initializer.binding = (Binding){
            .tag = BINDING_IDENTIFIER,
            .content_span = signature_this->initializer.binding.content_span,
            .has_explicit_type = true,
            .explicit_type = signature_this->initializer.binding.explicit_type,
            .explicit_type_span = signature_this->initializer.binding.explicit_type_span,
        };
        arg.initializer.binding.explicit_type.from_jsdoc = true;
        signature_this->initializer.binding.has_explicit_type = false;
        signature_this->initializer.binding.explicit_type = (TypeNode){0};
        vec_function_arg_insert_n(&function->args, 0, &arg, 1);
        has_this = true;
    }
    if (generic_empty(&function->generic) && !generic_empty(&signature->generic)) {
        function->generic = signature->generic;
        signature->generic = (Generic){0};
        for_each (i, vec_generic_item, &function->generic.items) i->jsdoc = true;
    }
    isize skip_own = has_this ? 1 : 0, skip_signature = signature_this ? 1 : 0;
    for_each (i, vec_function_arg, &function->args) {
        if (i_index < skip_own) continue;
        isize at = i_index - skip_own + skip_signature;
        if (at >= vec_function_arg_size(&signature->args)) break;
        FunctionArg* from = vec_function_arg_at_mut(&signature->args, at);
        Binding* binding = &i->initializer.binding;
        if (binding->has_explicit_type || !from->initializer.binding.has_explicit_type || from->rest != i->rest) continue;
        binding->explicit_type = from->initializer.binding.explicit_type;
        binding->explicit_type.from_jsdoc = true;
        i->jsdoc_span = from->span;
        binding->explicit_type_span = tag->type_span;
        binding->has_explicit_type = true;
        from->initializer.binding.has_explicit_type = false;
        from->initializer.binding.explicit_type = (TypeNode){0};
        if (from->explicit_optional && i->initializer.value.tag == EXPR_IMPLICIT_ANY) i->explicit_optional = true;
    }
    if (!function->ret_explicit && signature->ret_explicit && !function->is_constructor) {
        function->ret = signature->ret;
        function->ret.from_jsdoc = true;
        function->ret_span = tag->type_span;
        function->ret_explicit = true;
        signature->ret_explicit = false;
        signature->ret = (TypeNode){0};
    }
}

// `@` to the end of the tag's name: where tsc's declarations made by tags are
static Span jsdoc_tag_decl_span(JsDocTag const* tag) {
    const char* end = span_valid(tag->name) ? tag->name.sv.buf + tag->name.sv.size : tag->tag_name.sv.buf + tag->tag_name.sv.size;
    return span_make(tag->tag_name.source, tag->tag_name.sv.buf - 1, end);
}

// `{Object}`, `{object}`, `{Object[]}`: a type a tag's nested `@property`/`@param` tags replace with a literal (tsc's
// isObjectOrObjectArrayTypeReference). array: whether it's the array form
static bool jsdoc_is_object_type(JsDocTag const* tag, bool* array) {
    *array = false;
    if (!tag->has_type) return true;
    TypeNode const* type = &tag->type;
    if (type->tag == TYPE_ARRAY) {
        *array = true;
        type = type->array.element;
    }
    return type->tag == TYPE_IDENTIFIER && !vec_span_size(&type->identifier->namespaces) &&
        !vec_spanned_type_size(&type->identifier->generic_params) &&
        (span_equals(type->identifier->name, "Object") || span_equals(type->identifier->name, "object"));
}

static bool jsdoc_name_is_under(csview name, csview prefix) {
    return name.size > prefix.size + 1 && !memcmp(name.buf, prefix.buf, (size_t)prefix.size) && name.buf[prefix.size] == '.';
}

// The type literal the `@property`/`@param` tags named `prefix.x` among `props` make (`x` when prefix is empty; tsc's
// JSDocTypeLiteral, nested through `o.a` names under an `{Object}` one)
static TypeNode jsdoc_type_literal(JsDocTag* props, isize count, csview prefix, Span span) {
    VecInterfaceMember members = {0};
    for (isize i = 0; i < count; i++) {
        JsDocTag* tag = &props[i];
        if (!span_valid(tag->name)) continue;
        csview name = tag->name.sv;
        if (prefix.size) {
            if (!jsdoc_name_is_under(name, prefix)) continue;
            name = csview_from_range(name.buf + prefix.size + 1, name.buf + name.size);
        }
        if (memchr(name.buf, '.', (size_t)name.size) || memchr(name.buf, '[', (size_t)name.size)) continue;
        bool has_children = false;
        for (isize j = i + 1; j < count && !has_children; j++) {
            has_children = span_valid(props[j].name) && jsdoc_name_is_under(props[j].name.sv, tag->name.sv);
        }
        bool array;
        TypeNode type;
        if (has_children && jsdoc_is_object_type(tag, &array)) {
            type = jsdoc_type_literal(props + i + 1, count - i - 1, tag->name.sv, jsdoc_tag_decl_span(tag));
            if (array) type = type_array(c_new(TypeNode, type), false), type.parsed = true;
        } else if (tag->has_type) {
            type = jsdoc_take_type(tag, nullptr);
            type.from_jsdoc = false;
        } else {
            type = type_any;
        }
        // (A bracketed property is optional like `foo?: T`, which adds optionality to its type)
        bool optional = tag->bracketed || type.jsdoc_optional;
        Span name_span = span_make(tag->name.source, name.buf, name.buf + name.size);
        vec_interface_member_push(&members, interface_member_make(
            jsdoc_tag_decl_span(tag), nullptr, false, optional, cstr_from_sv(name), name_span, false, type
        ));
    }
    TypeNode ret = type_interface((Interface){ .members = members, .parsed = true, .span = span });
    ret.parsed = true;
    return ret;
}

// A declaration's `@template` tags as its type parameters, when it has none written (tsc's
// getJSDocTypeParameterDeclarations: not the templates of a comment with a `@typedef`/`@callback`, those are its)
static Generic jsdoc_template_generic(VecJsDocTag* tags);
static Generic jsdoc_comment_template_generic(VecJsDocTag* tags, const char* comment);
static TypeNode jsdoc_callback_type(JsDocTag* callback, JsDocTag* children, isize count);

static void jsdoc_apply_template(VecJsDocTag* tags, Generic* generic) {
    if (!generic_empty(generic)) return;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind == JSDOC_TAG_TYPEDEF || i->kind == JSDOC_TAG_CALLBACK) return;
    }
    generic_drop(generic);
    *generic = jsdoc_template_generic(tags);
}

// A class's JSDoc: `@template`s, and the type arguments of `@extends {Base<T>}` (tsc's getEffectiveBaseTypeNode: the
// tag's ExpressionWithTypeArguments stands for the heritage clause's)
// The rightmost name of an entity name expression (tsc's getIdentifierFromEntityNameExpression), null if it isn't one
static Span jsdoc_entity_name_last(Expr const* expr) {
    if (expr->tag == EXPR_GENERIC_PARAMS) expr = &expr->generic_params->expr;
    if (expr->tag == EXPR_IDENTIFIER) return expr->identifier.name;
    if (expr->tag == EXPR_PROPERTY && expr->property->form == PROPERTY_EXPR_DOT) return expr->property->prop.inner_span;
    return span_null;
}

static void jsdoc_apply_class(Parser* p, VecJsDocTag* tags, ClassDecl* class_decl) {
    jsdoc_apply_template(tags, &class_decl->generic);
    // `@augments A` on `class C extends B` (tsc's checkJSDocAugmentsTag)
    JsDocTag* augments = jsdoc_find_typed(tags, JSDOC_TAG_EXTENDS);
    Span extends_name = jsdoc_entity_name_last(&class_decl->extends);
    if (augments && augments->type.tag == TYPE_IDENTIFIER && span_valid(extends_name) &&
        !csview_equals_sv(augments->type.identifier->name.sv, extends_name.sv)
    ) {
        Span name = augments->type.identifier->name;
        jsdoc_error(p, name, 8023, "JSDoc '@", augments->tag_name.sv, " ", name.sv, "' does not match the 'extends ",
            extends_name.sv, "' clause.");
    }
    // `@implements {I}` (tsc's getEffectiveImplementsTypeNodes: in JavaScript the tags, not the clause)
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_IMPLEMENTS || !i->has_type || i->earlier) continue;
        Span span;
        TypeNode type = jsdoc_take_type(i, &span);
        vec_spanned_type_push(&class_decl->implements, spanned_type(span, type));
    }
    if (class_decl->extends.tag == EXPR_IMPLICIT_ANY || class_decl->extends.tag == EXPR_GENERIC_PARAMS) return;
    JsDocTag* tag = jsdoc_find_typed(tags, JSDOC_TAG_EXTENDS);
    if (!tag || tag->type.tag != TYPE_IDENTIFIER || !vec_spanned_type_size(&tag->type.identifier->generic_params)) return;
    VecSpannedType params = tag->type.identifier->generic_params;
    tag->type.identifier->generic_params = (VecSpannedType){0};
    for_each (i, vec_spanned_type, &params) i->type.from_jsdoc = true;
    Span span = class_decl->extends.outer_span;
    class_decl->extends = expr_generic_params(span, c_new(GenericParamsExpr, { .expr = class_decl->extends, .params = params }));
}

// What a function's JSDoc says about its parameters and return type: `@param`, `@returns`
// The `@param` tags that name no parameter (see FunctionImpl.jsdoc_param_issues). A dotted tag right after the tag of
// its object (`{Object}`/`{object}`, maybe an array) is part of that object's type (tsc's parseNestedTypeLiteral),
// any other is a tag of its own
static void jsdoc_find_param_issues(VecJsDocTag const* tags, FunctionImpl* function) {
    if (function->jsdoc_params_checked) return;
    function->jsdoc_params_checked = true;
    Span objects[8]; // The chain of object tags the next child may be under
    u32 depth = 0;
    isize index = 0;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_PARAM || i->nested || i->earlier || !span_valid(i->name)) continue;
        csview name = i->name.sv;
        char const* dot = nullptr;
        for (isize k = name.size; k-- > 0 && !dot;) if (name.buf[k] == '.') dot = name.buf + k;
        bool array;
        bool object = i->has_type && jsdoc_is_object_type(i, &array);
        if (dot) {
            csview left = csview_from_range(name.buf, dot);
            while (depth && !csview_equals_sv(objects[depth - 1].sv, left)) depth--;
            if (depth) {
                if (object && depth < c_arraylen(objects)) objects[depth++] = i->name;
                continue;
            }
        }
        depth = 0;
        if (object) objects[depth++] = i->name;
        isize at = index++;
        // (A pattern parameter goes by the tag at its position)
        if (at < vec_function_arg_size(&function->args) && function->args.data[at].initializer.binding.tag != BINDING_IDENTIFIER) continue;
        Span left = span_null;
        if (dot) {
            left = span_make(i->name.source, name.buf, dot);
        } else {
            bool found = i->name_first;
            for_each (a, vec_function_arg, &function->args) {
                Binding const* binding = &a->initializer.binding;
                found |= binding->tag == BINDING_IDENTIFIER && jsdoc_name_is(name, binding->content_span.sv);
            }
            if (found) continue;
        }
        function->jsdoc_param_issues = realloc(function->jsdoc_param_issues,
            sizeof(JsDocParamIssue) * (function->jsdoc_param_issue_count + 1));
        panic_if(!function->jsdoc_param_issues, "out of memory");
        function->jsdoc_param_issues[function->jsdoc_param_issue_count++] = (JsDocParamIssue){ i->name, left };
    }
}

static void jsdoc_apply_function(VecJsDocTag* tags, FunctionImpl* function) {
    jsdoc_find_param_issues(tags, function);
    // `@overload` tags (of every comment before the host), each a signature of their own: the function's, whose
    // implementation isn't one (tsc's getSignaturesOfSymbol). The comment's `@template`s are the overload's
    // (getEffectiveTypeParameterDeclarations of a JSDocSignature)
    if (!function->jsdoc_overload_count) {
        for_each (i, vec_jsdoc_tag, tags) {
            if (i->kind != JSDOC_TAG_OVERLOAD) continue;
            isize children = 0;
            while (i + 1 + children < tags->data + tags->size && i[1 + children].nested) children++;
            TypeNode type = jsdoc_callback_type(i, i + 1, children);
            if (i->earlier) {
                type.callable->function->generic = jsdoc_comment_template_generic(tags, i->comment);
            } else {
                // (The host's own comment: its `@template`s, which are the host's too)
                Generic* generic = &type.callable->function->generic;
                for_each (t, vec_jsdoc_tag, tags) {
                    if (t->kind != JSDOC_TAG_TEMPLATE || t->earlier || !span_valid(t->name)) continue;
                    vec_generic_item_push(&generic->items, (GenericItem){
                        .name = t->name, .full_span = t->name, .jsdoc = true, .jsdoc_shared = true,
                    });
                }
            }
            function->jsdoc_overloads = realloc(function->jsdoc_overloads, sizeof(TypeNode) * (function->jsdoc_overload_count + 1));
            panic_if(!function->jsdoc_overloads, "out of memory");
            function->jsdoc_overloads[function->jsdoc_overload_count++] = type;
        }
    }
    jsdoc_apply_template(tags, &function->generic);
    for_each (i, vec_jsdoc_tag, tags) {
        function->jsdoc_typed |= (i->kind == JSDOC_TAG_PARAM || i->kind == JSDOC_TAG_TYPE) && !i->nested && !i->earlier;
        function->jsdoc_class |= i->kind == JSDOC_TAG_CLASS && !i->nested && !i->earlier && !span_valid(function->arrow_span);
    }
    // `@this {T}`: the function's `this` parameter (tsc's getJSDocThisTag in getSignatureFromDeclaration and
    // getTypeForThisExpressionFromJSDoc). Not of an arrow function, which has no `this` of its own
    JsDocTag* this_tag = jsdoc_find_typed(tags, JSDOC_TAG_THIS);
    FunctionArg const* first = vec_function_arg_size(&function->args) ? function->args.data : nullptr;
    bool has_this = first && first->initializer.binding.tag == BINDING_IDENTIFIER &&
        span_equals(first->initializer.binding.content_span, "this");
    if (this_tag && !has_this && !span_valid(function->arrow_span) && !function->is_constructor) {
        // (No declaration span: tsc's this-parameter symbol of a JSDoc tag has no declaration, `Symbol(this)`)
        FunctionArg arg = {
            .initializer.value = expr_implicit_any(this_tag->tag_name),
            .jsdoc_this = true,
        };
        arg.initializer.binding = (Binding){
            .tag = BINDING_IDENTIFIER,
            .content_span = this_tag->tag_name,
            .has_explicit_type = true,
        };
        arg.initializer.binding.explicit_type = jsdoc_take_type(this_tag, &arg.initializer.binding.explicit_type_span);
        vec_function_arg_insert_n(&function->args, 0, &arg, 1);
    }
    jsdoc_apply_type_signature(tags, function);
    for_each (i, vec_function_arg, &function->args) {
        Binding* binding = &i->initializer.binding;
        if (binding->has_explicit_type) continue;
        JsDocTag* tag = jsdoc_param_tag(tags, function, i);
        if (!tag) continue;
        bool variadic = tag->variadic;
        bool bracketed = tag->bracketed;
        // `@param {Object} o` + `@param {string} o.a`: a literal of the nested ones
        isize children = 0;
        while (tag + 1 + children < tags->data + tags->size && tag[1 + children].kind == JSDOC_TAG_PARAM &&
            span_valid(tag[1 + children].name) && jsdoc_name_is_under(tag[1 + children].name.sv, tag->name.sv)
        ) {
            children++;
        }
        bool direct = false; // (`o.a.b` without `o.a` is TS8032, which isn't reported: o keeps its type)
        for (isize j = 1; j <= children && !direct; j++) {
            csview rest = csview_from_range(tag[j].name.sv.buf + tag->name.sv.size + 1, tag[j].name.sv.buf + tag[j].name.sv.size);
            direct = !memchr(rest.buf, '.', (size_t)rest.size);
        }
        bool array;
        if (direct && jsdoc_is_object_type(tag, &array)) {
            Span type_span = tag->type_span;
            TypeNode literal = jsdoc_type_literal(tag + 1, children, tag->name.sv, jsdoc_tag_decl_span(tag));
            if (array) literal = type_array(c_new(TypeNode, literal), false), literal.parsed = true;
            type_drop(&tag->type);
            tag->type = literal;
            tag->type_span = type_span;
        }
        binding->explicit_type = jsdoc_take_type(tag, &binding->explicit_type_span);
        binding->has_explicit_type = true;
        if (binding->explicit_type.jsdoc_optional || bracketed) {
            // `{T=}` (of type `T | undefined`, which tsc prints) and `[name]` are optional parameters
            if (i->initializer.value.tag == EXPR_IMPLICIT_ANY) i->explicit_optional = true;
        }
        i->jsdoc_variadic = variadic && !i->rest;
        if (variadic && i->rest) {
            // TODO: `{...T=}` is optional of variadic in tsc (parseJSDocType), which types the parameter any and prints
            // as `T[] | undefined` (getTypeFromJSDocVariadicType sees no parameter tag above it)
            binding->explicit_type = type_array(c_new(TypeNode, binding->explicit_type), false);
            binding->explicit_type.parsed = true;
        } else if (variadic) {
            // Not a rest parameter: T | undefined (tsc's getTypeFromJSDocVariadicType: addOptionality)
            binding->explicit_type = jsdoc_union(binding->explicit_type, jsdoc_synthesized(type_undefined));
        }
        binding->explicit_type.from_jsdoc = true;
    }
    // A getter's `@type` is the property's, its return type (tsc's getAnnotatedAccessorType; a setter's isn't looked at)
    JsDocTag* type_tag = jsdoc_find_typed(tags, JSDOC_TAG_TYPE);
    if (type_tag && function->accessor == 1 && !function->ret_explicit) {
        function->ret = jsdoc_take_type(type_tag, &function->ret_span);
        function->ret_explicit = true;
        type_tag = nullptr;
    }
    // A `@type` that isn't a written signature: its signature is resolved by the checker
    if (type_tag && type_tag->type.tag != TYPE_CALLABLE && type_tag->type.tag != TYPE_INTERFACE && !function->jsdoc_signature_type) {
        function->jsdoc_signature_type = c_new(TypeNode, jsdoc_take_type(type_tag, nullptr));
    }
    if (!function->ret_explicit && !function->is_constructor) {
        JsDocTag* tag = jsdoc_find_typed(tags, JSDOC_TAG_RETURNS);
        if (tag) {
            function->ret = jsdoc_take_type(tag, &function->ret_span);
            function->ret_explicit = true;
            if (tag->predicate && !function->predicate) {
                function->predicate = tag->predicate;
                tag->predicate = nullptr;
            }
        }
    }
}

// The function type of a `@callback` tag and its `@param`/`@returns` tags (tsc's JSDocSignature)
static TypeNode jsdoc_callback_type(JsDocTag* callback, JsDocTag* children, isize count) {
    FunctionImpl function = { .args = vec_function_arg_init(), .jsdoc_type = true, .jsdoc_callback = true };
    for (isize i = 0; i < count; i++) {
        JsDocTag* tag = &children[i];
        if (tag->kind == JSDOC_TAG_RETURNS) {
            if (tag->has_type && !function.ret_explicit) {
                function.ret = jsdoc_take_type(tag, &function.ret_span);
                function.ret.from_jsdoc = false;
                function.ret_explicit = true;
            }
            continue;
        }
        if (tag->kind != JSDOC_TAG_PARAM || !span_valid(tag->name) || memchr(tag->name.sv.buf, '.', (size_t)tag->name.sv.size)) continue;
        FunctionArg arg = { .initializer.value = expr_implicit_any(tag->name), .span = jsdoc_tag_decl_span(tag) };
        bool array;
        TypeNode type;
        bool has_children = false;
        for (isize j = i + 1; j < count && !has_children; j++) {
            has_children = children[j].kind == JSDOC_TAG_PARAM && span_valid(children[j].name) &&
                jsdoc_name_is_under(children[j].name.sv, tag->name.sv);
        }
        if (has_children && jsdoc_is_object_type(tag, &array)) {
            type = jsdoc_type_literal(children + i + 1, count - i - 1, tag->name.sv, jsdoc_tag_decl_span(tag));
            if (array) type = type_array(c_new(TypeNode, type), false), type.parsed = true;
        } else if (tag->has_type) {
            type = jsdoc_take_type(tag, nullptr);
            type.from_jsdoc = false;
        } else {
            type = jsdoc_synthesized(type_any); // (No type node: tsc's printer makes an `any`)
        }
        if (type.jsdoc_optional || tag->bracketed) arg.explicit_optional = true;
        if (tag->variadic) {
            arg.rest = true;
            arg.initializer.is_rest_parameter = true;
            type = type_array(c_new(TypeNode, type), false), type.parsed = true;
        }
        arg.initializer.binding = (Binding){
            .tag = BINDING_IDENTIFIER,
            .content_span = tag->name,
            .has_explicit_type = true,
            .explicit_type = type,
        };
        vec_function_arg_push(&function.args, arg);
    }
    Span span = jsdoc_tag_decl_span(callback);
    function.args_span = span;
    function.full_span = span;
    return function_impl_into_type(function);
}

// The type parameters of the `@typedef`/`@callback` tags of a comment: every `@template` in it (tsc's
// getEffectiveTypeParameterDeclarations of a JSDocTypeAlias)
static Generic jsdoc_template_generic(VecJsDocTag* tags) {
    return jsdoc_comment_template_generic(tags, nullptr);
}

// The `@template`s of one comment (null: of the host's, not an earlier one's)
static Generic jsdoc_comment_template_generic(VecJsDocTag* tags, const char* comment) {
    Generic ret = {0};
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_TEMPLATE || !span_valid(i->name)) continue;
        if (comment ? i->comment != comment : i->earlier) continue;
        GenericItem item = { .name = i->name, .full_span = i->name, .jsdoc = true, .is_const = i->is_const, .is_in = i->is_in, .is_out = i->is_out };
        if (i->has_type) {
            item.extends = jsdoc_take_type(i, nullptr);
            item.extends.from_jsdoc = false;
            item.extends_explicit = true;
        }
        if (i->has_default) {
            item.fallback = i->default_type;
            item.fallback_explicit = true;
            i->has_default = false;
        }
        vec_generic_item_push(&ret.items, item);
    }
    return ret;
}

static bool parse_import_export_statement(Parser* parent, Statement* ret);

// `@import { A } from "m"` (TS 5.5): a type-only import declaration, parsed by the statement parser from the tag's
// name on (tsc's JSDocImportTag). Parse errors are JSDoc errors
static void jsdoc_import_statement(Parser* parent, JsDocTag const* tag, const char* end, VecStatement* out) {
    Parser p = parser(parent);
    SourceFile const* source = p.shared->source;
    // The statement parser doesn't stop at the `*/`: what tsc's parseImportTag says about a clause, `from` or module
    // specifier that is missing comes first
    const char* last = tag->tag_name.sv.buf + tag->tag_name.sv.size; // Past the last token
    const char* c = jsdoc_skip_space(last, end);
    bool clause = false;
    if (c < end && (is_identifier_char(*c) || *c == '{' || *c == '*')) {
        clause = true;
        if (*c == '{') {
            while (c < end && *c != '}') c++;
            if (c < end) c++;
        } else if (*c == '*') {
            // `* as name`
            for (int word = 0; word < 2; word++) {
                c = jsdoc_skip_space(c + (word ? 0 : 1), end);
                while (c < end && is_identifier_char(*c)) c++;
            }
        } else {
            while (c < end && is_identifier_char(*c)) c++;
            const char* comma = jsdoc_skip_space(c, end);
            if (comma < end && *comma == ',') clause = false; // `x, { y }`: left to the statement parser
        }
        last = c;
        c = jsdoc_skip_space(c, end);
    }
    bool bad = false;
    if (clause) {
        const char* word = c;
        while (c < end && is_identifier_char(*c)) c++;
        if (csview_equals(csview_from_range(word, c), "from")) {
            last = c;
            c = jsdoc_skip_space(c, end);
        } else {
            c = word;
            const char* token_end = c;
            while (token_end < end && is_identifier_char(*token_end)) token_end++;
            if (token_end == c && c < end) token_end++;
            parse_error(&p, span_make(source, c, token_end), 1005, "'from' expected.");
            bad = true;
        }
    }
    if (c >= end) {
        // After the tag name, tsc's current token is the JSDoc scanner's whitespace or newline token there
        const char* token_end = last;
        if (!clause && token_end < end) {
            if (*token_end == '\n') token_end++;
            else if (token_end[0] == '\r' && token_end[1] == '\n') token_end += 2;
            else while (token_end < end && (*token_end == ' ' || *token_end == '\t')) token_end++;
        }
        parse_error(&p, span_make(source, last, token_end), 1109, "Expression expected.");
        bad = true;
    }
    if (bad) {
        jsdoc_keep_errors(&p);
        parse_fail(&p);
        return;
    }
    p.pos = tag->tag_name.sv.buf;
    p.whitespace_end = nullptr;
    bool outer_in_jsdoc = p.shared->in_jsdoc;
    const char* outer_end = p.shared->jsdoc_end;
    p.shared->in_jsdoc = true;
    p.shared->jsdoc_end = end;
    Statement statement;
    if (parse_import_export_statement(&p, &statement)) {
        if (statement.tag == STATEMENT_IMPORT_EXPORT && statement.import_export->requires_target &&
            !port_item_is_export(&statement.import_export->main) && statement.import_export->main.tag != PORT_IMPORT_ALIAS
        ) {
            statement.import_export->main.type_only = true;
            statement.import_export->jsdoc = true;
            statement.import_export->main.jsdoc = true;
            for_each (i, vec_port_item, &statement.import_export->items) i->jsdoc = true;
            statement.span = span_make(p.shared->source, tag->tag_name.sv.buf - 1, p.pos);
            statement.flow = nullptr;
            statement.jump_label = span_null;
            vec_statement_push(out, statement);
        } else {
            statement_drop(&statement);
        }
    }
    p.shared->in_jsdoc = outer_in_jsdoc;
    p.shared->jsdoc_end = outer_end;
    jsdoc_keep_errors(&p);
    parse_fail(&p);
}

// Binds the declaration tags (`@typedef`, `@callback`, `@import`) of the JSDoc comments from `from` to `to` that no
// statement list inside claimed yet, as type alias and import declarations appended to `out`, and claims them. tsc
// declares a typedef in the block scope of its host (delayedBindJSDocTypedefTag): the innermost statement list
// containing the comment. bind: false only claims them (the comments before a block's `}` have no host)
static void jsdoc_bind_comments(Parser* parent, const char* from, const char* to, bool bind, bool toplevel, VecStatement* out) {
    VecJsDocRange* comments = &parent->shared->jsdoc_comments;
    isize lo = 0, hi = comments->size;
    while (lo < hi) {
        isize mid = (lo + hi) / 2;
        if (comments->data[mid].start < from) lo = mid + 1;
        else hi = mid;
    }
    for (isize c = lo; c < comments->size && comments->data[c].end <= to; c++) {
        JsDocRange range = comments->data[c];
        if (range.claimed) continue;
        comments->data[c].claimed = true;
        if (!bind) continue;
        csview text = csview_from_range(range.start, range.end);
        if (!csview_contains(text, "@typedef") && !csview_contains(text, "@callback") && !csview_contains(text, "@import")) continue;
        VecJsDocTag tags = {0};
        parse_jsdoc_comment(parent, range, &tags);
        for_each (i, vec_jsdoc_tag, &tags) {
            if (i->kind == JSDOC_TAG_IMPORT && toplevel) {
                jsdoc_import_statement(parent, i, range.end - 2, out);
                continue;
            }
            if (i->kind != JSDOC_TAG_TYPEDEF && i->kind != JSDOC_TAG_CALLBACK) continue;
            if (!span_valid(i->name) || memchr(i->name.sv.buf, '.', (size_t)i->name.sv.size)) continue;
            isize children = 0;
            while (i + 1 + children < tags.data + tags.size && i[1 + children].nested) children++;
            TypeNode type;
            bool array;
            if (i->kind == JSDOC_TAG_CALLBACK) {
                type = jsdoc_callback_type(i, i + 1, children);
            } else {
                bool has_properties = false;
                JsDocTag* type_child = nullptr;
                for (isize j = 0; j < children; j++) {
                    has_properties |= i[1 + j].kind == JSDOC_TAG_PROPERTY;
                    if (!type_child && i[1 + j].kind == JSDOC_TAG_TYPE && i[1 + j].has_type) type_child = &i[1 + j];
                }
                bool child_array;
                if (type_child && !jsdoc_is_object_type(type_child, &child_array)) {
                    type = jsdoc_take_type(type_child, nullptr);
                    type.from_jsdoc = false;
                } else if (has_properties && jsdoc_is_object_type(i, &array)) {
                    type = jsdoc_type_literal(i + 1, children, (csview){0}, jsdoc_tag_decl_span(i));
                    if (array) type = type_array(c_new(TypeNode, type), false), type.parsed = true;
                } else if (i->has_type) {
                    type = jsdoc_take_type(i, nullptr);
                    type.from_jsdoc = false;
                } else {
                    // (tsc's checkJSDocTypeAliasTag: no type expression, not even a literal of `@property` tags)
                    if (!has_properties && !type_child) {
                        jsdoc_error(parent, i->name, 8021, "JSDoc '@typedef' tag should either have a type annotation or be "
                            "followed by '@property' or '@member' tags.");
                    }
                    type = type_any;
                }
            }
            Span span = jsdoc_tag_decl_span(i);
            const char* after = range.end;
            while (is_js_ascii_space(*after)) after++;
            vec_statement_push(out, (Statement){
                .tag = STATEMENT_DECL_TYPE,
                .decl.type = {
                    .name = i->name,
                    .full_span = span,
                    .generic = jsdoc_template_generic(&tags),
                    .type = type,
                    .jsdoc = true,
                    .jsdoc_twice = *after == '(',
                },
                .span = span,
            });
        }
        vec_jsdoc_tag_drop(&tags);
    }
}

// A function or arrow function expression is a JSDoc host of its own (`f(/** @param {T} x */ (x) => {})`)
static void jsdoc_apply_function_expression(Parser* p, Expr* function) {
    VecJsDocTag tags = {0};
    if (jsdoc_before(p, span_start_ptr(function->outer_span), &tags)) jsdoc_apply_function(&tags, function->function);
    vec_jsdoc_tag_drop(&tags);
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

    u32 saved_context = list_enter(&p, PARSING_TYPE_MEMBERS);
    while (*p.pos && *p.pos != '}' && p.pos != prev_s) {
        prev_s = p.pos;
        hop_whitespace(&p);

        ClassItem item;
        if (parse_list_member(&p, PARSING_TYPE_MEMBERS, true, true, &item)) {
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
                        bool typed_setter = false;
                        Span setter_param = span_null;
                        if (is_getter) {
                            type = type_move(&item.method.func.ret);
                        } else if (vec_function_arg_size(&item.method.func.args)) {
                            FunctionArg* arg = &item.method.func.args.data[0];
                            typed_setter = arg->initializer.binding.has_explicit_type;
                            if (arg->initializer.binding.tag == BINDING_IDENTIFIER) setter_param = arg->initializer.binding.content_span;
                            type = type_move(&arg->initializer.binding.explicit_type);
                        }
                        if (existing) {
                            vec_span_push(&existing->declarations, item.span);
                            existing->second_name = item.method.name_span;
                            if (!is_getter) existing->setter_param = setter_param;
                            // The setter's parameter type is what writes are checked against (tsc's getWriteTypeOfSymbol)
                            if (is_getter) {
                                if (existing->setter_first && !existing->has_write_type) {
                                    existing->write_type = existing->type;
                                    existing->has_write_type = true;
                                } else {
                                    type_drop(&existing->type);
                                }
                                existing->type = type;
                                existing->setter_first = false;
                            } else if (typed_setter && !existing->has_write_type) {
                                existing->write_type = type;
                                existing->has_write_type = true;
                                existing->readonly = false;
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
                            member.setter_first = typed_setter;
                            member.setter_param = setter_param;
                            vec_interface_member_push(&members, member);
                        }
                        break;
                    }
                    // (What a method signature is reported at, like a class method: its name)
                    item.method.func.member_name = item.method.name_span;
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
                    vec_interface_member_back_mut(&members)->implicit_any = !item.field.has_explicit_type;
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
            if (list_abort_or_skip(&p, PARSING_TYPE_MEMBERS)) break;
            continue;
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
    list_leave(&p, saved_context);

    expect_char(&p, '}');
    shrink_list(vec_interface_member, &members);

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
    if (!parse_generic_decl(&p, TYPE_PARAM_OF_TYPE, &generic)) {
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

// The heritage clauses of a class or interface, as tsc parses them: any number of `extends` and `implements` clauses
// in any order, each a list of expressions with type arguments. What tsc's checker doesn't look at (a second clause,
// a class's second base) is kept only for the baselines; checkGrammarClassDeclarationHeritageClauses and
// checkGrammarInterfaceDeclaration's complaints about it are grammar errors reported here
typedef struct HeritageClauses {
    bool is_class;
    Expr base; // A class's: the first element of its first `extends` clause
    Span base_clause; // `extends A<T>`
    VecExpr unchecked_exprs; // A class's other `extends` elements
    // Entity names (`A.B<T>`) of the checked clause: a class's first `implements` clause, an interface's first `extends`
    VecSpannedType types;
    VecTypeNode unchecked_types; // Entity names of the other clauses
    // Other expressions (`A?.B`, `f()`), which the checker rejects (TS2499/TS2500) in the checked clause, and only
    // resolves in the others
    VecExpr invalid;
    VecExpr unchecked_invalid;
} HeritageClauses;

// An `extends` element of a class or an interface
static bool heritage_is_entity_name(Expr const* e) {
    if (e->tag == EXPR_GENERIC_PARAMS) e = &e->generic_params->expr;
    while (e->tag == EXPR_PROPERTY && e->property->form == PROPERTY_EXPR_DOT && !e->property->optional_chain) {
        e = &e->property->expr;
    }
    return e->tag == EXPR_IDENTIFIER && !e->identifier.keyword;
}

// tsc's parseExpressionWithTypeArguments. Where a type is meant (`implements`, an interface's `extends`) an entity
// name is parsed again as the type reference it means
// tsc's parseExpressionWithTypeArguments: a left-hand side expression and type arguments (in JavaScript too, where
// the expression parser leaves `<` alone)
static void parse_heritage_expr(Parser* p, Expr* ret) {
    ExprCtx ctx = expr_ctx_null;
    ctx.reject_operators = true;
    Anchor a = anchor(p);
    expect_expr(p, ctx, ret);
    if (ret->tag == EXPR_GENERIC_PARAMS || !p->shared->source->is_js) return;
    Parser gp = parser(p);
    hop_whitespace(&gp);
    VecSpannedType params;
    if (*gp.pos == '<' && parse_generic_params(&gp, false, &params)) {
        parse_success(&gp);
        if (!vec_spanned_type_is_empty(&params)) {
            Span args = span_between(vec_spanned_type_front(&params)->span, vec_spanned_type_back(&params)->span);
            js_syntax_error(p, args, 8011, "Type arguments can only be used in TypeScript files.");
        }
        *ret = expr_generic_params(span_from(a), c_new(GenericParamsExpr, { .expr = *ret, .params = params }));
    } else {
        parse_fail(&gp);
    }
}

// An element of a heritage clause. Where a type is meant (`implements`, an interface's `extends`) an entity name is
// parsed again as the type reference it means
static void parse_heritage_element(Parser* p, HeritageClauses* h, bool is_extends, bool checked, Span keyword, u32 index) {
    if (h->is_class && is_extends) {
        Expr e;
        parse_heritage_expr(p, &e);
        if (checked && index == 0) {
            h->base = e;
            h->base_clause = span_make(p->shared->source, span_start_ptr(keyword), p->pos);
        } else {
            vec_expr_push(&h->unchecked_exprs, e);
        }
        return;
    }
    Parser ep = parser(p);
    const char* start = ep.pos;
    Expr e;
    parse_heritage_expr(&ep, &e);
    if (heritage_is_entity_name(&e)) {
        const char* end = ep.pos;
        parse_fail(&ep);
        Parser tp = parser(p);
        TypeNode t;
        if (parse_type(&tp, TYPE_PRECEDENCE_OPERATOR, &t)) {
            if (tp.pos == end) {
                Span span = span_make(p->shared->source, start, end);
                parse_success(&tp);
                expr_drop(&e);
                if (checked) {
                    vec_spanned_type_push(&h->types, spanned_type(span, t));
                } else {
                    vec_type_push(&h->unchecked_types, t);
                }
                return;
            }
            type_drop(&t);
        }
        parse_fail(&tp);
        expr_drop(&e);
        parse_heritage_expr(p, &e);
    } else {
        parse_success(&ep);
    }
    vec_expr_push(checked ? &h->invalid : &h->unchecked_invalid, e);
}

// The expressions that aren't entity names: the checked clause's first (checked_ret of them), then the others'
static void heritage_take_invalid(HeritageClauses* h, VecExpr* ret, u32* checked_ret) {
    *checked_ret = (u32)vec_expr_size(&h->invalid);
    *ret = h->invalid;
    for_each (i, vec_expr, &h->unchecked_invalid) vec_expr_push(ret, *i);
    h->unchecked_invalid.size = 0; // (The expressions were moved)
    vec_expr_drop(&h->unchecked_invalid);
}

// tsc's parseHeritageClauses (parseList(HeritageClauses, parseHeritageClause))
static void parse_heritage_clauses(Parser* p, HeritageClauses* h) {
    hop_whitespace(p);
    if (*p->pos != 'e' && *p->pos != 'i') return;
    Tok first = tok_peek(p);
    if (!tok_is(first, "extends") && !tok_is(first, "implements")) return;
    bool grammar_done = false;
    bool seen_extends = false;
    bool seen_implements = false;
    bool first_extends = true;
    bool first_implements = true;
    u32 saved_clauses = list_enter(p, PARSING_HERITAGE_CLAUSES);
    while (true) {
        hop_whitespace(p);
        Tok t = tok_peek(p);
        if (list_is_terminator(PARSING_HERITAGE_CLAUSES, t)) break;
        bool is_extends = tok_is(t, "extends");
        if (!is_extends && !tok_is(t, "implements")) {
            if (list_abort_or_skip(p, PARSING_HERITAGE_CLAUSES)) break;
            continue;
        }
        Span keyword = span_make(p->shared->source, t.start, t.end);
        p->pos = t.end;
        // The clause the checker looks at: the first `extends` clause, a class's first `implements` clause
        bool checked = is_extends ? first_extends : (h->is_class && first_implements);
        if (is_extends) first_extends = false;
        else first_implements = false;

        // tsc's parseDelimitedList(HeritageClauseElement, parseExpressionWithTypeArguments)
        u32 count = 0;
        Span trailing_comma = span_null;
        Span second_element = span_null;
        intptr_t clause_mark = error_mark(p);
        const char* clause_end = span_end_ptr(keyword);
        u32 saved_elements = list_enter(p, PARSING_HERITAGE_CLAUSE_ELEMENT);
        while (true) {
            hop_whitespace(p);
            Tok e = tok_peek(p);
            if (tok_is_heritage_clause_element(p, e, false)) {
                const char* start = p->pos;
                if (count == 1) second_element = span_make(p->shared->source, e.start, e.end);
                parse_heritage_element(p, h, is_extends, checked, keyword, count);
                clause_end = p->pos;
                count++;
                trailing_comma = span_null;
                hop_whitespace(p);
                if (hop_char(p, ',')) {
                    trailing_comma = span_from_previous_char(p);
                    continue;
                }
                if (list_is_terminator(PARSING_HERITAGE_CLAUSE_ELEMENT, tok_peek(p))) break;
                expect_char(p, ',');
                if (p->pos == start) hop_token(p);
                continue;
            }
            if (list_is_terminator(PARSING_HERITAGE_CLAUSE_ELEMENT, e)) break;
            if (list_abort_or_skip(p, PARSING_HERITAGE_CLAUSE_ELEMENT)) break;
        }
        list_leave(p, saved_elements);
        if (h->is_class && !is_extends && reports_js_syntax(p)) {
            js_syntax_skip_since(p, clause_mark);
            js_syntax_error(p, span_make(p->shared->source, span_start_ptr(keyword), clause_end), 8005,
                "'implements' clauses can only be used in TypeScript files.");
        }

        // checkGrammarClassDeclarationHeritageClauses, checkGrammarInterfaceDeclaration: the first complaint about
        // the clauses ends the check, then checkGrammarHeritageClause for each clause until then
        if (grammar_done) continue;
        if (is_extends) {
            if (seen_extends) {
                parse_error(p, keyword, 1172, "'extends' clause already seen.");
                grammar_done = true;
            } else if (seen_implements) {
                parse_error(p, keyword, 1173, "'extends' clause must precede 'implements' clause.");
                grammar_done = true;
            } else if (h->is_class && count > 1) {
                parse_error(p, second_element, 1174, "Classes can only extend a single class.");
                grammar_done = true;
            }
            seen_extends = true;
        } else if (!h->is_class) {
            parse_error(p, keyword, 1176, "Interface declaration cannot have 'implements' clause.");
            grammar_done = true;
        } else if (seen_implements) {
            parse_error(p, keyword, 1175, "'implements' clause already seen.");
            grammar_done = true;
        } else {
            seen_implements = true;
        }
        if (grammar_done) continue;
        if (span_valid(trailing_comma)) {
            parse_error(p, trailing_comma, 1009, "Trailing comma not allowed.");
        } else if (count == 0) {
            Span at = span_make(p->shared->source, span_end_ptr(keyword), span_end_ptr(keyword));
            parse_error(p, at, 1097, "'", keyword, "' list cannot be empty.");
        }
    }
    list_leave(p, saved_clauses);
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
    if (!parse_generic_decl(&p, TYPE_PARAM_OF_TYPE, &generic)) {
        generic = (Generic){0};
    }
    hop_whitespace(&p);

    HeritageClauses heritage = { .is_class = false };
    parse_heritage_clauses(&p, &heritage);
    hop_whitespace(&p);
    Interface interface;
    if (!parse_interface(&p, &interface)) {
        interface = (Interface){0};
        expected_token_error(&p, "{");
        hop_token(&p);
    }

    *ret = (InterfaceDecl) {
        .name = name,
        .full_span = span_from(a),
        .generic = generic,
        .extends = heritage.types,
        .unchecked_extends = heritage.unchecked_types,
        .type = type_interface(interface),
    };
    heritage_take_invalid(&heritage, &ret->invalid_extends, &ret->invalid_extends_checked);
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
    Span word;
    Parser probe = parser(&p);
    bool reserved = parse_identifier_or_keyword(&probe, &word) && is_reserved_word(word.sv);
    parse_fail(&probe);
    if (reserved) {
        // tsc's parseIdentifier: TS1359, and a missing name (the word stays for what follows to trip over)
        parse_error(&p, word, 1359, "Identifier expected. '", word, "' is a reserved word that cannot be used here.");
        name = span_from_pos(&p);
    } else {
        expect_identifier(&p, &name);
    }
    hop_whitespace(&p);

    // (Without its `{` the enum has no members: tsc's createMissingList)
    const char* before_body = p.pos;
    bool has_body = expect_char(&p, '{');
    hop_whitespace(&p);

    VecEnumCase cases = vec_enum_case_init();
    u32 saved_context = list_enter(&p, PARSING_ENUM_MEMBERS);
    if (!has_body) p.pos = before_body;
    while (has_body && *p.pos && *p.pos != '}') {
        Tok t = tok_peek(&p);
        if (!list_is_element(&p, PARSING_ENUM_MEMBERS, t)) {
            if (list_abort_or_skip(&p, PARSING_ENUM_MEMBERS)) break;
            continue;
        }
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
        } else if (isdigit((unsigned char)*p.pos) || (*p.pos == '.' && isdigit((unsigned char)p.pos[1]))) {
            // A numeric name (tsc's parsePropertyName): TS2452 from the checker
            Value value;
            if (parse_numeric_value(&p, &value)) {
                enum_case.numeric_name = value.tag == VALUE_NUMBER && isfinite(value.number) ? 1 : 2;
                value_drop(&value);
            }
            enum_case.name = enum_case.full_name = span_from(name_anchor);
        } else if (*p.pos == '[') {
            // A computed name (tsc's parsePropertyName): `['a']` names the member `a`, anything else is TS1164
            p.pos++;
            hop_whitespace(&p);
            Anchor key_anchor = anchor(&p);
            Expr key;
            bool literal = false;
            if (expect_expr(&p, expr_ctx_null, &key)) {
                if (key.tag == EXPR_LITERAL && (key.literal.tag == VALUE_STRING || key.literal.tag == VALUE_NUMBER)) literal = true;
                expr_drop(&key);
            }
            Span key_span = span_from(key_anchor);
            hop_whitespace(&p);
            expect_char(&p, ']');
            enum_case.full_name = span_from(name_anchor);
            if (literal && strchr("\"'`", *span_start_ptr(key_span))) {
                enum_case.name = span_make(key_span.source, span_start_ptr(key_span) + 1, span_end_ptr(key_span) - 1);
                enum_case.name_is_value = true;
            } else if (literal) {
                enum_case.name = key_span;
            } else {
                enum_case.name = enum_case.full_name;
                enum_case.computed_name = true;
            }
        } else if (*p.pos == '#' && identifier_char_size(p.pos + 1, true)) {
            // A private name (tsc's parsePropertyName): TS18024 from the checker
            p.pos++;
            parse_identifier_or_keyword(&p, nullptr);
            enum_case.name = enum_case.full_name = span_from(name_anchor);
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
        vec_enum_case_push(&cases, enum_case);
        // tsc's parseDelimitedList: a missing comma is reported (getExpectedCommaDiagnostic), and the list goes on
        if (!hop_char(&p, ',') && *p.pos && *p.pos != '}') {
            parse_error(&p, span_from_token(&p), 1357, "An enum member name must be followed by a ',', '=', or '}'.");
            if (p.pos == t.start) hop_token(&p);
        }
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);

    if (has_body) expect_char(&p, '}');

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
        Anchor a = anchor(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, &predicate.type);
        predicate.type_span = span_from(a);
        // (`arg is infer R ? ...`: the `infer` looked past the space for an `extends`)
        while (predicate.type_span.sv.size && is_js_ascii_space(predicate.type_span.sv.buf[predicate.type_span.sv.size - 1])) {
            predicate.type_span.sv.size--;
        }
        predicate.has_type = true;
    } else if (!asserts) {
        return parse_fail(&p);
    }

    ret->ret = asserts ? type_void : type_boolean;
    ret->predicate = c_new(TypePredicateNode, predicate);
    return parse_success(&p);
}

static bool parse_function_args_inner(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_ret_type,
    bool commit_on_paren,
    FunctionImpl* ret
);

// commit_on_paren: once the `(` is there this is a parameter list whatever follows (tsc has chosen a function by then):
// the list recovers from what isn't a parameter instead of failing
static bool parse_function_args(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_ret_type,
    bool commit_on_paren,
    FunctionImpl* ret
) {
    bool outer = parent->shared->in_parameters;
    parent->shared->in_parameters = true;
    u32 saved_context = list_enter(parent, PARSING_PARAMETERS);
    bool success = parse_function_args_inner(
        parent, parse_abstract_and_new, parse_type_return_syntax, parse_ret_type, commit_on_paren, ret
    );
    if (success) shrink_list(vec_function_arg, &ret->args);
    list_leave(parent, saved_context);
    parent->shared->in_parameters = outer;
    return success;
}

static bool parse_function_args_inner(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_ret_type,
    bool commit_on_paren,
    FunctionImpl* ret
) {
    // Without `abstract`/`new` a parameter list starts at its `(` or type parameters (member names come here first)
    if (!parse_abstract_and_new && *parent->pos != '(' && *parent->pos != '<') return false;
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

    if (parse_generic_decl(&p, TYPE_PARAM_OF_FUNCTION, &ret->generic)) {
        hop_whitespace(&p);
        is_definitely_function = true;
    } else {
        ret->generic = (Generic){0};
    }

    Anchor args_start = anchor(&p);

    if (hop_char(&p, '(')) {
        hop_whitespace(&p);
        is_definitely_function |= commit_on_paren;

        while (*p.pos && *p.pos != ')') {
            if (hop_char(&p, ',')) {
                parse_error(&p, span_from_previous_char(&p), 1138, "Parameter declaration expected.");
            } else {
                InitializedBinding initializer = {0};
                const char* before_param = p.pos;
                u32 errors_before_param = p.shared->errors_attempted;
                bool explicit_optional = false;
                bool rest = false;
                Span rest_span = span_null;
                Anchor arg_start = anchor(&p);
                VecExpr decorators = {0};
                ignore parse_decorators(&p, &decorators);

                ModifierList modifier_list;
                parse_modifier_list(&p, true, &modifier_list);
                bool is_readonly_property = modifier_list.includes[MODIFIER_READONLY];
                bool is_override_property = modifier_list.includes[MODIFIER_OVERRIDE];
                bool is_parameter_property = is_readonly_property || modifier_list.includes[MODIFIER_PUBLIC] ||
                    modifier_list.includes[MODIFIER_PROTECTED] || modifier_list.includes[MODIFIER_PRIVATE] ||
                    modifier_list.includes[MODIFIER_OVERRIDE];
                uint8_t access = modifier_list.includes[MODIFIER_PRIVATE] ? ACCESS_MODIFIER_PRIVATE :
                    modifier_list.includes[MODIFIER_PROTECTED] ? ACCESS_MODIFIER_PROTECTED : ACCESS_MODIFIER_PUBLIC;
                check_modifiers(&p, &modifier_list, (ModifiedNode){ .kind = MODIFIED_PARAMETER, .parent = MODIFIED_IN_OTHER });
                modifier_list_drop(&modifier_list);
                hop_whitespace(&p);

                if (hop_start(&p, "...")) {
                    hop_whitespace(&p);
                    rest = true;
                    rest_span = span_from(arg_start);
                }

                if (parse_binding_content_or_private_name(&p, &initializer.binding, 18009,
                        "Private identifiers cannot be used as parameters.")) {
                } else if (hop_start_if_ends_on_word_break(&p, "this")) {
                    initializer.binding.tag = BINDING_IDENTIFIER;
                    initializer.binding.content_span = span_make(p.shared->source, p.pos - 4, p.pos);
                } else if (!is_definitely_function) {
                    vec_expr_drop(&decorators);
                    goto drop_and_fail;
                } else if (p.pos == before_param) {
                    // Nothing of a parameter: the list's recovery (tsc's parseDelimitedList)
                    vec_expr_drop(&decorators);
                    if (list_abort_or_skip(&p, PARSING_PARAMETERS)) break;
                    continue;
                } else {
                    // `(...)`, `(@dec)`: tsc's parseParameter with a missing name
                    parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
                    initializer.binding = (Binding){ .tag = BINDING_IDENTIFIER, .content_span = span_from_pos(&p) };
                }
                hop_whitespace(&p);

                Span question_span = span_null;
                if (hop_char(&p, '?')) {
                    explicit_optional = true;
                    question_span = span_from_previous_char(&p);
                    js_syntax_error(&p, question_span, 8009, "The '?' modifier can only be used in TypeScript files.");
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

                // tsc's skipParameterStart: only a name or pattern that parses without errors counts
                if (vec_function_arg_is_empty(&ret->args) && initializer.binding.has_explicit_type &&
                    p.shared->errors_attempted == errors_before_param
                ) {
                    is_definitely_function = true;
                }

                initializer.is_rest_parameter = rest;
                vec_function_arg_push(&ret->args, (FunctionArg){
                    .span = span_from(arg_start),
                    .rest_span = rest_span,
                    .initializer = initializer,
                    .explicit_optional = explicit_optional,
                    .question_span = question_span,
                    .rest = rest,
                    .decorators = decorators,
                    .is_parameter_property = is_parameter_property,
                    .is_readonly_property = is_readonly_property,
                    .is_override_property = is_override_property,
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

// Where an `await` here is (the resolve walk reports what isn't allowed). A class `static {}` block is its own error
static AwaitPlace parser_await_place(Parser const* p) {
    if (p->shared->in_static_block) return AWAIT_IN_STATIC_BLOCK;
    if (p->shared->in_async) return AWAIT_ALLOWED;
    return p->shared->function_depth > 0 ? AWAIT_IN_FUNCTION : AWAIT_AT_TOP_LEVEL;
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

typedef enum Tristate {
    TRISTATE_FALSE,
    TRISTATE_TRUE,
    TRISTATE_UNKNOWN,
} Tristate;

// tsc's isParenthesizedArrowFunctionExpressionWorker, at `(`, `<` or `async`: TRUE is an arrow function whatever
// follows (`() {` is missing its `=>`, `(...)` its parameter name), FALSE is never one (`().x` is a parenthesized
// expression missing its contents), UNKNOWN is up to trying
static Tristate arrow_function_tristate(Parser* p) {
    Tok first = tok_peek(p);
    if (tok_is(first, "async")) {
        first = tok_next(p, first);
        if (first.newline_before || !(tok_is(first, "(") || tok_is(first, "<"))) return TRISTATE_FALSE;
    }
    Tok second = tok_next(p, first);
    if (tok_is(first, "(")) {
        if (tok_is(second, ")")) {
            Tok third = tok_next(p, second);
            return tok_is(third, "=>") || tok_is(third, ":") || tok_is(third, "{") ? TRISTATE_TRUE : TRISTATE_FALSE;
        }
        if (tok_is(second, "[") || tok_is(second, "{")) return TRISTATE_UNKNOWN;
        if (tok_is(second, "...")) return TRISTATE_TRUE;
        Tok third = tok_next(p, second);
        if (tok_is_modifier(second) && !tok_is(second, "async") && tok_is_identifier(third)) {
            return tok_is(third, "as") ? TRISTATE_FALSE : TRISTATE_TRUE;
        }
        if (!tok_is_identifier(second) && !tok_is(second, "this")) return TRISTATE_FALSE;
        if (tok_is(third, ":")) return TRISTATE_TRUE;
        if (tok_is(third, "?")) {
            Tok fourth = tok_next(p, third);
            return tok_is(fourth, ":") || tok_is(fourth, ",") || tok_is(fourth, "=") || tok_is(fourth, ")") ?
                TRISTATE_TRUE : TRISTATE_FALSE;
        }
        if (tok_is(third, ",") || tok_is(third, "=") || tok_is(third, ")")) return TRISTATE_UNKNOWN;
        return TRISTATE_FALSE;
    }
    if (tok_is(first, "<")) {
        return tok_is_identifier(second) || tok_is(second, "const") ? TRISTATE_UNKNOWN : TRISTATE_FALSE;
    }
    return TRISTATE_UNKNOWN;
}

static bool parse_arrow_function_inner(Parser* parent, ExprCtx ctx, bool committed, FunctionImpl* ret);

// committed: arrow_function_tristate() said TRUE, so this parses as an arrow function with errors rather than failing
static bool parse_arrow_function(Parser* parent, ExprCtx ctx, FunctionFlags flags, bool committed, FunctionImpl* ret) {
    FunctionCtx saved = function_ctx_enter(parent, flags);
    bool success = parse_arrow_function_inner(parent, ctx, committed, ret);
    function_ctx_leave(parent, saved);
    if (success) {
        ret->is_async = flags.is_async;
        js_syntax_function(parent, ret, true);
    }
    return success;
}

static bool parse_arrow_function_inner(Parser* parent, ExprCtx ctx, bool committed, FunctionImpl* ret) {
    Parser p = parser(parent);
    FAIL_CACHE_HOOK(&p, FAIL_CACHE_ARROW_FUNCTION);
    Anchor a = anchor(&p);
    bool is_definitely_function = false;
    Span lone_arg_name;

    // In a JSX file `<T>(x) => x` is an element: a generic arrow function is written `<T,>` or `<T extends X>`
    if (*p.pos == '<' && p.shared->source->is_jsx && !jsx_generic_arrow_start(&p)) {
        return parse_fail(&p);
    }

    if (parse_function_args(&p, false, false, true, committed, ret)) {

        is_definitely_function = committed || (
            !vec_function_arg_is_empty(&ret->args) &&
            vec_function_arg_at(&ret->args, 0)->initializer.binding.has_explicit_type
        );

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

    // tsc's parseParenthesizedArrowFunctionExpression: `a ? (x): string => x : y` keeps its return type
    if (ctx.is_parsing_ternary && ret->ret_explicit) {
        Parser q = parser(&p);
        hop_whitespace(&q);
        bool colon = *q.pos == ':';
        parse_fail(&q);
        if (!colon) {
            function_impl_drop(ret);
            return parse_fail(&p);
        }
    }

    ret->has_body = true;
    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_function_header(Parser* parent, FunctionFlags flags, FunctionImpl* ret) {
    ignore flags;
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!parse_function_args(&p, false, false, true, true, ret)) {
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
        ret->asterisk = flags.asterisk;
        js_syntax_function(parent, ret, true);
    }
    return success;
}

static bool parse_non_arrow_function_inner(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!parse_function_args(&p, false, false, true, true, ret)) {
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
        flags.asterisk = span_from_previous_char(&p);
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
    } else if (*parent->pos != '`' && parse_literal(parent, &value)) { // (a template is no property name)
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
    // tsc parses modifiers in front of any member and reports them from the checker (a grammar error)
    ModifierList modifiers;
    parse_modifier_list(&p, true, &modifiers);
    hop_whitespace(&p);
    if (modifiers.includes[MODIFIER_ASYNC]) {
        must_be_method = true;
        flags.is_async = true;
    }

    // tsc's checkGrammarObjectLiteralExpression: every modifier but `async` (TS1184 on a method is checkGrammarMethod)
    bool report_modifiers_cannot_appear = false;
    for_each (i, vec_spanned_modifier, &modifiers.spanned) {
        if (i->tag == MODIFIER_ASYNC) continue;
        parse_error(&p, i->span, 1042, "'", i->span, "' modifier cannot be used here.");
        report_modifiers_cannot_appear = true;
    }
    Span first_modifier = vec_spanned_modifier_is_empty(&modifiers.spanned) ?
        span_null :
        vec_spanned_modifier_front(&modifiers.spanned)->span;
    modifier_list_drop(&modifiers);

    if (hop_member_keyword(&p, "async", false)) {
        must_be_method = true;
        flags.is_async = true;
    }
    if (hop_char(&p, '*')) {
        flags.asterisk = span_from_previous_char(&p);
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
    } else if (*p.pos == '#' && identifier_char_size(p.pos + 1, true)) {
        // A private name (tsc's parsePropertyName), the checker reports it (not a shorthand: `{ #x }` misses a `:`)
        Anchor name_anchor = anchor(&p);
        p.pos++;
        parse_identifier_or_keyword(&p, nullptr);
        item.name_span = span_from(name_anchor);
        item.name_string = cstr_from_span(item.name_span);
    } else {
        const char* name_start = p.pos;
        if (!parse_name_or_value(&p, &item.name_span, &item.name_string, &name_is_value)) {
            if (!flags.is_generator) return parse_fail(&p);
            // `{ *() {} }`: committed to a generator method by the `*`, the name is missing
            parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
            item.name_span = span_from_pos(&p);
            item.name_string = cstr_init();
        }
        item.name_is_value = name_is_value;
        name_is_identifier = !name_is_value;
        if (name_is_identifier) {
            // Keywords are fine as property names, but only real identifiers can be shorthand properties
            Parser probe = parser(&p);
            probe.pos = name_start;
            Span ignored;
            name_is_identifier = !yield_await_is_keyword_here(&probe) && parse_identifier(&probe, &ignored);
            parse_fail(&probe);
        }
    }
    hop_whitespace(&p);

    // tsc's parser takes a `?` and a `!` after any member name, its checker reports them (grammar errors, see
    // parse_file())
    if (*p.pos == '?' && p.pos[1] != '?' && p.pos[1] != '.') {
        parse_error(&p, span_make(p.shared->source, p.pos, p.pos + 1), 1162, "An object member cannot be declared optional.");
        p.pos++;
        hop_whitespace(&p);
    }
    if (*p.pos == '!' && p.pos[1] != '=') {
        parse_error(&p, span_make(p.shared->source, p.pos, p.pos + 1), 1255,
            "A definite assignment assertion '!' is not permitted in this context.");
        p.pos++;
        hop_whitespace(&p);
    }

    FunctionImpl func;
    if ((*p.pos == '(' || *p.pos == '<') && parse_non_arrow_function(&p, flags, &func)) {
        if (report_modifiers_cannot_appear && item.tag == OBJECT_ITEM_PROPERTY) {
            parse_error(&p, first_modifier, 1184, "Modifiers cannot appear here.");
        }
        if (item.tag == OBJECT_ITEM_PROPERTY) {
            item.tag = OBJECT_ITEM_METHOD;
        }
        func.full_span = span_from(a);
        item.value = expr_function(span_from(a), c_new(FunctionImpl, func));
    } else if (must_be_method && (flags.is_generator || item.tag != OBJECT_ITEM_PROPERTY)) {
        // `*` and `get`/`set` commit to a method (tsc's parseMethodDeclaration/parseAccessorDeclaration): the
        // parameter list is missing, a body may follow
        expected_token_error(&p, "(");
        func = (FunctionImpl){ .is_generator = flags.is_generator, .asterisk = flags.asterisk };
        if (parse_block(&p, false, &func.body)) func.has_body = true;
        if (item.tag == OBJECT_ITEM_PROPERTY) item.tag = OBJECT_ITEM_METHOD;
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
    } else if (name_is_identifier && !item.computed_key) {
        // tsc's parseObjectLiteralElement: an identifier not followed by `:` is a shorthand property, whatever comes
        // next (`{ a; b }`, `{ a b }`: the list reports the missing comma)
        item.tag = OBJECT_ITEM_SHORTHAND;
        item.value = expr_identifier((IdentifierExpr){ .name = item.name_span, .shorthand = true });
        if (*p.pos == '=' && p.pos[1] != '=' && p.pos[1] != '>') {
            // `{ a = 1 }` is only valid as a destructuring assignment target
            Anchor equals = anchor(&p);
            hop_char(&p, '=');
            item.shorthand_equals = span_from(equals);
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
        // Committed: `{ "" }`, `{ [e] }`, `{ this }` are `':' expected` and a missing value
        expect_char(&p, ':');
        hop_whitespace(&p);
        expect_expr(&p, expr_ctx_null, &item.value);
    }

    item.span = span_from(a);
    // (A shorthand property ends with its name, not with the whitespace looked past for a `:`)
    if (item.tag == OBJECT_ITEM_SHORTHAND && !item.shorthand_default) {
        item.span = span_make(item.span.source, span_start_ptr(item.span), span_end_ptr(item.name_span));
    }
    if (p.shared->source->is_js) {
        // The tags of a method, or of a property whose value is a function, are the function's
        VecJsDocTag tags = {0};
        FunctionImpl* function = item.tag == OBJECT_ITEM_SHORTHAND ? nullptr : jsdoc_function_of(&item.value);
        if ((function || item.tag == OBJECT_ITEM_PROPERTY || item.tag == OBJECT_ITEM_SHORTHAND) && jsdoc_before(&p, a.start, &tags)) {
            // A `@type` that isn't a written signature (`{F["tap"]}`) is the declared type, which a function value is
            // contextually typed by
            JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
            bool signature = tag && (tag->type.tag == TYPE_CALLABLE || tag->type.tag == TYPE_INTERFACE);
            bool accessor = item.tag == OBJECT_ITEM_GETTER || item.tag == OBJECT_ITEM_SETTER; // (The function's, see below)
            if (tag && !(function && signature) && !accessor) item.jsdoc_type = c_new(TypeNode, jsdoc_take_type(tag, nullptr));
            if (function && accessor) function->accessor = item.tag == OBJECT_ITEM_GETTER ? 1 : 2; // (Set again by the resolve walk)
            if (function) jsdoc_apply_function(&tags, function);
        }
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

    // tsc's parseDelimitedList with considerSemicolonAsDelimiter
    u32 saved_context = list_enter(&p, PARSING_OBJECT_LITERAL_MEMBERS);
    while (*p.pos && *p.pos != '}') {
        ObjectExprItem item;
        const char* start = p.pos;
        if (!parse_object_item(&p, &item)) {
            if (list_abort_or_skip(&p, PARSING_OBJECT_LITERAL_MEMBERS)) break;
            continue;
        }
        vec_object_expr_item_push(ret, item);
        hop_whitespace(&p);
        if (hop_char(&p, ',') || !*p.pos || *p.pos == '}') {
            hop_whitespace(&p);
            continue;
        }
        expect_char(&p, ',');
        if (*p.pos == ';' && p.whitespace != WHITESPACE_NEWLINE) p.pos++;
        if (p.pos == start) hop_token(&p);
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);
    shrink_list(vec_object_expr_item, ret);

    if (!expect_closing(&p, '}', open)) {
        // The literal ends with its last token, not the whitespace after it
        while (p.pos > open + 1 && is_js_ascii_space(p.pos[-1])) p.pos--;
    }

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

    // tsc's parseDelimitedList(ArrayLiteralMembers): an element at `,` is omitted
    u32 saved_context = list_enter(&p, PARSING_ARRAY_LITERAL_MEMBERS);
    while (*p.pos && *p.pos != ']') {
        const char* start = p.pos;
        Expr expr;
        if (*p.pos == ',') {
            expr = expr_undefined(span_from_pos(&p));
        } else if (!parse_expr_or_spread(&p, &expr)) {
            if (!list_is_element(&p, PARSING_ARRAY_LITERAL_MEMBERS, tok_peek(&p))) {
                if (list_abort_or_skip(&p, PARSING_ARRAY_LITERAL_MEMBERS)) break;
                continue;
            }
            expect_expr(&p, expr_ctx_null, &expr);
        }
        vec_expr_push(ret, expr);
        hop_whitespace(&p);
        if (hop_char(&p, ',')) {
            hop_whitespace(&p);
            continue;
        }
        if (!*p.pos || *p.pos == ']') break;
        expect_char(&p, ',');
        if (p.pos == start) hop_token(&p);
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);

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

    Anchor in_type_a = anchor(&p);
    expect_type(&p, TYPE_PRECEDENCE_NULL, &ret->key.mapped.in_type);
    ret->key.mapped.in_type_span = span_from(in_type_a);
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

// tsc's isUnambiguouslyIndexSignature, at a `[`: `[x:`, `[x,`, `[x?:`, `[x?,`, `[x?]`, `[...`, `[]`, `[public x`
static bool is_index_signature_start(Parser* p) {
    if (*p->pos != '[') return false;
    Tok t = tok_next(p, tok_peek(p));
    if (tok_is(t, "...") || tok_is(t, "]")) return true;
    if (tok_is_modifier(t)) {
        t = tok_next(p, t);
        if (tok_is_identifier(t)) return true;
    } else if (!tok_is_identifier(t)) {
        return false;
    } else {
        t = tok_next(p, t);
    }
    if (tok_is(t, ":") || tok_is(t, ",")) return true;
    if (!tok_is(t, "?")) return false;
    t = tok_next(p, t);
    return tok_is(t, ":") || tok_is(t, ",") || tok_is(t, "]");
}

// An index signature (tsc's parseIndexSignatureDeclaration): once is_index_signature_start() says so, the brackets are
// a parameter list whatever is in them. The parameter grammar errors of tsc's checkGrammarIndexSignatureParameters are
// reported here (grammar codes, see is_grammar_code()); the type ones and TS1021 by interface_member_typecheck()
static bool parse_index_signature(Parser* parent, InterfaceMember* ret) {
    if (!is_index_signature_start(parent)) return false;
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    *ret = (InterfaceMember){ .key.tag = INTERFACE_KEY_INDEX };
    p.pos++;
    hop_whitespace(&p);

    u32 saved_context = list_enter(&p, PARSING_PARAMETERS);
    u32 count = 0;
    Span trailing_comma = span_null;
    Span first_rest = span_null, first_modifier_name = span_null, first_question = span_null;
    bool first_has_initializer = false, first_has_type = false;
    while (*p.pos && *p.pos != ']') {
        const char* before = p.pos;
        Anchor param_start = anchor(&p);
        VecExpr decorators = {0};
        ignore parse_decorators(&p, &decorators);
        vec_expr_drop(&decorators);
        ModifierList modifiers;
        parse_modifier_list(&p, true, &modifiers);
        bool has_modifiers = vec_spanned_modifier_size(&modifiers.spanned) != 0;
        bool is_property = modifiers.includes[MODIFIER_READONLY] || modifiers.includes[MODIFIER_PUBLIC] ||
            modifiers.includes[MODIFIER_PROTECTED] || modifiers.includes[MODIFIER_PRIVATE] ||
            modifiers.includes[MODIFIER_OVERRIDE];
        modifier_list_drop(&modifiers);
        hop_whitespace(&p);

        Span rest = span_null;
        if (hop_start(&p, "...")) {
            rest = span_make(p.shared->source, p.pos - 3, p.pos);
            hop_whitespace(&p);
        }
        Span name;
        if (!parse_identifier(&p, &name)) {
            if (p.pos == before) {
                if (list_abort_or_skip(&p, PARSING_PARAMETERS)) break;
                continue;
            }
            parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
            name = span_from_pos(&p);
        }
        hop_whitespace(&p);
        Span question = span_null;
        if (hop_char(&p, '?')) {
            question = span_from_previous_char(&p);
            hop_whitespace(&p);
        }
        TypeNode type = {0};
        bool has_type = false;
        if (hop_char(&p, ':')) {
            hop_whitespace(&p);
            Anchor type_start = anchor(&p);
            intptr_t mark = error_mark(&p);
            expect_type(&p, TYPE_PRECEDENCE_NULL, &type);
            js_syntax_skip_since(&p, mark);
            js_syntax_error(&p, span_from(type_start), 8010, "Type annotations can only be used in TypeScript files.");
            has_type = true;
            hop_whitespace(&p);
        } else if (span_valid(rest)) {
            type = type_array(c_new(TypeNode, type_any), false);
        }
        Expr initializer;
        bool has_initializer = parse_initialization(&p, expr_ctx_null, &initializer);
        if (has_initializer) {
            expr_drop(&initializer);
            hop_whitespace(&p);
        }

        if (count == 0) {
            ret->key.index.name = name;
            ret->key.index.decl = span_make(p.shared->source, param_start.start, span_end_ptr(name));
            ret->key.index.type = type;
            first_rest = rest;
            if (has_modifiers) first_modifier_name = name;
            if (is_property) ret->key.index.parameter_property = span_from(param_start);
            first_question = question;
            first_has_initializer = has_initializer;
            first_has_type = has_type;
        } else {
            vec_span_push(&ret->key.index.more_names, name);
            vec_span_push(&ret->key.index.more_decls, span_make(p.shared->source, param_start.start, span_end_ptr(name)));
            vec_type_push(&ret->key.index.more_types, type);
        }
        count++;

        trailing_comma = span_null;
        if (*p.pos == ']') break;
        if (hop_char(&p, ',')) {
            trailing_comma = span_from_previous_char(&p);
            hop_whitespace(&p);
        } else {
            expect_char(&p, ',');
            hop_whitespace(&p);
        }
    }
    list_leave(&p, saved_context);
    expect_char(&p, ']');
    hop_whitespace(&p);

    Span name = ret->key.index.name;
    bool reported = true;
    if (count != 1) {
        // (At the whole signature for `[]`: the span is finished below)
        if (count) parse_error(&p, name, 1096, "An index signature must have exactly one parameter.");
    } else {
        if (span_valid(trailing_comma)) {
            parse_error(&p, trailing_comma, 1025, "An index signature cannot have a trailing comma.");
        }
        if (span_valid(first_rest)) {
            parse_error(&p, first_rest, 1017, "An index signature cannot have a rest parameter.");
        } else if (span_valid(first_modifier_name)) {
            parse_error(&p, name, 1018, "An index signature parameter cannot have an accessibility modifier.");
        } else if (span_valid(first_question)) {
            parse_error(&p, first_question, 1019, "An index signature parameter cannot have a question mark.");
        } else if (first_has_initializer) {
            parse_error(&p, name, 1020, "An index signature parameter cannot have an initializer.");
        } else if (!first_has_type) {
            parse_error(&p, name, 1022, "An index signature parameter must have a type annotation.");
        } else {
            reported = false;
        }
    }
    ret->key.index.valid = count == 1 && first_has_type;
    ret->key.index.grammar_reported = reported;
    ret->key.span = span_from(a);

    if (hop_char(&p, ':')) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, &ret->type);
        ret->key.index.has_value_type = true;
    }

    // The node ends after the member's `;` or `,` (tsc's parseTypeMemberSemicolon)
    Span node = span_from(a);
    {
        Parser q = parser(&p);
        hop_whitespace(&q);
        if (*q.pos == ';' || *q.pos == ',') node = span_make(q.shared->source, a.start, q.pos + 1);
        parse_fail(&q);
    }
    ret->key.index.node = node;
    if (count == 0) parse_error(&p, node, 1096, "An index signature must have exactly one parameter.");

    return parse_success(&p);
}

static bool parse_expr_interface_member(Parser* parent, bool is_type, FunctionFlags flags, InterfaceMember* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    *ret = (InterfaceMember){ .key.tag = INTERFACE_KEY_EXPR };

    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Expr expr;
    if (!parse_expr(&p, expr_ctx_null, &expr)) {
        return parse_fail(&p);
    }
    ret->key.expr.owned = c_new(Expr, expr);
    ret->key.expr.ref = ret->key.expr.owned;
    hop_whitespace(&p);

    expect_char(&p, ']');
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
        expect_type(&p, TYPE_PRECEDENCE_NULL, &ret->type);
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

// The `[k]` of a class member
static bool parse_class_computed_key(Parser* parent, Expr** ret) {
    Parser p = parser(parent);
    if (*p.pos != '[' || is_index_signature_start(&p)) return parse_fail(&p);
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
// `@private`, `@protected`, `@readonly` of the host's own comment
static uint8_t jsdoc_modifiers(VecJsDocTag const* tags) {
    uint8_t ret = 0;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->earlier) continue;
        if (i->kind == JSDOC_TAG_PRIVATE) ret |= JSDOC_MODIFIER_PRIVATE;
        if (i->kind == JSDOC_TAG_PROTECTED) ret |= JSDOC_MODIFIER_PROTECTED;
        if (i->kind == JSDOC_TAG_READONLY) ret |= JSDOC_MODIFIER_READONLY;
        if (i->kind == JSDOC_TAG_OVERRIDE) ret |= JSDOC_MODIFIER_OVERRIDE;
    }
    return ret;
}

static void jsdoc_apply_class_item(Parser* p, const char* start, ClassItem* item) {
    VecJsDocTag tags = {0};
    if (!jsdoc_before(p, start, &tags)) return;
    // The modifier tags add to the written modifiers (tsc's getEffectiveModifierFlags)
    uint8_t modifiers = jsdoc_modifiers(&tags);
    if (item->access == ACCESS_MODIFIER_PUBLIC) {
        if (modifiers & JSDOC_MODIFIER_PRIVATE) item->access = ACCESS_MODIFIER_PRIVATE;
        else if (modifiers & JSDOC_MODIFIER_PROTECTED) item->access = ACCESS_MODIFIER_PROTECTED;
    }
    if (item->tag == CLASS_ITEM_FIELD && (modifiers & JSDOC_MODIFIER_READONLY)) item->field.is_readonly = true;
    if (modifiers & JSDOC_MODIFIER_OVERRIDE) item->is_override = true;
    if (span_valid(class_item_private_name(item))) {
        // (tsc's checkJSDocAccessibilityModifiers: the whole tag, up to the next one or the end of the comment)
        for_each (i, vec_jsdoc_tag, &tags) {
            if (i->earlier || (i->kind != JSDOC_TAG_PUBLIC && i->kind != JSDOC_TAG_PRIVATE && i->kind != JSDOC_TAG_PROTECTED)) continue;
            const char* at = i->tag_name.sv.buf - 1;
            const char* end = i + 1 < tags.data + tags.size && i[1].comment == i->comment ? i[1].tag_name.sv.buf - 1 : strstr(at, "*/");
            if (!end) continue;
            jsdoc_error(p, span_make(p->shared->source, at, end), 18010,
                "An accessibility modifier cannot be used with a private identifier.");
        }
    }
    switch (item->tag) {
        case CLASS_ITEM_CONSTRUCTOR: {
            // (tsc's checkGrammarConstructorTypeAnnotation: the effective return type node)
            JsDocTag* return_tag = jsdoc_find_typed(&tags, JSDOC_TAG_RETURNS);
            if (return_tag && !item->constructor.func.ret_explicit) {
                jsdoc_error(p, return_tag->type_span, 1093, "Type annotation cannot appear on a constructor declaration.");
            }
            jsdoc_apply_function(&tags, &item->constructor.func);
        }   break;
        case CLASS_ITEM_METHOD:
            item->method.func.accessor = (uint8_t)item->method.accessor; // (Set again by the resolve walk)
            jsdoc_apply_function(&tags, &item->method.func);
            break;
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
    intptr_t mark = error_mark(&p);

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
        function_flags.asterisk = span_from_previous_char(&p);
        hop_whitespace(&p);
    }

    Span name_span;
    cstr name_string;
    bool name_is_value;
    Expr* computed_key = nullptr;
    Anchor name_anchor = anchor(&p);
    if (hop_private_name(&p)) {
        // Private name, the `#` is part of the name (in a type too, TS18016 from the checker)
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

    // A static block is no yield context (tsc's parseClassStaticBlockBody)
    bool outer_static_block = p.shared->in_static_block, outer_generator = p.shared->in_generator;
    p.shared->in_static_block = true;
    p.shared->in_generator = false;
    bool static_block = !is_type && !name_is_value && span_equals(name_span, "static") &&
        parse_block(&p, false, &ret->static_block);
    p.shared->in_static_block = outer_static_block;
    p.shared->in_generator = outer_generator;
    if (static_block) {
        check_modifiers(&p, &modifiers, (ModifiedNode){ .kind = MODIFIED_CLASS_STATIC_BLOCK, .parent = MODIFIED_IN_CLASS });
        ret->tag = CLASS_ITEM_STATIC_BLOCK;
    } else if (span_valid(name_span)) {
        if (span_valid(name_span) && cstr_is_empty(&name_string)) {
            name_string = cstr_from_span(name_span);
        }

        bool optional = false;
        Span question = span_null;
        Span definite = span_null;
        if (hop_char(&p, '?')) {
            question = span_from_previous_char(&p);
            hop_whitespace(&p);
            optional = true;
            if (!is_type) js_syntax_error(&p, question, 8009, "The '?' modifier can only be used in TypeScript files.");
        } else if (!is_type && *p.pos == '!' && p.pos[1] != '=') {
            // Definite assignment assertion: see the field's grammar check below
            definite = span_make(p.shared->source, p.pos, p.pos + 1);
            p.pos++;
            hop_whitespace(&p);
        }

        FunctionImpl func;
        ModifiedNode modified = {
            .parent = is_type ? MODIFIED_IN_TYPE : MODIFIED_IN_CLASS,
            .in_abstract_class = is_abstract,
            .private_name = *span_start_ptr(name_span) == '#',
            .name = name_span,
        };
        if (!is_type && !name_is_value && span_equals(name_span, "constructor")) {
            modified.kind = MODIFIED_CONSTRUCTOR;
            check_modifiers(&p, &modifiers, modified);
            Span keyword = name_span;
            // From the modifiers to the keyword (tsc's getErrorSpanForNode of a constructor)
            name_span = span_from(a);
            if (span_valid(keyword) && span_end_ptr(keyword) >= span_start_ptr(name_span)) {
                name_span = span_make(name_span.source, span_start_ptr(name_span), span_end_ptr(keyword));
            }
            if (!parse_function_fn(&p, function_flags_null, &func)) {
                parse_error(&p, span_from_token(&p), TS_CODE_NONE, "constructor expected");
                func = (FunctionImpl){0};
            } else if (!func.has_body && reports_js_syntax(&p)) {
                js_syntax_skip_since(&p, mark);
                js_syntax_error(&p, keyword, 8017, "Signature declarations can only be used in TypeScript files.");
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
            modified.kind = is_getter ? MODIFIED_GET_ACCESSOR :
                is_setter ? MODIFIED_SET_ACCESSOR :
                is_type ? MODIFIED_METHOD_SIGNATURE :
                MODIFIED_METHOD;
            check_modifiers(&p, &modifiers, modified);
            if (is_type && modified.kind == MODIFIED_METHOD_SIGNATURE) {
                js_syntax_function(&p, &func, false);
            } else if (!is_type && !is_getter && !is_setter && !func.has_body && reports_js_syntax(&p)) {
                js_syntax_skip_since(&p, mark);
                js_syntax_error(&p, name_span, 8017, "Signature declarations can only be used in TypeScript files.");
            }
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

            modified.kind = is_type ? MODIFIED_PROPERTY_SIGNATURE : MODIFIED_PROPERTY;
            bool modifier_reported = check_modifiers(&p, &modifiers, modified);
            if (!modifier_reported && modifiers.includes[MODIFIER_ACCESSOR] && optional) {
                // (tsc's checkGrammarProperty)
                parse_error(&p, question, 1276, "An 'accessor' property cannot be declared optional.");
            }
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

            Binding binding = {0};
            if (parse_binding_type_ex(&p, &binding, !is_type)) {
                ret->field.explicit_type = binding.explicit_type;
                ret->field.explicit_type_span = binding.explicit_type_span;
                ret->field.has_explicit_type = binding.has_explicit_type;
                is_definitely_field = true;
                hop_whitespace(&p);
            }

            // An initializer is outside the yield and await contexts, a static block's too (tsc's parsePropertyDeclaration)
            FunctionCtx outer = { .in_async = p.shared->in_async, .in_generator = p.shared->in_generator,
                .in_static_block = p.shared->in_static_block, .in_parameters = p.shared->in_parameters };
            p.shared->in_async = p.shared->in_generator = p.shared->in_static_block = false;
            bool initialized = parse_initialization_or_set_to_default(&p, expr_ctx_null, &ret->field.value);
            p.shared->in_async = outer.in_async;
            p.shared->in_generator = outer.in_generator;
            p.shared->in_static_block = outer.in_static_block;
            if (initialized) {
                mark_named_evaluation(&ret->field.value, ret->field.computed_key);
                is_definitely_field = true;
                hop_whitespace(&p);
            }
            // tsc's checkGrammarProperty
            if (span_valid(definite) && !modifier_reported && (!ret->field.has_explicit_type || initialized ||
                p.shared->in_ambient || ret->field.is_declare || ret->field.is_static || ret->field.is_abstract)
            ) {
                if (initialized) {
                    parse_error(&p, definite, 1263, "Declarations with initializers cannot also have definite assignment assertions.");
                } else if (!ret->field.has_explicit_type) {
                    parse_error(&p, definite, 1264, "Declarations with definite assignment assertions must also have type annotations.");
                } else {
                    parse_error(&p, definite, 1255, "A definite assignment assertion '!' is not permitted in this context.");
                }
            }

            if (p.whitespace != WHITESPACE_NEWLINE) {
                switch (*p.pos) {
                    case ';': case ',': case '}': case '\0': break;
                    case '@':
                        // tsc's parseSemicolonAfterPropertyName: `public @dec m()` is a property `public`
                        if (!is_type) {
                            parse_error(&p, span_make(p.shared->source, p.pos, p.pos + 1), 1436,
                                "Decorators must precede the name and all keywords of property declarations.");
                            break;
                        }
                        // fallthrough
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
        ret->is_override = modifiers.includes[MODIFIER_OVERRIDE];
        ret->access = modifiers.includes[MODIFIER_PRIVATE] ?
            ACCESS_MODIFIER_PRIVATE :
            (modifiers.includes[MODIFIER_PROTECTED] ?
                ACCESS_MODIFIER_PROTECTED :
                ACCESS_MODIFIER_PUBLIC);
    } else {
        FunctionImpl func;
        if (parse_mapped_interface_member(&p, &ret->interface_memeber) ||
            parse_index_signature(&p, &ret->interface_memeber) ||
            parse_expr_interface_member(&p, is_type, function_flags, &ret->interface_memeber)
        ) {
            if (ret->interface_memeber.key.tag != INTERFACE_KEY_MAPPED) {
                check_modifiers(&p, &modifiers, (ModifiedNode){
                    .kind = ret->interface_memeber.key.tag == INTERFACE_KEY_INDEX ? MODIFIED_INDEX_SIGNATURE :
                        ret->interface_memeber.key.tag == INTERFACE_KEY_CALLABLE ? MODIFIED_METHOD_SIGNATURE :
                        MODIFIED_PROPERTY_SIGNATURE,
                    .parent = is_type ? MODIFIED_IN_TYPE : MODIFIED_IN_CLASS,
                });
            }
            ret->tag = CLASS_ITEM_INTERFACE_MEMBER;
            ret->interface_memeber.declarations = vec_span_single(span_from(a));
            ret->interface_memeber.readonly = modifiers.includes[MODIFIER_READONLY];
            ret->interface_member_static = modifiers.includes[MODIFIER_STATIC];
            // `readonly [K in T]: X` (the modifier went to the list before the key was known to be mapped)
            InterfaceMember* member = &ret->interface_memeber;
            if (is_type && (is_getter || is_setter) && member->key.tag == INTERFACE_KEY_EXPR && member->is_method &&
                member->type.tag == TYPE_CALLABLE
            ) {
                // `get [k](): T` in a type is a property of type T, `set [k](v: T)` one of type T to write
                FunctionImpl* accessor = member->type.callable->function;
                TypeNode type = type_any;
                if (is_getter) {
                    if (accessor->ret_explicit) type = type_move(&accessor->ret);
                } else if (vec_function_arg_size(&accessor->args)) {
                    FunctionArg* arg = &accessor->args.data[0];
                    if (arg->initializer.binding.has_explicit_type) type = type_move(&arg->initializer.binding.explicit_type);
                }
                type_drop(&member->type);
                member->type = type;
                member->is_method = false;
                member->computed_accessor = is_getter ? 1 : 2;
            }
            if (member->key.tag == INTERFACE_KEY_MAPPED && member->readonly && !member->key.mapped.readonly) {
                member->key.mapped.readonly = 1;
            }
        } else if (parse_function_args(&p, true, false, true, false, &func)) {
            check_modifiers(&p, &modifiers, (ModifiedNode){
                .kind = MODIFIED_METHOD_SIGNATURE,
                .parent = is_type ? MODIFIED_IN_TYPE : MODIFIED_IN_CLASS,
            });
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

// A class or type member where tsc's parseList would try one: its isClassMemberStart/isTypeMemberStart (`class C2 {`
// or `return 1;` in a class body aren't members, where parse_class_item() would make a field of the keyword). The
// predicate is only asked about members that parsed with errors, which keeps it off the path of valid code
static bool parse_list_member(Parser* parent, ParsingContext ctx, bool is_type, bool is_abstract, ClassItem* ret) {
    Parser p = parser(parent);
    u32 errors_before = p.shared->errors_attempted;
    if (!parse_class_item(&p, is_type, is_abstract, ret)) return parse_fail(&p);
    if (p.shared->errors_attempted != errors_before && !list_is_element(&p, ctx, tok_at(&p, parent->pos, false))) {
        class_item_drop(ret);
        return parse_fail(&p);
    }
    return parse_success(&p);
}

// abstract: the statement's modifiers had `abstract` (an expression's `abstract class` is taken here)
static bool parse_class_decl(Parser* parent, bool abstract, ClassDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    VecExpr decorators = {0};
    ignore parse_decorators(&p, &decorators);

    bool is_abstract = abstract;
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
        starts_with_implements_clause = identifier_char_size(c, true);
    }
    // (tsc's isBindingIdentifier: `class void {}` has no name, and then no `{`)
    const char* name_start = p.pos;
    if (starts_with_followed_by_word_break(&p, "extends") ||
        starts_with_implements_clause ||
        !parse_binding_identifier(&p)
    ) {
        ret->name = span_null;
    } else {
        ret->name = span_make(p.shared->source, name_start, p.pos);
    }
    hop_whitespace(&p);

    if (parse_generic_decl(&p, TYPE_PARAM_OF_CLASS, &ret->generic)) {
        hop_whitespace(&p);
        if (!generic_empty(&ret->generic)) {
            Span span = span_between(vec_generic_item_front(&ret->generic.items)->full_span,
                vec_generic_item_back(&ret->generic.items)->full_span);
            js_syntax_error(&p, span, 8004, "Type parameter declarations can only be used in TypeScript files.");
        }
    } else {
        ret->generic = (Generic){0};
    }

    HeritageClauses heritage = { .is_class = true, .base = expr_implicit_any_no_span };
    parse_heritage_clauses(&p, &heritage);
    ret->extends = heritage.base;
    ret->extends_clause = heritage.base_clause;
    ret->implements = heritage.types;
    heritage_take_invalid(&heritage, &ret->invalid_implements, &ret->invalid_implements_checked);
    ret->unchecked_extends = heritage.unchecked_exprs;
    ret->unchecked_implements = heritage.unchecked_types;

    if (!expect_char(&p, '{')) {
        ret->full_span = span_from(a);
        return parse_success(&p);
    }
    hop_whitespace(&p);

    ClassItem item;
    u32 saved_context = list_enter(&p, PARSING_CLASS_MEMBERS);
    while (*p.pos && *p.pos != '}') {
        if (hop_char(&p, ';')) {
            hop_whitespace(&p);
            continue;
        }
        if (!parse_list_member(&p, PARSING_CLASS_MEMBERS, false, is_abstract, &item)) {
            if (list_abort_or_skip(&p, PARSING_CLASS_MEMBERS)) break;
            continue;
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
    list_leave(&p, saved_context);
    expect_char(&p, '}');
    shrink_list(vec_class_item, &ret->items);

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_call_args(Parser* parent, VecExpr* args) {
    Parser p = parser(parent);

    if (!hop_start(&p, "(")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    // tsc's parseDelimitedList(ArgumentExpressions)
    *args = vec_expr_init();
    u32 saved_context = list_enter(&p, PARSING_ARGUMENT_EXPRESSIONS);
    while (*p.pos && *p.pos != ')' && *p.pos != ';') {
        const char* start = p.pos;
        Expr arg = {0};
        if (!parse_expr_or_spread(&p, &arg)) {
            // (The predicate only after the parser said no: it's what the list does on valid code that costs)
            if (!list_is_element(&p, PARSING_ARGUMENT_EXPRESSIONS, tok_peek(&p))) {
                if (list_abort_or_skip(&p, PARSING_ARGUMENT_EXPRESSIONS)) break;
                continue;
            }
            expect_expr(&p, expr_ctx_null, &arg);
        }
        vec_expr_push(args, arg);
        hop_whitespace(&p);
        if (hop_char(&p, ',')) {
            hop_whitespace(&p);
            continue;
        }
        if (!*p.pos || *p.pos == ')' || *p.pos == ';') break;
        expect_char(&p, ',');
        if (p.pos == start) hop_token(&p);
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);
    expect_char(&p, ')');
    shrink_list(vec_expr, args);

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
    if ((*p.pos >= '0' && *p.pos <= '9') || *p.pos == '.') {
        return parse_fail(&p); // `a ? .5 : b`, `01.0`: a number; `@dec ...rest`: a spread
    }
    const char* after_dot = p.pos;
    hop_whitespace(&p);

    // Committed after the dot (tsc's parseRightSideOfDot): a missing name is TS1003 and an empty name. `a.` followed
    // by two words on the next line is missing its name too, reported right after the dot
    Span prop;
    Tok t = tok_peek(&p);
    if (t.newline_before && tok_is_word(t) && tok_is_word(tok_next(&p, t)) && !tok_next(&p, t).newline_before) {
        prop = span_make(p.shared->source, after_dot, after_dot);
        parse_error(&p, prop, 1003, "Identifier expected.");
    } else if (*p.pos == '#' && p.pos[1] != '!') {
        // Private name, the `#` is part of the member's name
        Anchor name_anchor = anchor(&p);
        hop_private_name(&p);
        prop = span_from(name_anchor);
    } else if (!parse_identifier_or_keyword(&p, &prop)) {
        parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
        prop = span_from_pos(&p);
    }

    *ret = expr_literal(prop, value_string(cstr_from_span(prop)));
    *optional_chain_ret = optional_chain;
    return parse_success(&p);
}

static bool parse_ternary_expr(Parser* parent, ExprCtx ctx, Expr* ret_yes, Expr* ret_no) {
    Parser p = parser(parent);
    ctx.min_precedence = ternary_precedence - 1;
    bool parent_ternary = ctx.is_parsing_ternary;
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

    // The false branch allows return types as much as the conditional itself does
    ctx.is_parsing_ternary = parent_ternary;
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
    const char* after_bracket = p.pos;
    hop_whitespace(&p);

    if (*p.pos == ']') {
        // `new T[]`: tsc's parseElementAccessExpressionRest, a missing argument reported where the `]` token's
        // trivia starts
        Span missing = span_make(p.shared->source, after_bracket, after_bracket);
        parse_error(&p, missing, 1011, "An element access expression should take an argument.");
        *ret = expr_implicit_any(missing);
        p.pos++;
        return parse_success(&p);
    }
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

    Anchor type_start = anchor(&p);
    expect_type(&p, TYPE_PRECEDENCE_NULL, ret);
    if (*is_satisfies_ret) {
        js_syntax_error(&p, span_from(type_start), 8037, "Type satisfaction expressions can only be used in TypeScript files.");
    } else {
        js_syntax_error(&p, span_from(type_start), 8016, "Type assertion expressions can only be used in TypeScript files.");
    }

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

static bool is_exports_alias_name(Parser const* p, Expr const* e);

static void note_commonjs_start(ParserShared* shared, const char* start) {
    if (!shared->commonjs_start || start < shared->commonjs_start) shared->commonjs_start = start;
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
    // Only a left-hand side expression is assigned to (tsc's parseAssignmentExpressionOrHigher: isLeftHandSideExpression);
    // `await x = 1`, `a + b = c`, `x as T = y` end before the `=`
    if (precedence == binary_op_get_precedence(BINARY_OP_EQUAL) &&
        left_and_ret->outer_span.sv.size == left_and_ret->inner_span.sv.size &&
        ((left_and_ret->tag == EXPR_UNARY_OP && left_and_ret->unary_op->op != UNARY_OP_RIGHT_BANG) ||
            left_and_ret->tag == EXPR_BINARY_OP ||
            // (A JSDoc `/** @type {T} */ (x)` is tsc's ParenthesizedExpression: its type comes before the expression)
            (left_and_ret->tag == EXPR_AS_TYPE &&
                span_start_ptr(left_and_ret->as_type->keyword_span) > span_start_ptr(left_and_ret->outer_span)))) {
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
    if (op == BINARY_OP_EQUAL && p.shared->source->is_js && (expr_commonjs_target(left_and_ret, nullptr) ||
        (!p.shared->function_depth && span_valid(expr_static_property_name(left_and_ret)) &&
            is_exports_alias_name(&p, &left_and_ret->property->expr)))
    ) {
        p.shared->saw_commonjs = true;
        note_commonjs_start(p.shared, span_start_ptr(left_and_ret->outer_span));
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
    } else if (parent->shared->in_async || parent->shared->in_static_block) {
        return true; // (A static block is an await context: tsc's parseClassStaticBlockBody)
    }
    const char* c = parent->pos + strlen("await");
    while (*c == ' ' || *c == '\t') {
        c++;
    }
    if (identifier_char_size(c, true) || is_number_start_char(*c) || *c == '"' || *c == '\'' || *c == '`') {
        return true;
    }
    // At the top level an operand can start with anything that can't continue an expression that began with an
    // identifier called await
    return parent->shared->function_depth == 0 && (*c == '(' || *c == '[' || *c == '{' || *c == '!' || *c == '~');
}

// tsc's isYieldExpression outside a yield context: `yield` followed by an identifier, keyword or literal on the same line
// is a yield expression (TS1163 from the checker), anything else makes it an identifier
static bool yield_is_operator_outside_generator(Parser* parent) {
    if (!starts_with_followed_by_word_break(parent, "yield")) return false;
    const char* c = parent->pos + strlen("yield");
    while (*c == ' ' || *c == '\t') c++;
    if (c[0] == '/' && (c[1] == '/' || c[1] == '*')) return false; // (a comment: be conservative)
    return identifier_char_size(c, true) || is_number_start_char(*c) || (unsigned char)*c >= 0x80 || *c == '"' ||
        *c == '\'' || *c == '`';
}

static bool parse_left_unary_op(Parser* parent, UnaryOp* op) {
    if (hop_start(parent, "!")) {
        *op = UNARY_OP_LEFT_BANG;
    } else if (hop_start_if_ends_on_word_break(parent, "delete")) {
        *op = UNARY_OP_DELETE;
    } else if (await_is_operator(parent) && hop_start_if_ends_on_word_break(parent, "await")) {
        *op = UNARY_OP_AWAIT;
    } else if ((parent->shared->in_generator || yield_is_operator_outside_generator(parent)) &&
        hop_start_if_ends_on_word_break(parent, "yield")) {
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
    }
    AwaitPlace await_place = op == UNARY_OP_AWAIT && !p.shared->in_parameters ? parser_await_place(&p) : AWAIT_ALLOWED;
    // tsc reads a top-level `await (x)` as a call of `await`, which a module reparses as an expression in an await
    // context (reparseTopLevelAwait): no grammar errors. `await x` is an await expression from the start
    char c = *p.pos;
    if (await_place == AWAIT_AT_TOP_LEVEL && !(identifier_char_size(p.pos, true) || is_number_start_char(c) || c == '"' ||
        c == '\'' || c == '`')) {
        await_place = AWAIT_ALLOWED;
    }

    Expr expr;
    ctx.min_precedence = unary_op_get_precedence(op);
    // The operand of a prefix `++`/`--` is a left-hand side expression, no unary one (tsc's parseUpdateExpression):
    // `++-x`, `++await x` are missing it
    if (op == UNARY_OP_LEFT_PLUS_PLUS || op == UNARY_OP_LEFT_MINUS_MINUS) ctx.min_precedence++;
    bool bare_yield = false;
    if (op == UNARY_OP_YIELD) {
        // tsc's parseYieldExpression(): an operand if one starts on the same line (isStartOfExpression)
        char c = *p.pos;
        bool starts_expression = identifier_char_size(p.pos, true) || is_number_start_char(c) || (unsigned char)c >= 0x80 ||
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
            .await_place = (uint8_t)await_place,
            .outside_generator = (op == UNARY_OP_YIELD || op == UNARY_OP_YIELD_STAR) && !p.shared->in_generator,
            .expr = expr,
        })
    );
    return parse_success(&p);
}

static inline bool is_digit_of_base(char c, int base) {
    int v = hex_digit_value(c);
    return v >= 0 && v < base;
}

// The digits of `base` at c and the numeric separators between them (tsc's scanNumberFragment, scanHexDigits and
// scanBinaryOrOctalDigits, which agree on where a `_` may be): appends the digits to `digits`, returns their end
static const char* scan_digit_fragment(Parser* p, const char* c, int base, cstr* digits) {
    bool allow_separator = false;
    bool previous_separator = false;
    for (;; c++) {
        if (*c == '_') {
            if (allow_separator) {
                allow_separator = false;
                previous_separator = true;
            } else if (previous_separator) {
                parse_error(p, span_make(p->shared->source, c, c + 1), 6189,
                    "Multiple consecutive numeric separators are not permitted.");
            } else {
                parse_error(p, span_make(p->shared->source, c, c + 1), 6188, "Numeric separators are not allowed here.");
            }
        } else if (is_digit_of_base(*c, base)) {
            allow_separator = true;
            previous_separator = false;
            cstr_append_n(digits, c, 1);
        } else {
            break;
        }
    }
    if (c[-1] == '_') {
        parse_error(p, span_make(p->shared->source, c - 1, c), 6188, "Numeric separators are not allowed here.");
    }
    return c;
}

static double digits_value(csview digits, int base) {
    double value = 0;
    for (intptr_t i = 0; i < digits.size; i++) value = value * base + hex_digit_value(digits.buf[i]);
    return value;
}

// After a numeric literal that ends at c (tsc's checkForIdentifierStartAfterNumericLiteral): an identifier right
// after it is TS1351 and a token of its own, but a lone `n` is a bigint suffix in the wrong place (consumed)
static const char* check_identifier_after_number(Parser* p, const char* start, const char* c, bool scientific) {
    int size = identifier_char_size(c, true);
    if (!size) return c;
    const char* id_end = c + size;
    while ((size = identifier_char_size(id_end, false))) id_end += size;
    if (id_end - c == 1 && *c == 'n') {
        Span span = span_make(p->shared->source, start, id_end);
        if (scientific) parse_error(p, span, 1352, "A bigint literal cannot use exponential notation.");
        else parse_error(p, span, 1353, "A bigint literal must be an integer.");
        return id_end;
    }
    parse_error(p, span_make(p->shared->source, c, id_end), 1351,
        "An identifier or keyword cannot immediately follow a numeric literal.");
    return c;
}

// A numeric literal with its scanner diagnostics (tsc's scanNumber and the `0x`/`0b`/`0o` cases of scan()); `bigint`
// is set for an `n` suffix (the value stays a double, see Value)
static bool scan_numeric_literal(Parser* parent, double* ret, bool* bigint) {
    const char* start = parent->pos;
    const char* c = start;
    if (!(*c >= '0' && *c <= '9') && !(*c == '.' && c[1] >= '0' && c[1] <= '9')) return false;
    *bigint = false;
    cstr digits = cstr_init();
    char prefix = c[0] == '0' ? (char)(c[1] | 0x20) : 0;
    if ((prefix == 'x' || prefix == 'b' || prefix == 'o') && c[2]) {
        int base = prefix == 'x' ? 16 : prefix == 'b' ? 2 : 8;
        c = scan_digit_fragment(parent, c + 2, base, &digits);
        if (!cstr_size(&digits)) {
            Span at = span_make(parent->shared->source, c, c);
            if (base == 16) parse_error(parent, at, 1125, "Hexadecimal digit expected.");
            else if (base == 2) parse_error(parent, at, 1177, "Binary digit expected.");
            else parse_error(parent, at, 1178, "Octal digit expected.");
        }
        *ret = digits_value(cstr_sv(&digits), base);
        cstr_drop(&digits);
        if (*c == 'n') {
            *bigint = true;
            c++;
        }
        parent->pos = c;
        return true;
    }

    bool leading_zero = false;
    if (c[0] == '0' && c[1] >= '0' && c[1] <= '9') {
        // A legacy octal (TS1121), or a decimal with a leading zero (NonOctalDecimalIntegerLiteral: TS1489 below)
        const char* d = c + 1;
        bool octal = true;
        for (; *d >= '0' && *d <= '9'; d++) octal &= *d < '8';
        if (octal) {
            double value = digits_value(csview_from_range(c + 1, d), 8);
            *ret = value;
            parent->pos = d;
            cstr_drop(&digits);
            bool minus = start > cstr_str(&parent->shared->source->contents) && start[-1] == '-';
            char octal_text[64];
            snprintf(octal_text, sizeof(octal_text), "%s0o%llo", minus ? "-" : "", (unsigned long long)value);
            Span span = span_make(parent->shared->source, minus ? start - 1 : start, d);
            parse_error(parent, span, 1121, "Octal literals are not allowed. Use the syntax '", octal_text, "'.");
            return true;
        }
        leading_zero = true;
        cstr_append_n(&digits, c, d - c);
        c = d;
    } else if (c[0] == '0' && c[1] == '_') {
        // tsc reports this `_` and then reads the literal as if it wasn't a leading zero (the `_` is allowed there)
        parse_error(parent, span_make(parent->shared->source, c + 1, c + 2), 6188,
            "Numeric separators are not allowed here.");
        c = scan_digit_fragment(parent, c, 10, &digits);
    } else {
        c = scan_digit_fragment(parent, c, 10, &digits);
    }
    bool decimal = false;
    if (*c == '.') {
        decimal = true;
        cstr_append_n(&digits, ".", 1);
        c = scan_digit_fragment(parent, c + 1, 10, &digits);
    }
    const char* end = c;
    bool scientific = false;
    if (*c == 'e' || *c == 'E') {
        scientific = true;
        c++;
        char sign = 0;
        if (*c == '+' || *c == '-') sign = *c++;
        cstr exponent = cstr_init();
        c = scan_digit_fragment(parent, c, 10, &exponent);
        if (!cstr_size(&exponent)) {
            parse_error(parent, span_make(parent->shared->source, c, c), 1124, "Digit expected.");
        } else {
            cstr_append_n(&digits, "e", 1);
            if (sign) cstr_append_n(&digits, &sign, 1);
            cstr_append_s(&digits, exponent);
            end = c;
        }
        cstr_drop(&exponent);
    }
    *ret = strtod(cstr_str(&digits), nullptr);
    cstr_drop(&digits);
    if (leading_zero) {
        parse_error(parent, span_make(parent->shared->source, start, end), 1489,
            "Decimals with leading zeros are not allowed.");
    } else if (decimal || scientific) {
        c = check_identifier_after_number(parent, start, c, !decimal && scientific);
    } else {
        if (*c == 'n') {
            *bigint = true;
            c++;
        }
        c = check_identifier_after_number(parent, start, c, false);
    }
    parent->pos = c;
    return true;
}

// A number or, with an `n` suffix, a bigint literal (its value is kept as a double, see Value)
static bool parse_numeric_value(Parser* parent, Value* ret) {
    double number;
    bool bigint;
    if (!scan_numeric_literal(parent, &number, &bigint)) return false;
    *ret = bigint ? (Value){ .tag = VALUE_BIGINT, .bigint = number } : (Value){ .tag = VALUE_NUMBER, .number = number };
    return true;
}

// `report_escapes`: false in a tagged template, where invalid escapes are allowed
static bool parse_string_or_tagged(Parser* parent, cstr* ret, bool report_escapes) {
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
        } else if ((*p.pos == '\n' || *p.pos == '\r') && quote != '`') {
            break;
        } else if (*p.pos == '\\' && !p.pos[1]) {
            p.pos++;
            parse_error(&p, span_from_pos(&p), 1126, "Unexpected end of text."); // (in a tagged template too)
            break;
        } else if (*p.pos == '\\') {
            p.pos++;
            EscapeErrors errors = {0};
            if (!parse_string_escape(&p, &str, &errors) && report_escapes) report_escape_errors(&p, &errors);
            continue;
        } else if (*p.pos == '\r') {
            // Ignore?
        } else {
            // The plain run up to whatever the loop has to look at, appended at once
            const char* run = p.pos++;
            while (*p.pos && *p.pos != quote && *p.pos != '\n' && *p.pos != '\\' && *p.pos != '\r') p.pos++;
            cstr_append_n(&str, run, p.pos - run);
            continue;
        }
        p.pos++;
    }
    *ret = str;
    if (*p.pos == quote) {
        p.pos++;
    } else if (quote == '`') {
        parse_error(&p, span_from_pos(&p), 1160, "Unterminated template literal.");
    } else {
        parse_error(&p, span_from_pos(&p), 1002, "Unterminated string literal.");
    }

    return parse_success(&p);
}

static bool parse_string(Parser* parent, cstr* ret) {
    return parse_string_or_tagged(parent, ret, true);
}

// The end tsc gives an unterminated regular expression literal (in its reScanSlashToken): the first unbalanced `)`,
// `]` or `}` outside a character class, else the line's end, without the whitespace and semicolons before it
static const char* unterminated_regex_end(const char* body, const char* line_end) {
    const char* c = body;
    bool in_escape = false;
    bool in_quantifier = false;
    int class_depth = 0;
    int group_depth = 0;
    for (; c < line_end; c++) {
        char ch = *c;
        if (in_escape) {
            in_escape = false;
        } else if (ch == '\\') {
            in_escape = true;
        } else if (ch == '[') {
            class_depth++;
        } else if (ch == ']' && class_depth) {
            class_depth--;
        } else if (!class_depth) {
            if (ch == '{') {
                in_quantifier = true;
            } else if (ch == '}' && in_quantifier) {
                in_quantifier = false;
            } else if (!in_quantifier) {
                if (ch == '(') group_depth++;
                else if (ch == ')' && group_depth) group_depth--;
                else if (ch == ')' || ch == ']' || ch == '}') break;
            }
        }
    }
    while (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\v' || c[-1] == '\f' || c[-1] == ';') c--;
    return c;
}

// A regular expression literal. Only called where an expression may start, which is what makes the `/` not a division
// (tsc rescans the slash token in the same situation). Its body and flags are checked by the checker
static bool parse_regex(Parser* parent) {
    Parser p = parser(parent);

    if (*p.pos != '/' || p.pos[1] == '/' || p.pos[1] == '*') {
        return parse_fail(&p);
    }
    const char* start = p.pos;
    p.pos++;
    bool in_class = false;
    bool in_escape = false;
    while (true) {
        char c = *p.pos;
        if (!c || c == '\n' || c == '\r' || unicode_whitespace_size(p.pos) < 0) {
            p.pos = unterminated_regex_end(start + 1, p.pos);
            parse_error(&p, span_make(p.shared->source, start, p.pos), 1161, "Unterminated regular expression literal.");
            return parse_success(&p);
        }
        if (in_escape) {
            in_escape = false;
        } else if (c == '/' && !in_class) {
            break;
        } else if (c == '[') {
            in_class = true;
        } else if (c == '\\') {
            in_escape = true;
        } else if (c == ']') {
            in_class = false;
        }
        p.pos++;
    }
    p.pos++;
    int size;
    while ((size = identifier_char_size(p.pos, false))) p.pos += size;

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
    bool missing_tail = false; // As in parse_template()
    while (*p.pos && *p.pos != '`') {
        if (*p.pos == '\\' && !p.pos[1]) {
            p.pos++;
            parse_error(&p, span_from_pos(&p), 1126, "Unexpected end of text.");
        } else if (*p.pos == '\\') {
            p.pos++;
            EscapeErrors errors = {0};
            if (!parse_string_escape(&p, &text, &errors)) report_escape_errors(&p, &errors);
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
                missing_tail = true;
                break;
            }
        } else if (*p.pos == '\r') {
            p.pos++; // CRLF is LF in a template
        } else {
            const char* run = p.pos;
            p.pos++;
            while (*p.pos && *p.pos != '`' && *p.pos != '\\' && *p.pos != '$' && *p.pos != '\r') p.pos++;
            cstr_append_n(&text, run, p.pos - run);
        }
    }
    vec_cstr_push(&template.texts, text);

    if (vec_type_is_empty(&template.types)) {
        template_type_drop(&template);
        return parse_fail(&p);
    }

    if (*p.pos == '`') {
        p.pos++;
    } else if (!missing_tail) {
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
    // Where most expressions end: no branch below starts with these
    char c = *p.pos;
    switch (c) {
        case ';': case ',': case ')': case ']': case '}': case ':': case '\0': return parse_fail(&p);
        default: break;
    }

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
    // tsc's parseUpdateExpression: `a++` is no left-hand side expression, only operators continue it (`a--.b` and
    // `a++ ++` are missing a `;`)
    if (left_and_ret->tag == EXPR_UNARY_OP && !expr_is_perenthisized(left_and_ret) &&
        (left_and_ret->unary_op->op == UNARY_OP_RIGHT_PLUS_PLUS || left_and_ret->unary_op->op == UNARY_OP_RIGHT_MINUS_MINUS)
    ) {
        goto operators;
    }
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
            note_commonjs_start(p.shared, span_start_ptr(left_and_ret->outer_span));
            left_and_ret->call->callable.identifier.require_call = true;
            Expr const* arg = vec_expr_at(&left_and_ret->call->args, 0);
            if (arg->tag == EXPR_LITERAL && arg->literal.tag == VALUE_STRING) {
                ModuleRef* ref = add_module_ref(&p, cstr_clone(arg->literal.string), arg->inner_span);
                ref->syntax = MODULE_REF_REQUIRE;
                left_and_ret->call->callable.identifier.module_ref = ref;
            }
        } else if (p.shared->source->is_js && expr_is_define_property_call(left_and_ret, nullptr) &&
            expr_is_exports_object(vec_expr_at(&left_and_ret->call->args, 0))
        ) {
            // `Object.defineProperty(exports, "x", {...})` makes it CommonJS too (tsc's bindObjectDefinePropertyExport)
            p.shared->saw_commonjs = true;
            note_commonjs_start(p.shared, span_start_ptr(left_and_ret->outer_span));
        }
        return parse_success(&p);
    }

    // (A tagged template is part of a member expression, `new` included: tsc's parseMemberExpressionRest)
    if (!ctx.entity_name && new_keyword_precedence >= ctx.min_precedence && *p.pos == '`') {
        Anchor template_a = anchor(&p);
        cstr plain_string = cstr_null;
        if (!parse_template(&p, &parsed.args, nullptr)) {
            parsed.args = vec_expr_init();
            parse_string_or_tagged(&p, &plain_string, false);
        }
        *left_and_ret = expr_template(
            span_from(a),
            c_new(TemplateExpr, {
                .tag = c_new(Expr, *left_and_ret),
                .template_span = span_from(template_a),
                .substitutions = parsed.args,
                .plain_string = plain_string,
                .incomplete = p.pos - 1 <= template_a.start || p.pos[-1] != '`',
            })
        );
        return parse_success(&p);
    }

    if (parse_dot_prop(&p, &parsed.optional_chain, &parsed.expr_a)) {
        if (left_and_ret->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(left_and_ret) &&
            !parsed.optional_chain
        ) {
            // `f<T>.x` (tsc's parsePropertyAccessExpressionRest): over the type arguments, `<` to `>`
            const char* open = span_end_ptr(left_and_ret->generic_params->expr.outer_span);
            while (*open && *open != '<') open++;
            parse_error(&p, span_make(p.shared->source, open, span_end_ptr(left_and_ret->outer_span)), 1477,
                "An instantiation expression cannot be followed by a property access.");
        }
        *left_and_ret = expr_property(
            span_from(a),
            c_new(PropertyExpr, {
                .expr = *left_and_ret,
                .prop = parsed.expr_a,
                .form = PROPERTY_EXPR_DOT,
                .optional_chain = parsed.optional_chain,
            })
        );
        Span prop_name = parsed.expr_a.inner_span;
        if (prop_name.sv.size && prop_name.sv.buf[0] == '#' && expr_is_optional_chain(left_and_ret)) {
            // tsc's parsePropertyAccessExpressionRest
            parse_error(&p, prop_name, 18030, "An optional chain cannot contain private identifiers.");
        }
        if (p.shared->source->is_js && expr_is_module_exports(left_and_ret)) {
            u32 n = p.shared->module_exports_access_count++;
            if (!(n & (n - 1))) {
                p.shared->module_exports_accesses = realloc(p.shared->module_exports_accesses, sizeof(Span) * (n ? n * 2 : 1));
                panic_if(!p.shared->module_exports_accesses, "out of memory");
            }
            p.shared->module_exports_accesses[n] = left_and_ret->property->expr.inner_span;
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
    if ((c == 'a' || c == 's') && as_type_precedence > ctx.min_precedence &&
        parse_as_type(&p, &parsed.type, &as_type_span, &is_satisfies)
    ) {
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

        if (parse_arrow_function(&sub_p, ctx, function_flags_null, false, &parsed.function)) {
            parse_success(&sub_p);
            expr_drop(left_and_ret);
            *left_and_ret = expr_function(span_from(a), c_new(FunctionImpl, parsed.function));
            if (p.shared->source->is_js) jsdoc_apply_function_expression(&p, left_and_ret);
            return parse_success(&p);
        } else {
            parse_fail(&sub_p);
        }
    }

    Parser generic_p = parser(&p);
    // Never in JavaScript files, to avoid the ambiguity with `<` and `>` (tsc's parseTypeArgumentsInExpression)
    if (c == '<' && !p.shared->source->is_js && parse_generic_params(&generic_p, true, &parsed.generic_params)) {
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
        if (parse_right_unary_op(&p, &parsed.unary) &&
            // A postfix `++`/`--` takes a left-hand side expression (tsc's parseUpdateExpression): not in the operand
            // of a prefix one, `--a--`, and not after an operator expression, `--a --`
            !((parsed.unary == UNARY_OP_RIGHT_PLUS_PLUS || parsed.unary == UNARY_OP_RIGHT_MINUS_MINUS) &&
                (ctx.min_precedence > unary_op_get_precedence(UNARY_OP_LEFT_PLUS_PLUS) ||
                    (!expr_is_perenthisized(left_and_ret) && ((left_and_ret->tag == EXPR_UNARY_OP &&
                        left_and_ret->unary_op->op != UNARY_OP_RIGHT_BANG) || left_and_ret->tag == EXPR_BINARY_OP ||
                        left_and_ret->tag == EXPR_TERNARY ||
                        // (Not a JSDoc assertion, `/** @type {T} */ (x)++`: parenthesized, its type comes first)
                        (left_and_ret->tag == EXPR_AS_TYPE && span_start_ptr(left_and_ret->as_type->keyword_span) >
                            span_start_ptr(left_and_ret->as_type->expr.outer_span))))))
        ) {
            if (parsed.unary == UNARY_OP_RIGHT_BANG) {
                js_syntax_error(&p, span_from(a), 8013, "Non-null assertions can only be used in TypeScript files.");
            }
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

operators:
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

        // (A missing operand is still the right side, like tsc's missing identifier: `(a, )` checks `a`)
        bool missing = !expect_expr(&p, expr_ctx_null, &expr);

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
        if (missing) return parse_success(&p);
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
    // A missing `}` ends the template (tsc's parseTemplateSpan: a missing template tail)
    bool missing_tail = false;
    while (*p.pos && *p.pos != '`') {
        if (*p.pos == '\\' && !p.pos[1]) {
            p.pos++;
            parse_error(&p, span_from_pos(&p), 1126, "Unexpected end of text.");
        } else if (*p.pos == '\\') {
            p.pos++;
            EscapeErrors errors = {0};
            // (An invalid escape is fine in a tagged template)
            if (!parse_string_escape(&p, &text, &errors) && texts) report_escape_errors(&p, &errors);
        } else if (*p.pos == '\r') {
            p.pos++;
        } else if (p.pos[0] == '$' && p.pos[1] == '{') {
            vec_cstr_push(&cooked, text);
            text = cstr_null;
            p.pos += 2;
            hop_whitespace(&p);
            // Committed from the `${` on: a missing expression is one
            Expr expr;
            if (expect_expr(&p, expr_ctx_null, &expr)) parse_comma_ops_right_side(&p, &expr);
            vec_expr_push(&substitutions, expr);
            hop_whitespace(&p);
            if (!expect_char(&p, '}')) {
                missing_tail = true;
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
    } else if (!missing_tail) {
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

    if (!starts_with_followed_by_word_break(&p, "async")) {
        return parse_fail(&p);
    }
    Tristate arrow_tristate = arrow_function_tristate(&p);
    hop_start_if_ends_on_word_break(&p, "async");
    hop_whitespace(&p);
    if (p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }

    if (!parse_function_keyword_function(&p, true, ret) &&
        !parse_arrow_function(&p, ctx, (FunctionFlags){ .is_async = true }, arrow_tristate == TRISTATE_TRUE, ret)
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

static bool last_error_starts_at(Parser* me, Span span) {
    for (intptr_t i = vec_error_size(&my shared->errors); i-- > 0;) {
        Error const* last = vec_error_at(&my shared->errors, i);
        if (is_js_syntax_code(last->ts_code)) continue;
        return last->span.source == span.source && span_start_ptr(last->span) == span_start_ptr(span);
    }
    return false;
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
    int escape = identifier_escape_size(parent->pos);
    if (escape) {
        parent->pos += escape;
    } else if (!parse_identifier_or_keyword(parent, nullptr)) {
        return false;
    }
    while (true) {
        int size = hyphens && *parent->pos == '-' ? 1 : identifier_char_size(parent->pos, false);
        if (!size) size = identifier_escape_size(parent->pos);
        if (!size) break;
        parent->pos += size;
    }
    *ret = span_from(a);
    if (memchr(ret->sv.buf, '\\', ret->sv.size)) {
        parse_error(parent, *ret, 17021, "Unicode escape sequence cannot appear here.");
    }
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
        parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
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
        parse_error(p, jsx_span_from_token(p), 1003, "Identifier expected.");
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
            if (*dot.pos == '#' && identifier_char_size(dot.pos + 1, true)) {
                dot.pos++;
                parse_identifier_or_keyword(&dot, nullptr);
                parse_error(&dot, span_from_pos(&dot), 1003, "Identifier expected.");
            } else {
                parse_error(&dot, span_from_token(&dot), 1003, "Identifier expected.");
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
            parse_error(p, span_from_pos(p), 1005, "'}' expected.");
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
        parse_error(p, span_from_token(p), 1005, "'}' expected.");
    }
    return has_expr;
}

static bool parse_jsx_element(Parser* parent, bool in_expression_context, JsxElement const* enclosing,
    bool must_be_unary, const char* top_invalid_pos, Expr* ret);

// tsc's parseJsxAttributes: until `>` or `/`. Returns where the last of them ends (`end`, the tag's name's end, without)
static const char* parse_jsx_attributes(Parser* p, const char* end, VecJsxAttribute* ret) {
    bool first = true;
    while (true) {
        if (!first) end = p->pos;
        first = false;
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
            if (!hop_start(p, "...")) parse_error(p, span_from_token(p), 1005, "'...' expected.");
            hop_whitespace(p);
            if (expect_expr(p, expr_ctx_null, &attr.value)) parse_comma_ops_right_side(p, &attr.value);
            hop_whitespace(p);
            if (!hop_char(p, '}')) parse_error(p, span_from_token(p), 1005, "'}' expected.");
            attr.span = span_from(a);
            vec_jsx_attribute_push(ret, attr);
            continue;
        }
        Span name;
        if (!parse_jsx_identifier(p, &name)) {
            // tsc's parseList: skips what can't start an attribute, unless something around could use it
            // (isInSomeParsingContext: anything is a JSX child; elsewhere, roughly, whatever starts a statement ends
            // the tag: `<C<T> />` in a JS file)
            parse_error(p, jsx_span_from_token(p), 1003, "Identifier expected.");
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
                parse_error(p, span_from_token(p), 1145, "'{' or JSX element expected.");
            }
        }
        attr.span = span_from(a);
        vec_jsx_attribute_push(ret, attr);
    }
    return end;
}

static bool jsx_is_line_break(char c) {
    return c == '\n' || c == '\r';
}

// tsc's parseJsxChildren: until `</` (left for the caller) or the end of the file
// fragment_start: where a fragment's TS17014 starts (its full start in an expression, whitespace before it included)
static void parse_jsx_children_inner(Parser* p, JsxElement* el, bool is_fragment, const char* fragment_start);

static void parse_jsx_children(Parser* p, JsxElement* el, bool is_fragment, const char* fragment_start) {
    p->shared->jsx_children_depth++;
    parse_jsx_children_inner(p, el, is_fragment, fragment_start);
    p->shared->jsx_children_depth--;
}

static void parse_jsx_children_inner(Parser* p, JsxElement* el, bool is_fragment, const char* fragment_start) {
    while (true) {
        Anchor a = anchor(p);
        char c = *p->pos;
        if (!c) {
            // At the tag that lacks the closing element rather than at the end of the file
            if (is_fragment) {
                parse_error(p, span_make(p->shared->source, fragment_start, span_end_ptr(el->opening_span)), 17014,
                    "JSX fragment has no corresponding closing tag.");
            } else {
                parse_error(p, el->tag_name.outer_span, 17008, "JSX element '", el->tag_name.outer_span,
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
    const char* name_full_start = p.pos;
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
        const char* name_end = p.pos;
        if (el->tag_name.tag == EXPR_IDENTIFIER && !el->tag_name.identifier.name.sv.size) {
            // A missing name is where the trivia before it starts (tsc's missing node, its TS17008 is there)
            el->tag_name = expr_identifier((IdentifierExpr){
                .name = span_make(p.shared->source, name_full_start, name_full_start),
            });
            name_end = name_full_start;
        }
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
                    parse_error(&p, jsx_span_from_token(&p), 1110, "Type expected.");
                    break;
                }
                vec_spanned_type_push(&el->type_args, spanned_type(span_from(type_a), t));
                hop_whitespace(&p);
                Anchor comma_a = anchor(&p);
                if (!hop_char(&p, ',')) {
                    if (!hop_char(&p, '>')) parse_error(&p, jsx_span_from_token(&p), 1005, "'>' expected.");
                    break;
                }
                el->type_args_trailing_comma = span_from(comma_a);
                hop_whitespace(&p);
                if (hop_char(&p, '>')) break;
                el->type_args_trailing_comma = span_null;
            }
            name_end = p.pos;
            hop_whitespace(&p);
            attributes_a = anchor(&p);
        }
        const char* attributes_end = parse_jsx_attributes(&p, name_end, &attributes);
        if (!hop_char(&p, '>')) {
            bool slash = hop_char(&p, '/');
            if (!slash) parse_error(&p, span_from_token(&p), 1005, "'/' expected.");
            hop_whitespace(&p);
            if (!hop_char(&p, '>')) {
                parse_error(&p, span_from_token(&p), 1005, "'>' expected.");
                // Nothing of the tag's end: the element ends with its last token (`~< <`)
                if (!slash) p.pos = attributes_end;
            }
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
        // tsc reports at the opening fragment node, whose range starts before the trivia in an expression (a child's
        // is part of the text before it)
        const char* fragment_start = a.start;
        if (in_expression_context) {
            const char* file_start = cstr_str(&p.shared->source->contents);
            while (fragment_start > file_start && strchr(" \t\r\n", fragment_start[-1])) fragment_start--;
        }
        parse_jsx_children(&p, el, is_fragment, fragment_start);
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
                parse_error(&p, span_from_pos(&p), 1005, "'</' expected.");
                // tsc's parseJsxClosingElement goes on with a missing name
                if (!is_fragment) el->closing_name = expr_identifier((IdentifierExpr){ .name = span_from_pos(&p) });
            } else if (is_fragment) {
                hop_whitespace(&p);
                if (!hop_char(&p, '>')) {
                    parse_error(&p, span_from_token(&p), 17015, "Expected corresponding closing tag for JSX fragment.");
                }
                el->closing_span = span_from(closing_a);
            } else {
                hop_whitespace(&p);
                bool namespaced, broken = false;
                parse_jsx_tag_name(&p, &el->closing_name, &namespaced, &broken);
                hop_whitespace(&p);
                if (!hop_char(&p, '>')) parse_error(&p, span_from_token(&p), 1005, "'>' expected.");
                el->closing_span = span_from(closing_a);
                if (!jsx_tag_names_equal(&el->tag_name, &el->closing_name)) {
                    if (enclosing && jsx_tag_names_equal(&el->closing_name, &enclosing->tag_name)) {
                        // Our parent's closing tag: the error is ours
                        parse_error(&p, el->tag_name.outer_span, 17008, "JSX element '", el->tag_name.outer_span,
                            "' has no corresponding closing tag.");
                    } else {
                        parse_error(&p, el->closing_name.outer_span, 17002,
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
            parse_error(&p, span_make(p.shared->source, top, span_end_ptr(invalid.outer_span)), 2657,
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

// tsc's parseUpdateExpression: in a JSX file, `<` followed by an identifier, keyword or `>` starts an element. As the
// operand of a unary operator any `<` does (tsc's parseSimpleUnaryExpression: `+ <1234> x`)
__attribute__((noinline)) static bool at_jsx_element_start(Parser* parent, bool unary_operand) {
    if (!parent->shared->source->is_jsx || *parent->pos != '<') return false;
    if (unary_operand) return true;
    Parser p = parser(parent);
    p.pos++;
    hop_whitespace(&p);
    bool ret = *p.pos == '>' || identifier_char_size(p.pos, true) || identifier_escape_size(p.pos);
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
            // (Where tsc reports what's wrong with a JSDoc assertion: its type; a `@satisfies` its tag)
            .keyword_span = is_satisfies ? tag->tag_name : tag->type_span,
            .type = jsdoc_take_type(tag, nullptr),
            .is_satisfies = is_satisfies,
            .is_jsdoc = true,
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
    Tristate arrow_tristate;

    VecExpr substitutions;
    VecCStr texts;
    // Most branches below need a particular first character: test it before trying the branch
    char c = *p.pos;
    bool word = identifier_char_size(p.pos, true);
    if (c == '`' && parse_template(&p, &substitutions, &texts)) {
        *ret = expr_template(span_from(a), c_new(TemplateExpr, {
            .template_span = span_from(a),
            .substitutions = substitutions,
            .texts = texts,
        }));
    } else if ((c == 'u' || c == 'n' || c == 't' || c == 'f' || c == '"' || c == '\'' || c == '`' ||
            is_number_start_char(c)) && parse_literal(&p, &ret->literal)
    ) {
        ret->tag = EXPR_LITERAL;
    } else if (c == '/' && parse_regex(&p)) {
        *ret = expr_regex(span_from(a));
    } else if (*p.pos == '#' && identifier_char_size(p.pos + 1, true)) {
        p.pos++;
        Span ignored;
        parse_identifier_or_keyword(&p, &ignored);
        *ret = expr_private_name(span_from(a), c_new(PrivateNameExpr, {0}));
    } else if (c == '{' && parse_object(&p, &ret->object)) {
        ret->tag = EXPR_OBJECT;
        ret->jsdoc_enum_literal = false; // (ret isn't zeroed)
    } else if (c == '[' && parse_array(&p, &ret->array)) {
        ret->tag = EXPR_ARRAY;
    } else if ((c == '@' || c == 'a' || c == 'c') && parse_class_decl(&p, false, &parsed.class_decl)) {
        parsed.class_decl.is_expression = true;
        *ret = expr_class_decl(span_from(a), c_new(ClassDecl, parsed.class_decl));
    } else if (c == 'n' && parse_new_target(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "new")) {
        hop_whitespace(&p);
        ExprCtx sub_ctx = ctx;
        sub_ctx.min_precedence = new_keyword_precedence;
        CallExpr call = { .uses_new = true };
        if (*p.pos == '<' && !p.shared->source->is_jsx) {
            // A type assertion is no member expression (tsc's parseMemberExpressionOrHigher): `new <T>F()` is `new`
            // of a missing expression, then `< T > F()`
            expected_expression_error(&p);
            call.callable = expr_implicit_any(span_from_pos(&p));
        } else {
            expect_expr(&p, sub_ctx, &call.callable);
        }
        if (!parse_call_args(&p, &call.args)) {
            call.args = vec_expr_init();
        }
        *ret = expr_call(span_from(a), c_new(CallExpr, call));
    } else if ((!word || c == 'd' || c == 'a' || c == 'y' || c == 't' || c == 'v') &&
        parse_left_unary_op_expr(&p, ctx, ret)
    ) {
    } else if (c == 'f' && parse_function_keyword_function(&p, false, &parsed.function)) {
        *ret = expr_function(span_from(a), c_new(FunctionImpl, parsed.function));
        if (p.shared->source->is_js) jsdoc_apply_function_expression(&p, ret);
    } else if (c == 'a' && parse_async_function_expr(&p, ctx, &parsed.function)) {
        *ret = expr_function(span_from(a), c_new(FunctionImpl, parsed.function));
        if (p.shared->source->is_js) jsdoc_apply_function_expression(&p, ret);
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
        // tsc's parseSuperExpression
        Parser args_p = parser(&p);
        hop_whitespace(&args_p);
        Anchor args_a = anchor(&args_p);
        VecSpannedType args;
        if (*args_p.pos == '<' && parse_generic_params(&args_p, true, &args)) {
            vec_spanned_type_drop(&args);
            Span args_span = span_from(args_a);
            hop_whitespace(&args_p);
            bool is_call = *args_p.pos == '(';
            parse_fail(&args_p);
            // (The call parses the type arguments again: the baselines walk them)
            if (is_call) parse_error(&p, args_span, 2754, "'super' may not use type arguments.");
        } else {
            parse_fail(&args_p);
        }
        Parser next_p = parser(&p);
        hop_whitespace(&next_p);
        char next = *next_p.pos;
        parse_fail(&next_p);
        if (next != '(' && next != '.' && next != '[' && next != '<') {
            parse_error(&p, span_from_token(&p), 1034, "'super' must be followed by an argument list or member access.");
        }
    } else if (c == 'i' && parse_import_keyword_expr(&p, &ret->identifier)) {
        *ret = expr_identifier(ret->identifier);
    } else if (word && !yield_await_is_keyword_here(&p) && parse_identifier_expr(&p, &ret->identifier)) {
        *ret = expr_identifier(ret->identifier);
        inner_span_set = true;
    } else if ((*p.pos == '(' || *p.pos == '<') && (arrow_tristate = arrow_function_tristate(&p)) != TRISTATE_FALSE &&
        parse_arrow_function(&p, ctx, function_flags_null, arrow_tristate == TRISTATE_TRUE, &parsed.function)
    ) {
        *ret = expr_function(span_from(a), c_new(FunctionImpl, parsed.function));
        if (p.shared->source->is_js) jsdoc_apply_function_expression(&p, ret);
    } else if (hop_char(&p, '(')) {
        // Committed like tsc's parseParenthesizedExpression: `()` is missing its expression
        hop_whitespace(&p);
        expect_expr(&p, expr_ctx_null, ret);
        parse_comma_ops_right_side(&p, ret);
        hop_whitespace(&p);
        expect_char(&p, ')');
        inner_span_set = true;
        if (p.shared->source->is_js) jsdoc_apply_parenthesized(&p, a.start, span_from(a), ret);
    } else if (at_jsx_element_start(&p, ctx.min_precedence == unary_op_get_precedence(UNARY_OP_LEFT_BANG))) {
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
        const char* after_gt = p.pos;
        hop_whitespace(&p);

        // The operand is a unary expression: `<T>a || b` is `(<T>a) || b`
        Expr e;
        ExprCtx operand_ctx = ctx;
        operand_ctx.min_precedence = unary_op_get_precedence(UNARY_OP_LEFT_BANG);
        if (!expect_expr(&p, operand_ctx, &e) && e.outer_span.sv.size == 0 && p.pos == span_start_ptr(e.outer_span)) {
            p.pos = after_gt; // A missing operand ends the assertion at its `>` (tsc's missing node)
        }
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

    // An arrow function is a whole assignment expression: nothing continues it (`() => {} || a` is missing a `;`),
    // nor what it ends as the right side of an assignment or the last branch of a conditional (`x = () => {}` and a
    // `(` on the next line)
    while (!expr_ends_with_arrow(ret) && parse_expr_right_side(&p, ctx, ret)) {}

    return parse_success(&p);
}

static bool expr_ends_with_arrow(Expr const* e) {
    while (!expr_is_perenthisized(e)) {
        if (e->tag == EXPR_FUNCTION) return span_valid(e->function->arrow_span);
        if (e->tag == EXPR_BINARY_OP && binary_op_is_assignment(e->binary_op->op)) {
            e = &e->binary_op->right;
        } else if (e->tag == EXPR_TERNARY) {
            e = &e->ternary->no;
        } else {
            return false;
        }
    }
    return false;
}

static inline bool expect_expr(Parser* parent, ExprCtx ctx, Expr* ret) {
    if (parse_expr(parent, ctx, ret)) {
        return true;
    }
    expected_expression_error(parent);
    *ret = expr_implicit_any(span_from_pos(parent));
    // tsc's isStartOfExpression takes a binary operator for a missing left operand and goes on with the binary
    // expression (`{ ...*o }`, `x = * y`). Not `<` (a type assertion) or `/` (a regex)
    Tok t = tok_peek(parent);
    if (tok_is_binary_operator(t) && !tok_is(t, "<") && !tok_is(t, "/")) {
        while (parse_expr_right_side(parent, ctx, ret)) {}
    }
    return false;
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
    ImportResolutionOverride reference_resolution = IMPORT_RESOLUTION_DEFAULT;
    bool invalid_resolution = false;
    while (true) {
        if (hop_start(&p, "/>")) {
            break;
        }

        // (Names like `resolution-mode` are words joined by dashes)
        Anchor attr_a = anchor(&p);
        Span attr;
        if (!parse_identifier_or_keyword(&p, &attr)) {
            cstr_drop(&reference_path);
            return parse_fail(&p);
        }
        while (*p.pos == '-' && hop_char(&p, '-') && parse_identifier_or_keyword(&p, &attr)) {}
        attr = span_from(attr_a);
        hop_whitespace_full(&p, false, false);

        cstr value = cstr_null;
        Anchor value_a = anchor(&p);
        if (hop_char(&p, '=')) {
            hop_whitespace_full(&p, false, false);
            value_a = anchor(&p);
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
            if (span_equals(attr, "resolution-mode")) {
                if (cstr_equals(&value, "import")) reference_resolution = IMPORT_RESOLUTION_IMPORT;
                else if (cstr_equals(&value, "require")) reference_resolution = IMPORT_RESOLUTION_REQUIRE;
                else invalid_resolution = true;
            }
        }

        cstr_drop(&value);
        hop_whitespace_full(&p, false, false);
    }

    if (found_reference_attr) {
        if (invalid_resolution && reference_attr == SOURCE_DIRECTIVE_REFERENCE_TYPES) {
            // tsc's processPragmasIntoFields, on the name without its quotes
            Span name = reference_path_span;
            if (name.sv.size >= 2 && (*name.sv.buf == '"' || *name.sv.buf == '\'')) {
                name.sv = csview_slice(name.sv, 1, name.sv.size - 1);
            }
            parse_error(&p, name, 1453, "`resolution-mode` should be either `require` or `import`.");
        }
        *ret = (SourceDirectiveStatement){
            .tag = reference_attr,
            .path = reference_path,
            .path_span = reference_path_span,
            .preserve = reference_preserve,
            .resolution_override = reference_resolution,
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
            const char* end = p.pos;
            hop_whitespace(&p);

            if (hop_start_if_ends_on_word_break(&p, "as")) {
                hop_whitespace(&p);
                if (!parse_module_export_name(&p, &item.decl_name)) {
                    expect_identifier(&p, &item.decl_name);
                }
                end = p.pos;
            } else {
                item.decl_name = item.source_name;
            }

            item.full_span = span_make(p.shared->source, a.start, end);
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

    // (Up to this parser's position: the anchor is the parent's, which hasn't moved yet)
    statement->main.full_span = span_make(p.shared->source, statement_start.start, p.pos);
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
        js_syntax_error(&p, span_with_semicolon(span_from(statement_start)), 8003,
            "'export =' can only be used in TypeScript files.");
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
                a = statement_start; // (What TS2308 underlines: tsc's ExportDeclaration)
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
        statement.main.not_top_level = p.shared->statement_parent != MODIFIED_IN_SOURCE_FILE;
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
                // (tsc's checker reports it, checkExternalImportOrExportDeclaration: not after another error there)
                add_error_to_parser(&p, (Error){
                    .message = cstr_lit("String literal expected."),
                    .span = path_expr.inner_span,
                    .ts_code = 1141,
                    .phase = ERROR_PHASE_TYPECHECK,
                });
            }
            expr_drop(&path_expr);
            statement.attributes = parse_import_attributes(&p);
        } else {
            expr_drop(&path_expr); // (what expect_expr() made of a missing operand's binary expression)
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
    if (reports_js_syntax(&p)) {
        Span whole = span_with_semicolon(span_from(statement_start));
        if (statement.main.tag == PORT_IMPORT_ALIAS || statement.main.is_require) {
            js_syntax_error(&p, whole, 8002, "'import ... =' can only be used in TypeScript files.");
        } else if (statement.main.type_only) {
            js_syntax_error(&p, whole, 8006, "'", is_export ? "export type" : "import type",
                "' declarations can only be used in TypeScript files.");
        } else {
            for_each (i, vec_port_item, &statement.items) {
                if (!i->type_only) continue;
                js_syntax_error(&p, i->full_span, 8006, "'", is_export ? "export...type" : "import...type",
                    "' declarations can only be used in TypeScript files.");
            }
        }
    }
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

// tsc's isBindingIdentifier: an identifier that isn't a reserved word (`yield`/`await` are the caller's business)
static bool parse_binding_identifier(Parser* parent) {
    const char* start = parent->pos;
    if (!parse_identifier(parent, nullptr)) return false;
    if (is_reserved_word(csview_from_range(start, parent->pos))) {
        parent->pos = start;
        return false;
    }
    return true;
}

static bool parse_binding_content(Parser* parent, Binding* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    Binding binding;

    if (parse_binding_identifier(&p)) {
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
                shorthand_allowed = !is_reserved_word(name.sv);
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
                    Span word;
                    Parser probe = parser(&p);
                    bool reserved = parse_identifier_or_keyword(&probe, &word) && is_reserved_word(word.sv);
                    parse_fail(&probe);
                    if (reserved) {
                        // tsc's createIdentifier: a missing identifier, the word is left for the list to trip over
                        parse_error(&p, word, 1359, "Identifier expected. '", word, "' is a reserved word that cannot be used here.");
                    } else {
                        parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
                    }
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

// A variable's, parameter's or catch variable's binding: tsc's parseIdentifierOrPattern also takes a private name as an
// identifier, reporting code (TS18029 variables, TS18009 parameters)
static bool parse_binding_content_or_private_name(Parser* parent, Binding* ret, u32 code, char const* message) {
    if (*parent->pos != '#' || !identifier_char_size(parent->pos + 1, true)) return parse_binding_content(parent, ret);
    Anchor a = anchor(parent);
    parent->pos++;
    parse_identifier_or_keyword(parent, nullptr);
    *ret = (Binding){ .tag = BINDING_IDENTIFIER, .content_span = span_from(a) };
    parse_error(parent, ret->content_span, code, message);
    return true;
}

// `report_js`: TS8010 in JavaScript (not for a property signature)
static bool parse_binding_type_ex(Parser* parent, Binding* ret, bool report_js) {
    if (!hop_char(parent, ':')) {
        return false;
    }
    hop_whitespace(parent);

    Anchor a = anchor(parent);

    intptr_t mark = error_mark(parent);
    TypeNode type;
    expect_type(parent, TYPE_PRECEDENCE_NULL, &type);
    ret->has_explicit_type = true;
    ret->explicit_type = type;
    ret->explicit_type_span = span_from(a);
    if (report_js) {
        js_syntax_skip_since(parent, mark);
        js_syntax_error(parent, ret->explicit_type_span, 8010, "Type annotations can only be used in TypeScript files.");
    }

    return true;
}

static bool parse_binding_type(Parser* parent, Binding* ret) {
    return parse_binding_type_ex(parent, ret, true);
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
#define WORD_IS(lit) (word.size == sizeof(lit) - 1 && memcmp(word.buf, lit, sizeof(lit) - 1) == 0)
    if (word.size < 2 || word.size > 10) return false;
    switch (word.buf[0]) {
        case 'b': return WORD_IS("break");
        case 'c': return WORD_IS("case") || WORD_IS("catch") || WORD_IS("class") || WORD_IS("const") ||
            WORD_IS("continue");
        case 'd': return WORD_IS("debugger") || WORD_IS("default") || WORD_IS("delete") || WORD_IS("do");
        case 'e': return WORD_IS("else") || WORD_IS("enum") || WORD_IS("export") || WORD_IS("extends");
        case 'f': return WORD_IS("false") || WORD_IS("finally") || WORD_IS("for") || WORD_IS("function");
        case 'i': return WORD_IS("if") || WORD_IS("import") || WORD_IS("in") || WORD_IS("instanceof");
        case 'n': return WORD_IS("new") || WORD_IS("null");
        case 'r': return WORD_IS("return");
        case 's': return WORD_IS("super") || WORD_IS("switch");
        case 't': return WORD_IS("this") || WORD_IS("throw") || WORD_IS("true") || WORD_IS("try") || WORD_IS("typeof");
        case 'v': return WORD_IS("var") || WORD_IS("void");
        case 'w': return WORD_IS("while") || WORD_IS("with");
        default: return false;
    }
#undef WORD_IS
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
            .value_only = span_valid(accessed),
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
static bool is_exports_alias_name(Parser const* p, Expr const* e) {
    if (e->tag != EXPR_IDENTIFIER || e->identifier.keyword || expr_is_perenthisized(e)) return false;
    for (u32 i = 0; i < p->shared->exports_alias_count; i++) {
        if (csview_eq(&p->shared->exports_aliases[i], &e->identifier.name.sv)) return true;
    }
    return false;
}

// `const M = module.exports` at the top level (see ParserShared.exports_aliases)
static void note_exports_alias(Parser* p, InitializedBinding const* initializer) {
    if (p->shared->function_depth || initializer->binding.tag != BINDING_IDENTIFIER) return;
    Expr const* value = &initializer->value;
    bool alias = expr_is_module_exports(value) || is_exports_alias_name(p, value) || (value->tag == EXPR_IDENTIFIER &&
        !value->identifier.keyword && !expr_is_perenthisized(value) && span_equals(value->identifier.name, "exports"));
    if (!alias) return;
    ParserShared* shared = p->shared;
    if (shared->exports_alias_count == shared->exports_alias_cap) {
        shared->exports_alias_cap = shared->exports_alias_cap ? shared->exports_alias_cap * 2 : 4;
        shared->exports_aliases = realloc(shared->exports_aliases, sizeof(csview) * shared->exports_alias_cap);
        panic_if(!shared->exports_aliases, "out of memory");
    }
    shared->exports_aliases[shared->exports_alias_count++] = initializer->binding.content_span.sv;
}

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

    // tsc's parseDelimitedList for the declarators, but with the terminators checked after each (below)
    u32 saved_context = list_enter(&p, PARSING_VARIABLE_DECLARATIONS);
    while (true) {
        if (empty_for_of || !parse_binding_content_or_private_name(&p, &initializer.binding, 18029,
                "Private identifiers are not allowed in variable declarations.")) {
            if (span_valid(comma_span)) {
                parse_error(&p, comma_span, 1009, "Trailing comma not allowed.");
                p.pos = span_end_ptr(comma_span);
            } else if (vec_initialized_binding_is_empty(&ret->initializers)) {
                if (ret->keyword == VAR_INIT_LET) {
                    // let can also be a variable name, fail so we then attempt to parse it as such
                    list_leave(&p, saved_context);
                    variable_statement_drop(ret);
                    return parse_fail(&p);
                } else if (!list_is_terminator(PARSING_VARIABLE_DECLARATIONS, tok_peek(&p))) {
                    // `var class`: TS1389 (the checker's TS1123 for the empty list is a grammar error, dropped)
                    if (!list_abort_or_skip(&p, PARSING_VARIABLE_DECLARATIONS)) continue;
                } else {
                    Span s = span_from(end_of_keyword);
                    s.sv.size = 0;
                    parse_error(&p, s, 1123, "Variable declaration list cannot be empty.");
                }
            } else if (!list_abort_or_skip(&p, PARSING_VARIABLE_DECLARATIONS)) {
                // (tsc's abortParsingListOrMoveToNextToken: `var x = {} : 1` skips the `:`)
                comma_span = span_null;
                continue;
            }
            break;
        }
        const char* end_of_initialization = p.pos;
        hop_whitespace(&p);

        // `let x!: T` (tsc's parseVariableDeclarationAllowExclamation: not in a `for`)
        Span definite = span_null;
        if (!in_for && initializer.binding.tag == BINDING_IDENTIFIER && *p.pos == '!' && p.pos[1] != '=' &&
            p.whitespace != WHITESPACE_NEWLINE
        ) {
            definite = span_make(p.shared->source, p.pos, p.pos + 1);
            p.pos++;
            end_of_initialization = p.pos;
            hop_whitespace(&p);
        }

        bool typed = false;
        if (parse_binding_type_or_set_to_default(&p, &initializer.binding)) {
            typed = true;
            end_of_initialization = p.pos;
            hop_whitespace(&p);
        }

        bool initialized = parse_initialization_or_set_to_default(&p, ctx, &initializer.value);
        // tsc's checkGrammarVariableDeclaration (after its TS1155 for a `const` without an initializer)
        if (span_valid(definite)) {
            bool const_like = ret->keyword != VAR_INIT_VAR && ret->keyword != VAR_INIT_LET;
            if (initialized) {
                parse_error(&p, definite, 1263, "Declarations with initializers cannot also have definite assignment assertions.");
            } else if (const_like && !p.shared->in_ambient) {
            } else if (!typed) {
                parse_error(&p, definite, 1264, "Declarations with definite assignment assertions must also have type annotations.");
            } else if (p.shared->in_ambient) {
                parse_error(&p, definite, 1255, "A definite assignment assertion '!' is not permitted in this context.");
            } else {
                initializer.binding.definitely_assigned = true;
            }
        }
        if (initialized) {
            if (initializer.binding.tag == BINDING_IDENTIFIER) {
                mark_named_evaluation(&initializer.value, false);
                mark_assigned_name(&initializer.value, initializer.binding.content_span);
            }
            end_of_initialization = p.pos;
            hop_whitespace(&p);
        }

        if (p.shared->source->is_js) {
            note_require_declaration(&initializer);
            note_exports_alias(&p, &initializer);
        }
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
    list_leave(&p, saved_context);
    shrink_list(vec_initialized_binding, &ret->initializers);

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
    // `for await (x of y)`
    Span await_span = { .source = p.shared->source, .sv = csview_from_n(p.pos, 5) };
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
        statement.expr_jsdoc_type = nullptr;
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
        if (is_await && keyword_is_in) {
            // `for await (x in y)`: still a for-in loop
            Span in = { .source = p.shared->source, .sv = csview_from_n(p.pos - 2, 2) };
            parse_error(&p, in, 1005, "'of' expected.");
        }

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
                .await_place = (uint8_t)(is_await && !keyword_is_in ? parser_await_place(&p) : AWAIT_ALLOWED),
                .await_span = await_span,
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

// `with (x) statement` (tsc's parseWithStatement)
static bool parse_with_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "with")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    const char* open = p.pos;
    bool opened = expect_char(&p, '(');
    hop_whitespace(&p);

    Expr object = expr_implicit_any(span_from_pos(&p));
    if (expect_expr(&p, expr_ctx_null, &object)) {
        parse_comma_ops_right_side(&p, &object);
    }
    hop_whitespace(&p);

    if (opened) expect_closing(&p, ')', open);
    else expect_char(&p, ')');
    Span header = span_from(a);
    hop_whitespace(&p);

    Statement body = statement_noop;
    expect_statement(&p, false, &body);

    *ret = (Statement){
        .tag = STATEMENT_WITH,
        .with = c_new(WithStatement, {
            .object = object,
            .body = body,
            .header = header,
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
    if (yield_await_is_keyword_here(&p) || !parse_identifier(&p, &label)) {
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
    if (yield_await_is_keyword_here(&p)) {
        parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
        parse_success(&p);
    } else if (parse_identifier(&p, &label)) {
        *ret = label;
        parse_success(&p);
    } else {
        parse_fail(&p);
    }
}

// tsc's parseErrorForMissingSemicolonAfter for an expression statement that is an identifier followed by more on the
// same line: `module {`, `type = 1`, `asynd function f()`...
static void report_missing_semicolon_after_identifier(Parser* p, Span name) {
    Tok t = tok_peek(p);
    Span token = span_make(p->shared->source, t.start, t.end);
    if (span_equals(name, "const") || span_equals(name, "let") || span_equals(name, "var")) {
        parse_error(p, name, 1440, "Variable declaration not allowed at this location.");
    } else if (span_equals(name, "declare")) {
        // A declaration that failed to parse has reported already
    } else if (span_equals(name, "interface")) {
        if (tok_is(t, "{")) {
            parse_error(p, token, 1438, "Interface must be given a name.");
        } else {
            parse_error(p, token, 2427, "Interface name cannot be '", token, "'.");
        }
    } else if (span_equals(name, "is")) {
        parse_error(p, span_make(p->shared->source, span_start_ptr(name), t.start), 1228,
            "A type predicate is only allowed in return type position for functions and methods.");
    } else if (span_equals(name, "module") || span_equals(name, "namespace")) {
        if (tok_is(t, "{")) {
            parse_error(p, token, 1437, "Namespace must be given a name.");
        } else {
            parse_error(p, token, 2819, "Namespace name cannot be '", token, "'.");
        }
    } else if (span_equals(name, "type")) {
        if (tok_is(t, "=")) {
            parse_error(p, token, 1439, "Type alias must be given a name.");
        } else {
            parse_error(p, token, 2457, "Type alias name cannot be '", token, "'.");
        }
    } else {
        // A misspelled keyword (tsc's viableKeywordSuggestions: the keywords longer than two characters), or one
        // missing its space (`declareconst`)
        csview keywords[c_arraylen(all_keywords)];
        isize count = 0;
        for (const char* const* k = all_keywords; *k; k++) {
            if (strlen(*k) > 2) keywords[count++] = c_sv(*k, strlen(*k));
        }
        isize best = spelling_suggestion(name.sv, keywords, count);
        if (best >= 0) {
            parse_error(p, name, 1435, "Unknown keyword or identifier. Did you mean '", keywords[best], "'?");
            return;
        }
        for (isize i = 0; i < count; i++) {
            if (name.sv.size > keywords[i].size + 2 && memcmp(name.sv.buf, keywords[i].buf, keywords[i].size) == 0) {
                csview rest = { name.sv.buf + keywords[i].size, name.sv.size - keywords[i].size };
                parse_error(p, name, 1435, "Unknown keyword or identifier. Did you mean '", keywords[i], " ", rest, "'?");
                return;
            }
        }
        parse_error(p, name, 1434, "Unexpected keyword or identifier.");
    }
}

// Whether tsc ends the statement with parseSemicolon(), so that a `;` after it is part of it rather than an empty
// statement (`interface I {};` is two statements)
static bool statement_takes_semicolon(Statement const* me) {
    switch (my tag) {
        case STATEMENT_EXPR: case STATEMENT_EXPORT_DEFAULT_EXPR: case STATEMENT_RETURN_EXPR: case STATEMENT_THROW_EXPR:
        case STATEMENT_BREAK: case STATEMENT_CONTINUE: case STATEMENT_IMPORT_EXPORT: case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_TYPE:
            return true;
        case STATEMENT_NOOP: return span_valid(my span) && my span.sv.size && my span.sv.buf[0] == 'd'; // `debugger`
        case STATEMENT_DECL_FUNCTION: return !my decl.function.impl->has_body;
        case STATEMENT_DECL_NAMESPACE: return my decl.namespace.is_shorthand;
        case STATEMENT_FOR_LOOP: return my for_loop->is_do_while;
        default: return false;
    }
}

// ctx is the kind of list: source elements, block statements or a switch clause's statements
// What a statement parsed outside of a statement list (an `if` arm) left behind
static void jsdoc_drop_enum(Parser* p) {
    if (!p->shared->jsdoc_enum) return;
    statement_drop(p->shared->jsdoc_enum);
    free(p->shared->jsdoc_enum);
    p->shared->jsdoc_enum = nullptr;
}

// Whether the statement at p could be one tsc's isStartOfStatement rejects but parse_statement() accepts (as an
// expression statement with a missing semicolon: `private x = 1;`, `export extension class C`, `import x`); anything
// else that isn't a statement (`const` without a declaration) fails to parse. Keeps the full predicate off the path of
// ordinary statements
static bool statement_start_needs_check(Parser* p) {
    switch (*p->pos) {
        case 'a': return starts_with_followed_by_word_break(p, "accessor");
        case 'e': {
            if (!starts_with_followed_by_word_break(p, "export")) return false;
            // The usual `export <declaration>` needs no check (the predicate's lookahead is most of a .d.ts statement)
            Parser next = parser(p);
            next.pos += strlen("export");
            hop_whitespace(&next);
            static const char* const usual[] = {
                "declare", "class", "function", "interface", "type", "const", "let", "var", "enum", "namespace",
                "module", "default", "abstract", "async", "import", nullptr,
            };
            bool usual_start = strchr("{*=@", *next.pos) != nullptr;
            for (int i = 0; !usual_start && usual[i]; i++) {
                usual_start = starts_with_followed_by_word_break(&next, usual[i]);
            }
            parse_fail(&next);
            return !usual_start;
        }
        case 'i': {
            if (!starts_with_followed_by_word_break(p, "import")) return false;
            // (tsc's isStartOfDeclaration and import calls/`import.meta`: nearly always a declaration)
            Parser next = parser(p);
            next.pos += strlen("import");
            hop_whitespace(&next);
            char c = *next.pos;
            bool word = identifier_char_size(next.pos, true);
            parse_fail(&next);
            return !(word || (c && strchr("\"'*{(<.", c)));
        }
        case 'p':
            return starts_with_followed_by_word_break(p, "public") || starts_with_followed_by_word_break(p, "private") ||
                starts_with_followed_by_word_break(p, "protected");
        case 'r': return starts_with_followed_by_word_break(p, "readonly");
        case 's': return starts_with_followed_by_word_break(p, "static");
        default: return false;
    }
}

// list_start: where the list's own text starts (past the `{`), for the JSDoc comments it binds
static bool parse_append_statement_sequence(Parser* parent, bool toplevel, ParsingContext ctx, const char* list_start, VecStatement* ret) {
    Parser p = parser(parent);
    const char* jsdoc_from = list_start;

    u32 saved_context = list_enter(&p, ctx);
    ModifiedParent outer_parent = p.shared->statement_parent;
    p.shared->statement_parent = !toplevel ? MODIFIED_IN_OTHER :
        ctx == PARSING_SOURCE_ELEMENTS ? MODIFIED_IN_SOURCE_FILE :
        MODIFIED_IN_MODULE_BLOCK;
    while(
        *p.pos &&
        (ctx == PARSING_SOURCE_ELEMENTS || *p.pos != '}') &&
        !(ctx == PARSING_SWITCH_CLAUSE_STATEMENTS && (
            starts_with_followed_by_word_break(&p, "default") ||
            starts_with_followed_by_word_break(&p, "case")
        ))
    ) {
        Statement statement = {0};
        if (*p.pos == ';') {
            // tsc's EmptyStatement
            statement = statement_noop;
            statement.span = span_make(p.shared->source, p.pos, p.pos + 1);
            vec_statement_push(ret, statement);
            p.pos++;
        } else if (
            (!statement_start_needs_check(&p) || list_is_element(&p, ctx, tok_peek(&p))) &&
            (jsdoc_drop_enum(&p), parse_statement(&p, toplevel, &statement))
        ) {
            vec_statement_push(ret, statement);
            if (p.shared->jsdoc_enum) {
                vec_statement_push(ret, *p.shared->jsdoc_enum);
                free(p.shared->jsdoc_enum);
                p.shared->jsdoc_enum = nullptr;
            }
            if (p.shared->source->is_js) {
                jsdoc_bind_comments(&p, jsdoc_from, p.pos, true, toplevel, ret);
                jsdoc_from = p.pos;
            }
            bool ended_on_brace = p.pos > source_file_start(p.shared->source) && *(p.pos - 1) == '}';
            hop_whitespace(&p);
            if (*p.pos == ';' && statement_takes_semicolon(&statement)) {
                // tsc's parseSemicolon(): also after a newline or a brace (`x = {}\n;`), which is no empty statement
                p.pos++;
                Statement* pushed = vec_statement_back_mut(ret);
                pushed->span = span_make(p.shared->source, span_start_ptr(pushed->span), p.pos);
            } else if (p.whitespace == WHITESPACE_NEWLINE ||
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
                    report_missing_semicolon_after_identifier(&p, statement.expr.inner_span);
                } else {
                    expected_token_error(&p, ";");
                }
            }
        } else if (*p.pos == '\\') {
            parse_error(&p, span_from_pos(&p), 1127, "Invalid character.");
            p.pos++;
        } else if (list_abort_or_skip(&p, ctx)) {
            break;
        }
        hop_whitespace(&p); // TODO: is this needed?
    }
    list_leave(&p, saved_context);
    shrink_list(vec_statement, ret);
    p.shared->statement_parent = outer_parent;
    // (the end of the file has JSDoc in tsc, a block's `}` doesn't)
    if (p.shared->source->is_js) jsdoc_bind_comments(&p, jsdoc_from, p.pos, ctx == PARSING_SOURCE_ELEMENTS, toplevel, ret);

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
        ret->head = span_from(a);
    } else if (hop_start_if_ends_on_word_break(&p, "default")) {
        hop_whitespace(&p);

        expect_char(&p, ':');

        *ret = (SwitchCase){
            .is_pattern = false,
            .default_span = span_from(a),
            .statements = {0},
            .head = span_from(a),
        };
    } else {
        return parse_fail(&p);
    }
    const char* list_start = p.pos;
    hop_whitespace(&p);

    if (!parse_append_statement_sequence(&p, false, PARSING_SWITCH_CLAUSE_STATEMENTS, list_start, &ret->statements)) {
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

    u32 saved_context = list_enter(&p, PARSING_SWITCH_CLAUSES);
    while(*p.pos && *p.pos != '}') {
        SwitchCase case_;
        if (parse_switch_case(&p, &case_)) {
            vec_switch_case_push(&switch_.cases, case_);
        } else if (list_abort_or_skip(&p, PARSING_SWITCH_CLAUSES)) {
            break;
        }
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);
    expect_char(&p, '}');

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

                if (!parse_binding_content_or_private_name(&p, &try_catch.catch_var, 18029,
                        "Private identifiers are not allowed in variable declarations.")) {
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

    if (!hop_start_if_ends_on_word_break(&p, "throw")) {
        return parse_fail(&p);
    }
    const char* keyword_end = p.pos;
    hop_whitespace(&p);

    if (p.whitespace == WHITESPACE_NEWLINE) {
        // tsc's parseThrowStatement: no line break after `throw`, the expression is missing (zero-width, where the next
        // token starts: the checker's TS1142 goes after the keyword)
        *ret = expr_implicit_any(span_make(p.shared->source, p.pos, p.pos));
        p.pos = keyword_end;
    } else if (expect_expr(&p, expr_ctx_null, ret)) {
        parse_comma_ops_right_side(&p, ret);
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
                bool outer_in_namespace = p.shared->in_namespace_body;
                p.shared->in_namespace_body = false;
                bool has_block = parse_block(&p, true, &ret->contents);
                p.shared->in_namespace_body = outer_in_namespace;
                if (!has_block) {
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
    bool outer_in_namespace = p.shared->in_namespace_body;
    p.shared->in_namespace_body = true;
    bool has_block = parse_block(&p, true, &contents);
    p.shared->in_namespace_body = outer_in_namespace;
    if (!has_block) {
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
    bool outer_in_namespace = p.shared->in_namespace_body;
    p.shared->in_namespace_body = false;
    bool has_block = parse_block(&p, true, &ret->contents);
    p.shared->in_namespace_body = outer_in_namespace;
    if (!has_block) {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

// Declarations that only exist in TypeScript, reported at their name with nothing inside them (tsc's
// getJSSyntacticDiagnosticsForFile). `keyword`: where the declaration starts after its modifiers
static void js_syntax_declaration(Parser* p, intptr_t mark, const char* keyword, Statement const* s) {
    if (!reports_js_syntax(p)) return;
    Span name;
    unsigned code = 8006;
    char const* what;
    switch (s->tag) {
        case STATEMENT_DECL_INTERFACE: name = s->decl.interface->name; what = "interface"; break;
        case STATEMENT_DECL_ENUM: name = s->decl.enum_->name; what = "enum"; break;
        case STATEMENT_DECL_TYPE: name = s->decl.type.name; code = 8008; what = nullptr; break;
        case STATEMENT_DECL_NAMESPACE:
            name = s->decl.namespace.name;
            what = strncmp(keyword, "namespace", 9) == 0 ? "namespace" : "module";
            break;
        case STATEMENT_DECL_FUNCTION:
            if (s->decl.function.impl->has_body) return;
            name = s->decl.function.impl->name;
            code = 8017;
            what = nullptr;
            break;
        default: return;
    }
    js_syntax_skip_since(p, mark);
    if (!span_valid(name)) return;
    // (The first name of `namespace A.B`)
    if (s->tag == STATEMENT_DECL_NAMESPACE) {
        const char* dot = memchr(name.sv.buf, '.', (size_t)name.sv.size);
        if (dot) name = span_make(name.source, name.sv.buf, dot);
    }
    if (code == 8008) {
        js_syntax_error(p, name, 8008, "Type aliases can only be used in TypeScript files.");
    } else if (code == 8017) {
        js_syntax_error(p, name, 8017, "Signature declarations can only be used in TypeScript files.");
    } else {
        js_syntax_error(p, name, 8006, "'", what, "' declarations can only be used in TypeScript files.");
    }
}

static bool parse_decl_statement(Parser* parent, bool toplevel, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    intptr_t mark = error_mark(&p);

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
    const char* keyword = p.pos;
    DeclStatementProps decl_props = {
        .exported = modifiers.includes[MODIFIER_EXPORT_DEFAULT] ?
            DECL_EXPORTED_YES_DEFAULT :
            (modifiers.includes[MODIFIER_EXPORT] ?
                DECL_EXPORTED_YES :
                DECL_EXPORTED_NO
            ),
        .declared = modifiers.includes[MODIFIER_DECLARE] || p.shared->source->is_declaration,
    };
    for_each (i, vec_spanned_modifier, &modifiers.spanned) {
        if (i->tag == MODIFIER_DECLARE) decl_props.declare_keyword = i->span;
    }

    // tsc's checkGrammarModifiers() once the kind of declaration is known. What's inside a `declare` is ambient
    ModifiedNode modified = {
        .parent = toplevel ? p.shared->statement_parent : MODIFIED_IN_OTHER,
        .parent_ambient = p.shared->in_ambient,
        .parent_is_namespace = p.shared->in_namespace_body,
    };
    bool outer_ambient = p.shared->in_ambient;
    p.shared->in_ambient |= modifiers.includes[MODIFIER_DECLARE];

    if (decl_props.exported != DECL_EXPORTED_YES_DEFAULT &&
        parse_variable_statement(&p, expr_ctx_null, false, &parsed.variable)
    ) {
        modified.kind = MODIFIED_VARIABLE_STATEMENT;
        modified.using_kind = parsed.variable.keyword == VAR_INIT_USING ? 1 :
            parsed.variable.keyword == VAR_INIT_AWAIT_USING ? 2 :
            0;
        *ret = (Statement) {
            .tag = STATEMENT_DECL_VARIABLE,
            .decl = {
                .props = decl_props,
                .variable = parsed.variable,
            },
        };
    } else if (parse_function_keyword_function(&p, modifiers.includes[MODIFIER_ASYNC], &parsed.function)) {
        modified.kind = MODIFIED_FUNCTION;
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
    } else if (parse_class_decl(&p, modifiers.includes[MODIFIER_ABSTRACT], &parsed.class)) {
        modified.kind = MODIFIED_CLASS;
        parsed.class.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_CLASS,
            .decl = {
                .props = decl_props,
                .class = c_new(ClassDecl, parsed.class),
            },
        };
    } else if (parse_type_decl(&p, &parsed.type_decl)) {
        modified.kind = MODIFIED_TYPE_ALIAS;
        parsed.type_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_TYPE,
            .decl = {
                .props = decl_props,
                .type = parsed.type_decl,
            },
        };
    } else if (parse_interface_decl(&p, &parsed.interface_decl)) {
        modified.kind = MODIFIED_INTERFACE;
        parsed.interface_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_INTERFACE,
            .decl = {
                .props = decl_props,
                .interface = c_new(InterfaceDecl, parsed.interface_decl),
            },
        };
    } else if (parse_enum_decl(&p, &parsed.enum_decl)) {
        modified.kind = MODIFIED_ENUM;
        parsed.enum_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_ENUM,
            .decl = {
                .props = decl_props,
                .enum_ = c_new(EnumDecl, parsed.enum_decl),
            },
        };
    } else if (parse_global_block(&p, &ret->decl.namespace)) {
        modified.kind = MODIFIED_MODULE;
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
    } else if (decl_props.exported == DECL_EXPORTED_YES_DEFAULT && (parse_expr(&p, expr_ctx_null, &parsed.expr) ||
        // (`export default;`: TS1109 at the `;` and an export of nothing, like tsc)
        ((tok_is(tok_peek(&p), ";") || tok_is(tok_peek(&p), "}") || tok_is_eof(tok_peek(&p))) &&
            (expect_expr(&p, expr_ctx_null, &parsed.expr), true)))
    ) {
        modified.kind = MODIFIED_EXPORT_ASSIGNMENT;
        mark_named_evaluation(&parsed.expr, false);
        *ret = (Statement){
            .tag = STATEMENT_EXPORT_DEFAULT_EXPR,
            .expr = parsed.expr,
        };
    } else if (decl_props.exported != DECL_EXPORTED_YES_DEFAULT && parse_namespace(&p, &ret->decl.namespace)) {
        modified.kind = MODIFIED_MODULE;
        ret->decl.namespace.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_NAMESPACE,
            .decl = {
                .props = decl_props,
                .namespace = ret->decl.namespace,
            },
        };
    } else if (!vec_spanned_modifier_is_empty(&modifiers.spanned) && vec_expr_is_empty(&decorators) &&
        (*p.pos == 'i' || *p.pos == 'e') &&
        parse_import_export_statement(&p, ret)
    ) {
        // `export public import a = b`, `export export = x`, `declare import x from "m"`: tsc parses modifiers in front
        // of any declaration and complains in the checker
        unsigned code = 0;
        if (ret->tag == STATEMENT_EXPORT_DEFAULT_EXPR) {
            modified.kind = MODIFIED_EXPORT_ASSIGNMENT;
            code = 1120;
        } else {
            PortItem* main = &ret->import_export->main;
            bool is_import = span_equals(ret->import_export->keyword_span, "import");
            if (main->tag == PORT_IMPORT_ALIAS || main->is_require) {
                modified.kind = MODIFIED_IMPORT_EQUALS;
                main->export_import |= modifiers.includes[MODIFIER_EXPORT];
                main->declared |= modifiers.includes[MODIFIER_DECLARE];
            } else if (is_import) {
                modified.kind = MODIFIED_IMPORT;
                code = 1191;
            } else if (main->tag == PORT_EXPORT_AS_NAMESPACE) {
                modified.kind = MODIFIED_NAMESPACE_EXPORT;
                main->full_span = span_from(a);
                main->modifiers = main->full_span;
            } else {
                modified.kind = MODIFIED_EXPORT_DECLARATION;
                code = 1193;
            }
        }
        p.shared->in_ambient = outer_ambient;
        // (Decorators would be TS1206, but the statement has nowhere to keep them for the baselines' walk yet)
        Span first = vec_spanned_modifier_front(&modifiers.spanned)->span;
        if (!check_modifiers(&p, &modifiers, modified)) {
            if (code == 1120) parse_error(&p, first, 1120, "An export assignment cannot have modifiers.");
            if (code == 1191) parse_error(&p, first, 1191, "An import declaration cannot have modifiers.");
            if (code == 1193) parse_error(&p, first, 1193, "An export declaration cannot have modifiers.");
        }
        modifier_list_drop(&modifiers);
        vec_expr_drop(&decorators);
        return parse_success(&p);
    } else {
        p.shared->in_ambient = outer_ambient;
        modifier_list_drop(&modifiers);
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }
    p.shared->in_ambient = outer_ambient;
    check_modifiers(&p, &modifiers, modified);
    js_syntax_declaration(&p, mark, keyword, ret);

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
// `/** @satisfies {T} */ const x = v`: v is checked like `v satisfies T` (tsc's checkDeclarationInitializer and
// getContextualTypeForVariableLikeDeclaration: tryGetJSDocSatisfiesTypeNode). A second `@satisfies` of the host is
// TS1223 (tsc's checkJSDocSatisfiesTag: `/** @satisfies {T} */ (v)` is the first of the initializer's)
static void jsdoc_apply_satisfies_initializer(Parser* p, VecJsDocTag* tags, InitializedBinding* initializer) {
    JsDocTag* tag = jsdoc_find_typed(tags, JSDOC_TAG_SATISFIES);
    // (Not an empty object literal: that's a JavaScript container, whose type isn't the initializer's, tsc's
    // getJSContainerObjectType)
    if (!tag || initializer->value.tag == EXPR_IMPLICIT_ANY ||
        (initializer->value.tag == EXPR_OBJECT && !initializer->value.object.size)
    ) {
        return;
    }
    Expr* value = &initializer->value;
    if (value->tag == EXPR_AS_TYPE && value->as_type->is_jsdoc && value->as_type->is_satisfies) {
        jsdoc_error(p, tag->tag_name, 1223, "'satisfies' tag already specified.");
        return;
    }
    Expr inner = *value;
    *value = expr_as_type(inner.outer_span, c_new(AsTypeExpr, {
        .expr = inner,
        .keyword_span = tag->tag_name,
        .type = jsdoc_take_type(tag, nullptr),
        .is_satisfies = true,
        .is_jsdoc = true,
    }));
}

static void jsdoc_apply_statement(Parser* p, const char* start, Statement* statement) {
    VecJsDocTag tags = {0};
    if (!jsdoc_before(p, start, &tags)) return;
    switch (statement->tag) {
        case STATEMENT_DECL_VARIABLE: {
            VecInitializedBinding* list = &statement->decl.variable.initializers;
            if (vec_initialized_binding_size(list) != 1) break;
            InitializedBinding* initializer = list->data;
            JsDocTag* enum_tag = jsdoc_find_typed(&tags, JSDOC_TAG_ENUM);
            if (enum_tag && initializer->binding.tag == BINDING_IDENTIFIER) {
                // `/** @enum {T} */ const E = {...}`: E is also a type alias of T (tsc's JSDocEnumTag)
                Span span = jsdoc_tag_decl_span(enum_tag);
                if (p->shared->jsdoc_enum) {
                    statement_drop(p->shared->jsdoc_enum);
                    free(p->shared->jsdoc_enum);
                }
                TypeNode type = jsdoc_take_type(enum_tag, nullptr);
                type.from_jsdoc = false;
                if (initializer->value.tag == EXPR_OBJECT) initializer->value.jsdoc_enum_literal = true;
                p->shared->jsdoc_enum = c_new(Statement, (Statement){
                    .tag = STATEMENT_DECL_TYPE,
                    .decl.type = {
                        .name = initializer->binding.content_span, .full_span = span, .type = type, .jsdoc = true,
                        .jsdoc_enum = true,
                    },
                    // Exported with its variable only (tsc's jsdocTreatAsExported)
                    .decl.props.exported = statement->decl.props.exported,
                    .span = span,
                });
            }
            jsdoc_apply_type_to_binding(&tags, &initializer->binding);
            jsdoc_apply_satisfies_initializer(p, &tags, initializer);
            FunctionImpl* function = jsdoc_function_of(&initializer->value);
            if (function && !function->ret_explicit && initializer->binding.has_explicit_type &&
                initializer->binding.explicit_type.from_jsdoc
            ) {
                FunctionImpl const* signature = jsdoc_single_signature(&initializer->binding.explicit_type);
                if (signature && signature->ret_explicit) function->jsdoc_tag_ret = &signature->ret;
            }
            if (function) jsdoc_apply_function(&tags, function);
            if (initializer->value.tag == EXPR_CLASS_DECL) jsdoc_apply_class(p, &tags, initializer->value.class_decl);
        }   break;
        case STATEMENT_DECL_FUNCTION: {
            // (tsc's checkJSDocAugmentsTag)
            FunctionImpl const* function = statement->decl.function.impl;
            for_each (i, vec_jsdoc_tag, &tags) {
                if (i->kind != JSDOC_TAG_EXTENDS || i->earlier || i->nested || !span_valid(function->name)) continue;
                jsdoc_error(p, function->name, 8022, "JSDoc '@", i->tag_name.sv, "' is not attached to a class.");
            }
            jsdoc_apply_function(&tags, statement->decl.function.impl);
        }   break;
        case STATEMENT_DECL_CLASS:
            jsdoc_apply_class(p, &tags, statement->decl.class);
            break;
        case STATEMENT_EXPR:
            if (statement->expr.tag == EXPR_PROPERTY && !expr_is_perenthisized(&statement->expr)) {
                JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
                if (tag) statement->expr_jsdoc_type = c_new(TypeNode, jsdoc_take_type(tag, nullptr));
            }
            if (statement->expr.tag == EXPR_BINARY_OP && statement->expr.binary_op->op == BINARY_OP_EQUAL) {
                BinaryOpExpr* assignment = statement->expr.binary_op;
                // (Through a chain `a = b = function () {}`: tsc's getNextJSDocCommentLocation goes up through
                // assignments)
                Expr* assigned = &assignment->right;
                while (assigned->tag == EXPR_BINARY_OP && assigned->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(assigned)) {
                    assigned = &assigned->binary_op->right;
                }
                FunctionImpl* function = jsdoc_function_of(assigned);
                if (function) {
                    jsdoc_apply_function(&tags, function);
                } else if (assignment->right.tag == EXPR_CLASS_DECL && !expr_is_perenthisized(&assignment->right)) {
                    jsdoc_apply_class(p, &tags, assignment->right.class_decl);
                } else {
                    // The declared type of what the assignment declares (`this.x`, `exports.x`, `module.exports`)
                    JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
                    if (tag) assignment->jsdoc_type = c_new(TypeNode, jsdoc_take_type(tag, nullptr));
                    // (and of the assignments it assigns: `this.a = this.b = v`)
                    for (Expr* e = &assignment->right; assignment->jsdoc_type && e->tag == EXPR_BINARY_OP &&
                        e->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(e) && !e->binary_op->jsdoc_type;
                        e = &e->binary_op->right
                    ) {
                        e->binary_op->jsdoc_type = assignment->jsdoc_type;
                        e->binary_op->jsdoc_type_borrowed = true;
                    }
                }
                assignment->jsdoc_modifiers = jsdoc_modifiers(&tags);
            }
            break;
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR: {
            FunctionImpl* function = jsdoc_function_of(&statement->expr);
            if (function) {
                jsdoc_apply_function(&tags, function);
            } else if (statement->tag == STATEMENT_EXPORT_DEFAULT_EXPR && !statement->export_equals) {
                // `/** @type {T} */ export default e`: e is checked against T and T is the export's type (tsc's
                // checkExportAssignment, getEffectiveTypeAnnotationNode). `export default (e)` is a cast: the tag is
                // the parenthesized expression's too (getNextJSDocCommentLocation)
                JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
                Expr* e = &statement->expr;
                if (tag && expr_is_perenthisized(e) && !(e->tag == EXPR_AS_TYPE && e->as_type->is_jsdoc)) {
                    Expr inner = *e;
                    inner.outer_span = inner.inner_span;
                    *e = expr_as_type(e->outer_span, c_new(AsTypeExpr, {
                        .expr = inner,
                        .keyword_span = tag->type_span,
                        .type = jsdoc_take_type(tag, nullptr),
                        .is_jsdoc = true,
                    }));
                } else if (tag) {
                    statement->expr_jsdoc_type = c_new(TypeNode, jsdoc_take_type(tag, nullptr));
                }
            }
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
    // Every branch before the expression statement needs a particular first character: test it first
    char c = *p.pos;
    bool word = identifier_char_size(p.pos, true);
    if (c == ';') {
        *ret = statement_noop;
    } else if (hop_start_if_ends_on_word_break(&p, "debugger")) {
        *ret = statement_noop;
    } else if (word && parse_label(&p, &label)) {
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
    } else if ((c == 'i' || c == 'e') && parse_import_export_statement(&p, ret)) {
    } else if ((word || c == '@') && parse_decl_statement(&p, toplevel, ret)) {
    } else if (c == '{' && parse_block(&p, false, &ret->block)) {
        ret->tag = STATEMENT_BLOCK;
    } else if (c == 'i' && parse_if_else(&p, &ret->if_else)) {
        ret->tag = STATEMENT_IF_ELSE;
    } else if (c == 'f' && parse_for_loop_statement(&p, ret)) {
    } else if (c == 'w' && parse_while_loop_statement(&p, ret)) {
    } else if (c == 'w' && parse_with_statement(&p, ret)) {
    } else if (c == 'd' && parse_do_while_loop_statement(&p, ret)) {
    } else if (c == 's' && parse_switch_statement(&p, ret)) {
    } else if (c == 't' && parse_try_catch_statement(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "break")) {
        ret->tag = STATEMENT_BREAK;
        ret->keyword_span = span_from(a);
        parse_jump_target(&p, &label);
    } else if (hop_start_if_ends_on_word_break(&p, "continue")) {
        ret->tag = STATEMENT_CONTINUE;
        ret->keyword_span = span_from(a);
        parse_jump_target(&p, &label);
    } else if (c == 'r' && parse_return(&p, &ret->expr)) {
        ret->tag = STATEMENT_RETURN_EXPR;
        ret->expr_jsdoc_type = nullptr;
    } else if (c == 't' && parse_throw(&p, &ret->expr)) {
        ret->tag = STATEMENT_THROW_EXPR;
        ret->expr_jsdoc_type = nullptr;
    } else if (parse_expr(&p, expr_ctx_null, &ret->expr)) {
        ret->tag = STATEMENT_EXPR;
        ret->expr_jsdoc_type = nullptr;
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
    if (!parse_append_statement_sequence(&p, toplevel, PARSING_BLOCK_STATEMENTS, open + 1, &ret->statements)) {
        *ret = (Block){0};
    }
    hop_whitespace(&p);

    expect_closing(&p, '}', open);
    return parse_success(&p);
}

static bool is_grammar_code(unsigned code) {
    static unsigned const grammar_codes[] = {
        1009, 1017, 1018, 1019, 1020, 1022, 1024, 1025, 1028, 1029, 1030, 1031, 1038, 1040, 1042, 1044, 1070, 1071,
        1079, 1089, 1090, 1096, 1097, 1120, 1123, 1162, 1172, 1173, 1174, 1175, 1176, 1184, 1191, 1193, 1206, 1242, 1243, 1244,
        1248, 1253, 1255, 1263, 1264, 1273, 1274, 1275, 1276, 1277, 1319, 1491, 1495, 18010, 18012, 18019,
    };
    for (u32 c = 0; c < c_arraylen(grammar_codes); c++) {
        if (code == grammar_codes[c]) return true;
    }
    return false;
}

void parse_file(SourceFile* source, ErrorCtx* errs) {
    ParserShared shared = {
        .source = source,
        .fail_cache_start = source_file_start(source),
        .errors = vec_error_init(),
        .in_ambient = source->is_declaration,
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
    hop_conflict_markers(&p);
    if (source->is_json) {
        // A JSON file is the value of one expression (tsc's parseJsonText), which is what the module is
        hop_whitespace(&p);
        Anchor a = anchor(&p);
        Expr value;
        if (parse_expr(&p, expr_ctx_null, &value)) {
            Statement statement = { .tag = STATEMENT_EXPORT_DEFAULT_EXPR, .expr = value, .export_equals = true };
            statement.span = span_from(a);
            vec_statement_push(&source->ast.statements, statement);
        }
    } else {
        parse_source_directives(&p, &source->ast.statements);
        hop_whitespace(&p);
        parse_append_statement_sequence(&p, true, PARSING_SOURCE_ELEMENTS, source_file_start(source), &source->ast.statements);
    }

    // Grammar errors tsc's checker reports (checkGrammarModifiers, checkGrammarIndexSignatureParameters,
    // checkGrammarForInvalidQuestionMark...) that the parser is in a better place to report. They don't count as
    // parse errors, and like all grammar errors they aren't reported in a file with parse errors (hasParseDiagnostics)
    source->has_import_meta = shared.saw_import_meta;
    source->has_commonjs = shared.saw_commonjs;
    free(shared.exports_aliases);
    // tsc's binder declares `module` at the first `module.exports` it binds once the file is CommonJS (binding goes
    // in source order, a node before its children)
    source->module_exports_span = span_null;
    for (u32 i = 0; shared.commonjs_start && i < shared.module_exports_access_count; i++) {
        Span access = shared.module_exports_accesses[i];
        Span* first = &source->module_exports_span;
        if (span_start_ptr(access) >= shared.commonjs_start &&
            (!span_valid(*first) || span_start_ptr(access) < span_start_ptr(*first))
        ) {
            *first = access;
        }
    }
    free(shared.module_exports_accesses);
    source->has_jsx = shared.saw_jsx;
    source->has_parse_errors = false;
    for_each (i, vec_error, &shared.errors) {
        source->has_parse_errors |= !is_grammar_code(i->ts_code) && !is_js_syntax_code(i->ts_code);
    }
    for_each (i, vec_error, &shared.errors) {
        if (source->has_parse_errors && is_grammar_code(i->ts_code)) {
            error_drop(i);
        } else {
            error_ctx_append_error(errs, *i);
        }
        *i = (Error){0};
    }
    // Every attempt that parsed a comment's host reported its errors: the same error at the same place once
    for (isize i = 0; i < shared.jsdoc_errors.size; i++) {
        Error* error = &shared.jsdoc_errors.data[i];
        bool seen = false;
        for (isize j = 0; j < i && !seen; j++) {
            Error const* other = &shared.jsdoc_errors.data[j];
            seen = other->ts_code == error->ts_code && other->span.sv.buf == error->span.sv.buf &&
                other->span.sv.size == error->span.sv.size;
        }
        if (seen) continue;
        error_ctx_append_error(errs, *error);
        *error = (Error){0};
    }

    parser_shared_drop(&shared);
}
