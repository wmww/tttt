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
    TyBuf* collect; // Non-null: every free type parameter goes here and the walk goes on (true only when unsure)
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
        if (walk->collect) {
            for (u32 i = 0; i < walk->collect->len; i++) {
                if (walk->collect->data[i] == ty) return false;
            }
            ty_buf_push(walk->collect, ty);
            return false;
        }
        return !walk->only || walk->only == ty;
    }
    if (walk->collect) {
        // What a type is known to mention without looking inside it (which would lower lazy parts: a conditional
        // type's branches can be the alias being declared)
        if (ty->kind == TY_ANONYMOUS) {
            // A function's type mentions no type parameter from outside when none encloses it (not asking for its
            // return type, which may not be inferred yet: an object literal method's `this` can be what fixes the call
            // inferring it)
            if (ty->anonymous.function && ty->anonymous.function->no_enclosing_type_params) return false;
            Ty const* target = ty->anonymous.target ? ty->anonymous.target : ty;
            TyMapper const* mapper = ty->anonymous.target ? ty->anonymous.mapper : nullptr;
            if (target->kind == TY_ANONYMOUS && target->anonymous.node && !target->anonymous.free_params_unknown &&
                !ty->anonymous.applies_type_args
            ) {
                TyList params = target->anonymous.free_params;
                for (u32 i = 0; i < params.len; i++) {
                    Ty const* param = params.data[i];
                    for (u32 j = 0; mapper && j < mapper->len; j++) {
                        if (mapper->sources[j] == param) {
                            param = mapper->targets[j];
                            break;
                        }
                    }
                    if (mentions_type_variables(me, param, walk, depth + 1)) return true;
                }
                return false;
            }
        } else if (ty->kind == TY_CONDITIONAL && !ty->conditional.mapper) {
            TyConditionalOuter const* outer = ty_conditional_outer(me, ty->conditional.root ? ty->conditional.root : ty);
            if (!outer || outer->unknown) return true;
            for (u32 i = 0; i < outer->params.len; i++) mentions_type_variables(me, outer->params.data[i], walk, depth + 1);
            return false;
        }
    }
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
            if (members->mapped) {
                // Its own type parameter is bound in it
                MappedInfo const* mapped = members->mapped;
                if (walk->bound_len >= c_arraylen(walk->bound)) return true;
                u32 saved = walk->bound_len;
                walk->bound[walk->bound_len++] = mapped->type_param;
                bool found = mentions_type_variables(me, mapped->constraint, walk, depth + 1) ||
                    (mapped->name_type && mentions_type_variables(me, mapped->name_type, walk, depth + 1)) ||
                    mentions_type_variables(me, ty_mapped_template(me, mapped), walk, depth + 1) ||
                    (mapped->modifiers_type && mentions_type_variables(me, mapped->modifiers_type, walk, depth + 1));
                walk->bound_len = saved;
                return found;
            }
            for (u32 i = 0; i < members->prop_count; i++) {
                // (An object literal getter whose return type is being inferred further up: unsure, asking would infer
                // it again from below the barrier, where tsc's permissive instantiation doesn't look at properties)
                Prop const* prop = members->props[i];
                Prop const* root = prop;
                while (root->type_state != LAZY_STATE_DONE && root->target && !root->mapped) root = root->target;
                if (root->type_state != LAZY_STATE_DONE && (root->flags & PROP_GETTER) && !root->target &&
                    !root->decl->function->ret_explicit && ty_function_return_in_flight(me, root->decl->function)
                ) {
                    return true;
                }
                if (mentions_type_variables(me, prop_type(me, prop), walk, depth + 1)) return true;
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
        // Deferred types mention what their parts do (a signature's own `IE[T]` doesn't make it generic)
        case TY_KEYOF:
            return mentions_type_variables(me, ty->keyof.operand, walk, depth + 1);
        case TY_INDEXED:
            return mentions_type_variables(me, ty->indexed.object, walk, depth + 1) ||
                mentions_type_variables(me, ty->indexed.index, walk, depth + 1);
        case TY_TEMPLATE_LITERAL:
            for (u32 i = 0; i < ty->template_literal.types.len; i++) {
                if (mentions_type_variables(me, ty->template_literal.types.data[i], walk, depth + 1)) return true;
            }
            return false;
        case TY_STRING_MAPPING:
            return mentions_type_variables(me, ty->string_mapping.type, walk, depth + 1);
        case TY_SUBSTITUTION:
            // (`NoInfer<A>` mentions A)
            return mentions_type_variables(me, ty->substitution.base, walk, depth + 1) ||
                mentions_type_variables(me, ty->substitution.constraint, walk, depth + 1);
        case TY_CONDITIONAL: {
            TyMapper const* mapper = ty->conditional.mapper;
            if (ty->conditional.root && mapper) {
                // An instantiation mentions what the root mentions besides what the mapper replaces, and what that
                // is replaced with (walking its branches instead expands `DP<T[K]>` recursions level by level)
                for (u32 i = 0; i < mapper->len; i++) {
                    if (mentions_type_variables(me, mapper->targets[i], walk, depth + 1)) return true;
                }
                u32 saved = walk->bound_len;
                if (walk->bound_len + mapper->len > c_arraylen(walk->bound)) return true;
                for (u32 i = 0; i < mapper->len; i++) walk->bound[walk->bound_len++] = mapper->sources[i];
                bool found = mentions_type_variables(me, ty->conditional.root, walk, depth + 1);
                walk->bound_len = saved;
                return found;
            }
            // Its own `infer` type parameters are bound in it
            Ty const* root = ty->conditional.root ? ty->conditional.root : ty;
            TyList infers = ty_conditional_own_infers(me, root);
            u32 saved = walk->bound_len;
            if (walk->bound_len + infers.len > c_arraylen(walk->bound)) return true;
            for (u32 j = 0; j < infers.len; j++) walk->bound[walk->bound_len++] = infers.data[j];
            bool found = mentions_type_variables(me, ty->conditional.check, walk, depth + 1) ||
                mentions_type_variables(me, ty->conditional.extends, walk, depth + 1) ||
                mentions_type_variables(me, ty_conditional_branch(me, ty, true), walk, depth + 1) ||
                mentions_type_variables(me, ty_conditional_branch(me, ty, false), walk, depth + 1);
            walk->bound_len = saved;
            return found;
        }
        default:
            // Whatever else: trust the flag
            return true;
    }
}

