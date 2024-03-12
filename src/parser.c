#include "parser.h"
#include "block.h"
#include "interface.h"
#include "interface_member.h"
#include "statement.h"
#include "ternary_type.h"
#include "generic.h"
#include "callable_type.h"
#include "as_type_expr.h"
#include "extends_conditional.h"
#include "type.h"
#include "error.h"

typedef struct Parser Parser;
typedef struct Parser {
    Parser* parent; // the parser this parser was created from (and will merge it's result into on success)
    const char* const file_start; // needed to build spans
    const char* const start; // position this parser started at
    const char* pos; // current position in the code
    ErrorCtx errs;
} Parser;

static inline Parser parser(Parser* parent) {
    return (Parser){
        .parent = parent,
        .file_start = parent->file_start,
        .start = parent->pos,
        .pos = parent->pos,
        .errs = {0},
    };
}

static inline Span span_from_parsed(Parser* self) {
    return span_make(self->file_start, self->start - self->file_start, self->pos - self->start);
}

static inline Span span_from_pos(Parser* self) {
    return span_make(self->file_start, self->pos - self->file_start, 0);
}

/// Must not be used on the root parser, always returns true, drops parser
static inline bool parse_success(Parser* self) {
    self->parent->pos = self->pos;
    ErrorCtx_append_consume(&self->parent->errs, self->errs);
    return true;
}

/// Must not be used on the root parser, always returns false, drops parser
static inline bool parse_fail(Parser* self) {
    ErrorCtx_drop(&self->errs);
    return false;
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
        .start = parser->file_start + start.offset,
    };
}

Span span_end(Anchor spanner) {
    const char* file_start = spanner.parser->file_start;
    return span_make(file_start, spanner.start - file_start, spanner.parser->pos - spanner.start);
}

static bool parse_block(Parser* parent, Block* ret);
static bool parse_interface(Parser* parent, Interface* ret);
static bool parse_name_init(Parser* parent, NameInitData* ret);
static bool parse_statement(Parser* parent, Statement* ret);
static bool parse_type(Parser* parent, Type* ret);
static bool parse_type_compat_literal(Parser* parent, Value* ret);
static bool parse_expr(Parser* parent, Expr* ret);

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
    const char* start = parent->pos;
    Anchor a = span_start(parent);
    if (is_identifier_start_char(*parent->pos)) {
        parent->pos++;
        while (is_identifier_char(*parent->pos)) parent->pos++;
        *ret = cstr_from_n(start, parent->pos - start);
        if (span) {
            *span = span_end(a);
        }
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
                    case 0: return WHITESPACE_NONE;
                    default: break;
                }
                pos++;
            }
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
                break;

            case '\n':
                ret = WHITESPACE_NEWLINE;
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
        parent->pos++;
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
            ErrorCtx_parse_error(&p.errs, "invalid item in sequence");
            while (*p.pos != end && char_is_sep(sep, *p.pos)) {
                p.pos++;
            }
        }
        if (p.pos == before_callback) {
            p.pos++;
        }
        hop_whitespace(&p);
        if (sep_required(sep) && !char_is_sep(sep, *p.pos) && *p.pos != end) {
            ErrorCtx_parse_error(&p.errs, "expected separator or end of sequence");
        }
        bool first_sep = true;
        while (char_is_sep(sep, *p.pos)) {
            p.pos++;
            hop_whitespace(&p);
            if (!first_sep) {
                ErrorCtx_parse_error(&p.errs, "expected item before separator");
            }
            first_sep = false;
        }
    }
    if (*p.pos == end) {
        p.pos++;
    } else {
        ErrorCtx_parse_error(&p.errs, "expected end of sequence");
    }
    return parse_success(&p);
}

static bool parse_generic_decl_item(Parser* parent, void* data) {
    Parser p = parser(parent);
    vec_GenericItem* ret = data;
    cstr name = cstr_null;

    if (!parse_identifier(&p, &name, nullptr)) {
        ErrorCtx_parse_error(&p.errs, "missing generic name");
    }
    hop_whitespace(&p);

    Type extends = Type_any;
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        hop_whitespace(&p);
        parse_type(&p, &extends);
        hop_whitespace(&p);
    }

    Type fallback = Type_any;
    bool fallback_explicit = false;
    if (hop_start(&p, "=")) {
        hop_whitespace(&p);
        if (parse_type(&p, &fallback)) {
            fallback_explicit = true;
        }
    }

    vec_GenericItem_push(ret, (GenericItem){
        .name = name,
        .extends = extends,
        .fallback_explicit = fallback_explicit,
        .fallback = fallback,
    });
    return parse_success(&p);
}

