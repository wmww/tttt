#pragma once

#include "prelude.h"
#include "interface.h"
#include "value.h"

typedef struct TernaryType TernaryType;

typedef struct Type {
    enum {
        TYPE_ANY = 0,
        TYPE_BOOLEAN,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_VALUE,
        TYPE_INTERFACE,
        TYPE_TERNARY,
    } tag;
    union {
        Value value;
        Interface interface;
        TernaryType* ternary;
    } data;
} Type;

extern const Type Type_any;
extern const Type Type_boolean;
extern const Type Type_number;
extern const Type Type_string;
#undef TYPE_NEW_DECL
static inline Type Type_value_new(Value value) {
    return (Type){
        .tag = TYPE_VALUE,
        .data.value = value,
    };
}
static inline Type Type_interface_new(Interface interface) {
    return (Type){
        .tag = TYPE_INTERFACE,
        .data.interface = interface,
    };
}
static inline Type Type_ternary_new(TernaryType* ternary) {
    return (Type){
        .tag = TYPE_TERNARY,
        .data.ternary = ternary,
    };
}

void Type_drop(Type* self);
cstr Type_to_cstr(Type* self, int indent);
Type Type_move(Type* self);

bool Type_extends(Type const* sub, Type const* super);
