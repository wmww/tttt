#include "scope.h"
#include "name_stack.h"

void name_stack_push_block_scope(NameStack* me, Scope* scope);
void name_stack_push_function_scope(NameStack* me, Scope* scope);
void name_stack_push_generic_scope(NameStack* me, Generic const* generic, Scope* scope);
void name_stack_pop_scope(NameStack* me, Scope* scope);

NamedVariable* make_named_variable(Span name, Span declaration_full, Type type, Generic const* generic) {
    if (generic_empty(generic)) {
        generic = nullptr;
    }
    return c_new(NamedVariable, {
        .name = name,
        .full = declaration_full,
        .type = type,
        .generic = generic,
    });
}

void named_variable_destroy(NamedVariable* me) {
    if (!me) return;
    type_drop(&my type);
    free(me);
}

NamedType* make_named_type(Span name, Span declaration_full, Type type, Generic const* generic) {
    if (generic_empty(generic)) {
        generic = nullptr;
    }
    return c_new(NamedType, {
        .name = name,
        .declarations = vec_span_single(declaration_full),
        .type = type,
        .generic = generic,
    });
}

static void named_type_drop(NamedType* me) {
    vec_span_drop(&my declarations);
    type_drop(&my type);
    vec_type_drop(&my merged_types);
    lazy_type_drop(&my lazy_type);
}

void named_type_destroy(NamedType* me) {
    if (!me) return;
    my ref_count_minus_1--;
    if (my ref_count_minus_1 < 0) {
        named_type_drop(me);
        free(me);
    }
}

Type named_type_evaluate_type(NamedType* me) {
    // This should only be called when evaluating our LazyType, and our
    // LazyType should only be used when there are multiple types to merge
    ASSERT(my is_merged && vec_type_size(&my merged_types) > 1);
    Type ret = interface_from_merged(&my merged_types);
    vec_type_drop(&my merged_types);
    my merged_types = vec_type_init();
    return ret;
}

void scope_drop(Scope* me) {
    if (my current_name_stack) {
        name_stack_pop_scope(my current_name_stack, me);
    }
    hmap_str_named_type_ptr_drop(&my types);
    hmap_str_named_variable_ptr_drop(&my variables);
    hmap_str_name_stack_cptr_drop(&my namespaces);
    hmap_str_named_import_drop(&my imports);
}

Scope scope_clone(Scope me) {
    me.types = hmap_str_named_type_ptr_clone(me.types);
    me.variables = hmap_str_named_variable_ptr_clone(me.variables);
    me.namespaces = hmap_str_name_stack_cptr_clone(me.namespaces);
    me.imports = hmap_str_named_import_clone(me.imports);
    me.current_name_stack = nullptr;
    return me;
}

void scope_push_to_stack(Scope* me, NameStack* stack) {
    ASSERT(!my current_name_stack);
    my current_name_stack = stack;
    name_stack_push_block_scope(stack, me);
}

void scope_push_to_stack_as_function(Scope* me, NameStack* stack) {
    ASSERT(!my current_name_stack);
    my current_name_stack = stack;
    name_stack_push_function_scope(stack, me);
}

void scope_generic_push_to_stack(Scope* me, Generic* generic, NameStack* stack) {
    ASSERT(!my current_name_stack);
    my current_name_stack = stack;
    name_stack_push_generic_scope(stack, generic, me);
}

void scope_pop_from_stack(Scope* me) {
    ASSERT(my current_name_stack);
    name_stack_pop_scope(my current_name_stack, me);
    my current_name_stack = nullptr;
}
