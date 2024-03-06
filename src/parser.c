#include "parser.h"
#include "block.h"
#include "interface.h"
#include "interface_member.h"
#include "statement.h"
#include "ternary_type.h"
#include "generic.h"
#include "callable_type.h"
#include "extends_conditional.h"
#include "type.h"
#include "error.h"

static bool parse_block(const char** src, Block* ret, ErrorCtx* errs);
static bool parse_interface(const char** src, Interface* ret, ErrorCtx* errs);
static bool parse_name_init(const char** src, NameInitData* ret, ErrorCtx* errs);
static bool parse_statement(const char** src, Statement* ret, ErrorCtx* errs);
static bool parse_type(const char** src, Type* type, ErrorCtx* errs);
static bool parse_type_compat_literal(const char** src, Value* ret, ErrorCtx* errs);
static bool parse_expr(const char** src, Expr* ret, ErrorCtx* errs);

static inline bool hop_whitespace(const char** str) {
    bool ret = false;
    while (true) {
        switch (**str) {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
                break;

            case '/':
                switch (*((*str) + 1)) {
                    case '/':
                        (*str) += 2;
                        while (**str && **str != '\n') {
                            (*str)++;
                        }
                        break;

                    case '*':
                        (*str)++;
                        while (**str && (*((*str) - 1) != '*' || **str != '/')) {
                            (*str)++;
                        }
                        break;

                    default:
                        return ret;
                }
                break;

            default:
                return ret;
        }
        ret = true;
        (*str)++;
    }
}

static inline bool hop_start(const char** str, const char* prefix) {
    const char* local_str = *str;
    while (*prefix) {
        if (!*local_str || *prefix != *local_str) {
            return false;
        }
        prefix++;
        local_str++;
    }
    *str = local_str;
    return true;
}

static inline bool hop_char(const char** str, char c) {
    if (**str == c) {
        (*str)++;
        return true;
    } else {
        return false;
    }
}

static inline bool hop_start_and_required_whitespace(const char** str, const char* prefix) {
    const char* local_str = *str;
    if (hop_start(&local_str, prefix)) {
        if (hop_whitespace(&local_str)) {
            *str = local_str;
            return true;
        }
    }
    return false;
}

static inline bool is_identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static inline bool is_identifier_start_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static inline bool is_number_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

static inline bool is_number_start_char(char c) {
    return (c >= '0' && c <= '9') || c == '.';
}

static inline bool hop_start_if_ends_on_word_break(const char** str, const char* prefix) {
    const char* local_str = *str;
    if (hop_start(&local_str, prefix)) {
        if (!is_identifier_char(*local_str)) {
            *str = local_str;
            return true;
        }
    }
    return false;
}

static bool parse_identifier(const char** src, cstr* name) {
    const char* start = *src;
    if (is_identifier_start_char(**src)) {
        (*src)++;
        while (is_identifier_char(**src)) (*src)++;
        *name = cstr_from_n(start, *src - start);
        return true;
    } else {
        return false;
    }
}

static bool parse_number_str(const char** src, cstr* name) {
    const char* start = *src;
    if (is_number_start_char(**src)) {
        (*src)++;
        while (is_number_char(**src)) (*src)++;
        *name = cstr_from_n(start, *src - start);
        return true;
    } else {
        return false;
    }
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
    const char** src,
    const char* start,
    ParseSequenceSep sep,
    char end,
    bool (*callback)(const char** src, void* data, ErrorCtx* errs),
    void* data,
    ErrorCtx* errs
) {
    if (!hop_start(src, start)) {
        return false;
    }
    hop_whitespace(src);
    while (**src && **src != end) {
        const char* before_callback = *src;
        if (!callback(src, data, errs)) {
            ErrorCtx_parse_error(errs, *src, "invald item in sequence");
            while (**src && **src != end && char_is_sep(sep, **src)) {
                (*src)++;
            }
        }
        if (*src == before_callback) {
            (*src)++;
        }
        hop_whitespace(src);
        if (sep_required(sep) && !char_is_sep(sep, **src) && **src != end) {
            ErrorCtx_parse_error(errs, *src, "expected separator or end of sequence");
        }
        bool first_sep = true;
        while (char_is_sep(sep, **src)) {
            (*src)++;
            hop_whitespace(src);
            if (!first_sep) {
                ErrorCtx_parse_error(errs, *src, "expected item before separator");
            }
            first_sep = false;
        }
    }
    if (**src == end) {
        (*src)++;
    } else {
        ErrorCtx_parse_error(errs, *src, "expected end of sequence");
    }
    return true;
}

