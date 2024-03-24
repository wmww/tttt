#include "name_stack.h"

#define i_key_ssv
#define i_val_class_names ArcNamedType, arc_named_type
#include "wrap_stc/hmap.h"

#define i_key_ssv
#define i_val_class_names ArcNamedVariable, arc_named_variable
#include "wrap_stc/hmap.h"

typedef struct StackFrame {
    StackFrameTag tag;
    HmapStrArcNamedType types;
    HmapStrArcNamedVariable variables;
} StackFrame;

StackFrame stack_frame_init(StackFrameTag tag);
void stack_frame_drop(StackFrame* me);
StackFrame stack_frame_clone(StackFrame me);

#define i_key_class_names StackFrame, stack_frame
#include "wrap_stc/vec.h"

typedef struct NameStack {
    VecStackFrame stack;
} NameStack;

StackFrame stack_frame_init(StackFrameTag tag) {
    return (StackFrame){
        .tag = tag,
        .types = hmap_str_arc_named_type_init(),
        .variables = hmap_str_arc_named_variable_init(),
    };
}

void stack_frame_drop(StackFrame* me) {
    hmap_str_arc_named_type_drop(&my types);
    hmap_str_arc_named_variable_drop(&my variables);
}

StackFrame stack_frame_clone(StackFrame me) {
    return (StackFrame){
        .types = hmap_str_arc_named_type_clone(me.types),
        .variables = hmap_str_arc_named_variable_clone(me.variables),
    };
}

NameStack* name_stack_new() {
    NameStack* ret = c_new(NameStack, {
        .stack = vec_stack_frame_init()
    });
    name_stack_push_frame(ret, STACK_FRAME_BLOCK);

    name_stack_add_variable(ret, cstr_lit("console"), span_null, &type_any);
    name_stack_add_variable(ret, cstr_lit("String"), span_null, &type_any);
    name_stack_add_variable(ret, cstr_lit("Number"), span_null, &type_any);
    name_stack_add_variable(ret, cstr_lit("Boolean"), span_null, &type_any);
    name_stack_add_variable(ret, cstr_lit("NaN"), span_null, &type_any);

    name_stack_add_type(ret, cstr_lit("any"), span_null, &type_any);
    name_stack_add_type(ret, cstr_lit("unknown"), span_null, &type_unknown);
    name_stack_add_type(ret, cstr_lit("never"), span_null, &type_never);
    name_stack_add_type(ret, cstr_lit("void"), span_null, &type_void);
    name_stack_add_type(ret, cstr_lit("boolean"), span_null, &type_boolean);
    name_stack_add_type(ret, cstr_lit("number"), span_null, &type_number);
    name_stack_add_type(ret, cstr_lit("string"), span_null, &type_string);

    return ret;
}

void name_stack_destroy(NameStack* me) {
    if (vec_stack_frame_size(&my stack) != 1) {
        PANIC("stack size %ld on destroy, 1 expected", vec_stack_frame_size(&my stack));
    }
    vec_stack_frame_drop(&my stack);
    free(me);
}

void name_stack_push_frame(NameStack* me, StackFrameTag tag) {
    vec_stack_frame_push(&my stack, stack_frame_init(tag));
}

void name_stack_pop_frame(NameStack* me) {
    if (vec_stack_frame_empty(&my stack)) {
        PANIC("more stack frames popped than pushed");
    }
    vec_stack_frame_pop(&my stack);
}

bool name_stack_add_variable(NameStack* me, cstr name, Span name_span, Type const* type) {
    hmap_str_arc_named_variable_result result = hmap_str_arc_named_variable_insert(
        &vec_stack_frame_back(&my stack)->variables,
        name,
        arc_named_variable_make((NamedVariable){
            .name_span = name_span,
            .type = type,
        })
    );
    return result.inserted;
}

bool name_stack_add_type(NameStack* me, cstr name, Span name_span, Type const* type) {
    hmap_str_arc_named_type_result result = hmap_str_arc_named_type_insert(
        &vec_stack_frame_back(&my stack)->types,
        name,
        arc_named_type_make((NamedType){
            .name_span = name_span,
            .type = type,
            .is_generic = false,
            .generic_index = 0,
        })
    );
    return result.inserted;
}

bool name_stack_add_generic_type(NameStack* me, cstr name, Span name_span, Type const* type, int generic_index) {
    hmap_str_arc_named_type_result result = hmap_str_arc_named_type_insert(
        &vec_stack_frame_back(&my stack)->types,
        name,
        arc_named_type_make((NamedType){
            .name_span = name_span,
            .type = type,
            .is_generic = true,
            .generic_index = generic_index,
        })
    );
    return result.inserted;
}

ArcNamedVariable const* name_stack_get_variable(NameStack* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        hmap_str_arc_named_variable_value const* result = hmap_str_arc_named_variable_get(&i.ref->variables, name);
        if (result) {
            return &result->second;
        }
    }
    return nullptr;
}

ArcNamedType const* name_stack_get_type(NameStack* me, csview name, int* optional_generic_depth) {
    if (optional_generic_depth) {
        *optional_generic_depth = 0;
    }
    c_foreach_rev(i, vec_stack_frame, my stack) {
        if (optional_generic_depth && i.ref->tag == STACK_FRAME_GENERIC) {
            (*optional_generic_depth)++;
        }
        hmap_str_arc_named_type_value const* result = hmap_str_arc_named_type_get(&i.ref->types, name);
        if (result) {
            if (optional_generic_depth && i.ref->tag != STACK_FRAME_GENERIC) {
                *optional_generic_depth = 0;
            }
            return &result->second;
        }
    }
    return nullptr;
}
