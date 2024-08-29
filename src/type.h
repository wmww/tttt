#pragma once

#include "interface.h"
#include "value.h"
#include "span.h"

typedef struct IdentifierType IdentifierType;
typedef struct InferredType InferredType;
typedef struct TernaryType TernaryType;
typedef struct IndexedType IndexedType;
typedef struct KeyofType KeyofType;
typedef struct AbstractedType AbstractedType;
typedef struct CallableType CallableType;
typedef struct TupleElement TupleElement;
typedef struct ErrorCtx ErrorCtx;
typedef struct NameStack NameStack;
typedef struct NamedType NamedType;
typedef struct Generic Generic;
typedef struct CallExpr CallExpr;
typedef struct Expr Expr;
typedef struct LazyType LazyType;

wrap_forward_arc(Expr, expr);
wrap_forward_vec(Type, type);
wrap_forward_arc(NamedType, named_type);
wrap_forward_vec(TupleElement, tuple_element);
wrap_forward_arc(InferredType, inferred_type);

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
        TYPE_INFERRED,
        TYPE_CALLABLE,
        TYPE_INTERFACE,
        TYPE_ARRAY,
        TYPE_TUPLE,
        TYPE_INDEXED,
        TYPE_KEYOF,
        TYPE_TERNARY,
        TYPE_ABSTRACTED,
        TYPE_UNION,
        TYPE_INTERSECTION,
        TYPE_LAZY,
        TYPE_REF,
    } tag;
    union {
        struct {
            bool self_referential_error;
            bool excessively_deep_error;
        } any;
        Value value;
        IdentifierType* identifier;
        ArcInferredType inferred;
        CallableType* callable;
        Interface interface;
        struct {
            Type* element;
            bool readonly;
        } array;
        struct {
            VecTupleElement elements;
            bool readonly;
        } tuple;
        IndexedType* indexed;
        KeyofType* keyof;
        TernaryType* ternary;
        AbstractedType* abstracted;
        VecType composite; // used for both TYPE_UNION and TYPE_INTERSECTION
        LazyType* lazy;
        Type const* ref;
    };
} Type;

extern const Type type_any;
extern const Type type_unknown;
extern const Type type_self_referential_error;
extern const Type type_excessively_deep_error;
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

// type_identifier declared in several_types.h

static inline Type type_inferred(ArcInferredType inferred) {
    return (Type){
        .tag = TYPE_INFERRED,
        .inferred = inferred,
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

static inline Type type_array(Type* element, bool readonly) {
    return (Type){
        .tag = TYPE_ARRAY,
        .array = {
            .element = element,
            .readonly = readonly,
        }
    };
}

static inline Type type_tuple(VecTupleElement elements, bool readonly) {
    return (Type){
        .tag = TYPE_TUPLE,
        .tuple = {
            .elements = elements,
            .readonly = readonly,
        }
    };
}

static inline Type type_indexed(IndexedType* indexed) {
    return (Type){
        .tag = TYPE_INDEXED,
        .indexed = indexed,
    };
}

static inline Type type_keyof(KeyofType* keyof) {
    return (Type){
        .tag = TYPE_KEYOF,
        .keyof = keyof,
    };
}

static inline Type type_ternary(TernaryType* ternary) {
    return (Type){
        .tag = TYPE_TERNARY,
        .ternary = ternary,
    };
}

// Often make_type_abstracted() should be used instead
static inline Type type_abstracted(AbstractedType* abstracted) {
    return (Type) {
        .tag = TYPE_ABSTRACTED,
        .abstracted = abstracted,
    };
}

// type_composite() and type_composite_without_simplify() declared below

static inline Type type_lazy(LazyType* lazy) {
    return (Type){
        .tag = TYPE_LAZY,
        .lazy = lazy,
    };
}

static inline Type type_ref(Type const* ref) {
    return (Type){
        .tag = TYPE_REF,
        .ref = ref,
    };
}

static inline bool type_is_self_referential(Type const* me) {
    return my tag == TYPE_ANY && my any.self_referential_error;
}

Type type_composite_base(typeof((Type){}.tag) tag);
Type type_composite_without_simplify(typeof((Type){}.tag) tag, Type a, Type b);
Type type_composite(typeof((Type){}.tag) tag, Type a, Type b);
void composite_type_simplify(Type* me);

void type_drop(Type* me);
Type type_clone(Type me);
void cstr_append_type_error_msg(cstr* target, Type const* me);
static inline Type type_move(Type* me) {
    Type ret = *me;
    my tag = TYPE_ANY;
    return ret;
}

void type_resolve_names(Type* me, NameStack* names, ErrorCtx* errs);
void type_typecheck(Type const* me, ErrorCtx* errs);
Type const* type_resolve(Type const* me);
Type const* type_resolve_one_layer(Type const* me);
Type const* type_resolve_until_identifier(Type const* me);
bool type_extends(Type const* sub, Type const* super);
bool type_overlaps(Type const* a, Type const* b);
bool type_extends_array(Type const* sub);
void type_abstract_literals(Type* me); // {a: 7} -> {a: number}
void type_remove_null_and_undefined(Type* me); // T | null | undefined -> T

// Returns true if found, false if not. ret is unmodified if false is returned. both ret and errors may be null.
bool index_type_of(
    Type const* me,
    Type const* index,
    Span index_span,
    bool error_if_not_found,
    ErrorCtx* errs,
    Type* ret
);
void check_assignable_or_error(Type const* source, Type const* target, Span span, ErrorCtx* errs);
Type type_typecheck_call(Type const* me, CallExpr const* call, Span span, ErrorCtx* errs);

#define i_key_class_names Type, type
#define i_is_forward
#include "wrap_stc/vec.h"

typedef struct SpannedType {
    Span span;
    Type type;
} SpannedType;
static inline SpannedType spanned_type(Span span, Type type) { return (SpannedType){span, type}; }
static inline void spanned_type_drop(SpannedType* me) { type_drop(&my type); }
static inline SpannedType spanned_type_clone(SpannedType me) { return (SpannedType){.span = me.span, .type = type_clone(me.type)}; }

#define i_key_class_names SpannedType, spanned_type
#include "wrap_stc/vec.h"

static inline Type const* generic_param_at(VecSpannedType const* params, intptr_t i) {
    return i < vec_spanned_type_size(params) ? &vec_spanned_type_at(params, i)->type : &type_unknown;
}