// tsc's isDistributionDependent: a distributive conditional type whose branches (may) mention the check type
// parameter, so they depend on which member of a union it's instantiated with
bool ty_conditional_distribution_dependent(Checker* me, Ty const* conditional) {
    Ty const* root = conditional->conditional.root ? conditional->conditional.root : conditional;
    if (!root->conditional.distributive || root->conditional.check->kind != TY_TYPE_PARAM) return false;
    // (Written in a Block its check type parameter is declared outside of: possibly referenced, syntactically)
    TyConditionalOuter const* outer = ty_conditional_outer(me, root);
    if (outer && outer->check_outside_block) return true;
    MentionWalk walk = { .only = root->conditional.check };
    bool ret = mentions_type_variables(me, ty_conditional_branch(me, root, true), &walk, 0) ||
        mentions_type_variables(me, ty_conditional_branch(me, root, false), &walk, 0);
    free(walk.visited);
    return ret;
}

// Whether one of `params` occurs free in the type (true when unsure): what tsc finds out by instantiating a type with
// a mapper that reports the types it's asked to map (reportUnreliableMapper)
bool ty_mentions_any(Checker* me, Ty const* ty, Ty const* const* params, u32 count) {
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return false;
    TyBuf found = {0};
    MentionWalk walk = { .collect = &found };
    bool ret = mentions_type_variables(me, ty, &walk, 0);
    free(walk.visited);
    for (u32 i = 0; !ret && i < found.len; i++) {
        for (u32 j = 0; !ret && j < count; j++) ret = found.data[i] == params[j];
    }
    ty_buf_drop(&found);
    return ret;
}