static bool parse_ternary_type(const char** src, Type first, TernaryType* ret, ErrorCtx* errs) {
    const char* s = *src;
    Type maybeSuper = {0};
    Type trueType = {0};
    Type falseType = {0};
    hop_whitespace(&s);
    if (!hop_start_if_ends_on_word_break(&s, "extends")) {
        return false;
    }
    hop_whitespace(&s);
    parse_type(&s, &maybeSuper, errs);
    hop_whitespace(&s);
    if (!hop_start(&s, "?")) {
        ErrorCtx_parse_error(errs, *src, "expected '?'");
    }
    hop_whitespace(&s);
    parse_type(&s, &trueType, errs);
    hop_whitespace(&s);
    if (!hop_start(&s, ":")) {
        ErrorCtx_parse_error(errs, *src, "expected ':'");
    }
    hop_whitespace(&s);
    parse_type(&s, &falseType, errs);
    *ret = (TernaryType){
        .conditional = (ExtendsConditional){
            .maybeSub = first,
            .maybeSuper = maybeSuper,
        },
        .trueType = trueType,
        .falseType = falseType,
    };
    *src = s;
    return true;
}

static bool parse_type(const char** src, Type* ret, ErrorCtx* errs) {
    Type type;
    if (hop_start_if_ends_on_word_break(src, "any")) {
        type = Type_any;
    } else if (hop_start_if_ends_on_word_break(src, "void")) {
        type = Type_void;
    } else if (hop_start_if_ends_on_word_break(src, "boolean")) {
        type = Type_boolean;
    } else if (hop_start_if_ends_on_word_break(src, "number")) {
        type = Type_number;
    } else if (hop_start_if_ends_on_word_break(src, "string")) {
        type = Type_string;
    } else if (parse_type_compat_literal(src, &type.value, errs)) {
        type.tag = TYPE_VALUE;
    } else if (parse_identifier(src, &type.identifier.name)) {
        type.tag = TYPE_IDENTIFIER;
        type.identifier.type = nullptr;
    } else if (parse_interface(src, &type.interface, errs)) {
        type.tag = TYPE_INTERFACE;
    } else {
        ErrorCtx_parse_error(errs, *src, "did not find type");
        return false;
    }
    TernaryType ternary;
    if (parse_ternary_type(src, type, &ternary, errs)) {
        *ret = Type_ternary(c_new(TernaryType, ternary));
        return true;
    } else {
        *ret = type;
        return true;
    }
}

static bool parse_interface(const char** src, Interface* ret, ErrorCtx* errs) {
    const char* s = *src;
    if (!hop_char(&s, '{')) {
        return false;
    }
    vec_InterfaceMember members = {0};
    hop_whitespace(&s);
    const char* prev_s = nullptr;
    while (*s && *s != '}' && s != prev_s) {
        prev_s = s;
        hop_whitespace(&s);
        cstr name = cstr_null;
        if (!parse_identifier(&s, &name)) {
            ErrorCtx_parse_error(errs, *src, "expected interface member name");
        }
        hop_whitespace(&s);
        if (!hop_char(&s, ':')) {
            ErrorCtx_parse_error(errs, *src, "expected ':'");
        }
        hop_whitespace(&s);
        Type type;
        parse_type(&s, &type, errs);
        vec_InterfaceMember_push(&members, (InterfaceMember){
            .name = name,
            .type = type,
        });
        hop_whitespace(&s);
        // Typescript accepts EITHER of these in interfaces (or nothing at all). You can MIX them!
        // it also cares about newlines but we can sort that out later
        (void)(hop_char(&s, ',') || hop_char(&s, ';'));
        hop_whitespace(&s);
    }
    if (!hop_char(&s, '}')) {
        ErrorCtx_parse_error(errs, *src, "expected '}'");
    }
    *ret = (Interface) {
        .members = members,
    };
    *src = s;
    return true;
}

