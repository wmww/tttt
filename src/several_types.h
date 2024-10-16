#pragma once

#include "type.h"
#include "lazy_type.h"

// Identifier

// PERF: maybe split this into a simple version that fits in a type by value and a complex version with namespaces/
// generic params/etc
typedef struct IdentifierType {
    VecSpan namespaces;
    Span name;
    KeywordIdentifier keyword;
    Declaration const* decl_ref;
    VecSpannedType generic_params;
    LazyType type; // May contain a copy of the type with the generic parameters swapped in or a reference
} IdentifierType;

void identifier_type_drop(IdentifierType* me);
IdentifierType identifier_type_clone(IdentifierType me);
void identifier_type_lookup_names(IdentifierType* me, ScopeStack* names, ErrorCtx* errs);
Type identifier_type_evaluate_type(IdentifierType* me);
void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs);

static inline Type type_identifier(IdentifierType* identifier) {
    if (identifier->type.tag != LAZY_TYPE_EVALUATED) {
        identifier->type.tag = LAZY_TYPE_IDENTIFIER_TYPE;
    }
    return (Type){
        .tag = TYPE_IDENTIFIER,
        .identifier = identifier,
    };
}

// Inferred identifier

typedef struct InferredType {
    Span name;
    Span full_span;
    Type extends;
    ArcDeclaration owned_declaration;
    Declaration const* declaration_ref;
} InferredType;

void inferred_type_drop(InferredType* me);
InferredType inferred_type_clone(InferredType me);

// Tuple

typedef struct TupleElement {
    Type type;
    bool rest;
    bool readonly;
} TupleElement;

static inline void tuple_element_drop(TupleElement* me) { type_drop(&my type); }
static inline TupleElement tuple_element_clone(TupleElement me) { me.type = type_clone(me.type); return me; }

#define i_key_class_names TupleElement, tuple_element
#define i_is_forward
#include "wrap_stc/vec.h"

// Index

typedef struct IndexedType {
    Type interface;
    Type index;
    bool is_from_binding;
    bool array_only;
    bool has_cache;
    Type cached; // Should be {0} if has_cache is false
} IndexedType;

void indexed_type_drop(IndexedType* me);
IndexedType indexed_type_clone(IndexedType me);
void indexed_type_lookup_names(IndexedType* me, ScopeStack* names, ErrorCtx* errs);
void indexed_type_typecheck(IndexedType const* me, ErrorCtx* errs);

// Keyof

typedef struct KeyofType {
    Type interface;
    bool has_cache;
    Type cached; // Should be {0} if has_cache is false
} KeyofType;

void keyof_type_drop(KeyofType* me);
KeyofType keyof_type_clone(KeyofType me);
void keyof_type_lookup_names(KeyofType* me, ScopeStack* names, ErrorCtx* errs);
void keyof_type_typecheck(KeyofType const* me, ErrorCtx* errs);

// Typeof

typedef struct TypeofType {
    Expr* expr;
    bool owns_expr;
} TypeofType;

void typeof_type_drop(TypeofType* me);
TypeofType typeof_type_clone(TypeofType me);
void typeof_type_lookup_names(TypeofType* me, ScopeStack* names, ErrorCtx* errs);
void typeof_type_typecheck(TypeofType const* me, ErrorCtx* errs);

// Ternary

typedef struct TernaryType {
    Type maybe_sub;
    Type maybe_super;
    Type if_true;
    Type if_false;
    bool has_cache;
    Type cached; // Should be {0} if has_cache is false
} TernaryType;

void ternary_type_drop(TernaryType* me);
TernaryType ternary_type_clone(TernaryType me);

// Abstracted

typedef struct AbstractedType {
    Type inner;
    bool abstract_literals;
    bool has_cache;
    Type cached;
} AbstractedType;

void abstracted_type_drop(AbstractedType* me);
AbstractedType abstracted_type_clone(AbstractedType me);
Type const* abstracted_type_resolve(AbstractedType* me);

static inline Type make_type_abstracted(Type inner, bool abstract_literals) {
    if (abstract_literals) {
        return type_abstracted(c_new(AbstractedType, {
            .inner = inner,
            .abstract_literals = abstract_literals,
        }));
    } else {
        return inner;
    }
}
