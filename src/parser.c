#include "parser.h"
#include "block.h"
#include "interface.h"
#include "interface_member.h"
#include "src/span.h"
#include "statement.h"
#include "ternary_type.h"
#include "generic.h"
#include "class_decl.h"
#include "callable_type.h"
#include "indexed_type.h"
#include "as_type_expr.h"
#include "extends_conditional.h"
#include "type.h"
#include "error.h"

#define i_key uint8_t
#define i_type vec_u8
#include "wrap_stc/vec.h"
typedef vec_u8 VecU8;

typedef struct ParserShared {
    const char* const file_start;
    const char* fail_cache_start;
    VecU8 fail_cache;
} ParserShared;

void parser_shared_drop(ParserShared* me) {
    vec_u8_drop(&my fail_cache);
}

typedef enum WhitespaceType {
    WHITESPACE_NONE = 0,
    WHITESPACE_PARSED,
    WHITESPACE_NEWLINE,
} WhitespaceType;

typedef struct ExprCtx {
    bool block_in_op;
} ExprCtx;

static const ExprCtx expr_ctx_null = {0};

typedef enum FailCacheFlag {
    FAIL_CACHE_EXPR = 1 << 0,
    FAIL_CACHE_ARROW_FUNCTION = 1 << 1,
    FAIL_CACHE_TYPE = 1 << 2,
} FailCacheFlag;

typedef struct Parser Parser;
typedef struct Parser {
    ParserShared* shared;
    Parser* parent; // the parser this parser was created from (and will merge it's result into on success)
    const char* pos; // current position in the code
    WhitespaceType whitespace; // the type of whitespace detected in the most recent call to hop_whitespace()
    const char* whitespace_end; // the end of the most recent whitespace (whitespace type is retained if same as pos)
    uint8_t fail_cache_flag;
    ErrorCtx errs;
} Parser;

typedef enum TypePrecedence {
    TYPE_PRECEDENCE_NULL = 0,
    TYPE_PRECEDENCE_UNION,
    TYPE_PRECEDENCE_INTERSECTION,
    TYPE_PRECEDENCE_TERNARY,
} TypePrecedence;

static bool hop_token(Parser* parent);
static bool parse_literal(Parser* parent, Value* ret);
static bool parse_binary_op(Parser* parent, BinaryOp* op);
static bool parse_left_unary_op(Parser* parent, UnaryOp* op);
static bool parse_right_unary_op(Parser* parent, UnaryOp* op);
static bool parse_block(Parser* parent, bool toplevel, Block* ret);
static bool parse_interface(Parser* parent, Interface* ret);
static bool parse_name_init_with_name(Parser* parent, ExprCtx ctx, cstr name, Span name_span, NameInitData* ret);
static bool parse_name_init(Parser* parent, ExprCtx ctx, NameInitData* ret);
static bool parse_class_item(Parser* parent, bool is_type, ClassItem* ret);
static bool parse_statement(Parser* parent, bool toplevel, Statement* ret);
static bool parse_type(Parser* parent, TypePrecedence min_precedence, Type* ret);
static bool parse_function_args(Parser* parent, FunctionImpl /*nullable*/* func_ret, CallableType* type_ret);
static bool parse_type_compat_literal(Parser* parent, Value* ret);
static bool parse_expr(Parser* parent, ExprCtx ctx, Expr* ret);
static bool parse_binding_content(Parser* parent, Binding* ret);
static bool parse_binding_type(Parser* parent, Binding* ret);
static bool parse_initialization(Parser* parent, ExprCtx ctx, Expr* ret);

static inline Parser parser(Parser* parent) {
    return (Parser){
        .shared = parent->shared,
        .parent = parent,
        .pos = parent->pos,
        .whitespace = parent->whitespace,
        .whitespace_end = parent->whitespace_end,
        .errs = { .phase = ERROR_PHASE_PARSE },
    };
}

/// Must not be used on the root parser, always returns true, drops parser
static inline bool parse_success(Parser* me) {
    my parent->pos = my pos;
    my parent->whitespace = my whitespace;
    my parent->whitespace_end = my whitespace_end;
    error_ctx_append_consume(&my parent->errs, my errs);
    return true;
}

/// Must not be used on the root parser, always returns false, drops parser
static inline bool parse_fail(Parser* me) {
    error_ctx_drop(&my errs);
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
    // TODO: only activate fail cache once a threashold number of failures has been hit
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
#define FAIL_CACHE_HOOK(parser, flag) if (fail_cache_register_and_check(parser, flag)) return false

static void reset_fail_cache(Parser* me) {
    my shared->fail_cache_start = nullptr;
    // _clear() loops in order to drop. there's no drop work, so the loop is not required. this should be safe.
    my shared->fail_cache._len = 0;
}

typedef struct Anchor {
    Parser* parser;
    const char* start;
} Anchor;

Anchor span_start(Parser* parser) {
    return (Anchor) {
        .parser = parser,
        .start = parser->pos,
    };
}

Anchor span_starting_at(Parser* parser, Span start) {
    return (Anchor) {
        .parser = parser,
        .start = parser->shared->file_start + start.offset,
    };
}

Span span_end(Anchor spanner) {
    const char* file_start = spanner.parser->shared->file_start;
    return span_make(file_start, spanner.start - file_start, spanner.parser->pos - spanner.start);
}

static inline Span span_from_pos(Parser* me) {
    return span_make(my shared->file_start, my pos - my shared->file_start, 0);
}

static inline Span span_from_previous_char(Parser* me) {
    return span_make(my shared->file_start, my pos - my shared->file_start - 1, 1);
}

static Span span_from_token(Parser* me) {
    Parser p = parser(me);
    Anchor a = span_start(&p);
    hop_token(&p);
    parse_fail(&p);
    return span_end(a);
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

static inline bool hop_identifier_or_keyword(Parser* parent) {
    if (is_identifier_start_char(*parent->pos)) {
        parent->pos++;
        while (is_identifier_char(*parent->pos)) parent->pos++;
        return true;
    } else {
        return false;
    }
}

static bool parse_identifier(Parser* parent, cstr* ret, Span* span) {
    Anchor a = span_start(parent);
    if (!hop_identifier_or_keyword(parent)) {
        return false;
    }
    Span s = span_end(a);
    if (span) {
        *span = s;
    }
    if (span_eq_str(&s, "return") ||
        span_eq_str(&s, "throw") ||
        span_eq_str(&s, "case") ||
        span_eq_str(&s, "in")
    ) {
        return false; // TODO: check for all keywords efficiently
    }
    *ret = cstr_from_n(a.start, parent->pos - a.start);
    return true;
}

static bool parse_number_str(Parser* parent, cstr* ret) {
    const char* start = parent->pos;
    if (is_number_start_char(*parent->pos)) {
        parent->pos++;
        while (is_number_char(*parent->pos)) parent->pos++;
        *ret = cstr_from_n(start, parent->pos - start);
        return true;
    } else {
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

static inline bool hop_start_if_ends_on_word_break(Parser* parent, const char* prefix) {
    Parser p = parser(parent);
    if (hop_start(&p, prefix)) {
        if (!is_identifier_char(*p.pos)) {
            return parse_success(&p);
        }
    }
    return parse_fail(&p);
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
    } else if (hop_identifier_or_keyword(parent)) {
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
    Anchor a = span_start(parent);
    hop_token(parent);
    return span_end(a);
}

static bool parse_generic_decl_item(Parser* parent, GenericItem* ret) {
    Parser p = parser(parent);
    cstr name = cstr_null;
    Span name_span = span_from_pos(&p);

    if (!parse_identifier(&p, &name, &name_span)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Type extends = type_any;
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        hop_whitespace(&p);
        if (!parse_type(&p, TYPE_PRECEDENCE_NULL, &extends)) {
            extends = type_any;
        }
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
        .name_span = name_span,
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
            error_ctx_error(&p.errs, span_from_token(&p), 1110, "Type expected.");
        } else {
            break;
        }

        if (hop_char(&p, ',')) {
        } else if (*p.pos == '>') {
        } else {
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "',' expected.");
        }
        hop_whitespace(&p);
    }

    if (!hop_char(&p, '>')) {
        error_ctx_error(&p.errs, hop_token_get_span(&p), 1005, "'>' expected.");
    }

    return parse_success(&p);
}

static bool parse_generic_params(Parser* parent, VecType* ret) {
    Parser p = parser(parent);
    *ret = vec_type_init();

    if (!hop_char(&p, '<')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span trailing_comma_span = span_null;
    while (*p.pos && *p.pos != '>') {
        trailing_comma_span = span_null;
        Type t;
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
            vec_type_push(ret, t);
            hop_whitespace(&p);
        } else if (*p.pos == ',') {
            error_ctx_error(&p.errs, span_from_token(&p), 1110, "Type expected.");
        } else {
            break;
        }

        if (hop_char(&p, ',')) {
            trailing_comma_span = span_from_previous_char(&p);
        } else if (*p.pos == '>') {
        } else {
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "',' expected.");
        }
        hop_whitespace(&p);
    }

    if (span_valid(&trailing_comma_span)) {
        error_ctx_error(&p.errs, trailing_comma_span, 1009, "Trailing comma not allowed.");
    }

    if (!hop_char(&p, '>')) {
        error_ctx_error(&p.errs, hop_token_get_span(&p), 1005, "'>' expected.");
    }

    return parse_success(&p);
}

static bool parse_ternary_type(Parser* parent, Type* left_and_ret) {
    Parser p = parser(parent);
    Type MaybeSuper = {0};
    Type TrueType = {0};
    Type FalseType = {0};

    if (!hop_start_if_ends_on_word_break(&p, "extends")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    parse_type(&p, TYPE_PRECEDENCE_TERNARY, &MaybeSuper);
    hop_whitespace(&p);

    if (!hop_char(&p, '?')) {
        error_ctx_parse_error(&p.errs, "expected '?'");
    }
    hop_whitespace(&p);

    parse_type(&p, TYPE_PRECEDENCE_TERNARY, &TrueType);
    hop_whitespace(&p);

    if (!hop_char(&p, ':')) {
        error_ctx_parse_error(&p.errs, "expected ':'");
    }
    hop_whitespace(&p);

    parse_type(&p, TYPE_PRECEDENCE_TERNARY, &FalseType);

    *left_and_ret = type_ternary(c_new(TernaryType, {
        .conditional = (ExtendsConditional){
            .MaybeSub = *left_and_ret,
            .MaybeSuper = MaybeSuper,
        },
        .TrueType = TrueType,
        .FalseType = FalseType,
    }));
    return parse_success(&p);
}

static bool parse_tuple(Parser* parent, VecType* ret) {
    Parser p = parser(parent);
    Type t;
    *ret = vec_type_init();

    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    while (*p.pos && *p.pos != ']') {
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
            vec_type_push(ret, t);
        } else {
            error_ctx_error(&p.errs, span_from_token(&p), 1110, "Type expected.");
        }
        hop_whitespace(&p);

        if (*p.pos != ']' && !hop_char(&p, ',')) {
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "',' expected.");
            return parse_success(&p);
        }
        hop_whitespace(&p);
    }

    if (!hop_char(&p, ']')) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "']' expected.");
    }

    return parse_success(&p);

}

