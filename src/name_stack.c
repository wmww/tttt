#include "name_stack.h"

Namespace namespace_init(bool is_generic) {
    return (Namespace){
        .types = hmap_str_named_type_init(),
        .variables = hmap_str_c_type_ptr_init(),
        .is_generic = is_generic,
    };
}

void namespace_drop(Namespace* me) {
    hmap_str_named_type_drop(&me->types);
    hmap_str_c_type_ptr_drop(&me->variables);
}

Namespace namespace_clone(Namespace me) {
    return (Namespace){
        .types = hmap_str_named_type_clone(me.types),
        .variables = hmap_str_c_type_ptr_clone(me.variables),
    };
}

NameStack name_stack_init() {
    return (NameStack){
        .stack = c_init(vec_namespace, {{
            .variables = c_init(hmap_str_c_type_ptr, {
                {.first = c_sv("console"), .second = &type_any},
                {.first = c_sv("String"), .second = &type_any},
                {.first = c_sv("Number"), .second = &type_any},
                {.first = c_sv("Boolean"), .second = &type_any},
                {.first = c_sv("NaN"), .second = &type_any},
            }),
            .types = hmap_str_named_type_init(),
            .is_generic = false,
        }}),
    };
}

void name_stack_drop(NameStack* me) {
    vec_namespace_drop(&me->stack);
}

void name_stack_push_frame(NameStack* me, bool is_generic) {
    vec_namespace_push(&me->stack, namespace_init(is_generic));
}

void name_stack_pop_frame(NameStack* me) {
    vec_namespace_pop(&me->stack);
}

bool name_stack_add_variable(NameStack* me, cstr name, Type const* type) {
    hmap_str_c_type_ptr_result result = hmap_str_c_type_ptr_insert(
        &vec_namespace_back(&me->stack)->variables,
        name,
        type
    );
    return result.inserted;
}

bool name_stack_add_type(NameStack* me, cstr name, Type const* type, bool is_generic, int generic_index) {
    hmap_str_named_type_result result = hmap_str_named_type_insert(
        &vec_namespace_back(&me->stack)->types,
        name,
        (NamedType){
            .type = type,
            .is_generic = is_generic,
            .generic_index = generic_index,
        }
    );
    return result.inserted;
}

Type const* name_stack_get_variable(NameStack* me, csview name) {
    c_foreach_rev(i, vec_namespace, me->stack) {
        hmap_str_c_type_ptr_value const* result = hmap_str_c_type_ptr_get(&i.ref->variables, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}

NamedType const* name_stack_get_type(NameStack* me, csview name, int* optional_generic_depth) {
    if (optional_generic_depth) {
        *optional_generic_depth = 0;
    }
    c_foreach_rev(i, vec_namespace, me->stack) {
        if (optional_generic_depth && i.ref->is_generic) {
            (*optional_generic_depth)++;
        }
        hmap_str_named_type_value const* result = hmap_str_named_type_get(&i.ref->types, name);
        if (result) {
            if (optional_generic_depth && !i.ref->is_generic) {
                *optional_generic_depth = 0;
            }
            return &result->second;
        }
    }
    return nullptr;
}
