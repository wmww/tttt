#include "generic.h"
#include "name_stack.h"
#include "binding.h"
#include "ternary_type.h"
#include "indexed_type.h"
#include "callable_type.h"
#include "interface.h"
#include "interface_member.h"
#include "identifier_type.h"
#include "ternary_type.h"
#include "expr.h"
#include "error.h"

typedef NamedType const* NamedTypeCPtr;
#define i_key_names NamedTypeCPtr, named_type_cptr
#define i_val_class_names VecType, vec_type
#include "wrap_stc/hmap.h"

void generic_item_drop(GenericItem* me) {
    cstr_drop(&my name);
    type_drop(&my extends);
    type_drop(&my fallback);
    arc_named_type_drop(&my type);
}

GenericItem generic_item_clone(GenericItem me) {
    return (GenericItem){
        .name = cstr_clone(me.name),
        .name_span = me.name_span,
        .extends = type_clone(me.extends),
        .fallback_explicit = me.fallback_explicit,
        .fallback = type_clone(me.fallback),
        .type = arc_named_type_clone(me.type),
    };
}

void generic_drop(Generic* me) {
    vec_generic_item_drop(&my items);
}

Generic generic_clone(Generic me) {
    return (Generic) {
        .items = vec_generic_item_clone(me.items),
    };
}

