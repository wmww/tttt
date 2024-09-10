#include "scope.h"
#include "name_stack.h"
#include "error.h"
#include "source_file.h"
#include "interface_member.h"

void name_stack_push_scope(NameStack* me, Scope* scope);
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
    hmap_str_scope_cptr_drop(&my namespaces);
    hmap_str_named_import_drop(&my imports);
}

Scope scope_clone(Scope me) {
    me.types = hmap_str_named_type_ptr_clone(me.types);
    me.variables = hmap_str_named_variable_ptr_clone(me.variables);
    me.namespaces = hmap_str_scope_cptr_clone(me.namespaces);
    me.imports = hmap_str_named_import_clone(me.imports);
    me.current_name_stack = nullptr;
    return me;
}

void scope_push_to_stack(Scope* me, NameStack* stack) {
    ASSERT(!my current_name_stack);
    my current_name_stack = stack;
    name_stack_push_scope(stack, me);
}

void scope_push_generic_to_stack(Scope* me, Generic* generic, NameStack* stack) {
    ASSERT(my tag == SCOPE_DEFAULT);
    my tag = SCOPE_GENERIC;
    ASSERT(!my current_name_stack);
    if (!generic_empty(generic)) {
        generic_add_names(generic, me);
        my current_name_stack = stack;
        name_stack_push_scope(stack, me);
        generic_lookup_names(generic, stack, my errs);
    }
}

void scope_pop_from_stack(Scope* me) {
    ASSERT(my current_name_stack);
    name_stack_pop_scope(my current_name_stack, me);
    my current_name_stack = nullptr;
}

void scope_add_variable(Scope* me, NamedVariable* var) {
    scope_add_variable_s(me, cstr_from_span(var->name), var);
}

void scope_add_type(Scope* me, NamedType* type) {
    scope_add_type_s(me, cstr_from_span(type->name), type);
}

void scope_add_variable_s(Scope* me, cstr name_string, NamedVariable* var) {
    ASSERT(!my current_name_stack);
    Span name_span = var->name;
    auto result = hmap_str_named_variable_ptr_insert(&my variables, name_string, var);
    if (!result.inserted) {
        switch (my tag) {
            case SCOPE_DEFAULT:
                error_ctx_error(
                    my errs,
                    name_span,
                    2451,
                    "Cannot redeclare block-scoped variable '%.*s'.",
                    c_SPAN(name_span)
                );
                error_ctx_error(
                    my errs,
                    result.ref->second->name,
                    2451,
                    "Cannot redeclare block-scoped variable '%.*s'.",
                    c_SPAN(result.ref->second->name)
                );
                break;

            case SCOPE_FUNCTION:
            case SCOPE_GENERIC:
                error_ctx_error(
                    my errs,
                    name_span,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SPAN(name_span)
                );
                error_ctx_error(
                    my errs,
                    result.ref->second->name,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SPAN(result.ref->second->name)
                );
                break;
        }
    }
}

void scope_add_type_s(Scope* me, cstr name_string, NamedType* type) {
    ASSERT(!my current_name_stack);
    Span name_span = type->name;
    auto result = hmap_str_named_type_ptr_insert(&my types, name_string, type);
    if (!result.inserted) {
        switch (my tag) {
            case SCOPE_DEFAULT:
            case SCOPE_FUNCTION:
                error_ctx_error(
                    my errs,
                    result.ref->second->name,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SPAN(result.ref->second->name)
                );
                // Fallthrough

            case SCOPE_GENERIC:
                error_ctx_error(
                    my errs,
                    name_span,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SPAN(name_span)
                );
                break;
        }
    }
}

