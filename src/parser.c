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
static bool parse_name_init_with_name(Parser* parent, cstr name, Span name_span, NameInitData* ret);
static bool parse_name_init(Parser* parent, NameInitData* ret);
static bool parse_statement(Parser* parent, bool toplevel, Statement* ret);
static bool parse_type(Parser* parent, TypePrecedence min_precedence, Type* ret);
static bool parse_type_compat_literal(Parser* parent, Value* ret);
static bool parse_expr(Parser* parent, Expr* ret);

static inline Parser parser(Parser* parent) {
    return (Parser){
        .shared = parent->shared,
        .parent = parent,
        .pos = parent->pos,
        .errs = {0},
    };
}

/// Must not be used on the root parser, always returns true, drops parser
static inline bool parse_success(Parser* me) {
    my parent->pos = my pos;
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

static bool parse_identifier(Parser* parent, cstr* ret, Span* span) {
    Anchor a = span_start(parent);
    if (is_identifier_start_char(*parent->pos)) {
        parent->pos++;
        while (is_identifier_char(*parent->pos)) parent->pos++;
        if (span) {
            *span = span_end(a);
        }
        csview sv = csview_from_n(a.start, parent->pos - a.start);
        if (csview_cmp(&sv, &c_sv("return")) == 0) {
            return false; // TODO: check for all keywords efficiently
        }
        *ret = cstr_from_sv(sv);
        return true;
    } else {
        return false;
    }
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

typedef enum WhitespaceRet {
    WHITESPACE_NONE = 0,
    WHITESPACE_PARSED,
    WHITESPACE_NEWLINE,
} WhitespaceRet;

static inline WhitespaceRet hop_comment(Parser* parent) {
    if (*parent->pos != '/') {
        return WHITESPACE_NONE;
    }
    switch (*(parent->pos + 1)) {
        case '/':
            (parent->pos) += 2;
            while (*parent->pos && *parent->pos != '\n') {
                (parent->pos)++;
            }
            return WHITESPACE_PARSED;

        case '*': {
            WhitespaceRet ret = WHITESPACE_PARSED;
            const char* pos = parent->pos;
            pos += 2;
            while (*(pos - 1) != '*' || *pos != '/') {
                switch (*pos) {
                    case '\n': ret = WHITESPACE_NEWLINE; break;
                    case '\0': return WHITESPACE_NONE;
                    default: break;
                }
                pos++;
            }
            pos++;
            parent->pos = pos;
            return ret;
        }

        default: break;
    }
    return WHITESPACE_NONE;
}

static inline WhitespaceRet hop_whitespace(Parser* parent) {
    // TODO unicode and additional character support: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Lexical_grammar
    WhitespaceRet ret = WHITESPACE_NONE;
    while (true) {
        switch (*parent->pos) {
            case ' ':
            case '\t':
            case '\r':
                parent->pos++;
                break;

            case '\n':
                ret = WHITESPACE_NEWLINE;
                parent->pos++;
                break;

            case '/':
                switch (hop_comment(parent)) {
                    case WHITESPACE_NONE: return ret;
                    case WHITESPACE_PARSED: break;
                    case WHITESPACE_NEWLINE: ret = WHITESPACE_NEWLINE; break;
                }
                break;

            default:
                return ret;
        }
        if (ret == WHITESPACE_NONE) {
            ret = WHITESPACE_PARSED;
        }
    }
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

static inline bool hop_start_and_required_whitespace(Parser* parent, const char* prefix) {
    Parser p = parser(parent);
    if (hop_start(&p, prefix)) {
        if (hop_whitespace(&p)) {
            return parse_success(&p);
        }
    }
    return parse_fail(&p);
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

        default: break;
    }

    union {
        Value value;
        BinaryOp bin_op;
        UnaryOp un_op;
        cstr identifier;
    } parsed;

    if (parse_literal(parent, &parsed.value)) {
        value_drop(&parsed.value);
        return true;
    } else if (parse_identifier(parent, &parsed.identifier, nullptr)) {
        cstr_drop(&parsed.identifier);
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
        // TODO: hop keywords
        parent->pos++;
        return true;
    }
}

static Span hop_token_get_span(Parser* parent) {
    Anchor a = span_start(parent);
    hop_token(parent);
    return span_end(a);
}

typedef enum ParseSequenceSep {
    SEQUENCE_SEP_COMMA,
    SEQUENCE_SEP_COMMA_SEMI_NONE, // typescript interfaces are weird
} ParseSequenceSep;

static inline bool sep_required(ParseSequenceSep sep) {
    switch (sep) {
        case SEQUENCE_SEP_COMMA: return true;
        case SEQUENCE_SEP_COMMA_SEMI_NONE: return false;
    }
    UNREACHABLE;
}

static inline bool char_is_sep(ParseSequenceSep sep, char c) {
    switch (sep) {
        case SEQUENCE_SEP_COMMA: return c == ',';
        case SEQUENCE_SEP_COMMA_SEMI_NONE: return c == ',' || c == ';';
    }
    UNREACHABLE;
}

// Yes having start be a string and end a char is weird. whatcha gonna do about it?
static bool parse_sequence(
    Parser* parent,
    const char* start,
    ParseSequenceSep sep,
    char end,
    bool (*callback)(Parser* parent, void* data),
    void* data
) {
    Parser p = parser(parent);
    if (!hop_start(&p, start)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    while (*p.pos && *p.pos != end) {
        const char* before_callback = p.pos;
        if (!callback(&p, data)) {
            error_ctx_error(&p.errs, span_from_pos(&p), TS_CODE_NONE, "invalid item in sequence");
            while (*p.pos != end && char_is_sep(sep, *p.pos)) {
                p.pos++;
            }
        }
        if (p.pos == before_callback) {
            p.pos++;
        }
        hop_whitespace(&p);
        if (sep_required(sep) && !char_is_sep(sep, *p.pos) && *p.pos != end) {
            error_ctx_parse_error(&p.errs, "expected separator or end of sequence");
        }
        bool first_sep = true;
        while (char_is_sep(sep, *p.pos)) {
            p.pos++;
            hop_whitespace(&p);
            if (!first_sep) {
                error_ctx_parse_error(&p.errs, "expected item before separator");
            }
            first_sep = false;
        }
    }
    if (*p.pos == end) {
        p.pos++;
    } else {
        error_ctx_parse_error(&p.errs, "expected end of sequence");
    }
    return parse_success(&p);
}

static bool parse_generic_decl_item(Parser* parent, void* data) {
    Parser p = parser(parent);
    VecGenericItem* ret = data;
    cstr name = cstr_null;
    Span name_span = span_from_pos(&p);

    if (!parse_identifier(&p, &name, &name_span)) {
        error_ctx_parse_error(&p.errs, "missing generic name");
    }
    hop_whitespace(&p);

    Type extends = type_any;
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        hop_whitespace(&p);
        parse_type(&p, TYPE_PRECEDENCE_NULL, &extends);
        hop_whitespace(&p);
    }

    Type fallback = type_any;
    bool fallback_explicit = false;
    if (hop_start(&p, "=")) {
        hop_whitespace(&p);
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &fallback)) {
            fallback_explicit = true;
        }
    }

    vec_generic_item_push(ret, (GenericItem){
        .name = name,
        .name_span = name_span,
        .extends = extends,
        .fallback_explicit = fallback_explicit,
        .fallback = fallback,
    });
    return parse_success(&p);
}

