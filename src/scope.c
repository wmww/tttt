#include "scope.h"
#include "scope_stack.h"
#include "error.h"
#include "source_file.h"
#include "interface_member.h"
#include "generic.h"
#include "import_statement.h"

void scope_drop(Scope* me) {
    if (my current_scope_stack) {
        scope_stack_pop_scope(my current_scope_stack, me);
    }
    hmap_str_arc_declaration_drop(&my declarations);
    hmap_str_import_item_ptr_drop(&my imports);
    vec_import_item_ptr_drop(&my wildcard_reexports);
    if (my export_scope) {
        scope_drop(my export_scope);
        free(my export_scope);
    }
}

Scope scope_clone(Scope me) {
    me.declarations = hmap_str_arc_declaration_clone(me.declarations);
    me.imports = hmap_str_import_item_ptr_clone(me.imports);
    me.wildcard_reexports = vec_import_item_ptr_clone(me.wildcard_reexports);
    me.current_scope_stack = nullptr;
    if (me.export_scope) {
        me.export_scope = c_new(Scope, scope_clone(*me.export_scope));
    }
    return me;
}

void scope_push_to_stack(Scope* me, ScopeStack* stack) {
    ASSERT(!my current_scope_stack);
    my current_scope_stack = stack;
    scope_stack_push_scope(stack, me);
}

void scope_push_generic_to_stack(Scope* me, Generic* generic, ScopeStack* stack) {
    ASSERT(my tag == SCOPE_DEFAULT);
    my tag = SCOPE_GENERIC;
    ASSERT(!my current_scope_stack);
    if (!generic_empty(generic)) {
        generic_add_names(generic, me);
        my current_scope_stack = stack;
        scope_stack_push_scope(stack, me);
        generic_lookup_names(generic, stack, my errs);
    }
}

void scope_pop_from_stack(Scope* me) {
    ASSERT(my current_scope_stack);
    scope_stack_pop_scope(my current_scope_stack, me);
    my current_scope_stack = nullptr;
}

void scope_init_declaration(Scope* me, ArcDeclaration* ret, DeclProps props) {
    ASSERT(!my current_scope_stack);
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
                        case SCOPE_EXPORT:
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
            } else if (declaration->value_tag == DECLARATION_FUNCTION && props.tag == DECLARATION_FUNCTION) {
                // TODO: allow function overloads
            } else {
                switch (my tag) {
                    case SCOPE_DEFAULT:
                    case SCOPE_EXPORT:
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
            hmap_str_arc_declaration_insert(
                &my declarations,
                cstr_from_span(props.name),
                arc_declaration_clone(*ret)
            );
        }
    }
    ArcDeclaration non_exported = arc_declaration_clone(*ret);
    if (props.exported && my tag != SCOPE_EXPORT) {
        if (my export_scope) {
            arc_declaration_drop(ret);
            *ret = arc_declaration_init();
            scope_init_declaration(my export_scope, ret, decl_props_clone(props));
        } else {
            NONFATAL("Tried to add exported declaration to non-toplevel scope");
        }
    }
    declaration_init(non_exported.get, props);
    arc_declaration_drop(&non_exported);
}

void scope_add_import(Scope* me, ImportItem* import) {
    ASSERT(!my current_scope_stack);
    if (import->is_export_wildcard) {
        vec_import_item_ptr_push(&my wildcard_reexports, import);
    } else {
        auto result = hmap_str_import_item_ptr_insert(
            &my imports,
            cstr_from_span(import->decl_name),
            import
        );
        if (!result.inserted) {
            error_ctx_error(
                my errs,
                import->decl_name,
                2300,
                "Duplicate identifier '%.*s'.",
                c_SPAN(import->decl_name)
            );
        }
    }
}

static void add_placeholder_for_failed_import(Scope* me, ImportItem* import) {
    ASSERT(!import->declaration.get);
    import->declaration = arc_declaration_make((Declaration){
        .name = import->decl_name,
        .declarations = vec_span_single(import->full_span),
        .value_tag = DECLARATION_NAMESPACE_WITH_VALUE,
        .type_tag = DECLARATION_NAMESPACE_WITH_VALUE,
        .namespace_tag = DECLARATION_NAMESPACE_WITH_VALUE,
    });
    if (!hmap_str_arc_declaration_insert(
        &my declarations,
        cstr_from_sv(import->decl_name.sv),
        arc_declaration_clone(import->declaration)
    ).inserted) {
        error_ctx_error(
            my errs,
            import->decl_name,
            2300,
            "Duplicate identifier '%.*s'.",
            c_SPAN(import->decl_name)
        );
    }
}

