#pragma once

#include "prelude.h"
#include "type.h"

typedef struct vec_expr VecExpr;

typedef struct GenericItem {
    cstr name;
    Span name_span;
    Type extends;
    bool fallback_explicit;
    Type fallback;
} GenericItem;

void generic_item_drop(GenericItem* me);
GenericItem generic_item_clone(GenericItem me);

#define i_key_class_names GenericItem, generic_item
#include "wrap_stc/vec.h"

void cstr_append_vec_generic_item_debug(cstr* target, VecGenericItem* me, int indent);
void cstr_append_vec_generic_item_error_msg(cstr* target, VecGenericItem* me);

typedef struct Generic {
    VecGenericItem items;
    Type inner;
} Generic;

void generic_drop(Generic* me);
Generic generic_clone(Generic me);
void cstr_append_generic_debug(cstr* target, Generic* me, int indent);
void cstr_append_generic_error_msg(cstr* target, Generic* me);
void generic_resolve_names(Generic* me, NameStack* names, ErrorCtx* errs);
Type generic_callable_infer(Generic const* me, VecExpr* args, ErrorCtx* errs);
