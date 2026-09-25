#include "ty/ty_internal.h"

// Resolution of conditional types: `Check extends Extends ? True : False`, with inference for the `infer X` type
// parameters in the extends type. Distribution over unions happens at instantiation (instantiate.c), since that's
// where the check type parameter gets its union.

typedef struct MentionWalk {
    Ty const* bound[16]; // Type parameters of the signatures on the way: not free
    u32 bound_len;
    // Types already walked (a type seen again is either being walked or mentions nothing: either way nothing new).
    // Open addressing on ids, 0 is empty
    u32* visited;
    u32 visited_cap;
    u32 visited_len;
    Ty const* only; // Non-null: the one type parameter that counts
} MentionWalk;

#define MENTION_MAX_VISITED 8192
#define MENTION_MAX_DEPTH 48

// Adds `id`; false if it was there already
static bool mention_visit(MentionWalk* walk, u32 id) {
    if (walk->visited_len * 2 >= walk->visited_cap) {
        u32 cap = walk->visited_cap ? walk->visited_cap * 2 : 128;
        u32* grown = calloc(cap, sizeof(u32));
        panic_if(!grown, "out of memory");
        for (u32 i = 0; i < walk->visited_cap; i++) {
            u32 old = walk->visited[i];
            if (!old) continue;
            u32 j = (old * 0x9e3779b9u) & (cap - 1);
            while (grown[j]) j = (j + 1) & (cap - 1);
            grown[j] = old;
        }
        free(walk->visited);
        walk->visited = grown;
        walk->visited_cap = cap;
    }
    u32 j = (id * 0x9e3779b9u) & (walk->visited_cap - 1);
    while (walk->visited[j]) {
        if (walk->visited[j] == id) return false;
        j = (j + 1) & (walk->visited_cap - 1);
    }
    walk->visited[j] = id;
    walk->visited_len++;
    return true;
}

// Whether a type variable really occurs in the type: TYF_COULD_CONTAIN_TYPE_VARIABLES is only a hint (like tsc's
// couldContainTypeVariables, it is set for every function type and object literal type). Walks the members of object
// types; true when unsure (too deep, too big, a return type still being inferred)
static bool mentions_type_variables(Checker* me, Ty const* ty, MentionWalk* walk, u32 depth) {
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return false;
    if (ty->kind == TY_TYPE_PARAM) {
        for (u32 i = 0; i < walk->bound_len; i++) {
            if (walk->bound[i] == ty) return false;
        }
        return !walk->only || walk->only == ty;
    }
    if (ty->flags & TYF_DEFERRED) return true;
    if (depth > MENTION_MAX_DEPTH || walk->visited_len >= MENTION_MAX_VISITED) return true;
    if (!mention_visit(walk, ty->id + 1)) return false;
    switch ((TyKind)ty->kind) {
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (mentions_type_variables(me, ty->composite.types.data[i], walk, depth + 1)) return true;
            }
            return false;
        case TY_REFERENCE: {
            TyList args = ty_reference_args(me, ty);
            for (u32 i = 0; i < args.len; i++) {
                if (mentions_type_variables(me, args.data[i], walk, depth + 1)) return true;
            }
            return ty->reference.this_arg && mentions_type_variables(me, ty->reference.this_arg, walk, depth + 1);
        }
        case TY_ANONYMOUS: {
            Members const* members = ty_members(me, ty);
            if (members->mapped) return true;
            for (u32 i = 0; i < members->prop_count; i++) {
                if (mentions_type_variables(me, prop_type(me, members->props[i]), walk, depth + 1)) return true;
            }
            for (u32 i = 0; i < members->index_count; i++) {
                if (mentions_type_variables(me, members->index[i].key, walk, depth + 1)) return true;
                if (mentions_type_variables(me, members->index[i].value, walk, depth + 1)) return true;
            }
            for (u32 kind = 0; kind < 2; kind++) {
                Signature* const* list = kind ? members->construct : members->call;
                u32 count = kind ? members->construct_count : members->call_count;
                for (u32 i = 0; i < count; i++) {
                    Signature const* sig = list[i];
                    signature_resolve(me, sig);
                    if (ty_signature_return_in_flight(me, sig)) return true;
                    u32 saved = walk->bound_len;
                    if (walk->bound_len + sig->type_params.len > c_arraylen(walk->bound)) return true;
                    for (u32 j = 0; j < sig->type_params.len; j++) walk->bound[walk->bound_len++] = sig->type_params.data[j];
                    bool found = sig->this_type && mentions_type_variables(me, sig->this_type, walk, depth + 1);
                    for (u32 j = 0; !found && j < sig->param_count; j++) {
                        found = mentions_type_variables(me, sig->params[j].type, walk, depth + 1);
                    }
                    for (u32 j = 0; !found && j < sig->type_params.len; j++) {
                        Ty const* constraint = ty_param_constraint(me, sig->type_params.data[j]);
                        found = constraint && mentions_type_variables(me, constraint, walk, depth + 1);
                    }
                    found = found || mentions_type_variables(me, signature_return_type(me, sig), walk, depth + 1);
                    walk->bound_len = saved;
                    if (found) return true;
                }
            }
            return false;
        }
        default:
            // Template literals, string mappings, and whatever else: trust the flag
            return true;
    }
}

