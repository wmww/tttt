#include "ty/ty_internal.h"

// Resolution of conditional types: `Check extends Extends ? True : False`, with inference for the `infer X` type
// parameters in the extends type. Distribution over unions happens at instantiation (instantiate.c), since that's
// where the check type parameter gets its union.

typedef struct MentionWalk {
    TyBuf bound; // Type parameters of the signatures, mapped types and conditional types on the way: not free
    // Types already walked (a type seen again is either being walked or mentions nothing: either way nothing new).
    // Open addressing on ids, 0 is empty
    u32* visited;
    u32 visited_cap;
    u32 visited_len;
    Ty const* only; // Non-null: the one type parameter that counts
    TyBuf* collect; // Non-null: every free type parameter goes here and the walk goes on (true only when unsure)
    Ty const* self; // The function expression type ty_function_type_mentions() is collecting for
} MentionWalk;

#define MENTION_MAX_VISITED 8192
#define MENTION_MAX_DEPTH 48

static void mention_walk_drop(MentionWalk* walk) {
    free(walk->visited);
    ty_buf_drop(&walk->bound);
}

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

// The type ty_function_type() makes for a function expression, which ty_function_type_mentions() answers for
static bool function_expression_type(Ty const* target) {
    return target->kind == TY_ANONYMOUS && target->anonymous.function && !target->anonymous.target && !target->anonymous.node &&
        target->anonymous.free_params_unknown && !target->anonymous.overload_count && !target->anonymous.merged_value &&
        !target->anonymous.is_reverse && !target->anonymous.applies_type_args && target->anonymous.function->is_expression;
}

// Whether a type variable really occurs in the type: TYF_COULD_CONTAIN_TYPE_VARIABLES is only a hint (like tsc's
// couldContainTypeVariables, it is set for every function type and object literal type). Walks the members of object
// types; true when unsure (too deep, too big, a return type still being inferred)
// tsc's couldContainTypeVariables where tttt's flag is narrower: tsc says yes for every mapped and reverse mapped type
// (and so for unions, intersections and references with one in them), and instantiates them, where tttt knows a mapped
// type over known types mentions none. What a fixing mapper looks up goes through those (see the reverse mapped case of
// mentions_type_variables())
static bool tsc_could_contain_type_variables(Ty const* ty, u32 depth) {
    if (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) return true;
    if (depth > 4) return false;
    switch ((TyKind)ty->kind) {
        case TY_ANONYMOUS: return ty->anonymous.mapped || ty->anonymous.is_reverse;
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (tsc_could_contain_type_variables(ty->composite.types.data[i], depth + 1)) return true;
            }
            return false;
        case TY_REFERENCE:
            if (ty->reference.deferred) return false;
            for (u32 i = 0; i < ty->reference.args.len; i++) {
                if (tsc_could_contain_type_variables(ty->reference.args.data[i], depth + 1)) return true;
            }
            return false;
        default: return false;
    }
}

