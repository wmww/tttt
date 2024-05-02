#pragma once

#include "interface.h"
#include "value.h"
#include "span.h"

typedef struct TernaryType TernaryType;
typedef struct IndexedType IndexedType;
typedef struct CallableType CallableType;
typedef struct ErrorCtx ErrorCtx;
typedef struct NameStack NameStack;
typedef struct NamedType NamedType;
typedef struct Generic Generic;
typedef struct CallExpr CallExpr;

wrap_forward_arc(NamedType, named_type);
wrap_forward_vec(Type, type);

typedef struct Type {
    enum {
        TYPE_ANY = 0,
        TYPE_UNKNOWN,
        TYPE_NEVER,
        TYPE_VOID,
        TYPE_BOOLEAN,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_VALUE,
        TYPE_IDENTIFIER,
        TYPE_CALLABLE,
        TYPE_INTERFACE,
        TYPE_ARRAY,
        TYPE_TUPLE,
        TYPE_INDEXED,
        TYPE_TERNARY,
        TYPE_UNION,
        TYPE_INTERSECTION,
    } tag;
    union {
        Value value;
        struct {
            cstr name;
            Span span;
            ArcNamedType type;
            VecType generic_params;
        } identifier;
        CallableType* callable;
        Interface interface;
        Type* array;
        VecType tuple;
        IndexedType* indexed;
        TernaryType* ternary;
        VecType composite; // used for both TYPE_UNION and TYPE_INTERSECTION
    };
} Type;

extern const Type type_any;
extern const Type type_unknown;
extern const Type type_never;
extern const Type type_void;
extern const Type type_object;
extern const Type type_boolean;
extern const Type type_number;
extern const Type type_string;
extern const Type type_null;
extern const Type type_undefined;
#undef TYPE_NEW_DECL

static inline Type type_value(Value value) {
    return (Type){
        .tag = TYPE_VALUE,
        .value = value,
    };
}

static inline Type type_identifier(cstr identifier, Span span) {
    return (Type){
        .tag = TYPE_IDENTIFIER,
        .identifier.name = identifier,
        .identifier.span = span,
    };
}

static inline Type type_identifier_full(
    cstr identifier,
    Span span,
    ArcNamedType type,
    VecType generic_params
) {
    return (Type){
        .tag = TYPE_IDENTIFIER,
        .identifier.name = identifier,
        .identifier.span = span,
        .identifier.type = type,
        .identifier.generic_params = generic_params,
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

static inline Type type_tuple(VecType members) {
    return (Type){
        .tag = TYPE_TUPLE,
        .tuple = members,
    };
}

static inline Type type_indexed(IndexedType* indexed) {
    return (Type){
        .tag = TYPE_INDEXED,
        .indexed = indexed,
    };
}

static inline Type type_ternary(TernaryType* ternary) {
    return (Type){
        .tag = TYPE_TERNARY,
        .ternary = ternary,
    };
}

Type type_composite_without_simplify(Type a, Type b, typeof((Type){}.tag) tag);
Type type_composite(Type a, Type b, typeof((Type){}.tag) tag);

void type_drop(Type* me);
void cstr_append_type_debug(cstr* target, Type const* me, int indent);
void cstr_append_type_error_msg(cstr* target, Type const* me);
Type type_clone(Type me);
static inline Type type_move(Type* me) {
    Type ret = *me;
    my tag = TYPE_ANY;
    return ret;
}
// Returns the non-empty generic of the toplevel type, or null
Generic* type_get_generic(Type const* me);

void type_resolve_names(Type* me, NameStack* names, ErrorCtx* errs);
bool type_extends(Type const* sub, Type const* super);
bool type_extends_array(Type const* sub);
void type_abstract_tuple_to_array(Type* me); // [number | string] -> (number | string)[]
void type_abstract_literals(Type* me); // {a: 7} -> {a: number}

Type array_type_of(Type value);
Type property_type_of(Type value, Type prop, Span prop_span, ErrorCtx* errs);
void check_assignable_or_error(Type const* source, Type const* target, Span span, ErrorCtx* errs);
Type type_typecheck_call(Type const* me, CallExpr const* call, Span span, ErrorCtx* errs, int level);

#define i_key_class_names Type, type
#define i_is_forward
#include "wrap_stc/vec.h"
