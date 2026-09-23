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
    // The body's members directly: it isn't a type literal (whose `this` would be invalid)
    for_each (i, vec_interface_member, &my type.interface.members) {
        interface_member_lookup_names(i, names, errs);
    }
}

// tsc's checkInterfaceDeclaration, once per symbol (at its first declaration): the interface (with its own `this`) has to
// fit every base of every declaration, with that `this`
static void interface_heritage_typecheck(InterfaceDecl* me, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    if (!symbol || symbol_find_node(symbol, SYMBOL_DECL_INTERFACE) != me || (symbol->flags & SYMBOL_CLASS)) return;
    Checker* checker = current_checker;
    Ty const* declared = ty_declared_type_of(checker, symbol);
    if (declared->kind != TY_DECLARED) return;
    Ty const* this_param = declared->declared.this_param;
    Ty const* type_with_this = this_param ? ty_with_this_arg(checker, declared, this_param) :
        declared->declared.type_params.len ? ty_reference(checker, declared, declared->declared.type_params) : declared;
    LowerCtx ctx = { .generic = true };
    for (u32 d = 0; d < symbol->decl_count; d++) {
        if (symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* decl = symbol->decls[d].node;
        for_each (extends, vec_spanned_type, &decl->extends) {
            Ty const* base = ty_from_node(checker, &extends->type, ctx);
            if (!(base->flags & TYF_OBJECT) || base == declared) continue;
            if (this_param) base = ty_with_this_arg(checker, base, this_param);
            ty_check_heritage(checker, type_with_this, base, nullptr, 0, 2430, "Interface", "incorrectly extends interface",
                my name, errs);
        }
    }
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
        // TODO: TS2312 (not an object type)
    }

    type_typecheck(&my type, errs);
    // TODO: off until checking `CallChainRoot extends CallChain` early stops making the corpus' aliases circular
    // (triage.md, "TS2430"); TS2415/TS2416/TS2420 for classes are on
    if (getenv("TTTT_INTERFACE_HERITAGE")) interface_heritage_typecheck(me, errs);
}
