#pragma once

#include "interface.h"
#include "value.h"
#include "span.h"

typedef struct TernaryType TernaryType;
typedef struct CallableType CallableType;
typedef struct Generic Generic;
typedef struct ErrorCtx ErrorCtx;
typedef struct NameStack NameStack;

typedef struct Type {
    enum {
        TYPE_ANY = 0,
        TYPE_UNKOWN,
        TYPE_NEVER,
        TYPE_VOID,
        TYPE_BOOLEAN,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_VALUE,
        TYPE_IDENTIFIER,
        TYPE_GENERIC,
        TYPE_CALLABLE,
        TYPE_INTERFACE,
        TYPE_ARRAY,
        TYPE_TERNARY,
    } tag;
    union {
        Value value;
        struct {
            cstr name;
            Type const* type;
            // 0: this identifier is not a generic item
            // 1: this identifier is the generic_indexth item of the first generic above this type
            // 2+: there are generics between this type and the generic it is an item of
            int generic_depth;
            // Only used if generic_depth != 0
            int generic_index;
            Span span;
        } identifier;
        Generic* generic;
        CallableType* callable;
        Interface interface;
        Type* array;
        TernaryType* ternary;
    };
} Type;

extern const Type type_any;
extern const Type type_unkown;
extern const Type type_never;
extern const Type type_void;
extern const Type type_boolean;
extern const Type type_number;
extern const Type type_string;
#undef TYPE_NEW_DECL

static inline Type type_value(Value value) {
    return (Type){
        .tag = TYPE_VALUE,
        .value = value,
    };
}

static inline Type type_identifier(cstr identifier, Type const* type, int generic_depth, int generic_index, Span span) {
    return (Type){
        .tag = TYPE_IDENTIFIER,
        .identifier.name = identifier,
        .identifier.type = type,
        .identifier.generic_depth = generic_depth,
        .identifier.generic_index = generic_index,
        .identifier.span = span,
    };
}

static inline Type type_generic(Generic* generic) {
    return (Type){
        .tag = TYPE_GENERIC,
        .generic = generic,
    };
}

static inline Type type_callable(CallableType* callable) {
    return (Type){
        .tag = TYPE_CALLABLE,
        .callable = callable,
    };
}

static inline Type type_interface(Interface interface) {
    return (Type){
        .tag = TYPE_INTERFACE,
        .interface = interface,
    };
}

static inline Type type_array(Type* member) {
    return (Type){
        .tag = TYPE_ARRAY,
        .array = member,
    };
}

static inline Type type_ternary(TernaryType* ternary) {
    return (Type){
        .tag = TYPE_TERNARY,
        .ternary = ternary,
    };
}

void type_drop(Type* me);
void cstr_append_type_debug(cstr* target, Type const* me, int indent);
void cstr_append_type_error_msg(cstr* target, Type const* me);
Type type_clone(Type me);
Type type_move(Type* me);

void type_resolve_names(Type* me, NameStack* names, ErrorCtx* errs);
bool type_extends(Type const* sub, Type const* super);
void type_abstract_literals(Type* me); // {a: 7} -> {a: number}

void check_assignable_or_error(Type const* source, Type const* target, Span span, ErrorCtx* errs);

#define i_key_class_names Type, type
#include "wrap_stc/vec.h"