static bool mentions_type_variables(Checker* me, Ty const* ty, MentionWalk* walk, u32 depth) {
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && !(walk->collect && tsc_could_contain_type_variables(ty, 0))) return false;
    if (ty->kind == TY_TYPE_PARAM) {
        ty = ty_non_distributed(ty);
        for (u32 i = 0; i < walk->bound.len; i++) {
            if (walk->bound.data[i] == ty) return false;
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
            if (ty->anonymous.function && ty_function_closed(me, ty->anonymous.function)) return false;
            Ty const* target = ty->anonymous.target ? ty->anonymous.target : ty;
            TyMapper const* mapper = ty->anonymous.target ? ty->anonymous.mapper : nullptr;
            // A function expression's type (or an instance of one) by what its signature mentions, without reading the
            // members of instances (which instantiates them, level by level for a function returning a call of its own
            // container: `deeper: <U>(c: U) => testRecFun<T & U>(...)`)
            if (walk->self && (target != walk->self || depth > 0)) {
                if (target == walk->self) {
                    // (Itself, or an instance of itself: what it maps its own mentions to, and those, which the walk
                    // collects anyway)
                    for (u32 j = 0; mapper && j < mapper->len; j++) {
                        if (mentions_type_variables(me, mapper->targets[j], walk, depth + 1)) return true;
                    }
                    return false;
                }
                if (function_expression_type(target) && !ty->anonymous.applies_type_args) {
                    TyList params;
                    if (!ty_function_type_mentions(me, target, &params)) return true;
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
            }
            // (So is a declared function's whose signature is all annotations: a method of a generic class read
            // through an instance, `table(): TableProp<TDexieSubClass>` of `Entity<TodoDB>`, mentions what its type
            // arguments do, without resolving its signature, whose types may be the mapped type being resolved)
            TyList params = target->kind == TY_ANONYMOUS ? target->anonymous.free_params : (TyList){0};
            bool known = target->kind == TY_ANONYMOUS && !ty->anonymous.applies_type_args && (
                (target->anonymous.node && !target->anonymous.free_params_unknown) ||
                (target->anonymous.function && !target->anonymous.node && !target->anonymous.overload_count &&
                    !target->anonymous.is_reverse && !target->anonymous.merged_value && ty_function_free_params(me, target->anonymous.function, &params)));
            if (known) {
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
    ReverseMapped const* reverse = ty_reverse_of(ty);
    if (reverse && !ty->anonymous.target) {
        // A reverse mapped type mentions what its source and the mapped type it reverses do, not what its members
        // were inferred to be: tsc's instantiateReverseMappedType instantiates those three, so a fixing mapper fixes
        // the type parameter `{ [K in keyof T]: ... }` maps over (arktype's `type({ "child?": ["this", "=>", f] })`:
        // the second pass's contextual signature for f fixes `def`, tsc keeps the first pass's inference)
        MappedInfo const* mapped = reverse->mapped;
        u32 saved = walk->bound.len;
        ty_buf_push(&walk->bound, mapped->type_param);
        bool found = mentions_type_variables(me, reverse->operand, walk, depth + 1) ||
            mentions_type_variables(me, reverse->source, walk, depth + 1) ||
            mentions_type_variables(me, mapped->constraint, walk, depth + 1) ||
            (mapped->name_type && mentions_type_variables(me, mapped->name_type, walk, depth + 1)) ||
            mentions_type_variables(me, ty_mapped_template(me, mapped), walk, depth + 1);
        walk->bound.len = saved;
        return found;
    }
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
                u32 saved = walk->bound.len;
                ty_buf_push(&walk->bound, mapped->type_param);
                bool found = mentions_type_variables(me, mapped->constraint, walk, depth + 1) ||
                    (mapped->name_type && mentions_type_variables(me, mapped->name_type, walk, depth + 1)) ||
                    mentions_type_variables(me, ty_mapped_template(me, mapped), walk, depth + 1) ||
                    (mapped->modifiers_type && mentions_type_variables(me, mapped->modifiers_type, walk, depth + 1));
                walk->bound.len = saved;
                return found;
            }
            for (u32 i = 0; i < members->prop_count; i++) {
                // (An object literal getter whose return type is being inferred further up: unsure, asking would infer
                // it again from below the barrier, where tsc's permissive instantiation doesn't look at properties)
                Prop const* prop = members->props[i];
                Prop const* root = prop;
                while (!prop_type_peek(me, root) && root->target && !root->mapped) root = root->target;
                if (!prop_type_peek(me, root) && (root->flags & PROP_GETTER) && !root->target &&
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
                    SigResolved const* sig_rs = signature_resolve(me, sig);
                    if (ty_signature_return_in_flight(me, sig)) return true;
                    u32 saved = walk->bound.len;
                    for (u32 j = 0; j < sig_rs->type_params.len; j++) ty_buf_push(&walk->bound, sig_rs->type_params.data[j]);
                    bool found = sig_rs->this_type && mentions_type_variables(me, sig_rs->this_type, walk, depth + 1);
                    for (u32 j = 0; !found && j < sig_rs->param_count; j++) {
                        found = mentions_type_variables(me, sig_rs->params[j].type, walk, depth + 1);
                    }
                    for (u32 j = 0; !found && j < sig_rs->type_params.len; j++) {
                        Ty const* constraint = ty_param_constraint(me, sig_rs->type_params.data[j]);
                        found = constraint && mentions_type_variables(me, constraint, walk, depth + 1);
                    }
                    found = found || mentions_type_variables(me, signature_return_type(me, sig), walk, depth + 1);
                    // (A type predicate's type too: `(ast: AST): ast is Extract<AST, { _tag: T }>` mentions T)
                    TyPredicate const* predicate = found ? nullptr : signature_predicate(me, sig);
                    found = found || (predicate && predicate->type && mentions_type_variables(me, predicate->type, walk, depth + 1));
                    walk->bound.len = saved;
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
                u32 saved = walk->bound.len;
                for (u32 i = 0; i < mapper->len; i++) ty_buf_push(&walk->bound, mapper->sources[i]);
                bool found = mentions_type_variables(me, ty->conditional.root, walk, depth + 1);
                walk->bound.len = saved;
                return found;
            }
            // Its own `infer` type parameters are bound in it
            Ty const* root = ty->conditional.root ? ty->conditional.root : ty;
            TyList infers = ty_conditional_own_infers(me, root);
            u32 saved = walk->bound.len;
            for (u32 j = 0; j < infers.len; j++) ty_buf_push(&walk->bound, infers.data[j]);
            bool found = mentions_type_variables(me, ty->conditional.check, walk, depth + 1) ||
                mentions_type_variables(me, ty->conditional.extends, walk, depth + 1) ||
                mentions_type_variables(me, ty_conditional_branch(me, ty, true), walk, depth + 1) ||
                mentions_type_variables(me, ty_conditional_branch(me, ty, false), walk, depth + 1);
            walk->bound.len = saved;
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
    // (Cached on the root like tsc's ConditionalRoot.isDistributionDependent: the relation asks for every instance)
    u64 key[2] = { KEY_DISTRIBUTION_DEPENDENT, root->id };
    void const* known = intern_get(&my links, key, 2);
    if (known) return (uintptr_t)known == 2;
    MentionWalk walk = { .only = ty_non_distributed(root->conditional.check) };
    bool ret = mentions_type_variables(me, ty_conditional_branch(me, root, true), &walk, 0) ||
        mentions_type_variables(me, ty_conditional_branch(me, root, false), &walk, 0);
    mention_walk_drop(&walk);
    *intern_slot(me, &my links, key, 2) = (void const*)(uintptr_t)(1 + ret);
    return ret;
}

// Whether one of `params` occurs free in the type (true when unsure): what tsc finds out by instantiating a type with
// a mapper that reports the types it's asked to map (reportUnreliableMapper)
bool ty_mentions_any(Checker* me, Ty const* ty, Ty const* const* params, u32 count) {
    if (!tsc_could_contain_type_variables(ty, 0)) return false;
    TyBuf found = {0};
    MentionWalk walk = { .collect = &found };
    bool ret = mentions_type_variables(me, ty, &walk, 0);
    mention_walk_drop(&walk);
    for (u32 i = 0; !ret && i < found.len; i++) {
        for (u32 j = 0; !ret && j < count; j++) ret = found.data[i] == params[j];
    }
    ty_buf_drop(&found);
    return ret;
}

typedef struct FunctionMentions { TyList params; bool known; } FunctionMentions;

// The type parameters a function expression's or object literal's type mentions, collected under a CLAIM_MENTIONS
// claim and published once known
static bool compute_mentions(Checker* me, Ty const* ty, u64 const* key, TyList* out) {
    Claim claim;
    ty_claim_push(me, CLAIM_MENTIONS, &claim, ty, 0);
    TyBuf found = {0};
    MentionWalk walk = { .collect = &found, .self = ty };
    bool unsure = mentions_type_variables(me, ty, &walk, 0);
    mention_walk_drop(&walk);
    ty_claim_pop(me, CLAIM_MENTIONS, &claim);
    FunctionMentions* computed = arena_new(&my arena, FunctionMentions);
    computed->known = !unsure;
    if (!unsure) computed->params = ty_list_copy(me, found.data, found.len);
    ty_buf_drop(&found);
    *intern_slot(me, &my links, key, 2) = computed;
    *out = computed->params;
    return computed->known;
}

// An object literal's type (ty_object_literal()), which ty_function_type_mentions() answers for too
static bool object_literal_type(Ty const* target) {
    return target->kind == TY_ANONYMOUS && target->anonymous.object_literal && !target->anonymous.target &&
        target->anonymous.free_params_unknown && !target->anonymous.function && !target->anonymous.node &&
        !target->anonymous.is_reverse && !target->anonymous.applies_type_args && !target->anonymous.merged_value;
}

// What instantiating an object literal's type can change: the type parameters its properties' types mention. tsc keys
// these instantiations by every outer type parameter of the literal (getObjectTypeInstantiation); tttt's were keyed by
// the whole mapper, so the same literal of a generic function got a new instance for every mapper that reached it
// (feathers' typebox: `Type.Object({ $gt: def, ... })` in a generic arrow, 7x tsgo's types). Unknown while a property's
// type isn't (a getter or method still lazy), and when unsure
static bool object_literal_type_mentions(Checker* me, Ty const* ty, TyList* out) {
    u64 key[2] = { KEY_FUNCTION_TYPE_MENTIONS, (u64)(uintptr_t)ty };
    FunctionMentions* cached = (FunctionMentions*)intern_get(&my links, key, 2);
    if (cached) {
        *out = cached->params;
        return cached->known;
    }
    // (Found again while collecting: unknown. In flight is the checker's claim, the links only get the answer)
    if (ty_claim_find(me, CLAIM_MENTIONS, ty, 0)) {
        TAINT_EVENT(TAINT_IN_FLIGHT_OTHER);
        return false;
    }
    if (!ty->anonymous.state) return false;
    Members const* members = ty_members(me, ty);
    if (members->call_count || members->construct_count || members->mapped) return false;
    // (Not with a getter: one of a recursive schema, zod's `get content() { return z.array(z.tuple([Self, ...])) }`, can
    // have been resolved inside its own circularity, and an instance resolving it again later is what gets it right:
    // the identity instance made recursive-types.test.ts's TS2344 spurious)
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        // (A property type not known yet: unknown, a peek that selects between results, so an event (rule 5))
        if (TAINT_IF(TAINT_IN_FLIGHT_OTHER, !prop_type_peek(me, prop)) || prop->target || (prop->flags & PROP_GETTER)) return false;
    }
    return compute_mentions(me, ty, key, out);
}

// What instantiating a function expression's type can change: the type parameters its one signature mentions (its
// parameters, `this`, the inferred return type). tsc keys these instantiations by the function's outer type parameters
// (getObjectTypeInstantiation); tttt's contextually typed parameters may also carry a call's type parameters (plan 51),
// so it goes by what the resolved signature really mentions instead. Asked before the signature is resolved or the
// return type inferred, unknown (and not remembered: it is known later); found again while collecting (two functions
// whose types mention each other's), unknown
bool ty_function_type_mentions(Checker* me, Ty const* ty, TyList* out) {
    *out = (TyList){0};
    if (object_literal_type(ty)) return object_literal_type_mentions(me, ty, out);
    if (!function_expression_type(ty)) return false;
    u64 key[2] = { KEY_FUNCTION_TYPE_MENTIONS, (u64)(uintptr_t)ty };
    FunctionMentions* cached = (FunctionMentions*)intern_get(&my links, key, 2);
    if (cached) {
        *out = cached->params;
        return cached->known;
    }
    // (Found again while collecting: unknown. In flight is the checker's claim, the links only get the answer)
    if (ty_claim_find(me, CLAIM_MENTIONS, ty, 0)) {
        TAINT_EVENT(TAINT_IN_FLIGHT_OTHER);
        return false;
    }
    // (Rule 5 of plan 86 4f: a peek that selects between results is a read of in-flight state. The members and the
    // signature's resolution are computed instead, which leaves the default path's output as it was)
    if (!ty->anonymous.state) return false;
    Members const* members = ty_members(me, ty);
    if (members->call_count != 1 || members->construct_count || members->prop_count || members->index_count || members->mapped) return false;
    Signature const* sig = members->call[0];
    signature_resolve(me, sig);
    if (sig_target(me, sig) || sig->function != ty->anonymous.function) return false;
    FunctionImpl const* function = sig->function;
    if (TAINT_IF(TAINT_PEEK, sig->ret_state != LAZY_STATE_DONE && !function->ret_explicit && function->has_body && !ty_function_return_known(me, function))) return false;
    return compute_mentions(me, ty, key, out);
}

// (Computed each time, not remembered in the branch: what it is in this resolution, the constraint root's context
// included. It took the branch's remembered type when there was one, and this resolution's otherwise: history, whose
// removal moved nothing on the suite and 12 corpora, plan 86 "P2 progress")
static Ty const* branch_type(Checker* me, TyCondBranch const* branch) {
    return ty_cond_branch_compute(me, branch, branch->mapper);
}

// The branch instantiated with the `infer` results too: in one go with what the branch has been instantiated with so
// far, like tsc's combinedMapper (a substitution type in it resolves differently in two steps)
static Ty const* branch_type_with(Checker* me, TyCondBranch const* branch, TyMapper const* mapper) {
    if (mapper) return ty_cond_branch_compute(me, branch, ty_compose_mappers(me, branch->mapper, mapper));
    return ty_instantiate_tail(me, branch_type(me, branch), mapper);
}

// `[T] extends [X]`: tuples of the same arity (no optional or rest elements), where tsc's isDeferredType with
// checkTuples looks at the elements: a generic one in either side defers like a generic check type
bool ty_conditional_simple_tuples(Ty const* check_type, Ty const* extends) {
    TupleShape const* check_shape = ty_tuple_shape(check_type);
    TupleShape const* extends_shape = ty_tuple_shape(extends);
    if (!check_shape || !extends_shape || check_shape->arity != extends_shape->arity) return false;
    // (The empty tuple is its own target; a type parameter constrained to a tuple, `infer R extends [A, B]`, is no tuple)
    if (check_type->kind != TY_REFERENCE || extends->kind != TY_REFERENCE) return false;
    for (u32 i = 0; i < check_shape->arity; i++) {
        if (check_shape->flags[i] || extends_shape->flags[i]) return false;
    }
    return true;
}

static bool has_generic_element(Checker* me, Ty const* tuple) {
    if (tuple->kind != TY_REFERENCE) return false;
    TyList elements = ty_reference_args(me, tuple);
    u32 arity = ty_tuple_shape(tuple)->arity;
    for (u32 i = 0; i < arity && i < elements.len; i++) {
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
    return ty->kind == TY_ANONYMOUS && ty_generic_mapped(me, ty) != nullptr;
}

// A type parameter without a constraint standing in for `param` (tsc's getRestrictiveTypeParameter)
static Ty const* restrictive_type_param(Checker* me, Ty const* param) {
    u64 key[2] = { KEY_RESTRICTIVE_PARAM, param->id };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) {
        Ty const* ret = ty_synthetic_type_param(me, atom_sv(param->param.name));
        ty_param_constraint_init(ret->param.state, nullptr, false);
        intern_set(me, &my links, slot, ret);
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
    mention_walk_drop(&walk);
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

// A function or object type against a type that needs call or construct signatures it doesn't have is unrelated
// whatever type variables it mentions inside (instantiating doesn't add signatures), without resolving its signatures:
// `T[P] extends new () => any ? never : P` over a class whose static method takes `this: ModelStatic<M>`, an alias
// of the mapped type being resolved, doesn't resolve that parameter (tsc instantiates signatures lazily, and
// signaturesRelatedTo fails on the counts first)
static bool object_lacks_signatures(Checker* me, Ty const* check_type, Ty const* extends) {
    if (check_type->kind != TY_ANONYMOUS || extends->kind != TY_ANONYMOUS) return false;
    Members const* source = ty_members(me, check_type);
    Members const* target = ty_members(me, extends);
    if (source->mapped || target->mapped) return false;
    return (target->construct_count && !source->construct_count) || (target->call_count && !source->call_count);
}

// An object type with call signatures against `(...args: any[]) => any`, or one with call or construct signatures
// against the global `Function` (the "is it a function" idioms), is related whatever its signatures return or mention,
// without resolving them: tsc's relation compares the target's return type first and stops at any
// (compareSignaturesRelated), and finds Function's members on anything callable (getPropertyOfType). So
// `T[P] extends (...args: any[]) => any` over a class whose method infers its return type from a body that needs the
// mapped type being resolved isn't circular (dexie's NonInsertProps<T>), nor is `T[K] & {} extends Function` over a
// method whose type parameter is constrained by it (mikro-orm's CleanKeys<T, K>): TS2615 depending on which file a
// checker saw first. Not when the source declares a property Function has (it may not fit Function's)
static bool callable_definitely_related(Checker* me, Ty const* check_type, Ty const* extends) {
    if (!(check_type->flags & TYF_OBJECT)) return false;
    if (check_type->kind != TY_ANONYMOUS && check_type->kind != TY_REFERENCE && check_type->kind != TY_DECLARED &&
        check_type->kind != TY_INTERSECTION
    ) {
        return false;
    }
    bool any_function = false;
    if (extends->kind == TY_ANONYMOUS) {
        Members const* target = ty_members(me, extends);
        if (target->mapped || target->prop_count || target->index_count || target->construct_count || target->call_count != 1) {
            return false;
        }
        Signature const* sig = target->call[0];
        SigResolved const* sig_rs = signature_resolve(me, sig);
        if (sig_rs->type_params.len || sig_rs->this_type || !(signature_return_type(me, sig)->flags & TYF_ANY)) return false;
        if (!sig_rs->param_count || !sig_rs->params[sig_rs->param_count - 1].rest) return false;
        for (u32 i = 0; i < sig_rs->param_count; i++) {
            Ty const* type = sig_rs->params[i].type;
            if (sig_rs->params[i].rest && !(type->flags & TYF_ANY)) type = ty_is_array(me, type) ? ty_rest_element_type(me, type) : nullptr;
            if (!type || !(type->flags & TYF_ANY)) return false;
        }
        any_function = true;
    } else if (extends != ty_global_function_type(me)) {
        return false;
    }
    Members const* source = ty_members(me, check_type);
    if (source->mapped || !(source->call_count || (!any_function && source->construct_count))) return false;
    // Unless it has a property of its own that Function has too (lodash's `toString(value: any): string`): that one,
    // not Function's, is what tsc's relation compares, so the real relation decides
    if (!any_function) {
        Ty const* function_type = ty_global_function_type(me);
        for (u32 i = 0; i < source->prop_count; i++) {
            if (ty_get_prop(me, function_type, source->props[i]->name)) return false;
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
    if (object_against_primitives(check_type, extends) || object_lacks_signatures(me, check_type, extends)) return true;
    TyBuf params = {0};
    MentionWalk walk = { .collect = &params };
    bool unsure = mentions_type_variables(me, check_type, &walk, 0) || mentions_type_variables(me, extends, &walk, 0);
    mention_walk_drop(&walk);
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
    InternMiss miss;
    void const* known = intern_lookup(&my links, key, 4, &miss);
    if (known) return (uintptr_t)known == 2;
    bool mentions = false;
    bool ret = assignable ? definitely_related(me, source, target, &mentions) : definitely_unrelated(me, source, target);
    *intern_insert(me, &my links, &miss, key, 4) = (void const*)(uintptr_t)(1 + ret);
    return ret;
}

bool ty_definitely_unassignable(Checker* me, Ty const* source, Ty const* target) {
    return definitely_memo(me, source, target, false);
}

bool ty_definitely_assignable(Checker* me, Ty const* source, Ty const* target) {
    return definitely_memo(me, source, target, true);
}

// What the `infer` type parameters infer from the check type (tsc's getConditionalType: inferTypes, then the
// combinedMapper getInferredType makes): what isn't inferred or doesn't satisfy its constraint is the constraint
static TyMapper const* conditional_infer(Checker* me, Ty const* check_type, Ty const* extends, TyList infers, TyMapper const* outer) {
    // Only its own `infer X`: one of an outer conditional type is a type variable like any other here, which keeps
    // this one deferred until the outer one's true branch is instantiated with what it inferred
    Inference inf;
    inference_init(&inf, infers);
    inf.always_strict = true;
    infer_types(me, &inf, check_type, extends, INFER_PRIORITY_NORMAL);
    Ty const** params = arena_new_n(&my arena, Ty const*, inf.len);
    Ty const** results = arena_new_n(&my arena, Ty const*, inf.len);
    bool* none = arena_new_n(&my arena, bool, inf.len);
    for (u32 i = 0; i < inf.len; i++) {
        params[i] = inf.params[i].param;
        results[i] = inference_conditional_result(me, &inf, i);
        none[i] = !results[i];
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
        // (Also a constraint that stays generic, like tsc: `T3<X> = "a.b" extends `${infer S extends keyof X &
        // string}.${string}` ? S : 0` is 0 for T3<{}>; test/probes/plan19/zod/ark_infer_constraint.ts)
        // (Nothing inferred is the constraint without asking: unknown is assignable to `{}` without
        // strictNullChecks, tsc's getInferredType doesn't compare then)
        if (none[i] || !ty_is_related(me, results[i], constraint, TY_RELATION_ASSIGNABLE)) checked[i] = constraint;
    }
    results = checked;
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = params, .len = inf.len }, (TyList){ .data = results, .len = inf.len });
    inference_drop(&inf);
    return mapper;
}

// tsc's isDeferredType of a check type: what waits for its type variables rather than inferring from them
// (A tuple with a variadic element of a type variable is a generic object type too, tsc's isGenericTupleType:
// `[...M] extends [infer H, ...infer T]` waits for M)
static bool check_type_deferred(Checker* me, Ty const* check_type, bool check_tuples) {
    return ty_is_generic_index(check_type) || is_generic_mapped(me, check_type, 0) || ty_is_generic_tuple(me, check_type) ||
        (check_tuples && has_generic_element(me, check_type));
}

// The mapper a deferred conditional type's true branch is seen through (tsc's combinedMapper, which
// getInferredTrueTypeFromConditionalType instantiates it with): its `infer` type parameters as inferred from its check
// type, when that one isn't deferred itself (`((...args: A) => void) extends (...args: [A[0], ...infer R]) => void`
// infers R from A). Null when nothing was inferred
TyMapper const* ty_conditional_inferred_mapper(Checker* me, Ty const* conditional, TyList infers) {
    Ty const* check_type = ty_actual_type_variable(me, conditional->conditional.check);
    Ty const* extends = conditional->conditional.extends;
    if (check_type_deferred(me, check_type, conditional->conditional.check_tuples)) return nullptr;
    return conditional_infer(me, check_type, extends, infers, conditional->conditional.mapper);
}

Ty const* ty_conditional_resolve(
    Checker* me, Ty const* check_type, Ty const* extends, TyList infers, TyCondBranch const* if_true,
    TyCondBranch const* if_false, TyMapper const* outer, Ty const* constraint_root, bool check_tuples
) {
    bool for_constraint = constraint_root != nullptr;
    // (tsc compares with errorType itself, before deferring: an unresolved reference's error type (ty_unresolved, tsc's
    // errorTypes) is any to it, `Parameters<Unresolved>` is unknown[]; a union with one is errorType)
    Ty const* error = ty_intrinsic(me, TY_ERROR);
    if (check_type == error || extends == error) return error;
    // (tsc's isDeferredType of the check type: `Required<T> extends {}` waits for T)
    if (check_type_deferred(me, check_type, check_tuples)) return nullptr;

    TyMapper const* mapper = infers.len ? conditional_infer(me, check_type, extends, infers, outer) : nullptr;
    Ty const* inferred_extends = ty_instantiate(me, extends, mapper);
    // (tsc's isDeferredType of the extends type: `{} extends Mapped<T>` waits for T too)
    if (ty_is_generic_index(inferred_extends) || is_generic_mapped(me, inferred_extends, 0) ||
        ty_is_generic_tuple(me, inferred_extends) || (check_tuples && has_generic_element(me, inferred_extends))
    ) {
        return nullptr;
    }

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
    if (callable_definitely_related(me, check_type, inferred_extends)) return branch_type_with(me, if_true, mapper);
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
    if (for_constraint && if_false->node && if_false->node->tag == TYPE_TERNARY) {
        Ty const* nested = ty_from_node(me, if_false->node, if_false->ctx);
        if (nested->kind == TY_CONDITIONAL &&
            (!nested->conditional.distributive ||
                ty_non_distributed(nested->conditional.check) == ty_non_distributed(constraint_root->conditional.check))
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