static bool parse_type_right_side(Parser* parent, TypePrecedence min_precedence, Type* left_and_ret) {
    Parser p = parser(parent);
    Type type;

    hop_whitespace(&p);

    if (hop_char(&p, '[')) {
        hop_whitespace(&p);
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
            hop_whitespace(&p);
            if (!hop_char(&p, ']')) {
                error_ctx_error(&p.errs, span_from_token(&p), 1005, "']' expected.");
            }
            *left_and_ret = type_indexed(c_new(IndexedType, {
                .interface = *left_and_ret,
                .index = type,
            }));
            return parse_success(&p);
        } else {
            if (!hop_char(&p, ']')) {
                error_ctx_error(&p.errs, span_from_token(&p), 1005, "']' expected.");
            }
            *left_and_ret = type_array(c_new(Type, *left_and_ret));
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

            if (!parse_type(&p, TYPE_PRECEDENCE_INTERSECTION, &type)) {
                error_ctx_error(&p.errs, span_from_token(&p), 1110, "Type expected.");
                type = type_any;
            }

            *left_and_ret = type_composite_without_simplify(*left_and_ret, type, TYPE_INTERSECTION);
            return parse_success(&p);
        }
    }

    if (TYPE_PRECEDENCE_UNION >= min_precedence) {
        if (hop_char(&p, '|') && *p.pos != '|' && *p.pos != '=') {
            hop_whitespace(&p);

            if (!parse_type(&p, TYPE_PRECEDENCE_UNION, &type)) {
                error_ctx_error(&p.errs, span_from_token(&p), 1110, "Type expected.");
                type = type_any;
            }

            *left_and_ret = type_composite_without_simplify(*left_and_ret, type, TYPE_UNION);
            return parse_success(&p);
        }
    }

    return parse_fail(&p);
}

static bool parse_type(Parser* parent, TypePrecedence min_precedence, Type* ret) {
    Parser p = parser(parent);

    union {
        struct {
            Span span;
            cstr name;
        };
        CallableType callable;
    } parsed;

    if (hop_start_if_ends_on_word_break(&p, "any")) {
        *ret = type_any;
    } else if (hop_start_if_ends_on_word_break(&p, "unknown")) {
        *ret = type_unknown;
    } else if (hop_start_if_ends_on_word_break(&p, "never")) {
        *ret = type_never;
    } else if (hop_start_if_ends_on_word_break(&p, "void")) {
        *ret = type_void;;
    } else if (hop_start_if_ends_on_word_break(&p, "boolean")) {
        *ret = type_boolean;
    } else if (hop_start_if_ends_on_word_break(&p, "number")) {
        *ret = type_number;
    } else if (hop_start_if_ends_on_word_break(&p, "string")) {
        *ret = type_string;
    } else if (parse_type_compat_literal(&p, &ret->value)) {
        ret->tag = TYPE_VALUE;
    } else if (parse_identifier(&p, &parsed.name, &parsed.span)) {
        *ret = type_identifier(parsed.name, parsed.span);
        hop_whitespace(&p);
        if (p.whitespace != WHITESPACE_NEWLINE) {
            if (!parse_generic_params(&p, &ret->identifier.generic_params)) {
                ret->identifier.generic_params = vec_type_init();
            }
        }
    } else if (parse_tuple(&p, &ret->tuple)){
        ret->tag = TYPE_TUPLE;
    } else if (parse_function_args(&p, nullptr, &parsed.callable)) {
        *ret = type_callable(c_new(CallableType, parsed.callable));
    } else if (hop_char(&p, '(')) {
        hop_whitespace(&p);
        if (!parse_type(&p, TYPE_PRECEDENCE_NULL, ret)) {
            error_ctx_error(&p.errs, span_from_token(&p), 1110, "Type expected.");
            *ret = type_any;
        }
        hop_whitespace(&p);
        if (!hop_char(&p, ')')) {
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "')' expected.");
        }
    } else if (parse_interface(&p, &ret->interface)) {
        ret->tag = TYPE_INTERFACE;
    } else {
        error_ctx_parse_error(&p.errs, "did not find type");
        return parse_fail(&p);
    }

    while (parse_type_right_side(&p, min_precedence, ret)) {}

    return parse_success(&p);
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
        if (parse_class_item(&p, true, &item)) {
            switch (item.tag) {
                case CLASS_ITEM_MEMBER:
                    vec_interface_member_push(&members, interface_member_make(
                        item.span,
                        cstr_move(&item.member.name),
                        item.member.name_span,
                        type_callable(c_new(CallableType, item.member.func.type))
                    ));
                    item.member.func.type = (CallableType){0};
                    break;

                case CLASS_ITEM_FIELD:
                    vec_interface_member_push(&members, interface_member_make(
                        item.span,
                        cstr_move(&item.field.data.name),
                        item.field.data.name_span,
                        type_move(&item.field.data.type)
                    ));
                    break;

                default:
                    error_ctx_error(&p.errs, span_from_pos(&p), TS_CODE_NONE, "Invalid class item for interface.");
                    break;
            }
            class_item_drop(&item);
        } else {
            error_ctx_error(&p.errs, hop_token_get_span(&p), 1131, "Property or signature expected.");
        }
        hop_whitespace(&p);

        // Typescript accepts EITHER , ; or newlines in interfaces. You can MIX them!
        if (!hop_char(&p, ',') &&
            !hop_char(&p, ';') &&
            *p.pos != '}' &&
            p.whitespace != WHITESPACE_NEWLINE
        ) {
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "';' expected.");
        }
        hop_whitespace(&p);
    }

    if (!hop_char(&p, '}')) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "'}' expected.");
    }

    *ret = (Interface) {
        .members = members,
    };
    return parse_success(&p);
}

static bool parse_type_decl(Parser* parent, TypeDecl* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    if (!hop_start_if_ends_on_word_break(&p, "type")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }

    cstr name;
    Span name_span;
    if (!parse_identifier(&p, &name, &name_span)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Generic generic;
    if (!parse_generic_decl(&p, &generic)) {
        generic = (Generic){0};
    }
    hop_whitespace(&p);

    Type type;
    if (hop_start(&p, "=")) {
        hop_whitespace(&p);
        if (!parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
            error_ctx_error(&p.errs, span_from_token(&p), 1109, "Type expected.");
            type = type_any;
        }
    } else {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "'=' expected.");
        type = type_any;
    }

    *ret = (TypeDecl) {
        .full_span = span_end(a),
        .is_interface = false,
        .name = name,
        .name_span = name_span,
        .generic = generic,
        .type = type,
    };
    return parse_success(&p);
}

