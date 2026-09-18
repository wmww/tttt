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
        declaration_init(&i->typeof_declaration, (DeclProps){
            .name = i->name,
            .full = i->full_name,
            .tag = DECLARATION_ENUM_CASE_TYPE,
            .declared_type = type_lazy(&i->value.lazy_type),
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
        });
    }

    declaration_init(&my typeof_declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .tag = DECLARATION_ENUM_CASE_TYPE,
        .declared_type = type_lazy(&my interface_type),
        .is_typeof = true,
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
    });
}

void enum_decl_lookup_names(EnumDecl* me, ScopeStack* names, ErrorCtx* errs) {
    lazy_type_bind(&my instance_type, LAZY_TYPE_ENUM_INSTANCE);
    lazy_type_bind(&my interface_type, LAZY_TYPE_ENUM_INTERFACE);

    for_each (i, vec_enum_case, &my cases) {
        expr_lookup_names(&i->value, names, errs);
    }
}

Type enum_decl_evaluate_instance_type(EnumDecl* me) {
    Type ret = type_never;
    for_each (i, vec_enum_case, &my cases) {
        ret = type_composite(TYPE_UNION, ret, type_clone(*expr_get_type(&i->value)));
    }
    return ret;
}

Type enum_decl_evaluate_interface_type(EnumDecl* me) {
    VecInterfaceMember members = vec_interface_member_init();
    for_each (i, vec_enum_case, &my cases) {
        vec_interface_member_push(&members, interface_member_make(
            i->full_name,
            my declaration.get,
            true,
            false,
            cstr_from_span(i->name),
            i->full_name,
            i->name_is_value,
            type_refcopy(&i->declaration.get->value_type)
        ));
    }
    return type_interface((Interface){
        .members = members,
    });
}

void enum_decl_typecheck(EnumDecl* me, ErrorCtx* errs) {
    ty_shadow_record_declaration(my declaration.get);
    ty_shadow_record_declaration(&my typeof_declaration);
    for_each (i, vec_enum_case, &my cases) {
        expr_typecheck(&i->value, errs);
    }
}
