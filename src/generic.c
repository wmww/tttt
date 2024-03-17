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

#define i_key_class_names VecType, vec_type
#include "wrap_stc/vec.h"

void generic_item_drop(GenericItem* self) {
    cstr_drop(&self->name);
    type_drop(&self->extends);
    type_drop(&self->fallback);
}

GenericItem generic_item_clone(GenericItem self) {
    return (GenericItem){
        .name = cstr_clone(self.name),
        .extends = type_clone(self.extends),
        .fallback_explicit = self.fallback_explicit,
        .fallback = type_clone(self.fallback),
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

void generic_drop(Generic* self) {
    vec_generic_item_drop(&self->items);
    type_drop(&self->inner);
}

Generic generic_clone(Generic self) {
    return (Generic) {
        .items = vec_generic_item_clone(self.items),
        .inner = type_clone(self.inner),
    };
}

void cstr_append_generic_debug(cstr* target, Generic* self, int indent) {
    if (self->inner.tag == TYPE_CALLABLE) {
        cstr_append_vec_generic_item_debug(target, &self->items, indent);
        cstr_append_type_debug(target, &self->inner, indent);
    } else {
        cstr_append_type_debug(target, &self->inner, indent);
        cstr_append_vec_generic_item_debug(target, &self->items, indent);
    }
}

void cstr_append_generic_error_msg(cstr* target, Generic* self) {
    if (self->inner.tag == TYPE_CALLABLE) {
        cstr_append_vec_generic_item_error_msg(target, &self->items);
        cstr_append_type_error_msg(target, &self->inner);
    } else {
        cstr_append_type_error_msg(target, &self->inner);
        cstr_append_vec_generic_item_error_msg(target, &self->items);
    }
}

void generic_resolve_names(Generic* self, NameStack* names, ErrorCtx* errs) {
    name_stack_push_frame(names, true);
    long generic_count = vec_generic_item_size(&self->items);
    for (long i = 0; i < generic_count; i++) {
        if (!name_stack_add_type(
            names,
            cstr_clone(vec_generic_item_at(&self->items, i)->name),
            &vec_generic_item_at(&self->items, i)->extends,
            true, i
        )) {
            // name is cstr_str(&result.ref->first)
            error_ctx_type_error(errs, "multiple function generics with same name");
        }
    }
    type_resolve_names(&self->inner, names, errs);
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
            if (arg->identifier.type) {
                collect_generic_candidates(generic, arg->identifier.type, ctx);
            }
            return;

        case TYPE_TERNARY:
            collect_generic_candidates(generic, ternary_type_resolve(arg->ternary), ctx);
            return;

        default: break;
    }

    switch (generic->tag) {
        case TYPE_ANY:
        case TYPE_UNKOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_VALUE:
            return;

        case TYPE_IDENTIFIER:
            if (generic->identifier.generic_depth == ctx.generic_depth) {
                int index = generic->identifier.generic_index;
                if (index > ctx.candidates_len || index < 0) {
                    PANIC(
                        "generic %s has invalid index %d (there are only %d generic params)",
                        cstr_str(&generic->identifier.name),
                        generic->identifier.generic_index,
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

static void swap_generics_for_specific_types(Type* self, TraverseCtx ctx) {
    switch (self->tag) {
        case TYPE_ANY:
        case TYPE_UNKOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_VALUE:
            return;

        case TYPE_IDENTIFIER:
            if (self->identifier.generic_depth == ctx.generic_depth) {
                int index = self->identifier.generic_index;
                if (index > ctx.candidates_len || index < 0) {
                    PANIC(
                        "generic %s has invalid index %d (there are only %d generic params)",
                        cstr_str(&self->identifier.name),
                        self->identifier.generic_index,
                        ctx.candidates_len
                    );
                }
                type_drop(self);
                if (vec_type_empty(ctx.candidates + index)) {
                    *self = type_unkown;
                } else {
                    *self = type_clone(*ctx.candidates[index].data);
                }
            } else {
                if (self->identifier.generic_depth > ctx.generic_depth) {
                    self->identifier.generic_depth--;
                }
                // Hopefully assumption that named types can't refer to generic params holds
            }
            return;

        case TYPE_GENERIC: {
            c_foreach(i, vec_generic_item, self->generic->items) {
                swap_generics_for_specific_types(&i.ref->extends, ctx);
                swap_generics_for_specific_types(&i.ref->fallback, ctx);
            }
            TraverseCtx sub = ctx;
            sub.generic_depth++;
            swap_generics_for_specific_types(&self->generic->inner, sub);
        }   return;

        case TYPE_CALLABLE:
            c_foreach(i, vec_callable_type_arg, self->callable->args) {
                swap_generics_for_specific_types(&i.ref->type, ctx);
            }
            swap_generics_for_specific_types(&self->callable->ret, ctx);
            return;

        case TYPE_INTERFACE:
            c_foreach(i, vec_interface_member, self->interface.members) {
                swap_generics_for_specific_types(&i.ref->type, ctx);
            }
            return;

        case TYPE_ARRAY:
            swap_generics_for_specific_types(self->array, ctx);
            return;

        case TYPE_TERNARY:
            swap_generics_for_specific_types(&self->ternary->conditional.MaybeSub, ctx);
            swap_generics_for_specific_types(&self->ternary->conditional.MaybeSuper, ctx);
            swap_generics_for_specific_types(&self->ternary->TrueType, ctx);
            swap_generics_for_specific_types(&self->ternary->FalseType, ctx);
            return;
    }
    UNREACHABLE;
}

Type generic_callable_infer(Generic const* self, VecExpr* args, ErrorCtx* errs) {
    if (self->inner.tag != TYPE_CALLABLE) {
        error_ctx_type_error(errs, "Generic is not callable");
        return type_any;
    }
    CallableType const* callable = self->inner.callable;
    int generic_count = vec_generic_item_size(&self->items);
    int call_arg_count = vec_expr_size(args);
    int callable_arg_count = vec_callable_type_arg_size(&callable->args);
    VecVecType candidates = vec_vec_type_with_size(generic_count, vec_type_init());
    for (int i = 0; i < callable_arg_count && i < call_arg_count; i++) {
        Type call_arg_type = expr_get_type_and_typecheck(vec_expr_at_mut(args, i), errs);
        collect_generic_candidates(
            &vec_callable_type_arg_at(&self->inner.callable->args, i)->type,
            &call_arg_type,
            traverse_ctx_make(&candidates, errs)
        );
        type_drop(&call_arg_type);
    }
    Type ret = type_clone(self->inner);
    swap_generics_for_specific_types(
        &ret,
        traverse_ctx_make(&candidates, errs)
    );
    vec_vec_type_drop(&candidates);
    return ret;
}
