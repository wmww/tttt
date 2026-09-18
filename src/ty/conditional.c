#include "ty/ty_internal.h"

// Resolution of conditional types: `Check extends Extends ? True : False`, with inference for the `infer X` type
// parameters in the extends type. Distribution over unions happens at instantiation (instantiate.c), since that's
// where the check type parameter gets its union.

#define MAX_INFER_PARAMS 16
#define MAX_INFER_DEPTH 24

typedef struct Inference {
    Ty const* params[MAX_INFER_PARAMS];
    Ty const* candidates[MAX_INFER_PARAMS]; // union of what was inferred so far, null if nothing
    u32 len;
    u32 depth;
    // Pairs being inferred between further up the stack
    Ty const* visiting_sources[MAX_INFER_DEPTH];
    Ty const* visiting_targets[MAX_INFER_DEPTH];
} Inference;

static void infer_from(Checker* me, Inference* inf, Ty const* source, Ty const* target);

static void infer_candidate(Checker* me, Inference* inf, Ty const* param, Ty const* candidate) {
    for (u32 i = 0; i < inf->len; i++) {
        if (inf->params[i] == param) {
            if (inf->candidates[i] && inf->candidates[i] != candidate) {
                Ty const* parts[2] = { inf->candidates[i], candidate };
                candidate = ty_union(me, parts, 2, nullptr, (TyList){0});
            }
            inf->candidates[i] = candidate;
            return;
        }
    }
    if (inf->len == MAX_INFER_PARAMS) return;
    inf->params[inf->len] = param;
    inf->candidates[inf->len++] = candidate;
}

static bool is_tuple(Ty const* ty) {
    return ty->kind == TY_REFERENCE && ty->reference.target->declared.kind == TY_DECLARED_TUPLE_TARGET;
}

static void infer_from_signatures(Checker* me, Inference* inf, Signature const* source, Signature const* target) {
    signature_resolve(me, source);
    signature_resolve(me, target);
    for (u32 i = 0; i < target->param_count; i++) {
        SigParam const* target_param = &target->params[i];
        if (target_param->rest) {
            // `(...args: infer A) => any`: the rest of the source's parameters as a tuple
            if (i < source->param_count && source->params[i].rest && i + 1 == source->param_count) {
                infer_from(me, inf, source->params[i].type, target_param->type);
            } else {
                u32 arity = source->param_count > i ? source->param_count - i : 0;
                unsigned char* flags = calloc(arity ? arity : 1, 1);
                Atom const** names = calloc(arity ? arity : 1, sizeof(Atom const*));
                Ty const** elements = calloc(arity ? arity : 1, sizeof(Ty const*));
                for (u32 j = 0; j < arity; j++) {
                    SigParam const* source_param = &source->params[i + j];
                    flags[j] = (source_param->optional ? TUPLE_ELEMENT_OPTIONAL : 0) | (source_param->rest ? TUPLE_ELEMENT_REST : 0);
                    names[j] = source_param->name;
                    elements[j] = source_param->type;
                }
                TupleShape shape = { .arity = arity, .flags = flags, .names = names };
                Ty const* tuple_target = ty_tuple_target(me, &shape);
                Ty const* tuple = arity ? ty_reference(me, tuple_target, (TyList){ .data = elements, .len = arity }) : tuple_target;
                infer_from(me, inf, tuple, target_param->type);
                free(flags);
                free(names);
                free(elements);
            }
            break;
        }
        if (i >= source->param_count) break;
        infer_from(me, inf, source->params[i].type, target_param->type);
    }
    infer_from(me, inf, signature_return_type(me, source), signature_return_type(me, target));
}

static void infer_from_objects(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    if (is_tuple(source) && is_tuple(target)) {
        TupleShape const* source_shape = source->reference.target->declared.tuple;
        TupleShape const* target_shape = target->reference.target->declared.tuple;
        TyList source_args = ty_reference_args(me, source);
        TyList target_args = ty_reference_args(me, target);
        for (u32 i = 0; i < target_shape->arity; i++) {
            if (target_shape->flags[i] & TUPLE_ELEMENT_REST) {
                // `[infer H, ...infer R]`: a trailing rest element gets what's left of the source
                if (i + 1 != target_shape->arity || i > source_shape->arity) break;
                u32 arity = source_shape->arity - i;
                TupleShape shape = {
                    .arity = arity,
                    .readonly = source_shape->readonly,
                    .flags = source_shape->flags + i,
                    .names = source_shape->names ? source_shape->names + i : nullptr,
                };
                Ty const* rest_target = ty_tuple_target(me, &shape);
                Ty const* rest = arity ?
                    ty_reference(me, rest_target, (TyList){ .data = source_args.data + i, .len = arity }) :
                    rest_target;
                infer_from(me, inf, rest, target_args.data[i]);
                break;
            }
            if (i >= source_shape->arity) break;
            infer_from(me, inf, source_args.data[i], target_args.data[i]);
        }
        return;
    }
    if (source->kind == TY_REFERENCE && target->kind == TY_REFERENCE && source->reference.target == target->reference.target) {
        TyList source_args = ty_reference_args(me, source);
        TyList target_args = ty_reference_args(me, target);
        for (u32 i = 0; i < source_args.len && i < target_args.len; i++) {
            infer_from(me, inf, source_args.data[i], target_args.data[i]);
        }
        return;
    }
    if (is_tuple(source) && target->kind == TY_REFERENCE && ty_reference_args(me, target).len == 1) {
        // A tuple against an array: each element against the element type
        TyList source_args = ty_reference_args(me, source);
        for (u32 i = 0; i < source_args.len; i++) infer_from(me, inf, source_args.data[i], ty_reference_args(me, target).data[0]);
        return;
    }
    Members const* target_members = ty_members(me, target);
    for (u32 i = 0; i < target_members->prop_count; i++) {
        Prop const* target_prop = target_members->props[i];
        Prop const* source_prop = ty_get_prop(me, source, target_prop->name);
        if (source_prop) infer_from(me, inf, prop_type(me, source_prop), prop_type(me, target_prop));
    }
    Members const* source_members = ty_members(me, source);
    if (target_members->call_count && source_members->call_count) {
        // The last signatures, which for overloads is the most general one
        infer_from_signatures(
            me, inf, source_members->call[source_members->call_count - 1], target_members->call[target_members->call_count - 1]
        );
    }
    if (target_members->construct_count && source_members->construct_count) {
        infer_from_signatures(
            me, inf, source_members->construct[source_members->construct_count - 1],
            target_members->construct[target_members->construct_count - 1]
        );
    }
    for (u32 i = 0; i < target_members->index_count; i++) {
        for (u32 j = 0; j < source_members->index_count; j++) {
            if (source_members->index[j].key == target_members->index[i].key) {
                infer_from(me, inf, source_members->index[j].value, target_members->index[i].value);
            }
        }
    }
}