static bool parse_interface_decl(Parser* parent, TypeDecl* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    if (!hop_start_if_ends_on_word_break(&p, "interface")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    cstr name;
    Span name_span;
    if (!parse_identifier(&p, &name, &name_span)) {
        error_ctx_parse_error(&p.errs, "interface does not have a name");
        name = cstr_null;
        name_span = span_null;
    }
    hop_whitespace(&p);

    Generic generic;
    if (!parse_generic_decl(&p, &generic)) {
        generic = (Generic){0};
    }
    hop_whitespace(&p);

    Interface interface;
    if (!parse_interface(&p, &interface)) {
        cstr_drop(&name);
        generic_drop(&generic);
        return parse_fail(&p);
    }

    *ret = (TypeDecl) {
        .full_span = span_end(a),
        .is_interface = true,
        .name = name,
        .name_span = name_span,
        .generic = generic,
        .type = type_interface(interface),
    };
    return parse_success(&p);
}

// If func_ret is null it is parsed like a type instead of a function implementation
static bool parse_function_args(Parser* parent, FunctionImpl /*nullable*/* func_ret, CallableType* type_ret) {
    Parser p = parser(parent);
    bool is_definitely_function = false;

    *type_ret = (CallableType){0};

    if (parse_generic_decl(&p, &type_ret->generic)) {
        hop_whitespace(&p);
        is_definitely_function = true;
    } else {
        type_ret->generic = (Generic){0};
    }

    if (func_ret) {
        func_ret->arg_defaults = vec_expr_init();
    }

    Anchor args_start = span_start(&p);

    if (hop_char(&p, '(')) {
        hop_whitespace(&p);

        while (*p.pos && *p.pos != ')') {
            if (hop_char(&p, ',')) {
                error_ctx_error(&p.errs, span_from_previous_char(&p), 1138, "Parameter declaration expected.");
            } else {
                Binding binding = {0};
                Expr default_value;
                const char* before_param = p.pos;
                bool optional = false;
                bool explicit_optional = false;
                bool rest = false;
                Span rest_span = span_null;
                Anchor arg_start = span_start(&p);

                if (hop_start(&p, "...")) {
                    hop_whitespace(&p);
                    rest = true;
                    rest_span = span_end(arg_start);
                }

                if (!parse_binding_content(&p, &binding)) {
                    goto drop_and_fail;
                }
                hop_whitespace(&p);

                if (hop_char(&p, '?')) {
                    optional = true;
                    explicit_optional = true;
                    hop_whitespace(&p);
                }

                if (parse_binding_type(&p, &binding)) {
                    if (explicit_optional) {
                        binding.type = type_composite(binding.type, type_undefined, TYPE_UNION);
                    }
                    hop_whitespace(&p);
                }

                if (parse_initialization(&p, expr_ctx_null, &default_value)) {
                    optional = true;
                    hop_whitespace(&p);
                } else {
                    default_value = expr_implicit_any_no_span;
                }

                if (vec_callable_arg_empty(&type_ret->args) && binding.type_explicit) {
                    is_definitely_function = true;
                }

                vec_callable_arg_push(&type_ret->args, (CallableArg){
                    .span = span_end(arg_start),
                    .rest_span = rest_span,
                    .binding = binding,
                    .optional = optional,
                    .explicit_optional = explicit_optional,
                    .rest = rest,
                });

                if (func_ret) {
                    while (vec_expr_size(&func_ret->arg_defaults) < vec_callable_arg_size(&type_ret->args) - 1) {
                        vec_expr_push(&func_ret->arg_defaults, expr_implicit_any(span_from_pos(&p)));
                    }
                    vec_expr_push(&func_ret->arg_defaults, default_value);
                } else {
                    expr_drop(&default_value);
                }

                if (p.pos == before_param) {
                    if (is_definitely_function) {
                        // parse_name_init already added error
                        hop_token(&p);
                    } else {
                        goto drop_and_fail;
                    }
                } else {
                    hop_whitespace(&p);

                    if (*p.pos != ')' && !hop_char(&p, ',')) {
                        error_ctx_error(&p.errs, span_from_token(&p), 1005, "',' expected.");
                    }
                }
            }

            hop_whitespace(&p);
        }

        if (!hop_char(&p, ')')) {
            if (func_ret) {
                vec_expr_drop(&func_ret->arg_defaults);
                func_ret->arg_defaults = vec_expr_init();
            }
            callable_type_drop(type_ret);
            *type_ret = (CallableType){0};
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "')' expected.");
            return parse_success(&p);
        }
    } else if (is_definitely_function) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "'(' expected.");
    } else {
        goto drop_and_fail;
    }

    if (func_ret) {
        func_ret->args_span = span_end(args_start);
    }

    bool parse_ret_type = true;
    if (func_ret) {
        Parser type_p = parser(&p);
        hop_whitespace(&type_p);

        if (hop_start(&type_p, ":")) {
            parse_success(&type_p);
        } else {
            parse_fail(&type_p);
            parse_ret_type = false;
        }
    } else {
        hop_whitespace(&p);

        if (!hop_start(&p, "=>")){
            if (is_definitely_function) {
                error_ctx_error(&p.errs, span_from_token(&p), 1005, "'=>' expected.");
            } else {
                goto drop_and_fail;
            }
        }
    }
    hop_whitespace(&p);

    if (parse_ret_type) {
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &type_ret->ret)) {
            type_ret->ret_explicit = true;
        } else {
            goto drop_and_fail;
        }
    }

    return parse_success(&p);

drop_and_fail:
    callable_type_drop(type_ret);
    if (func_ret) {
        vec_expr_drop(&func_ret->arg_defaults);
    }
    return parse_fail(&p);
}

static bool parse_arrow_function(Parser* parent, ExprCtx ctx, FunctionImpl* ret) {
    Parser p = parser(parent);
    FAIL_CACHE_HOOK(&p, FAIL_CACHE_ARROW_FUNCTION);
    Anchor a = span_start(&p);
    bool is_definitely_function = false;
    cstr lone_arg_name;
    Span lone_arg_span;
    *ret = (FunctionImpl){0};

    if (parse_function_args(&p, ret, &ret->type)) {

        is_definitely_function =
            !vec_callable_arg_empty(&ret->type.args) &&
            vec_callable_arg_at(&ret->type.args, 0)->binding.type_explicit;

    } else if (parse_identifier(&p, &lone_arg_name, &lone_arg_span)) {
        *ret = (FunctionImpl){0};

        ret->args_span = lone_arg_span;
        Binding binding = {
            .tag = BINDING_IDENTIFIER,
            .identifier = cstr_clone(lone_arg_name),
            .content_span = lone_arg_span,
        };
        vec_callable_arg_push(&ret->type.args, (CallableArg){ .binding = binding });

    } else {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    bool newline_after_args = p.whitespace == WHITESPACE_NEWLINE;

    Anchor arrow_a = span_start(&p);
    if (!hop_start(&p, "=>")) {
        function_impl_drop(ret);
        if (is_definitely_function) {
            *ret = (FunctionImpl){0};
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "'=>' expected.");
            ret->full_span = span_end(a);
            return parse_success(&p);
        } else {
            return parse_fail(&p);
        }
    }
    ret->arrow_span = span_end(arrow_a);

    if (newline_after_args) {
        // This will generate an error in the typecheck phase, why it's not a parse error I have no idea.
        ret->newline_before_arrow = true;
    }
    hop_whitespace(&p);

    if (!parse_block(&p, false, &ret->body)) {
        Expr expr;
        if (!parse_expr(&p, ctx, &expr)) {
            error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
            expr = expr_implicit_any(span_from_pos(&p));
            p.pos = span_end_ptr(&ret->arrow_span);
        }
        ret->body = (Block){0};
        vec_statement_push(&ret->body.statements, (Statement){
            .tag = STATEMENT_RETURN_EXPR,
            .expr = expr,
        });
    }

    ret->has_block = true;
    ret->full_span = span_end(a);
    return parse_success(&p);
}

static bool parse_function_header(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);
    *ret = (FunctionImpl){0};

    if (!parse_function_args(&p, ret, &ret->type)) {
        return parse_fail(&p);
    }

    ret->full_span = span_end(a);
    return parse_success(&p);
}

static bool parse_non_arrow_function(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);
    *ret = (FunctionImpl){0};

    if (!parse_function_args(&p, ret, &ret->type)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (parse_block(&p, false, &ret->body)) {
        ret->has_block = true;
    } else if (*p.pos == ';' || p.whitespace == WHITESPACE_NEWLINE) {
        ret->body = (Block){0};
    } else {
        error_ctx_error(&p.errs, span_from_token(&p), 1144, "'{' or ';' expected.");
    }

    ret->full_span = span_end(a);
    return parse_success(&p);
}

static bool parse_function_keyword_function(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    if (!hop_start_if_ends_on_word_break(&p, "function")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    cstr name;
    Span name_span;
    if (!parse_identifier(&p, &name, &name_span)) {
        name = cstr_null;
        name_span = span_null;
    }

    if (!parse_non_arrow_function(&p, ret)) {
        error_ctx_error(&p.errs, span_from_token(&p), TS_CODE_NONE, "Expected function.");
        *ret = (FunctionImpl){0};
    }

    ret->name = name;
    ret->name_span = name_span;
    ret->full_span = span_end(a);
    return parse_success(&p);
}

static bool parse_object_item(Parser* parent, ObjectExprItem* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);
    cstr name;
    Span name_span;
    Value name_value;

    if (parse_identifier(&p, &name, &name_span)) {
    } else if (parse_literal(&p, &name_value)) {
        if (name_value.tag == VALUE_STRING) {
            name = name_value.string;
            name_span = span_end(a);
        } else {
            name = cstr_null;
            cstr_append_value_js(&name, &name_value);
            name_span = span_end(a);
            value_drop(&name_value);
        }
    } else {
            return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!hop_start(&p, ":")) {
        cstr_drop(&name);
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Expr value;
    if (!parse_expr(&p, expr_ctx_null, &value)) {
        error_ctx_parse_error(&p.errs, "bad expression in object");
        value = expr_undefined(span_from_pos(&p));
    }

    *ret = (ObjectExprItem){
        .name = name,
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
            error_ctx_error(&p.errs, span_from_token(&p), 1136, "Property assignment expected.");
        } else {
            break;
        }
        hop_whitespace(&p);

        if (!hop_char(&p, ',') && *p.pos != '}') {
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "',' expected.");
            hop_char(&p, ';'); // Optional
        }
        hop_whitespace(&p);
    }

    if (!hop_char(&p, '}')) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "'}' expected.");
    }

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

        if (!hop_char(&p, ',') && *p.pos != ']') {
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "',' expected.");
        }
        hop_whitespace(&p);
    }

    if (!hop_char(&p, ']')) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "']' expected.");
    }

    return parse_success(&p);
}