static bool parse_named_interface(const char** src, InterfaceStatement* ret, ErrorCtx* errs) {
    const char* s = *src;
    if (!hop_start_and_required_whitespace(&s, "interface")) {
        return false;
    }
    cstr name = cstr_null;
    ErrorCtx local_errs = {0};
    if (!parse_identifier(&s, &name)) {
        ErrorCtx_parse_error(&local_errs, *src, "interface does not have a name");
    }
    hop_whitespace(&s);
    Interface interface;
    if (!parse_interface(&s, &interface, errs)) {
        ErrorCtx_drop(&local_errs);
        cstr_drop(&name);
        return false;
    }
    ErrorCtx_append_consume(errs, local_errs);
    *ret = (InterfaceStatement) {
        .name = name,
        .type = Type_interface(interface),
    };
    *src = s;
    return true;
}

typedef struct ParseFunctionCtx {
    vec_GenericItem generics;
    CallableType callable;
    FunctionImpl function;
} ParseFunctionCtx;

static void ParseFunctionCtx_drop(ParseFunctionCtx* self) {
    vec_GenericItem_drop(&self->generics);
    CallableType_drop(&self->callable);
    FunctionImpl_drop(&self->function);
}

static bool parse_function_generic(const char** src, void* data, ErrorCtx* errs) {
    ParseFunctionCtx* ctx = data;

    cstr name = cstr_null;
    if (!parse_identifier(src, &name)) {
        ErrorCtx_parse_error(errs, *src, "missing generic name");
    }
    hop_whitespace(src);

    Type extends = Type_any;
    if (hop_start_if_ends_on_word_break(src, "extends")) {
        hop_whitespace(src);
        parse_type(src, &extends, errs);
        hop_whitespace(src);
    }

    Type fallback = Type_any;
    bool fallback_explicit = false;
    if (hop_start(src, "=")) {
        hop_whitespace(src);
        if (parse_type(src, &fallback, errs)) {
            fallback_explicit = true;
        }
    }

    vec_GenericItem_push(&ctx->generics, (GenericItem){
        .name = name,
        .extends = extends,
        .fallback_explicit = fallback_explicit,
        .fallback = fallback,
    });
    return true;
}

static bool parse_function_arg(const char** src, void* data, ErrorCtx* errs) {
    ParseFunctionCtx* ctx = data;

    NameInitData name_init = {0};
    if (!parse_name_init(src, &name_init, errs)) {
        return false;
    }

    vec_FunctionArg_push(&ctx->function.args, (FunctionArg){
        .name = cstr_clone(name_init.name),
        .value_explicit = name_init.value_explicit,
        .value = name_init.value,
    });

    vec_CallableTypeArg_push(&ctx->callable.args, (CallableTypeArg){
        .name = name_init.name,
        .type_explicit = name_init.type_explicit,
        .type = name_init.type,
    });

    return true;
}

static bool parse_function(const char** src, bool require_arrow, FunctionImpl* ret, ErrorCtx* errs) {
    const char* s = *src;
    ErrorCtx local_errs = {0};
    ParseFunctionCtx ctx = {0};

    if (parse_sequence(&s, "<", SEQUENCE_SEP_COMMA, '>', parse_function_generic, &ctx, &local_errs)) {
        hop_whitespace(&s);
    }

    if (!parse_sequence(&s, "(", SEQUENCE_SEP_COMMA, ')', parse_function_arg, &ctx, &local_errs)) {
        ErrorCtx_drop(&local_errs);
        ParseFunctionCtx_drop(&ctx);
        return false;
    }
    hop_whitespace(&s);

    if (hop_start(&s, ":")) {
        hop_whitespace(&s);
        if (parse_type(&s, &ctx.callable.ret, &local_errs)) {
            ctx.callable.ret_explicit = true;
        } else {
            ErrorCtx_parse_error(&local_errs, s, "invalid return type");
        }
        hop_whitespace(&s);
    }

    if (require_arrow) {
        if (!hop_start(&s, "=>")) {
            ErrorCtx_drop(&local_errs);
            ParseFunctionCtx_drop(&ctx);
            return false;
        }
        hop_whitespace(&s);
        Expr expr = {0};
        if (parse_block(&s, &ctx.function.body, &local_errs)) {
        } else if (parse_expr(&s, &expr, &local_errs)) {
            ctx.function.body = (Block){
                .statements = vec_Statement_init(),
            };
            vec_Statement_push(&ctx.function.body.statements, (Statement){
                .tag = STATEMENT_RETURN,
                .expr = expr,
            });
        } else {
            ErrorCtx_parse_error(&local_errs, s, "invalid arrow function body");
        }
    } else {
        if (!parse_block(&s, &ctx.function.body, &local_errs)) {
            ErrorCtx_parse_error(&local_errs, s, "invalid function body");
        }
    }

    ErrorCtx_append_consume(errs, local_errs);
    ctx.function.type = Type_callable(c_new(CallableType, ctx.callable));
    if (!vec_GenericItem_empty(&ctx.generics)) {
        ctx.function.type = Type_generic(c_new(Generic, {
            .items = ctx.generics,
            .inner = ctx.function.type,
        }));
    }
    *ret = ctx.function;
    *src = s;
    return true;
}

