#pragma once

#include "interface.h"
#include "value.h"
#include "span.h"

typedef struct IdentifierType IdentifierType;
typedef struct InferredType InferredType;
typedef struct TernaryType TernaryType;
typedef struct IndexedType IndexedType;
typedef struct KeyofType KeyofType;
typedef struct TypeofType TypeofType;
typedef struct CallableType CallableType;
typedef struct TupleElement TupleElement;
typedef struct ErrorCtx ErrorCtx;
typedef struct ScopeStack ScopeStack;
typedef struct Scope Scope;
typedef struct Generic Generic;
typedef struct CallExpr CallExpr;
typedef struct Expr Expr;
typedef struct LazyType LazyType;
typedef struct Declaration Declaration;
typedef struct Ty Ty; // A semantic type, see ty/ty.h
typedef Declaration const* DeclarationCPtr;

g_declare_arc(Expr, expr);
g_declare_arc(Declaration, declaration);
g_declare_vec(DeclarationCPtr, declaration_cptr);
g_declare_vec(TupleElement, tuple_element);

typedef struct Type {
    enum {
        TYPE_ANY = 0,
        TYPE_UNKNOWN,
        TYPE_NEVER,
        TYPE_VOID,
        TYPE_BOOLEAN,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_SYMBOL,
        TYPE_BIGINT,
        TYPE_VALUE,
        TYPE_IDENTIFIER,
        TYPE_INFERRED,
        TYPE_CALLABLE,
        TYPE_INTERFACE,
        TYPE_ARRAY,
        TYPE_TUPLE,
        TYPE_INDEXED,
        TYPE_KEYOF,
        TYPE_TYPEOF,
        TYPE_TERNARY,
        TYPE_UNION,
        TYPE_INTERSECTION,
        TYPE_LAZY,
        TYPE_REF,
    } tag;
    bool parsed : 1;
    union {
        struct {
            bool self_referential_error;
            Span unkown_symbol_span; // Used by tests
        } any;
        Value value;
        IdentifierType* identifier;
        InferredType* inferred;
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
        TypeofType* typeof_;
        TernaryType* ternary;
        VecType composite; // used for both TYPE_UNION and TYPE_INTERSECTION
        LazyType* lazy;
        Type const* ref;
    };
} Type;

extern const Type type_any;
extern const Type type_self_referential_error;
extern const Type type_unknown;
extern const Type type_never;
extern const Type type_void;
extern const Type type_object;
extern const Type type_boolean;
extern const Type type_number;
extern const Type type_string;
extern const Type type_symbol;
extern const Type type_bigint;
extern const Type type_null;
extern const Type type_undefined;

static inline Type type_value(Value value) {
    return (Type){
        .tag = TYPE_VALUE,
        .value = value,
    };
}

// type_identifier declared in several_types.h

static inline Type type_inferred(InferredType* inferred) {
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

static inline Type type_typeof(TypeofType* typeof_) {
    return (Type){
        .tag = TYPE_TYPEOF,
        .typeof_ = typeof_,
    };
}

static inline Type type_ternary(TernaryType* ternary) {
    return (Type){
        .tag = TYPE_TERNARY,
        .ternary = ternary,
    };
}

// type_composite() and type_composite_without_simplify() declared below

static inline Type type_lazy(LazyType* lazy) {
    return (Type){
        .tag = TYPE_LAZY,
        .lazy = lazy,
    };
}

static inline Type type_ref_always(Type const* ref) {
    return (Type){
        .tag = TYPE_REF,
        .ref = ref,
    };
}

// Either clones or refs, whichevers cheaper (result never needs to be dropped)
static inline Type type_refcopy(Type const* ref) {
    switch (ref->tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_LAZY:
        case TYPE_REF:
            return *ref;

        case TYPE_VALUE:
        case TYPE_IDENTIFIER:
        case TYPE_INFERRED:
        case TYPE_CALLABLE:
        case TYPE_INTERFACE:
        case TYPE_ARRAY:
        case TYPE_TUPLE:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TYPEOF:
        case TYPE_TERNARY:
        case TYPE_INTERSECTION:
        case TYPE_UNION:
            break;
    }
    return type_ref_always(ref);
}

static inline bool type_is_self_referential(Type const* me) {
    return my tag == TYPE_ANY && my any.self_referential_error;
}

Type type_composite_without_simplify(typeof((Type){}.tag) tag, Type a, Type b);
static inline Type type_composite_base(typeof((Type){}.tag) tag) {
    switch (tag) {
        case TYPE_UNION:        return (Type){.tag = TYPE_NEVER};
        case TYPE_INTERSECTION: return (Type){.tag = TYPE_ANY};
        default:                panic("Invalid composite tag");
    }
}

// PERF: should type_drop() be inlined?
void type_drop(Type* me);
// PERF: should type_clone() be inlined?
Type type_clone(Type me);
static inline Type type_move(Type* me) {
    Type ret = *me;
    my tag = TYPE_ANY;
    return ret;
}

void type_lookup_names(Type* me, ScopeStack* names, ErrorCtx* errs);
void type_typecheck(Type const* me, ErrorCtx* errs);
#define g_class_key Type, type
#define g_declared
#include <cleat/generic/vec.h>

typedef struct SpannedType {
    Span span;
    Type type;
} SpannedType;
static inline SpannedType spanned_type(Span span, Type type) { return (SpannedType){span, type}; }
static inline void spanned_type_drop(SpannedType* me) { type_drop(&my type); }
static inline SpannedType spanned_type_clone(SpannedType me) { return (SpannedType){.span = me.span, .type = type_clone(me.type)}; }

#define g_class_key SpannedType, spanned_type
#include <cleat/generic/vec.h>

static inline Type const* generic_param_at(VecSpannedType const* params, intptr_t i) {
    return i < vec_spanned_type_size(params) ? &vec_spanned_type_at(params, i)->type : &type_any;
}

typedef enum KeywordIdentifier {
    KEYWORD_IDENTIFIER_NONE = 0,
    KEYWORD_IDENTIFIER_THIS,
    KEYWORD_IDENTIFIER_SUPER,
} KeywordIdentifier;