// tsc's isDistributionDependent: a distributive conditional type whose branches (may) mention the check type
// parameter, so they depend on which member of a union it's instantiated with
bool ty_conditional_distribution_dependent(Checker* me, Ty const* conditional) {
    Ty const* root = conditional->conditional.root ? conditional->conditional.root : conditional;
    if (!root->conditional.distributive || root->conditional.check->kind != TY_TYPE_PARAM) return false;
    MentionWalk walk = { .only = root->conditional.check };
    bool ret = mentions_type_variables(me, ty_conditional_branch(me, root, true), &walk, 0) ||
        mentions_type_variables(me, ty_conditional_branch(me, root, false), &walk, 0);
    free(walk.visited);
    return ret;
}

static Ty const* branch_type(Checker* me, TyCondBranch const* branch) {
    TyCondBranch copy = *branch;
    return ty_cond_branch_type(me, &copy);
}

Ty const* ty_conditional_resolve(
    Checker* me, Ty const* check_type, Ty const* extends, TyList infers, TyCondBranch const* if_true,
    TyCondBranch const* if_false
) {
    if (ty_is_generic_index(check_type)) return nullptr;
    if ((check_type->flags | extends->flags) & TYF_ERROR) return ty_intrinsic(me, TY_ERROR);

    TyMapper const* mapper = nullptr;
    // Only its own `infer X`: one of an outer conditional type is a type variable like any other here, which keeps
    // this one deferred until the outer one's true branch is instantiated with what it inferred
    if (infers.len) {
        Inference inf;
        inference_init(&inf, infers);
        infer_types(me, &inf, check_type, extends, INFER_PRIORITY_NORMAL);
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
        inference_drop(&inf);
    }
    Ty const* inferred_extends = ty_instantiate(me, extends, mapper);
    if (ty_is_generic_index(inferred_extends)) return nullptr;

    if (check_type->flags & TYF_ANY) {
        Ty const* parts[2] = { ty_instantiate(me, branch_type(me, if_true), mapper), branch_type(me, if_false) };
        return ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    bool related = (inferred_extends->flags & (TYF_ANY | TYF_UNKNOWN)) ||
        ty_is_related(me, check_type, inferred_extends, TY_RELATION_ASSIGNABLE);
    if (related) return ty_instantiate(me, branch_type(me, if_true), mapper);
    // HACK: tsc picks the false branch only if the check fails even with every type variable replaced by `any`
    // (permissive instantiation), and defers otherwise. There's no such instantiation here yet: with a type variable
    // nested in either side, a failed check defers
    MentionWalk walk = {0};
    bool mentions = mentions_type_variables(me, check_type, &walk, 0) || mentions_type_variables(me, inferred_extends, &walk, 0);
    free(walk.visited);
    return mentions ? nullptr : branch_type(me, if_false);
}