static bool parse_generic_decl_list(Parser* parent, vec_GenericItem* ret) {
    return parse_sequence(parent, "<", SEQUENCE_SEP_COMMA, '>', parse_generic_decl_item, ret);
}

static bool parse_ternary_type(Parser* parent, Type first, TernaryType* ret) {
    Parser p = parser(parent);
    Type maybeSuper = {0};
    Type trueType = {0};
    Type falseType = {0};

    if (!hop_start_if_ends_on_word_break(&p, "extends")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    parse_type(&p, &maybeSuper);
    hop_whitespace(&p);

    if (!hop_char(&p, '?')) {
        ErrorCtx_parse_error(&p.errs, "expected '?'");
    }
    hop_whitespace(&p);

    parse_type(&p, &trueType);
    hop_whitespace(&p);

    if (!hop_char(&p, ':')) {
        ErrorCtx_parse_error(&p.errs, "expected ':'");
    }
    hop_whitespace(&p);

    parse_type(&p, &falseType);

    *ret = (TernaryType){
        .conditional = (ExtendsConditional){
            .maybeSub = first,
            .maybeSuper = maybeSuper,
        },
        .trueType = trueType,
        .falseType = falseType,
    };
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
        ErrorCtx_parse_error(&p.errs, "initializer not allowed in function type");
        Expr_drop(&name_init.value);
    }

    vec_CallableTypeArg_push(&ret->args, (CallableTypeArg){
        .name = name_init.name,
        .type_explicit = name_init.type_explicit,
        .type = name_init.type,
    });

    return parse_success(&p);
}

static bool parse_callable_type(Parser* parent, Type* ret) {
    Parser p = parser(parent);
    vec_GenericItem generics = {0};
    CallableType callable = {0};

    if (parse_generic_decl_list(&p, &generics)) {
        hop_whitespace(&p);
    }

    if (!parse_sequence(&p, "(", SEQUENCE_SEP_COMMA, ')', parse_callable_type_arg, &callable)) {
        vec_GenericItem_drop(&generics);
        CallableType_drop(&callable);
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!hop_start(&p, "=>")) {
        vec_GenericItem_drop(&generics);
        CallableType_drop(&callable);
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_type(&p, &callable.ret)) {
        ErrorCtx_parse_error(&p.errs, "invalid return type");
    }

    *ret = Type_callable(c_new(CallableType, callable));
    if (!vec_GenericItem_empty(&generics)) {
        *ret = Type_generic(c_new(Generic, {
            .items = generics,
            .inner = *ret,
        }));
    }
    return parse_success(&p);
}