static Declaration const* find_declaration_in_exports(Scope* me, csview name) {
    ASSERT(my export_scope);

    auto decl_result = hmap_str_arc_declaration_get(&my export_scope->declarations, name);
    if (decl_result) {
        return decl_result->second.get;
    }

    auto import_result = hmap_str_import_item_ptr_get_mut(&my imports, name);
    if (import_result && import_result->second->is_export) {
        Scope* next = &import_result->second->source_file->scope;
        if (next->currently_adding_import) {
            // TODO: Record this somewhere so 2303, "Circular definition of import alias '%.*s'.", can be raised
            return nullptr;
        }
        next->currently_adding_import = true;
        Declaration const* ret = ret = find_declaration_in_exports(next, import_result->second->source_name.sv);
        next->currently_adding_import = false;
        return ret;
    }

    c_foreach(i, vec_import_item_ptr, my wildcard_reexports) {
        Scope* next = &(*i.ref)->source_file->scope;
        if (next->currently_adding_import) {
            // We don't want to raise an error here
            return nullptr;
        }
        next->currently_adding_import = true;
        Declaration const* ret = find_declaration_in_exports(next, name);
        next->currently_adding_import = false;
        return ret;
    }

    return nullptr;
}

// This function sets the import's declaration and adds it to the scope. source_import starts the same as import, but if
// the imported item is a re-export in a file that has not yet had its imports processed, this function is called
// recursively with source_import being the re-exported which we are importing through.
static void process_import(Scope* me, ImportItem* import, ImportItem const* source_import) {
    if (!import->source_file->is_real) {
        import->error_reported = true;
        add_placeholder_for_failed_import(me, import);
    }

    Scope* source_scope_private = &source_import->source_file->scope;
    Scope* source_scope = source_scope_private->export_scope;
    ASSERT(source_scope);

    if (import->error_reported) return;

    csview source_name;
    switch (import->source_tag) {
        case IMPORT_SOURCE_DEFAULT:
            // TODO: error if there is no default export
            source_name = my default_export;
            break;

        case IMPORT_SOURCE_WILDCARD:
            add_placeholder_for_failed_import(me, import);
            return;

        case IMPORT_SOURCE_SPECIFIC:
            source_name = import->source_name.sv;
            break;

        default: PANIC("Invalid import source %d", import->source_tag);
    }

    Declaration const* source_decl = find_declaration_in_exports(&source_import->source_file->scope, source_name);

    if (source_decl) {
        import->declaration = arc_declaration_make(*source_decl);
        declaration_clone_in_place(import->declaration.get);
        vec_span_drop(&import->declaration.get->declarations);
        import->declaration.get->declarations = vec_span_single(import->full_span);
        import->declaration.get->exported = import->is_export;
        ASSERT(my export_scope);
        HmapStrArcDeclaration* declarations = import->is_export ? &my export_scope->declarations : &my declarations;
        if (!hmap_str_arc_declaration_insert(
            declarations,
            cstr_from_sv(import->decl_name.sv),
            arc_declaration_clone(import->declaration)
        ).inserted) {
            error_ctx_error(
                my errs,
                import->decl_name,
                2440,
                "Import declaration conflicts with local declaration of '%.*s'.",
                c_SPAN(import->decl_name)
            );
        }
    } else {
        error_ctx_error(
            my errs,
            import->source_name,
            2305,
            "Module '%s' has no exported member '%.*s'.",
            cstr_str(&import->source_file->absolute_path),
            c_SPAN(import->source_name)
        );
        import->error_reported = true;
        add_placeholder_for_failed_import(me, import);
    }
}

Type scope_make_value_interface(Scope const* me) {
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
    c_foreach(i, hmap_str_import_item_ptr, my imports) {
        process_import(me, i.ref->second, i.ref->second);
    }
    hmap_str_import_item_ptr_drop(&my imports);
    my imports = hmap_str_import_item_ptr_init();
}
