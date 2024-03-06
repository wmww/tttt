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

NameStackFrame NameStackFrame_clone(NameStackFrame self) {
    return (NameStackFrame){
        .types = hmap_str_CTypePtr_clone(self.types),
        .values = hmap_str_CTypePtr_clone(self.values),
    };
}

NameStack NameStack_init() {
    return (NameStack){
        .stack = c_init(vec_NameStackFrame, {{
            .values = c_init(hmap_str_CTypePtr, {
                {.first = c_sv("console"), .second = &Type_any},
                {.first = c_sv("String"), .second = &Type_any},
                {.first = c_sv("Number"), .second = &Type_any},
                {.first = c_sv("Boolean"), .second = &Type_any},
                {.first = c_sv("NaN"), .second = &Type_any},
            }),
            .types = hmap_str_CTypePtr_init(),
        }}),
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
