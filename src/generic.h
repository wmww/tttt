#pragma once

#include "ast_alloc.h"
#include "type.h"

typedef struct vec_expr VecExpr;

typedef struct GenericItem {
    Span name;
    Span full_span;
    bool is_const : 1; // `const T`
    bool is_in : 1; // `in T`: a variance annotation, with `is_out` too `in out T`
    bool is_out : 1; // `out T`
    bool extends_explicit : 1;
    bool fallback_explicit : 1; // fallback is set
    bool jsdoc : 1; // A JSDoc `@template`: not part of the baselines' walk
    // `@template [T]`: bracketed without a default, tsgo's missing type node (declaration emit prints `T = `)
    bool jsdoc_default_missing : 1;
    TypeNode extends;
    Span extends_span; // Null for a JSDoc `@template`
    TypeNode* fallback; // Owned, the default (`= T`); null without one
    Span fallback_span; // Null for a JSDoc `@template`
    Symbol* owned_symbol;
    Symbol const* symbol_ref;
    // A JSDoc `@template`'s whole tag, which the type parameters of the tag share (tsc's JSDocTemplateTag, where
    // unused.c reports them when none of them is used)
    Span jsdoc_tag;
} GenericItem;

void generic_item_drop(GenericItem* me);

#define g_class_key GenericItem, generic_item
#define g_no_clone
#define g_allocator ast
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
    Span owner; // The name of what declares them (a function, class, interface or alias), `f.C` prints it
    Span owner_class; // A method's class, which qualifies its name (`X.m<number>.C`). Invalid otherwise
};

static inline bool generic_empty(Generic const* me) {
    return !me || vec_generic_item_is_empty(&my items);
}

void generic_drop(Generic* me);
void generic_add_names(Generic const* me, Scope* scope);
void generic_lookup_names(Generic const* me, Scope* names, ErrorCtx* errs);
void generic_typecheck(Generic const* me, ErrorCtx* errs);
