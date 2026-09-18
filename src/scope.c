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
    ASSERT(!my current_scope_stack);
    my current_scope_stack = stack;
    scope_stack_push_scope(stack, me);
}

void scope_push_generic_to_stack(Scope* me, Generic* generic, ScopeStack* stack) {
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

void scope_copy_declarations_from(Scope* me, Scope const* source) {
    c_foreach(i, hmap_arc_declaration, source->declarations) {
        i.ref->second.get->shared_between_scopes = true;
        hmap_arc_declaration_insert(
            &my declarations,
            cstr_clone(i.ref->first),
            arc_declaration_clone(i.ref->second)
        );
    }
}

void scope_init_declaration(Scope* me, ArcDeclaration* ret, DeclProps props) {
    ASSERT(!my current_scope_stack);
    if (ret->get) {
        NONFATAL("Declaration %.*s acquired multiple times", c_SPAN(ret->get->name));
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
    ASSERT(!item->declaration.get);
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
        error_ctx_error(
            my errs,
            item->decl_name,
            2300,
            "Duplicate identifier '%.*s'.",
            c_SPAN(item->decl_name)
        );
    }
}

void scope_add_port(Scope* me, PortItem* item) {
    ASSERT(!my current_scope_stack);

    if (!item->source_file) {
        item->error_reported = true;
        add_placeholder_for_failed_import(me, item);
        return;
    }

    Scope* source_scope = source_file_get_scope_if_module(item->source_file);
    if (!source_scope) {
        NONFATAL("Imported source file '%s' is not a module", cstr_str(&item->source_file->canonical_path));
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
                .exported = port_item_is_export(item),
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
                error_ctx_error(
                    my errs,
                    item->decl_name,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SPAN(item->decl_name)
                );
            }
        }   break;

        case PORT_REEXPORT_SPLAT:
            vec_port_item_ptr_push(&my wildcard_reexports, item);
            break;
    }
}

static Declaration const* find_declaration_in_exports(Scope const* me, csview name, bool search_exports) {
    Scope const* search_scope = search_exports ? my export_scope : me;
    ASSERT(search_scope);

    auto decl_result = hmap_arc_declaration_get(&search_scope->declarations, name);
    if (decl_result) {
        return decl_result->second.get;
    }

    auto import_result = hmap_port_item_ptr_get(&my ports, name);
    if (import_result && port_item_is_export(import_result->second)) {
        Scope* next = source_file_get_scope_if_module(import_result->second->source_file);
        ASSERT(next);
        if (next->currently_adding_import) {
            // TODO: Record this somewhere so 2303, "Circular definition of import alias '%.*s'.", can be raised
            return nullptr;
        }
        next->currently_adding_import = true;
        Declaration const* ret = find_declaration_in_exports(next, import_result->second->source_name.sv, true);
        next->currently_adding_import = false;
        return ret;
    }

    c_foreach(i, vec_port_item_ptr, my wildcard_reexports) {
        Scope* next = source_file_get_scope_if_module((*i.ref)->source_file);
        ASSERT(next);
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
    ASSERT(source_scope);
    ASSERT(source_scope->export_scope);
    item->source_decl = find_declaration_in_exports(source_scope, source_name, search_exports);

    if (item->source_decl) {
        item->declaration = arc_declaration_make(*item->source_decl);
        declaration_clone_in_place(item->declaration.get);
        vec_span_drop(&item->declaration.get->declarations);
        item->declaration.get->declarations = vec_span_single(item->full_span);
        bool is_export = port_item_is_export(item);
        item->declaration.get->exported = is_export;
        ASSERT(my export_scope);
        HMapArcDeclaration* declarations = is_export ? &my export_scope->declarations : &my declarations;
        if (!hmap_arc_declaration_insert(
            declarations,
            cstr_from_sv(item->decl_name.sv),
            arc_declaration_clone(item->declaration)
        ).inserted) {
            error_ctx_error(
                my errs,
                item->decl_name,
                2440,
                "Import declaration conflicts with local declaration of '%.*s'.",
                c_SPAN(item->decl_name)
            );
        }
    } else {
        error_ctx_error(
            my errs,
            item->source_name,
            2305,
            "Module '%s' has no exported member '%.*s'.",
            cstr_str(&item->source_file->canonical_path),
            c_SPAN(item->source_name)
        );
        item->error_reported = true;
        add_placeholder_for_failed_import(me, item);
    }
}

void scope_process_imports(Scope* me) {
    c_foreach(i, hmap_port_item_ptr, my ports) {
        process_port(me, i.ref->second);
    }
}

Type scope_evaluate_value_interface(Scope const* me) {
    VecInterfaceMember members = vec_interface_member_init();
    c_foreach(i, hmap_arc_declaration, my declarations) {
        if (i.ref->second.get->value_tag) {
            InterfaceMember member = interface_member_make(
                span_null, // Declaration
                nullptr, // Interface
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
        .members = members
    });
}

Declaration const* scope_get_declaration(Scope const* me, Span name) {
    return find_declaration_in_exports(me, name.sv, false);
}