// This function is a clusterfuck. I'm sorry.
static bool parse_class_item(Parser* parent, bool is_type, ClassItem* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    enum {
        MODIFIER_ERROR_NONE = 0,
        READONLY_ALREADY_SEEN,
        ACCCESS_ALREADY_SEEN,
        MUST_PRECEDE_READONLY,
        MUST_PRECEDE_STATIC,
        MUST_PRECEDE_ASYNC,
        INVALID_READONLY,
        UNEXPECTED_IDENT,
        MODIFIERS_CANNOT_APPEAR,
        MODIFIER_ON_TYPE,
        EXPORT_INVALID,
    } modifier_error = 0;
    enum {
        NOT_GETTER_SETTER = 0,
        IS_GETTER,
        IS_SETTER,
    } getter_setter = 0;
    Span modifier_error_span = span_null;
    Span access_span = span_null;
    Span readonly_span = span_null;
    Span static_span = span_null;
    Span async_span = span_null;

    cstr name = cstr_null;
    Span name_span = span_null;

    ret->access = ACCESS_MODIFIER_PUBLIC;
    while (true) {
        cstr next_name;
        Span next_name_span;
        if (!parse_identifier(&p, &next_name, &next_name_span)) {
            break;
        }

        if (cstr_equals(&name, "readonly")) {
            if (span_valid(&readonly_span)) {
                if (!modifier_error) {
                    modifier_error = READONLY_ALREADY_SEEN;
                    modifier_error_span = name_span;
                }
            } else {
                readonly_span = name_span;
            }
        } else if (cstr_equals(&name, "static")) {
            if (span_valid(&static_span)) {
                if (!modifier_error) {
                    modifier_error = UNEXPECTED_IDENT;
                    modifier_error_span = name_span;
                }
            } else {
                static_span = name_span;
            }

            if (!modifier_error) {
                if (is_type) {
                    modifier_error = MODIFIER_ON_TYPE;
                    modifier_error_span = name_span;
                } else if (span_valid(&async_span)) {
                    modifier_error = MUST_PRECEDE_ASYNC;
                    modifier_error_span = name_span;
                }
            }
        } else if (cstr_equals(&name, "export")) {
            if (!modifier_error) {
                modifier_error = EXPORT_INVALID;
                modifier_error_span = name_span;
            }
        } else if (cstr_equals(&name, "async")) {
            if (span_valid(&async_span)) {
                if (!modifier_error) {
                    modifier_error = UNEXPECTED_IDENT;
                    modifier_error_span = name_span;
                }
            } else {
                async_span = name_span;
            }

            if (!modifier_error) {
                if (is_type) {
                    modifier_error = MODIFIER_ON_TYPE;
                    modifier_error_span = name_span;
                }
            }
        } else if (cstr_equals(&name, "declare")) {
        } else if (cstr_equals(&name, "get")) {
            getter_setter = IS_GETTER;
        } else if (cstr_equals(&name, "set")) {
            getter_setter = IS_SETTER;
        } else if (cstr_equals(&name, "public") || cstr_equals(&name, "protected") || cstr_equals(&name, "private")) {
            if (cstr_equals(&name, "public")) {
            } else if (cstr_equals(&name, "protected")) {
                if (!span_valid(&access_span)) ret->access = ACCESS_MODIFIER_PROTECTED;
            } else if (cstr_equals(&name, "private")) {
                if (!span_valid(&access_span)) ret->access = ACCESS_MODIFIER_PRIVATE;
            }

            if (!modifier_error) {
                if (is_type) {
                    modifier_error = MODIFIER_ON_TYPE;
                    modifier_error_span = name_span;
                } else if (span_valid(&access_span)) {
                    modifier_error = ACCCESS_ALREADY_SEEN;
                    modifier_error_span = name_span;
                } else if (span_valid(&readonly_span)) {
                    modifier_error = MUST_PRECEDE_READONLY;
                    modifier_error_span = name_span;
                } else if (span_valid(&static_span)) {
                    modifier_error = MUST_PRECEDE_STATIC;
                    modifier_error_span = name_span;
                } else if (span_valid(&async_span)) {
                    modifier_error = MUST_PRECEDE_ASYNC;
                    modifier_error_span = name_span;
                }
            }

            if (!span_valid(&access_span)) {
                access_span = name_span;
            }
        } else if (!cstr_empty(&name) && !modifier_error) {
            if (cstr_equals(&name, "var")) {
                cstr_drop(&name);
                return parse_fail(&p);
            }
            // If previous name is not empty
            modifier_error = UNEXPECTED_IDENT;
            modifier_error_span = name_span;
        }
        hop_whitespace(&p);

        cstr_drop(&name);
        name = next_name;
        name_span = next_name_span;

        if (p.whitespace == WHITESPACE_NEWLINE) {
            break;
        }

        if (getter_setter) {
            break;
        }
    }

    bool (*parse_function_fn)(Parser* parent, FunctionImpl* ret) =
        is_type ? parse_function_header : parse_non_arrow_function;

    if (!is_type && cstr_equals(&name, "constructor")) {
        ret->tag = CLASS_ITEM_CONSTRUCTOR;
        name_span = span_end(a);
        if (!parse_function_fn(&p, &ret->constructor.func)) {
            error_ctx_error(&p.errs, span_from_token(&p), TS_CODE_NONE, "constructor expected");
            ret->constructor.func = (FunctionImpl){0};
        }
        ret->constructor.name_span = name_span;
        ret->constructor.func.full_span = span_end(a);
        if (span_valid(&readonly_span)) {
            modifier_error = INVALID_READONLY;
            modifier_error_span = readonly_span;
        }
    } else if (!is_type && cstr_equals(&name, "static") && parse_block(&p, false, &ret->static_block)) {
        ret->tag = CLASS_ITEM_STATIC_BLOCK;
        if (span_valid(&async_span)) {
            modifier_error = MODIFIERS_CANNOT_APPEAR;
            modifier_error_span = async_span;
        } else if (span_valid(&readonly_span)) {
            modifier_error = MODIFIERS_CANNOT_APPEAR;
            modifier_error_span = readonly_span;
        } else if (span_valid(&access_span)) {
            modifier_error = MODIFIERS_CANNOT_APPEAR;
            modifier_error_span = access_span;
        }
    } else if (!cstr_empty(&name)) {
        if (parse_function_fn(&p, &ret->member.func)) {
            ret->tag = CLASS_ITEM_MEMBER;
            ret->member.name = name;
            ret->member.name_span = name_span;
            if (span_valid(&readonly_span)) {
                modifier_error = INVALID_READONLY;
                modifier_error_span = readonly_span;
            }
        } else if (parse_name_init_with_name(&p, expr_ctx_null, name, name_span, &ret->field.data)) {
            ret->tag = CLASS_ITEM_FIELD;
            ret->field.readonly = span_valid(&readonly_span);
        } else {
            error_ctx_error(&p.errs, span_from_token(&p), TS_CODE_NONE, "class member expected");
            ret->tag = CLASS_ITEM_MEMBER;
            ret->member = (typeof(ret->member)){0};
        }
    } else {
        cstr_drop(&name);
        return parse_fail(&p);
    }

    switch (modifier_error) {
        case MODIFIER_ERROR_NONE: break;

        case READONLY_ALREADY_SEEN:
            // Put error into parent errors because we know we will return success, and this will fix the ordering
            error_ctx_error(&parent->errs, modifier_error_span, 1030, "'readonly' modifier already seen.");
            break;

        case ACCCESS_ALREADY_SEEN:
            error_ctx_error(&parent->errs, modifier_error_span, 1028, "Accessibility modifier already seen.");
            break;

        case MUST_PRECEDE_READONLY:
            error_ctx_error(
                &parent->errs,
                modifier_error_span,
                1029,
                "'%.*s' modifier must precede 'readonly' modifier.",
                c_SV(span_sv(&modifier_error_span))
            );
            break;

        case MUST_PRECEDE_STATIC:
            error_ctx_error(
                &parent->errs,
                modifier_error_span,
                1029,
                "'%.*s' modifier must precede 'static' modifier.",
                c_SV(span_sv(&modifier_error_span))
            );
            break;

        case MUST_PRECEDE_ASYNC:
            error_ctx_error(
                &parent->errs,
                modifier_error_span,
                1029,
                "'%.*s' modifier must precede 'async' modifier.",
                c_SV(span_sv(&modifier_error_span))
            );
            break;

        case INVALID_READONLY:
            error_ctx_error(
                &parent->errs,
                modifier_error_span,
                1024,
                "'readonly' modifier can only appear on a property declaration or index signature."
            );
            break;

        case UNEXPECTED_IDENT:
            error_ctx_error(
                &parent->errs,
                modifier_error_span,
                1434,
                "Unexpected keyword or identifier."
            );
            break;

        case MODIFIERS_CANNOT_APPEAR:
            error_ctx_error(
                &parent->errs,
                modifier_error_span,
                1184,
                "Modifiers cannot appear here."
            );
            break;

        case MODIFIER_ON_TYPE:
            error_ctx_error(
                &parent->errs,
                modifier_error_span,
                1070,
                "'%.*s' modifier cannot appear on a type member.",
                c_SV(span_sv(&modifier_error_span))
            );
            break;

        case EXPORT_INVALID:
            error_ctx_error(
                &parent->errs,
                modifier_error_span,
                1031,
                "'export' modifier cannot appear on class elements of this kind."
            );
            break;
    }

    ret->span = span_end(a);
    return parse_success(&p);
}