static bool parse_object_item(const char** src, void* data, ErrorCtx* errs) {
    vec_ObjectExprItem* ret = data;
    cstr name;
    if (!parse_identifier(src, &name)) {
        return false;
    }
    hop_whitespace(src);
    if (!hop_start(src, ":")) {
        return false;
    }
    hop_whitespace(src);
    Expr value;
    if (!parse_expr(src, &value, errs)) {
        ErrorCtx_parse_error(errs, *src, "bad expression in object");
    }
    vec_ObjectExprItem_push(ret, (ObjectExprItem){
        .name = name,
        .value = value,
    });
    return true;
}

static bool parse_object(const char** src, ObjectExpr* ret, ErrorCtx* errs) {
    vec_ObjectExprItem items = {0};
    if (!parse_sequence(src, "{", SEQUENCE_SEP_COMMA, '}', parse_object_item, &items, errs)) {
        vec_ObjectExprItem_drop(&items);
        return false;
    }
    *ret = items;
    return true;
}

static bool parse_call_args(const char** src, vec_Expr* args, ErrorCtx* errs) {
    if (!hop_start(src, "(")) {
        return false;
    }
    hop_whitespace(src);
    *args = vec_Expr_init();
    while (**src && **src != ')') {
        Expr arg = {0};
        if (parse_expr(src, &arg, errs)) {
            vec_Expr_push(args, arg);
        } else {
            ErrorCtx_parse_error(errs, *src, "invald expression in call");
            while (**src && **src != ')' && **src != ',') {
                (*src)++;
            }
        }
        hop_whitespace(src);
        if (**src != ',' && **src != ')') {
            ErrorCtx_parse_error(errs, *src, "expected ',' or ')' in call");
        }
        bool first_comma = true;
        while (**src == ',') {
            (*src)++;
            hop_whitespace(src);
            if (!first_comma) {
                ErrorCtx_parse_error(errs, *src, "expected argument before ','");
            }
            first_comma = false;
        }
    }
    if (**src == ')') {
        (*src)++;
    } else {
        ErrorCtx_parse_error(errs, *src, "expected ')' at end of call");
    }
    return true;
}

static bool parse_right_unary_op_expr(const char** src, UnaryOp* op, ErrorCtx* errs) {
    (void)errs;
    if (hop_start(src, "++")) {
        *op = UNARY_OP_RIGHT_PLUS_PLUS;
    } else if (hop_start(src, "--")) {
        *op = UNARY_OP_RIGHT_MINUS_MINUS;
    } else {
        return false;
    }
    return true;
}

static bool parse_dot_prop(const char** src, cstr* prop, ErrorCtx* errs) {
    if (!hop_start(src, ".")) {
        return false;
    }
    hop_whitespace(src);
    if (!parse_identifier(src, prop)) {
        ErrorCtx_parse_error(errs, *src, "expected property name");
        *prop = cstr_lit("INVALID PROPERTY");
    }
    return true;
}

