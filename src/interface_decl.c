#include "interface_decl.h"
#include "ty/ty.h"
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
        .node = { DECL_NODE_INTERFACE, me },
    });
}

void interface_decl_lookup_names(InterfaceDecl* me, ScopeStack* names, ErrorCtx* errs) {
    lazy_type_bind(&my lazy_type, LAZY_TYPE_INTERFACE_DECL);
    Scope generic_scope = scope_init(errs);
    scope_push_generic_to_stack(&generic_scope, &my generic, names);
    for_each (i, vec_spanned_type, &my extends) {
        type_lookup_names(&i->type, names, errs);
    }
    Scope block_scope = scope_init(errs);

    scope_push_to_stack(&block_scope, names);
    type_lookup_names(&my type, names, errs);

    scope_drop(&block_scope);
    scope_drop(&generic_scope);
}

Ty const* interface_decl_evaluate_type(InterfaceDecl* me) {
    // Interfaces are evaluated by the new type core (ty/members.c)
    if (my declaration.get) return ty_declared_type_of(current_checker, my declaration.get);
    return ty_from_node(current_checker, &my type, (LowerCtx){ .generic = true });
}

void interface_decl_typecheck(InterfaceDecl* me, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);

    check(my type.tag == TYPE_INTERFACE);
    for_each (extends, vec_spanned_type, &my extends) {
        type_typecheck(&extends->type, errs);
        if (extends->type.tag != TYPE_IDENTIFIER) {
            error_ctx_error_d(
                errs,
                extends->span,
                2499,
                "An interface can only extend an identifier/qualified-name with optional type arguments."
            );
        }
        // TODO: TS2312 (not an object type), TS2430 (incorrectly extends)
    }

    type_typecheck(&my type, errs);
}
