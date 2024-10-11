#include "enum_decl.h"
#include "scope.h"
#include "several_types.h"
#include "interface_member.h"

void enum_case_drop(EnumCase* me) {
    expr_drop(&my value);
    named_type_destroy(my type_proxy);
    type_drop(&my identifier_type);
}

void enum_decl_drop(EnumDecl* me) {
    vec_enum_case_drop(&my cases);
    lazy_type_drop(&my instance_type);
    lazy_type_drop(&my interface_type);
    named_type_destroy(my named_type);
    named_variable_destroy(my named_var);
    type_drop(&my instance_identifier_type);
    type_drop(&my interface_identifier_type);
    named_type_destroy(my instance_type_proxy);
    named_type_destroy(my interface_type_proxy);
}

void enum_decl_add_names(EnumDecl* me, Scope* scope) {
    ASSERT(!my named_type);
    my named_type = make_named_type(my name, my full_span, type_ref(&my instance_identifier_type), nullptr);
    scope_add_type(scope, my named_type);
    my named_var = make_named_variable(my name, my full_span, type_ref(&my interface_identifier_type), nullptr);
    scope_add_variable(scope, my named_var);
}

void enum_decl_lookup_names(EnumDecl* me, NameStack* names, ErrorCtx* errs) {
    my instance_type.tag = LAZY_TYPE_ENUM_INSTANCE;
    my interface_type.tag = LAZY_TYPE_ENUM_INTERFACE;

    my instance_type_proxy = make_named_type(my name, my full_span, type_lazy(&my instance_type), nullptr);
    my interface_type_proxy = make_named_type(my name, my full_span, type_lazy(&my interface_type), nullptr);
    my interface_type_proxy->is_typeof_class = true;

    my instance_identifier_type = type_identifier(c_new(IdentifierType, {
        .name = my name,
        .named_type = my instance_type_proxy,
    }));

    my interface_identifier_type = type_identifier(c_new(IdentifierType, {
        .name = my name,
        .named_type = my interface_type_proxy,
    }));

    c_foreach(i, vec_enum_case, my cases) {
        i.ref->type_proxy = make_named_type(i.ref->name, i.ref->name, type_lazy(&i.ref->value.lazy_type), nullptr);
        i.ref->identifier_type = type_identifier(c_new(IdentifierType, {
            .namespaces = vec_span_single(my name),
            .name = i.ref->name,
            .named_type = i.ref->type_proxy,
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
