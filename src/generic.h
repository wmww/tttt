#pragma once

#include "type.h"

typedef struct TypeRefinement {
    NamedType const* target;
    Type const* refined;
} TypeRefinement;

#define i_key_names TypeRefinement, type_refinement
#include "wrap_stc/vec.h"

typedef struct vec_expr VecExpr;

typedef struct GenericItem {
    Span name;
    Type extends;
    bool fallback_explicit;
    Type fallback;
    ArcNamedType type;
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
void cstr_append_generic_debug(cstr* target, Generic const* me, int indent);
void cstr_append_generic_error_msg(cstr* target, Generic const* me);
void generic_resolve_names(Generic* me, NameStack* names, ErrorCtx* errs);
void replace_named_types_with(Type* t, VecTypeRefinement const* refinements);
Type generic_degenericize_type(Generic const* me, Type t, VecSpannedType const* generic_params);
CallableType generic_infer_callable_types(
    Generic const* me,
    CallableType const* callable,
    VecExpr const* args,
    ErrorCtx* errs
);