static bool parse_class_decl(Parser* parent, ClassDecl* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

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
        .interface = type_any,
        .extends = expr_implicit_any(span_null),
        .implements = {0},
    };
    if (!parse_identifier(&p, &ret->name, &ret->name_span)) {
        ret->name = cstr_null;
        ret->name_span = span_null;
    }
    hop_whitespace(&p);

    if (parse_generic_decl(&p, &ret->generic)) {
        hop_whitespace(&p);
    } else {
        ret->generic = (Generic){0};
    }

    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        hop_whitespace(&p);

        if (!parse_expr(&p, expr_ctx_null, &ret->extends)) {
            ret->extends = expr_implicit_any(span_null);
            error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
        }
        hop_whitespace(&p);

        if (hop_char(&p, ',')) {
            // TODO consume and give correct error when multiple classes are extended
            error_ctx_error(&p.errs, span_from_previous_char(&p), 1009, "Trailing comma not allowed.");
            hop_whitespace(&p);
        }
    }

    if (hop_start_if_ends_on_word_break(&p, "implements")) {
        hop_whitespace(&p);

        while (true) {
            Type t;
            if (parse_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
                vec_type_push(&ret->implements, t);

                hop_whitespace(&p);
                if (!hop_char(&p, ',')) {
                    break;
                }
                hop_whitespace(&p);
            } else {
                error_ctx_error(&p.errs, span_from_token(&p), 1109, "Type expected.");
                break;
            }
        }
    }

    if (!hop_char(&p, '{')) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "'{' expected.");
        ret->full_span = span_end(a);
        return parse_success(&p);
    }
    hop_whitespace(&p);

    ClassItem item;
    while (*p.pos && *p.pos != '}') {
        if (!parse_class_item(&p, false, &item)) {
            error_ctx_error(
                &p.errs,
                span_from_token(&p),
                1068,
                "Unexpected token. A constructor, method, accessor, or property was expected."
            );
            ret->full_span = span_end(a);
            return parse_success(&p);
        }
        vec_class_item_push(&ret->items, item);
        do {
            hop_whitespace(&p);
        } while (hop_char(&p, ';'));
    }

    if (!hop_char(&p, '}')) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "'}' expected.");
    }

    ret->full_span = span_end(a);
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
        if (parse_expr(&p, expr_ctx_null, &arg)) {
            vec_expr_push(args, arg);
        } else {
            error_ctx_parse_error(&p.errs, "invald expression in call");
            while (*p.pos && *p.pos != ')' && *p.pos != ',') {
                p.pos++;
            }
        }
        hop_whitespace(&p);
        if (*p.pos != ',' && *p.pos != ')') {
            error_ctx_parse_error(&p.errs, "expected ',' or ')' in call");
        }
        bool first_comma = true;
        while (*p.pos == ',') {
            p.pos++;
            hop_whitespace(&p);
            if (!first_comma) {
                error_ctx_parse_error(&p.errs, "expected argument before ','");
            }
            first_comma = false;
        }
    }
    if (*p.pos == ')') {
        p.pos++;
    } else {
        error_ctx_parse_error(&p.errs, "expected ')' at end of call");
    }
    return parse_success(&p);
}

static bool parse_right_unary_op(Parser* parent, UnaryOp* op) {
    Parser p = parser(parent);

    if (hop_start(&p, "++")) {
        *op = UNARY_OP_RIGHT_PLUS_PLUS;
    } else if (hop_start(&p, "--")) {
        *op = UNARY_OP_RIGHT_MINUS_MINUS;
    } else {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_dot_prop(Parser* parent, Expr* ret) {
    Parser p = parser(parent);

    if (!hop_char(&p, '.')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    cstr prop;
    Span prop_span;
    if (!parse_identifier(&p, &prop, &prop_span)) {
        return parse_fail(&p);
    }

    *ret = expr_literal(prop_span, value_string(prop));
    return parse_success(&p);
}

static bool parse_ternary_expr(Parser* parent, ExprCtx ctx, Expr* ret_yes, Expr* ret_no) {
    Parser p = parser(parent);

    if (!hop_char(&p, '?')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, ctx, ret_yes)) {
        error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
        *ret_yes = expr_undefined(span_from_pos(&p));
        *ret_no = expr_undefined(span_from_pos(&p));
        return parse_success(&p);
    }
    hop_whitespace(&p);

    if (!hop_char(&p, ':')) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "':' expected.");
        *ret_no = expr_undefined(span_from_pos(&p));
        return parse_success(&p);
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, ctx, ret_no)) {
        error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
        *ret_no = expr_undefined(span_from_pos(&p));
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

    if (!parse_expr(&p, expr_ctx_null, ret)) {
        error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
        *ret = expr_undefined(span_from_pos(&p));
        return parse_success(&p);
    }

    if (!hop_char(&p, ']')) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "']' expected.");
        return parse_success(&p);
    }

    return parse_success(&p);
}

static bool parse_as_type(Parser* parent, Type* ret) {
    Parser p = parser(parent);

    if (!hop_start(&p, "as")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_type(&p, TYPE_PRECEDENCE_NULL, ret)) {
        error_ctx_error(&p.errs, span_from_token(&p), 1110, "Type expected.");
        *ret = type_any;
    }

    return parse_success(&p);
}

static bool parse_binary_op(Parser* parent, BinaryOp* op) {
    Parser p = parser(parent);

    // TODO: transform this into a big switch or hashmap or something
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
    else if (hop_start(&p, "=")) *op = BINARY_OP_EQUAL;
    else if (hop_start(&p, "+")) *op = BINARY_OP_PLUS;
    else if (hop_start(&p, "-")) *op = BINARY_OP_MINUS;
    // else if (hop_start(&p, ",")) *op = BINARY_OP_COMMA; handled in parse_comma_ops_right_side()
    else return parse_fail(&p);

    return parse_success(&p);
}

static bool parse_binary_op_expr(Parser* parent, ExprCtx ctx, BinaryOp* op, Expr* right) {
    Parser p = parser(parent);

    if (!parse_binary_op(&p, op)) {
        return parse_fail(&p);
    }
    if (ctx.block_in_op && *op == BINARY_OP_IN) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, ctx, right)) {
        error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
        *right = expr_undefined(span_from_pos(&p));
    }

    return parse_success(&p);
}

