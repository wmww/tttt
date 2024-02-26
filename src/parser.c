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
    while (is_identifier_char(**src)) (*src)++;
    if (start == *src) {
        return false;
    } else {
        *name = cstr_from_n(start, *src - start);
        return true;
    }
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
        ErrorCtx_error(errs, *src, "expected '?'");
    }
    hop_whitespace(&s);
    parse_type(&s, &trueType, errs);
    hop_whitespace(&s);
    if (!hop_start(&s, ":")) {
        ErrorCtx_error(errs, *src, "expected ':'");
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
        type = type_any_new();
    } else if (hop_start_if_ends_on_word_break(src, "undefined")) {
        type = type_undefined_new();
    } else if (hop_start_if_ends_on_word_break(src, "null")) {
        type = type_null_new();
    } else if (hop_start_if_ends_on_word_break(src, "boolean")) {
        type = type_boolean_new();
    } else if (hop_start_if_ends_on_word_break(src, "number")) {
        type = type_number_new();
    } else if (hop_start_if_ends_on_word_break(src, "string")) {
        type = type_string_new();
    } else if (parse_interface(src, &type.data.interface, errs)) {
        type.tag = TYPE_INTERFACE;
    } else {
        ErrorCtx_error(errs, *src, "did not find type");
        return false;
    }
    TernaryType ternary;
    if (parse_ternary_type(src, type, &ternary, errs)) {
        *ret = type_ternary_new(c_new(TernaryType, ternary));
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
            ErrorCtx_error(errs, *src, "expected interface member name");
        }
        hop_whitespace(&s);
        if (!hop_char(&s, ':')) {
            ErrorCtx_error(errs, *src, "expected ':'");
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
        ErrorCtx_error(errs, *src, "expected '}'");
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
        ErrorCtx_error(&local_errs, *src, "interface does not have a name");
    }
    hop_whitespace(&s);
    Interface interface;
    if (!parse_interface(&s, &interface, errs)) {
        ErrorCtx_drop(&local_errs);
        cstr_drop(&name);
        return false;
    }
    ErrorCtx_append(errs, local_errs);
    *ret = (NamedInterface) {
        .name = name,
        .content = interface,
    };
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
            ErrorCtx_error(errs, *src, "invalid statement");
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
        ErrorCtx_error(errs, *src, "expected '}'");
    }
    *src = s;
    return true;
}

bool parse_file(const char* src, Scope* scope) {
    ErrorCtx errs = {0};
    hop_whitespace(&src);
    parse_scope_inner(&src, scope, &errs);
    if (*src) {
        ErrorCtx_error(&errs, src, "unexpected '}'");
    }
    if (ErrorCtx_empty(&errs)) {
        return true;
    } else {
        ErrorCtx_display(&errs);
        ErrorCtx_drop(&errs);
        Scope_drop(scope);
        return false;
    }
}
