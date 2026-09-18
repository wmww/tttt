#include "port_statement.h"
#include "error.h"
#include "program.h"
#include "source_file.h"
#include "scope.h"
#include "scope_stack.h"

void port_item_drop(PortItem* me) {
    arc_declaration_drop(&my declaration);
    vec_span_drop(&my alias_path);
    vec_declaration_cptr_drop(&my alias_path_decls);
    declaration_drop(&my wrapped_declaration);
}

void port_statement_drop(PortStatement* me) {
    port_item_drop(&my main);
    vec_port_item_drop(&my items);
    cstr_drop(&my target_path);
}

void port_statement_assign_source(PortStatement* me, SourceFile* source) {
    my main.source_file = source;
    for_each (i, vec_port_item, &my items) {
        i->source_file = my main.source_file;
    }
}

void port_statement_add_names(PortStatement* me, Scope* scope) {
    if (span_valid(my target_path_span)) {
        if (!scope->export_scope) {
            error_ctx_error_d(
                scope->errs,
                my keyword_span,
                1232,
                "An import declaration can only be used at the top level of a namespace or module."
            );
            return;
        }
        // tsc doesn't mind unresolved side effect imports (`import "./a"`)
        if (!my main.source_file && !my side_effect_only) {
            error_ctx_error_d(
                scope->errs, my target_path_span, 2307,
                "Cannot find module '", my target_path, "' or its corresponding type declarations."
            );
        }
    }

    scope_add_port(scope, &my main);
    for_each (i, vec_port_item, &my items) {
        scope_add_port(scope, i);
    }
}

// Import aliases (`import foo = a.b.c`) are resolved lexically, so unlike other ports they can't be handled by the Scope
// while names are being added. scope_add_port() has already put a placeholder declaration in the scope, here it gets
// replaced (in place, things may already point to it) with a copy of the target.
static void port_item_resolve_alias(PortItem* me, ScopeStack* names, ErrorCtx* errs) {
    if (vec_span_is_empty(&my alias_path)) {
        return;
    }

    Declaration const* target = nullptr;
    Span first = *vec_span_front(&my alias_path);
    if (vec_span_size(&my alias_path) == 1) {
        target = scope_stack_get_namespace(names, first.sv);
        if (!target) target = scope_stack_get_value(names, first.sv);
        if (!target) target = scope_stack_get_type(names, first.sv);
        if (!target) {
            error_ctx_error_d(errs, first, 2304, "Cannot find name '", first, "'.");
        } else {
            vec_declaration_cptr_push(&my alias_path_decls, target);
        }
    } else {
        target = scope_stack_get_namespace(names, first.sv);
        if (!target) {
            error_ctx_error_d(errs, first, 2503, "Cannot find namespace '", first, "'.");
        } else {
            vec_declaration_cptr_push(&my alias_path_decls, target);
        }
        for (isize i = 1; target && i < vec_span_size(&my alias_path); i++) {
            Span name = *vec_span_at(&my alias_path, i);
            Span prev = *vec_span_at(&my alias_path, i - 1);
            bool last = i == vec_span_size(&my alias_path) - 1;
            Declaration const* next = target->namespace ? scope_get_declaration(target->namespace, name) : nullptr;
            if (target->namespace && (!next || (!last && !next->namespace_tag))) {
                error_ctx_error_d(errs, name, 2694, "Namespace '", prev, "' has no exported member '", name, "'.");
                next = nullptr;
            }
            target = next;
            if (target) {
                vec_declaration_cptr_push(&my alias_path_decls, target);
            }
        }
    }

    if (my alias_conflict) {
        // TODO: aliases of things without a value should merge with the existing declaration
        if (target && target->value_tag) {
            error_ctx_error_d(errs, my full_span, 2440,
                "Import declaration conflicts with local declaration of '", my decl_name, "'.");
        }
        return;
    }

    if (!target || !my declaration.get || target == my declaration.get) {
        return;
    }

    my source_decl = target;
    Declaration* decl = my declaration.get;
    bool exported = decl->exported;
    declaration_drop(decl);
    *decl = *target;
    declaration_clone_in_place(decl);
    vec_span_drop(&decl->declarations);
    decl->declarations = vec_span_single(my full_span);
    decl->name = my decl_name;
    decl->exported = exported;
}

void port_statement_resolve_names(PortStatement* me, ScopeStack* names, ErrorCtx* errs) {
    if (my main.tag == PORT_IMPORT_ALIAS) {
        port_item_resolve_alias(&my main, names, errs);
    }
}
