#include "name_stack.h"

Namespace Namespace_init(bool is_generic) {
    return (Namespace){
        .types = hmap_str_NamedType_init(),
        .variables = hmap_str_CTypePtr_init(),
        .is_generic = is_generic,
    };
}

void Namespace_drop(Namespace* self) {
    hmap_str_NamedType_drop(&self->types);
    hmap_str_CTypePtr_drop(&self->variables);
}

Namespace Namespace_clone(Namespace self) {
    return (Namespace){
        .types = hmap_str_NamedType_clone(self.types),
        .variables = hmap_str_CTypePtr_clone(self.variables),
    };
}

NameStack NameStack_init() {
    return (NameStack){
        .stack = c_init(vec_Namespace, {{
            .variables = c_init(hmap_str_CTypePtr, {
                {.first = c_sv("console"), .second = &Type_any},
                {.first = c_sv("String"), .second = &Type_any},
                {.first = c_sv("Number"), .second = &Type_any},
                {.first = c_sv("Boolean"), .second = &Type_any},
                {.first = c_sv("NaN"), .second = &Type_any},
            }),
            .types = hmap_str_NamedType_init(),
            .is_generic = false,
        }}),
    };
}

void NameStack_drop(NameStack* self) {
    vec_Namespace_drop(&self->stack);
}

void NameStack_push_frame(NameStack* self, bool is_generic) {
    vec_Namespace_push(&self->stack, Namespace_init(is_generic));
}

void NameStack_pop_frame(NameStack* self) {
    vec_Namespace_pop(&self->stack);
}

bool NameStack_add_variable(NameStack* self, cstr name, Type const* type) {
    hmap_str_CTypePtr_result result = hmap_str_CTypePtr_insert(
        &vec_Namespace_back(&self->stack)->variables,
        cstr_clone(name),
        type
    );
    return result.inserted;
}

bool NameStack_add_type(NameStack* self, cstr name, Type const* type, bool is_generic, int generic_index) {
    hmap_str_NamedType_result result = hmap_str_NamedType_insert(
        &vec_Namespace_back(&self->stack)->types,
        cstr_clone(name),
        (NamedType){
            .type = type,
            .is_generic = is_generic,
            .generic_index = generic_index,
        }
    );
    return result.inserted;
}

Type const* NameStack_get_variable(NameStack* self, csview name) {
    c_foreach_rev(i, vec_Namespace, self->stack) {
        hmap_str_CTypePtr_value const* result = hmap_str_CTypePtr_get(&i.ref->variables, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}

NamedType const* NameStack_get_type(NameStack* self, csview name, int* optional_generic_depth) {
    if (optional_generic_depth) {
        *optional_generic_depth = 0;
    }
    c_foreach_rev(i, vec_Namespace, self->stack) {
        if (optional_generic_depth && i.ref->is_generic) {
            (*optional_generic_depth)++;
        }
        hmap_str_NamedType_value const* result = hmap_str_NamedType_get(&i.ref->types, name);
        if (result) {
            if (optional_generic_depth && !i.ref->is_generic) {
                *optional_generic_depth = 0;
            }
            return &result->second;
        }
    }
    return nullptr;
}