static Ty const* branch_type(Checker* me, TyCondBranch const* branch) {
    TyCondBranch copy = *branch;
    return ty_cond_branch_type(me, &copy);
}

// The branch instantiated with the `infer` results too: in one go with what the branch has been instantiated with so
// far, like tsc's combinedMapper (a substitution type in it resolves differently in two steps)
static Ty const* branch_type_with(Checker* me, TyCondBranch const* branch, TyMapper const* mapper) {
    if (mapper && (branch->node || branch->source) && branch->state != LAZY_STATE_DONE) {
        TyCondBranch copy = *branch;
        copy.mapper = ty_compose_mappers(me, branch->mapper, mapper);
        return ty_cond_branch_type(me, &copy);
    }
    return ty_instantiate_tail(me, branch_type(me, branch), mapper);
}

// `[T] extends [X]`: tuples of the same arity (no optional or rest elements) where some element of the check type is
// generic are deferred like a generic check type (tsc's isDeferredType with checkTuples)
static bool is_deferred_tuple_check(Checker* me, Ty const* check_type, Ty const* extends) {
    TupleShape const* check_shape = ty_tuple_shape(check_type);
    TupleShape const* extends_shape = ty_tuple_shape(extends);
    if (!check_shape || !extends_shape || check_shape->arity != extends_shape->arity) return false;
    if (check_type->kind != TY_REFERENCE) return false; // (the empty tuple is its own target)
    for (u32 i = 0; i < check_shape->arity; i++) {
        if (check_shape->flags[i] || extends_shape->flags[i]) return false;
    }
    TyList elements = ty_reference_args(me, check_type);
    for (u32 i = 0; i < check_shape->arity && i < elements.len; i++) {
        if (ty_is_generic_index(elements.data[i]) || ty_is_generic_object(me, elements.data[i])) return true;
    }
    return false;
}

// A mapped type whose keys aren't known yet, or an intersection with one (the rest of tsc's isGenericObjectType; type
// variables are ty_is_generic_index()'s)
static bool is_generic_mapped(Checker* me, Ty const* ty, u32 depth) {
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) || depth > 8) return false;
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_generic_mapped(me, ty->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    return ty->kind == TY_ANONYMOUS && ty_members(me, ty)->mapped != nullptr;
}

// A type parameter without a constraint standing in for `param` (tsc's getRestrictiveTypeParameter)
static Ty const* restrictive_type_param(Checker* me, Ty const* param) {
    u64 key[2] = { KEY_RESTRICTIVE_PARAM, param->id };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) {
        Ty const* ret = ty_synthetic_type_param(me, atom_sv(param->param.name));
        ret->param.state->constraint_state = LAZY_STATE_DONE;
        *slot = ret;
    }
    return *slot;
}

