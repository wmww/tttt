#include "scope.h"
#include "several_types.h"
#include "scope_stack.h"
#include "error.h"
#include "source_file.h"
#include "interface_member.h"
#include "generic.h"
#include "program.h"
#include "port_statement.h"

void scope_drop(Scope* me) {
    if (my current_scope_stack) {
        scope_stack_pop_scope(my current_scope_stack, me);
    }
    vec_span_drop(&my namespaces);
    hmap_arc_declaration_drop(&my declarations);
    hmap_port_item_ptr_drop(&my ports);
    vec_port_item_ptr_drop(&my wildcard_reexports);
    lazy_type_drop(&my value_interface);
    if (my export_scope) {
        scope_drop(my export_scope);
        free(my export_scope);
    }
}

Scope scope_clone(Scope me) {
    me.namespaces = vec_span_clone(me.namespaces);
    me.declarations = hmap_arc_declaration_clone(me.declarations);
    me.ports = hmap_port_item_ptr_clone(me.ports);
    me.wildcard_reexports = vec_port_item_ptr_clone(me.wildcard_reexports);
    me.current_scope_stack = nullptr;
    if (me.export_scope) {
        me.export_scope = c_new(Scope, scope_clone(*me.export_scope));
    }
    return me;
}

void scope_push_to_stack(Scope* me, ScopeStack* stack) {
    check(!my current_scope_stack);
    my current_scope_stack = stack;
    scope_stack_push_scope(stack, me);
}

void scope_push_generic_to_stack(Scope* me, Generic* generic, ScopeStack* stack) {
    check(!my current_scope_stack);
    if (!generic_empty(generic)) {
        generic_add_names(generic, me);
        my current_scope_stack = stack;
        scope_stack_push_scope(stack, me);
        generic_lookup_names(generic, stack, my errs);
    }
}

void scope_pop_from_stack(Scope* me) {
    check(my current_scope_stack);
    scope_stack_pop_scope(my current_scope_stack, me);
    my current_scope_stack = nullptr;
}

void scope_copy_declarations_from(Scope* me, Scope const* source) {
    for_each_kv (name, declaration, hmap_arc_declaration, &source->declarations) {
        declaration->get->shared_between_scopes = true;
        hmap_arc_declaration_insert(
            &my declarations,
            cstr_clone(*name),
            arc_declaration_clone(*declaration)
        );
    }
}

void scope_init_declaration(Scope* me, ArcDeclaration* ret, DeclProps props) {
    check(!my current_scope_stack);
    if (ret->get) {
        log_error("Declaration ", ret->get->name, " acquired multiple times");
        arc_declaration_drop(ret);
    }
    auto existing = hmap_arc_declaration_get(&my declarations, props.name.sv);
    bool mergable = false;
    if (existing) {
        mergable = declaration_test_mergable(existing->second.get, &props, my errs);
    }
    if (mergable) {
        if (existing->second.get->shared_between_scopes) {
            *ret = arc_declaration_make(*existing->second.get);
            declaration_clone_in_place(ret->get); // clears shared_between_scopes
            hmap_arc_declaration_insert_or_assign(
                &my declarations,
                cstr_clone(existing->first),
                arc_declaration_clone(*ret)
            );
        } else {
            *ret = arc_declaration_clone(existing->second);
        }
    } else {
        *ret = arc_declaration_make((Declaration){
            .name = props.name,
            .namespaces = vec_span_clone(my namespaces),
        });
        if (!existing) {
            hmap_arc_declaration_insert(
                &my declarations,
                cstr_from_span(props.name),
                arc_declaration_clone(*ret)
            );
        }
    }
    ArcDeclaration non_exported = arc_declaration_clone(*ret);
    if (props.exported && my export_scope) {
        arc_declaration_drop(ret);
        *ret = arc_declaration_init();
        scope_init_declaration(my export_scope, ret, decl_props_clone(props));
    }
    declaration_init(non_exported.get, props);
    arc_declaration_drop(&non_exported);
}

