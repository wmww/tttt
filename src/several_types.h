#pragma once

#include "type.h"
#include "lazy_type.h"

typedef struct ErrorCtx ErrorCtx;

// Identifier

typedef struct IdentifierType {
    VecSpan namespaces;
    Span name;
    Span full_span;
    NamedType* type;
    VecSpannedType generic_params;
    typeof((LazyType){}.tag) degenericized_state;
    Type degenericized; // A copy of the type with the generic parameters swapped in
} IdentifierType;

void identifier_type_drop(IdentifierType* me);
IdentifierType identifier_type_clone(IdentifierType me);
void identifier_type_resolve_names(IdentifierType* me, NameStack* names, ErrorCtx* errs);
void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs);

// Inferred identifier

typedef struct InferredType {
    Span name;
    Span full_span;
    Type extends;
    NamedType* named_type;
} InferredType;

void inferred_type_drop(InferredType* me);

#define i_key_class_names InferredType, inferred_type
#define i_opt c_no_atomic
#define i_is_forward
#include "wrap_stc/arc.h"

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
void indexed_type_resolve_names(IndexedType* me, NameStack* names, ErrorCtx* errs);
void indexed_type_typecheck(IndexedType const* me, ErrorCtx* errs);

// Keyof

typedef struct KeyofType {
    Type interface;
    bool has_cache;
    Type cached; // Should be {0} if has_cache is false
} KeyofType;

void keyof_type_drop(KeyofType* me);
KeyofType keyof_type_clone(KeyofType me);
void keyof_type_resolve_names(KeyofType* me, NameStack* names, ErrorCtx* errs);
void keyof_type_typecheck(KeyofType const* me, ErrorCtx* errs);

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
