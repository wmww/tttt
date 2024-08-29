#include "generic.h"
#include "name_stack.h"
#include "several_types.h"
#include "callable_type.h"
#include "interface.h"
#include "interface_member.h"
#include "expr.h"
#include "error.h"

typedef NamedType const* NamedTypeCPtr;
#define i_key_names NamedTypeCPtr, named_type_cptr
#define i_val_class_names VecType, vec_type
#include "wrap_stc/hmap.h"

void generic_item_drop(GenericItem* me) {
    type_drop(&my extends);
    type_drop(&my fallback);
    if (my owns_named_type) {
        named_type_destroy(my named_type);
    }
}

GenericItem generic_item_clone(GenericItem me) {
    me.extends = type_clone(me.extends);
    me.fallback = type_clone(me.fallback);
    me.owns_named_type = false;
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

void generic_resolve_names(Generic const* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_generic_item, my items) {
        if (!i.ref->named_type) {
            i.ref->owns_named_type = true;
            i.ref->named_type = make_named_type(i.ref->name, i.ref->name, type_ref(&i.ref->extends), nullptr);
        }
        name_stack_add_type(names, i.ref->named_type);
    }
    c_foreach(i, vec_generic_item, my items) {
        type_resolve_names(&i.ref->extends, names, errs);
        type_resolve_names(&i.ref->fallback, names, errs);
    }
}

static void collect_generic_candidates(
    Type const* me,
    Type const* arg,
    VecTypeRefinement* results,
    ErrorCtx* errs
) {
    arg = type_resolve(arg);
    me  = type_resolve_until_identifier(me);

    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_VALUE:
            return;

        case TYPE_IDENTIFIER:
            if (my identifier->named_type) {
                c_foreach(i, vec_type_refinement, *results) {
                    if (i.ref->target == my identifier->named_type) {
                        i.ref->refined = arg;
                        break;
                    }
                }
            }
            return;

        case TYPE_CALLABLE:
            error_ctx_error(errs, span_null, TS_CODE_NONE,
                "inferring types of generic with callable not yet implemented"
            );
            return;

        case TYPE_INTERFACE:
            error_ctx_error(errs, span_null, TS_CODE_NONE,
                "inferring types of generic with interface not yet implemented"
            );
            return;

        case TYPE_ARRAY:
            error_ctx_error(errs, span_null, TS_CODE_NONE,
                "inferring types of generic with array not yet implemented"
            );
            return;

        case TYPE_TUPLE:
            error_ctx_error(errs, span_null, TS_CODE_NONE,
                "inferring types of generic with tuple not yet implemented"
            );
            return;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            error_ctx_error(errs, span_null, TS_CODE_NONE,
                "inferring types of generic with union/intersection not yet implemented"
            );
            return;

        case TYPE_INFERRED:
        case TYPE_INDEXED:
        case TYPE_KEYOF:
        case TYPE_TERNARY:
        case TYPE_ABSTRACTED:
        case TYPE_LAZY:
        case TYPE_REF:
            PANIC("Type with tag %d should have been resolved away", my tag);
    }
    UNREACHABLE;
}

static inline void generic_replace_named_types_with(Generic* me, VecTypeRefinement const* refinements) {
    c_foreach(i, vec_generic_item, my items) {
        // Is this needed?
        // replace_named_types_with(&i.ref->extends, refinements);
        replace_named_types_with(&i.ref->fallback, refinements);
    }
}