static void add_placeholder_for_failed_import(Scope* me, PortItem* item) {
    check(!item->declaration.get);
    item->declaration = arc_declaration_make((Declaration){
        .name = item->decl_name,
        .declarations = vec_span_single(item->full_span),
        .value_tag = DECLARATION_NAMESPACE_WITH_VALUE,
        .type_tag = DECLARATION_NAMESPACE_WITH_VALUE,
        .namespace_tag = DECLARATION_NAMESPACE_WITH_VALUE,
    });
    if (!hmap_arc_declaration_insert(
        &my declarations,
        cstr_from_sv(item->decl_name.sv),
        arc_declaration_clone(item->declaration)
    ).inserted) {
        error_ctx_error_d(my errs, item->decl_name, 2300, "Duplicate identifier '", item->decl_name, "'.");
    }
}

void scope_add_port(Scope* me, PortItem* item) {
    check(!my current_scope_stack);

    if (item->tag == PORT_IMPORT_ALIAS) {
        // Filled in by port_statement_resolve_names()
        if (hmap_arc_declaration_get(&my declarations, item->decl_name.sv)) {
            item->alias_conflict = true;
            return;
        }
        add_placeholder_for_failed_import(me, item);
        if (item->export_import && my export_scope) {
            hmap_arc_declaration_insert(
                &my export_scope->declarations,
                cstr_from_sv(item->decl_name.sv),
                arc_declaration_clone(item->declaration)
            );
        }
        item->declaration.get->exported = item->export_import;
        return;
    }

    if (!item->source_file) {
        item->error_reported = true;
        add_placeholder_for_failed_import(me, item);
        return;
    }

    Scope* source_scope = source_file_get_scope_if_module(item->source_file);
    if (!source_scope) {
        log_error("Imported source file '", item->source_file->canonical_path, "' is not a module");
        return;
    }

    switch (item->tag) {
        case PORT_NOOP:
        case PORT_EXPORT_AS_NAMESPACE:
            break;

        case PORT_IMPORT_GATHER:
        case PORT_REEXPORT_GATHER:
            declaration_init(&item->wrapped_declaration, (DeclProps){
                .name = item->decl_name,
                .full = item->full_span,
                .tag = DECLARATION_TYPE,
                .declared_type = type_lazy(&source_scope->value_interface),
                .is_typeof = true,
            });
            scope_init_declaration(me, &item->declaration, (DeclProps){
                .name = item->decl_name,
                .full = item->full_span,
                .exported = port_item_is_export(item) || item->export_import,
                .tag = DECLARATION_NAMESPACE_WITH_VALUE,
                .value_type = type_identifier(c_new(IdentifierType, {
                    .name = item->decl_name,
                    .decl_ref = &item->wrapped_declaration,
                })),
                .namespace = source_scope,
            });
            break;

        case PORT_IMPORT_DEFAULT:
        case PORT_IMPORT_SPECIFIC:
        case PORT_EXPORT_SPECIFIC:
        case PORT_REEXPORT_SPECIFIC: {
            auto result = hmap_port_item_ptr_insert(
                &my ports,
                cstr_from_span(item->decl_name),
                item
            );
            if (!result.inserted) {
                error_ctx_error_d(my errs, item->decl_name, 2300,
                    "Duplicate identifier '", item->decl_name, "'.");
            }
        }   break;

        case PORT_REEXPORT_SPLAT:
            vec_port_item_ptr_push(&my wildcard_reexports, item);
            break;

        case PORT_IMPORT_ALIAS:
            impossible;
    }
}

static Declaration const* find_declaration_in_exports(Scope const* me, csview name, bool search_exports) {
    Scope const* search_scope = search_exports ? my export_scope : me;
    check(search_scope);

    auto decl_result = hmap_arc_declaration_get(&search_scope->declarations, name);
    if (decl_result) {
        return decl_result->second.get;
    }

    auto import_result = hmap_port_item_ptr_get(&my ports, name);
    if (import_result && port_item_is_export(import_result->second)) {
        Scope* next = source_file_get_scope_if_module(import_result->second->source_file);
        check(next);
        if (next->currently_adding_import) {
            // TODO: Record this somewhere so 2303, "Circular definition of import alias '%.*s'.", can be raised
            return nullptr;
        }
        next->currently_adding_import = true;
        Declaration const* ret = find_declaration_in_exports(next, import_result->second->source_name.sv, true);
        next->currently_adding_import = false;
        return ret;
    }

    for_each (i, vec_port_item_ptr, &my wildcard_reexports) {
        Scope* next = source_file_get_scope_if_module((*i)->source_file);
        check(next);
        if (next->currently_adding_import) {
            // We don't want to raise an error here
            return nullptr;
        }
        next->currently_adding_import = true;
        Declaration const* ret = find_declaration_in_exports(next, name, true);
        next->currently_adding_import = false;
        if (ret) {
            return ret;
        }
    }

    return nullptr;
}

