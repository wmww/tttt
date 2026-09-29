#include "generic.h"
#include "ty/ty.h"
#include "scope.h"
#include "several_types.h"
#include "callable_type.h"
#include "interface.h"
#include "interface_member.h"
#include "expr.h"
#include "error.h"

void generic_item_drop(GenericItem* me) {
    type_drop(&my extends);
    type_drop(&my fallback);
}

void generic_drop(Generic* me) {
    vec_generic_item_drop(&my items);
}

void generic_add_names(Generic const* me, Scope* scope) {
    for_each (i, vec_generic_item, &my items) {
        if (i->jsdoc_shared) continue;
        scope_init_declaration(scope, &i->owned_symbol, (DeclProps){
            .name = i->name,
            .full = i->full_span, // (tsc's declaration position starts at the modifiers)
            .kind = SYMBOL_DECL_TYPE_PARAMETER,
            .node = i,
        });
        i->symbol_ref = i->owned_symbol;
    }
}

void generic_lookup_names(Generic const* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_generic_item, &my items) {
        type_lookup_names(&i->extends, names, errs);
        TypeParamDefaultScope saved = identifier_type_default_scope;
        identifier_type_default_scope = (TypeParamDefaultScope){ me, i_index };
        type_lookup_names(&i->fallback, names, errs);
        identifier_type_default_scope = saved;
    }
}

void generic_typecheck(Generic const* me, ErrorCtx* errs) {
    bool found_optional = false;
    for_each (i, vec_generic_item, &my items) {
        type_typecheck(&i->extends, errs);
        type_typecheck(&i->fallback, errs);
        // tsc's checkTypeParameter: hasNonCircularBaseConstraint
        if (i->extends_explicit && span_valid(i->extends_span) && i->symbol_ref &&
            ty_param_constraint_is_circular(current_checker, ty_type_param(current_checker, i->symbol_ref))
        ) {
            error_ctx_error_d(errs, i->extends_span, 2313, "Type parameter '", i->name, "' has a circular constraint.");
        }
        if (found_optional && !i->fallback_explicit) {
            error_ctx_error_d(
                errs, i->full_span, 2706,
                "Required type parameters may not follow optional type parameters."
            );
        }
        if (i->fallback_explicit) {
            found_optional = true;
        }
    }
}
