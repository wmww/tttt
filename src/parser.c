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

#define i_key uint8_t
#define i_type vec_u8
#include "wrap_stc/vec.h"
typedef vec_u8 VecU8;

#define MAX_ERRORS 500 // Must be less than the max value storable in Parser.error_count

typedef struct ParserShared {
    SourceFile const* source;
    const char* fail_cache_start;
    VecU8 fail_cache;
    VecError errors;
} ParserShared;

void parser_shared_drop(ParserShared* me) {
    vec_u8_drop(&my fail_cache);
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
} TypePrecedence;

static bool hop_token(Parser* parent);
static bool parse_literal(Parser* parent, Value* ret);
static bool parse_binary_op(Parser* parent, BinaryOp* op);
static bool parse_left_unary_op(Parser* parent, UnaryOp* op);
static bool parse_right_unary_op(Parser* parent, UnaryOp* op);
static bool parse_block(Parser* parent, bool toplevel, Block* ret);
static bool parse_interface(Parser* parent, Interface* ret);
static bool parse_class_item(Parser* parent, bool is_type, bool is_abstract, ClassItem* ret);
static bool parse_statement(Parser* parent, bool toplevel, Statement* ret);
static inline bool expect_statement(Parser* parent, bool toplevel, Statement* ret);
static bool parse_type(Parser* parent, TypePrecedence min_precedence, Type* ret);
static inline bool parse_type_or_set_to_default(Parser* parent, TypePrecedence min_precedence, Type* ret);
static inline bool expect_type(Parser* parent, TypePrecedence min_precedence, Type* ret);
static bool parse_function_args(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_function_args,
    FunctionImpl* ret
);
static bool parse_number(Parser* parent, double* ret);
static bool parse_type_compat_literal(Parser* parent, Value* ret);
static bool parse_expr(Parser* parent, ExprCtx ctx, Expr* ret);
static inline bool expect_expr(Parser* parent, ExprCtx ctx, Expr* ret);
static bool parse_binding_content(Parser* parent, Binding* ret);
static bool parse_binding_type_or_set_to_default(Parser* parent, Binding* ret);
static bool parse_initialization_or_set_to_default(Parser* parent, ExprCtx ctx, Expr* ret);

static inline Parser parser(Parser* parent) {
#ifdef DEBUG_BUILD
    ASSERT(!parent->has_panding_child);
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
    ASSERT(my parent->has_panding_child);
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
    ASSERT(my parent->has_panding_child);
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
    my shared->fail_cache._len = 0;
}

// Should only be called for the current active parser
static inline Error* add_error_to_parser(Parser* me, Error error) {
#ifdef DEBUG_BUILD
    ASSERT(!my has_panding_child);
#endif
    my error_count++;
    return vec_error_push(&my shared->errors, error);
}

static Error* parse_error(Parser* me, Span span, unsigned ts_code, const char* fmt, ...) __attribute__ ((format (printf, 4, 5)));
static Error* parse_error(Parser* me, Span span, unsigned ts_code, const char* fmt, ...) {
    cstr message = cstr_null;
    if (vec_error_size(&my shared->errors) >= MAX_ERRORS) {
        return nullptr;
    } else if (vec_error_size(&my shared->errors) == MAX_ERRORS - 1) {
        ts_code = TS_CODE_NONE;
        message = cstr_lit("Too many errors, additional errors not shown.");
    } else {
        va_list args;
        va_start(args, fmt);
        cstr_vfmt(&message, 0, fmt, args);
        va_end(args);
    }
    return add_error_to_parser(me, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code,
        .phase = ERROR_PHASE_PARSE,
    });
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

static inline bool hop_comment(Parser* parent) {
    if (*parent->pos != '/') {
        return false;
    }
    switch (*(parent->pos + 1)) {
        case '/':
            (parent->pos) += 2;
            while (*parent->pos && *parent->pos != '\n') {
                (parent->pos)++;
            }
            if (parent->whitespace == WHITESPACE_NONE) {
                parent->whitespace = WHITESPACE_PARSED;
            }
            break;

        case '*': {
            if (parent->whitespace == WHITESPACE_NONE) {
                parent->whitespace = WHITESPACE_PARSED;
            }
            const char* pos = parent->pos;
            pos += 2;
            while (*(pos - 1) != '*' || *pos != '/') {
                switch (*pos) {
                    case '\n': parent->whitespace = WHITESPACE_NEWLINE; break;
                    case '\0': return false;
                    default: break;
                }
                pos++;
            }
            pos++;
            parent->pos = pos;
        }   break;

        default: return false;
    }
    return true;
}

static inline void hop_whitespace(Parser* parent) {
    // TODO unicode and additional character support: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Lexical_grammar
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
                parent->whitespace = WHITESPACE_NEWLINE;
                parent->pos++;
                break;

            case '/':
                if (!hop_comment(parent)) {
                    goto ws_end;
                }
                break;

            default: goto ws_end;
        }
        if (parent->whitespace == WHITESPACE_NONE) {
            parent->whitespace = WHITESPACE_PARSED;
        }
    }

ws_end:
    parent->whitespace_end = parent->pos;
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
        parse_error(parent, span_from_token(parent), 1005, "'%c' expected.", c);
        return false;
    }
}