void cstr_append_generic_debug(cstr* target, Generic const* me, int indent) {
    if (generic_empty(me)) {
        return;
    }
    cstr_append_sv(target, c_sv("<"));
    c_foreach(i, vec_generic_item, my items) {
        cstr_append_sv(target, c_sv(CLR_TYPE));
        cstr_append_s(target, i.ref->name);
        cstr_append_sv(target, c_sv(CLR0));
        if (i.ref->extends.tag != TYPE_ANY) {
            cstr_append_sv(target, c_sv(" extends "));
            cstr_append_type_debug(target, &i.ref->extends, indent);
        }
        if (i.ref->fallback_explicit) {
            cstr_append_sv(target, c_sv(" = "));
            cstr_append_type_debug(target, &i.ref->fallback, indent);
        }
        if (i.ref < i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(">"));
}

void cstr_append_generic_error_msg(cstr* target, Generic const* me) {
    if (generic_empty(me)) {
        return;
    }
    cstr_append_sv(target, c_sv("<"));
    c_foreach(i, vec_generic_item, my items) {
        cstr_append_s(target, i.ref->name);
        if (i.ref->extends.tag != TYPE_ANY) {
            cstr_append_sv(target, c_sv(" extends "));
            cstr_append_type_error_msg(target, &i.ref->extends);
        }
        if (i.ref->fallback_explicit) {
            cstr_append_sv(target, c_sv(" = "));
            cstr_append_type_error_msg(target, &i.ref->fallback);
        }
        if (i.ref < i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(">"));
}

void generic_resolve_names(Generic* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_generic_item, my items) {
        if (!name_stack_add_type(names, cstr_clone(i.ref->name), i.ref->name_span, &i.ref->extends, nullptr, false)) {
            // name is cstr_str(&result.ref->first)
            error_ctx_type_error(errs, "multiple generics with same name");
        }
        i.ref->type = arc_named_type_clone(*name_stack_get_type(names, cstr_sv(&i.ref->name)));
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
            if (my identifier->type.get) {
                c_foreach(i, vec_type_refinement, *results) {
                    if (i.ref->target == my identifier->type.get) {
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

        case TYPE_INDEXED:
            error_ctx_error(errs, span_null, TS_CODE_NONE,
                "inferring types of generic with index type not yet implemented"
            );
            return;

        case TYPE_KEYOF:
            error_ctx_error(errs, span_null, TS_CODE_NONE,
                "inferring types of generic with keyof type not yet implemented"
            );
            return;

        case TYPE_TERNARY:
            error_ctx_error(errs, span_null, TS_CODE_NONE,
                "inferring types of generic with ternary not yet implemented"
            );
            return;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            error_ctx_error(errs, span_null, TS_CODE_NONE,
                "inferring types of generic with union/intersection not yet implemented"
            );
            return;
    }
    UNREACHABLE;
}

static inline void generic_replace_named_types_with(Generic* me, VecTypeRefinement const* refinements) {
    c_foreach(i, vec_generic_item, my items) {
        replace_named_types_with(&i.ref->extends, refinements);
        replace_named_types_with(&i.ref->fallback, refinements);
    }
}

static inline void callable_replace_named_types_with(CallableType* me, VecTypeRefinement const* refinements) {
    c_foreach(i, vec_callable_arg, my args) {
        replace_named_types_with(&i.ref->binding.type, refinements);
    }
    replace_named_types_with(&my ret, refinements);
}

void replace_named_types_with(Type* me, VecTypeRefinement const* refinements) {
    Generic* generic = type_get_generic(me);
    if (generic) {
        generic_replace_named_types_with(generic, refinements);
    }
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
            c_foreach(i, vec_type, my identifier->generic_params) {
                replace_named_types_with(i.ref, refinements);
            }
            if (my identifier->type.get) {
                Type const* refinement = nullptr;
                c_foreach(i, vec_type_refinement, *refinements) {
                    if (i.ref->target == my identifier->type.get) {
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

        case TYPE_CALLABLE:
            callable_replace_named_types_with(my callable, refinements);
            return;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                switch(i.ref->key.tag) {
                    case INTERFACE_KEY_NAME: break;
                    case INTERFACE_KEY_INDEX: replace_named_types_with(&i.ref->key.index, refinements); break;
                    case INTERFACE_KEY_MAPPED: break;
                }
                replace_named_types_with(&i.ref->type, refinements);
            }
            return;

        case TYPE_ARRAY:
            replace_named_types_with(my array, refinements);
            return;

        case TYPE_TUPLE:
            c_foreach(i, vec_type, my tuple) {
                replace_named_types_with(i.ref, refinements);
            }
            return;

        case TYPE_INDEXED:
            replace_named_types_with(&my indexed->interface, refinements);
            replace_named_types_with(&my indexed->index, refinements);
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
            return;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                replace_named_types_with(i.ref, refinements);
            }
            return;
    }
    UNREACHABLE;
}

Type generic_degenericize_type(Generic const* me, Type t, VecType const* generic_params) {
    VecTypeRefinement refinements = vec_type_refinement_with_capacity(vec_generic_item_size(&my items));
    c_foreach(i, vec_generic_item, my items) {
        intptr_t index = i.ref - my items.data;
        if (i.ref->type.get) {
            vec_type_refinement_push(&refinements, (TypeRefinement){
                .target = i.ref->type.get,
                .refined = index < vec_type_size(generic_params) ?
                    vec_type_at(generic_params, index) :
                    i.ref->type.get->type
            });
        } else {
            NONFATAL("Generic %s type is null", cstr_str(&i.ref->name));
        }
    }
    replace_named_types_with(&t, &refinements);
    vec_type_refinement_drop(&refinements);
    return t;
}

CallableType generic_infer_callable_types(
    Generic const* me,
    CallableType const* callable,
    VecExpr const* args,
    ErrorCtx* errs
) {
    int call_arg_count = vec_expr_size(args);
    int callable_arg_count = vec_callable_arg_size(&callable->args);
    VecTypeRefinement refinements = {0};
    c_foreach(i, vec_generic_item, my items) {
        if (i.ref->type.get) {
            vec_type_refinement_push(&refinements, (TypeRefinement){
                .target = i.ref->type.get,
                .refined = nullptr,
            });
        } else {
            NONFATAL("Generic %s type is null", cstr_str(&i.ref->name));
        }
    }
    VecType arg_types = {0};
    c_foreach(i, vec_expr, *args) {
        vec_type_push(&arg_types, expr_get_type_and_typecheck(i.ref, errs));
    }
    for (int i = 0; i < callable_arg_count && i < call_arg_count; i++) {
        collect_generic_candidates(
            &vec_callable_arg_at(&callable->args, i)->binding.type,
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
