#include "parser.h"
#include "scope.h"
#include "interface.h"
#include "interface_member.h"
#include "statement.h"
#include "ternary_type.h"
#include "extends_conditional.h"
#include "type.h"
#include "error.h"

static bool parse_scope(const char** src, Scope* ret, ErrorCtx* errs);
static bool parse_interface(const char** src, Interface* ret, ErrorCtx* errs);
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
        if (!callback(src, data, errs)) {
            ErrorCtx_parse_error(errs, *src, "invald item in sequence");
            while (**src && **src != end && char_is_sep(sep, **src)) {
                (*src)++;
            }
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
    } else if (parse_type_compat_literal(src, &type.data.value, errs)) {
        type.tag = TYPE_VALUE;
    } else if (parse_interface(src, &type.data.interface, errs)) {
        type.tag = TYPE_INTERFACE;
    } else {
        ErrorCtx_parse_error(errs, *src, "did not find type");
        return false;
    }
    TernaryType ternary;
    if (parse_ternary_type(src, type, &ternary, errs)) {
        *ret = Type_ternary_new(c_new(TernaryType, ternary));
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

static bool parse_named_interface(const char** src, NamedInterface* ret, ErrorCtx* errs) {
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
    *ret = (NamedInterface) {
        .name = name,
        .content = interface,
    };
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
        *op = BINARY_OP_TRIPLE_EQUALS;
    } else if (hop_start(src, "==")) {
        *op = BINARY_OP_DOUBLE_EQUALS;
    } else if (hop_start(src, "=")) {
        *op = BINARY_OP_SINGLE_EQUALS;
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
    if (hop_start_if_ends_on_word_break(src, "null")) {
        *ret = Value_null_new();
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "undefined")) {
        *ret = Value_undefined_new();
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "true")) {
        *ret = Value_boolean_new(true);
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "false")) {
        *ret = Value_boolean_new(false);
        return true;
    } else if (parse_number(src, &ret->data.number, errs)) {
        ret->tag = VALUE_NUMBER;
        return true;
    } else if (parse_string(src, &ret->data.string, errs)) {
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
    if (parse_literal(src, &ret->data.literal, errs)) {
        ret->tag = EXPR_LITERAL;
    } else if (parse_object(src, &ret->data.object, errs)) {
        ret->tag = EXPR_OBJECT;
    } else if (parse_left_unary_op_expr(src, ret, errs)) {
    } else if (parse_identifier(src, &name)) {
        *ret = Expr_indentifier_new(name);
    } else if (hop_start(src, "(")) {
        parse_expr(src, ret, errs);
        if (!hop_start(src, ")")) {
            ErrorCtx_parse_error(errs, *src, "expected ')'");
        }
        switch (ret->tag) {
            case EXPR_BINARY_OP: ret->data.binary_op->parenthesized = true; break;
            case EXPR_UNARY_OP: ret->data.unary_op->parenthesized = true; break;
            default: break;
        }
    } else {
        return false;
    }
    hop_whitespace(src);

    // Function call
    if (hop_start(src, "(")) {
        vec_Expr args = {0};
        hop_whitespace(src);
        while (**src && **src != ')') {
            Expr arg = {0};
            if (parse_expr(src, &arg, errs)) {
                vec_Expr_push(&args, arg);
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
        *ret = Expr_call_new(c_new(CallExpr, {
            .callable = *ret,
            .args = args,
        }));
    }

    UnaryOp right_unary;
    while (parse_right_unary_op_expr(src, &right_unary, errs)) {
        *ret = Expr_unary_op_new(c_new(UnaryOpExpr, {
            .op = right_unary,
            .expr = *ret,
        }));
    }

    Expr right = Expr_default;
    BinaryOp binary = 0;
    while (parse_binary_op_expr(src, &binary, &right, errs)) {
        if (right.tag == EXPR_BINARY_OP &&
            !right.data.binary_op->parenthesized &&
            BinaryOp_cmp_precedence(binary, right.data.binary_op->op) >= 0
        ) {
            right.data.binary_op->left = Expr_binary_op_new(c_new(BinaryOpExpr, {
                .left = *ret,
                .op = binary,
                .right = right.data.binary_op->left,
            }));
            *ret = right;
        } else {
            *ret = Expr_binary_op_new(c_new(BinaryOpExpr, {
                .left = *ret,
                .op = binary,
                .right = right,
            }));
        }
    }

    return true;
}

static bool parse_name_init(const char** src, NameInitData* ret, ErrorCtx* errs) {
    const char* s = *src;
    cstr name = cstr_null;
    if (!parse_identifier(&s, &name)) {
        ErrorCtx_parse_error(errs, *src, "missing name");
    }
    hop_whitespace(&s);
    Type type = {0};
    bool has_type = false;
    if (hop_start_if_ends_on_word_break(&s, ":")) {
        hop_whitespace(&s);
        if (parse_type(&s, &type, errs)) {
            has_type = true;
        }
        hop_whitespace(&s);
    }
    Expr value = {0};
    bool has_value = false;
    if (hop_start_if_ends_on_word_break(&s, "=")) {
        hop_whitespace(&s);
        if (parse_expr(&s, &value, errs)) {
            has_value = true;
        }
    }
    *ret = (NameInitData) {
        .name = name,
        .has_type = has_type,
        .type = type,
        .has_value = has_value,
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

static bool parse_function_arg(const char** src, void* data, ErrorCtx* errs) {
    vec_FunctionStatementArg* args = data;
    FunctionStatementArg arg = {0};
    if (!parse_name_init(src, &arg.data, errs)) {
        return false;
    }
    vec_FunctionStatementArg_push(args, arg);
    return true;
}

static bool parse_function(const char** src, FunctionStatement* ret, ErrorCtx* errs) {
    const char* s = *src;
    ErrorCtx local_errs = {0};
    if (!hop_start_and_required_whitespace(&s, "function")) {
        return false;
    }
    cstr name = cstr_null;
    if (!parse_identifier(&s, &name)) {
        ErrorCtx_parse_error(&local_errs, s, "missing function name");
    }
    hop_whitespace(&s);
    vec_FunctionStatementArg args = {0};
    if (!parse_sequence(&s, "(", SEQUENCE_SEP_COMMA, ')', parse_function_arg, &args, &local_errs)) {
        ErrorCtx_drop(&local_errs);
        cstr_drop(&name);
        return false;
    }
    hop_whitespace(&s);
    bool has_return_type = false;
    Type return_type = Type_any;
    if (hop_start(&s, ":")) {
        hop_whitespace(&s);
        if (parse_type(&s, &return_type, &local_errs)) {
            has_return_type = true;
        } else {
            ErrorCtx_parse_error(&local_errs, s, "invalid return type");
        }
    }
    hop_whitespace(&s);
    Scope body = {0};
    if (!parse_scope(&s, &body, &local_errs)) {
        ErrorCtx_parse_error(&local_errs, s, "invalid function body");
    }
    *ret = (FunctionStatement) {
        .name = name,
        .args = args,
        .has_return_type = has_return_type,
        .return_type = return_type,
        .body = body,
    };
    ErrorCtx_append_consume(errs, local_errs);
    *src = s;
    return true;
}

static bool parse_statement(const char** src, Statement* ret, ErrorCtx* errs) {
    hop_whitespace(src);
    if (parse_named_interface(src, &ret->data.interface, errs)) {
        ret->tag = STATEMENT_INTERFACE;
        return true;
    } else if (parse_scope(src, &ret->data.scope, errs)) {
        ret->tag = STATEMENT_SCOPE;
        return true;
    } else if (parse_var_init(src, &ret->data.var_init, errs)) {
        ret->tag = STATEMENT_VAR_INIT;
        return true;
    } else if (parse_function(src, &ret->data.function, errs)) {
        ret->tag = STATEMENT_FUNCTION;
        return true;
    } else if (parse_expr(src, &ret->data.expr, errs)) {
        ret->tag = STATEMENT_EXPR;
        return true;
    } else {
        return false;
    }
}

static void parse_scope_inner(const char** src, Scope* ret, ErrorCtx* errs) {
    const char* s = *src;
    vec_Statement statements = {0};
    hop_whitespace(&s);
    while(*s && *s != '}') {
        Statement statement;
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
    *ret = (Scope){
        .statements = statements,
    };
    *src = s;
}

static bool parse_scope(const char** src, Scope* ret, ErrorCtx* errs) {
    const char* s = *src;
    if (!hop_char(&s, '{')) return false;
    parse_scope_inner(&s, ret, errs);
    hop_whitespace(&s);
    if (!hop_char(&s, '}')) {
        ErrorCtx_parse_error(errs, *src, "expected '}'");
    }
    *src = s;
    return true;
}

Scope parse_file(const char* src, ErrorCtx* errs) {
    hop_whitespace(&src);
    Scope scope;
    parse_scope_inner(&src, &scope, errs);
    if (*src) {
        ErrorCtx_parse_error(errs, src, "unexpected '}'");
    }
    return scope;
}
