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

    name_stack_add_variable(ret, cstr_lit("console"), &type_any, span_null);
    name_stack_add_variable(ret, cstr_lit("String"), &type_any, span_null);
    name_stack_add_variable(ret, cstr_lit("Number"), &type_any, span_null);
    name_stack_add_variable(ret, cstr_lit("Boolean"), &type_any, span_null);
    name_stack_add_variable(ret, cstr_lit("NaN"), &type_any, span_null);

    name_stack_add_type(ret, cstr_lit("any"), &type_any);
    name_stack_add_type(ret, cstr_lit("unknown"), &type_unknown);
    name_stack_add_type(ret, cstr_lit("never"), &type_never);
    name_stack_add_type(ret, cstr_lit("void"), &type_void);
    name_stack_add_type(ret, cstr_lit("boolean"), &type_boolean);
    name_stack_add_type(ret, cstr_lit("number"), &type_number);
    name_stack_add_type(ret, cstr_lit("string"), &type_string);

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

bool name_stack_add_variable(NameStack* me, cstr name, Type const* type, Span decl_span) {
    hmap_str_arc_named_variable_result result = hmap_str_arc_named_variable_insert(
        &vec_stack_frame_back(&my stack)->variables,
        name,
        arc_named_variable_make((NamedVariable){
            .type = type,
            .decl_span = decl_span,
        })
    );
    return result.inserted;
}

bool name_stack_add_type(NameStack* me, cstr name, Type const* type) {
    hmap_str_arc_named_type_result result = hmap_str_arc_named_type_insert(
        &vec_stack_frame_back(&my stack)->types,
        name,
        arc_named_type_make((NamedType){
            .type = type,
            .is_generic = false,
            .generic_index = 0,
        })
    );
    return result.inserted;
}

bool name_stack_add_generic_type(NameStack* me, cstr name, Type const* type, int generic_index) {
    hmap_str_arc_named_type_result result = hmap_str_arc_named_type_insert(
        &vec_stack_frame_back(&my stack)->types,
        name,
        arc_named_type_make((NamedType){
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
