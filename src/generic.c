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

GenericItem generic_item_clone(GenericItem me) {
    me.extends = type_clone(me.extends);
    me.fallback = type_clone(me.fallback);
    me.owned_declaration = (DeclHandle){0};
    return me;
}

void generic_drop(Generic* me) {
    vec_generic_item_drop(&my items);
}

Generic generic_clone(Generic me) {
    return (Generic) {
        .items = vec_generic_item_clone(me.items),
    };
}

void generic_add_names(Generic const* me, Scope* scope) {
    for_each (i, vec_generic_item, &my items) {
        scope_init_declaration(scope, &i->owned_declaration, (DeclProps){
            .name = i->name,
            .full = i->name,
            .tag = DECLARATION_GENERIC_TYPE,
            .declared_type = type_refcopy(&i->extends),
        });
        i->declaration_ref = i->owned_declaration.get;
    }
}

void generic_lookup_names(Generic const* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_generic_item, &my items) {
        type_lookup_names(&i->extends, names, errs);
        type_lookup_names(&i->fallback, names, errs);
    }
}

void generic_typecheck(Generic const* me, ErrorCtx* errs) {
    bool found_optional = false;
    for_each (i, vec_generic_item, &my items) {
        type_typecheck(&i->extends, errs);
        type_typecheck(&i->fallback, errs);
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