static bool parse_left_unary_op(Parser* parent, UnaryOp* op) {
    if (hop_start(parent, "!")) {
        *op = UNARY_OP_BANG;
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

    Anchor a = span_start(&p);
    UnaryOp op = 0;
    if (!parse_left_unary_op(&p, &op)) {
        return parse_fail(&p);
    }
    Span op_span = span_end(a);
    hop_whitespace(&p);

    Expr expr;
    if (!parse_expr(&p, ctx, &expr)) {
        error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
        expr = expr_undefined(span_from_pos(&p));
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
    Parser p = parser(parent);

    cstr number_str;
    if (!parse_number_str(&p, &number_str)) {
        return parse_fail(&p);
    } else {
        // TODO: javascript-compliant number parsing
        char* end;
        *ret = strtod(cstr_str(&number_str), &end);
        if (end != cstr_str(&number_str) + cstr_size(&number_str)) {
            error_ctx_parse_error(&p.errs, "invalid number");
        }
        cstr_drop(&number_str);
        return parse_success(&p);
    }
}

static bool parse_string(Parser* parent, cstr* ret) {
    Parser p = parser(parent);

    char quote = *p.pos;
    if (quote != '"' && quote != '\'') {
        return parse_fail(&p);
    }
    p.pos++;
    const char* start = p.pos;
    // TODO: remotely correct string parsing
    while (*p.pos && *p.pos != '\n' && *p.pos != quote) {
        if (*p.pos == '\\' && *(p.pos + 1)) {
            p.pos += 2;
        } else {
            p.pos++;
        }
    }
    *ret = cstr_from_n(start, p.pos - start);
    if (*p.pos == quote) {
        p.pos++;
    } else {
        error_ctx_error(&p.errs, span_from_pos(&p), 1002, "Unterminated string literal.");
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
    Anchor a = span_starting_at(&p, left_and_ret->span);

    hop_whitespace(&p);
    bool hit_newline = p.whitespace == WHITESPACE_NEWLINE;

    union {
        VecExpr args;
        struct {
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
    } parsed;

    if (parse_call_args(&p, &parsed.args)) {
        *left_and_ret = expr_call(
            span_end(a),
            c_new(CallExpr, {
                .uses_new = false,
                .callable = *left_and_ret,
                .args = parsed.args,
            })
        );
        return parse_success(&p);
    }

    if (parse_dot_prop(&p, &parsed.expr_a)) {
        *left_and_ret = expr_property(
            span_end(a),
            c_new(PropertyExpr, {
                .expr = *left_and_ret,
                .prop = parsed.expr_a,
                .form = PROPERTY_EXPR_DOT,
            })
        );
        return parse_success(&p);
    }

    if (parse_ternary_expr(&p, ctx, &parsed.expr_a, &parsed.expr_b)) {
        *left_and_ret = expr_ternary(
            span_end(a),
            c_new(TernaryExpr, {
                .condition = *left_and_ret,
                .yes = parsed.expr_a,
                .no = parsed.expr_b,
            })
        );
        return parse_success(&p);
    }

    if (parse_bracket_prop(&p, &parsed.expr_a)) {
        *left_and_ret = expr_property(
            span_end(a),
            c_new(PropertyExpr, {
                .expr = *left_and_ret,
                .prop = parsed.expr_a,
                .form = PROPERTY_EXPR_BRACKET,
            })
        );
        return parse_success(&p);
    }

    if (parse_as_type(&p, &parsed.type)) {
        *left_and_ret = expr_as_type(
            span_end(a),
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
        sub_p.pos = span_start_ptr(&left_and_ret->span);

        if (parse_arrow_function(&sub_p, ctx, &parsed.function)) {
            parse_success(&sub_p);
            expr_drop(left_and_ret);
            *left_and_ret = expr_function(span_end(a), c_new(FunctionImpl, parsed.function));
            return parse_success(&p);
        }
    }

    if (!hit_newline) {
        if (parse_right_unary_op(&p, &parsed.unary)) {
            *left_and_ret = expr_unary_op(
                span_end(a),
                c_new(UnaryOpExpr, {
                    .op = parsed.unary,
                    .expr = *left_and_ret,
                })
            );
            return parse_success(&p);
        }
    }

    if (parse_binary_op_expr(&p, ctx, &parsed.binary.op, &parsed.binary.expr)) {
        Expr* insert_at = &parsed.binary.expr;

        while (
            insert_at->tag == EXPR_BINARY_OP &&
            insert_at->peren_depth == 0 &&
            binary_op_cmp_precedence(parsed.binary.op, insert_at->binary_op->op) >= 0
        ) {
            insert_at->span = span_between(&left_and_ret->span, &insert_at->span);
            insert_at = &insert_at->binary_op->left;
        }

        *insert_at = expr_binary_op(span_between(&left_and_ret->span, &insert_at->span), c_new(BinaryOpExpr, {
            .left = *left_and_ret,
            .op = parsed.binary.op,
            .right = *insert_at,
        }));

        *left_and_ret = parsed.binary.expr;

        return parse_success(&p);
    }

    return parse_fail(&p);
}

static bool parse_comma_ops_right_side(Parser* parent, Expr* left_and_ret) {
    Parser p = parser(parent);
    Anchor a = span_starting_at(&p, left_and_ret->span);
    Expr expr;
    bool parsed_commas = false;

    hop_whitespace(&p);

    while (hop_char(&p, ',')) {
        parsed_commas = true;
        hop_whitespace(&p);

        if (!parse_expr(&p, expr_ctx_null, &expr)) {
            error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
            return parse_success(&p);
        }

        *left_and_ret = expr_binary_op(
            span_end(a),
            c_new(BinaryOpExpr, {
                .left = *left_and_ret,
                .op = BINARY_OP_COMMA,
                .right = expr,
            })
        );
    }
    return parsed_commas ? parse_success(&p) : parse_fail(&p);
}

static bool parse_expr(Parser* parent, ExprCtx ctx, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);
    FAIL_CACHE_HOOK(&p, FAIL_CACHE_EXPR);
    ret->peren_depth = 0;

    union {
        cstr name;
        FunctionImpl function;
        ClassDecl class_decl;
    } parsed;

    if (parse_literal(&p, &ret->literal)) {
        ret->tag = EXPR_LITERAL;
    } else if (parse_object(&p, &ret->object)) {
        ret->tag = EXPR_OBJECT;
    } else if (parse_array(&p, &ret->array)) {
        ret->tag = EXPR_ARRAY;
    } else if (parse_class_decl(&p, &parsed.class_decl)) {
        *ret = expr_class_decl(span_end(a), c_new(ClassDecl, parsed.class_decl));
    } else if (hop_start_if_ends_on_word_break(&p, "new")) {
        hop_whitespace(&p);
        if (!parse_expr(&p, ctx, ret)) {
            error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
            *ret = expr_undefined(span_end(a));
        }
        if (ret->tag == EXPR_CALL) {
            ret->call->uses_new = true;
        } else {
            *ret = expr_call(
                span_end(a),
                c_new(CallExpr, {
                    .uses_new = true,
                    .callable = *ret,
                    .args = vec_expr_init(),
                })
            );
        }
    } else if (parse_left_unary_op_expr(&p, ctx, ret)) {
    } else if (parse_function_keyword_function(&p, &parsed.function)) {
        *ret = expr_function(span_end(a), c_new(FunctionImpl, parsed.function));
    } else if (parse_identifier(&p, &parsed.name, nullptr)) {
        *ret = expr_identifier(span_end(a), parsed.name);
    } else if (parse_arrow_function(&p, ctx, &parsed.function)) {
        *ret = expr_function(span_end(a), c_new(FunctionImpl, parsed.function));
    } else if (hop_char(&p, '(')) {
        hop_whitespace(&p);
        if (!parse_expr(&p, expr_ctx_null, ret)) {
            return parse_fail(&p);
        }
        parse_comma_ops_right_side(&p, ret);
        hop_whitespace(&p);
        if (!hop_char(&p, ')')) {
            error_ctx_error(&p.errs, hop_token_get_span(&p), 1005, "')' expected.");
        }
        ret->peren_depth++;
    } else if (hop_char(&p, '<')) {
        Type t;
        Expr e;
        hop_whitespace(&p);
        if (!parse_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
            error_ctx_error(&p.errs, span_from_token(&p), 1110, "Type expected.");
            t = type_any;
        }
        hop_whitespace(&p);
        if (!hop_char(&p, '>')) {
            error_ctx_error(&p.errs, hop_token_get_span(&p), 1005, "'>' expected.");
        }
        hop_whitespace(&p);
        if (!parse_expr(&p, ctx, &e)) {
            Span s = hop_token_get_span(&p);
            error_ctx_error(&p.errs, s, 1109, "Expression expected.");
            e = expr_undefined(s);
        }
        *ret = expr_as_type(span_end(a), c_new(AsTypeExpr, {
            .expr = e,
            .type = t,
            .angle_bracket_syntax = true,
        }));
    } else {
        return parse_fail(&p);
    }

    ret->span = span_end(a);

    while (parse_expr_right_side(&p, ctx, ret)) {}

    return parse_success(&p);
}

static bool parse_name_init_with_name(Parser* parent, ExprCtx ctx, cstr name, Span name_span, NameInitData* ret) {
    Parser p = parser(parent);

    Type type = type_any;
    bool type_explicit = false;
    Anchor type_a = span_start(&p);
    if (hop_start(&p, ":")) {
        hop_whitespace(&p);
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
            type_explicit = true;
        } else {
            error_ctx_error(&p.errs, span_from_token(&p), 1110, "Type expected.");
            type = type_any;
        }
        hop_whitespace(&p);
    }
    Span type_span = span_end(type_a);

    Expr value;
    bool value_explicit = false;
    if (hop_start(&p, "=")) {
        hop_whitespace(&p);
        if (parse_expr(&p, ctx, &value)) {
            value_explicit = true;
        } else {
            error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
        }
    }
    if (!value_explicit) {
        value = expr_undefined(span_from_pos(&p));
    }

    *ret = (NameInitData) {
        .name = name,
        .name_span = name_span,
        .type_explicit = type_explicit,
        .type = type,
        .type_span = type_span,
        .value_explicit = value_explicit,
        .value = value,
    };
    return parse_success(&p);
}

static bool parse_name_init(Parser* parent, ExprCtx ctx, NameInitData* ret) {
    Parser p = parser(parent);

    cstr name = cstr_null;
    Span name_span;
    if (!parse_identifier(&p, &name, &name_span)) {
        name_span = span_from_pos(&p);
        error_ctx_error(&p.errs, span_from_token(&p), 1003, "Identifier expected.");
    }
    hop_whitespace(&p);

    if (!parse_name_init_with_name(&p, ctx, name, name_span, ret)) {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

// Only parses content, sets type and value to defaults
static bool parse_binding_content(Parser* parent, Binding* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    cstr identifier;
    Binding binding;

    if (parse_identifier(&p, &identifier, nullptr)) {
        ret->tag = BINDING_IDENTIFIER;
        ret->identifier = identifier;
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
                    .tag = BINDING_NOOP,
                });
            } else {
                break;
            }
            hop_whitespace(&p);

            if (!hop_char(&p, ',') && *p.pos != ']') {
                error_ctx_error(&p.errs, span_from_token(&p), 1005, "',' expected.");
            }
            hop_whitespace(&p);
        }
        if (!hop_char(&p, ']')) {
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "']' expected.");
        }
    } else if (hop_char(&p, '{')) {
        hop_whitespace(&p);
        ret->tag = BINDING_OBJECT;
        ret->object = vec_object_binding_item_init();
        while (*p.pos && *p.pos != ']') {
            if (parse_identifier(&p, &identifier, nullptr)) {
                Span identifier_span = span_end(a);

                hop_whitespace(&p);

                if (hop_char(&p, ':')) {
                    hop_whitespace(&p);

                    binding = (Binding){0};
                    if (!parse_binding_content(&p, &binding)) {
                        // TODO
                        break;
                    }

                    vec_object_binding_item_push(&ret->object, (ObjectBindingItem){
                        .source = value_string(cstr_clone(identifier)),
                        .source_span = identifier_span,
                        .target = binding,
                    });
                } else {
                    vec_object_binding_item_push(&ret->object, (ObjectBindingItem){
                        .source = value_string(cstr_clone(identifier)),
                        .source_span = identifier_span,
                        .target = (Binding){
                            .tag = BINDING_IDENTIFIER,
                            .identifier = identifier,
                            .content_span = identifier_span,
                        },
                    });
                }
            } else {
                break;
            }
            hop_whitespace(&p);

            if (!hop_char(&p, ',') && *p.pos != '}') {
                error_ctx_error(&p.errs, span_from_token(&p), 1005, "',' expected.");
            }
            hop_whitespace(&p);
        }
        if (!hop_char(&p, '}')) {
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "'}' expected.");
        }
    } else {
        return parse_fail(&p);
    }

    ret->content_span = span_end(a);
    return parse_success(&p);
}