static void process_port(Scope* me, PortItem* item) {
    if (item->error_reported) return;

    csview source_name = {0};
    bool search_exports = true;
    switch (item->tag) {
        case PORT_NOOP:
        case PORT_IMPORT_GATHER:
        case PORT_EXPORT_AS_NAMESPACE:
        case PORT_REEXPORT_GATHER:
        case PORT_REEXPORT_SPLAT:
        case PORT_IMPORT_ALIAS:
            add_placeholder_for_failed_import(me, item);
            return;

        case PORT_IMPORT_DEFAULT:
            // TODO: error if there is no default export
            source_name = my default_export;
            break;

        case PORT_IMPORT_SPECIFIC:
        case PORT_REEXPORT_SPECIFIC:
            source_name = item->source_name.sv;
            break;

        case PORT_EXPORT_SPECIFIC:
            source_name = item->source_name.sv;
            search_exports = false;
            break;
    }

    Scope* source_scope = source_file_get_scope_if_module(item->source_file);
    check(source_scope);
    check(source_scope->export_scope);
    item->source_decl = find_declaration_in_exports(source_scope, source_name, search_exports);

    if (item->source_decl) {
        item->declaration = arc_declaration_make(*item->source_decl);
        declaration_clone_in_place(item->declaration.get);
        vec_span_drop(&item->declaration.get->declarations);
        item->declaration.get->declarations = vec_span_single(item->full_span);
        bool is_export = port_item_is_export(item);
        item->declaration.get->exported = is_export;
        check(my export_scope);
        HMapArcDeclaration* declarations = is_export ? &my export_scope->declarations : &my declarations;
        if (!hmap_arc_declaration_insert(
            declarations,
            cstr_from_sv(item->decl_name.sv),
            arc_declaration_clone(item->declaration)
        ).inserted) {
            error_ctx_error_d(
                my errs, item->decl_name, 2440,
                "Import declaration conflicts with local declaration of '", item->decl_name, "'."
            );
        }
    } else {
        error_ctx_error_d(
            my errs, item->source_name, 2305,
            "Module '", item->source_file->canonical_path, "' has no exported member '", item->source_name, "'."
        );
        item->error_reported = true;
        add_placeholder_for_failed_import(me, item);
    }
}

void scope_process_imports(Scope* me) {
    for_each (i, hmap_port_item_ptr, &my ports) {
        process_port(me, i->second);
    }
}

Type scope_evaluate_value_interface(Scope const* me) {
    VecInterfaceMember members = vec_interface_member_init();
    for_each (i, hmap_arc_declaration, &my declarations) {
        if (i->second.get->value_tag) {
            InterfaceMember member = interface_member_make(
                span_null, // Declaration
                nullptr, // Interface
                false, // readonly
                false, // optional
                cstr_from_span(i->second.get->name),
                i->second.get->name,
                false, // name is value
                type_clone(i->second.get->value_type)
            );
            // TODO: make this less stupid (probably by refactoring interface_member_make() to take a props struct)
            vec_span_drop(&member.declarations);
            member.declarations = vec_span_clone(i->second.get->declarations);
            vec_interface_member_push(&members, member);
        }
    }
    return type_interface((Interface){
        .members = members,
        .namespace_scope = vec_span_is_empty(&my namespaces) ? nullptr : me,
    });
}

Declaration const* scope_get_declaration(Scope const* me, Span name) {
    return find_declaration_in_exports(me, name.sv, false);
}
