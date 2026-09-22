#include "interface_decl.h"
#include "ty/ty.h"
#include "error.h"
#include "interface_member.h"
#include "interface.h"
#include "type.h"
#include "scope.h"

void interface_decl_drop(InterfaceDecl* me) {
    generic_drop(&my generic);
    vec_spanned_type_drop(&my extends);
    type_drop(&my type);
}

void interface_decl_add_names(InterfaceDecl* me, Scope* scope, bool exported) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .kind = SYMBOL_DECL_INTERFACE,
        .node = me,
    });
}

void interface_decl_lookup_names(InterfaceDecl* me, Scope* names, ErrorCtx* errs) {
    names = scope_new_generic(names, &my generic, errs);
    Scope* this_scope = scope_new(names, SCOPE_BLOCK);
    this_scope->this_ref = (ThisRef){ .kind = THIS_INTERFACE, .symbol = my symbol, .interface = me };
    names = this_scope;
    check(my type.tag == TYPE_INTERFACE);
    // HACK (tsc's too, for pre-3.0 libs): a `symbol` property of the global `SymbolConstructor` is a unique symbol
    bool symbol_constructor = my symbol && !my symbol->parent && !my symbol->is_local &&
        csview_equals(my symbol->name, "SymbolConstructor");
    for_each (i, vec_interface_member, &my type.interface.members) {
        if (symbol_constructor && i->type.tag == TYPE_SYMBOL) i->type = (TypeNode){ .tag = TYPE_UNIQUE_SYMBOL };
        if (i->type.tag == TYPE_UNIQUE_SYMBOL) i->type.unique_symbol.owner = my symbol;
    }
    for_each (i, vec_spanned_type, &my extends) {
        type_lookup_names(&i->type, names, errs);
    }
    type_lookup_names(&my type, names, errs);
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