static bool parse_binding_type(Parser* parent, Binding* ret) {
    if (!hop_char(parent, ':')) {
        return false;
    }
    hop_whitespace(parent);

    Anchor a = span_start(parent);
    if (!parse_type(parent, TYPE_PRECEDENCE_NULL, &ret->type)) {
        error_ctx_error(&parent->errs, span_from_token(parent), 1110, "Type expected.");
        ret->type = type_any;
    }
    ret->type_explicit = true;
    ret->type_span = span_end(a);

    return true;
}

static bool parse_initialization(Parser* parent, ExprCtx ctx, Expr* ret) {
    if (!hop_char(parent, '=')) {
        return false;
    }
    hop_whitespace(parent);

    if (!parse_expr(parent, ctx, ret)) {
        error_ctx_error(&parent->errs, span_from_token(parent), 1109, "Expression expected.");
        *ret = expr_implicit_any(span_from_pos(parent));
    }

    return true;
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
    Anchor end_of_keyword = span_start(&p);
    hop_whitespace(&p);

    Span comma_span = span_null;
    InitializedBinding initializer;
    ret->initializers = vec_initialized_binding_init();

    while (true) {
        if (!parse_binding_content(&p, &initializer.binding)) {
            if (span_valid(&comma_span)) {
                error_ctx_error(&p.errs, comma_span, 1009, "Trailing comma not allowed.");
                p.pos = span_end_ptr(&comma_span);
            } else if (vec_initialized_binding_empty(&ret->initializers)) {
                if (ret->keyword == VAR_INIT_LET) {
                    // let can also be a variable name, fail so we then attempt to parse it as such
                    variable_statement_drop(ret);
                    return parse_fail(&p);
                } else {
                    Span s = span_end(end_of_keyword);
                    s.length = 0;
                    error_ctx_error(&p.errs, s, 1123, "Variable declaration list cannot be empty.");
                }
            } else {
                error_ctx_error(&p.errs, span_from_token(&p), 1134, "Variable declaration expected.");
            }
            break;
        }
        const char* end_of_name_init = p.pos;
        hop_whitespace(&p);

        if (parse_binding_type(&p, &initializer.binding)) {
            end_of_name_init = p.pos;
            hop_whitespace(&p);
        } else {
            initializer.binding.type_span = span_null;
            initializer.binding.type = type_any;
            initializer.binding.type_explicit = false;
        }

        if (parse_initialization(&p, ctx, &initializer.value)) {
            end_of_name_init = p.pos;
            hop_whitespace(&p);
        } else {
            initializer.value = expr_implicit_any(span_from_pos(&p));
        }

        vec_initialized_binding_push(&ret->initializers, initializer);

        if (*p.pos == ';' ||
            *p.pos == ')' ||
            *p.pos == '\0' ||
            hop_start_if_ends_on_word_break(&p, "in") ||
            hop_start_if_ends_on_word_break(&p, "of")
        ) {
            p.pos = end_of_name_init;
            break;
        }

        if (hop_char(&p, ',')) {
            comma_span = span_from_previous_char(&p);
        } else {
            comma_span = span_null;
            if (p.whitespace == WHITESPACE_NEWLINE) {
                break;
            } else {
                error_ctx_error(&p.errs, span_from_token(&p), 1005, "',' expected.");
            }
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

    if (!hop_start(&p, "(")) {
        error_ctx_parse_error(&p.errs, "expected '(' after if");
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, expr_ctx_null, condition)) {
        error_ctx_parse_error(&p.errs, "invalid if condition");
        *condition = expr_literal(span_from_pos(&p), value_boolean(false));
    }
    hop_whitespace(&p);

    if (!hop_start(&p, ")")) {
        error_ctx_parse_error(&p.errs, "expected ')' after if condition");
    }
    hop_whitespace(&p);

    if (!parse_statement(&p, false, statement)) {
        error_ctx_parse_error(&p.errs, "invalid if body");
        *statement = statement_noop;
    }
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
            } else if (parse_statement(&p, false, &statement)) {
                vec_statement_push(&statements, statement);
                break;
            } else {
                error_ctx_parse_error(&p.errs, "expected else statement");
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

    if (!hop_start(&p, "(")) {
        error_ctx_parse_error(&p.errs, "expected '(' after for");
    }
    hop_whitespace(&p);

    bool is_for_each = false;
    Statement statement;
    Expr left_expr;
    Expr right_expr;
    Statement body;
    bool keyword_is_in = false;

    if (parse_variable_statement(&p, (ExprCtx){.block_in_op = true}, &statement.decl.variable)) {
        statement.decl.exported = DECL_EXPORTED_NO;
        statement.tag = STATEMENT_DECL_VARIABLE;
    } else if (parse_expr(&p, (ExprCtx){.block_in_op = true}, &statement.expr)) {
        parse_comma_ops_right_side(&p, &statement.expr);
        statement.tag = STATEMENT_EXPR;
    } else if (*p.pos == ';') {
        statement = statement_noop;
    } else {
        error_ctx_error(&p.errs, hop_token_get_span(&p), 1009, "Expression expected.");
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

        if (hop_start(&p, ";")) {
            hop_whitespace(&p);
            if (parse_expr(&p, expr_ctx_null, &right_expr)) {
                parse_comma_ops_right_side(&p, &right_expr);
            } else {
                right_expr = expr_undefined(span_from_pos(&p));
            }
            hop_whitespace(&p);
        } else {
            error_ctx_error(&p.errs, span_from_token(&p), 1005, "';' expected.");
            right_expr = expr_undefined(span_from_pos(&p));
            hop_token(&p);
        }

    } else if ((statement.tag == STATEMENT_EXPR || statement.tag == STATEMENT_DECL_VARIABLE) && (
        hop_start_if_ends_on_word_break(&p, "of") ||
        (hop_start_if_ends_on_word_break(&p, "in") && (keyword_is_in = true)) // control flow is my passion
    )) {
        is_for_each = true;

        hop_whitespace(&p);

        if (parse_expr(&p, expr_ctx_null, &right_expr)) {
            parse_comma_ops_right_side(&p, &right_expr);
        } else {
            error_ctx_error(&p.errs, span_from_token(&p), 1009, "Expression expected.");
            right_expr = expr_undefined(span_from_pos(&p));
        }
        hop_whitespace(&p);
    } else {
        // Unclear why this is the error message in this case
        error_ctx_error(&p.errs, hop_token_get_span(&p), 1005, "',' expected.");
        left_expr = expr_undefined(span_from_pos(&p));
        right_expr = expr_undefined(span_from_pos(&p));
    }
    hop_whitespace(&p);

    if (hop_start(&p, ")")) {
        hop_whitespace(&p);
        if (!parse_statement(&p, false, &body)) {
            error_ctx_error(&p.errs, span_from_token(&p), 1009, "Expression expected.");
            body = statement_noop;
        }
    } else {
        error_ctx_error(&p.errs, hop_token_get_span(&p), 1005, "')' expected.");
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

    if (!hop_start(&p, "(")) {
        error_ctx_parse_error(&p.errs, "expected '(' after while");
    }
    hop_whitespace(&p);

    Expr condition = expr_literal(span_from_pos(&p), value_boolean(false));
    Statement body = statement_noop;
    if (parse_expr(&p, expr_ctx_null, &condition)) {
        parse_comma_ops_right_side(&p, &condition);
    } else {
        error_ctx_parse_error(&p.errs, "expected expression before ')'");
    }
    hop_whitespace(&p);

    if (!hop_start(&p, ")")) {
        error_ctx_parse_error(&p.errs, "expected ')' at end of for header");
    }
    hop_whitespace(&p);

    if (!parse_statement(&p, false, &body)) {
        error_ctx_parse_error(&p.errs, "expected while loop body");
    }

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
                    error_ctx_error(&p.errs, span_from_token(&p), 1003, "Identifier expected.");
                    try_catch.catch_var = (Binding){0};
                }
                hop_whitespace(&p);

                if (!hop_char(&p, ')')) {
                    error_ctx_error(&p.errs, span_from_token(&p), 1005, "')' expected.");
                }
                hop_whitespace(&p);
            }
            if (!parse_block(&p, false, &try_catch.catch_block)) {
                error_ctx_error(&p.errs, span_from_token(&p), 1005, "'{' expected.");
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
                error_ctx_error(&p.errs, span_from_token(&p), 1005, "'{' expected.");
                try_catch.finally_block = (Block){0};
            }
        } else {
            try_catch.finally_block = (Block){0};
        }

        if (!has_catch && !has_finally) {
            error_ctx_error(&p.errs, span_from_token(&p), 1472, "'catch' or 'finally' expected.");
        }
    } else {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "'{' expected.");
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
    Anchor a = span_start(&p);

    if (!hop_start_if_ends_on_word_break(&p, "return")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, expr_ctx_null, ret)) {
        *ret = expr_undefined(span_end(a));
    }

    return parse_success(&p);
}