NamedType* scope_add_mergeable_type(
    Scope* me,
    Span name,
    Span declaration_full,
    Type type,
    Generic const* generic,
    bool is_class
) {
    ASSERT(!my current_name_stack);
    if (generic_empty(generic)) {
        generic = nullptr;
    }
    auto existing = hmap_str_named_type_ptr_get(&my types, name.sv);
    if (!existing || !existing->second->is_mergeable || (is_class && existing->second->is_class)) {
        // If there is no existing type by this name the normal add type process is fine
        // Two classes can't be merged, so if we try fall back to normal duplicate name behavior
        NamedType* ret = c_new(NamedType, {
            .name = name,
            .declarations = vec_span_single(declaration_full),
            .type = type,
            .generic = generic,
            .is_class = is_class,
            .is_mergeable = true,
        });
        // This might error and not add ret to the name stack. Even if that happens we want to return ret (the caller
        // will own it either way)
        scope_add_type_s(me, cstr_from_span(name), ret);
        return ret;
    } else {
        // Merge declarations
        NamedType* merged = existing->second;
        // TODO: check generics are equal
        if (!merged->is_merged) {
            // Previously this was not a merged declaration, so we have some setup to do
            // 1. move the previous type into the merged vec
            // 2. set up our lazy type
            // 3. make our type refer to our lazy type
            // Resolving our type later (in the typecheck phase) will then result in named_type_evaluate_type() being
            // called, which will do the actual merging
            vec_type_push(&merged->merged_types, merged->type);
            merged->lazy_type.tag = LAZY_TYPE_NAMED_TYPE;
            merged->type = type_lazy(&merged->lazy_type);
            merged->is_merged = true;
        }
        vec_span_push(&merged->declarations, declaration_full);
        vec_type_push(&merged->merged_types, type);
        merged->ref_count_minus_1++;
        return merged;
    }
}

void scope_add_namespace(Scope* me, Span name, Scope const* scope) {
    ASSERT(!my current_name_stack);
    // PERF: I don't think cloning into a box is needed? The reference we get should simply live long enough
    // NameStacks may not need to be clonable if that's the case
    cstr name_string = cstr_from_span(name);
    auto result = hmap_str_scope_cptr_insert(&my namespaces, name_string, scope);
    if (!result.inserted) {
        // TODO: declaration merging
        error_ctx_error(
            my errs,
            name,
            TS_CODE_NONE,
            "Duplicate namespace '%.*s'.",
            c_SPAN(name)
        );
    }
}

void scope_add_import(Scope* me, NamedImport import) {
    ASSERT(!my current_name_stack);
    auto result = hmap_str_named_import_insert(
        &my imports,
        cstr_from_span(import.decl_name),
        import
    );
    if (!result.inserted) {
        error_ctx_error(
            my errs,
            import.decl_name,
            2300,
            "Duplicate identifier '%.*s'.",
            c_SPAN(import.decl_name)
        );
    }
}

static void add_import(Scope* me, Span decl_name, NamedImport* import) {
    Scope* source_scope = &import->source_file->scope;
    csview source_name = span_valid(import->source_name) ? import->source_name.sv : source_scope->default_export;
    if (import->error_reported) return;

    bool found = false;
    {
        auto result = hmap_str_named_variable_ptr_get(&source_scope->variables, source_name);
        if (result) {
            scope_add_variable_s(me, cstr_from_span(decl_name), result->second);
            found = true;
        }
    }{
        auto result = hmap_str_named_type_ptr_get(&source_scope->types, source_name);
        if (result) {
            scope_add_type_s(me, cstr_from_span(decl_name), result->second);
            found = true;
        }
    }{
        auto result = hmap_str_scope_cptr_get(&source_scope->namespaces, source_name);
        if (result) {
            scope_add_namespace(me, decl_name, result->second);
            found = true;
        }
    }

    if (!found) {
        auto result = hmap_str_named_import_get_mut(&source_scope->imports, source_name);
        if (result) {
            source_scope->currently_adding_import = true;
            if (result->second.source_file->scope.currently_adding_import) {
                error_ctx_error(
                    my errs,
                    span_between(import->decl_name, import->source_name),
                    2303,
                    "Circular definition of import alias '%.*s'.",
                    c_SPAN(import->decl_name)
                );
                import->error_reported = true;
            } else {
                add_import(me, decl_name, &result->second);
            }
            found = true;
            source_scope->currently_adding_import = false;
        }
    }

    if (!found) {
        error_ctx_error(
            my errs,
            import->source_name,
            2305,
            "Module '%s' has no exported member '%.*s'.",
            cstr_str(&import->source_file->absolute_path),
            c_SPAN(import->source_name)
        );
        import->error_reported = true;
    }
}

Type scope_make_interface_from(Scope const* me) {
    VecInterfaceMember members = vec_interface_member_init();
    c_foreach(i, hmap_str_named_variable_ptr, my variables) {
        vec_interface_member_push(&members, interface_member_make(
            i.ref->second->full,
            span_null, // Interface name
            false, // readonly
            false, // optional
            cstr_from_span(i.ref->second->name),
            i.ref->second->name,
            false, // name is value
            type_clone(i.ref->second->type)
        ));
    }
    return type_interface((Interface){
        members
    });
}

void scope_process_imports(Scope* me) {
    c_foreach(i, hmap_str_named_import, my imports) {
        add_import(me, i.ref->second.decl_name, &i.ref->second);
    }
}
