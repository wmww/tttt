#pragma once

#include "type.h"

typedef struct ErrorCtx ErrorCtx;

// Identifier

typedef struct IdentifierType {
    VecSpan namespaces;
    Span name;
    NamedType* type;
    VecSpannedType generic_params;
    ResolveState degenericized_state;
    Type degenericized; // A copy of the type with the generic parameters swapped in
    bool inferred;
    ArcNamedType inferred_type;
} IdentifierType;

void identifier_type_drop(IdentifierType* me);
IdentifierType identifier_type_clone(IdentifierType me);
void identifier_type_resolve_names(IdentifierType* me, NameStack* names, ErrorCtx* errs);
void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs);

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
} TernaryType;

void ternary_type_drop(TernaryType* me);
TernaryType ternary_type_clone(TernaryType me);
