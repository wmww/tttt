#pragma once

#include "prelude.h"
#include "interface.h"
#include "value.h"

typedef struct TernaryType TernaryType;

typedef struct Type {
    enum {
        TYPE_ANY = 0,
        TYPE_VOID,
        TYPE_BOOLEAN,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_VALUE,
        TYPE_IDENTIFIER,
        TYPE_INTERFACE,
        TYPE_TERNARY,
    } tag;
    union {
        Value value;
        cstr identifier;
        Interface interface;
        TernaryType* ternary;
    } data;
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
        .data.value = value,
    };
}
static inline Type Type_identifier(cstr identifier) {
    return (Type){
        .tag = TYPE_IDENTIFIER,
        .data.identifier = identifier,
    };
}
static inline Type Type_interface(Interface interface) {
    return (Type){
        .tag = TYPE_INTERFACE,
        .data.interface = interface,
    };
}
static inline Type Type_ternary(TernaryType* ternary) {
    return (Type){
        .tag = TYPE_TERNARY,
        .data.ternary = ternary,
    };
}

void Type_drop(Type* self);
void cstr_append_type_debug(cstr* target, Type* self, int indent);
Type Type_clone(Type self);
Type Type_move(Type* self);

bool Type_extends(Type const* sub, Type const* super);
