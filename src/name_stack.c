#include "name_stack.h"
#include "generic.h"
#include "several_types.h"
#include "error.h"

typedef NamedType* NamedTypePtr;
typedef NamedVariable* NamedVariablePtr;

#define i_key_ssv
#define i_val_names NamedTypePtr, named_type_ptr
#include "wrap_stc/hmap.h"

#define i_key_ssv
#define i_val_names NamedVariablePtr, named_variable_ptr
#include "wrap_stc/hmap.h"

NamedVariable* make_named_variable(Span name, Span full, LazyType* type, Generic const* generic) {
    if (generic_empty(generic)) {
        generic = nullptr;
    }
    return c_new(NamedVariable, {
        .name = name,
        .full = full,
        .type = type,
        .generic = generic,
    });
}

void named_variable_destroy(NamedVariable* me) {
    if (!me) return;
    free(me);
}

NamedType* make_named_type(Span name, Span full, LazyType* type, Generic const* generic) {
    if (generic_empty(generic)) {
        generic = nullptr;
    }
    return c_new(NamedType, {
        .name = name,
        .full = full,
        .type = type,
        .generic = generic,
    });
}

void named_type_drop(NamedType* me) {
    __(me);
}

void named_type_destroy(NamedType* me) {
    if (!me) return;
    free(me);
}

typedef enum StackFrameTag {
    STACK_FRAME_BLOCK,
    STACK_FRAME_FUNCTION,
    STACK_FRAME_GENERIC,
} StackFrameTag;

typedef struct StackFrame {
    StackFrameTag tag;
    HmapStrNamedTypePtr types;
    HmapStrNamedVariablePtr variables;
} StackFrame;

StackFrame stack_frame_init(StackFrameTag tag);
void stack_frame_drop(StackFrame* me);
StackFrame stack_frame_clone(StackFrame me);

#define i_key_class_names StackFrame, stack_frame
#include "wrap_stc/vec.h"

typedef struct NameStack {
    VecStackFrame stack;
    ErrorCtx* errs;
} NameStack;

StackFrame stack_frame_init(StackFrameTag tag) {
    return (StackFrame){
        .tag = tag,
        .types = hmap_str_named_type_ptr_init(),
        .variables = hmap_str_named_variable_ptr_init(),
    };
}

void stack_frame_drop(StackFrame* me) {
    hmap_str_named_type_ptr_drop(&my types);
    hmap_str_named_variable_ptr_drop(&my variables);
}

StackFrame stack_frame_clone(StackFrame me) {
    return (StackFrame){
        .types = hmap_str_named_type_ptr_clone(me.types),
        .variables = hmap_str_named_variable_ptr_clone(me.variables),
    };
}

static inline void name_stack_push_frame_private(NameStack* me, StackFrameTag tag) {
    vec_stack_frame_push(&my stack, stack_frame_init(tag));
}

