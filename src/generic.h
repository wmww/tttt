#pragma once

#include "type.h"

typedef struct vec_expr VecExpr;

typedef struct GenericItem {
    Span name;
    Span full_span;
    bool is_const; // `const T`
    bool extends_explicit;
    TypeNode extends;
    bool fallback_explicit;
    TypeNode fallback;
    Symbol* owned_symbol;
    Symbol const* symbol_ref;
} GenericItem;

void generic_item_drop(GenericItem* me);

#define g_class_key GenericItem, generic_item
#define g_no_clone
#include <cleat/generic/vec.h>

typedef struct Generic {
    VecGenericItem items;
} Generic;

static inline bool generic_empty(Generic const* me) {
    return !me || vec_generic_item_is_empty(&my items);
}

void generic_drop(Generic* me);
void generic_add_names(Generic const* me, Scope* scope);
void generic_lookup_names(Generic const* me, Scope* names, ErrorCtx* errs);
void generic_typecheck(Generic const* me, ErrorCtx* errs);
