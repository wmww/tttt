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
        ArcDeclaration declaration;
        VecBinding array;
        VecObjectBindingItem object;
    };
    Span content_span; // Just the name/array/object, not the type/value
    bool has_explicit_type; // If the type was explicitly specified and explicit_type is meaningful
    Span explicit_type_span; // Only valid if has_explicit_type
    TypeNode explicit_type; // The type explicitly specified
    TypeNode assigned_type; // Not meaningful until binding_add_names() is called
} Binding;

typedef struct ObjectBindingItem {
    Value source;
    Span source_span;
    Binding target;
} ObjectBindingItem;

typedef struct BindingAddNamesProps {
    bool abstract_literals;
    bool exported;
    bool is_function_arg;
    Span full;
} BindingAddNamesProps;

void binding_drop(Binding* me);
void binding_add_names(Binding* me, TypeNode input, Scope* scope, BindingAddNamesProps props);
TypeNode const* binding_get_effective_type(Binding const* me); // Only valid after binding_add_names() is called
void binding_lookup_names(Binding* me, ScopeStack* names, ErrorCtx* errs);
void binding_add_implicit_any_type_errors(Binding const* me, bool toplevel, ErrorCtx* errs);
void binding_typecheck(Binding* me, ErrorCtx* errs);

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
} InitializedBinding;

void initialized_binding_drop(InitializedBinding* me);
void initialized_binding_add_names(InitializedBinding* me, Scope* scope, BindingAddNamesProps props);
void initialized_binding_lookup_names(InitializedBinding* me, ScopeStack* names, ErrorCtx* errs);
Ty const* initialized_binding_evaluate_assigned_type(InitializedBinding* me);
void initialized_binding_typecheck(InitializedBinding* me, ErrorCtx* errs);

#define g_class_key InitializedBinding, initialized_binding
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>
