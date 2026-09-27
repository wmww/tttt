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
    bool jsdoc; // A JSDoc `@template`: not part of the baselines' walk
    // A JSDoc `@overload` in its host's comment: the host's `@template` of that name (symbol_ref, set when the host's
    // names are resolved), which it doesn't declare again (tsc: the same TypeParameterDeclaration)
    bool jsdoc_shared;
} GenericItem;

void generic_item_drop(GenericItem* me);

#define g_class_key GenericItem, generic_item
#define g_no_clone
#include <cleat/generic/vec.h>

typedef struct Generic {
    VecGenericItem items;
} Generic;

// The type parameter lists around a declaration (tsc's getOuterTypeParameters), innermost first: what a local class,
// interface or type alias captures. Made by the resolve walk (Scope.generics) in the binder's arena, shared by
// everything declared in the same scope
struct OuterGenerics {
    struct OuterGenerics const* parent;
    Generic const* generic;
    Span owner; // The name of what declares them (a function, class, interface or alias), `f<number>.C` prints it
    Span owner_class; // A method's class, which qualifies its name (`X.m<number>.C`). Invalid otherwise
};

static inline bool generic_empty(Generic const* me) {
    return !me || vec_generic_item_is_empty(&my items);
}

void generic_drop(Generic* me);
void generic_add_names(Generic const* me, Scope* scope);
void generic_lookup_names(Generic const* me, Scope* names, ErrorCtx* errs);
void generic_typecheck(Generic const* me, ErrorCtx* errs);
