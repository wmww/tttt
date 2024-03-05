#include "name_stack.h"

NameStackFrame NameStackFrame_init() {
    return (NameStackFrame){
        .types = hmap_str_CTypePtr_init(),
        .values = hmap_str_CTypePtr_init(),
    };
}

void NameStackFrame_drop(NameStackFrame* self) {
    hmap_str_CTypePtr_drop(&self->types);
    hmap_str_CTypePtr_drop(&self->values);
}

NameStack NameStack_init() {
    return (NameStack){
        .stack = vec_NameStackFrame_init(),
    };
}

void NameStack_drop(NameStack* self) {
    vec_NameStackFrame_drop(&self->stack);
}

void NameStack_push_frame(NameStack* self) {
    vec_NameStackFrame_push(&self->stack, NameStackFrame_init());
}

void NameStack_pop_frame(NameStack* self) {
    vec_NameStackFrame_pop(&self->stack);
}