// Whether the check type is assignable to the extends type whatever the type variables in them turn out to be: with
// every type parameter replaced by one without a constraint (tsc's getRestrictiveInstantiation in getConditionalType).
// `Extract<CL, Pick<T, "kind">>` with `T extends CL` isn't its true branch for any member of CL. *mentions: whether
// either side has type variables (or might: when the walk gives up, the constraints stay)
static bool definitely_related(Checker* me, Ty const* check_type, Ty const* extends, bool* mentions) {
    if (!((check_type->flags | extends->flags) & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        return ty_is_related(me, check_type, extends, TY_RELATION_ASSIGNABLE);
    }
    TyBuf params = {0};
    MentionWalk walk = { .collect = &params };
    bool unsure = mentions_type_variables(me, check_type, &walk, 0) || mentions_type_variables(me, extends, &walk, 0);
    free(walk.visited);
    *mentions = unsure || params.len;
    bool ret;
    if (unsure || !params.len) {
        ret = ty_is_related(me, check_type, extends, TY_RELATION_ASSIGNABLE);
    } else {
        Ty const** restrictive = malloc(sizeof(Ty const*) * params.len);
        panic_if(!restrictive, "out of memory");
        for (u32 i = 0; i < params.len; i++) restrictive[i] = restrictive_type_param(me, params.data[i]);
        TyMapper const* mapper = ty_mapper_new(
            me, (TyList){ .data = params.data, .len = params.len }, (TyList){ .data = restrictive, .len = params.len }
        );
        free(restrictive);
        Ty const* instantiated_check = ty_instantiate(me, check_type, mapper);
        ret = ty_is_related(me, instantiated_check, ty_instantiate(me, extends, mapper), TY_RELATION_ASSIGNABLE);
    }
    ty_buf_drop(&params);
    return ret;
}

// An object type against primitives only is unrelated whatever type variables it mentions inside (its permissive
// instantiation is an object too), without walking its members: a method whose parameter is the mapped type being
// resolved (`[K in keyof T]: T[K] extends number ? K : never` over a class with `inc(key: Keys<this>)`) isn't resolved
// again (tsc instantiates signatures lazily)
static bool object_against_primitives(Ty const* check_type, Ty const* extends) {
    if (!(check_type->flags & TYF_OBJECT)) return false;
    if (check_type->kind != TY_ANONYMOUS && check_type->kind != TY_REFERENCE && check_type->kind != TY_DECLARED) return false;
    u32 const primitive = TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL |
        TYF_BOOLEAN | TYF_BOOLEAN_LITERAL | TYF_ES_SYMBOL_LIKE | TYF_NULLABLE | TYF_VOID | TYF_NEVER;
    TyList parts = extends->kind == TY_UNION ? extends->composite.types : (TyList){ .data = &extends, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        if (parts.data[i]->kind == TY_UNION || !(parts.data[i]->flags & primitive) ||
            (parts.data[i]->flags & (TYF_ANY | TYF_UNKNOWN | TYF_TYPE_PARAM | TYF_DEFERRED))
        ) {
            return false;
        }
    }
    return true;
}

// Whether the check type isn't assignable to the extends type whatever the type variables in them turn out to be: not
// even with each of them the wildcard, which relates to everything (tsc's getPermissiveInstantiation). When the walk
// can't tell which type variables there are, it doesn't know
static bool definitely_unrelated(Checker* me, Ty const* check_type, Ty const* extends) {
    if (!((check_type->flags | extends->flags) & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        return !ty_is_related(me, check_type, extends, TY_RELATION_ASSIGNABLE);
    }
    if (object_against_primitives(check_type, extends)) return true;
    TyBuf params = {0};
    MentionWalk walk = { .collect = &params };
    bool unsure = mentions_type_variables(me, check_type, &walk, 0) || mentions_type_variables(me, extends, &walk, 0);
    free(walk.visited);
    bool ret = false;
    if (!unsure) {
        Ty const** wildcards = malloc(sizeof(Ty const*) * (params.len ? params.len : 1));
        panic_if(!wildcards, "out of memory");
        for (u32 i = 0; i < params.len; i++) wildcards[i] = ty_intrinsic(me, TY_WILDCARD);
        TyMapper const* mapper = ty_mapper_new(
            me, (TyList){ .data = params.data, .len = params.len }, (TyList){ .data = wildcards, .len = params.len }
        );
        free(wildcards);
        Ty const* source = params.len ? ty_instantiate(me, check_type, mapper) : check_type;
        Ty const* target = params.len ? ty_instantiate(me, extends, mapper) : extends;
        ret = source != ty_intrinsic(me, TY_WILDCARD) && target != ty_intrinsic(me, TY_WILDCARD) &&
            !ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE);
    }
    ty_buf_drop(&params);
    return ret;
}

// (Memoized: relations ask again and again, and each instantiates both sides with a fresh mapper. tsc keeps the
// permissive and restrictive instantiations on the type)
static bool definitely_memo(Checker* me, Ty const* source, Ty const* target, bool assignable) {
    u64 key[4] = { KEY_DEFINITELY_RELATED, source->id, target->id, assignable };
    void const* known = intern_get(&my links, key, 4);
    if (known) return (uintptr_t)known == 2;
    bool mentions = false;
    bool ret = assignable ? definitely_related(me, source, target, &mentions) : definitely_unrelated(me, source, target);
    *intern_slot(me, &my links, key, 4) = (void const*)(uintptr_t)(1 + ret);
    return ret;
}

bool ty_definitely_unassignable(Checker* me, Ty const* source, Ty const* target) {
    return definitely_memo(me, source, target, false);
}

bool ty_definitely_assignable(Checker* me, Ty const* source, Ty const* target) {
    return definitely_memo(me, source, target, true);
}

Ty const* ty_conditional_resolve(
    Checker* me, Ty const* check_type, Ty const* extends, TyList infers, TyCondBranch const* if_true,
    TyCondBranch const* if_false, TyMapper const* outer, Ty const* constraint_root
) {
    bool for_constraint = constraint_root != nullptr;
    // (tsc compares with errorType itself, before deferring: an unresolved reference's error type (ty_unresolved, tsc's
    // errorTypes) is any to it, `Parameters<Unresolved>` is unknown[]; a union with one is errorType)
    Ty const* error = ty_intrinsic(me, TY_ERROR);
    if (check_type == error || extends == error) return error;
    // (tsc's isDeferredType of the check type: `Required<T> extends {}` waits for T)
    if (ty_is_generic_index(check_type) || is_generic_mapped(me, check_type, 0) || is_deferred_tuple_check(me, check_type, extends)) {
        return nullptr;
    }

    TyMapper const* mapper = nullptr;
    // Only its own `infer X`: one of an outer conditional type is a type variable like any other here, which keeps
    // this one deferred until the outer one's true branch is instantiated with what it inferred
    if (infers.len) {
        Inference inf;
        inference_init(&inf, infers);
        inf.always_strict = true;
        infer_types(me, &inf, check_type, extends, INFER_PRIORITY_NORMAL);
        Ty const** params = arena_new_n(&my arena, Ty const*, inf.len);
        Ty const** results = arena_new_n(&my arena, Ty const*, inf.len);
        for (u32 i = 0; i < inf.len; i++) {
            params[i] = inf.params[i].param;
            results[i] = inference_conditional_result(me, &inf, i);
            if (!results[i]) results[i] = ty_intrinsic(me, TY_UNKNOWN_);
        }
        // Nothing inferred, or what doesn't satisfy `infer X extends C` (or an implied constraint: `string` in a
        // template), is the constraint (tsc's getInferredType), as instantiated with what instantiated the conditional
        // type and what the `infer`s inferred (its nonFixingMapper): `infer K extends keyof T & string` with T = {} is
        // never, so nothing matches
        TyMapper const* constraint_mapper = nullptr;
        Ty const** checked = arena_new_n(&my arena, Ty const*, inf.len);
        for (u32 i = 0; i < inf.len; i++) {
            checked[i] = results[i];
            Ty const* constraint = ty_param_constraint(me, params[i]);
            if (!constraint || constraint == params[i]) continue;
            if (constraint->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
                if (!constraint_mapper) {
                    constraint_mapper = ty_mapper_new(me, (TyList){ .data = params, .len = inf.len }, (TyList){ .data = results, .len = inf.len });
                    for (u32 j = 0; outer && j < outer->len; j++) {
                        // (The first mapping of a source is the one that counts)
                        bool own = false;
                        for (u32 k = 0; k < constraint_mapper->len; k++) own |= outer->sources[j] == constraint_mapper->sources[k];
                        if (!own) constraint_mapper = ty_mapper_with(me, constraint_mapper, outer->sources[j], outer->targets[j]);
                    }
                }
                constraint = ty_instantiate(me, constraint, constraint_mapper);
            }
            // HACK: tsc also falls back to a constraint that stays generic (`T3<X> = "a.b" extends `${infer S extends
            // keyof X & string}.${string}` ? S : 0` is 0 for T3<{}> in tsc, "a" here: it resolves while X is generic).
            // Doing it made valibot's ValidPath<TInput, TPath> iterate to a type mentioning LazyPath's own TValue
            // (probe test/probes/plan19/zod/ark_infer_constraint.ts has both)
            if (constraint->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) continue;
            if (!ty_is_related(me, results[i], constraint, TY_RELATION_ASSIGNABLE)) checked[i] = constraint;
        }
        results = checked;
        mapper = ty_mapper_new(me, (TyList){ .data = params, .len = inf.len }, (TyList){ .data = results, .len = inf.len });
        inference_drop(&inf);
    }
    Ty const* inferred_extends = ty_instantiate(me, extends, mapper);
    // (tsc's isDeferredType of the extends type: `{} extends Mapped<T>` waits for T too)
    if (ty_is_generic_index(inferred_extends) || is_generic_mapped(me, inferred_extends, 0)) return nullptr;

    // (An `any` or `unknown` extends type takes anything, `any` too: only then is `any` not both branches)
    if (inferred_extends->flags & (TYF_ANY | TYF_UNKNOWN)) return branch_type_with(me, if_true, mapper);
    if (check_type->flags & TYF_ANY) {
        Ty const* parts[2] = { branch_type_with(me, if_true, mapper), branch_type(me, if_false) };
        return ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    // Definitely false: not even with every type variable in either side the wildcard (tsc's permissive
    // instantiation); definitely true: with each one unconstrained (restrictive); otherwise deferred
    // (For a constraint, false still has the true branch too: what the constraint stands for may match, tsc's
    // forConstraint)
    bool mentions = false;
    bool is_false = definitely_unrelated(me, check_type, inferred_extends);
    if (!is_false && definitely_related(me, check_type, inferred_extends, &mentions)) {
        return branch_type_with(me, if_true, mapper);
    }
    if (!is_false && mentions) return nullptr;
    // (Only if the extends type, or a member of it, is assignable to the check type: tsc's forConstraint check)
    bool narrows_to_extends = false;
    if (for_constraint && !(inferred_extends->flags & TYF_NEVER)) {
        TyList members = inferred_extends->kind == TY_UNION ? inferred_extends->composite.types : (TyList){ .data = &inferred_extends, .len = 1 };
        for (u32 i = 0; i < members.len && !narrows_to_extends; i++) {
            narrows_to_extends = ty_is_related(me, members.data[i], check_type, TY_RELATION_ASSIGNABLE);
        }
    }
    // (A conditional type written as the false branch goes on for the constraint too, tsc's tail loop in
    // getConditionalType: `T extends null ? null : T extends string ? "" : ...`)
    Ty const* falsy = nullptr;
    if (for_constraint && if_false->node && if_false->node->tag == TYPE_TERNARY && if_false->state != LAZY_STATE_DONE) {
        Ty const* nested = ty_from_node(me, if_false->node, if_false->ctx);
        if (nested->kind == TY_CONDITIONAL &&
            (!nested->conditional.distributive || nested->conditional.check == constraint_root->conditional.check)
        ) {
            Ty const* saved = my for_constraint_root;
            my for_constraint_root = nested;
            falsy = branch_type(me, if_false);
            my for_constraint_root = saved;
        }
    }
    if (!falsy) falsy = branch_type(me, if_false);
    if (narrows_to_extends) {
        Ty const* parts[2] = { branch_type_with(me, if_true, mapper), falsy };
        return ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    return falsy;
}
