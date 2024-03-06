#pragma once

#include "prelude.h"
#include "interface.h"
#include "value.h"

typedef struct TernaryType TernaryType;
typedef struct CallableType CallableType;
typedef struct Generic Generic;
typedef struct ErrorCtx ErrorCtx;
typedef struct NameStack NameStack;

typedef struct Type {
    enum {
        TYPE_ANY = 0,
        TYPE_VOID,
        TYPE_BOOLEAN,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_VALUE,
        TYPE_IDENTIFIER,
        TYPE_GENERIC,
        TYPE_CALLABLE,
        TYPE_INTERFACE,
        TYPE_TERNARY,
    } tag;
    union {
        Value value;
        struct {
            cstr name;
            Type const* type;
        } identifier;
        Generic* generic;
        Interface interface;
        CallableType* callable;
        TernaryType* ternary;
    };
} Type;

extern const Type Type_any;
extern const Type Type_void;
extern const Type Type_boolean;
extern const Type Type_number;
extern const Type Type_string;
#undef TYPE_NEW_DECL
static inline Type Type_value(Value value) {
    return (Type){
        .tag = TYPE_VALUE,
        .value = value,
    };
}
static inline Type Type_identifier(cstr identifier, Type const* type) {
    return (Type){
        .tag = TYPE_IDENTIFIER,
        .identifier.name = identifier,
        .identifier.type = type,
    };
}
static inline Type Type_generic(Generic* generic) {
    return (Type){
        .tag = TYPE_GENERIC,
        .generic = generic,
    };
}
static inline Type Type_callable(CallableType* callable) {
    return (Type){
        .tag = TYPE_CALLABLE,
        .callable = callable,
    };
}
static inline Type Type_interface(Interface interface) {
    return (Type){
        .tag = TYPE_INTERFACE,
        .interface = interface,
    };
}
static inline Type Type_ternary(TernaryType* ternary) {
    return (Type){
        .tag = TYPE_TERNARY,
        .ternary = ternary,
    };
}

void Type_drop(Type* self);
void cstr_append_type_debug(cstr* target, Type* self, int indent);
Type Type_clone(Type self);
Type Type_move(Type* self);

void Type_resolve_names(Type* self, NameStack* names, ErrorCtx* errs);
bool Type_extends(Type const* sub, Type const* super);