static bool parse_type(Parser* parent, Type* ret) {
    Parser p = parser(parent);
    Span span;
    Type type;
    cstr name;

    if (hop_start_if_ends_on_word_break(&p, "any")) {
        type = Type_any;
    } else if (hop_start_if_ends_on_word_break(&p, "unkown")) {
        type = Type_unkown;
    } else if (hop_start_if_ends_on_word_break(&p, "never")) {
        type = Type_never;
    } else if (hop_start_if_ends_on_word_break(&p, "void")) {
        type = Type_void;
    } else if (hop_start_if_ends_on_word_break(&p, "boolean")) {
        type = Type_boolean;
    } else if (hop_start_if_ends_on_word_break(&p, "number")) {
        type = Type_number;
    } else if (hop_start_if_ends_on_word_break(&p, "string")) {
        type = Type_string;
    } else if (parse_type_compat_literal(&p, &type.value)) {
        type.tag = TYPE_VALUE;
    } else if (parse_identifier(&p, &name, &span)) {
        type = Type_identifier(name, nullptr, 0, 0, span);
    } else if (parse_callable_type(&p, &type)) {
        // Tag set to either callable or generic
    } else if (parse_interface(&p, &type.interface)) {
        type.tag = TYPE_INTERFACE;
    } else {
        ErrorCtx_parse_error(&p.errs, "did not find type");
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    TernaryType ternary;
    if (parse_ternary_type(&p, type, &ternary)) {
        *ret = Type_ternary(c_new(TernaryType, ternary));
        return parse_success(&p);
    } else {
        *ret = type;
        return parse_success(&p);
    }
}

static bool parse_interface(Parser* parent, Interface* ret) {
    Parser p = parser(parent);

    if (!hop_char(&p, '{')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    vec_InterfaceMember members = {0};
    const char* prev_s = nullptr;

    while (*p.pos && *p.pos != '}' && p.pos != prev_s) {
        prev_s = p.pos;
        hop_whitespace(&p);

        cstr name = cstr_null;
        if (!parse_identifier(&p, &name, nullptr)) {
            ErrorCtx_parse_error(&p.errs, "expected interface member name");
        }
        hop_whitespace(&p);

        if (!hop_char(&p, ':')) {
            ErrorCtx_parse_error(&p.errs, "expected ':'");
        }
        hop_whitespace(&p);

        Type type;
        parse_type(&p, &type);
        vec_InterfaceMember_push(&members, InterfaceMember_make(name, type));
        hop_whitespace(&p);

        // Typescript accepts EITHER of these in interfaces (or nothing at all). You can MIX them!
        // it also cares about newlines but we can sort that out later
        (void)(hop_char(&p, ',') || hop_char(&p, ';'));
        hop_whitespace(&p);
    }

    if (!hop_char(&p, '}')) {
        ErrorCtx_parse_error(&p.errs, "expected '}'");
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
        ErrorCtx_parse_error(&p.errs, "interface does not have a name");
    }
    hop_whitespace(&p);

    Interface interface;
    if (!parse_interface(&p, &interface)) {
        cstr_drop(&name);
        return parse_fail(&p);
    }

    *ret = (InterfaceStatement) {
        .name = name,
        .type = Type_interface(interface),
    };
    return parse_success(&p);
}

static CallableType* function_impl_callable(FunctionImpl* function) {
    if (function->type.tag == 0) {
        function->type = Type_callable(c_new(CallableType, {0}));
    } else if (function->type.tag != TYPE_CALLABLE) {
        PANIC("function type is not default or callable");
    }
    return function->type.callable;
}

static bool parse_function_arg(Parser* parent, void* data) {
    Parser p = parser(parent);
    FunctionImpl* ret = data;
    CallableType* callable = function_impl_callable(ret);

    NameInitData name_init = {0};
    if (!parse_name_init(&p, &name_init)) {
        return parse_fail(&p);
    }

    vec_FunctionArg_push(&ret->args, (FunctionArg){
        .name = cstr_clone(name_init.name),
        .name_span = name_init.name_span,
        .value_explicit = name_init.value_explicit,
        .value = name_init.value,
    });

    vec_CallableTypeArg_push(&callable->args, (CallableTypeArg){
        .name = name_init.name,
        .type_explicit = name_init.type_explicit,
        .type = name_init.type,
    });

    return parse_success(&p);
}

static bool parse_function(Parser* parent, bool parse_args, bool require_arrow, FunctionImpl* ret) {
    Parser p = parser(parent);
    vec_GenericItem generics = {0};
    *ret = (FunctionImpl){0};

    if (parse_args) {
        if (parse_generic_decl_list(&p, &generics)) {
            hop_whitespace(&p);
        }

        Anchor args_a = span_start(&p);
        if (!parse_sequence(&p, "(", SEQUENCE_SEP_COMMA, ')', parse_function_arg, ret)) {
            vec_GenericItem_drop(&generics);
            FunctionImpl_drop(ret);
            return parse_fail(&p);
        }
        ret->args_span = span_end(args_a);
        hop_whitespace(&p);
    }

    if (hop_start(&p, ":")) {
        hop_whitespace(&p);
        CallableType* callable = function_impl_callable(ret);
        if (parse_type(&p, &callable->ret)) {
            callable->ret_explicit = true;
        } else {
            ErrorCtx_parse_error(&p.errs, "invalid return type");
        }
        hop_whitespace(&p);
    }

    if (require_arrow) {
        if (!hop_start(&p, "=>")) {
            vec_GenericItem_drop(&generics);
            FunctionImpl_drop(ret);
            return parse_fail(&p);
        }
        hop_whitespace(&p);

        Expr expr = {0};
        if (parse_block(&p, &ret->body)) {
        } else if (parse_expr(&p, &expr)) {
            ret->body = (Block){
                .statements = vec_Statement_init(),
            };
            vec_Statement_push(&ret->body.statements, (Statement){
                .tag = STATEMENT_RETURN,
                .expr = expr,
            });
        } else {
            ErrorCtx_parse_error(&p.errs, "invalid arrow function body");
        }
    } else {
        if (!parse_block(&p, &ret->body)) {
            ErrorCtx_parse_error(&p.errs, "invalid function body");
        }
    }

    function_impl_callable(ret); // Make sure we create a callable type in case we haven't already

    if (!vec_GenericItem_empty(&generics)) {
        ret->type = Type_generic(c_new(Generic, {
            .items = generics,
            .inner = ret->type,
        }));
    }
    return parse_success(&p);
}

static bool parse_object_item(Parser* parent, void* data) {
    Parser p = parser(parent);
    vec_ObjectExprItem* ret = data;
    cstr name;

    if (!parse_identifier(&p, &name, nullptr)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!hop_start(&p, ":")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Expr value;
    if (!parse_expr(&p, &value)) {
        ErrorCtx_parse_error(&p.errs, "bad expression in object");
    }

    vec_ObjectExprItem_push(ret, (ObjectExprItem){
        .name = name,
        .value = value,
    });
    return parse_success(&p);
}

static bool parse_object(Parser* parent, ObjectExpr* ret) {
    Parser p = parser(parent);
    vec_ObjectExprItem items = {0};

    if (!parse_sequence(&p, "{", SEQUENCE_SEP_COMMA, '}', parse_object_item, &items)) {
        vec_ObjectExprItem_drop(&items);
        return parse_fail(&p);
    }

    *ret = items;
    return parse_success(&p);
}

static bool parse_call_args(Parser* parent, vec_Expr* args) {
    Parser p = parser(parent);

    if (!hop_start(&p, "(")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    *args = vec_Expr_init();
    while (*p.pos && *p.pos != ')') {
        Expr arg = {0};
        if (parse_expr(&p, &arg)) {
            vec_Expr_push(args, arg);
        } else {
            ErrorCtx_parse_error(&p.errs, "invald expression in call");
            while (*p.pos && *p.pos != ')' && *p.pos != ',') {
                p.pos++;
            }
        }
        hop_whitespace(&p);
        if (*p.pos != ',' && *p.pos != ')') {
            ErrorCtx_parse_error(&p.errs, "expected ',' or ')' in call");
        }
        bool first_comma = true;
        while (*p.pos == ',') {
            p.pos++;
            hop_whitespace(&p);
            if (!first_comma) {
                ErrorCtx_parse_error(&p.errs, "expected argument before ','");
            }
            first_comma = false;
        }
    }
    if (*p.pos == ')') {
        p.pos++;
    } else {
        ErrorCtx_parse_error(&p.errs, "expected ')' at end of call");
    }
    return parse_success(&p);
}

static bool parse_right_unary_op_expr(Parser* parent, UnaryOp* op) {
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

static bool parse_dot_prop(Parser* parent, cstr* prop) {
    Parser p = parser(parent);

    if (!hop_char(&p, '.')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_identifier(&p, prop, nullptr)) {
        ErrorCtx_parse_error(&p.errs, "expected property name");
        *prop = cstr_lit("INVALID_PROPERTY");
    }

    return parse_success(&p);
}

static bool parse_as_type(Parser* parent, Type* ret) {
    Parser p = parser(parent);

    if (!hop_start(&p, "as")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_type(&p, ret)) {
        ErrorCtx_error(&p.errs, span_from_pos(&p), 1110, "Type expected.");
        *ret = Type_any;
    }

    return parse_success(&p);
}

static bool parse_binary_op_expr(Parser* parent, BinaryOp* op, Expr* right) {
    Parser p = parser(parent);

    if (hop_start(&p, "+")) {
        if (*p.pos == '+') {
            return parse_fail(&p);
        }
        *op = BINARY_OP_PLUS;
    } else if (hop_start(&p, "-")) {
        if (*p.pos == '-') {
            return parse_fail(&p);
        }
        *op = BINARY_OP_MINUS;
    } else if (hop_start(&p, "*")) {
        *op = BINARY_OP_STAR;
    } else if (hop_start(&p, "/")) {
        *op = BINARY_OP_SLASH;
    } else if (hop_start(&p, "===")) {
        *op = BINARY_OP_TRIPLE_EQUAL;
    } else if (hop_start(&p, "==")) {
        *op = BINARY_OP_DOUBLE_EQUAL;
    } else if (hop_start(&p, "=")) {
        *op = BINARY_OP_SINGLE_EQUAL;
    } else if (hop_start(&p, "<=")) {
        *op = BINARY_OP_LESS_EQUAL;
    } else if (hop_start(&p, ">=")) {
        *op = BINARY_OP_GREATER_EQUAL;
    } else if (hop_start(&p, "<")) {
        *op = BINARY_OP_LESS;
    } else if (hop_start(&p, ">")) {
        *op = BINARY_OP_GREATER;
    } else if (hop_start(&p, "!==")) {
        *op = BINARY_OP_DOUBLE_NOT_EQUAL;
    } else if (hop_start(&p, "!=")) {
        *op = BINARY_OP_NOT_EQUAL;
    } else {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, right)) {
        ErrorCtx_error(&p.errs, span_from_pos(&p), 1109, "Expression expected.");
        *right = Expr_default(span_from_pos(&p));
    }

    return parse_success(&p);
}

static bool parse_left_unary_op_expr(Parser* parent, Expr* ret) {
    Parser p = parser(parent);

    Anchor a = span_start(&p);
    UnaryOp op = 0;
    if (hop_start(&p, "!")) {
        op = UNARY_OP_BANG;
    } else if (hop_start(&p, "++")) {
        op = UNARY_OP_LEFT_PLUS_PLUS;
    } else if (hop_start(&p, "--")) {
        op = UNARY_OP_LEFT_MINUS_MINUS;
    } else if (hop_start(&p, "+")) {
        op = UNARY_OP_PLUS;
    } else if (hop_start(&p, "-")) {
        op = UNARY_OP_MINUS;
    } else {
        return parse_fail(&p);
    }
    Span op_span = span_end(a);
    hop_whitespace(&p);

    Expr expr;
    if (!parse_expr(&p, &expr)) {
        ErrorCtx_parse_error(&p.errs, "expected expression after unary operation");
        expr = Expr_default(span_from_pos(&p));
    }

    *ret = Expr_unary_op(
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
            ErrorCtx_parse_error(&p.errs, "invalid number");
        }
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
    while (*p.pos != quote) {
        if (*p.pos == '\\') {
            p.pos += 2;
        } else {
            p.pos++;
        }
    }
    *ret = cstr_from_n(start, p.pos - start);
    p.pos++;

    return parse_success(&p);
}

static bool parse_type_compat_literal(Parser* parent, Value* ret) {
    Parser p = parser(parent);

    if (hop_start_if_ends_on_word_break(&p, "undefined")) {
        *ret = Value_undefined;
    } else if (hop_start_if_ends_on_word_break(&p, "null")) {
        *ret = Value_null;
    } else if (hop_start_if_ends_on_word_break(&p, "true")) {
        *ret = Value_boolean(true);
    } else if (hop_start_if_ends_on_word_break(&p, "false")) {
        *ret = Value_boolean(false);
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
        vec_Expr args;
        cstr str;
        Type type;
        UnaryOp unary;
        struct {
            Expr expr;
            BinaryOp op;
        } binary;
        FunctionImpl function;
    } parsed;

    if (parse_call_args(&p, &parsed.args)) {
        *left_and_ret = Expr_call(
            span_end(a),
            c_new(CallExpr, {
                .callable = *left_and_ret,
                .args = parsed.args,
            })
        );
        return parse_success(&p);
    }

    if (parse_dot_prop(&p, &parsed.str)) {
        *left_and_ret = Expr_dot_prop(
            span_end(a),
            c_new(DotPropExpr, {
                .expr = *left_and_ret,
                .prop = parsed.str,
            })
        );
        return parse_success(&p);
    }

    if (parse_as_type(&p, &parsed.type)) {
        *left_and_ret = Expr_as_type(
            span_end(a),
            c_new(AsTypeExpr, {
                .expr = *left_and_ret,
                .type = parsed.type,
            })
        );
        return parse_success(&p);
    }

    if (!hit_newline && left_and_ret->tag == EXPR_IDENTIFIER) {
        if (parse_function(&p, false, true, &parsed.function)) {
            vec_FunctionArg_push(&parsed.function.args, (FunctionArg){
                .name = cstr_clone(left_and_ret->identifier.name),
                .name_span = left_and_ret->span,
                .value_explicit = false,
                .value = Expr_default(span_from_pos(&p)),
            });
            parsed.function.args_span = left_and_ret->span;
            if (parsed.function.type.tag != TYPE_CALLABLE) {
                PANIC("Function type is not callable");
            }
            vec_CallableTypeArg_push(&parsed.function.type.callable->args, (CallableTypeArg){
                .name = cstr_clone(left_and_ret->identifier.name),
                .type_explicit = false,
                .type = Type_any,
            });
            Expr_drop(left_and_ret);
            *left_and_ret = Expr_function(span_end(a), c_new(FunctionImpl, parsed.function));
            return parse_success(&p);
        }
    }

    if (!hit_newline) {
        if (parse_right_unary_op_expr(&p, &parsed.unary)) {
            *left_and_ret = Expr_unary_op(
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
        if (parsed.binary.expr.tag == EXPR_BINARY_OP &&
            !parsed.binary.expr.binary_op->parenthesized &&
            BinaryOp_cmp_precedence(parsed.binary.op, parsed.binary.expr.binary_op->op) >= 0
        ) {
            parsed.binary.expr.binary_op->left = Expr_binary_op(
                span_end(a),
                c_new(BinaryOpExpr, {
                    .left = *left_and_ret,
                    .op = parsed.binary.op,
                    .right = parsed.binary.expr.binary_op->left,
                })
            );
            *left_and_ret = parsed.binary.expr;
        } else {
            *left_and_ret = Expr_binary_op(
                span_end(a),
                c_new(BinaryOpExpr, {
                    .left = *left_and_ret,
                    .op = parsed.binary.op,
                    .right = parsed.binary.expr,
                })
            );
        }
        return parse_success(&p);
    }

    return parse_fail(&p);
}

static bool parse_expr(Parser* parent, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    cstr name = cstr_null;
    FunctionImpl function = {0};
    if (parse_literal(&p, &ret->literal)) {
        ret->tag = EXPR_LITERAL;
    } else if (parse_object(&p, &ret->object)) {
        ret->tag = EXPR_OBJECT;
    } else if (parse_left_unary_op_expr(&p, ret)) {
    } else if (parse_identifier(&p, &name, nullptr)) {
        *ret = Expr_identifier(span_from_parsed(&p), name);
    } else if (parse_function(&p, true, true, &function)) {
        *ret = Expr_function(span_from_parsed(&p), c_new(FunctionImpl, function));
    } else if (hop_start(&p, "(")) {
        parse_expr(&p, ret);
        if (!hop_start(&p, ")")) {
            ErrorCtx_parse_error(&p.errs, "expected ')'");
        }
        switch (ret->tag) {
            case EXPR_BINARY_OP: ret->binary_op->parenthesized = true; break;
            case EXPR_UNARY_OP: ret->unary_op->parenthesized = true; break;
            default: break;
        }
    } else {
        return parse_fail(&p);
    }

    ret->span = span_end(a);

    while (parse_expr_right_side(&p, ret)) {}

    return parse_success(&p);
}

static bool parse_name_init(Parser* parent, NameInitData* ret) {
    Parser p = parser(parent);

    cstr name = cstr_null;
    Span name_span;
    if (!parse_identifier(&p, &name, &name_span)) {
        ErrorCtx_parse_error(&p.errs, "missing name");
        name_span = span_from_pos(&p);
    }
    hop_whitespace(&p);

    Type type = Type_any;
    bool type_explicit = false;
    Anchor type_a = span_start(&p);
    if (hop_start(&p, ":")) {
        hop_whitespace(&p);
        if (parse_type(&p, &type)) {
            type_explicit = true;
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
        }
    }
    if (!value_explicit) {
        value = Expr_default(span_from_pos(&p));
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

static bool parse_var_init(Parser* parent, VarInitStatement* ret) {
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

    NameInitData data;
    if (!parse_name_init(&p, &data)) {
        return parse_fail(&p);
    }

    *ret = (VarInitStatement) {
        .keyword = keyword,
        .data = data,
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
        ErrorCtx_parse_error(&p.errs, "expected '(' after if");
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, condition)) {
        ErrorCtx_parse_error(&p.errs, "invalid if condition");
        *condition = Expr_literal(span_from_pos(&p), Value_boolean(false));
    }
    hop_whitespace(&p);

    if (!hop_start(&p, ")")) {
        ErrorCtx_parse_error(&p.errs, "expected ')' after if condition");
    }
    hop_whitespace(&p);

    if (!parse_statement(&p, statement)) {
        ErrorCtx_parse_error(&p.errs, "invalid if body");
        *statement = Statement_noop;
    }
    return parse_success(&p);
}

static bool parse_if_else(Parser* parent, IfStatement* ret) {
    Parser p = parser(parent);

    vec_Expr conditions = {0};
    vec_Statement statements = {0};
    while (true) {
        Expr condition = {0};
        Statement statement = {0};
        if (!parse_if_arm(&p, &condition, &statement)) {
            if (vec_Expr_empty(&conditions)) {
                return parse_fail(&p);
            } else if (parse_statement(&p, &statement)) {
                vec_Statement_push(&statements, statement);
                break;
            } else {
                ErrorCtx_parse_error(&p.errs, "expected else statement");
            }
        }
        hop_whitespace(&p);

        vec_Expr_push(&conditions, condition);
        vec_Statement_push(&statements, statement);
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

static bool parse_for_loop(Parser* parent, LoopStatement* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "for")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!hop_start(&p, "(")) {
        ErrorCtx_parse_error(&p.errs, "expected '(' after for");
    }
    hop_whitespace(&p);

    Statement initialization = Statement_noop;
    Expr afterthought;
    Statement body = Statement_noop;
    parse_statement(&p, &initialization); // It's fine if it doesn't parse anything
    hop_whitespace(&p);

    if (!hop_start(&p, ";")) {
        ErrorCtx_parse_error(&p.errs, "expected ';' in for header");
        p.pos++;
    }
    hop_whitespace(&p);

    Expr condition = Expr_literal(span_from_pos(&p), Value_boolean(true));
    parse_expr(&p, &condition); // It's fine if it doesn't parse anything
    hop_whitespace(&p);

    if (!hop_start(&p, ";")) {
        ErrorCtx_parse_error(&p.errs, "expected ';' in for header");
        p.pos++;
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, &afterthought)) {
        afterthought = Expr_default(span_from_pos(&p));
    }
    hop_whitespace(&p);

    if (!hop_start(&p, ")")) {
        ErrorCtx_parse_error(&p.errs, "expected ')' at end of for header");
    }
    hop_whitespace(&p);

    if (!parse_statement(&p, &body)) {
        ErrorCtx_parse_error(&p.errs, "expected for loop body");
    }

    *ret = (LoopStatement) {
        .initialization = initialization,
        .condition = condition,
        .afterthought = afterthought,
        .body = body,
    };
    return parse_success(&p);
}

static bool parse_while_loop(Parser* parent, LoopStatement* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "while")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!hop_start(&p, "(")) {
        ErrorCtx_parse_error(&p.errs, "expected '(' after while");
    }
    hop_whitespace(&p);

    Expr condition = Expr_literal(span_from_pos(&p), Value_boolean(false));
    Statement body = Statement_noop;
    if (!parse_expr(&p, &condition)) {
        ErrorCtx_parse_error(&p.errs, "expected expression before ')'");
    }
    hop_whitespace(&p);

    if (!hop_start(&p, ")")) {
        ErrorCtx_parse_error(&p.errs, "expected ')' at end of for header");
    }
    hop_whitespace(&p);

    if (!parse_statement(&p, &body)) {
        ErrorCtx_parse_error(&p.errs, "expected while loop body");
    }

    *ret = (LoopStatement) {
        .initialization = Statement_noop,
        .condition = condition,
        .afterthought = Expr_default(span_from_pos(&p)),
        .body = body,
    };
    return parse_success(&p);
}

static bool parse_return(Parser* parent, Expr* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "return")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!parse_expr(&p, ret)) {
        *ret = Expr_default(span_from_parsed(&p));
    }

    return parse_success(&p);
}

static bool parse_function_statement(Parser* parent, FunctionStatement* ret) {
    Parser p = parser(parent);

    if (!hop_start_and_required_whitespace(&p, "function")) {
        return parse_fail(&p);
    }

    if (!parse_identifier(&p, &ret->name, nullptr)) {
        ErrorCtx_parse_error(&p.errs, "missing function name");
    }
    hop_whitespace(&p);

    if (!parse_function(&p, true, false, &ret->impl)) {
        cstr_drop(&ret->name);
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = span_start(&p);

    LoopStatement loop;
    if (parse_named_interface(&p, &ret->interface)) {
        ret->tag = STATEMENT_INTERFACE;
    } else if (parse_block(&p, &ret->block)) {
        ret->tag = STATEMENT_BLOCK;
    } else if (parse_var_init(&p, &ret->var_init)) {
        ret->tag = STATEMENT_VAR_INIT;
    } else if (parse_if_else(&p, &ret->if_else)) {
        ret->tag = STATEMENT_IF_ELSE;
    } else if (parse_for_loop(&p, &loop)) {
        *ret = (Statement){
            .tag = STATEMENT_LOOP,
            .loop = c_new(LoopStatement, loop),
        };
    } else if (parse_while_loop(&p, &loop)) {
        *ret = (Statement){
            .tag = STATEMENT_LOOP,
            .loop = c_new(LoopStatement, loop),
        };
    } else if (hop_start_if_ends_on_word_break(&p, "break")) {
        ret->tag = STATEMENT_BREAK;
        ret->break_span = span_end(a);
    } else if (parse_return(&p, &ret->expr)) {
        ret->tag = STATEMENT_RETURN;
    } else if (parse_function_statement(&p, &ret->function)) {
        ret->tag = STATEMENT_FUNCTION;
    } else if (parse_expr(&p, &ret->expr)) {
        ret->tag = STATEMENT_EXPR;
    } else {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_block_inner(Parser* parent, Block* ret) {
    Parser p = parser(parent);

    vec_Statement statements = {0};
    while(*p.pos && *p.pos != '}') {
        Statement statement = {0};
        if (*p.pos == ';') {
            p.pos++;
        } else if (parse_statement(&p, &statement)) {
            vec_Statement_push(&statements, statement);
            if (hop_whitespace(&p) == WHITESPACE_NEWLINE || *p.pos || *p.pos == '}') {}
            else if (*p.pos == ';') {
                p.pos++;
            } else {
                ErrorCtx_error(&p.errs, span_from_pos(&p), 1005, "';' expected.");
            }
        } else {
            ErrorCtx_error(&p.errs, span_from_pos(&p), 1128, "Declaration or statement expected.");
            p.pos++;
        }
        hop_whitespace(&p);
    }

    *ret = (Block){
        .statements = statements,
    };
    return parse_success(&p);
}

static bool parse_block(Parser* parent, Block* ret) {
    Parser p = parser(parent);

    if (!hop_char(&p, '{')) return parse_fail(&p);
    hop_whitespace(&p);

    if (!parse_block_inner(&p, ret)) {
        *ret = (Block){
            .statements = vec_Statement_init(),
        };
    }
    hop_whitespace(&p);

    if (!hop_char(&p, '}')) {
        ErrorCtx_parse_error(&p.errs, "expected '}'");
    }
    return parse_success(&p);
}

Block parse_file(const char* source, ErrorCtx* errs) {
    Parser p = {
        .parent = nullptr,
        .file_start = source,
        .start = source,
        .pos = source,
        .errs = {0},
    };

    if (p.pos[0] == '\xEF' && p.pos[1] == '\xBB' && p.pos[2] == '\xBF') {
        // Skip Byte Order Mark nonsense
        p.pos += 3;
    }

    hop_whitespace(&p);

    Block scope;
    parse_block_inner(&p, &scope);
    if (*p.pos) {
        ErrorCtx_parse_error(&p.errs, "unexpected '}'");
    }

    ErrorCtx_append_consume(errs, p.errs);
    return scope;
}
