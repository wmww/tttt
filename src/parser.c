#include "parser.h"
#include "scope.h"
#include "interface.h"
#include "interface_member.h"
#include "statement.h"
#include "ternary_type.h"
#include "extends_conditional.h"
#include "type.h"

static bool parse_scope(const char** src, Scope* ret);
static bool parse_interface(const char** src, Interface* ret);
static bool parse_type(const char** src, Type* type);

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

static bool parse_ternary_type(const char** src, TernaryType* ret) {
    const char* s = *src;
    Type maybeSub = {0};
    Type maybeSuper = {0};
    Type trueType = {0};
    Type falseType = {0};
    if (!hop_start_if_ends_on_word_break(&s, "extends")) {
        goto fail;
    }
    hop_whitespace(&s);
    if (!parse_type(&s, &maybeSub)) {
        goto fail;
    }
    hop_whitespace(&s);
    if (!parse_type(&s, &maybeSuper)) {
        goto fail;
    }
    hop_whitespace(&s);
    if (!hop_start(&s, "?")) {
        goto fail;
    }
    hop_whitespace(&s);
    if (!parse_type(&s, &trueType)) {
        goto fail;
    }
    hop_whitespace(&s);
    if (!hop_start(&s, ":")) {
        goto fail;
    }
    hop_whitespace(&s);
    if (!parse_type(&s, &falseType)) {
        goto fail;
    }
    *ret = (TernaryType){
        .conditional = (ExtendsConditional){
            .maybeSub = maybeSub,
            .maybeSuper = maybeSuper,
        },
        .trueType = trueType,
        .falseType = falseType,
    };
    *src = s;
    return true;

fail:
    Type_drop(&maybeSub);
    Type_drop(&maybeSuper);
    Type_drop(&trueType);
    Type_drop(&falseType);
    return false;
}

static bool parse_type(const char** src, Type* type) {
    TernaryType ternary;
    if (hop_start_if_ends_on_word_break(src, "any")) {
        *type = type_any_new();
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "undefined")) {
        *type = type_undefined_new();
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "null")) {
        *type = type_null_new();
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "boolean")) {
        *type = type_boolean_new();
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "number")) {
        *type = type_number_new();
        return true;
    } else if (hop_start_if_ends_on_word_break(src, "string")) {
        *type = type_string_new();
        return true;
    } else if (parse_interface(src, &type->data.interface)) {
        type->tag = TYPE_INTERFACE;
        return true;
    } else if (parse_ternary_type(src, &ternary)) {
        *type = type_ternary_new(c_new(TernaryType, ternary));
        return true;
    } else {
        return false;
    }
}

static bool parse_interface(const char** src, Interface* ret) {
    const char* s = *src;
    if (!hop_char(&s, '{')) {
        return false;
    }
    vec_InterfaceMember members = {0};
    hop_whitespace(&s);
    while (*s && *s != '}') {
        hop_whitespace(&s);
        cstr name = cstr_null;
        if (!parse_identifier(&s, &name)) {
            vec_InterfaceMember_drop(&members);
            return false;
        }
        hop_whitespace(&s);
        if (!hop_char(&s, ':')) {
            vec_InterfaceMember_drop(&members);
            cstr_drop(&name);
            return false;
        }
        hop_whitespace(&s);
        Type type;
        if (!parse_type(&s, &type)) {
            vec_InterfaceMember_drop(&members);
            cstr_drop(&name);
            return false;
        }
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
    if (hop_char(&s, '}')) {
        *ret = (Interface) {
            .members = members,
        };
        *src = s;
        return true;
    } else {
        vec_InterfaceMember_drop(&members);
        return false;
    }
}

static bool parse_named_interface(const char** src, NamedInterface* ret) {
    const char* s = *src;
    if (!hop_start_and_required_whitespace(&s, "interface")) {
        return false;
    }
    cstr name;
    if (!parse_identifier(&s, &name)) {
        return false;
    }
    hop_whitespace(&s);
    Interface interface;
    if (!parse_interface(&s, &interface)) {
        cstr_drop(&name);
        return false;
    }
    *ret = (NamedInterface) {
        .name = name,
        .content = interface,
    };
    *src = s;
    return true;
}

static bool parse_statement(const char** src, Statement* ret) {
    hop_whitespace(src);
    if (parse_named_interface(src, &ret->data.interface)) {
        ret->tag = STATEMENT_INTERFACE;
        return true;
    } else if (parse_scope(src, &ret->data.scope)) {
        ret->tag = STATEMENT_SCOPE;
        return true;
    } else {
        return false;
    }
}

static bool parse_scope_inner(const char** src, Scope* ret) {
    const char* s = *src;
    vec_Statement statements = {0};
    hop_whitespace(&s);
    while(*s && *s != '}') {
        Statement statement;
        if (!parse_statement(&s, &statement)) {
            vec_Statement_drop(&statements);
            return false;
        }
        vec_Statement_push(&statements, statement);
        hop_whitespace(&s);
    }
    *ret = (Scope){
        .statements = statements,
    };
    *src = s;
    return true;
}

static bool parse_scope(const char** src, Scope* ret) {
    const char* s = *src;
    if (!hop_char(&s, '{')) return false;
    if (!parse_scope_inner(&s, ret)) {
        return false;
    }
    hop_whitespace(&s);
    if (!hop_char(&s, '}')) {
        Scope_drop(ret);
        return false;
    }
    *src = s;
    return true;
}

Scope parse_file(const char* src) {
    Scope scope;
    hop_whitespace(&src);
    if (!parse_scope_inner(&src, &scope)) {
        PANIC("parse error");
    }
    if (*src) {
        PANIC("unexpected }");
    }
    return scope;
}
