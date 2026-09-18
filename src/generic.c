#include "generic.h"
#include "ty/ty.h"
#include "scope_stack.h"
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
    arc_declaration_drop(&my owned_declaration);
}

GenericItem generic_item_clone(GenericItem me) {
    me.extends = type_clone(me.extends);
    me.fallback = type_clone(me.fallback);
    me.owned_declaration = arc_declaration_init();
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

void generic_lookup_names(Generic const* me, ScopeStack* names, ErrorCtx* errs) {
    for_each (i, vec_generic_item, &my items) {
        type_lookup_names(&i->extends, names, errs);
        type_lookup_names(&i->fallback, names, errs);
    }
}

static inline void generic_replace_named_types_with(Generic* me, VecTypeRefinement const* refinements) {
    for_each (i, vec_generic_item, &my items) {
        // Is this needed?
        // replace_named_types_with(&i->extends, refinements);
        replace_named_types_with(&i->fallback, refinements);
    }
}

static inline void callable_replace_named_types_with(CallableType* me, VecTypeRefinement const* refinements) {
    generic_replace_named_types_with(&my generic, refinements);
    for_each (i, vec_callable_arg, &my args) {
        replace_named_types_with(&i->type, refinements);
    }
    replace_named_types_with(&my ret, refinements);
}

void replace_named_types_with(Type* me, VecTypeRefinement const* refinements) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
            return;

        case TYPE_IDENTIFIER:
            for_each (i, vec_spanned_type, &my identifier->generic_params) {
                replace_named_types_with(&i->type, refinements);
            }
            if (my identifier->decl_ref) {
                Type const* refinement = nullptr;
                for_each (i, vec_type_refinement, refinements) {
                    if (i->target == my identifier->decl_ref) {
                        refinement = i->refined;
                        break;
                    }
                }
                if (refinement) {
                    type_drop(me);
                    *me = type_clone(*refinement);
                }
            }
            return;

        case TYPE_INFERRED:
            return;

        case TYPE_CALLABLE:
            callable_replace_named_types_with(my callable, refinements);
            return;

        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &my interface.members) {
                switch(i->key.tag) {
                    case INTERFACE_KEY_NAME: break;
                    case INTERFACE_KEY_INDEX: replace_named_types_with(&i->key.index.type, refinements); break;
                    case INTERFACE_KEY_EXPR: break;
                    case INTERFACE_KEY_MAPPED:
                        replace_named_types_with(&i->key.mapped.in_type, refinements);
                        replace_named_types_with(&i->key.mapped.as_type, refinements);
                        break;
                    case INTERFACE_KEY_CALLABLE: break;
                }
                replace_named_types_with(&i->type, refinements);
            }
            return;

        case TYPE_ARRAY:
            replace_named_types_with(my array.element, refinements);
            return;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                replace_named_types_with(&i->type, refinements);
            }
            return;

        case TYPE_INDEXED:
            replace_named_types_with(&my indexed->interface, refinements);
            replace_named_types_with(&my indexed->index, refinements);
            return;

        case TYPE_KEYOF:
            replace_named_types_with(&my keyof->interface, refinements);
            return;

        case TYPE_TYPEOF: return;

        case TYPE_TERNARY:
            replace_named_types_with(&my ternary->maybe_sub, refinements);
            replace_named_types_with(&my ternary->maybe_super, refinements);
            replace_named_types_with(&my ternary->if_true, refinements);
            replace_named_types_with(&my ternary->if_false, refinements);
            return;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                replace_named_types_with(i, refinements);
            }
            return;

        case TYPE_LAZY:
            *me = type_clone(*lazy_type_get(my lazy));
            replace_named_types_with(me, refinements);
            return;

        case TYPE_REF:
            *me = type_clone(*my ref);
            replace_named_types_with(me, refinements);
            return;

        case TYPE_NEW:
            *me = type_new(ty_instantiate_with_refinements(current_checker, type_new_ty(current_checker, me), refinements));
            return;
    }
    impossible;
}

Type generic_degenericize_type(Generic const* me, Type t, VecSpannedType const* generic_params) {
    VecTypeRefinement refinements = vec_type_refinement_with_capacity(vec_generic_item_size(&my items));
    for_each (i, vec_generic_item, &my items) {
        intptr_t index = i_index;
        if (i->declaration_ref) {
            vec_type_refinement_push(&refinements, (TypeRefinement){
                .target = i->declaration_ref,
                .refined = generic_param_at(generic_params, index),
            });
        } else {
            log_error("Generic ", i->name, " type is null");
        }
    }
    replace_named_types_with(&t, &refinements);
    vec_type_refinement_drop(&refinements);
    return t;
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
