#include "scope.h"
#include "scope_stack.h"
#include "error.h"
#include "source_file.h"
#include "interface_member.h"
#include "generic.h"

void scope_drop(Scope* me) {
    if (my current_scope_stack) {
        scope_stack_pop_scope(my current_scope_stack, me);
    }
    hmap_str_arc_declaration_drop(&my declarations);
    hmap_str_named_import_drop(&my imports);
}

Scope scope_clone(Scope me) {
    me.declarations = hmap_str_arc_declaration_clone(me.declarations);
    me.imports = hmap_str_named_import_clone(me.imports);
    me.current_scope_stack = nullptr;
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

void scope_add_import(Scope* me, NamedImport import) {
    ASSERT(!my current_scope_stack);
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

static void add_placeholder_for_failed_import(Scope* me, Span decl_name, NamedImport* import) {
    ASSERT(!import->declaration->get);
    *import->declaration = arc_declaration_make((Declaration){
        .name = decl_name,
        .declarations = vec_span_single(import->decl_span),
        .value_tag = DECLARATION_VARIABLE,
        .type_tag = DECLARATION_TYPE,
    });
    if (!hmap_str_arc_declaration_insert(
        &my declarations,
        cstr_from_sv(decl_name.sv),
        arc_declaration_clone(*import->declaration)
    ).inserted) {
        error_ctx_error(
            my errs,
            decl_name,
            2300,
            "Duplicate identifier '%.*s'.",
            c_SPAN(decl_name)
        );
    }
}

static void process_import(Scope* me, Span decl_name, NamedImport* import) {
    if (!import->source_file->is_real) {
        import->error_reported = true;
        add_placeholder_for_failed_import(me, decl_name, import);
    }

    Scope* source_scope = &import->source_file->scope;
    if (import->error_reported) return;

    csview source_name;
    switch (import->source) {
        case IMPORT_SOURCE_DEFAULT:
            // TODO: error if there is no default export
            source_name = source_scope->default_export;
            break;

        case IMPORT_SOURCE_WILDCARD:
            add_placeholder_for_failed_import(me, decl_name, import);
            return;

        case IMPORT_SOURCE_SPECIFIC:
            source_name = import->source_name.sv;
            break;

        default: PANIC("Invalid import source %d", import->source);
    }

    bool found = false;

    auto result = hmap_str_arc_declaration_get(&source_scope->declarations, source_name);
    if (result) {
        // TODO: this probably isn't the most elegant way to do this
        if (!hmap_str_arc_declaration_insert(
            &my declarations,
            cstr_from_sv(decl_name.sv),
            arc_declaration_clone(result->second)
        ).inserted) {
            error_ctx_error(
                my errs,
                decl_name,
                2300,
                "Duplicate identifier '%.*s'.",
                c_SPAN(decl_name)
            );
        }
        found = true;
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
                process_import(me, decl_name, &result->second);
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
        add_placeholder_for_failed_import(me, decl_name, import);
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
    c_foreach(i, hmap_str_named_import, my imports) {
        process_import(me, i.ref->second.decl_name, &i.ref->second);
    }
}