static bool parse_binary_op_expr(const char** src, BinaryOp* op, Expr* right, ErrorCtx* errs) {
    if (hop_start(src, "+")) {
        *op = BINARY_OP_PLUS;
    } else if (hop_start(src, "-")) {
        *op = BINARY_OP_MINUS;
    } else if (hop_start(src, "*")) {
        *op = BINARY_OP_STAR;
    } else if (hop_start(src, "/")) {
        *op = BINARY_OP_SLASH;
    } else if (hop_start(src, "===")) {
        *op = BINARY_OP_TRIPLE_EQUAL;
    } else if (hop_start(src, "==")) {
        *op = BINARY_OP_DOUBLE_EQUAL;
    } else if (hop_start(src, "=")) {
        *op = BINARY_OP_SINGLE_EQUAL;
    } else if (hop_start(src, "<")) {
        *op = BINARY_OP_LESS;
    } else if (hop_start(src, ">")) {
        *op = BINARY_OP_GREATER;
    } else if (hop_start(src, "<=")) {
        *op = BINARY_OP_LESS_EQUAL;
    } else if (hop_start(src, ">=")) {
        *op = BINARY_OP_GREATER_EQUAL;
    } else if (hop_start(src, "!==")) {
        *op = BINARY_OP_DOUBLE_NOT_EQUAL;
    } else if (hop_start(src, "!=")) {
        *op = BINARY_OP_NOT_EQUAL;
    } else {
        return false;
    }

    hop_whitespace(src);
    if (!parse_expr(src, right, errs)) {
        ErrorCtx_parse_error(errs, *src, "expected expression after binary operation");
    }
    return true;
}

static bool parse_left_unary_op_expr(const char** src, Expr* ret, ErrorCtx* errs) {
    (void)errs;
    UnaryOp op = 0;
    if (hop_start(src, "!")) {
        op = UNARY_OP_BANG;
    } else if (hop_start(src, "++")) {
        op = UNARY_OP_LEFT_PLUS_PLUS;
    } else if (hop_start(src, "--")) {
        op = UNARY_OP_LEFT_MINUS_MINUS;
    } else if (hop_start(src, "+")) {
        op = UNARY_OP_PLUS;
    } else if (hop_start(src, "-")) {
        op = UNARY_OP_MINUS;
    } else {
        return false;
    }
    hop_whitespace(src);
    Expr expr = Expr_default;
    if (!parse_expr(src, &expr, errs)) {
        ErrorCtx_parse_error(errs, *src, "expected expression after unary operation");
    }
    *ret = Expr_unary_op_new(c_new(UnaryOpExpr, {
        .op = op,
        .expr = expr,
    }));
    return true;
}

static bool parse_number(const char** src, double* ret, ErrorCtx* errs) {
    const char* s = *src;
    cstr number_str;
    if (!parse_number_str(&s, &number_str)) {
        return false;
    } else {
        // TODO: javascript-compliant number parsing
        char* end;
        *ret = strtod(cstr_str(&number_str), &end);
        if (end != cstr_str(&number_str) + cstr_size(&number_str)) {
            ErrorCtx_parse_error(errs, *src, "invalid number");
        }
        *src = s;
        return true;
    }
}

static bool parse_string(const char** src, cstr* ret, ErrorCtx* errs) {
    (void)errs;
    char quote = **src;
    if (quote != '"' && quote != '\'') {
        return false;
    }
    (*src)++;
    const char* start = *src;
    // TODO: remotely correct string parsing
    while (**src != quote) {
        if (**src == '\\') {
            (*src) += 2;
        } else {
            (*src)++;
        }
    }
    *ret = cstr_from_n(start, *src - start);
    (*src)++;
    return true;
}

static bool parse_type_compat_literal(const char** src, Value* ret, ErrorCtx* errs) {
    if (hop_start_if_ends_on_word_break(src, "undefined")) {
        *ret = Value_undefined;
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "null")) {
        *ret = Value_null;
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "true")) {
        *ret = Value_boolean(true);
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "false")) {
        *ret = Value_boolean(false);
        return true;
    } else if (parse_number(src, &ret->number, errs)) {
        ret->tag = VALUE_NUMBER;
        return true;
    } else if (parse_string(src, &ret->string, errs)) {
        ret->tag = VALUE_STRING;
        return true;
    } else {
        return false;
    }
}

static bool parse_literal(const char** src, Value* ret, ErrorCtx* errs) {
    return parse_type_compat_literal(src, ret, errs);
}

