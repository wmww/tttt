#include "enum_decl.h"
#include "scope.h"
#include "several_types.h"
#include "interface_member.h"

void enum_case_drop(EnumCase* me) {
    expr_drop(&my value);
    declaration_drop(&my declaration);
    type_drop(&my identifier_type);
}

void enum_decl_drop(EnumDecl* me) {
    vec_enum_case_drop(&my cases);
    lazy_type_drop(&my instance_type);
    lazy_type_drop(&my interface_type);
    arc_declaration_drop(&my declaration);
    declaration_drop(&my interface_declaration);
    declaration_drop(&my instance_declaration);
}

void enum_decl_add_names(EnumDecl* me, Scope* scope) {
    declaration_init(&my interface_declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .tag = DECLARATION_TYPE,
        .declared_type = type_lazy(&my interface_type),
        .is_typeof = true,
    });

    declaration_init(&my instance_declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .tag = DECLARATION_TYPE,
        .declared_type = type_lazy(&my instance_type),
    });

    scope_init_declaration(scope, &my declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .tag = DECLARATION_ENUM,
        .value_type = type_identifier(c_new(IdentifierType, {
            .name = my name,
            .decl_ref = &my interface_declaration,
        })),
        .declared_type = type_identifier(c_new(IdentifierType, {
            .name = my name,
            .decl_ref = &my instance_declaration,
        })),
    });
}

void enum_decl_lookup_names(EnumDecl* me, NameStack* names, ErrorCtx* errs) {
    my instance_type.tag = LAZY_TYPE_ENUM_INSTANCE;
    my interface_type.tag = LAZY_TYPE_ENUM_INTERFACE;

    c_foreach(i, vec_enum_case, my cases) {
        declaration_init(&i.ref->declaration, (DeclProps){
            .name = i.ref->name,
            .full = i.ref->name,
            .tag = DECLARATION_TYPE,
            .declared_type = type_lazy(&i.ref->value.lazy_type),
        });
        i.ref->identifier_type = type_identifier(c_new(IdentifierType, {
            .namespaces = vec_span_single(my name),
            .name = i.ref->name,
            .decl_ref = &i.ref->declaration,
        }));
        expr_lookup_names(&i.ref->value, names, errs);
    }
}

Type enum_decl_evaluate_instance_type(EnumDecl* me) {
    Type ret = type_never;
    c_foreach(i, vec_enum_case, my cases) {
        ret = type_composite(TYPE_UNION, ret, type_clone(*expr_get_type(&i.ref->value)));
    }
    return ret;
}

Type enum_decl_evaluate_interface_type(EnumDecl* me) {
    VecInterfaceMember members = vec_interface_member_init();
    c_foreach(i, vec_enum_case, my cases) {
        vec_interface_member_push(&members, interface_member_make(
            i.ref->name,
            my name,
            true,
            false,
            cstr_from_span(i.ref->name),
            i.ref->name,
            false,
            type_ref(&i.ref->identifier_type)
        ));
    }
    return type_interface((Interface){
        .members = members,
    });
}

void enum_decl_typecheck(EnumDecl* me, ErrorCtx* errs) {
    c_foreach(i, vec_enum_case, my cases) {
        expr_typecheck(&i.ref->value, errs);
    }
}
