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
typedef struct Scope Scope;
typedef struct Generic Generic;
typedef struct CallExpr CallExpr;
typedef struct Expr Expr;
typedef struct LazyType LazyType;
typedef struct Declaration Declaration;
typedef struct Ty Ty; // A semantic type, see ty/ty.h
typedef Declaration const* DeclarationCPtr;

g_declare_arc(Expr, expr);

// What an AST node knows its declaration by. Not owning: symbols live as long as the program
typedef struct Symbol Symbol;
typedef struct DeclHandle {
    Declaration* get;
    Symbol* symbol;
} DeclHandle;
typedef Symbol const* SymbolCPtr;
g_declare_vec(SymbolCPtr, symbol_cptr);
g_declare_vec(DeclarationCPtr, declaration_cptr);
g_declare_vec(TupleElement, tuple_element);

typedef struct TypeNode {
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
            TypeNode* element;
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
        VecTypeNode composite; // used for both TYPE_UNION and TYPE_INTERSECTION
        LazyType* lazy;
        TypeNode const* ref;
    };
} TypeNode;

extern const TypeNode type_any;
extern const TypeNode type_self_referential_error;
extern const TypeNode type_unknown;
extern const TypeNode type_never;
extern const TypeNode type_void;
extern const TypeNode type_object;
extern const TypeNode type_boolean;
extern const TypeNode type_number;
extern const TypeNode type_string;
extern const TypeNode type_symbol;
extern const TypeNode type_bigint;
extern const TypeNode type_null;
extern const TypeNode type_undefined;

static inline TypeNode type_value(Value value) {
    return (TypeNode){
        .tag = TYPE_VALUE,
        .value = value,
    };
}

// type_identifier declared in several_types.h

static inline TypeNode type_inferred(InferredType* inferred) {
    return (TypeNode){
        .tag = TYPE_INFERRED,
        .inferred = inferred,
    };
}

static inline TypeNode type_callable(CallableType* callable) {
    return (TypeNode){
        .tag = TYPE_CALLABLE,
        .callable = callable,
    };
}

static inline TypeNode type_interface(Interface interface) {
    return (TypeNode){
        .tag = TYPE_INTERFACE,
        .interface = interface,
    };
}

static inline TypeNode type_array(TypeNode* element, bool readonly) {
    return (TypeNode){
        .tag = TYPE_ARRAY,
        .array = {
            .element = element,
            .readonly = readonly,
        }
    };
}

static inline TypeNode type_tuple(VecTupleElement elements, bool readonly) {
    return (TypeNode){
        .tag = TYPE_TUPLE,
        .tuple = {
            .elements = elements,
            .readonly = readonly,
        }
    };
}

static inline TypeNode type_indexed(IndexedType* indexed) {
    return (TypeNode){
        .tag = TYPE_INDEXED,
        .indexed = indexed,
    };
}

static inline TypeNode type_keyof(KeyofType* keyof) {
    return (TypeNode){
        .tag = TYPE_KEYOF,
        .keyof = keyof,
    };
}

static inline TypeNode type_typeof(TypeofType* typeof_) {
    return (TypeNode){
        .tag = TYPE_TYPEOF,
        .typeof_ = typeof_,
    };
}

static inline TypeNode type_ternary(TernaryType* ternary) {
    return (TypeNode){
        .tag = TYPE_TERNARY,
        .ternary = ternary,
    };
}

// type_composite() and type_composite_without_simplify() declared below

static inline TypeNode type_lazy(LazyType* lazy) {
    return (TypeNode){
        .tag = TYPE_LAZY,
        .lazy = lazy,
    };
}

static inline TypeNode type_ref_always(TypeNode const* ref) {
    return (TypeNode){
        .tag = TYPE_REF,
        .ref = ref,
    };
}

// Either clones or refs, whichevers cheaper (result never needs to be dropped)
static inline TypeNode type_refcopy(TypeNode const* ref) {
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

static inline bool type_is_self_referential(TypeNode const* me) {
    return my tag == TYPE_ANY && my any.self_referential_error;
}

TypeNode type_composite_without_simplify(typeof((TypeNode){}.tag) tag, TypeNode a, TypeNode b);
static inline TypeNode type_composite_base(typeof((TypeNode){}.tag) tag) {
    switch (tag) {
        case TYPE_UNION:        return (TypeNode){.tag = TYPE_NEVER};
        case TYPE_INTERSECTION: return (TypeNode){.tag = TYPE_ANY};
        default:                panic("Invalid composite tag");
    }
}

// PERF: should type_drop() be inlined?
void type_drop(TypeNode* me);
// PERF: should type_clone() be inlined?
TypeNode type_clone(TypeNode me);
static inline TypeNode type_move(TypeNode* me) {
    TypeNode ret = *me;
    my tag = TYPE_ANY;
    return ret;
}

void type_lookup_names(TypeNode* me, Scope* names, ErrorCtx* errs);
void type_typecheck(TypeNode const* me, ErrorCtx* errs);
#define g_class_key TypeNode, type
#define g_declared
#include <cleat/generic/vec.h>

typedef struct SpannedType {
    Span span;
    TypeNode type;
} SpannedType;
static inline SpannedType spanned_type(Span span, TypeNode type) { return (SpannedType){span, type}; }
static inline void spanned_type_drop(SpannedType* me) { type_drop(&my type); }
static inline SpannedType spanned_type_clone(SpannedType me) { return (SpannedType){.span = me.span, .type = type_clone(me.type)}; }

#define g_class_key SpannedType, spanned_type
#include <cleat/generic/vec.h>

static inline TypeNode const* generic_param_at(VecSpannedType const* params, intptr_t i) {
    return i < vec_spanned_type_size(params) ? &vec_spanned_type_at(params, i)->type : &type_any;
}

typedef enum KeywordIdentifier {
    KEYWORD_IDENTIFIER_NONE = 0,
    KEYWORD_IDENTIFIER_THIS,
    KEYWORD_IDENTIFIER_SUPER,
} KeywordIdentifier;
