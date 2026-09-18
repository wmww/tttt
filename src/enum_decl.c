#include "enum_decl.h"
#include "ty/ty.h"
#include "scope.h"
#include "several_types.h"
#include "interface_member.h"

void enum_case_drop(EnumCase* me) {
    expr_drop(&my value);
    declaration_drop(&my typeof_declaration);
    arc_declaration_drop(&my declaration);
}

void enum_decl_drop(EnumDecl* me) {
    vec_enum_case_drop(&my cases);
    lazy_type_drop(&my instance_type);
    lazy_type_drop(&my interface_type);
    scope_drop(&my namespace);
    declaration_drop(&my typeof_declaration);
    arc_declaration_drop(&my declaration);
}

void enum_decl_add_names(EnumDecl* me, Scope* scope, bool exported) {
    my namespace = scope_init_namespace(my name, scope, scope->errs);

    for_each (i, vec_enum_case, &my cases) {
        i->parent = me;
        declaration_init(&i->typeof_declaration, (DeclProps){
            .name = i->name,
            .full = i->full_name,
            .tag = DECLARATION_ENUM_CASE_TYPE,
            .declared_type = type_lazy(&i->value.lazy_type),
            .node = { DECL_NODE_ENUM_CASE, i },
        });

        scope_init_declaration(&my namespace, &i->declaration, (DeclProps) {
            .name = i->name,
            .full = i->full_name,
            .tag = DECLARATION_ENUM_CASE,
            .value_type = type_identifier(c_new(IdentifierType, {
                .namespaces = vec_span_single(my name),
                .name = i->name,
                .decl_ref = &i->typeof_declaration,
            })),
            .declared_type = type_identifier(c_new(IdentifierType, {
                .namespaces = vec_span_single(my name),
                .name = i->name,
                .decl_ref = &i->typeof_declaration,
            })),
            .node = { DECL_NODE_ENUM_CASE, i },
        });
    }

    declaration_init(&my typeof_declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .tag = DECLARATION_ENUM_CASE_TYPE,
        .declared_type = type_lazy(&my interface_type),
        .is_typeof = true,
        .node = { DECL_NODE_ENUM, me },
    });

    scope_init_declaration(scope, &my declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .tag = DECLARATION_ENUM,
        .value_type = type_identifier(c_new(IdentifierType, {
            .name = my name,
            .decl_ref = &my typeof_declaration,
        })),
        .declared_type = type_lazy(&my instance_type),
        .namespace = &my namespace,
        .node = { DECL_NODE_ENUM, me },
    });
}

void enum_decl_lookup_names(EnumDecl* me, ScopeStack* names, ErrorCtx* errs) {
    lazy_type_bind(&my instance_type, LAZY_TYPE_ENUM_INSTANCE);
    lazy_type_bind(&my interface_type, LAZY_TYPE_ENUM_INTERFACE);

    // Initializers see the members by their bare names
    scope_push_to_stack(&my namespace, names);
    for_each (i, vec_enum_case, &my cases) {
        expr_lookup_names(&i->value, names, errs);
    }
    scope_pop_from_stack(&my namespace);
}

// Enums are evaluated by the new type core (ty/enums.c)
Type enum_decl_evaluate_instance_type(EnumDecl* me) {
    return my declaration.get ? type_new(ty_declared_type_of(current_checker, my declaration.get)) : type_any;
}

Type enum_decl_evaluate_interface_type(EnumDecl* me) {
    return type_new(ty_declared_type_of(current_checker, &my typeof_declaration));
}

void enum_decl_typecheck(EnumDecl* me, ErrorCtx* errs) {
    ty_shadow_record_declaration(my declaration.get);
    ty_shadow_record_declaration(&my typeof_declaration);
    for_each (i, vec_enum_case, &my cases) {
        expr_typecheck(&i->value, errs);
    }
}
