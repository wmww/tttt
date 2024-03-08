#pragma once

#include "prelude.h"
#include "type.h"

typedef struct vec_Expr vec_Expr;

typedef struct GenericItem {
    cstr name;
    Type extends;
    bool fallback_explicit;
    Type fallback;
} GenericItem;

void GenericItem_drop(GenericItem* self);
GenericItem GenericItem_clone(GenericItem self);

#define i_key_class GenericItem
#include "stc/vec.h"

void cstr_append_vec_generic_item_debug(cstr* target, vec_GenericItem* self, int indent);

typedef struct Generic {
    vec_GenericItem items;
    Type inner;
} Generic;

void Generic_drop(Generic* self);
Generic Generic_clone(Generic self);
void cstr_append_generic_debug(cstr* target, Generic* self, int indent);
void Generic_resolve_names(Generic* self, NameStack* names, ErrorCtx* errs);
Type Generic_callable_infer(Generic const* self, vec_Expr* args, ErrorCtx* errs);