static bool parse_generic_decl_list(Parser* parent, VecGenericItem* ret) {
    *ret = (VecGenericItem){0};
    if (!parse_sequence(parent, "<", SEQUENCE_SEP_COMMA, '>', parse_generic_decl_item, ret)) {
        vec_generic_item_drop(ret);
        return false;
    }
    return true;
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

static bool parse_callable_type_arg(Parser* parent, void* data) {
    Parser p = parser(parent);
    CallableType* ret = data;

    NameInitData name_init = {0};
    if (!parse_name_init(&p, &name_init)) {
        return parse_fail(&p);
    }

    if (name_init.value_explicit) {
        error_ctx_parse_error(&p.errs, "initializer not allowed in function type");
        expr_drop(&name_init.value);
    }

    vec_callable_type_arg_push(&ret->args, (CallableTypeArg){
        .name = name_init.name,
        .type_explicit = name_init.type_explicit,
        .type = name_init.type,
    });

    return parse_success(&p);
}

static bool parse_callable_type(Parser* parent, Type* ret) {
    Parser p = parser(parent);
    VecGenericItem generics;
    CallableType callable = {0};

    if (parse_generic_decl_list(&p, &generics)) {
        hop_whitespace(&p);
    } else {
        generics = (VecGenericItem){0};
    }

    if (!parse_sequence(&p, "(", SEQUENCE_SEP_COMMA, ')', parse_callable_type_arg, &callable)) {
        vec_generic_item_drop(&generics);
        callable_type_drop(&callable);
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!hop_start(&p, "=>")) {
        vec_generic_item_drop(&generics);
        callable_type_drop(&callable);
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_type(&p, TYPE_PRECEDENCE_NULL, &callable.ret)) {
        error_ctx_parse_error(&p.errs, "invalid return type");
    }

    *ret = type_callable(c_new(CallableType, callable));
    if (!vec_generic_item_empty(&generics)) {
        *ret = type_generic(c_new(Generic, {
            .items = generics,
            .inner = *ret,
        }));
    }
    return parse_success(&p);
}

static bool parse_type_right_side(Parser* parent, TypePrecedence min_precedence, Type* left_and_ret) {
    Parser p = parser(parent);
    Type type;

    hop_whitespace(&p);

    if (TYPE_PRECEDENCE_TERNARY >= min_precedence) {
        if (parse_ternary_type(&p, left_and_ret)) {
            return parse_success(&p);
        }
    }

    if (TYPE_PRECEDENCE_INTERSECTION >= min_precedence) {
        if (hop_char(&p, '&' && *p.pos != '&' && *p.pos != '=')) {
            hop_whitespace(&p);

            if (!parse_type(&p, TYPE_PRECEDENCE_INTERSECTION, &type)) {
                error_ctx_error(&p.errs, span_from_token(&p), 1110, "Type expected.");
                type = type_any;
            }

            *left_and_ret = type_composite(*left_and_ret, type, TYPE_INTERSECTION);
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

            *left_and_ret = type_composite(*left_and_ret, type, TYPE_UNION);
            return parse_success(&p);
        }
    }

    return parse_fail(&p);
}

static bool parse_type(Parser* parent, TypePrecedence min_precedence, Type* ret) {
    Parser p = parser(parent);
    Span span;
    cstr name;

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
    } else if (parse_type_compat_literal(&p, &ret->value)) {
        ret->tag = TYPE_VALUE;
    } else if (parse_identifier(&p, &name, &span)) {
        *ret = type_identifier(name, span);
    } else if (parse_callable_type(&p, ret)) {
        // Tag set to either callable or generic
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

        cstr name = cstr_null;
        if (!parse_identifier(&p, &name, nullptr)) {
            error_ctx_parse_error(&p.errs, "expected interface member name");
        }
        hop_whitespace(&p);

        if (!hop_char(&p, ':')) {
            error_ctx_parse_error(&p.errs, "expected ':'");
        }
        hop_whitespace(&p);

        Type type;
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
            vec_interface_member_push(&members, interface_member_make(name, type));
        } else {
            cstr_drop(&name);
            error_ctx_error(&p.errs, span_from_pos(&p), TS_CODE_NONE, "expected type");
            while (*p.pos && *p.pos != '}' && *p.pos != ',' && *p.pos != ';' && *p.pos != '\n') p.pos++;
        }
        hop_whitespace(&p);

        // Typescript accepts EITHER of these in interfaces (or nothing at all). You can MIX them!
        // it also cares about newlines but we can sort that out later
        (void)(hop_char(&p, ',') || hop_char(&p, ';'));
        hop_whitespace(&p);
    }

    if (!hop_char(&p, '}')) {
        error_ctx_parse_error(&p.errs, "expected '}'");
    }

    *ret = (Interface) {
        .members = members,
    };
    return parse_success(&p);
}