static bool parse_expr(const char** src, Expr* ret, ErrorCtx* errs) {
    cstr name = cstr_null;
    FunctionImpl function = {0};
    if (parse_literal(src, &ret->literal, errs)) {
        ret->tag = EXPR_LITERAL;
    } else if (parse_object(src, &ret->object, errs)) {
        ret->tag = EXPR_OBJECT;
    } else if (parse_left_unary_op_expr(src, ret, errs)) {
    } else if (parse_identifier(src, &name)) {
        *ret = Expr_indentifier_new(name);
    } else if (parse_function(src, true, &function, errs)) {
        *ret = Expr_function_new(c_new(FunctionImpl, function));
    } else if (hop_start(src, "(")) {
        parse_expr(src, ret, errs);
        if (!hop_start(src, ")")) {
            ErrorCtx_parse_error(errs, *src, "expected ')'");
        }
        switch (ret->tag) {
            case EXPR_BINARY_OP: ret->binary_op->parenthesized = true; break;
            case EXPR_UNARY_OP: ret->unary_op->parenthesized = true; break;
            default: break;
        }
    } else {
        return false;
    }

    bool done = false;
    while (!done) {
        done = true;
        hop_whitespace(src);

        vec_Expr args;
        if (parse_call_args(src, &args, errs)) {
            *ret = Expr_call_new(c_new(CallExpr, {
                .callable = *ret,
                .args = args,
            }));
            done = false;
        }

        cstr dot_prop = cstr_null;
        if (parse_dot_prop(src, &dot_prop, errs)) {
            *ret = Expr_dot_prop_new(c_new(DotPropExpr, {
                .expr = *ret,
                .prop = dot_prop,
            }));
            done = false;
        }

        UnaryOp right_unary;
        if (parse_right_unary_op_expr(src, &right_unary, errs)) {
            *ret = Expr_unary_op_new(c_new(UnaryOpExpr, {
                .op = right_unary,
                .expr = *ret,
            }));
            done = false;
        }

        Expr right = Expr_default;
        BinaryOp binary = 0;
        if (parse_binary_op_expr(src, &binary, &right, errs)) {
            if (right.tag == EXPR_BINARY_OP &&
                !right.binary_op->parenthesized &&
                BinaryOp_cmp_precedence(binary, right.binary_op->op) >= 0
            ) {
                right.binary_op->left = Expr_binary_op_new(c_new(BinaryOpExpr, {
                    .left = *ret,
                    .op = binary,
                    .right = right.binary_op->left,
                }));
                *ret = right;
            } else {
                *ret = Expr_binary_op_new(c_new(BinaryOpExpr, {
                    .left = *ret,
                    .op = binary,
                    .right = right,
                }));
            }
            done = false;
        }
    }

    return true;
}

static bool parse_name_init(const char** src, NameInitData* ret, ErrorCtx* errs) {
    const char* s = *src;
    cstr name = cstr_null;
    if (!parse_identifier(&s, &name)) {
        ErrorCtx_parse_error(errs, s, "missing name");
    }
    hop_whitespace(&s);
    Type type = Type_any;
    bool type_explicit = false;
    if (hop_start(&s, ":")) {
        hop_whitespace(&s);
        if (parse_type(&s, &type, errs)) {
            type_explicit = true;
        }
        hop_whitespace(&s);
    }
    Expr value = Expr_default;
    bool value_explicit = false;
    if (hop_start(&s, "=")) {
        hop_whitespace(&s);
        if (parse_expr(&s, &value, errs)) {
            value_explicit = true;
        }
    }
    *ret = (NameInitData) {
        .name = name,
        .type_explicit = type_explicit,
        .type = type,
        .value_explicit = value_explicit,
        .value = value,
    };
    *src = s;
    return true;
}

static bool parse_var_init(const char** src, VarInitStatement* ret, ErrorCtx* errs) {
    const char* s = *src;
    typeof(ret->keyword) keyword;
    if (hop_start_and_required_whitespace(&s, "var")) {
        keyword = VAR_INIT_VAR;
    } else if (hop_start_and_required_whitespace(&s, "let")) {
        keyword = VAR_INIT_LET;
    } else if (hop_start_and_required_whitespace(&s, "const")) {
        keyword = VAR_INIT_CONST;
    } else {
        return false;
    }
    NameInitData data;
    if (!parse_name_init(&s, &data, errs)) {
        return false;
    }
    *ret = (VarInitStatement) {
        .keyword = keyword,
        .data = data,
    };
    *src = s;
    return true;
}