NameStack* name_stack_new(ErrorCtx* errs) {
    NameStack* ret = c_new(NameStack, {
        .stack = vec_stack_frame_init(),
        .errs = errs,
    });
    name_stack_push_frame_private(ret, STACK_FRAME_BLOCK);

#define BUILTIN_VAR(name, t) \
    static LazyType name##_lazy_type = {0}; \
    static NamedVariable name = { .type = &name##_lazy_type }; \
    lazy_type_drop(&name##_lazy_type); \
    name##_lazy_type = lazy_type_preeval(t);

#define BUILTIN_TYPE(name, t) \
    static LazyType name##_lazy_type = {0}; \
    static NamedType name = { .type = &name##_lazy_type }; \
    lazy_type_drop(&name##_lazy_type); \
    name##_lazy_type = lazy_type_preeval(t);

    BUILTIN_VAR(builtin_var_any, type_any);
    BUILTIN_VAR(builtin_var_undefined, type_undefined);

    BUILTIN_TYPE(builtin_type_any, type_any);
    BUILTIN_TYPE(builtin_type_unkown, type_unknown);
    BUILTIN_TYPE(builtin_type_never, type_never);
    BUILTIN_TYPE(builtin_type_void, type_void);
    BUILTIN_TYPE(builtin_type_object, type_object);
    BUILTIN_TYPE(builtin_type_boolean, type_boolean);
    BUILTIN_TYPE(builtin_type_number, type_number);
    BUILTIN_TYPE(builtin_type_string, type_string);

#undef BUILTIN_VAR
#undef BUILTIN_TYPE

    name_stack_add_variable_s(ret, cstr_lit("console"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("String"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Number"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Boolean"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("NaN"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Date"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Error"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Set"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Array"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Object"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("this"), &builtin_var_undefined);

    name_stack_add_type_s(ret, cstr_lit("any"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("unknown"), &builtin_type_unkown);
    name_stack_add_type_s(ret, cstr_lit("never"), &builtin_type_never);
    name_stack_add_type_s(ret, cstr_lit("void"), &builtin_type_void);
    name_stack_add_type_s(ret, cstr_lit("object"), &builtin_type_object);
    name_stack_add_type_s(ret, cstr_lit("boolean"), &builtin_type_boolean);
    name_stack_add_type_s(ret, cstr_lit("number"), &builtin_type_number);
    name_stack_add_type_s(ret, cstr_lit("string"), &builtin_type_string);
    name_stack_add_type_s(ret, cstr_lit("String"), &builtin_type_string);
    name_stack_add_type_s(ret, cstr_lit("Number"), &builtin_type_number);
    name_stack_add_type_s(ret, cstr_lit("Boolean"), &builtin_type_boolean);
    name_stack_add_type_s(ret, cstr_lit("Date"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("bigint"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("symbol"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("intrinsic"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("Error"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("Set"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("Array"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("Object"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("BooleanConstructor"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("NumberConstructor"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("StringConstructor"), &builtin_type_any);

    return ret;
}

void name_stack_push_block_frame(NameStack* me) {
    name_stack_push_frame_private(me, STACK_FRAME_BLOCK);
}

void name_stack_push_function_frame(NameStack* me) {
    name_stack_push_frame_private(me, STACK_FRAME_FUNCTION);
}

void name_stack_destroy(NameStack* me) {
    if (vec_stack_frame_size(&my stack) != 1) {
        PANIC("stack size %ld on destroy, 1 expected", vec_stack_frame_size(&my stack));
    }
    vec_stack_frame_drop(&my stack);
    free(me);
}

void name_stack_push_generic_frame(NameStack* me, Generic const* generic) {
    if (generic_empty(generic)) {
        // Frame is only needed so a frame can be unconditionally popped later
        name_stack_push_frame_private(me, STACK_FRAME_BLOCK);
    } else {
        name_stack_push_frame_private(me, STACK_FRAME_GENERIC);
        generic_resolve_names(generic, me, my errs);
    }
}

void name_stack_pop_frame(NameStack* me) {
    if (vec_stack_frame_empty(&my stack)) {
        PANIC("more stack frames popped than pushed");
    }
    vec_stack_frame_pop(&my stack);
}

void name_stack_add_variable(NameStack* me, NamedVariable* var) {
    name_stack_add_variable_s(me, cstr_from_span(&var->name), var);

}

void name_stack_add_type(NameStack* me, NamedType* type) {
    name_stack_add_type_s(me, cstr_from_span(&type->name), type);
}

void name_stack_add_variable_s(NameStack* me, cstr name_string, NamedVariable* var) {
    Span name_span = var->name;
    hmap_str_named_variable_ptr_result result = hmap_str_named_variable_ptr_insert(
        &vec_stack_frame_back(&my stack)->variables,
        name_string,
        var
    );
    if (!result.inserted) {
        switch (vec_stack_frame_back(&my stack)->tag) {
            case STACK_FRAME_BLOCK:
                error_ctx_error(
                    my errs,
                    name_span,
                    2451,
                    "Cannot redeclare block-scoped variable '%.*s'.",
                    c_SV(span_sv(&name_span))
                );
                error_ctx_error(
                    my errs,
                    result.ref->second->name,
                    2451,
                    "Cannot redeclare block-scoped variable '%.*s'.",
                    c_SV(span_sv(&result.ref->second->name))
                );
                break;

            case STACK_FRAME_FUNCTION:
            case STACK_FRAME_GENERIC:
                error_ctx_error(
                    my errs,
                    name_span,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SV(span_sv(&name_span))
                );
                error_ctx_error(
                    my errs,
                    result.ref->second->name,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SV(span_sv(&result.ref->second->name))
                );
                break;
        }
    }
}

void name_stack_add_type_s(NameStack* me, cstr name_string, NamedType* type) {
    Span name_span = type->name;
    hmap_str_named_type_ptr_result result = hmap_str_named_type_ptr_insert(
        &vec_stack_frame_back(&my stack)->types,
        name_string,
        type
    );
    if (!result.inserted) {
        switch (vec_stack_frame_back(&my stack)->tag) {
            case STACK_FRAME_BLOCK:
            case STACK_FRAME_FUNCTION:
                error_ctx_error(
                    my errs,
                    result.ref->second->name,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SV(span_sv(&result.ref->second->name))
                );
                // Fallthrough

            case STACK_FRAME_GENERIC:
                error_ctx_error(
                    my errs,
                    name_span,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SV(span_sv(&name_span))
                );
                break;
        }
    }
}

bool name_stack_has_function(NameStack* me) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        if (i.ref->tag == STACK_FRAME_FUNCTION) {
            return true;
        }
    }
    return false;
}

NamedVariable* name_stack_get_variable(NameStack* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        hmap_str_named_variable_ptr_value const* result = hmap_str_named_variable_ptr_get(&i.ref->variables, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}

NamedType* name_stack_get_type(NameStack* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        hmap_str_named_type_ptr_value const* result = hmap_str_named_type_ptr_get(&i.ref->types, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}
