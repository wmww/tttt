#include "enum_decl.h"
#include "ty/ty.h"
#include "scope.h"
#include "several_types.h"
#include "interface_member.h"

void enum_case_drop(EnumCase* me) {
    expr_drop(&my value);
}

void enum_decl_drop(EnumDecl* me) {
    vec_enum_case_drop(&my cases);
}

void enum_decl_add_names(EnumDecl* me, Scope* scope, bool exported) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .kind = SYMBOL_DECL_ENUM,
        .node = me,
        .is_const_enum = my is_const,
    });

    // The members are the exports of the enum symbol, shared with the other declarations of the enum
    my namespace = scope_new(scope, SCOPE_ENUM);
    my namespace->container = my symbol;

    for_each (i, vec_enum_case, &my cases) {
        i->parent = me;
        scope_init_declaration(my namespace, &i->symbol, (DeclProps) {
            .name = i->name,
            .exported = true,
            .full = i->full_name,
            .kind = SYMBOL_DECL_ENUM_MEMBER,
            .node = i,
        });
    }
}

void enum_decl_lookup_names(EnumDecl* me, Scope* names, ErrorCtx* errs) {
    // Initializers see the members by their bare names
    (void)names;
    for_each (i, vec_enum_case, &my cases) {
        expr_lookup_names(&i->value, my namespace, errs);
    }
}


void enum_decl_typecheck(EnumDecl* me, ErrorCtx* errs) {
    for_each (i, vec_enum_case, &my cases) {
        expr_typecheck(&i->value, errs);
    }
}
