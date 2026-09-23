#include "ty/ty_internal.h"

// Resolution of conditional types: `Check extends Extends ? True : False`, with inference for the `infer X` type
// parameters in the extends type. Distribution over unions happens at instantiation (instantiate.c), since that's
// where the check type parameter gets its union.

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

    TyMapper const* mapper = nullptr;
    if (extends->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
        Inference inf;
        inference_init(&inf, (TyList){0});
        inf.collect_infer_params = true;
        inference_collect_infer_params(me, &inf, extends);
        infer_types(me, &inf, check_type, extends, INFER_PRIORITY_NORMAL);
        if (inf.len) {
            Ty const** params = arena_new_n(&my arena, Ty const*, inf.len);
            Ty const** results = arena_new_n(&my arena, Ty const*, inf.len);
            for (u32 i = 0; i < inf.len; i++) {
                params[i] = inf.params[i].param;
                results[i] = inference_conditional_result(me, &inf, i);
                if (!results[i]) results[i] = ty_intrinsic(me, TY_UNKNOWN_);
                // Nothing inferred, or what doesn't satisfy `infer X extends C` (or an implied constraint: `string` in a
                // template), is the constraint (tsc's getInferredType)
                Ty const* constraint = ty_param_constraint(me, params[i]);
                if (constraint && constraint != params[i] && !(constraint->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
                    !ty_is_related(me, results[i], constraint, TY_RELATION_ASSIGNABLE)
                ) {
                    results[i] = constraint;
                }
            }
            mapper = ty_mapper_new(me, (TyList){ .data = params, .len = inf.len }, (TyList){ .data = results, .len = inf.len });
        }
        inference_drop(&inf);
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
