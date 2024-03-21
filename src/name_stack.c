#include "name_stack.h"

StackFrame stack_frame_init(StackFrameTag tag) {
    return (StackFrame){
        .tag = tag,
        .types = hmap_str_named_type_init(),
        .variables = hmap_str_c_type_ptr_init(),
    };
}

void stack_frame_drop(StackFrame* me) {
    hmap_str_named_type_drop(&my types);
    hmap_str_c_type_ptr_drop(&my variables);
}

StackFrame stack_frame_clone(StackFrame me) {
    return (StackFrame){
        .types = hmap_str_named_type_clone(me.types),
        .variables = hmap_str_c_type_ptr_clone(me.variables),
    };
}

NameStack name_stack_init() {
    return (NameStack){
        .stack = c_init(vec_stack_frame, {{
            .tag = STACK_FRAME_BLOCK,
            .variables = c_init(hmap_str_c_type_ptr, {
                // TODO: populate from lib file in tsc
                {.first = c_sv("console"), .second = &type_any},
                {.first = c_sv("String"), .second = &type_any},
                {.first = c_sv("Number"), .second = &type_any},
                {.first = c_sv("Boolean"), .second = &type_any},
                {.first = c_sv("NaN"), .second = &type_any},
            }),
            .types = hmap_str_named_type_init(),
        }}),
    };
}

void name_stack_drop(NameStack* me) {
    vec_stack_frame_drop(&my stack);
}

void name_stack_push_frame(NameStack* me, StackFrameTag tag) {
    vec_stack_frame_push(&my stack, stack_frame_init(tag));
}

void name_stack_pop_frame(NameStack* me) {
    vec_stack_frame_pop(&my stack);
}

bool name_stack_add_variable(NameStack* me, cstr name, Type const* type) {
    hmap_str_c_type_ptr_result result = hmap_str_c_type_ptr_insert(
        &vec_stack_frame_back(&my stack)->variables,
        name,
        type
    );
    return result.inserted;
}

bool name_stack_add_type(NameStack* me, cstr name, Type const* type) {
    hmap_str_named_type_result result = hmap_str_named_type_insert(
        &vec_stack_frame_back(&my stack)->types,
        name,
        (NamedType){
            .type = type,
            .is_generic = false,
            .generic_index = 0,
        }
    );
    return result.inserted;
}

bool name_stack_add_generic_type(NameStack* me, cstr name, Type const* type, int generic_index) {
    hmap_str_named_type_result result = hmap_str_named_type_insert(
        &vec_stack_frame_back(&my stack)->types,
        name,
        (NamedType){
            .type = type,
            .is_generic = true,
            .generic_index = generic_index,
        }
    );
    return result.inserted;
}

Type const* name_stack_get_variable(NameStack* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
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
    c_foreach_rev(i, vec_stack_frame, my stack) {
        if (optional_generic_depth && i.ref->tag == STACK_FRAME_GENERIC) {
            (*optional_generic_depth)++;
        }
        hmap_str_named_type_value const* result = hmap_str_named_type_get(&i.ref->types, name);
        if (result) {
            if (optional_generic_depth && i.ref->tag != STACK_FRAME_GENERIC) {
                *optional_generic_depth = 0;
            }
            return &result->second;
        }
    }
    return nullptr;
}