static bool parse_throw(Parser* parent, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    if (!hop_start_if_ends_on_word_break(&p, "throw")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, expr_ctx_null, ret)) {
        *ret = expr_undefined(span_end(a));
    }

    return parse_success(&p);
}

static bool parse_statement_decl(Parser* parent, DeclExported exported, Statement* ret) {
    Parser p = parser(parent);

    union {
        VariableStatement variable;
        FunctionImpl function;
        ClassDecl class;
        TypeDecl type_decl;
        Expr expr;
    } parsed;

    if (exported != DECL_EXPORTED_YES_DEFAULT &&
        parse_variable_statement(&p, expr_ctx_null, &parsed.variable)
    ) {
        *ret = (Statement) {
            .tag = STATEMENT_DECL_VARIABLE,
            .decl.exported = exported,
            .decl.variable = parsed.variable,
        };
    } else if (parse_function_keyword_function(&p, &parsed.function)) {
        *ret = (Statement){
            .tag = STATEMENT_DECL_FUNCTION,
            .decl.exported = exported,
            .decl.function = parsed.function,
        };
    } else if (parse_class_decl(&p, &parsed.class)) {
        *ret = (Statement){
            .tag = STATEMENT_DECL_CLASS,
            .decl.exported = exported,
            .decl.class = c_new(ClassDecl, parsed.class),
        };
    } else if (parse_interface_decl(&p, &parsed.type_decl) ||
               parse_type_decl(&p, &parsed.type_decl)
    ) {
        *ret = (Statement){
            .tag = STATEMENT_DECL_TYPE,
            .decl.exported = exported,
            .decl.type = parsed.type_decl,
        };
    } else if (exported == DECL_EXPORTED_YES_DEFAULT && parse_expr(&p, expr_ctx_null, &parsed.expr)) {
        *ret = (Statement){
            .tag = STATEMENT_EXPORT_DEFAULT_EXPR,
            .expr = parsed.expr,
        };
    } else {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_statement(Parser* parent, bool toplevel, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    if (*p.pos == ';') {
        *ret = statement_noop;
    } else if (hop_start_if_ends_on_word_break(&p, "export")) {
        if (!toplevel) {
            error_ctx_error(&p.errs, span_end(a), 1184, "Modifiers cannot appear here.");
        }

        hop_whitespace(&p);

        DeclExported exported = DECL_EXPORTED_YES;
        if (hop_start_if_ends_on_word_break(&p, "default")) {
            exported = DECL_EXPORTED_YES_DEFAULT;
            hop_whitespace(&p);
        }

        if (!parse_statement_decl(&p, toplevel ? exported : DECL_EXPORTED_NO, ret)) {
            if (exported == DECL_EXPORTED_YES_DEFAULT) {
                *ret = statement_noop;
                error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
                return parse_success(&p);
            } else {
                return parse_fail(&p);
            }
        }

        switch (ret->tag) {
            case STATEMENT_DECL_FUNCTION: ret->decl.function.full_span = span_end(a); break;
            case STATEMENT_DECL_CLASS: ret->decl.class->full_span = span_end(a); break;
            case STATEMENT_DECL_TYPE: ret->decl.type.full_span = span_end(a); break;
            default: break;
        }
    } else if (parse_statement_decl(&p, DECL_EXPORTED_NO, ret)) {
    } else if (parse_block(&p, false, &ret->block)) {
        ret->tag = STATEMENT_BLOCK;
    } else if (parse_if_else(&p, &ret->if_else)) {
        ret->tag = STATEMENT_IF_ELSE;
    } else if (parse_for_loop_statement(&p, ret)) {
    } else if (parse_while_loop_statement(&p, ret)) {
    } else if (parse_try_catch_statement(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "break")) {
        ret->tag = STATEMENT_BREAK;
        ret->break_span = span_end(a);
    } else if (parse_return(&p, &ret->expr)) {
        ret->tag = STATEMENT_RETURN_EXPR;
    } else if (parse_throw(&p, &ret->expr)) {
        ret->tag = STATEMENT_THROW_EXPR;
    } else if (parse_expr(&p, expr_ctx_null, &ret->expr)) {
        ret->tag = STATEMENT_EXPR;
        parse_comma_ops_right_side(&p, &ret->expr);
        if (ret->tag == STATEMENT_EXPR && ret->expr.tag == EXPR_IDENTIFIER && cstr_equals(&ret->expr.identifier.name, "static")) {
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

static bool parse_block_inner_append(Parser* parent, bool toplevel, Block* ret) {
    Parser p = parser(parent);

    while(*p.pos && *p.pos != '}') {
        Statement statement = {0};
        if (*p.pos == ';') {
            p.pos++;
        } else if (parse_statement(&p, toplevel, &statement)) {
            vec_statement_push(&ret->statements, statement);
            bool ended_on_brace = p.pos > p.shared->file_start && *(p.pos - 1) == '}';
            hop_whitespace(&p);
            if (p.whitespace == WHITESPACE_NEWLINE ||
                statement.tag == STATEMENT_NOOP ||
                !*p.pos ||
                ended_on_brace ||
                *p.pos == '}'
            ) {
            } else if (*p.pos == ';') {
                p.pos++;
            } else if (*p.pos) {
                if (statement.tag == STATEMENT_EXPR && statement.expr.tag == EXPR_IDENTIFIER) {
                    error_ctx_error(&p.errs, statement.expr.span, 1434, "Unexpected keyword or identifier.");
                } else {
                    error_ctx_error(&p.errs, span_from_token(&p), 1005, "';' expected.");
                }
            }
        } else if (*p.pos == '\\') {
            error_ctx_error(&p.errs, span_from_pos(&p), 1127, "Invalid character.");
            p.pos++;
        } else {
            error_ctx_error(&p.errs, hop_token_get_span(&p), 1128, "Declaration or statement expected.");
        }
        hop_whitespace(&p);
    }

    return parse_success(&p);
}

static bool parse_block(Parser* parent, bool toplevel, Block* ret) {
    Parser p = parser(parent);

    if (!hop_char(&p, '{')) return parse_fail(&p);
    hop_whitespace(&p);

    *ret = (Block){0};
    if (!parse_block_inner_append(&p, toplevel, ret)) {
        *ret = (Block){0};
    }
    hop_whitespace(&p);

    if (!hop_char(&p, '}')) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "'}' expected.");
    }
    return parse_success(&p);
}

Block parse_file(const char* source, ErrorCtx* errs) {
    ParserShared shared = {
        .file_start = source,
        .fail_cache_start = source,
    };

    Parser p = {
        .shared = &shared,
        .parent = nullptr,
        .pos = source,
        .errs = { .phase = ERROR_PHASE_PARSE },
    };

    if (p.pos[0] == '\xEF' && p.pos[1] == '\xBB' && p.pos[2] == '\xBF') {
        // Skip Byte Order Mark nonsense
        p.pos += 3;
    }

    hop_whitespace(&p);

    Block scope = {0};
    while (true) {
        parse_block_inner_append(&p, true, &scope);
        if (*p.pos) {
            error_ctx_error(&p.errs, hop_token_get_span(&p), 1128, "Declaration or statement expected.");
            hop_whitespace(&p);
        } else {
            break;
        }
    }

    parser_shared_drop(&shared);

    error_ctx_append_consume(errs, p.errs);
    return scope;
}
