#include "interface_decl.h"
#include "error.h"
#include "interface_member.h"
#include "interface.h"
#include "type.h"
#include "scope_stack.h"
#include "scope.h"

void interface_decl_drop(InterfaceDecl* me) {
    generic_drop(&my generic);
    vec_spanned_type_drop(&my extends);
    type_drop(&my type);
    lazy_type_drop(&my lazy_type);
    arc_declaration_drop(&my declaration);
}

void interface_decl_add_names(InterfaceDecl* me, Scope* scope, bool exported) {
    scope_init_declaration(scope, &my declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .tag = DECLARATION_INTERFACE,
        .generic = &my generic,
        .declared_type = type_lazy(&my lazy_type),
    });
}

void interface_decl_lookup_names(InterfaceDecl* me, ScopeStack* names, ErrorCtx* errs) {
    my lazy_type.tag = LAZY_TYPE_INTERFACE_DECL;
    Scope generic_scope = scope_init(errs);
    scope_push_generic_to_stack(&generic_scope, &my generic, names);
    c_foreach(i, vec_spanned_type, my extends) {
        type_lookup_names(&i.ref->type, names, errs);
    }
    Scope block_scope = scope_init(errs);

    scope_push_to_stack(&block_scope, names);
    type_lookup_names(&my type, names, errs);

    scope_drop(&block_scope);
    scope_drop(&generic_scope);
}

Type interface_decl_evaluate_type(InterfaceDecl* me) {
    ASSERT(my type.tag == TYPE_INTERFACE);

    c_foreach(i, vec_interface_member, my type.interface.members) {
        i.ref->origin_interface = my declaration.get;
    }

    VecType types = vec_type_init();

    vec_type_push(&types, type_clone(my type));

    c_foreach(extends, vec_spanned_type, my extends) {
        Type t;
        if (interface_try_from_type(&extends.ref->type, &t)) {
            vec_type_push(&types, t);
        }
    }

    return interface_from_merged(types);
}

void interface_decl_typecheck(InterfaceDecl* me, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);

    ASSERT(my type.tag == TYPE_INTERFACE);
    c_foreach(extends, vec_spanned_type, my extends) {
        type_typecheck(&extends.ref->type, errs);
        Type const* resolved = type_resolve(&extends.ref->type);
        if (extends.ref->type.tag != TYPE_IDENTIFIER) {
            error_ctx_error(
                errs,
                extends.ref->span,
                2499,
                "An interface can only extend an identifier/qualified-name with optional type arguments."
            );
        } else if (resolved->tag != TYPE_INTERFACE) {
            /*
            error_ctx_error(
                errs,
                extends.ref->span,
                2312,
                "An interface can only extend an object type or intersection of object types with statically known members."
            );
            */
        } else {
            /*
            if (!type_extends(&i.ref->type, &my type)) {
                error_ctx_error(
                    errs,
                    my name,
                    2430,
                    "Interface '%.*s' incorrectly extends interface '%.*s'.",
                    c_SV(span_sv(&my name)),
                    c_SV(span_sv(&i.ref->type.identifier->name))
                );
            }
            */
        }
        //my effective_type = type_composite(my effective_type, type_clone(extends.ref->type), TYPE_INTERSECTION);
    }

    type_typecheck(&my type, errs);
    interface_typecheck_members_against(&my type.interface, my declaration.get, errs);
}
