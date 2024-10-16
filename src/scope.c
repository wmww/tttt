#include "scope.h"
#include "name_stack.h"
#include "error.h"
#include "source_file.h"
#include "interface_member.h"
#include "generic.h"

void name_stack_push_scope(NameStack* me, Scope* scope);
void name_stack_pop_scope(NameStack* me, Scope* scope);

void scope_drop(Scope* me) {
    if (my current_name_stack) {
        name_stack_pop_scope(my current_name_stack, me);
    }
    hmap_str_arc_declaration_drop(&my declarations);
    hmap_str_scope_cptr_drop(&my namespaces);
    hmap_str_named_import_drop(&my imports);
}

Scope scope_clone(Scope me) {
    me.declarations = hmap_str_arc_declaration_clone(me.declarations);
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

void scope_init_declaration(Scope* me, ArcDeclaration* ret, DeclProps props) {
    if (ret->get) {
        NONFATAL("Declaration %.*s acquired multiple times", c_SPAN(ret->get->name));
        arc_declaration_drop(ret);
    }
    auto existing = hmap_str_arc_declaration_get(&my declarations, props.name.sv);
    bool existing_invalid = false;
    if (existing) {
        Declaration* declaration = existing->second.get;
        if (declaration_tag_has_type(props.tag)) {
            bool can_merge = false;
            switch (declaration->type_tag) {
                case DECLARATION_NONE:
                    can_merge = true;
                    break;

                case DECLARATION_INTERFACE:
                    switch (props.tag) {
                        case DECLARATION_INTERFACE:
                        case DECLARATION_CLASS:
                            can_merge = true;
                            break;

                        default: break;
                    }
                    break;

                case DECLARATION_CLASS:
                    if (props.tag == DECLARATION_INTERFACE) {
                        can_merge = true;
                    }
                    break;

                case DECLARATION_ENUM:
                    if (props.tag == DECLARATION_ENUM) {
                        can_merge = true;
                    }
                    break;

                default: break;
            }
            if (!can_merge) {
                existing_invalid = true;
                if (declaration->type_tag == DECLARATION_ENUM || props.tag == DECLARATION_ENUM) {
                    error_ctx_error(
                        my errs,
                        declaration->name,
                        2567,
                        "Enum declarations can only merge with namespace or other enum declarations."
                    );
                    error_ctx_error(
                        my errs,
                        props.name,
                        2567,
                        "Enum declarations can only merge with namespace or other enum declarations."
                    );
                } else {
                    switch (my tag) {
                        case SCOPE_DEFAULT:
                        case SCOPE_FUNCTION:
                            error_ctx_error(
                                my errs,
                                declaration->name,
                                2300,
                                "Duplicate identifier '%.*s'.",
                                c_SPAN(declaration->name)
                            );
                            // Fallthrough

                        case SCOPE_GENERIC:
                            error_ctx_error(
                                my errs,
                                props.name,
                                2300,
                                "Duplicate identifier '%.*s'.",
                                c_SPAN(props.name)
                            );
                            break;
                    }
                }
            }
        }
        if (declaration->value_tag && declaration_tag_has_value(props.tag)) {
            existing_invalid = true;
            if (declaration->value_tag == DECLARATION_ENUM || props.tag == DECLARATION_ENUM) {
                error_ctx_error(
                    my errs,
                    declaration->name,
                    2567,
                    "Enum declarations can only merge with namespace or other enum declarations."
                );
                error_ctx_error(
                    my errs,
                    props.name,
                    2567,
                    "Enum declarations can only merge with namespace or other enum declarations."
                );
            } else {
                switch (my tag) {
                    case SCOPE_DEFAULT:
                        error_ctx_error(
                            my errs,
                            props.name,
                            2451,
                            "Cannot redeclare block-scoped variable '%.*s'.",
                            c_SPAN(props.name)
                        );
                        error_ctx_error(
                            my errs,
                            declaration->name,
                            2451,
                            "Cannot redeclare block-scoped variable '%.*s'.",
                            c_SPAN(declaration->name)
                        );
                        break;

                    case SCOPE_FUNCTION:
                    case SCOPE_GENERIC:
                        error_ctx_error(
                            my errs,
                            props.name,
                            2300,
                            "Duplicate identifier '%.*s'.",
                            c_SPAN(props.name)
                        );
                        error_ctx_error(
                            my errs,
                            declaration->name,
                            2300,
                            "Duplicate identifier '%.*s'.",
                            c_SPAN(declaration->name)
                        );
                        break;
                }
            }
        }
    }
    if (existing && !existing_invalid) {
        *ret = arc_declaration_clone(existing->second);
    } else {
        *ret = arc_declaration_make((Declaration){ .name = props.name });
        if (!existing) {
            hmap_str_arc_declaration_insert(&my declarations, cstr_from_span(props.name), arc_declaration_clone(*ret));
        }
    }
    declaration_init(ret->get, props);
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
        auto result = hmap_str_arc_declaration_get(&source_scope->declarations, source_name);
        if (result) {
            // TODO: this probably isn't the most elegant way to do this
            if (!hmap_str_arc_declaration_insert(
                &my declarations,
                cstr_from_sv(import->decl_name.sv),
                arc_declaration_clone(result->second)
            ).inserted) {
                error_ctx_error(
                    my errs,
                    import->decl_name,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SPAN(import->decl_name)
                );
            }
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
    c_foreach(i, hmap_str_arc_declaration, my declarations) {
        if (i.ref->second.get->value_tag) {
            InterfaceMember member = interface_member_make(
                span_null, // Declaration
                span_null, // Interface name
                false, // readonly
                false, // optional
                cstr_from_span(i.ref->second.get->name),
                i.ref->second.get->name,
                false, // name is value
                type_clone(i.ref->second.get->value_type)
            );
            // TODO: make this less stupid (probably by refactoring interface_member_make() to take a props struct)
            vec_span_drop(&member.declarations);
            member.declarations = vec_span_clone(i.ref->second.get->declarations);
            vec_interface_member_push(&members, member);
        }
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