static Error* expected_token_error(Parser* parent, const char* expected) {
    return parse_error(parent, span_from_token(parent), 1005, "'%s' expected.", expected);
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
        if (p.whitespace == WHITESPACE_NEWLINE) {
            goto loop_end;
        }

        if (!is_identifier_char(*p.pos)) {
            switch (*p.pos) {
                case '[': case '"': case '\'': break;

                case '(':
                case '{':
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

static void modifier_list_validate_during_parse(Parser* parent, ModifierList const* me, Span name, ModifierValidationFlags flags) {
    ErrorCtx errs = { .phase = ERROR_PHASE_PARSE };
    modifier_list_validate(me, name, flags, &errs);
    c_foreach(i, vec_error, errs.errors) {
        add_error_to_parser(parent, *i.ref);
        *i.ref = (Error){0};
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

    Span name;
    if (!parse_identifier(&p, &name)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Type extends = type_any;
    bool extends_explicit = false;
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        extends_explicit = true;
        hop_whitespace(&p);
        parse_type_or_set_to_default(&p, TYPE_PRECEDENCE_NULL, &extends);
        hop_whitespace(&p);
    }

    Type fallback = type_any;
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
        Type t;
        Anchor type_a = anchor(&p);
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
            vec_spanned_type_push(ret, spanned_type(span_from(type_a), t));
            hop_whitespace(&p);
        } else if (*p.pos == ',') {
            vec_spanned_type_push(ret, spanned_type(span_from_pos(&p), (Type){
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

    if (vec_spanned_type_empty(ret)) {
        parse_error(&p, span_from(a), 1099, "Type argument list cannot be empty.");
    }

    return parse_success(&p);

fail:
    vec_spanned_type_drop(ret);
    return parse_fail(&p);
}

static bool parse_ternary_type(Parser* parent, Type* left_and_ret) {
    Parser p = parser(parent);
    Type maybe_super = {0};
    Type if_true = {0};
    Type if_false = {0};

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

        expect_type(&p, TYPE_PRECEDENCE_NULL, &elem.type);
        hop_whitespace(&p);

        vec_tuple_element_push(ret, elem);

        if (*p.pos != ']' && !expect_char(&p, ',')) {
            return parse_success(&p);
        }
        hop_whitespace(&p);
    }
    expect_char(&p, ']');

    return parse_success(&p);

}

static bool parse_type_right_side(Parser* parent, TypePrecedence min_precedence, Type* left_and_ret) {
    Parser p = parser(parent);
    Type type;

    hop_whitespace(&p);

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
            *left_and_ret = type_array(c_new(Type, *left_and_ret), false);
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

            expect_type(&p, TYPE_PRECEDENCE_INTERSECTION, &type);
            *left_and_ret = type_composite_without_simplify(TYPE_INTERSECTION, *left_and_ret, type);
            return parse_success(&p);
        }
    }

    if (TYPE_PRECEDENCE_UNION >= min_precedence) {
        if (hop_char(&p, '|') && *p.pos != '|' && *p.pos != '=') {
            hop_whitespace(&p);

            expect_type(&p, TYPE_PRECEDENCE_UNION, &type);
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
        vec_span_push(&namespaces, name);
        hop_whitespace(&p);
        expect_identifier(&p, &name);
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

    Type extends;
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

static bool parse_type(Parser* parent, TypePrecedence min_precedence, Type* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    union {
        IdentifierType identifier;
        InferredType inferred;
        FunctionImpl func;
        Type type;
        VecTupleElement tuple_elements;
    } parsed;

    Span readonly_span = span_null;
    if (hop_start_if_ends_on_word_break(&p, "readonly")) {
        readonly_span = span_from(a);
        hop_whitespace(&p);
    }

    if (hop_start_if_ends_on_word_break(&p, "any")) {
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
        expect_type(&p, min_precedence, &parsed.type);
        *ret = type_keyof(c_new(KeyofType, {
            .interface = parsed.type,
            .has_cache = false,
            .cached = {0},
        }));
    } else if (hop_start_if_ends_on_word_break(&p, "typeof")) {
        hop_whitespace(&p);
        Expr expr;
        ExprCtx expr_ctx = {
            .reject_operators = true,
        };
        if (!parse_expr(&p, expr_ctx, &expr)) {
            parse_error(&p, span_from_token(&p), 1003, "Identifier expected.");
            expr = expr_implicit_any(span_from_pos(&p));
        }
        *ret = type_typeof(c_new(TypeofType, {
            .expr = c_new(Expr, expr),
            .owns_expr = true,
        }));
    } else if (parse_type_compat_literal(&p, &ret->value)) {
        ret->tag = TYPE_VALUE;
    } else if (hop_start(&p, "-")) {
        hop_whitespace(&p);
        if (parse_number(&p, &ret->value.number)) {
            ret->tag = TYPE_VALUE;
            ret->value.tag = VALUE_NUMBER;
            ret->value.number = -ret->value.number;
        } else {
            return parse_fail(&p);
        }
    } else if (parse_function_args(&p, true, true, true, &parsed.func)) {
        *ret = function_impl_into_type(parsed.func);
    } else if (parse_inferred_type(&p, &parsed.inferred)) {
        *ret = type_inferred(c_new(InferredType, parsed.inferred));
    } else if (parse_unique_symbol_type(&p)) {
        *ret = type_symbol;
    } else if (parse_identifier_type(&p, &parsed.identifier)) {
        *ret = type_identifier(c_new(IdentifierType, parsed.identifier));
    } else if (parse_tuple(&p, &parsed.tuple_elements)){
        *ret = type_tuple(parsed.tuple_elements, span_valid(readonly_span));
        readonly_span = span_null;
    } else if (hop_char(&p, '(')) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, ret);
        hop_whitespace(&p);
        expect_char(&p, ')');
    } else if (parse_interface(&p, &ret->interface)) {
        ret->tag = TYPE_INTERFACE;
    } else if (*p.pos == '|' || *p.pos == '&') {
        *ret = *p.pos == '|' ? type_never : type_any;
    } else {
        return parse_fail(&p);
    }

    if (parse_type_right_side(&p, min_precedence, ret)) {
        if (ret->tag == TYPE_ARRAY && span_valid(readonly_span)) {
            ret->array.readonly = true;
            readonly_span = span_null;
        }
        while (parse_type_right_side(&p, min_precedence, ret)) {}
    }

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

static inline bool parse_type_or_set_to_default(Parser* parent, TypePrecedence min_precedence, Type* ret) {
    if (parse_type(parent, min_precedence, ret)) {
        return true;
    } else {
        *ret = (Type){0};
        return false;
    }
}

static inline bool expect_type(Parser* parent, TypePrecedence min_precedence, Type* ret) {
    if (parse_type(parent, min_precedence, ret)) {
        return true;
    } else {
        parse_error(parent, span_from_token(parent), 1110, "Type expected.");
        *ret = (Type){
            .tag = TYPE_ANY,
            .any.unkown_symbol_span = span_from_pos(parent),
        };
        return false;
    }
}

static bool parse_interface(Parser* parent, Interface* ret) {
    Parser p = parser(parent);

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
                    ASSERT(member.key.tag == INTERFACE_KEY_NAME);
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

    Type type;
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
        Type last_type = type_any;
        Span last_type_span = span_null;

        while (*p.pos) {
            Type t;
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
        if (!expect_identifier(&p, &enum_case.name)) {
            hop_token(&p);
        }
        hop_whitespace(&p);
        if (hop_char(&p, '=')) {
            hop_whitespace(&p);
            expect_expr(&p, expr_ctx_null, &enum_case.value);
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
        .cases = cases,
    };
    return parse_success(&p);
}

static bool parse_function_args(
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

                ModifierList modifier_list;
                parse_modifier_list(&p, true, &modifier_list);
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
                    goto drop_and_fail;
                }
                hop_whitespace(&p);

                if (hop_char(&p, '?')) {
                    explicit_optional = true;
                    hop_whitespace(&p);
                }

                parse_binding_type_or_set_to_default(&p, &initializer.binding);
                hop_whitespace(&p);

                parse_initialization_or_set_to_default(&p, expr_ctx_null, &initializer.value);
                hop_whitespace(&p);

                if (vec_function_arg_empty(&ret->args) && initializer.binding.has_explicit_type) {
                    is_definitely_function = true;
                }

                vec_function_arg_push(&ret->args, (FunctionArg){
                    .span = span_from(arg_start),
                    .rest_span = rest_span,
                    .initializer = initializer,
                    .explicit_optional = explicit_optional,
                    .rest = rest,
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
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &ret->ret)) {
            ret->ret_span = span_from(ret_a);
            ret->ret_explicit = true;
            hop_whitespace(&p);
            if (p.whitespace != WHITESPACE_NEWLINE && ret->ret.tag == TYPE_IDENTIFIER) {
                if (hop_start_if_ends_on_word_break(&p, "is")) {
                    hop_whitespace(&p);
                    Type t;
                    expect_type(&p, TYPE_PRECEDENCE_NULL, &t);
                    type_drop(&t);
                    type_drop(&ret->ret);
                    ret->ret = type_boolean;
                } else if (span_equals(ret->ret.identifier->name, "asserts")) {
                    Span param;
                    if (parse_identifier(&p, &param)) {
                        hop_whitespace(&p);
                        if (hop_start_if_ends_on_word_break(&p, "is")) {
                            hop_whitespace(&p);
                            Type t;
                            expect_type(&p, TYPE_PRECEDENCE_NULL, &t);
                            type_drop(&t);
                        }
                        type_drop(&ret->ret);
                        ret->ret = type_void;
                    }
                }
            }
        } else {
            goto drop_and_fail;
        }
    }

    return parse_success(&p);

drop_and_fail:
    function_impl_drop(ret);
    return parse_fail(&p);
}

static bool parse_arrow_function(Parser* parent, ExprCtx ctx, FunctionImpl* ret) {
    Parser p = parser(parent);
    FAIL_CACHE_HOOK(&p, FAIL_CACHE_ARROW_FUNCTION);
    Anchor a = anchor(&p);
    bool is_definitely_function = false;
    Span lone_arg_name;

    if (parse_function_args(&p, false, false, !ctx.is_parsing_ternary, ret)) {

        is_definitely_function =
            !vec_function_arg_empty(&ret->args) &&
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
    }

    ret->has_body = true;
    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_function_header(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!parse_function_args(&p, false, false, true, ret)) {
        return parse_fail(&p);
    }

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_non_arrow_function(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!parse_function_args(&p, false, false, true, ret)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (parse_block(&p, false, &ret->body)) {
        ret->has_body = true;
    } else if (*p.pos == ';' || p.whitespace == WHITESPACE_NEWLINE) {
        ret->body = (Block){0};
    } else {
        parse_error(&p, span_from_token(&p), 1144, "'{' or ';' expected.");
    }

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_function_keyword_function(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "function")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span name;
    if (!parse_identifier(&p, &name)) {
        name = span_null;
    }
    hop_whitespace(&p);

    if (!parse_non_arrow_function(&p, ret)) {
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

static bool parse_object_item(Parser* parent, ObjectExprItem* ret) {
    Parser p = parser(parent);
    Span name_span;
    cstr name_string;
    bool name_is_value;

    if (!parse_name_or_value(&p, &name_span, &name_string, &name_is_value)) {
        if (hop_start(&p, "...")) {
            Expr expr;
            expect_expr(&p, expr_ctx_null, &expr);
            *ret = (ObjectExprItem){
                .is_spread = true,
                .value = expr,
            };
            return parse_success(&p);
        } else {
            return parse_fail(&p);
        }
    }
    hop_whitespace(&p);

    if (!hop_char(&p, ':')) {
        cstr_drop(&name_string);
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Expr value;
    expect_expr(&p, expr_ctx_null, &value);

    *ret = (ObjectExprItem){
        .name_string = name_string,
        .name_span = name_span,
        .value = value,
    };
    return parse_success(&p);
}

static bool parse_object(Parser* parent, ObjectExpr* ret) {
    Parser p = parser(parent);
    *ret = vec_object_expr_item_init();

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

    expect_char(&p, '}');

    return parse_success(&p);
}

static bool parse_array(Parser* parent, VecExpr* ret) {
    Parser p = parser(parent);
    *ret = vec_expr_init();

    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    while (*p.pos && *p.pos != ']') {
        Expr expr;
        if (parse_expr(&p, expr_ctx_null, &expr)) {
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

    expect_char(&p, ']');

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

static bool parse_index_or_expr_interface_member(Parser* parent, bool is_type, bool is_index, InterfaceMember* ret) {
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

    bool (*parse_function_fn)(Parser* parent, FunctionImpl* ret) =
        is_type ? parse_function_header : parse_non_arrow_function;

    FunctionImpl func;
    if (parse_function_fn(&p, &func)) {
        ret->type = function_impl_into_type(func);
        ret->is_method = true;
    } else if (hop_char(&p, ':')) {
        hop_whitespace(&p);
        (show_errors ? expect_type : parse_type_or_set_to_default)(&p, TYPE_PRECEDENCE_NULL, &ret->type);
    }

    return parse_success(&p);
}

static bool parse_class_item(Parser* parent, bool is_type, bool is_abstract, ClassItem* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    ModifierList modifiers;
    if (!parse_modifier_list(&p, true, &modifiers)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    bool is_getter = false, is_setter = false;
    ignore is_setter;
    ignore is_getter;

    Span name_span;
    cstr name_string;
    bool name_is_value;
    if (parse_name_or_value(&p, &name_span, &name_string, &name_is_value)) {
        if (!name_is_value && (span_equals(name_span, "get") || span_equals(name_span, "set"))) {
            hop_whitespace(&p);

            Span real_name_span;
            cstr real_name_string;
            bool real_name_is_value;
            if (parse_name_or_value(&p, &real_name_span, &real_name_string, &real_name_is_value)) {
                if (span_equals(name_span, "get")) {
                    is_getter = true;
                } else {
                    is_setter = true;
                }
                cstr_drop(&name_string);
                name_span = real_name_span;
                name_string = real_name_string;
                name_is_value = real_name_is_value;
            }
        }
    } else {
        name_span = span_null;
        name_string = cstr_null;
        name_is_value = false;
    }
    hop_whitespace(&p);

    bool (*parse_function_fn)(Parser* parent, FunctionImpl* ret) =
        is_type ? parse_function_header : parse_non_arrow_function;

    if (
        !is_type &&
        !name_is_value &&
        span_equals(name_span, "static") &&
        parse_block(&p, false, &ret->static_block)
    ) {
        modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_MODIFIERS_NOT_ALLOWED);
        ret->tag = CLASS_ITEM_STATIC_BLOCK;
    } else if (span_valid(name_span)) {
        if (span_valid(name_span) && cstr_empty(&name_string)) {
            name_string = cstr_from_span(name_span);
        }

        bool optional = false;
        if (hop_char(&p, '?')) {
            hop_whitespace(&p);
            optional = true;
        }

        FunctionImpl func;
        ModifierValidationFlags flags =
            (is_type ? MODIFIER_VALIDATION_FLAG_TYPE_MEMEBER : 0) |
            (is_abstract ? MODIFIER_VALIDATION_FLAG_ABSTRACT : 0);
        if (!is_type && !name_is_value && span_equals(name_span, "constructor")) {
            modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_METHOD | flags);
            name_span = span_from(a);
            if (!parse_function_fn(&p, &func)) {
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
        } else if (parse_function_fn(&p, &func)) {
            modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_METHOD | flags);
            *ret = (ClassItem){
                .tag = CLASS_ITEM_METHOD,
                .method = {
                    .is_static = modifiers.includes[MODIFIER_STATIC],
                    .name_string = cstr_move(&name_string),
                    .name_span = name_span,
                    .is_optional = optional,
                    .name_is_value = name_is_value,
                    .func = func,
                }
            };
        } else {
            bool is_definitely_field = false;

            modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_FIELD | flags);
            *ret = (ClassItem){
                .tag = CLASS_ITEM_FIELD,
                .field = {
                    .is_readonly = modifiers.includes[MODIFIER_READONLY],
                    .is_static = modifiers.includes[MODIFIER_STATIC],
                    .is_optional = optional,
                    .name_string = cstr_move(&name_string),
                    .name_span = name_span,
                    .name_is_value = name_is_value,
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
            parse_index_or_expr_interface_member(&p, is_type, true, &ret->interface_memeber) ||
            parse_index_or_expr_interface_member(&p, is_type, false, &ret->interface_memeber)
        ) {
            modifier_list_validate_during_parse(&p, &modifiers, name_span, MODIFIER_VALIDATION_FLAG_TYPE_MEMEBER | MODIFIER_VALIDATION_FLAG_FIELD);
            ret->tag = CLASS_ITEM_INTERFACE_MEMBER;
            ret->interface_memeber.declarations = vec_span_single(span_from(a));
            ret->interface_memeber.readonly = modifiers.includes[MODIFIER_READONLY];
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

    bool is_abstract = false;
    if (hop_start_if_ends_on_word_break(&p, "abstract")) {
        hop_whitespace(&p);
        is_abstract = true;
    }

    if (!hop_start_if_ends_on_word_break(&p, "class")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    *ret = (ClassDecl){
        .is_abstract = is_abstract,
        .extends = expr_implicit_any_no_span,
    };
    if (!parse_identifier(&p, &ret->name)) {
        ret->name = span_null;
    }
    hop_whitespace(&p);

    if (parse_generic_decl(&p, &ret->generic)) {
        hop_whitespace(&p);
    } else {
        ret->generic = (Generic){0};
    }

    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        hop_whitespace(&p);

        ExprCtx ctx = expr_ctx_null;
        ctx.reject_operators = true;
        expect_expr(&p, ctx, &ret->extends);
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
            Type t;
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
        if (expect_expr(&p, expr_ctx_null, &arg)) {
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
    hop_whitespace(&p);

    Span prop;
    if (!parse_identifier_or_keyword(&p, &prop)) {
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

    if (!expect_char(&p, ']')) {
        return parse_success(&p);
    }

    return parse_success(&p);
}

static bool parse_as_type(Parser* parent, Type* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "as")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    expect_type(&p, TYPE_PRECEDENCE_NULL, ret);

    return parse_success(&p);
}

static bool parse_binary_op(Parser* parent, BinaryOp* op) {
    Parser p = parser(parent);

    // PERF: transform this into a big switch or hashmap or something
    if (hop_start(&p, ">>>=")) *op = BINARY_OP_GREATER_GREATER_GREATER_EQUAL;
    else if (hop_start(&p, "===")) *op = BINARY_OP_TRIPLE_EQUAL;
    else if (hop_start(&p, "!==")) *op = BINARY_OP_TRIPLE_NOT_EQUAL;
    else if (hop_start(&p, ">>>")) *op = BINARY_OP_GREATER_GREATER_GREATER;
    else if (hop_start(&p, "<<=")) *op = BINARY_OP_LESS_LESS_EQUAL;
    else if (hop_start(&p, ">>=")) *op = BINARY_OP_GREATER_GREATER_EQUAL;
    else if (hop_start(&p, "&&=")) *op = BINARY_OP_AND_AND_EQUAL;
    else if (hop_start(&p, "||=")) *op = BINARY_OP_OR_OR_EQUAL;
    else if (hop_start(&p, "\?\?=")) *op = BINARY_OP_EH_EH_EQUAL;
    else if (hop_start(&p, "++") || hop_start(&p, "--")) return parse_fail(&p);
    else if (hop_start(&p, "**")) *op = BINARY_OP_STAR_STAR;
    else if (hop_start(&p, "<<")) *op = BINARY_OP_LESS_LESS;
    else if (hop_start(&p, ">>")) *op = BINARY_OP_GREATER_GREATER;
    else if (hop_start(&p, "<=")) *op = BINARY_OP_LESS_EQUAL;
    else if (hop_start(&p, ">=")) *op = BINARY_OP_GREATER_EQUAL;
    else if (hop_start_if_ends_on_word_break(&p, "in")) *op = BINARY_OP_IN;
    else if (hop_start_if_ends_on_word_break(&p, "instanceof")) *op = BINARY_OP_INSTANCEOF;
    else if (hop_start(&p, "==")) *op = BINARY_OP_DOUBLE_EQUAL;
    else if (hop_start(&p, "!=")) *op = BINARY_OP_NOT_EQUAL;
    else if (hop_start(&p, "&&")) *op = BINARY_OP_AND_AND;
    else if (hop_start(&p, "||")) *op = BINARY_OP_OR_OR;
    else if (hop_start(&p, "\?\?")) *op = BINARY_OP_EH_EH;
    else if (hop_start(&p, "+=")) *op = BINARY_OP_PLUS_EQUAL;
    else if (hop_start(&p, "-=")) *op = BINARY_OP_MINUS_EQUAL;
    else if (hop_start(&p, "**=")) *op = BINARY_OP_STAR_STAR_EQUAL;
    else if (hop_start(&p, "*=")) *op = BINARY_OP_STAR_EQUAL;
    else if (hop_start(&p, "/=")) *op = BINARY_OP_SLASH_EQUAL;
    else if (hop_start(&p, "%=")) *op = BINARY_OP_MOD_EQUAL;
    else if (hop_start(&p, "&=")) *op = BINARY_OP_AND_EQUAL;
    else if (hop_start(&p, "^=")) *op = BINARY_OP_XOR_EQUAL;
    else if (hop_start(&p, "|=")) *op = BINARY_OP_OR_EQUAL;
    else if (hop_start(&p, "&")) *op = BINARY_OP_AND;
    else if (hop_start(&p, "^")) *op = BINARY_OP_XOR;
    else if (hop_start(&p, "|")) *op = BINARY_OP_OR;
    else if (hop_start(&p, "<")) *op = BINARY_OP_LESS;
    else if (hop_start(&p, ">")) *op = BINARY_OP_GREATER;
    else if (hop_start(&p, "*")) *op = BINARY_OP_STAR;
    else if (hop_start(&p, "/")) *op = BINARY_OP_SLASH;
    else if (hop_start(&p, "%")) *op = BINARY_OP_MOD;
    else if (hop_start(&p, "=>")) return parse_fail(&p);
    else if (hop_start(&p, "=")) *op = BINARY_OP_EQUAL;
    else if (hop_start(&p, "+")) *op = BINARY_OP_PLUS;
    else if (hop_start(&p, "-")) *op = BINARY_OP_MINUS;
    // else if (hop_start(&p, ",")) *op = BINARY_OP_COMMA; handled in parse_comma_ops_right_side()
    else return parse_fail(&p);

    return parse_success(&p);
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
    ctx.min_precedence = precedence;
    expect_expr(&p, ctx, &right);

    *left_and_ret = expr_binary_op(span_between(left_and_ret->outer_span, right.outer_span), c_new(BinaryOpExpr, {
        .left = *left_and_ret,
        .op = op,
        .right = right,
    }));
    return parse_success(&p);
}

static bool parse_left_unary_op(Parser* parent, UnaryOp* op) {
    if (hop_start(parent, "!")) {
        *op = UNARY_OP_LEFT_BANG;
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
    hop_whitespace(&p);

    Expr expr;
    ctx.min_precedence = unary_op_get_precedence(op);
    expect_expr(&p, ctx, &expr);

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
    const char* end = string_to_double(parent->pos, nullptr, true, true, ret);
    if (end) {
        parent->pos = end;
        if (*parent->pos == 'e' || *parent->pos == 'E') {
            parent->pos++;
            parse_error(parent, span_from_pos(parent), 1124, "Digit expected.");
        }
        return true;
    } else {
        return false;
    }
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
            switch (*p.pos) {
                case 'n': cstr_append_lit(&str, "\n"); break;
                case 'r': cstr_append_lit(&str, "\r"); break;
                default: break; // TODO
            }
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
    } else if (parse_number(&p, &ret->number)) {
        ret->tag = VALUE_NUMBER;
    } else if (parse_string(&p, &ret->string)) {
        ret->tag = VALUE_STRING;
    } else {
        return parse_fail(&p);
    }

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
        Type type;
        UnaryOp unary;
        struct {
            Expr expr;
            BinaryOp op;
        } binary;
        FunctionImpl function;
        VecSpannedType generic_params;
    } parsed;

    if (new_keyword_precedence > ctx.min_precedence && parse_call_args(&p, &parsed.args)) {
        *left_and_ret = expr_call(
            span_from(a),
            c_new(CallExpr, {
                .uses_new = false,
                .callable = *left_and_ret,
                .args = parsed.args,
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
        return parse_success(&p);
    }

    if (parse_bracket_prop(&p, &parsed.expr_a)) {
        *left_and_ret = expr_property(
            span_from(a),
            c_new(PropertyExpr, {
                .expr = *left_and_ret,
                .prop = parsed.expr_a,
                .form = PROPERTY_EXPR_BRACKET,
            })
        );
        return parse_success(&p);
    }

    if (as_type_precedence > ctx.min_precedence && parse_as_type(&p, &parsed.type)) {
        *left_and_ret = expr_as_type(
            span_from(a),
            c_new(AsTypeExpr, {
                .expr = *left_and_ret,
                .type = parsed.type,
                .angle_bracket_syntax = false,
            })
        );
        return parse_success(&p);
    }

    if (left_and_ret->tag == EXPR_IDENTIFIER && *p.pos == '=' && *(p.pos + 1) == '>') {
        Parser sub_p = parser(&p);
        sub_p.pos = span_start_ptr(left_and_ret->outer_span);

        if (parse_arrow_function(&sub_p, ctx, &parsed.function)) {
            parse_success(&sub_p);
            expr_drop(left_and_ret);
            *left_and_ret = expr_function(span_from(a), c_new(FunctionImpl, parsed.function));
            return parse_success(&p);
        } else {
            parse_fail(&sub_p);
        }
    }

    Parser generic_p = parser(&p);
    if (parse_generic_params(&generic_p, true, &parsed.generic_params)) {
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

    if (ternary_precedence >= ctx.min_precedence && parse_ternary_expr(&p, ctx, &parsed.expr_a, &parsed.expr_b)) {
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

static bool parse_identifier_expr(Parser* parent, IdentifierExpr* ret) {
    Parser p = parser(parent);

    Span name;
    if (!parse_identifier(&p, &name)) {
        return parse_fail(&p);
    }
    *ret = (IdentifierExpr){ .name = name };

    return parse_success(&p);
}

static bool parse_expr(Parser* parent, ExprCtx ctx, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    FAIL_CACHE_HOOK(&p, FAIL_CACHE_EXPR);
    ret->currently_getting_type = nullptr;

    union {
        FunctionImpl function;
        ClassDecl class_decl;
    } parsed;
    bool inner_span_set = false;

    if (parse_literal(&p, &ret->literal)) {
        ret->tag = EXPR_LITERAL;
    } else if (parse_object(&p, &ret->object)) {
        ret->tag = EXPR_OBJECT;
    } else if (parse_array(&p, &ret->array)) {
        ret->tag = EXPR_ARRAY;
    } else if (parse_class_decl(&p, &parsed.class_decl)) {
        *ret = expr_class_decl(span_from(a), c_new(ClassDecl, parsed.class_decl));
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
    } else if (parse_function_keyword_function(&p, &parsed.function)) {
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
    } else if (parse_identifier_expr(&p, &ret->identifier)) {
        *ret = expr_identifier(ret->identifier);
        inner_span_set = true;
    } else if (parse_arrow_function(&p, ctx, &parsed.function)) {
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
    } else if (hop_char(&p, '<')) {
        hop_whitespace(&p);

        Type t;
        expect_type(&p, TYPE_PRECEDENCE_NULL, &t);
        hop_whitespace(&p);

        expect_char(&p, '>');
        hop_whitespace(&p);

        Expr e;
        expect_expr(&p, ctx, &e);
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
    if (!ret->currently_getting_type) {
        ret->currently_getting_type = c_new(bool, false);
    }

    ret->lazy_type = (LazyType){0};

    while (parse_expr_right_side(&p, ctx, ret)) {}

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
            .is_export = ret->main.is_export,
            .source_tag = IMPORT_SOURCE_SPECIFIC,
        };
        Anchor a = anchor(&p);

        if (hop_start_if_ends_on_word_break(&p, "type")) {
            hop_whitespace(&p);
            item.type_only = true;
        }

        if (expect_identifier(&p, &item.source_name)) {
            hop_whitespace(&p);

            if (hop_start_if_ends_on_word_break(&p, "as")) {
                hop_whitespace(&p);
                expect_identifier(&p, &item.decl_name);
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

static bool parse_port_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    PortStatement import = {0};

    if (hop_start_if_ends_on_word_break(&p, "import")) {
    } else if (hop_start_if_ends_on_word_break(&p, "export")) {
        import.main.is_export = true;
    } else {
        return parse_fail(&p);
    }
    import.import_keyword_span = span_from(a);
    hop_whitespace(&p);

    a = anchor(&p);
    if (hop_start_if_ends_on_word_break(&p, "type")) {
        import.main.type_only = true;
        hop_whitespace(&p);
    }

    if (hop_char(&p, '*')) {
        hop_whitespace(&p);
        import.main.source_tag = IMPORT_SOURCE_WILDCARD;

        if (import.main.is_export) {
            if (hop_start_if_ends_on_word_break(&p, "as")) {
                hop_whitespace(&p);
                expect_identifier(&p, &import.main.decl_name);
            } else {
                import.main.is_export_wildcard = true;
            }
        } else {
            if (!hop_start_if_ends_on_word_break(&p, "as")) {
                expected_token_error(&p, "as");
            }
            hop_whitespace(&p);

            expect_identifier(&p, &import.main.decl_name);
        }
    } else if (!import.main.is_export && parse_identifier(&p, &import.main.decl_name)) {
        hop_whitespace(&p);
        import.main.source_tag = IMPORT_SOURCE_DEFAULT;

        if (hop_char(&p, ',')) {
            hop_whitespace(&p);

            if (!parse_import_item_list(&p, &import)) {
                import.items = vec_port_item_init();
                expected_token_error(&p, "{");
            }
        }
    } else if (parse_import_item_list(&p, &import)) {
        import.main.source_tag = IMPORT_SOURCE_NONE; // Only the item list is imported, no main import
    } else {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    bool parse_path = true;
    if (!hop_start_if_ends_on_word_break(&p, "from")) {
        if (import.main.is_export) {
            parse_path = false;
        } else {
            expected_token_error(&p, "from");
        }
    }
    hop_whitespace(&p);

    if (parse_path) {
        Expr path_expr;
        if (expect_expr(&p, expr_ctx_null, &path_expr)) {
            if (path_expr.tag == EXPR_LITERAL && path_expr.literal.tag == VALUE_STRING) {
                import.source_path = cstr_move(&path_expr.literal.string);
                import.source_path_span = path_expr.inner_span;
            } else {
                parse_error(&p, path_expr.inner_span, 1141, "String literal expected.");
            }
            expr_drop(&path_expr);
        }
    } else {
        import.main.source_tag = IMPORT_SOURCE_LOCAL;
    }

    import.main.full_span = span_from(a);

    *ret = (Statement){
        .tag = STATEMENT_IMPORT,
        .import = c_new(PortStatement, import),
    };
    return parse_success(&p);
}

// Only parses content, sets type and value to defaults
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
            if (parse_binding_content(&p, &binding)) {
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
            Span name;
            if (parse_identifier(&p, &name)) {
                hop_whitespace(&p);

                if (hop_char(&p, ':')) {
                    hop_whitespace(&p);

                    binding = (Binding){0};
                    if (!parse_binding_content(&p, &binding)) {
                        // TODO
                        break;
                    }

                    vec_object_binding_item_push(&ret->object, (ObjectBindingItem){
                        .source = value_string(cstr_from_span(name)),
                        .source_span = name,
                        .target = binding,
                    });
                } else {
                    vec_object_binding_item_push(&ret->object, (ObjectBindingItem){
                        .source = value_string(cstr_from_span(name)),
                        .source_span = name,
                        .target = (Binding){
                            .tag = BINDING_IDENTIFIER,
                            .content_span = name,
                        },
                    });
                }
            } else {
                break;
            }
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

    Type type;
    expect_type(parent, TYPE_PRECEDENCE_NULL, &type);
    ret->has_explicit_type = true;
    ret->explicit_type = type;
    ret->assigned_type = (Type){0};
    ret->explicit_type_span = span_from(a);

    return true;
}

static bool parse_binding_type_or_set_to_default(Parser* parent, Binding* ret) {
    if (parse_binding_type(parent, ret)) {
        return true;
    } else {
        ret->explicit_type_span = span_null;
        ret->has_explicit_type = false;
        ret->explicit_type = (Type){0};
        ret->assigned_type = (Type){0};
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

static bool parse_variable_statement(Parser* parent, ExprCtx ctx, VariableStatement* ret) {
    Parser p = parser(parent);

    if (hop_start_if_ends_on_word_break(&p, "var")) {
        ret->keyword = VAR_INIT_VAR;
    } else if (hop_start_if_ends_on_word_break(&p, "let")) {
        ret->keyword = VAR_INIT_LET;
    } else if (hop_start_if_ends_on_word_break(&p, "const")) {
        ret->keyword = VAR_INIT_CONST;
    } else {
        return parse_fail(&p);
    }
    Anchor end_of_keyword = anchor(&p);
    hop_whitespace(&p);

    Span comma_span = span_null;
    InitializedBinding initializer;
    ret->initializers = vec_initialized_binding_init();

    while (true) {
        if (!parse_binding_content(&p, &initializer.binding)) {
            if (span_valid(comma_span)) {
                parse_error(&p, comma_span, 1009, "Trailing comma not allowed.");
                p.pos = span_end_ptr(comma_span);
            } else if (vec_initialized_binding_empty(&ret->initializers)) {
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

        if (parse_binding_type_or_set_to_default(&p, &initializer.binding)) {
            end_of_initialization = p.pos;
            hop_whitespace(&p);
        }

        if (parse_initialization_or_set_to_default(&p, ctx, &initializer.value)) {
            end_of_initialization = p.pos;
            hop_whitespace(&p);
        }

        vec_initialized_binding_push(&ret->initializers, initializer);

        if (*p.pos == ';' ||
            *p.pos == ')' ||
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

    expect_expr(&p, expr_ctx_null, condition);
    hop_whitespace(&p);

    if (!hop_char(&p, ')')) {
        Error* error = expected_token_error(&p, ")");
        error_related_error(error, open_token, 1007, "The parser expected to find a ')' to match the '(' token here.");
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
            if (vec_expr_empty(&conditions)) {
                return parse_fail(&p);
            } else if (expect_statement(&p, false, &statement)) {
                vec_statement_push(&statements, statement);
                break;
            }
        }

        vec_expr_push(&conditions, condition);
        vec_statement_push(&statements, statement);
        hop_whitespace(&p);

        if (!hop_start_if_ends_on_word_break(&p, "else")) {
            break;
        }
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

    expect_char(&p, '(');
    hop_whitespace(&p);

    bool is_for_each = false;
    Statement statement = {0};
    Expr left_expr;
    Expr right_expr;
    Statement body;
    bool keyword_is_in = false;

    if (parse_variable_statement(&p, (ExprCtx){.reject_in_operator = true}, &statement.decl.variable)) {
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

            default: PANIC("Invalid statement type for for-each loop");
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

    expect_char(&p, '(');
    hop_whitespace(&p);

    Expr condition = expr_literal(span_from_pos(&p), value_boolean(false));
    Statement body = statement_noop;
    if (expect_expr(&p, expr_ctx_null, &condition)) {
        parse_comma_ops_right_side(&p, &condition);
    }
    hop_whitespace(&p);

    expect_char(&p, ')');
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

        expect_expr(&p, expr_ctx_null, &ret->pattern);
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

    expect_expr(&p, expr_ctx_null, &switch_.needle);
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

    if (!parse_expr(&p, expr_ctx_null, ret)) {
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

    if (!parse_expr(&p, expr_ctx_null, ret)) {
        *ret = expr_undefined(span_from(a));
    }

    return parse_success(&p);
}

static bool parse_namespace(Parser* parent, NamespaceDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "namespace")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }

    *ret = (NamespaceDecl){0};

    if (!parse_identifier(&p, &ret->name)) {
        Value val;
        Anchor a = anchor(&p);
        if (*p.pos == '{') {
            parse_error(&p, span_from_token(&p), 1437, "Scope must be given a name.");
        } else if (parse_literal(&p, &val)) {
            if (val.tag == VALUE_STRING) {
                parse_error(&p, span_from(a), 1003, "Identifier expected.");
            } else {
                cstr msg = cstr_lit("Scope name cannot be '");
                cstr_append_value_error_msg(&msg, &val);
                cstr_append_lit(&msg, "'.");
                parse_error(&p, span_from(a), 2819, "%s", cstr_str(&msg));
                cstr_drop(&msg);
            }
            value_drop(&val);
            return parse_success(&p);
        } else {
            return parse_fail(&p);
        }
    }
    hop_whitespace(&p);

    if (!parse_block(&p, true, &ret->contents)) {
        ret->contents = (Block){0};
        expected_token_error(&p, "{");
    }

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_global_block(Parser* parent, Block* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "global")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_block(&p, false, ret)) {
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

    ModifierList modifiers;
    if (!parse_modifier_list(&p, false, &modifiers)) {
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
        .declared = modifiers.includes[MODIFIER_DECLARE],
    };

    modifier_list_validate_during_parse(
        &p,
        &modifiers,
        span_from(a),
        toplevel ? MODIFIER_VALIDATION_FLAG_TOPLEVEL_STATEMENT : MODIFIER_VALIDATION_FLAG_MODIFIERS_NOT_ALLOWED
    );

    if (decl_props.exported != DECL_EXPORTED_YES_DEFAULT &&
        parse_variable_statement(&p, expr_ctx_null, &parsed.variable)
    ) {
        *ret = (Statement) {
            .tag = STATEMENT_DECL_VARIABLE,
            .decl = {
                .props = decl_props,
                .variable = parsed.variable,
            },
        };
    } else if (parse_function_keyword_function(&p, &parsed.function)) {
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
        parsed.class.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_CLASS,
            .decl = {
                .props = decl_props,
                .class = c_new(ClassDecl, parsed.class),
            },
        };
    } else if (parse_type_decl(&p, &parsed.type_decl)) {
        parsed.type_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_TYPE,
            .decl = {
                .props = decl_props,
                .type = parsed.type_decl,
            },
        };
    } else if (parse_interface_decl(&p, &parsed.interface_decl)) {
        parsed.interface_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_INTERFACE,
            .decl = {
                .props = decl_props,
                .interface = c_new(InterfaceDecl, parsed.interface_decl),
            },
        };
    } else if (parse_enum_decl(&p, &parsed.enum_decl)) {
        parsed.enum_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_ENUM,
            .decl = {
                .props = decl_props,
                .enum_ = c_new(EnumDecl, parsed.enum_decl),
            },
        };
    } else if (parse_global_block(&p, &parsed.block)) {
        // TODO
        *ret = (Statement){
            .tag = STATEMENT_BLOCK,
            .block = parsed.block,
        };
    } else if (decl_props.exported == DECL_EXPORTED_YES_DEFAULT && parse_expr(&p, expr_ctx_null, &parsed.expr)) {
        *ret = (Statement){
            .tag = STATEMENT_EXPORT_DEFAULT_EXPR,
            .expr = parsed.expr,
        };
    } else if (decl_props.exported != DECL_EXPORTED_YES_DEFAULT && parse_namespace(&p, &ret->decl.namespace)) {
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
        return parse_fail(&p);
    }

    modifier_list_drop(&modifiers);
    return parse_success(&p);
}

static bool parse_statement(Parser* parent, bool toplevel, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (*p.pos == ';') {
        *ret = statement_noop;
    } else if (parse_port_statement(&p, ret)) {
    } else if (parse_decl_statement(&p, toplevel, ret)) {
    } else if (parse_block(&p, false, &ret->block)) {
        ret->tag = STATEMENT_BLOCK;
    } else if (parse_if_else(&p, &ret->if_else)) {
        ret->tag = STATEMENT_IF_ELSE;
    } else if (parse_for_loop_statement(&p, ret)) {
    } else if (parse_while_loop_statement(&p, ret)) {
    } else if (parse_switch_statement(&p, ret)) {
    } else if (parse_try_catch_statement(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "break")) {
        ret->tag = STATEMENT_BREAK;
        ret->keyword_span = span_from(a);
    } else if (hop_start_if_ends_on_word_break(&p, "continue")) {
        ret->tag = STATEMENT_CONTINUE;
        ret->keyword_span = span_from(a);
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

    if (!hop_char(&p, '{')) return parse_fail(&p);
    hop_whitespace(&p);

    *ret = (Block){0};
    if (!parse_append_statement_sequence(&p, toplevel, &ret->statements)) {
        *ret = (Block){0};
    }
    hop_whitespace(&p);

    expect_char(&p, '}');
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

    c_foreach(i, vec_error, shared.errors) {
        error_ctx_append_error(errs, *i.ref);
        *i.ref = (Error){0};
    }

    parser_shared_drop(&shared);
}