static bool parse_if_arm(const char** src, Expr* condition, Statement* statement, ErrorCtx* errs) {
    if (!hop_start_if_ends_on_word_break(src, "if")) {
        return false;
    }
    hop_whitespace(src);
    if (!hop_start(src, "(")) {
        ErrorCtx_parse_error(errs, *src, "expected '(' after if");
    }
    hop_whitespace(src);
    if (!parse_expr(src, condition, errs)) {
        ErrorCtx_parse_error(errs, *src, "invalid if condition");
        *condition = Expr_literal_new(Value_boolean(false));
    }
    hop_whitespace(src);
    if (!hop_start(src, ")")) {
        ErrorCtx_parse_error(errs, *src, "expected ')' after if condition");
    }
    hop_whitespace(src);
    if (!parse_statement(src, statement, errs)) {
        ErrorCtx_parse_error(errs, *src, "invalid if body");
        *statement = Statement_noop;
    }
    return true;
}

static bool parse_if_else(const char** src, IfStatement* ret, ErrorCtx* errs) {
    vec_Expr conditions = {0};
    vec_Statement statements = {0};
    while (true) {
        Expr condition = {0};
        Statement statement = {0};
        if (!parse_if_arm(src, &condition, &statement, errs)) {
            if (vec_Expr_empty(&conditions)) {
                return false;
            } else if (parse_statement(src, &statement, errs)) {
                vec_Statement_push(&statements, statement);
                break;
            } else {
                ErrorCtx_parse_error(errs, *src, "expected else statement");
            }
        }
        hop_whitespace(src);
        vec_Expr_push(&conditions, condition);
        vec_Statement_push(&statements, statement);
        if (!hop_start_if_ends_on_word_break(src, "else")) {
            break;
        }
    }
    *ret = (IfStatement){
        .conditions = conditions,
        .statements = statements,
    };
    return true;
}

static bool parse_for_loop(const char** src, LoopStatement* ret, ErrorCtx* errs) {
    if (!hop_start_if_ends_on_word_break(src, "for")) {
        return false;
    }
    hop_whitespace(src);
    if (!hop_start(src, "(")) {
        ErrorCtx_parse_error(errs, *src, "expected '(' after for");
    }
    hop_whitespace(src);
    Statement initialization = Statement_noop;
    Expr condition = Expr_literal_new(Value_boolean(true));
    Expr afterthought = Expr_default;
    Statement body = Statement_noop;
    parse_statement(src, &initialization, errs); // It's fine if it doesn't parse anything
    hop_whitespace(src);
    if (!hop_start(src, ";")) {
        ErrorCtx_parse_error(errs, *src, "expected ';' in for header");
        (*src)++;
    }
    hop_whitespace(src);
    parse_expr(src, &condition, errs); // It's fine if it doesn't parse anything
    hop_whitespace(src);
    if (!hop_start(src, ";")) {
        ErrorCtx_parse_error(errs, *src, "expected ';' in for header");
        (*src)++;
    }
    hop_whitespace(src);
    parse_expr(src, &afterthought, errs); // It's fine if it doesn't parse anything
    hop_whitespace(src);
    if (!hop_start(src, ")")) {
        ErrorCtx_parse_error(errs, *src, "expected ')' at end of for header");
    }
    hop_whitespace(src);
    if (!parse_statement(src, &body, errs)) {
        ErrorCtx_parse_error(errs, *src, "expected for loop body");
    }
    *ret = (LoopStatement) {
        .initialization = initialization,
        .condition = condition,
        .afterthought = afterthought,
        .body = body,
    };
    return true;
}

static bool parse_while_loop(const char** src, LoopStatement* ret, ErrorCtx* errs) {
    if (!hop_start_if_ends_on_word_break(src, "while")) {
        return false;
    }
    hop_whitespace(src);
    if (!hop_start(src, "(")) {
        ErrorCtx_parse_error(errs, *src, "expected '(' after while");
    }
    hop_whitespace(src);
    Expr condition = Expr_literal_new(Value_boolean(false));
    Statement body = Statement_noop;
    if (!parse_expr(src, &condition, errs)) {
        ErrorCtx_parse_error(errs, *src, "expected expression before ')'");
    }
    hop_whitespace(src);
    if (!hop_start(src, ")")) {
        ErrorCtx_parse_error(errs, *src, "expected ')' at end of for header");
    }
    hop_whitespace(src);
    if (!parse_statement(src, &body, errs)) {
        ErrorCtx_parse_error(errs, *src, "expected while loop body");
    }
    *ret = (LoopStatement) {
        .initialization = Statement_noop,
        .condition = condition,
        .afterthought = Expr_default,
        .body = body,
    };
    return true;
}

