#pragma once

#include "prelude.h"
#include "interface.h"

typedef struct TernaryType TernaryType;

typedef struct Type {
    enum {
        TYPE_ANY,
        TYPE_UNDEFINED,
        TYPE_NULL,
        TYPE_BOOLEAN,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_INTERFACE,
        TYPE_TERNARY,
    } tag;
    union {
        Interface interface;
        TernaryType* ternary;
    } data;
} Type;

#define TYPE_NEW_DECL(name, type) static inline Type type_##name##_new() { return (Type){ .tag = TYPE_##type }; }
TYPE_NEW_DECL(any, NULL)
TYPE_NEW_DECL(undefined, UNDEFINED)
TYPE_NEW_DECL(null, NULL)
TYPE_NEW_DECL(boolean, BOOLEAN)
TYPE_NEW_DECL(number, NUMBER)
TYPE_NEW_DECL(string, STRING)
#undef TYPE_NEW_DECL
static inline Type type_interface_new(Interface interface) {
    return (Type){
        .tag = TYPE_INTERFACE,
        .data.interface = interface,
    };
}
static inline Type type_ternary_new(TernaryType* ternary) {
    return (Type){
        .tag = TYPE_TERNARY,
        .data.ternary = ternary,
    };
}

void Type_drop(Type* self);
cstr Type_to_cstr(Type* self);

bool type_extends(Type *sub, Type *super);
Type type_simplify(Type t);
