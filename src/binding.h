#pragma once

#include "type.h"
#include "expr.h"
#include "value.h"

typedef struct ObjectBindingItem ObjectBindingItem;

g_declare_vec(ObjectBindingItem, object_binding_item);

typedef struct Binding {
    enum {
        BINDING_NOOP = 0,
        BINDING_IDENTIFIER,
        BINDING_ARRAY,
        BINDING_OBJECT,
    } tag;
    union {
        Symbol* symbol;
        VecBinding array;
        VecObjectBindingItem object;
    };
    Span content_span; // Just the name/array/object, not the type/value
    bool rest; // `...x` as an element of an array or object pattern
    Span element_span; // With the `...` and the default, only valid for elements of a pattern
    Expr* default_value; // Owned, `x = 1` as an element of a pattern. Null if there's none
    bool definitely_assigned; // `let x!: T`
    bool optional_parameter; // `x?: T` of a function: read as `T | undefined`
    bool has_explicit_type; // If the type was explicitly specified and explicit_type is meaningful
    Span explicit_type_span; // Only valid if has_explicit_type
    TypeNode explicit_type; // The type explicitly specified
    // Where the type comes from when there's no explicit type, see ty_binding_type(). Set by binding_add_names()
    struct Binding const* parent; // The pattern this is an element of, null for the root of a pattern
    ObjectBindingItem const* parent_item; // If parent is an object pattern
    u32 parent_index; // If parent is an array pattern
    // Root only: the type of what is assigned (the initializer, the elements of a for..of). Null: any (a catch variable)
    LazyType* source;
} Binding;

typedef struct ObjectBindingItem {
    Value source;
    Span source_span;
    Expr* computed_key; // Owned, `{ [k]: x }`. Null otherwise, and source is meaningless if set
    Binding target;
} ObjectBindingItem;

typedef struct BindingAddNamesProps {
    bool abstract_literals;
    bool exported;
    bool is_function_arg;
    bool function_scoped; // `var`
    bool is_const;
    bool is_ambient; // `declare`
    Span full;
} BindingAddNamesProps;

void binding_drop(Binding* me);
void binding_add_names(Binding* me, LazyType* source, Scope* scope, BindingAddNamesProps props);
void binding_lookup_names(Binding* me, Scope* names, ErrorCtx* errs);
// Control flow: an assignment node for every variable in the pattern
void binding_flow_assign(Binding const* me, Scope* names);
// An expression that is only evaluated sometimes (a default)
void expr_lookup_names_initializer(Expr* me, Scope* names, ErrorCtx* errs);
void binding_add_implicit_any_type_errors(Binding const* me, bool toplevel, ErrorCtx* errs);
/// `initializer`: what the binding is initialized with, if anything (a failed check is elaborated into it)
void binding_typecheck(Binding* me, Expr const* initializer, ErrorCtx* errs);

void object_binding_item_drop(ObjectBindingItem* me);

#define g_class_key Binding, binding
#define g_declared
#define g_no_clone
#include <cleat/generic/vec.h>

#define g_class_key ObjectBindingItem, object_binding_item
#define g_declared
#define g_no_clone
#include <cleat/generic/vec.h>

typedef struct InitializedBinding {
    Binding binding;
    Expr value;
    bool abstract_literals;
    LazyType lazy_type;
    // The unannotated parameter of a setter: the getter of the same property, whose return type it has
    struct FunctionImpl const* getter;
} InitializedBinding;

void initialized_binding_drop(InitializedBinding* me);
void unique_symbol_decl_mark(UniqueSymbolDecl decl, TypeNode* annotation, Expr* initializer);
void initialized_binding_add_names(InitializedBinding* me, Scope* scope, BindingAddNamesProps props);
void initialized_binding_lookup_names(InitializedBinding* me, Scope* names, ErrorCtx* errs);
void initialized_binding_lookup_names_parameter(InitializedBinding* me, Scope* names, ErrorCtx* errs);
Ty const* initialized_binding_evaluate_assigned_type(InitializedBinding* me);
void initialized_binding_typecheck(InitializedBinding* me, ErrorCtx* errs);

#define g_class_key InitializedBinding, initialized_binding
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>
