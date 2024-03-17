#pragma once

#include "prelude.h"
#include "type.h"

typedef struct vec_expr VecExpr;

typedef struct GenericItem {
    cstr name;
    Type extends;
    bool fallback_explicit;
    Type fallback;
} GenericItem;

void generic_item_drop(GenericItem* self);
GenericItem generic_item_clone(GenericItem self);

#define i_key_class_names GenericItem, generic_item
#include "wrap_stc/vec.h"

void cstr_append_vec_generic_item_debug(cstr* target, VecGenericItem* self, int indent);
void cstr_append_vec_generic_item_error_msg(cstr* target, VecGenericItem* self);

typedef struct Generic {
    VecGenericItem items;
    Type inner;
} Generic;

void generic_drop(Generic* self);
Generic generic_clone(Generic self);
void cstr_append_generic_debug(cstr* target, Generic* self, int indent);
void cstr_append_generic_error_msg(cstr* target, Generic* self);
void generic_resolve_names(Generic* self, NameStack* names, ErrorCtx* errs);
Type generic_callable_infer(Generic const* self, VecExpr* args, ErrorCtx* errs);
