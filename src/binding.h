#pragma once

#include "type.h"
#include "expr.h"
#include "value.h"

typedef struct ObjectBindingItem ObjectBindingItem;

wrap_forward_vec(ObjectBindingItem, object_binding_item);

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
    Type explicit_type; // The type explicitly specified
    Type assigned_type; // Not meaningful until binding_add_names() is called
} Binding;

typedef struct ObjectBindingItem {
    Value source;
    Span source_span;
    Binding target;
} ObjectBindingItem;

void binding_drop(Binding* me);
void binding_add_names(Binding* me, Type input, Scope* scope);
Type const* binding_get_effective_type(Binding const* me); // Only valid after binding_add_names() is called
void binding_lookup_names(Binding* me, ScopeStack* names, ErrorCtx* errs);
void binding_add_implicit_any_type_errors(Binding const* me, bool toplevel, ErrorCtx* errs);
void binding_typecheck(Binding* me, ErrorCtx* errs);

void object_binding_item_drop(ObjectBindingItem* me);

#define i_key_class_names Binding, binding
#define i_is_forward
#define i_no_clone
#include "wrap_stc/vec.h"

#define i_key_class_names ObjectBindingItem, object_binding_item
#define i_is_forward
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct InitializedBinding {
    Binding binding;
    Expr value;
} InitializedBinding;

void initialized_binding_drop(InitializedBinding* me);
void initialized_binding_add_names(InitializedBinding* me, bool abstract_literals, Scope* scope);
void initialized_binding_lookup_names(InitializedBinding* me, ScopeStack* names, ErrorCtx* errs);
void initialized_binding_typecheck(InitializedBinding* me, ErrorCtx* errs);

#define i_key_class_names InitializedBinding, initialized_binding
#define i_no_clone
#define i_is_forward
#include "wrap_stc/vec.h"
