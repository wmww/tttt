#include "name_stack.h"

typedef struct StackFrame {
    Scope* scope;
} StackFrame;

StackFrame stack_frame_init(Scope* scope);
void stack_frame_drop(StackFrame* me);
StackFrame stack_frame_clone(StackFrame me);

#define i_key_class_names StackFrame, stack_frame
#include "wrap_stc/vec.h"

typedef struct NameStack {
    Scope global_scope;
    VecStackFrame stack;
} NameStack;

static void name_stack_drop(NameStack* me);

StackFrame stack_frame_init(Scope* scope) {
    return (StackFrame){
        .scope = scope,
    };
}

void stack_frame_drop(StackFrame* me) {
    ignore me;
}

StackFrame stack_frame_clone(StackFrame me) {
    return me;
}

static inline void name_stack_push_frame_private(NameStack* me, Scope* scope) {
    vec_stack_frame_push(&my stack, stack_frame_init(scope));
}

NameStack* name_stack_new() {
    NameStack* me = c_new(NameStack, {
        .global_scope = scope_init(null_error_ctx),
        .stack = vec_stack_frame_init()
    });
    vec_stack_frame_push(&my stack, stack_frame_init(&my global_scope));

#define BUILTIN_VAR(name, t) \
    static NamedVariable name = { 0 }; \
    name.type = t;

#define BUILTIN_TYPE(name, t) \
    static NamedType name = { 0 }; \
    name.type = t

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

    scope_add_variable_s(&my global_scope, cstr_lit("console"), &builtin_var_any);
    scope_add_variable_s(&my global_scope, cstr_lit("String"), &builtin_var_any);
    scope_add_variable_s(&my global_scope, cstr_lit("Number"), &builtin_var_any);
    scope_add_variable_s(&my global_scope, cstr_lit("Boolean"), &builtin_var_any);
    scope_add_variable_s(&my global_scope, cstr_lit("NaN"), &builtin_var_any);
    scope_add_variable_s(&my global_scope, cstr_lit("Date"), &builtin_var_any);
    scope_add_variable_s(&my global_scope, cstr_lit("Error"), &builtin_var_any);
    scope_add_variable_s(&my global_scope, cstr_lit("Set"), &builtin_var_any);
    scope_add_variable_s(&my global_scope, cstr_lit("Array"), &builtin_var_any);
    scope_add_variable_s(&my global_scope, cstr_lit("Object"), &builtin_var_any);
    scope_add_variable_s(&my global_scope, cstr_lit("this"), &builtin_var_undefined);

    scope_add_type_s(&my global_scope, cstr_lit("any"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("unknown"), &builtin_type_unkown);
    scope_add_type_s(&my global_scope, cstr_lit("never"), &builtin_type_never);
    scope_add_type_s(&my global_scope, cstr_lit("void"), &builtin_type_void);
    scope_add_type_s(&my global_scope, cstr_lit("object"), &builtin_type_object);
    scope_add_type_s(&my global_scope, cstr_lit("boolean"), &builtin_type_boolean);
    scope_add_type_s(&my global_scope, cstr_lit("number"), &builtin_type_number);
    scope_add_type_s(&my global_scope, cstr_lit("string"), &builtin_type_string);
    scope_add_type_s(&my global_scope, cstr_lit("String"), &builtin_type_string);
    scope_add_type_s(&my global_scope, cstr_lit("Number"), &builtin_type_number);
    scope_add_type_s(&my global_scope, cstr_lit("Boolean"), &builtin_type_boolean);
    scope_add_type_s(&my global_scope, cstr_lit("Date"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("bigint"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("symbol"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("intrinsic"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("Error"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("Set"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("Array"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("Object"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("BooleanConstructor"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("NumberConstructor"), &builtin_type_any);
    scope_add_type_s(&my global_scope, cstr_lit("StringConstructor"), &builtin_type_any);

    return me;
}

static void name_stack_drop(NameStack* me) {
    PANIC_IF(
        vec_stack_frame_size(&my stack) != 1,
        "Name stack dropped with %zd frames left on it",
        vec_stack_frame_size(&my stack));
    scope_drop(&my global_scope);
    vec_stack_frame_drop(&my stack);
}

void name_stack_destroy(NameStack* me) {
    name_stack_drop(me);
    free(me);
}

// Declared and used exclusivly by scope.c
void name_stack_push_scope(NameStack* me, Scope* scope) {
    name_stack_push_frame_private(me, scope);
}

// Declared and used exclusivly by scope.c
void name_stack_pop_scope(NameStack* me, Scope* scope) {
    PANIC_IF(vec_stack_frame_empty(&my stack), "Tried to pop too many scopes");
    PANIC_IF(
        vec_stack_frame_back(&my stack)->scope != scope,
        "Tried to pop incorrect scope, %zd scopes on the stack",
        vec_stack_frame_size(&my stack));
    vec_stack_frame_pop(&my stack);
}

bool name_stack_has_function(NameStack* me) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        if (i.ref->scope->tag == SCOPE_FUNCTION) {
            return true;
        }
    }
    return false;
}

NamedVariable* name_stack_get_variable(NameStack const* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        auto result = hmap_str_named_variable_ptr_get(&i.ref->scope->variables, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}

NamedType* name_stack_get_type(NameStack const* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        auto result = hmap_str_named_type_ptr_get(&i.ref->scope->types, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}

Scope const* name_stack_get_namespace(NameStack const* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        auto result = hmap_str_scope_cptr_get(&i.ref->scope->namespaces, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}
