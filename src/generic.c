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

#define i_key_class Type
#include "stc/vec.h"

#define i_key_class vec_Type
#include "stc/vec.h"

void GenericItem_drop(GenericItem* self) {
    cstr_drop(&self->name);
    Type_drop(&self->extends);
    Type_drop(&self->fallback);
}

GenericItem GenericItem_clone(GenericItem self) {
    return (GenericItem){
        .name = cstr_clone(self.name),
        .extends = Type_clone(self.extends),
        .fallback_explicit = self.fallback_explicit,
        .fallback = Type_clone(self.fallback),
    };
}

void cstr_append_vec_generic_item_debug(cstr* target, vec_GenericItem* generics, int indent) {
    if (vec_GenericItem_empty(generics)) {
        return;
    }
    cstr_append_sv(target, c_sv("<"));
    c_foreach(i, vec_GenericItem, *generics) {
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

void Generic_drop(Generic* self) {
    vec_GenericItem_drop(&self->items);
    Type_drop(&self->inner);
}

Generic Generic_clone(Generic self) {
    return (Generic) {
        .items = vec_GenericItem_clone(self.items),
        .inner = Type_clone(self.inner),
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

void Generic_resolve_names(Generic* self, NameStack* names, ErrorCtx* errs) {
    NameStack_push_frame(names, true);
    long generic_count = vec_GenericItem_size(&self->items);
    for (long i = 0; i < generic_count; i++) {
        if (!NameStack_add_type(
            names,
            cstr_clone(vec_GenericItem_at(&self->items, i)->name),
            &vec_GenericItem_at(&self->items, i)->extends,
            true, i
        )) {
            // name is cstr_str(&result.ref->first)
            ErrorCtx_type_error(errs, "multiple function generics with same name");
        }
    }
    Type_resolve_names(&self->inner, names, errs);
    NameStack_pop_frame(names);
}

typedef struct TraverseCtx {
    int generic_depth;
    // array of vecs, candidates_len is number of vec_Type
    vec_Type* candidates;
    int candidates_len;
} TraverseCtx;

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
            collect_generic_candidates(generic, TernaryType_resolve(arg->ternary), ctx);
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
                vec_Type_push(ctx.candidates + index, Type_clone(*arg));
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
            PANIC("inferring types of generic with callable not yet implemented");

        case TYPE_INTERFACE:
            PANIC("inferring types of generic with interface not yet implemented");
            break;

        case TYPE_TERNARY:
            PANIC("inferring types of generic with ternary not yet implemented");
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
                Type_drop(self);
                if (vec_Type_empty(ctx.candidates + index)) {
                    *self = Type_unkown;
                } else {
                    *self = Type_clone(*ctx.candidates[index].data);
                }
            } else {
                if (self->identifier.generic_depth > ctx.generic_depth) {
                    self->identifier.generic_depth--;
                }
                // Hopefully assumption that named types can't refer to generic params holds
            }
            return;

        case TYPE_GENERIC: {
            c_foreach(i, vec_GenericItem, self->generic->items) {
                swap_generics_for_specific_types(&i.ref->extends, ctx);
                swap_generics_for_specific_types(&i.ref->fallback, ctx);
            }
            TraverseCtx sub = ctx;
            sub.generic_depth++;
            swap_generics_for_specific_types(&self->generic->inner, sub);
        }   return;

        case TYPE_CALLABLE:
            c_foreach(i, vec_CallableTypeArg, self->callable->args) {
                swap_generics_for_specific_types(&i.ref->type, ctx);
            }
            swap_generics_for_specific_types(&self->callable->ret, ctx);
            return;

        case TYPE_INTERFACE:
            c_foreach(i, vec_InterfaceMember, self->interface.members) {
                swap_generics_for_specific_types(&i.ref->type, ctx);
            }
            return;

        case TYPE_TERNARY:
            swap_generics_for_specific_types(&self->ternary->conditional.maybeSub, ctx);
            swap_generics_for_specific_types(&self->ternary->conditional.maybeSuper, ctx);
            swap_generics_for_specific_types(&self->ternary->trueType, ctx);
            swap_generics_for_specific_types(&self->ternary->falseType, ctx);
            return;
    }
    UNREACHABLE;
}

Type Generic_callable_infer(Generic const* self, vec_Expr* args, ErrorCtx* errs) {
    if (self->inner.tag != TYPE_CALLABLE) {
        ErrorCtx_type_error(errs, "Generic is not callable");
        return Type_any;
    }
    CallableType const* callable = self->inner.callable;
    int generic_count = vec_GenericItem_size(&self->items);
    int call_arg_count = vec_Expr_size(args);
    int callable_arg_count = vec_CallableTypeArg_size(&callable->args);
    vec_vec_Type candidates = vec_vec_Type_with_size(generic_count, vec_Type_init());
    for (int i = 0; i < callable_arg_count && i < call_arg_count; i++) {
        Type call_arg_type = Expr_get_type_and_typecheck(vec_Expr_at_mut(args, i), errs);
        collect_generic_candidates(
            &vec_CallableTypeArg_at(&self->inner.callable->args, i)->type,
            &call_arg_type,
            (TraverseCtx){
                .generic_depth = 1,
                .candidates = candidates.data,
                .candidates_len = generic_count,
            }
        );
        Type_drop(&call_arg_type);
    }
    Type ret = Type_clone(self->inner);
    swap_generics_for_specific_types(
        &ret,
        (TraverseCtx){
            .generic_depth = 1,
            .candidates = candidates.data,
            .candidates_len = generic_count,
        }
    );
    vec_vec_Type_drop(&candidates);
    return ret;
}