static bool parse_named_interface(Parser* parent, InterfaceStatement* ret) {
    Parser p = parser(parent);

    if (!hop_start_and_required_whitespace(&p, "interface")) {
        return parse_fail(&p);
    }

    cstr name = cstr_null;
    if (!parse_identifier(&p, &name, nullptr)) {
        error_ctx_parse_error(&p.errs, "interface does not have a name");
    }
    hop_whitespace(&p);

    Interface interface;
    if (!parse_interface(&p, &interface)) {
        cstr_drop(&name);
        return parse_fail(&p);
    }

    *ret = (InterfaceStatement) {
        .name = name,
        .type = type_interface(interface),
    };
    return parse_success(&p);
}

static bool parse_function_args(
    Parser* parent,
    VecFunctionArg* args_ret,
    CallableType* callable_ret
) {
    Parser p = parser(parent);
    bool is_definitely_function = false;

    if (!hop_char(&p, '(')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    *args_ret = vec_function_arg_init();
    *callable_ret = (CallableType){0};

    while (*p.pos && *p.pos != ')') {
        if (hop_char(&p, ',')) {
            error_ctx_error(&p.errs, span_from_previous_char(&p), 1138, "Parameter declaration expected.");
        } else {
            NameInitData name_init = {0};
            const char* before_param = p.pos;

            if (!parse_name_init(&p, &name_init)) {
                vec_function_arg_drop(args_ret);
                callable_type_drop(callable_ret);
                return parse_fail(&p);
            }

            if (vec_function_arg_empty(args_ret) && name_init.type_explicit) {
                is_definitely_function = true;
            }

            vec_function_arg_push(args_ret, (FunctionArg){
                .name = cstr_clone(name_init.name),
                .name_span = name_init.name_span,
                .value_explicit = name_init.value_explicit,
                .value = name_init.value,
            });

            vec_callable_type_arg_push(&callable_ret->args, (CallableTypeArg){
                .name = name_init.name,
                .type_explicit = name_init.type_explicit,
                .type = name_init.type,
            });

            if (p.pos == before_param) {
                if (is_definitely_function) {
                    // parse_name_init already added error
                    hop_token(&p);
                } else {
                    vec_function_arg_drop(args_ret);
                    callable_type_drop(callable_ret);
                    return parse_fail(&p);
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
        vec_function_arg_drop(args_ret);
        callable_type_drop(callable_ret);
        *args_ret = vec_function_arg_init();
        *callable_ret = (CallableType){0};
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "')' expected.");
        return parse_success(&p);
    }

    Parser type_p = parser(&p);
    hop_whitespace(&type_p);
    if (hop_start(&type_p, ":")) {
        hop_whitespace(&type_p);
        if (parse_type(&type_p, TYPE_PRECEDENCE_NULL, &callable_ret->ret)) {
            callable_ret->ret_explicit = true;
        } else {
            Span span = hop_token_get_span(&type_p);
            error_ctx_error(&p.errs, span, 1110, "Type expected.");
        }
        parse_success(&type_p);
    }

    return parse_success(&p);
}

static bool parse_function_generics_args_and_return_type(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    VecGenericItem generics;
    CallableType callable;
    *ret = (FunctionImpl){0};

    if (parse_generic_decl_list(&p, &generics)) {
        hop_whitespace(&p);
    } else {
        generics = (VecGenericItem){0};
    }

    Anchor args_a = span_start(&p);

    if (!parse_function_args(&p, &ret->args, &callable)) {
        vec_generic_item_drop(&generics);
        return parse_fail(&p);
    }

    ret->args_span = span_end(args_a);
    ret->type = type_callable(c_new(CallableType, callable));
    if (!vec_generic_item_empty(&generics)) {
        ret->type = type_generic(c_new(Generic, {
            .items = generics,
            .inner = ret->type,
        }));
    }

    return parse_success(&p);
}

static bool parse_arrow_function(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    FAIL_CACHE_HOOK(&p, FAIL_CACHE_ARROW_FUNCTION);
    Anchor a = span_start(&p);
    bool is_definitely_function = false;
    cstr lone_arg_name;
    Span lone_arg_span;

    if (parse_function_generics_args_and_return_type(&p, ret)) {

        CallableType* callable = function_impl_callable_type(ret);
        is_definitely_function =
            !vec_callable_type_arg_empty(&callable->args) &&
            vec_callable_type_arg_at(&callable->args, 0)->type_explicit;

    } else if (parse_identifier(&p, &lone_arg_name, &lone_arg_span)) {
        *ret = (FunctionImpl){0};

        ret->args_span = lone_arg_span;
        vec_function_arg_push(&ret->args, (FunctionArg){
            .name = cstr_clone(lone_arg_name),
            .name_span = lone_arg_span,
            .value_explicit = false,
            .value = expr_undefined(span_from_pos(&p)),
        });
        CallableType* callable = function_impl_callable_type(ret);
        vec_callable_type_arg_push(&callable->args, (CallableTypeArg){
            .name = lone_arg_name,
            .type_explicit = false,
            .type = type_any,
        });

    } else {
        return parse_fail(&p);
    }

    bool newline_after_args = hop_whitespace(&p) == WHITESPACE_NEWLINE;

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
        if (!parse_expr(&p, &expr)) {
            error_ctx_error(&p.errs, span_from_token(&p), 1109, "Expression expected.");
            expr = expr_implicit_any(span_from_pos(&p));
            p.pos = span_end_ptr(&ret->arrow_span);
        }
        ret->body = (Block){
            .statements = vec_statement_init(),
        };
        vec_statement_push(&ret->body.statements, (Statement){
            .tag = STATEMENT_RETURN_EXPR,
            .expr = expr,
        });
    }

    ret->full_span = span_end(a);
    return parse_success(&p);
}

static bool parse_non_arrow_function(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    if (!parse_function_generics_args_and_return_type(&p, ret)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_block(&p, false, &ret->body)) {
        error_ctx_error(&p.errs, span_from_token(&p), 1144, "'{' or ';' expected.");
    }

    ret->full_span = span_end(a);
    ret->name = cstr_null;
    ret->name_span = span_null;
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

static bool parse_object_item(Parser* parent, void* data) {
    Parser p = parser(parent);
    VecObjectExprItem* ret = data;
    cstr name = cstr_null;
    Value name_value;

    if (parse_identifier(&p, &name, nullptr)) {
    } else if (parse_literal(&p, &name_value)) {
        if (name_value.tag == VALUE_STRING) {
            name = name_value.string;
        } else {
            name = cstr_null;
            cstr_append_value_js(&name, &name_value);
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
    if (!parse_expr(&p, &value)) {
        error_ctx_parse_error(&p.errs, "bad expression in object");
        value = expr_undefined(span_from_pos(&p));
    }

    vec_object_expr_item_push(ret, (ObjectExprItem){
        .name = name,
        .value = value,
    });
    return parse_success(&p);
}

static bool parse_object(Parser* parent, ObjectExpr* ret) {
    Parser p = parser(parent);
    VecObjectExprItem items = {0};

    if (!parse_sequence(&p, "{", SEQUENCE_SEP_COMMA, '}', parse_object_item, &items)) {
        vec_object_expr_item_drop(&items);
        return parse_fail(&p);
    }

    *ret = items;
    return parse_success(&p);
}

static bool parse_array(Parser* parent, VecExpr* ret) {
    Parser p = parser(parent);
    *ret = vec_expr_init();

    if (!hop_start(&p, "[")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    while (*p.pos && *p.pos != ']') {
        Expr expr;
        if (parse_expr(&p, &expr)) {
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
static bool parse_class_item(Parser* parent, ClassItem* ret) {
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

        if (cstr_empty(&name)) {
        } else if (cstr_equals(&name, "readonly")) {
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

            if (span_valid(&async_span) && !modifier_error) {
                modifier_error = MUST_PRECEDE_ASYNC;
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
                if (span_valid(&access_span)) {
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
        } else {
            if (!modifier_error) {
                modifier_error = UNEXPECTED_IDENT;
                modifier_error_span = name_span;
            }
        }

        cstr_drop(&name);
        name = next_name;
        name_span = next_name_span;

        if (hop_whitespace(&p) == WHITESPACE_NEWLINE) {
            break;
        }

        if (getter_setter) {
            break;
        }
    }

    if (cstr_equals(&name, "constructor")) {
        ret->tag = CLASS_ITEM_CONSTRUCTOR;
        name_span = span_end(a);
        if (!parse_non_arrow_function(&p, &ret->constructor.func)) {
            error_ctx_error(&p.errs, span_from_token(&p), TS_CODE_NONE, "constructor expected");
            ret->constructor.func = (FunctionImpl){0};
        }
        ret->constructor.name_span = name_span;
        ret->constructor.func.full_span = span_end(a);
        if (span_valid(&readonly_span)) {
            modifier_error = INVALID_READONLY;
            modifier_error_span = readonly_span;
        }
    } else if (cstr_equals(&name, "static") && parse_block(&p, false, &ret->static_block)) {
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
        if (parse_non_arrow_function(&p, &ret->member.func)) {
            ret->tag = CLASS_ITEM_MEMBER;
            ret->member.name = name;
            ret->member.name_span = name_span;
            if (span_valid(&readonly_span)) {
                modifier_error = INVALID_READONLY;
                modifier_error_span = readonly_span;
            }
        } else if (parse_name_init_with_name(&p, name, name_span, &ret->field.data)) {
            ret->tag = CLASS_ITEM_FIELD;
            ret->field.readonly = span_valid(&readonly_span);
        } else {
            error_ctx_error(&p.errs, span_from_token(&p), TS_CODE_NONE, "class member expected");
            ret->member = (typeof(ret->member)){0};
        }
    } else {
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
                "'%.*s' modifier must precede 'readonly' modifier.", c_SV(span_sv(&modifier_error_span))
            );
            break;

        case MUST_PRECEDE_STATIC:
            error_ctx_error(
                &parent->errs,
                modifier_error_span,
                1029,
                "'%.*s' modifier must precede 'static' modifier.", c_SV(span_sv(&modifier_error_span))
            );
            break;

        case MUST_PRECEDE_ASYNC:
            error_ctx_error(
                &parent->errs,
                modifier_error_span,
                1029,
                "'%.*s' modifier must precede 'async' modifier.", c_SV(span_sv(&modifier_error_span))
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
    }

    return parse_success(&p);
}

static bool parse_class_decl(Parser* parent, ClassDecl* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    if (!hop_start_and_required_whitespace(&p, "class")) {
        return parse_fail(&p);
    }

    *ret = (ClassDecl){
        .interface = type_any,
    };
    if (!parse_identifier(&p, &ret->name, &ret->name_span)) {
        ret->name = cstr_null;
        ret->name_span = span_null;
    }
    hop_whitespace(&p);

    if (!hop_char(&p, '{')) {
        error_ctx_error(&p.errs, span_from_token(&p), 1005, "'{' expected.");
        ret->full_span = span_end(a);
        return parse_success(&p);
    }
    hop_whitespace(&p);

    ClassItem item;
    while (*p.pos && *p.pos != '}') {
        if (!parse_class_item(&p, &item)) {
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
        if (parse_expr(&p, &arg)) {
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

static bool parse_dot_prop(Parser* parent, cstr* prop, Span* span) {
    Parser p = parser(parent);

    if (!hop_char(&p, '.')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_identifier(&p, prop, span)) {
        error_ctx_parse_error(&p.errs, "expected property name");
        *prop = cstr_lit("INVALID_PROPERTY");
        *span = span_null;
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

static bool parse_binary_op_expr(Parser* parent, BinaryOp* op, Expr* right) {
    Parser p = parser(parent);

    if (!parse_binary_op(&p, op)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, right)) {
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

static bool parse_left_unary_op_expr(Parser* parent, Expr* ret) {
    Parser p = parser(parent);

    Anchor a = span_start(&p);
    UnaryOp op = 0;
    if (!parse_left_unary_op(&p, &op)) {
        return parse_fail(&p);
    }
    Span op_span = span_end(a);
    hop_whitespace(&p);

    Expr expr;
    if (!parse_expr(&p, &expr)) {
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

static bool parse_expr_right_side(Parser* parent, Expr* left_and_ret) {
    Parser p = parser(parent);
    Anchor a = span_starting_at(&p, left_and_ret->span);

    bool hit_newline = hop_whitespace(&p) == WHITESPACE_NEWLINE;

    union {
        VecExpr args;
        struct {
            cstr str;
            Span span;
        } prop;
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

    if (parse_dot_prop(&p, &parsed.prop.str, &parsed.prop.span)) {
        *left_and_ret = expr_dot_prop(
            span_end(a),
            c_new(DotPropExpr, {
                .expr = *left_and_ret,
                .prop = parsed.prop.str,
                .prop_span = parsed.prop.span,
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
            })
        );
        return parse_success(&p);
    }

    if (left_and_ret->tag == EXPR_IDENTIFIER && *p.pos == '=' && *(p.pos + 1) == '>') {
        Parser sub_p = parser(&p);
        sub_p.pos = span_start_ptr(&left_and_ret->span);

        if (parse_arrow_function(&sub_p, &parsed.function)) {
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

    if (parse_binary_op_expr(&p, &parsed.binary.op, &parsed.binary.expr)) {
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

        if (!parse_expr(&p, &expr)) {
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

static bool parse_expr(Parser* parent, Expr* ret) {
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
        if (!parse_expr(&p, ret)) {
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
    } else if (parse_left_unary_op_expr(&p, ret)) {
    } else if (parse_function_keyword_function(&p, &parsed.function)) {
        *ret = expr_function(span_end(a), c_new(FunctionImpl, parsed.function));
    } else if (parse_identifier(&p, &parsed.name, nullptr)) {
        *ret = expr_identifier(span_end(a), parsed.name);
    } else if (parse_arrow_function(&p, &parsed.function)) {
        *ret = expr_function(span_end(a), c_new(FunctionImpl, parsed.function));
    } else if (hop_char(&p, '(')) {
        if (!parse_expr(&p, ret)) {
            return parse_fail(&p);
        }
        parse_comma_ops_right_side(&p, ret);
        hop_whitespace(&p);
        if (!hop_char(&p, ')')) {
            Span span = hop_token_get_span(&p);
            error_ctx_error(&p.errs, span, 1005, "')' expected.");
        }
        ret->peren_depth++;
    } else {
        return parse_fail(&p);
    }

    ret->span = span_end(a);

    while (parse_expr_right_side(&p, ret)) {}

    return parse_success(&p);
}

static bool parse_name_init_with_name(Parser* parent, cstr name, Span name_span, NameInitData* ret) {
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
        }
        hop_whitespace(&p);
    }
    Span type_span = span_end(type_a);

    Expr value;
    bool value_explicit = false;
    if (hop_start(&p, "=")) {
        hop_whitespace(&p);
        if (parse_expr(&p, &value)) {
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

static bool parse_name_init(Parser* parent, NameInitData* ret) {
    Parser p = parser(parent);

    cstr name = cstr_null;
    Span name_span;
    if (!parse_identifier(&p, &name, &name_span)) {
        name_span = span_from_pos(&p);
        error_ctx_error(&p.errs, span_from_token(&p), 1003, "Identifier expected.");
    }
    hop_whitespace(&p);

    if (!parse_name_init_with_name(&p, name, name_span, ret)) {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_var_init(Parser* parent, VariableStatement* ret) {
    Parser p = parser(parent);

    typeof(ret->keyword) keyword;
    if (hop_start_and_required_whitespace(&p, "var")) {
        keyword = VAR_INIT_VAR;
    } else if (hop_start_and_required_whitespace(&p, "let")) {
        keyword = VAR_INIT_LET;
    } else if (hop_start_and_required_whitespace(&p, "const")) {
        keyword = VAR_INIT_CONST;
    } else {
        return parse_fail(&p);
    }

    Span comma_span = span_null;
    NameInitData data;
    VecNameInitData vars = vec_name_init_data_init();
    while (true) {
        Parser sub = parser(&p);
        if (!parse_name_init(&sub, &data)) {
            error_ctx_error(&p.errs, span_from_token(&p), 1134, "Variable declaration expected.");
            parse_fail(&sub);
            break;
        }
        if (cstr_empty(&data.name) && span_valid(&comma_span)) {
            error_ctx_error(&p.errs, comma_span, 1009, "Trailing comma not allowed.");
            name_init_data_drop(&data);
            parse_fail(&sub);
            p.pos = span_end_ptr(&comma_span);
            break;
        }
        parse_success(&sub);
        vec_name_init_data_push(&vars, data);
        const char* end_of_name_init = p.pos;
        hop_whitespace(&p);

        if (!hop_char(&p, ',')) {
            p.pos = end_of_name_init;
            break;
        }
        comma_span = span_from_previous_char(&p);
        hop_whitespace(&p);
    }

    *ret = (VariableStatement) {
        .keyword = keyword,
        .vars = vars,
    };
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

    if (!parse_expr(&p, condition)) {
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
        hop_whitespace(&p);

        vec_expr_push(&conditions, condition);
        vec_statement_push(&statements, statement);
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

    bool is_for_of = false;
    Statement statement;
    Expr left_expr;
    Expr right_expr;
    Statement body;
    bool keyword_is_in = false;

    if (parse_var_init(&p, &statement.decl.variable)) {
        statement.decl.exported = DECL_EXPORTED_NO;
        statement.tag = STATEMENT_DECL_VARIABLE;
    } else if (parse_expr(&p, &statement.expr)) {
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

        if (!parse_expr(&p, &left_expr)) {
            left_expr = expr_literal(span_from_pos(&p), value_boolean(true));
        }
        hop_whitespace(&p);

        if (!hop_start(&p, ";")) {
            // Unclear why this is the error message in this case
            error_ctx_error(&p.errs, hop_token_get_span(&p), 1005, "',' expected.");
            hop_token(&p);
        }
        hop_whitespace(&p);

        if (!parse_expr(&p, &right_expr)) {
            right_expr = expr_undefined(span_from_pos(&p));
        }
        hop_whitespace(&p);
    } else if ((statement.tag == STATEMENT_EXPR || statement.tag == STATEMENT_DECL_VARIABLE) && (
        hop_start_if_ends_on_word_break(&p, "of") ||
        (hop_start_if_ends_on_word_break(&p, "in") && (keyword_is_in = true)) // control flow is my passion
    )) {
        is_for_of = true;

        hop_whitespace(&p);

        if (!parse_expr(&p, &right_expr)) {
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

    if (!hop_start(&p, ")")) {
        error_ctx_error(&p.errs, hop_token_get_span(&p), 1005, "')' expected.");
        while (*p.pos && *p.pos != '\n' && *p.pos != ';' && *p.pos != ')') {
            hop_token(&p);
            if (hop_whitespace(&p) == WHITESPACE_NEWLINE) {
                break;
            }
        }
    }
    hop_whitespace(&p);

    if (!parse_statement(&p, false, &body)) {
        error_ctx_error(&p.errs, span_from_token(&p), 1009, "Expression expected.");
        body = statement_noop;
    }

    if (is_for_of) {
        switch (statement.tag) {
            case STATEMENT_DECL_VARIABLE:
                *ret = (Statement){
                    .tag = STATEMENT_FOR_EACH_LOOP,
                    .for_each_loop = c_new(ForEachLoopStatement, {
                        .variable = {
                            .tag = FOR_EACH_LOOP_STATEMENT,
                            .statement = statement.decl.variable,
                        },
                        .iterable = right_expr,
                        .body = body,
                    })
                };
                break;

            case STATEMENT_EXPR:
                *ret = (Statement){
                    .tag = STATEMENT_FOR_EACH_LOOP,
                    .for_each_loop = c_new(ForEachLoopStatement, {
                        .keyword = keyword_is_in ? FOR_EACH_LOOP_IN : FOR_EACH_LOOP_OF,
                        .variable = {
                            .tag = FOR_EACH_LOOP_EXPR,
                            .expr = statement.expr,
                        },
                        .iterable = right_expr,
                        .body = body,
                    })
                };
                break;

            default: PANIC("Invalid statement type for for-of loop");
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
    if (!parse_expr(&p, &condition)) {
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

static bool parse_return(Parser* parent, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    if (!hop_start_if_ends_on_word_break(&p, "return")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, ret)) {
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
        InterfaceStatement interface;
        Expr expr;
    } parsed;

    if (exported != DECL_EXPORTED_YES_DEFAULT && parse_var_init(&p, &parsed.variable)) {
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
    } else if (parse_named_interface(&p, &parsed.interface)) {
        *ret = (Statement){
            .tag = STATEMENT_DECL_INTERFACE,
            .decl.exported = exported,
            .decl.interface = parsed.interface,
        };
    } else if (exported == DECL_EXPORTED_YES_DEFAULT && parse_expr(&p, &parsed.expr)) {
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
            default: break;
        }
    } else if (parse_statement_decl(&p, DECL_EXPORTED_NO, ret)) {
    } else if (parse_block(&p, false, &ret->block)) {
        ret->tag = STATEMENT_BLOCK;
    } else if (parse_if_else(&p, &ret->if_else)) {
        ret->tag = STATEMENT_IF_ELSE;
    } else if (parse_for_loop_statement(&p, ret)) {
    } else if (parse_while_loop_statement(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "break")) {
        ret->tag = STATEMENT_BREAK;
        ret->break_span = span_end(a);
    } else if (parse_return(&p, &ret->expr)) {
        ret->tag = STATEMENT_RETURN_EXPR;
    } else if (parse_expr(&p, &ret->expr)) {
        ret->tag = STATEMENT_EXPR;
        parse_comma_ops_right_side(&p, &ret->expr);
    } else {
        return parse_fail(&p);
    }

    reset_fail_cache(&p);
    return parse_success(&p);
}

static bool parse_block_inner(Parser* parent, bool toplevel, Block* ret) {
    Parser p = parser(parent);

    VecStatement statements = {0};
    while(*p.pos && *p.pos != '}') {
        Statement statement = {0};
        if (*p.pos == ';') {
            p.pos++;
        } else if (parse_statement(&p, toplevel, &statement)) {
            vec_statement_push(&statements, statement);
            if (hop_whitespace(&p) == WHITESPACE_NEWLINE ||
                statement.tag == STATEMENT_NOOP ||
                !*p.pos ||
                *p.pos == '}'
            ) {
            } else if (*p.pos == ';') {
                p.pos++;
            } else if (*p.pos) {
                error_ctx_error(&p.errs, hop_token_get_span(&p), 1005, "';' expected.");
            }
        } else if (*p.pos == '\\') {
            error_ctx_error(&p.errs, span_from_pos(&p), 1127, "Invalid character.");
            p.pos++;
        } else {
            error_ctx_error(&p.errs, hop_token_get_span(&p), 1128, "Declaration or statement expected.");
        }
        hop_whitespace(&p);
    }

    *ret = (Block){
        .statements = statements,
    };
    return parse_success(&p);
}

static bool parse_block(Parser* parent, bool toplevel, Block* ret) {
    Parser p = parser(parent);

    if (!hop_char(&p, '{')) return parse_fail(&p);
    hop_whitespace(&p);

    if (!parse_block_inner(&p, toplevel, ret)) {
        *ret = (Block){
            .statements = vec_statement_init(),
        };
    }
    hop_whitespace(&p);

    if (!hop_char(&p, '}')) {
        error_ctx_parse_error(&p.errs, "expected '}'");
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
        .errs = {0},
    };

    if (p.pos[0] == '\xEF' && p.pos[1] == '\xBB' && p.pos[2] == '\xBF') {
        // Skip Byte Order Mark nonsense
        p.pos += 3;
    }

    hop_whitespace(&p);

    Block scope;
    parse_block_inner(&p, true, &scope);
    if (*p.pos) {
        error_ctx_parse_error(&p.errs, "unexpected '}'");
    }

    parser_shared_drop(&shared);

    error_ctx_append_consume(errs, p.errs);
    return scope;
}
