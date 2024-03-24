#include "generic.h"
#include "err.h"
#include "name_stack.h"
#include "ternary_type.h"
#include "callable_type.h"
#include "interface.h"
#include "interface_member.h"
#include "ternary_type.h"
#include "extends_conditional.h"
#include "expr.h"
#include "error.h"

#define i_key_class_names VecType, vec_type
#include "wrap_stc/vec.h"

void generic_item_drop(GenericItem* me) {
    cstr_drop(&my name);
    type_drop(&my extends);
    type_drop(&my fallback);
}

GenericItem generic_item_clone(GenericItem me) {
    return (GenericItem){
        .name = cstr_clone(me.name),
        .extends = type_clone(me.extends),
        .fallback_explicit = me.fallback_explicit,
        .fallback = type_clone(me.fallback),
    };
}

void cstr_append_vec_generic_item_debug(cstr* target, VecGenericItem* generics, int indent) {
    if (vec_generic_item_empty(generics)) {
        return;
    }
    cstr_append_sv(target, c_sv("<"));
    c_foreach(i, vec_generic_item, *generics) {
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

void cstr_append_vec_generic_item_error_msg(cstr* target, VecGenericItem* generics) {
    if (vec_generic_item_empty(generics)) {
        return;
    }
    cstr_append_sv(target, c_sv("<"));
    c_foreach(i, vec_generic_item, *generics) {
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

void generic_drop(Generic* me) {
    vec_generic_item_drop(&my items);
    type_drop(&my inner);
}

Generic generic_clone(Generic me) {
    return (Generic) {
        .items = vec_generic_item_clone(me.items),
        .inner = type_clone(me.inner),
    };
}

void cstr_append_generic_debug(cstr* target, Generic* me, int indent) {
    if (my inner.tag == TYPE_CALLABLE) {
        cstr_append_vec_generic_item_debug(target, &my items, indent);
        cstr_append_type_debug(target, &my inner, indent);
    } else {
        cstr_append_type_debug(target, &my inner, indent);
        cstr_append_vec_generic_item_debug(target, &my items, indent);
    }
}

void cstr_append_generic_error_msg(cstr* target, Generic* me) {
    if (my inner.tag == TYPE_CALLABLE) {
        cstr_append_vec_generic_item_error_msg(target, &my items);
        cstr_append_type_error_msg(target, &my inner);
    } else {
        cstr_append_type_error_msg(target, &my inner);
        cstr_append_vec_generic_item_error_msg(target, &my items);
    }
}

void generic_resolve_names(Generic* me, NameStack* names, ErrorCtx* errs) {
    name_stack_push_frame(names, STACK_FRAME_GENERIC);
    long generic_count = vec_generic_item_size(&my items);
    for (long i = 0; i < generic_count; i++) {
        if (!name_stack_add_generic_type(
            names,
            cstr_clone(vec_generic_item_at(&my items, i)->name),
            &vec_generic_item_at(&my items, i)->extends,
            i
        )) {
            // name is cstr_str(&result.ref->first)
            error_ctx_type_error(errs, "multiple function generics with same name");
        }
    }
    type_resolve_names(&my inner, names, errs);
    name_stack_pop_frame(names);
}

typedef struct TraverseCtx {
    int generic_depth;
    // array of vecs, candidates_len is number of VecType
    VecType* candidates;
    int candidates_len;
    ErrorCtx* errs;
} TraverseCtx;

static inline TraverseCtx traverse_ctx_make(VecVecType* candidates, ErrorCtx* errs) {
    return (TraverseCtx) {
        .generic_depth = 1,
        .candidates = candidates->data,
        .candidates_len = vec_vec_type_size(candidates),
        .errs = errs,
    };
}

static void collect_generic_candidates(
    Type const* generic,
    Type const* arg,
    TraverseCtx ctx
) {
    switch (arg->tag) {
        case TYPE_IDENTIFIER:
            if (arg->identifier.type.get) {
                collect_generic_candidates(generic, arg->identifier.type.get->type, ctx);
            }
            return;

        case TYPE_TERNARY:
            collect_generic_candidates(generic, ternary_type_resolve(arg->ternary), ctx);
            return;

        default: break;
    }

    switch (generic->tag) {
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
            if (generic->identifier.generic_depth == ctx.generic_depth) {
                int index = generic->identifier.type.get->generic_index;
                if (index > ctx.candidates_len || index < 0) {
                    PANIC(
                        "generic %s has invalid index %d (there are only %d generic params)",
                        cstr_str(&generic->identifier.name),
                        index,
                        ctx.candidates_len
                    );
                }
                vec_type_push(ctx.candidates + index, type_clone(*arg));
            }
            // Hopefully assumption that named types can't refer to generic params holds
            return;

        case TYPE_GENERIC: {
            TraverseCtx sub = ctx;
            sub.generic_depth++;
            collect_generic_candidates(&generic->generic->inner, arg, sub);
            return;
        }

        case TYPE_CALLABLE:
            error_ctx_error(ctx.errs, span_null, TS_CODE_NONE,
                "inferring types of generic with callable not yet implemented"
            );
            return;

        case TYPE_INTERFACE:
            error_ctx_error(ctx.errs, span_null, TS_CODE_NONE,
                "inferring types of generic with interface not yet implemented"
            );
            return;

        case TYPE_ARRAY:
            error_ctx_error(ctx.errs, span_null, TS_CODE_NONE,
                "inferring types of generic with array not yet implemented"
            );
            return;

        case TYPE_TERNARY:
            error_ctx_error(ctx.errs, span_null, TS_CODE_NONE,
                "inferring types of generic with ternary not yet implemented"
            );
            return;
    }
    UNREACHABLE;
}

static void swap_generics_for_specific_types(Type* me, TraverseCtx ctx) {
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
            if (my identifier.generic_depth == ctx.generic_depth) {
                int index = my identifier.type.get->generic_index;
                if (index > ctx.candidates_len || index < 0) {
                    PANIC(
                        "generic %s has invalid index %d (there are only %d generic params)",
                        cstr_str(&my identifier.name),
                        index,
                        ctx.candidates_len
                    );
                }
                type_drop(me);
                if (vec_type_empty(ctx.candidates + index)) {
                    *me = type_unknown;
                } else {
                    *me = type_clone(*ctx.candidates[index].data);
                }
            } else {
                if (my identifier.generic_depth > ctx.generic_depth) {
                    my identifier.generic_depth--;
                }
                // Hopefully assumption that named types can't refer to generic params holds
            }
            return;

        case TYPE_GENERIC: {
            c_foreach(i, vec_generic_item, my generic->items) {
                swap_generics_for_specific_types(&i.ref->extends, ctx);
                swap_generics_for_specific_types(&i.ref->fallback, ctx);
            }
            TraverseCtx sub = ctx;
            sub.generic_depth++;
            swap_generics_for_specific_types(&my generic->inner, sub);
        }   return;

        case TYPE_CALLABLE:
            c_foreach(i, vec_callable_type_arg, my callable->args) {
                swap_generics_for_specific_types(&i.ref->type, ctx);
            }
            swap_generics_for_specific_types(&my callable->ret, ctx);
            return;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, my interface.members) {
                swap_generics_for_specific_types(&i.ref->type, ctx);
            }
            return;

        case TYPE_ARRAY:
            swap_generics_for_specific_types(my array, ctx);
            return;

        case TYPE_TERNARY:
            swap_generics_for_specific_types(&my ternary->conditional.MaybeSub, ctx);
            swap_generics_for_specific_types(&my ternary->conditional.MaybeSuper, ctx);
            swap_generics_for_specific_types(&my ternary->TrueType, ctx);
            swap_generics_for_specific_types(&my ternary->FalseType, ctx);
            return;
    }
    UNREACHABLE;
}

Type generic_callable_infer(Generic const* me, VecExpr* args, ErrorCtx* errs) {
    if (my inner.tag != TYPE_CALLABLE) {
        error_ctx_type_error(errs, "Generic is not callable");
        return type_any;
    }
    CallableType const* callable = my inner.callable;
    int generic_count = vec_generic_item_size(&my items);
    int call_arg_count = vec_expr_size(args);
    int callable_arg_count = vec_callable_type_arg_size(&callable->args);
    VecVecType candidates = vec_vec_type_with_size(generic_count, vec_type_init());
    for (int i = 0; i < callable_arg_count && i < call_arg_count; i++) {
        Type call_arg_type = expr_get_type_and_typecheck(vec_expr_at_mut(args, i), errs);
        collect_generic_candidates(
            &vec_callable_type_arg_at(&my inner.callable->args, i)->type,
            &call_arg_type,
            traverse_ctx_make(&candidates, errs)
        );
        type_drop(&call_arg_type);
    }
    Type ret = type_clone(my inner);
    swap_generics_for_specific_types(
        &ret,
        traverse_ctx_make(&candidates, errs)
    );
    vec_vec_type_drop(&candidates);
    return ret;
}
