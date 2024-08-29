#pragma once

#include "lazy_type.h"

typedef struct TypeRefinement {
    NamedType const* target;
    Type const* refined;
} TypeRefinement;

#define i_key_names TypeRefinement, type_refinement
#include "wrap_stc/vec.h"

typedef struct vec_expr VecExpr;

typedef struct GenericItem {
    Span name;
    Span full_span;
    bool extends_explicit;
    Type extends;
    bool fallback_explicit;
    Type fallback;
    bool owns_named_type;
    NamedType* named_type;
} GenericItem;

void generic_item_drop(GenericItem* me);
GenericItem generic_item_clone(GenericItem me);

#define i_key_class_names GenericItem, generic_item
#include "wrap_stc/vec.h"

typedef struct Generic {
    VecGenericItem items;
} Generic;

static inline bool generic_empty(Generic const* me) {
    return !me || vec_generic_item_empty(&my items);
}

void generic_drop(Generic* me);
Generic generic_clone(Generic me);
void generic_resolve_names(Generic const* me, NameStack* names, ErrorCtx* errs);
void replace_named_types_with(Type* t, VecTypeRefinement const* refinements);
Type generic_degenericize_type(Generic const* me, Type t, VecSpannedType const* generic_params);
CallableType generic_infer_callable_types(
    Generic const* me,
    CallableType* callable,
    VecExpr const* args,
    ErrorCtx* errs
);
void generic_typecheck(Generic const* me, ErrorCtx* errs);
