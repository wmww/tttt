#pragma once

#include "type.h"
#include "expr.h"

typedef struct Binding Binding;
typedef struct ObjectBindingItem ObjectBindingItem;

wrap_forward_vec(Binding, binding);
wrap_forward_vec(ObjectBindingItem, object_binding_item);

typedef struct Binding {
    enum {
        BINDING_NOOP = 0,
        BINDING_IDENTIFIER,
        BINDING_ARRAY,
        BINDING_OBJECT,
    } tag;
    union {
        cstr identifier;
        VecBinding array;
        VecObjectBindingItem object;
    };
    Span content_span; // Just the name/array/object, not the type/value
    bool type_explicit;
    Type type;
    Span type_span;
    bool value_explicit;
    Expr value;
} Binding;

typedef struct ObjectBindingItem {
    Value source;
    Span source_span;
    Binding target;
} ObjectBindingItem;

void binding_drop(Binding* me);
void cstr_append_binding_debug(cstr* target, Binding* me, int indent);
void binding_add_names(Binding* me, NameStack* names, ErrorCtx* errs);
void binding_resolve_names(Binding* me, NameStack* names, ErrorCtx* errs);
void binding_typecheck(Binding* me, bool absract_literals, ErrorCtx* errs);

void object_binding_item_drop(ObjectBindingItem* me);

#define i_key_class_names Binding, binding
#define i_no_clone
#define i_is_forward
#include "wrap_stc/vec.h"

#define i_key_class_names ObjectBindingItem, object_binding_item
#define i_no_clone
#define i_is_forward
#include "wrap_stc/vec.h"