static inline void callable_replace_named_types_with(CallableType* me, VecTypeRefinement const* refinements) {
    generic_replace_named_types_with(&my generic, refinements);
    c_foreach(i, vec_callable_arg, my args) {
        replace_named_types_with(&i.ref->type, refinements);
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
        case TYPE_VALUE:
            return;

        case TYPE_IDENTIFIER:
            c_foreach(i, vec_spanned_type, my identifier->generic_params) {
                replace_named_types_with(&i.ref->type, refinements);
            }
            if (my identifier->named_type) {
                Type const* refinement = nullptr;
                c_foreach(i, vec_type_refinement, *refinements) {
                    if (i.ref->target == my identifier->named_type) {
                        refinement = i.ref->refined;
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
            c_foreach(i, vec_interface_member, my interface.members) {
                switch(i.ref->key.tag) {
                    case INTERFACE_KEY_NAME: break;
                    case INTERFACE_KEY_INDEX: replace_named_types_with(&i.ref->key.index, refinements); break;
                    case INTERFACE_KEY_EXPR: break;
                    case INTERFACE_KEY_MAPPED:
                        replace_named_types_with(&i.ref->key.mapped.in_type, refinements);
                        replace_named_types_with(&i.ref->key.mapped.as_type, refinements);
                        break;
                    case INTERFACE_KEY_CALLABLE: break;
                }
                replace_named_types_with(&i.ref->type, refinements);
            }
            return;

        case TYPE_ARRAY:
            replace_named_types_with(my array.element, refinements);
            return;

        case TYPE_TUPLE:
            c_foreach(i, vec_tuple_element, my tuple.elements) {
                replace_named_types_with(&i.ref->type, refinements);
            }
            return;

        case TYPE_INDEXED:
            replace_named_types_with(&my indexed->interface, refinements);
            replace_named_types_with(&my indexed->index, refinements);
            if (my indexed->has_cache) {
                type_drop(&my indexed->cached);
                my indexed->cached = (Type){0};
                my indexed->has_cache = false;
            }
            return;

        case TYPE_KEYOF:
            replace_named_types_with(&my keyof->interface, refinements);
            if (my keyof->has_cache) {
                type_drop(&my keyof->cached);
                my keyof->cached = (Type){0};
                my keyof->has_cache = false;
            }
            return;

        case TYPE_TERNARY:
            replace_named_types_with(&my ternary->maybe_sub, refinements);
            replace_named_types_with(&my ternary->maybe_super, refinements);
            replace_named_types_with(&my ternary->if_true, refinements);
            replace_named_types_with(&my ternary->if_false, refinements);
            if (my ternary->has_cache) {
                type_drop(&my ternary->cached);
                my ternary->cached = (Type){0};
                my ternary->has_cache = false;
            }
            return;

        case TYPE_ABSTRACTED:
            replace_named_types_with(&my abstracted->inner, refinements);
            if (my abstracted->has_cache) {
                type_drop(&my abstracted->cached);
                my abstracted->cached = (Type){0};
                my abstracted->has_cache = false;
            }
            return;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                replace_named_types_with(i.ref, refinements);
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
    }
    UNREACHABLE;
}

Type generic_degenericize_type(Generic const* me, Type t, VecSpannedType const* generic_params) {
    VecTypeRefinement refinements = vec_type_refinement_with_capacity(vec_generic_item_size(&my items));
    c_foreach(i, vec_generic_item, my items) {
        intptr_t index = i.ref - my items.data;
        if (i.ref->named_type) {
            vec_type_refinement_push(&refinements, (TypeRefinement){
                .target = i.ref->named_type,
                .refined = generic_param_at(generic_params, index),
            });
        } else {
            NONFATAL("Generic %.*s type is null", c_SV(span_sv(&i.ref->name)));
        }
    }
    replace_named_types_with(&t, &refinements);
    vec_type_refinement_drop(&refinements);
    return t;
}

CallableType generic_infer_callable_types(
    Generic const* me,
    CallableType* callable,
    VecExpr const* args,
    ErrorCtx* errs
) {
    int call_arg_count = vec_expr_size(args);
    int callable_arg_count = vec_callable_arg_size(&callable->args);
    VecTypeRefinement refinements = {0};
    c_foreach(i, vec_generic_item, my items) {
        if (i.ref->named_type) {
            vec_type_refinement_push(&refinements, (TypeRefinement){
                .target = i.ref->named_type,
                .refined = nullptr,
            });
        } else {
            NONFATAL("Generic %.*s type is null", c_SV(span_sv(&i.ref->name)));
        }
    }
    VecType arg_types = {0};
    c_foreach(i, vec_expr, *args) {
        vec_type_push(&arg_types, expr_get_type_and_typecheck(i.ref, nullptr));
    }
    for (int i = 0; i < callable_arg_count && i < call_arg_count; i++) {
        collect_generic_candidates(
            &vec_callable_arg_at_mut(&callable->args, i)->type,
            vec_type_at(&arg_types, i),
            &refinements,
            errs
        );
    }

    CallableType ret = callable_type_clone(*callable);
    callable_replace_named_types_with(&ret, &refinements);

    vec_type_refinement_drop(&refinements);
    vec_type_drop(&arg_types);

    return ret;
}

void generic_typecheck(Generic const* me, ErrorCtx* errs) {
    bool found_optional = false;
    c_foreach(i, vec_generic_item, my items) {
        type_typecheck(&i.ref->extends, errs);
        type_typecheck(&i.ref->fallback, errs);
        if (found_optional && !i.ref->fallback_explicit) {
            error_ctx_error(
                errs, i.ref->full_span, 2706,
                "Required type parameters may not follow optional type parameters."
            );
        }
        if (i.ref->fallback_explicit) {
            found_optional = true;
        }
    }
}