static void infer_from(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    if (!(target->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return;
    if (target->kind == TY_TYPE_PARAM) {
        if (target->param.state->is_infer) infer_candidate(me, inf, target, source);
        return;
    }
    if (inf->depth == MAX_INFER_DEPTH) return;
    for (u32 i = 0; i < inf->depth; i++) {
        if (inf->visiting_sources[i] == source && inf->visiting_targets[i] == target) return;
    }
    inf->visiting_sources[inf->depth] = source;
    inf->visiting_targets[inf->depth] = target;
    inf->depth++;
    if (target->kind == TY_UNION || target->kind == TY_INTERSECTION) {
        // To each member. TODO: tsc first matches up identical members and infers only from what's left to the
        // naked type parameters
        for (u32 i = 0; i < target->composite.types.len; i++) {
            Ty const* member = target->composite.types.data[i];
            if (source->kind == TY_UNION && member->kind != TY_TYPE_PARAM) {
                for (u32 j = 0; j < source->composite.types.len; j++) {
                    infer_from(me, inf, source->composite.types.data[j], member);
                }
            } else {
                infer_from(me, inf, source, member);
            }
        }
    } else if (source->kind == TY_UNION) {
        for (u32 i = 0; i < source->composite.types.len; i++) {
            infer_from(me, inf, source->composite.types.data[i], target);
        }
    } else if (target->kind == TY_KEYOF && source->kind == TY_KEYOF) {
        infer_from(me, inf, source->keyof.operand, target->keyof.operand);
    } else if (target->kind == TY_INDEXED && source->kind == TY_INDEXED) {
        infer_from(me, inf, source->indexed.object, target->indexed.object);
        infer_from(me, inf, source->indexed.index, target->indexed.index);
    } else if ((target->flags & TYF_OBJECT) && (source->flags & TYF_OBJECT)) {
        infer_from_objects(me, inf, source, target);
    }
    inf->depth--;
}

// Generic as far as deferring a conditional type goes: type variables at the top level, not inside object types
// (`{ a: T } extends { a: infer U }` resolves, like in tsc)
static bool is_generic(Ty const* ty) {
    if (ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED)) return true;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return false;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_generic(ty->composite.types.data[i])) return true;
        }
    }
    return false;
}

static Ty const* branch_type(Checker* me, TyCondBranch const* branch) {
    TyCondBranch copy = *branch;
    return ty_cond_branch_type(me, &copy);
}

Ty const* ty_conditional_resolve(
    Checker* me, Ty const* check_type, Ty const* extends, TyCondBranch const* if_true, TyCondBranch const* if_false
) {
    if (is_generic(check_type)) return nullptr;
    if ((check_type->flags | extends->flags) & TYF_ERROR) return ty_intrinsic(me, TY_ERROR);

    Inference* inf = nullptr;
    TyMapper const* mapper = nullptr;
    if (extends->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
        inf = calloc(1, sizeof(Inference));
        panic_if(!inf, "out of memory");
        infer_from(me, inf, check_type, extends);
        if (inf->len) {
            for (u32 i = 0; i < inf->len; i++) {
                // TODO: an inference that doesn't satisfy the constraint of `infer X extends C` fails the check
                if (!inf->candidates[i]) inf->candidates[i] = ty_intrinsic(me, TY_UNKNOWN_);
            }
            mapper = ty_mapper_new(
                me, (TyList){ .data = inf->params, .len = inf->len }, (TyList){ .data = inf->candidates, .len = inf->len }
            );
        }
        free(inf);
    }
    Ty const* inferred_extends = ty_instantiate(me, extends, mapper);
    if (is_generic(inferred_extends)) return nullptr;

    if (check_type->flags & TYF_ANY) {
        Ty const* parts[2] = { ty_instantiate(me, branch_type(me, if_true), mapper), branch_type(me, if_false) };
        return ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    bool related = (inferred_extends->flags & (TYF_ANY | TYF_UNKNOWN)) ||
        ty_is_related(me, check_type, inferred_extends, TY_RELATION_ASSIGNABLE);
    if (related) return ty_instantiate(me, branch_type(me, if_true), mapper);
    // HACK: tsc picks the false branch only if the check fails even with every type variable replaced by `any`
    // (permissive instantiation), and defers otherwise. There's no such instantiation here yet: with type variables
    // nested in either side, a failed check defers
    if ((check_type->flags | inferred_extends->flags) & TYF_COULD_CONTAIN_TYPE_VARIABLES) return nullptr;
    return branch_type(me, if_false);
}