static bool parse_return(const char** src, Expr* ret, ErrorCtx* errs) {
    if (!hop_start_if_ends_on_word_break(src, "return")) {
        return false;
    }
    hop_whitespace(src);
    if (!parse_expr(src, ret, errs)) {
        *ret = Expr_default;
    }
    return true;
}

static bool parse_function_statement(const char** src, FunctionStatement* ret, ErrorCtx* errs) {
    const char* s = *src;
    ErrorCtx local_errs = {0};

    if (!hop_start_and_required_whitespace(&s, "function")) {
        return false;
    }

    if (!parse_identifier(&s, &ret->name)) {
        ErrorCtx_parse_error(&local_errs, s, "missing function name");
    }
    hop_whitespace(&s);

    if (!parse_function(&s, false, &ret->impl, &local_errs)) {
        cstr_drop(&ret->name);
        ErrorCtx_drop(&local_errs);
        return false;
    }

    ErrorCtx_append_consume(errs, local_errs);
    *src = s;
    return true;
}

static bool parse_statement(const char** src, Statement* ret, ErrorCtx* errs) {
    hop_whitespace(src);
    LoopStatement loop;
    if (parse_named_interface(src, &ret->interface, errs)) {
        ret->tag = STATEMENT_INTERFACE;
        return true;
    } else if (parse_block(src, &ret->block, errs)) {
        ret->tag = STATEMENT_BLOCK;
        return true;
    } else if (parse_var_init(src, &ret->var_init, errs)) {
        ret->tag = STATEMENT_VAR_INIT;
        return true;
    } else if (parse_if_else(src, &ret->if_else, errs)) {
        ret->tag = STATEMENT_IF_ELSE;
        return true;
    } else if (parse_for_loop(src, &loop, errs)) {
        *ret = (Statement){
            .tag = STATEMENT_LOOP,
            .loop = c_new(LoopStatement, loop),
        };
        return true;
    } else if (parse_while_loop(src, &loop, errs)) {
        *ret = (Statement){
            .tag = STATEMENT_LOOP,
            .loop = c_new(LoopStatement, loop),
        };
        return true;
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "break")) {
        ret->tag = STATEMENT_BREAK;
        return true;
    } else if (parse_return(src, &ret->expr, errs)) {
        ret->tag = STATEMENT_RETURN;
        return true;
    } else if (parse_function_statement(src, &ret->function, errs)) {
        ret->tag = STATEMENT_FUNCTION;
        return true;
    } else if (parse_expr(src, &ret->expr, errs)) {
        ret->tag = STATEMENT_EXPR;
        return true;
    } else {
        return false;
    }
}

static void parse_block_inner(const char** src, Block* ret, ErrorCtx* errs) {
    const char* s = *src;
    vec_Statement statements = {0};
    hop_whitespace(&s);
    while(*s && *s != '}') {
        Statement statement = {0};
        if (*s == ';') {
            s++;
        } else if (parse_statement(&s, &statement, errs)) {
            vec_Statement_push(&statements, statement);
        } else {
            ErrorCtx_parse_error(errs, s, "invalid statement");
            while (*s && *s != '}' && *s != ';' && *s != '\n') { s++; }
        }
        hop_whitespace(&s);
    }
    *ret = (Block){
        .statements = statements,
    };
    *src = s;
}

static bool parse_block(const char** src, Block* ret, ErrorCtx* errs) {
    const char* s = *src;
    if (!hop_char(&s, '{')) return false;
    parse_block_inner(&s, ret, errs);
    hop_whitespace(&s);
    if (!hop_char(&s, '}')) {
        ErrorCtx_parse_error(errs, *src, "expected '}'");
    }
    *src = s;
    return true;
}

Block parse_file(const char* src, ErrorCtx* errs) {
    hop_whitespace(&src);
    Block scope;
    parse_block_inner(&src, &scope, errs);
    if (*src) {
        ErrorCtx_parse_error(errs, src, "unexpected '}'");
    }
    return scope;
}
