#include "ty/ty_internal.h"
#include "error.h"

// Type inference: walks a source type and a target type together and collects, for every type parameter being
// inferred that the target mentions, the source types found in its place. Follows what tsc observably does
// (inferTypes/getInferredType): candidates from covariant and contravariant positions are kept apart, candidates of a
// better priority replace worse ones, and a call picks the common supertype of the candidates where a conditional type
// takes their union.

// Zeroed but for the walk's stacks, read only below their depths: they are most of its ~900 bytes, and there are half
// a million inferences on vscode
static void inference_zero(Inference* inf) {
    char* bytes = (char*)inf;
    memset(bytes, 0, offsetof(Inference, visiting_sources));
    memset(bytes + offsetof(Inference, visited), 0, offsetof(Inference, object_sources) - offsetof(Inference, visited));
    memset(bytes + offsetof(Inference, object_depth), 0, sizeof(Inference) - offsetof(Inference, object_depth));
}

void inference_init(Inference* inf, TyList params) {
    inference_zero(inf);
    inf->cap = params.len > 4 ? params.len : 4;
    inf->params = calloc(inf->cap, sizeof(InferParam));
    panic_if(!inf->params, "out of memory");
    for (u32 i = 0; i < params.len; i++) {
        inf->params[inf->len++] = (InferParam){ .param = params.data[i], .top_level = true };
    }
}

void inference_drop(Inference* inf) {
    // (Most of these are null: no call for them)
    for (u32 i = 0; i < inf->len; i++) {
        if (inf->params[i].co) free(inf->params[i].co);
        if (inf->params[i].contra) free(inf->params[i].contra);
    }
    free(inf->params);
    if (inf->visited) {
        free(inf->visited);
        free(inf->visited_priority);
    }
    if (inf->circular) free(inf->circular);
    if (inf->array_literals) free(inf->array_literals);
    if (inf->inferred_type_params) free(inf->inferred_type_params);
    inference_zero(inf);
}

// The key's slot in `visited` (its priority in visited_priority), added if it wasn't there (`*existed` says)
static u32 visited_slot(Inference* inf, VisitKey key, bool* existed) {
    if ((inf->visited_len + 1) * 2 > inf->visited_cap) {
        u32 cap = inf->visited_cap ? inf->visited_cap * 2 : 16;
        VisitKey* grown = calloc(cap, sizeof(VisitKey));
        u32* grown_priority = calloc(cap, sizeof(u32));
        panic_if(!grown || !grown_priority, "out of memory");
        for (u32 i = 0; i < inf->visited_cap; i++) {
            VisitKey old = inf->visited[i];
            if (!old.pair) continue;
            u32 at = visit_key_hash(old) & (cap - 1);
            while (grown[at].pair) at = (at + 1) & (cap - 1);
            grown[at] = old;
            grown_priority[at] = inf->visited_priority[i];
        }
        free(inf->visited);
        free(inf->visited_priority);
        inf->visited = grown;
        inf->visited_priority = grown_priority;
        inf->visited_cap = cap;
    }
    u32 at = visit_key_hash(key) & (inf->visited_cap - 1);
    while (inf->visited[at].pair) {
        if (visit_key_eq(inf->visited[at], key)) {
            *existed = true;
            return at;
        }
        at = (at + 1) & (inf->visited_cap - 1);
    }
    inf->visited[at] = key;
    inf->visited_priority[at] = UINT32_MAX; // (While its walk runs: nothing inferred yet)
    inf->visited_len++;
    *existed = false;
    return at;
}

// A pair in the mode of the walk that meets it: the priority and variances it is inferred at are part of tsgo's
// InferenceKey, so a pair met contravariantly (a method's parameter) is walked again covariantly (a callback's
// parameter), each making its own candidates (webpack's tapable hooks: `promise(...args: AsArray<T>)` beside
// `tap(options, fn: (...args: AsArray<T>) => R)`)
static VisitKey visited_key(Inference const* inf, Ty const* source, Ty const* target) {
    u32 mode = inf->priority | (u32)inf->contravariant << 24 | (u32)inf->bivariant << 25 | (u32)inf->always_strict << 26;
    return (VisitKey){ .pair = ((u64)source->id << 32) | target->id, .mode = mode };
}

// False if the pair was there already; then what its walk inferred at counts as inferred here too (tsc's invokeOnce:
// `inferencePriority = min(inferencePriority, status)`), so a source seen before is still used up for a naked type
// parameter beside the target
static bool visited_add(Inference* inf, Ty const* source, Ty const* target) {
    VisitKey key = visited_key(inf, source, target);
    if (!key.pair) return true;
    bool existed;
    u32 at = visited_slot(inf, key, &existed);
    if (existed && inf->visited_priority[at] < inf->hit_priority) inf->hit_priority = inf->visited_priority[at];
    return !existed;
}

// After the walk of a pair visited_add() added: what it inferred at (the best priority since `saved`, which the caller
// reset hit_priority from)
static void visited_done(Inference* inf, Ty const* source, Ty const* target, u32 saved) {
    VisitKey key = visited_key(inf, source, target);
    if (key.pair) {
        bool existed;
        u32 at = visited_slot(inf, key, &existed);
        inf->visited_priority[at] = inf->hit_priority;
    }
    if (saved < inf->hit_priority) inf->hit_priority = saved;
}

static InferParam* inference_param(Inference* inf, Ty const* ty) {
    // (An indexed access is inferred to by reverse mapped types: `T[K]`, see reverse_mapped_prop_type())
    if (ty->kind != TY_TYPE_PARAM && ty->kind != TY_INDEXED) return nullptr;
    ty = ty_non_distributed(ty); // (tsgo's getInferenceInfoForType)
    for (u32 i = 0; i < inf->len; i++) {
        if (inf->params[i].param == ty) return &inf->params[i];
    }
    return nullptr;
}

static void candidates_push(Ty const*** list, u32* len, Ty const* candidate) {
    for (u32 i = 0; i < *len; i++) {
        if ((*list)[i] == candidate) return;
    }
    // (Room for 4, then doubling: the capacity follows from the length, which a better priority resets to 0 with the
    // list kept)
    u32 n = *len;
    if (n == 0 || (n >= 4 && !(n & (n - 1)))) {
        *list = realloc(*list, (n ? n * 2 : 4) * sizeof(Ty const*));
        panic_if(!*list, "out of memory");
    }
    (*list)[(*len)++] = candidate;
}

// tsc's isTypeParameterAtTopLevel: a union or intersection member, or a branch of a conditional type (up to three
// deep: `C1<C2<C3<T>>>` with each `T extends unknown ? T | undefined : never`)
static bool param_at_top_level(Checker* me, Ty const* type, Ty const* param, u32 depth) {
    if (type == param) return true;
    if (type->kind == TY_UNION || type->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < type->composite.types.len; i++) {
            if (param_at_top_level(me, type->composite.types.data[i], param, depth)) return true;
        }
    }
    if (type->kind == TY_CONDITIONAL && depth < 3) {
        return param_at_top_level(me, ty_conditional_branch(me, type, true), param, depth + 1) ||
            param_at_top_level(me, ty_conditional_branch(me, type, false), param, depth + 1);
    }
    return false;
}

static bool is_param_at_top_level(Checker* me, Ty const* type, Ty const* param) {
    return param_at_top_level(me, type, param, 0);
}

static void infer_from(Checker* me, Inference* inf, Ty const* source, Ty const* target);
static inline void infer_from_reduced(Checker* me, Inference* inf, Ty const* source, Ty const* target);

static void infer_with_priority(Checker* me, Inference* inf, Ty const* source, Ty const* target, u32 priority) {
    u32 saved = inf->priority;
    inf->priority = saved | priority;
    infer_from(me, inf, source, target);
    inf->priority = saved;
}

// A type parameter of the source signature isn't something to infer: it stands for its constraint (tsc's
// getBaseSignature). Constraints that mention the signature's own type parameters are resolved through each other
// (once per parameter), what is still left of them is `any`. Outer type parameters stay: `C extends keyof S` in a
// method of a class over S is `keyof S`
static TyMapper const* base_signature_mapper(Checker* me, Signature const* sig) {
    TyList params = sig_res(me, sig)->type_params;
    if (!params.len) return nullptr;
    u64 key[2] = { KEY_BASE_SIGNATURE, (u64)(uintptr_t)sig };
    TyMapper const* cached = intern_get(&my links, key, 2);
    if (cached) return cached;
    Ty const** constraints = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 i = 0; i < params.len; i++) {
        Ty const* constraint = ty_param_constraint(me, params.data[i]);
        constraints[i] = constraint ? constraint : ty_intrinsic(me, TY_UNKNOWN_);
    }
    TyMapper const* constraint_mapper = ty_mapper_new(me, params, (TyList){ .data = constraints, .len = params.len });
    Ty const** targets = arena_new_n(&my arena, Ty const*, params.len);
    memcpy(targets, constraints, sizeof(Ty const*) * params.len);
    for (u32 round = 0; round + 1 < params.len; round++) {
        for (u32 i = 0; i < params.len; i++) targets[i] = ty_instantiate(me, targets[i], constraint_mapper);
    }
    Ty const** anys = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 i = 0; i < params.len; i++) anys[i] = ty_intrinsic(me, TY_ANY);
    TyMapper const* eraser = ty_mapper_new(me, params, (TyList){ .data = anys, .len = params.len });
    for (u32 i = 0; i < params.len; i++) targets[i] = ty_instantiate(me, targets[i], eraser);
    TyMapper const* ret = ty_mapper_new(me, params, (TyList){ .data = targets, .len = params.len });
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

// What an optional parameter holds: `undefined` too, under strictNullChecks (tsc's addOptionality)
Ty const* ty_optional_param_type(Checker* me, Ty const* type) {
    if (!compiler_options()->strict_null_checks || (type->flags & TYF_UNDEFINED)) return type;
    return ty_union(me, (Ty const*[]){ type, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
}

// Its type without the undefined its optionality added: what an annotation says (a setter's, tsc's
// getAnnotatedAccessorType)
Ty const* ty_sig_param_declared_type(Checker* me, SigParam const* param) {
    return param->undefined_added ? ty_without_undefined(me, param->type) : param->type;
}

// The parameter symbol's type (tsc's getTypeOfSymbol): a `?` adds undefined, an initializer only to the signature's
// (getTypeOfParameter), and a pattern without annotation is its pattern's type (getTypeForVariableLikeDeclaration)
Ty const* ty_sig_param_symbol_type(Checker* me, SigParam const* param) {
    FunctionArg const* arg = param->decl;
    bool bare_pattern = arg && arg->initializer.binding.tag != BINDING_IDENTIFIER && !arg->initializer.binding.has_explicit_type;
    bool question = arg && (arg->explicit_optional || arg->jsdoc_optional_initialized) && !bare_pattern;
    return question ? param->type : ty_sig_param_declared_type(me, param);
}

// The parameters of `sig` from `from` on, as the tuple a rest parameter in that position would receive (tsc's
// getRestTypeAtPosition, less its shortcut for a lone rest parameter)
static Ty const* signature_params_tuple(Checker* me, Signature const* sig, u32 from, TyMapper const* mapper, bool readonly);

Ty const* ty_signature_params_tuple(Checker* me, Signature const* sig, u32 from, TyMapper const* mapper) {
    return signature_params_tuple(me, sig, from, mapper, false);
}

// `readonly`: the tuple is readonly (inferring to a const rest type parameter without a mutable array-like constraint)
static Ty const* signature_params_tuple(Checker* me, Signature const* sig, u32 from, TyMapper const* mapper, bool readonly) {
    SigResolved const* sig_r = sig_res(me, sig);
    // A rest parameter of a tuple type is its elements (tsc's createTupleType normalizing a variadic tuple element),
    // which `from` may be past the start of (tsc's getRestTypeAtPosition slices the tuple)
    Ty const* rest_tuple = nullptr;
    if (sig_r->param_count && sig_r->params[sig_r->param_count - 1].rest) {
        Ty const* rest = ty_instantiate(me, sig_r->params[sig_r->param_count - 1].type, mapper);
        if (ty_is_tuple(rest) && !ty_is_generic_tuple(me, rest)) rest_tuple = rest;
    }
    // Past an array rest parameter: an array of its elements (tsc's getRestTypeAtPosition), `(...z: number[])` from 1
    // is number[]
    if (!rest_tuple && sig_r->param_count && sig_r->params[sig_r->param_count - 1].rest && from >= sig_r->param_count) {
        Ty const* rest = ty_instantiate(me, sig_r->params[sig_r->param_count - 1].type, mapper);
        if (ty_is_array(me, rest)) return ty_array(me, ty_rest_element_type(me, rest), false);
    }
    TupleShape const* rest_shape = rest_tuple ? ty_tuple_shape(rest_tuple) : nullptr;
    u32 fixed_end = rest_shape ? sig_r->param_count - 1 : sig_r->param_count; // (Parameters before the tuple)
    u32 fixed = fixed_end > from ? fixed_end - from : 0;
    u32 skip = rest_shape && from > fixed_end ? from - fixed_end : 0;
    u32 rest_count = rest_shape && rest_shape->arity > skip ? rest_shape->arity - skip : 0;
    u32 arity = fixed + rest_count;
    unsigned char* flags = calloc(arity ? arity : 1, 1);
    // (Names and labels in one block)
    void const** name_block = calloc(arity ? arity * 2 : 2, sizeof(void const*));
    panic_if(!name_block, "out of memory");
    Atom const** names = (Atom const**)name_block;
    void const** labels = name_block + (arity ? arity : 1);
    Ty const** elements = calloc(arity ? arity : 1, sizeof(Ty const*));
    // (tsc's getMinArgumentCount: trailing parameters that accept void can be left out, `(_: void) => void` takes
    // `[_?: void | undefined]`)
    u32 min_args = fixed ? ty_min_argument_count(me, sig) : 0;
    for (u32 j = 0; j < fixed; j++) {
        SigParam const* param = &sig_r->params[from + j];
        // Optional past the minimum argument count (an untyped JS function's parameters all are); labelled by
        // parameter declarations with a plain name (getNameableDeclarationAtPosition: not a JS function's
        // synthetic `args`, nor a destructuring)
        bool optional = param->optional || from + j >= min_args;
        flags[j] = param->rest ? TUPLE_ELEMENT_REST : optional ? TUPLE_ELEMENT_OPTIONAL : 0;
        names[j] = param->decl && param->decl->initializer.binding.tag == BINDING_IDENTIFIER ? param->name : nullptr;
        labels[j] = names[j] ? param->decl : nullptr;
        // (An optional element gets its undefined from the tuple: tsc's createNormalizedTupleType addOptionality)
        elements[j] = ty_instantiate(me, param->type, mapper);
    }
    if (rest_count) {
        TyList rest_args = ty_reference_args(me, rest_tuple);
        for (u32 k = 0; k < rest_count; k++) {
            u32 j = fixed + k;
            flags[j] = rest_shape->flags[skip + k];
            names[j] = rest_shape->names ? rest_shape->names[skip + k] : nullptr;
            labels[j] = rest_shape->labels ? rest_shape->labels[skip + k] : nullptr;
            elements[j] = rest_args.data[skip + k];
        }
    }
    TupleShape shape = { .arity = arity, .flags = flags, .names = names, .readonly = readonly, .labels = labels };
    Ty const* target = ty_tuple_target(me, &shape);
    Ty const* ret = arity ? ty_reference(me, target, (TyList){ .data = elements, .len = arity }) : target;
    free(flags);
    free(name_block);
    free(elements);
    return ret;
}

// `top`: a walk of its own at that priority (tsc's inferTypes()), else a step of the walk in progress
typedef struct InferStep {
    bool top;
    u32 priority;
    bool contravariant; // (top only) starting in a contravariant position
} InferStep;

static void infer_types_from(Checker* me, Inference* inf, Ty const* source, Ty const* target, u32 priority, bool contravariant);
static void infer_to_template(Checker* me, Inference* inf, Ty const* source, Ty const* target);

static void infer_step(Checker* me, Inference* inf, InferStep step, Ty const* source, Ty const* target) {
    if (step.top) infer_types_from(me, inf, source, target, step.priority, step.contravariant);
    else infer_from(me, inf, source, target);
}

// The type of parameter position i of `sig` instantiated with `mapper` (tsc's getTypeAtPosition, without optionality):
// a rest parameter stands for every position from its own on, a rest tuple for its elements. Null past the end
static Ty const* source_type_at(Checker* me, Signature const* sig, u32 i, TyMapper const* mapper) {
    SigResolved const* sig_r = sig_res(me, sig);
    if (i < sig_r->param_count && !sig_r->params[i].rest) {
        // (An optional parameter's type has the undefined, tsc's getTypeAtPosition)
        return ty_instantiate(me, sig_r->params[i].type, mapper);
    }
    if (!sig_r->param_count || !sig_r->params[sig_r->param_count - 1].rest) return nullptr;
    Ty const* rest = ty_instantiate(me, sig_r->params[sig_r->param_count - 1].type, mapper);
    // (`[]` too, a tuple target rather than a reference: `(...args: Parameters<() => R>)` has no parameter 0)
    TupleShape const* shape = ty_tuple_shape(rest);
    if (shape) {
        TyList elements = shape->arity ? ty_reference_args(me, rest) : (TyList){0};
        u32 j = i + 1 - sig_r->param_count;
        bool ends_in_rest = shape->arity && (shape->flags[shape->arity - 1] & TUPLE_ELEMENT_REST);
        if (j >= shape->arity) return ends_in_rest ? ty_rest_element_type(me, elements.data[shape->arity - 1]) : nullptr;
        if (shape->flags[j] & TUPLE_ELEMENT_REST) return ty_rest_element_type(me, elements.data[j]);
        return elements.data[j];
    }
    // A generic rest type: its element by position (tsc's tryGetTypeAtPosition: `A[0]`)
    if (rest->kind == TY_TYPE_PARAM || (rest->flags & TYF_DEFERRED)) {
        Value index = value_number((double)(i + 1 - sig_r->param_count));
        return ty_indexed(me, rest, ty_literal(me, &index));
    }
    return ty_rest_element_type(me, rest);
}

static u32 tuple_fixed_length(TupleShape const* shape);

// The rest type of a rest parameter of type `type` past its tuple's leading fixed elements (tsc's getEffectiveRestType),
// null for a tuple without a variable part
static Ty const* effective_rest_type(Checker* me, Ty const* type) {
    TupleShape const* shape = ty_is_tuple(type) ? ty_tuple_shape(type) : nullptr;
    u32 fixed = shape ? tuple_fixed_length(shape) : 0;
    if (!fixed) return type;
    return fixed < shape->arity ? ty_slice_tuple(me, shape, ty_reference_args(me, type), fixed, 0) : nullptr;
}

// From the parameters of `source` from position `pos` on to a rest parameter type of the target (tsc's
// applyToParameterTypes: getRestTypeAtPosition(source, paramCount) to the target's effective rest type)
static void infer_from_rest_params(
    Checker* me, Inference* inf, InferStep step, Signature const* source, u32 pos, TyMapper const* mapper, Ty const* target_rest
) {
    SigResolved const* source_r = sig_res(me, source);
    // `(...args: A) => any`: the rest of the source's parameters as a tuple
    // (A lone rest parameter as it is, unless it's a tuple: its elements are parameters, and tsc's
    // getRestTypeAtPosition makes a new tuple of them, mutable unless inferring to a const type parameter;
    // only the variable tail tsc's getEffectiveRestType slices off a tuple starting with one is mutable
    // either way: `(...args: readonly [s: string]) => R` infers `[s: string]` for `Fn<infer P>`)
    Ty const* rest = pos < source_r->param_count && source_r->params[pos].rest && pos + 1 == source_r->param_count ?
        ty_instantiate(me, source_r->params[pos].type, mapper) : nullptr;
    bool starts_variable = false;
    if (rest && ty_is_tuple(rest) && !ty_is_generic_tuple(me, rest)) {
        TupleShape const* shape = ty_tuple_shape(rest);
        starts_variable = shape->arity && (shape->flags[0] & TUPLE_ELEMENT_REST);
        rest = nullptr;
    }
    if (rest) {
        // (A rest parameter of type any is any[]: tsc's getEffectiveRestType)
        if (rest->kind == TY_INTRINSIC && (rest->flags & TYF_ANY)) rest = ty_array(me, rest, false);
        infer_step(me, inf, step, rest, target_rest);
        return;
    }
    // (Readonly for a const type parameter that isn't constrained to a mutable array-like, tsc's
    // applyToParameterTypes)
    bool readonly = !starts_variable && ty_is_const_type_variable(me, target_rest, 0) && !ty_some_mutable_array_like(me, target_rest);
    infer_step(me, inf, step, signature_params_tuple(me, source, pos, mapper, readonly), target_rest);
}

// From the `this` and parameter types of `source` (instantiated with `mapper`) to those of `target` (tsc's
// applyToParameterTypes)
static void infer_from_params(
    Checker* me, Inference* inf, InferStep step, Signature const* source, Signature const* target, TyMapper const* mapper
) {
    SigResolved const* source_r = sig_res(me, source);
    SigResolved const* target_r = sig_res(me, target);
    if (source_r->this_type && target_r->this_type) {
        infer_step(me, inf, step, ty_instantiate(me, source_r->this_type, mapper), target_r->this_type);
    }
    for (u32 i = 0; i < target_r->param_count; i++) {
        SigParam const* target_param = &target_r->params[i];
        if (target_param->rest) {
            // A rest parameter of a tuple type: its leading fixed elements are parameters of their own, paired by
            // position while the source has parameters there, and what follows them (if anything) is the rest type
            // the rest of the source goes to (tsc's getParameterCount and getEffectiveRestType: `(...args: [A[0],
            // ...infer R])` against `(...args: []) => void` infers `[]` for R)
            TupleShape const* shape = ty_is_tuple(target_param->type) ? ty_tuple_shape(target_param->type) : nullptr;
            u32 fixed = shape ? tuple_fixed_length(shape) : 0;
            if (fixed) {
                TyList elements = ty_reference_args(me, target_param->type);
                u32 j = 0;
                for (; j < fixed; j++) {
                    Ty const* source_type = source_type_at(me, source, i + j, mapper);
                    if (!source_type) break;
                    infer_step(me, inf, step, source_type, elements.data[j]);
                }
                Ty const* rest = effective_rest_type(me, target_param->type);
                if (rest) infer_from_rest_params(me, inf, step, source, i + j, mapper, rest);
                break;
            }
            infer_from_rest_params(me, inf, step, source, i, mapper, target_param->type);
            break;
        }
        Ty const* source_type = source_type_at(me, source, i, mapper);
        if (!source_type) {
            // Past the source's parameters, a rest parameter of the target still gets the rest of them: none, `[]`
            // (tsc's applyToParameterTypes: getRestTypeAtPosition(source, paramCount))
            SigParam const* last = target_r->param_count ? &target_r->params[target_r->param_count - 1] : nullptr;
            Ty const* rest = last && last->rest ? effective_rest_type(me, last->type) : nullptr;
            if (rest) {
                bool readonly = ty_is_const_type_variable(me, rest, 0) && !ty_some_mutable_array_like(me, rest);
                infer_step(me, inf, step, signature_params_tuple(me, source, i, mapper, readonly), rest);
            }
            break;
        }
        infer_step(me, inf, step, source_type, target_param->type);
    }
}

// tsc's couldContainTypeVariables where tttt's flag knows the type has none: an anonymous type of a function, type
// literal or object literal (an object rest type too), or a union or intersection with one, unless it's a non-generic
// alias's (isNonGenericTopLevelType)
static bool tsc_could_contain_type_variables(Ty const* ty) {
    if (ty->alias && !ty->alias_args.len) return false;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        if (ty->flags & TYF_ENUM_LITERAL) return false;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (tsc_could_contain_type_variables(ty->composite.types.data[i])) return true;
        }
        return false;
    }
    if (ty->kind != TY_ANONYMOUS) return false;
    Ty const* root = ty->anonymous.target ? ty->anonymous.target : ty;
    return root->anonymous.node || root->anonymous.function || root->anonymous.object_literal || root->anonymous.mapped ||
        ty->anonymous.rest_of_binding;
}

static void force_source_parts(Checker* me, Ty const* source, Ty const* target, u32 depth);

// From the return type (or type predicate) of `source` to that of `target` (tsc's applyToReturnTypes)
static void infer_from_returns(
    Checker* me, Inference* inf, InferStep step, Signature const* source, Signature const* target, TyMapper const* mapper
) {
    // (The source's predicate only for a target with one: inferring it from an unannotated source evaluates the source's
    // returns, which may be what is being resolved, `Parameters<typeof f>` in a signature f's body calls)
    TyPredicate const* target_predicate = signature_predicate(me, target);
    TyPredicate const* source_predicate = target_predicate ? signature_predicate(me, source) : nullptr;
    if (source_predicate && target_predicate && source_predicate->type && target_predicate->type &&
        source_predicate->is_this == target_predicate->is_this && source_predicate->asserts == target_predicate->asserts
    ) {
        infer_step(me, inf, step, ty_instantiate(me, source_predicate->type, mapper), target_predicate->type);
        return;
    }
    // (Only a target that could mention what's inferred asks for the source's, which may still be being inferred)
    Ty const* target_return = signature_return_type(me, target);
    if (!(target_return->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        // tsc's couldContainTypeVariables says yes to an object literal's, type literal's or function's type (not
        // knowing better): the source's return type is asked for even so, which finds it in flight, `function g()
        // { return f(g); }` against `(x: T) => typeof obj` (TS7023). Nothing is inferred from it
        if (tsc_could_contain_type_variables(target_return)) force_source_parts(me, signature_return_type(me, source), target_return, 4);
        return;
    }
    infer_step(me, inf, step, ty_instantiate(me, signature_return_type(me, source), mapper), target_return);
}

static void infer_from_signatures(Checker* me, Inference* inf, Signature const* source, Signature const* target) {
    signature_resolve(me, source);
    SigResolved const* target_res = signature_resolve(me, target);
    if (target_res->type_params.len) {
        // The target's own type parameters are any (tsc's getErasedSignature): they aren't what is being inferred, and
        // walking into them would infer to them (`map<M>` of `N1<X>` compared with itself)
        Ty const* stack_anys[16];
        Ty const** anys = target_res->type_params.len <= c_arraylen(stack_anys) ? stack_anys : malloc(sizeof(Ty const*) * target_res->type_params.len);
        panic_if(!anys, "out of memory");
        for (u32 i = 0; i < target_res->type_params.len; i++) anys[i] = ty_intrinsic(me, TY_ANY);
        target = ty_signature_instantiation(me, target, (TyList){ .data = anys, .len = sig_res(me, target)->type_params.len });
        if (anys != stack_anys) free(anys);
        signature_resolve(me, target);
    }
    TyMapper const* base = base_signature_mapper(me, source);
    // Parameters are a contravariant position. Without strictFunctionTypes they compare bivariantly and tsc infers as
    // if they were covariant; the parameters of a method (and everything nested in them) are bivariant too
    bool flip = compiler_options()->strict_function_types || inf->always_strict;
    bool saved_bivariant = inf->bivariant;
    inf->bivariant |= (target->flags & SIGNATURE_METHOD) != 0;
    if (flip) inf->contravariant = !inf->contravariant;
    if (!(source->flags & SIGNATURE_NON_INFERRABLE)) infer_from_params(me, inf, (InferStep){0}, source, target, base);
    if (flip) inf->contravariant = !inf->contravariant;
    inf->bivariant = saved_bivariant;
    infer_from_returns(me, inf, (InferStep){0}, source, target, base);
}

// Signature instantiation

Signature const* ty_signature_instantiation(Checker* me, Signature const* sig, TyList args) {
    SigResolved const* sig_rs = signature_resolve(me, sig);
    check(args.len == sig_rs->type_params.len);
    u64 stack_key[18];
    u32 key_len = 2 + args.len;
    u64* key = key_len <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * key_len);
    panic_if(!key, "out of memory");
    key[0] = KEY_SIGNATURE_INSTANCE;
    key[1] = (u64)(uintptr_t)sig;
    for (u32 i = 0; i < args.len; i++) key[2 + i] = args.data[i]->id;
    void const** slot = intern_slot(me, &my links, key, key_len);
    if (key != stack_key) free(key);
    if (*slot) return *slot;
    Ty const** targets = arena_new_n(&my arena, Ty const*, args.len ? args.len : 1);
    memcpy(targets, args.data, sizeof(Ty const*) * args.len);
    Signature* ret = arena_new(&my arena, Signature);
    *ret = (Signature){
        .flags = sig->flags,
        .target = sig,
        .mapper = ty_mapper_new(me, sig_rs->type_params, (TyList){ .data = targets, .len = args.len }),
       
        .instantiates_own_params = true,
    };
    // (Its own until resolved: published with --shared-graph once it is)
    *slot = ret;
    signature_resolve(me, ret); // (Without type parameters, see signature_resolve_slow())
    *slot = nullptr;
    return intern_set(me, &my links, slot, ret);
}

static bool has_candidates(InferParam const* param) {
    return param->co_len || param->contra_len;
}

static bool names_type_param(Ty const* const* list, u32 len, Atom const* name) {
    for (u32 i = 0; i < len; i++) {
        if (list[i]->param.name == name) return true;
    }
    return false;
}

// tsc's getUniqueTypeParameters: the type parameters of `sig`, those whose name is taken by one propagated already (or
// an earlier one of the list) replaced by a copy named `T1`, `T2`... whose constraint refers to the renamed ones
static TyList unique_type_params(Checker* me, Inference* inf, TyList params) {
    Ty const** ret = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    Ty const** all = malloc(sizeof(Ty const*) * (inf->inferred_type_param_count + params.len + 1));
    panic_if(!all, "out of memory");
    memcpy(all, inf->inferred_type_params, sizeof(Ty const*) * inf->inferred_type_param_count);
    u32 all_len = inf->inferred_type_param_count;
    Ty const** old = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    Ty const** renamed = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    u32 renamed_count = 0;
    for (u32 i = 0; i < params.len; i++) {
        Ty const* param = params.data[i];
        Atom const* name = param->param.name;
        if (names_type_param(all, all_len, name)) {
            // (tsc's getUniqueTypeParameterName: the name without its trailing digits, numbered from 1: T1, T2)
            csview base = atom_sv(name);
            while (base.size > 1 && base.buf[base.size - 1] >= '0' && base.buf[base.size - 1] <= '9') base.size--;
            Atom const* fresh_name = name;
            for (u32 n = 1; names_type_param(all, all_len, fresh_name); n++) {
                cstr text = cstr_d(base, n);
                fresh_name = checker_atom(me, cstr_sv(&text));
                cstr_drop(&text);
            }
            old[renamed_count] = param;
            param = renamed[renamed_count++] = ty_clone_type_param(me, param, nullptr, fresh_name);
        }
        ret[i] = param;
        all[all_len++] = param;
    }
    free(all);
    if (renamed_count) {
        TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = old, .len = renamed_count }, (TyList){ .data = renamed, .len = renamed_count });
        for (u32 i = 0; i < renamed_count; i++) ((Ty*)renamed[i])->param.state->clone_mapper = mapper;
    }
    return (TyList){ .data = ret, .len = params.len };
}

Signature const* inference_propagate_generic(Checker* me, Inference* inf, Signature const* sig, Signature const* contextual) {
    SigResolved const* sig_rs = signature_resolve(me, sig);
    signature_resolve(me, contextual);
    TyList unique = unique_type_params(me, inf, sig_rs->type_params);
    Signature const* instantiated = ty_signature_instantiation(me, sig, unique);
    // From the instantiated parameters to the contextual ones, into a fresh set of candidates (contravariantly: the
    // contextual signature's parameters receive what the argument's accept)
    Ty const** params = malloc(sizeof(Ty const*) * (inf->len ? inf->len : 1));
    panic_if(!params, "out of memory");
    for (u32 i = 0; i < inf->len; i++) params[i] = inf->params[i].param;
    Inference fresh;
    inference_init(&fresh, (TyList){ .data = params, .len = inf->len });
    free(params);
    infer_from_params(me, &fresh, (InferStep){ .top = true, .priority = INFER_PRIORITY_NORMAL, .contravariant = true },
        instantiated, contextual, nullptr);
    bool any = false;
    for (u32 i = 0; i < fresh.len; i++) any |= has_candidates(&fresh.params[i]);
    Signature const* ret = nullptr;
    if (any) {
        // One or more of the contextual signature's type parameters are in its parameters: the returns too
        infer_from_returns(me, &fresh, (InferStep){ .top = true, .priority = INFER_PRIORITY_NORMAL }, instantiated, contextual, nullptr);
        bool overlap = false;
        for (u32 i = 0; i < fresh.len; i++) overlap |= has_candidates(&inf->params[i]) && has_candidates(&fresh.params[i]);
        if (!overlap) {
            // tsc's mergeInferences
            for (u32 i = 0; i < fresh.len; i++) {
                if (has_candidates(&inf->params[i]) || !has_candidates(&fresh.params[i])) continue;
                free(inf->params[i].co);
                free(inf->params[i].contra);
                inf->params[i] = fresh.params[i];
                fresh.params[i].co = fresh.params[i].contra = nullptr;
                fresh.params[i].co_len = fresh.params[i].contra_len = 0;
            }
            u32 count = inf->inferred_type_param_count + unique.len;
            inf->inferred_type_params = realloc(inf->inferred_type_params, sizeof(Ty const*) * (count ? count : 1));
            panic_if(!inf->inferred_type_params, "out of memory");
            memcpy(inf->inferred_type_params + inf->inferred_type_param_count, unique.data, sizeof(Ty const*) * unique.len);
            inf->inferred_type_param_count = count;
            ret = instantiated;
        }
    }
    inference_drop(&fresh);
    return ret;
}

Signature const* ty_signature_with_type_params(Checker* me, Signature const* sig, TyList type_params) {
    signature_resolve(me, sig);
    signature_return_type(me, sig);
    signature_predicate(me, sig);
    Signature* ret = arena_new(&my arena, Signature);
    signature_copy(me, ret, sig);
    SigResolved res = *sig_res(me, ret);
    res.type_params = type_params;
    sig_res_init(ret, sig_resolved_new(me, res));
    return ret;
}

Signature const* ty_instantiate_signature_in_context_of(
    Checker* me, Signature const* sig, Signature const* contextual, TyMapper const* context_mapper, bool in_relation
) {
    SigResolved const* sig_rs = signature_resolve(me, sig);
    signature_resolve(me, contextual);
    Inference inf;
    inference_init(&inf, sig_rs->type_params);
    inf.within_relation = in_relation;
    // (Parameters to parameters, covariantly: the contextual signature's parameter types are what `sig`'s receive)
    infer_from_params(me, &inf, (InferStep){ .top = true, .priority = INFER_PRIORITY_NORMAL }, contextual, sig, context_mapper);
    if (!context_mapper) {
        infer_from_returns(me, &inf, (InferStep){ .top = true, .priority = INFER_PRIORITY_RETURN_TYPE }, contextual, sig, nullptr);
    }
    TyMapper const* mapper = inference_mapper(me, sig, &inf, false);
    inference_drop(&inf);
    return ty_signature_instantiation(me, sig, (TyList){ .data = mapper->targets, .len = sig_rs->type_params.len });
}

static void infer_from_signature_lists(
    Checker* me, Inference* inf, Signature* const* sources, u32 source_count, Signature* const* targets, u32 target_count
) {
    // Matched up from the end: for overloads the last one is the most general. A target with more signatures than the
    // source has its first ones all inferred from the source's first (tsc's inferFromSignatures)
    if (!source_count) return;
    for (u32 i = 0; i < target_count; i++) {
        i64 at = (i64)source_count - (i64)target_count + (i64)i;
        infer_from_signatures(me, inf, sources[at > 0 ? at : 0], targets[i]);
    }
}

static bool is_empty_tuple(Ty const* ty) {
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_TUPLE_TARGET;
}

// What a property of type `source` says about `T[K]` in the template of `{ [K in keyof T]: Template }` (tsc's
// inferReverseMappedType): the template is inferred to with `T[K]` as the one type variable
Ty const* ty_reverse_mapped_prop_type(Checker* me, Ty const* source, MappedInfo const* mapped, Ty const* operand) {
    Ty const* indexed = ty_indexed(me, operand, mapped->type_param);
    Inference inf;
    inference_init(&inf, (TyList){ .data = &indexed, .len = 1 });
    // (tsc's getTemplateTypeFromMappedType: a `?` mapped type's template is optional, which takes an optional source
    // property's undefined)
    Ty const* template = ty_mapped_template(me, mapped);
    if (mapped->optional > 0 && compiler_options()->strict_null_checks) {
        template = ty_union(me, (Ty const*[]){ template, ty_undefined_or_missing(me) }, 2, nullptr, (TyList){0});
    }
    infer_types(me, &inf, source, template, INFER_PRIORITY_NORMAL);
    Ty const* ret = inference_conditional_result(me, &inf, 0);
    inference_drop(&inf);
    // (Widened: a nested object literal's type isn't fresh, its excess properties aren't checked against what the
    // reverse mapped type is related to, tsc 6's inferReverseMappedTypeWorker getWidenedType)
    return ret ? ty_widened_for_declaration(me, ret, false) : ty_intrinsic(me, TY_UNKNOWN_);
}

// The T for which `{ [K in keyof T]: Template }` would be `source` (tsc's createReverseMappedType), eagerly. Null if
// the source isn't an object with properties or a string index
static Ty const* reverse_mapped_type_uncached(Checker* me, Ty const* source, MappedInfo const* mapped, Ty const* operand);

// A mapped type that is an instantiation of another one with its `keyof P` operand P replaced says the same about the
// source as that one does about P, with the other type parameters mapped the same: what tsc gets to by
// replaceIndexedAccess in resolveReverseMappedTypeMembers. That's what makes a recursive mapped type's reverse mapped
// types the same type at every level, `Deep<T>`'s `{ [K in keyof T]: Deep<T[K]> }` inferred to from `interface A { a: A }`
static void reverse_mapped_canonical(Checker* me, MappedInfo const** mapped, Ty const** operand) {
    for (;;) {
        MappedInfo const* root = (*mapped)->template_from;
        if (!root || root->constraint->kind != TY_KEYOF || root->constraint->keyof.operand->kind != TY_TYPE_PARAM) return;
        Ty const* param = root->constraint->keyof.operand;
        TyMapper const* mapper = (*mapped)->template_mapper;
        bool found = false, others = false;
        for (u32 i = 0; i < mapper->len; i++) {
            if (mapper->sources[i] == param) {
                if (mapper->targets[i] != *operand) return;
                found = true;
            } else {
                others |= mapper->targets[i] != mapper->sources[i];
            }
        }
        if (!found) return;
        *operand = param;
        if (others) {
            *mapped = ty_mapped_info_instantiate(me, root, ty_mapper_without(me, mapper, (TyList){ .data = &param, .len = 1 }));
            return;
        }
        *mapped = root;
    }
}

// The type of a member of a reverse mapped type whose source member has type `source`. Of one for `T[K]`, nested in one
// for T, inferred like the outer one's are (tsc's replaceIndexedAccess in resolveReverseMappedTypeMembers, for
// properties only): that's what ends a recursive mapped type's nesting
Ty const* ty_reverse_mapped_member_type(Checker* me, ReverseMapped const* reverse, Ty const* source) {
    MappedInfo const* mapped = reverse->mapped;
    Ty const* operand = reverse->operand;
    if (operand->kind == TY_INDEXED && operand->indexed.object->kind == TY_TYPE_PARAM &&
        operand->indexed.index->kind == TY_TYPE_PARAM
    ) {
        reverse_mapped_canonical(me, &mapped, &operand);
    }
    return ty_reverse_mapped_prop_type(me, source, mapped, operand);
}

// Cached like tsc's reverseHomomorphicMappedCache: `keyof T & ("a" | "b" | ...)` as the constraint asks for the same
// one once per member (tsc's createComputedCompilerOptions, 135 of them). Only at the top level, where the depth
// cutoff below doesn't depend on the caller
static Ty const* reverse_mapped_type(Checker* me, Ty const* source, MappedInfo const* mapped, Ty const* operand) {
    if (my reverse_mapped_depth) return reverse_mapped_type_uncached(me, source, mapped, operand);
    u64 key[4] = { KEY_REVERSE_MAPPED, source->id, (u64)(uintptr_t)mapped, operand->id };
    void const* cached = intern_get(&my links, key, 4);
    if (cached) return cached == mapped ? nullptr : cached;
    Ty const* ret = reverse_mapped_type_uncached(me, source, mapped, operand);
    // (Release: a nested request for the same key may have published its result already, which this one replaces,
    // --shared-graph's other checkers reading it)
    __atomic_store_n(intern_slot(me, &my links, key, 4), ret ? (void const*)ret : (void const*)mapped, __ATOMIC_RELEASE);
    return ret;
}

// Of an array or tuple: an array or tuple of the reverse mapped element types
static Ty const* reverse_mapped_array_like(Checker* me, Ty const* source, MappedInfo const* mapped, Ty const* operand) {
    // (A tuple target itself, the declared type, has no arguments to map)
    if (source->kind != TY_REFERENCE) return source;
    TyList args = ty_reference_args(me, source);
    if (ty_is_array(me, source)) {
        Ty const* element = ty_reverse_mapped_prop_type(me, args.data[0], mapped, operand);
        return ty_array(me, element, source->reference.target == my readonly_array_target);
    }
    TupleShape const* shape = ty_tuple_shape(source);
    if (!shape->arity) return source;
    Ty const** elements = malloc(sizeof(Ty const*) * shape->arity);
    unsigned char* flags = malloc(shape->arity);
    panic_if(!elements || !flags, "out of memory");
    for (u32 i = 0; i < shape->arity; i++) {
        Ty const* element = args.data[i];
        if ((shape->flags[i] & TUPLE_ELEMENT_REST) && ty_is_array(me, element)) {
            // A rest element is held as its array: it's the element type that is reverse mapped (tsc's getElementTypes)
            Ty const* reversed = ty_reverse_mapped_prop_type(me, ty_reference_args(me, element).data[0], mapped, operand);
            elements[i] = ty_array(me, reversed, element->reference.target == my readonly_array_target);
        } else {
            elements[i] = ty_reverse_mapped_prop_type(me, element, mapped, operand);
        }
        // (`?` in the mapped type: T's element needn't be optional)
        flags[i] = mapped->optional > 0 ? shape->flags[i] & ~(unsigned)TUPLE_ELEMENT_OPTIONAL : shape->flags[i];
    }
    TupleShape reversed = { .arity = shape->arity, .readonly = shape->readonly, .flags = flags, .names = shape->names,
        .labels = shape->labels };
    Ty const* ret = ty_reference(me, ty_tuple_target(me, &reversed), (TyList){ .data = elements, .len = shape->arity });
    free(elements);
    free(flags);
    return ret;
}

// tsc's isPartiallyInferableType: not made non-inferrable by a context-sensitive function left out of the first pass,
// or an object literal with a property (a tuple with an element) that is partially inferable
static bool is_partially_inferable(Checker* me, Ty const* ty, u32 depth) {
    if (!(ty->flags & TYF_NON_INFERRABLE)) return true;
    if (depth > 8) return false;
    if (ty_tuple_shape(ty)) {
        TyList elements = ty_reference_args(me, ty);
        for (u32 i = 0; i < ty_tuple_shape(ty)->arity && i < elements.len; i++) {
            if (is_partially_inferable(me, elements.data[i], depth + 1)) return true;
        }
        return false;
    }
    if (ty->kind != TY_ANONYMOUS || !(ty->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, ty);
    for (u32 i = 0; i < members->prop_count; i++) {
        if (is_partially_inferable(me, prop_type(me, members->props[i]), depth + 1)) return true;
    }
    return false;
}

// The properties of what's reverse mapped (tsc's getPropertiesOfType): an object's own, an intersection's those of all
// of its members, each name once, of the type and with the modifiers the intersection has for it. malloc'd
u32 ty_reverse_mapped_source_props(Checker* me, Ty const* source, ObjectLiteralProp** out) {
    *out = nullptr;
    if (source->flags & TYF_OBJECT) {
        Members const* members = ty_members(me, source);
        *out = calloc(members->prop_count + 1, sizeof(ObjectLiteralProp));
        panic_if(!*out, "out of memory");
        for (u32 i = 0; i < members->prop_count; i++) {
            Prop const* prop = members->props[i];
            // (Declared where the source's is: tsc's createReverseMappedType copies its declarations, which is where
            // TS6500 points and what the symbols baseline shows)
            Prop const* origin = prop;
            while (origin->target) origin = origin->target;
            PropDecl const* decl = origin->decl;
            TypeNode const* annotation = origin == prop && decl->node && decl->node->parsed && !decl->lazy &&
                !decl->function && !prop_decl_rare(decl)->overload_count && !decl->symbol ? decl->node : nullptr;
            (*out)[i] = (ObjectLiteralProp){
                .name = prop->name, .flags = prop->flags, .type = prop_type(me, prop), .name_form = ty_prop_name_form_of(prop),
                .name_span = prop->decl->name_span,
                // (An instantiated property's declarations are its target's: tsc's instantiateSymbol)
                .decl_spans = prop->decl->decl_span_count ? prop->decl->decl_spans : decl->decl_spans,
                .decl_span_count = prop->decl->decl_span_count ? prop->decl->decl_span_count : decl->decl_span_count,
                .name_is_value = prop->decl->name_is_value,
                // (Or an object literal property's type assertion: tsc's typeFromExpression reuses it too)
                .asserted = annotation ? annotation : origin == prop ? prop_decl_asserted(decl) : nullptr, .name_type = prop_decl_rare(prop->decl)->name_type,
            };
        }
        return members->prop_count;
    }
    u32 count = 0, cap = 0;
    for (u32 p = 0; p < source->composite.types.len; p++) {
        Ty const* part = ty_apparent_type(me, source->composite.types.data[p]);
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, part);
        for (u32 i = 0; i < members->prop_count; i++) {
            Atom const* name = members->props[i]->name;
            bool seen = false;
            for (u32 k = 0; k < count && !seen; k++) seen = (*out)[k].name == name;
            if (seen) continue;
            FoundProp found = ty_find_prop(me, source, name);
            if (!found.found) continue;
            if (count == cap) {
                cap = cap ? cap * 2 : 8;
                *out = realloc(*out, sizeof(ObjectLiteralProp) * cap);
                panic_if(!*out, "out of memory");
            }
            (*out)[count++] = (ObjectLiteralProp){
                .name = name, .flags = found.flags, .type = found.type,
                .name_form = found.prop ? ty_prop_name_form_of(found.prop) : 0,
                .name_type = found.prop ? prop_decl_rare(found.prop->decl)->name_type : nullptr,
                .name_span = found.prop ? found.prop->decl->name_span : span_null,
                .decl_spans = found.prop ? found.prop->decl->decl_spans : nullptr,
                .decl_span_count = found.prop ? found.prop->decl->decl_span_count : 0,
            };
        }
    }
    return count;
}

// Whether a source can be reverse mapped through a homomorphic mapped type: an object type or an intersection with a
// string index signature, or with properties and partially inferable (tsc's createReverseMappedType). A function type
// without properties can't: `{ [P in keyof T]: T[P] }` infers nothing for T from it
static bool reverse_mappable(Checker* me, Ty const* source) {
    if (!(source->flags & TYF_OBJECT) && source->kind != TY_INTERSECTION) return false;
    if (ty_applicable_index_info(me, source, ty_intrinsic(me, TY_STRING))) return true;
    ObjectLiteralProp* source_props;
    u32 source_prop_count = ty_reverse_mapped_source_props(me, source, &source_props);
    free(source_props);
    return source_prop_count && is_partially_inferable(me, source, 0);
}

static Ty const* reverse_mapped_type_uncached(Checker* me, Ty const* source, MappedInfo const* mapped, Ty const* operand) {
    if (!reverse_mappable(me, source)) return nullptr;
    ObjectLiteralProp* source_props;
    u32 source_prop_count = ty_reverse_mapped_source_props(me, source, &source_props);
    if (ty_is_array(me, source) || ty_tuple_shape(source)) {
        free(source_props);
        if (my reverse_mapped_depth >= 4) return nullptr;
        my reverse_mapped_depth++;
        Ty const* ret = reverse_mapped_array_like(me, source, mapped, operand);
        my reverse_mapped_depth--;
        return ret;
    }
    free(source_props);
    bool has_string_index = false;
    Members const* members = ty_members(me, source);
    for (u32 i = 0; i < members->index_count; i++) has_string_index |= (members->index[i].key->flags & TYF_STRING) != 0;
    if (!source_prop_count && !has_string_index) return nullptr;
    // An object: a type whose members are the sources, each property's type inferred when it is asked for
    ReverseMapped* reverse = arena_new(&my arena, ReverseMapped);
    *reverse = (ReverseMapped){ .source = source, .mapped = mapped, .operand = operand };
    return ty_reverse_mapped_object(me, reverse);
}

// tsc's inferToMappedType: `{ [K in keyof T]: X }` infers T by reverse mapping the source, `{ [P in K]: X }` infers
// the keys of the source to K and its property types to X. False if the constraint says nothing to infer to
static bool infer_to_mapped(Checker* me, Inference* inf, Ty const* source, MappedInfo const* mapped, Ty const* constraint) {
    if (constraint->kind == TY_UNION || constraint->kind == TY_INTERSECTION) {
        bool any = false;
        for (u32 i = 0; i < constraint->composite.types.len; i++) {
            any |= infer_to_mapped(me, inf, source, mapped, constraint->composite.types.data[i]);
        }
        return any;
    }
    if (constraint->kind == TY_KEYOF) {
        InferParam* param = inference_param(inf, constraint->keyof.operand);
        if (param && !param->fixed) {
            Ty const* reversed = reverse_mapped_type(me, source, mapped, constraint->keyof.operand);
            if (reversed) {
                // (What a partly non-inferrable source says is only a partial answer)
                infer_with_priority(me, inf, reversed, constraint->keyof.operand, source->flags & TYF_NON_INFERRABLE ?
                    INFER_PRIORITY_PARTIAL_HOMOMORPHIC_MAPPED_TYPE : INFER_PRIORITY_HOMOMORPHIC_MAPPED_TYPE);
            }
        }
        return true;
    }
    if (constraint->kind == TY_TYPE_PARAM && inference_param(inf, constraint)) {
        infer_with_priority(me, inf, ty_keyof(me, source), constraint, INFER_PRIORITY_MAPPED_TYPE_CONSTRAINT);
        // K's own constraint, `K extends keyof T`, makes it the homomorphic mapped type over T (Pick<T, K>)
        Ty const* extended = ty_param_constraint(me, constraint);
        if (extended && extended != constraint && infer_to_mapped(me, inf, source, mapped, extended)) return true;
        Members const* members = ty_members(me, source);
        u32 count = members->prop_count + members->index_count;
        if (!count) return true;
        Ty const** types = malloc(sizeof(Ty const*) * count);
        panic_if(!types, "out of memory");
        for (u32 i = 0; i < members->prop_count; i++) types[i] = prop_type(me, members->props[i]);
        for (u32 i = 0; i < members->index_count; i++) types[members->prop_count + i] = members->index[i].value;
        infer_from(me, inf, ty_union(me, types, count, nullptr, (TyList){0}), ty_mapped_template(me, mapped));
        free(types);
        return true;
    }
    return false;
}

// tsc's isObjectTypeWithInferableIndex, as far as tttt can tell: anonymous object types (object and type literals,
// mapped types) without signatures, and intersections of them
// tsc's isApplicableIndexType: whether an index signature keyed by `target` applies to keys of type `source`
static bool index_key_applies(Checker* me, Ty const* source, Ty const* target) {
    if (ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE)) return true;
    if (target == ty_intrinsic(me, TY_STRING)) return ty_is_related(me, source, ty_intrinsic(me, TY_NUMBER), TY_RELATION_ASSIGNABLE);
    if (target == ty_intrinsic(me, TY_NUMBER)) {
        return ty_is_numeric_string_type(me, source) ||
            ((source->flags & TYF_STRING_LITERAL) && source->literal.string && ty_is_numeric_name(source->literal.string));
    }
    return false;
}

static bool inferable_index(Checker* me, Ty const* ty, u32 depth) {
    if (ty->kind == TY_INTERSECTION && depth < 4) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!inferable_index(me, ty->composite.types.data[i], depth + 1)) return false;
        }
        return true;
    }
    // (Namespaces and enum objects too: tsc's isObjectTypeWithInferableIndex)
    if (ty->kind == TY_DECLARED) {
        if (ty->declared.kind == TY_DECLARED_ENUM_OBJECT) return true;
        if (ty->declared.kind != TY_DECLARED_NAMESPACE) return false;
        // (Not merged with a function or class, nor an expando function: those have signatures)
    } else if (ty->kind != TY_ANONYMOUS) {
        return false;
    }
    Members const* members = ty_members(me, ty);
    return !members->call_count && !members->construct_count;
}

static Ty const* slice_element_type(Checker* me, TupleShape const* shape, TyList args, u32 from, u32 end_skip);
static Ty const* tuple_elements(Checker* me, TupleShape const* shape, TyList args, u32 from, u32 end_skip);

// Elements `from` to `arity - end_skip` of a tuple as a mutable tuple (tsc's sliceTupleType); past the leading fixed
// elements an array of what the elements from there on can be (getRestArrayTypeOfTupleType), the empty tuple if none
Ty const* ty_slice_tuple(Checker* me, TupleShape const* shape, TyList args, u32 from, u32 end_skip) {
    u32 fixed = 0;
    while (fixed < shape->arity && !(shape->flags[fixed] & TUPLE_ELEMENT_REST)) fixed++;
    if (from > fixed) {
        Ty const* rest = slice_element_type(me, shape, args, fixed, 0);
        return rest ? ty_array(me, rest, false) : ty_tuple_target(me, &(TupleShape){0});
    }
    return tuple_elements(me, shape, args, from, end_skip);
}

// Elements `from` to `arity - end_skip` of a tuple as a mutable tuple, whatever they are (tsc's createTupleType of a
// slice of the element types and flags)
static Ty const* tuple_elements(Checker* me, TupleShape const* shape, TyList args, u32 from, u32 end_skip) {
    u32 arity = shape->arity - from - end_skip;
    TupleShape slice = {
        .arity = arity,
        .flags = shape->flags + from,
        .names = shape->names ? shape->names + from : nullptr,
        .labels = shape->labels ? shape->labels + from : nullptr,
    };
    Ty const* target = ty_tuple_target(me, &slice);
    return arity ? ty_reference(me, target, (TyList){ .data = args.data + from, .len = arity }) : target;
}

// The union of the element types of a slice of a tuple (tsc's getElementTypeOfSliceOfTupleType), null if it's empty
static Ty const* slice_element_type(Checker* me, TupleShape const* shape, TyList args, u32 from, u32 end_skip) {
    TyBuf parts = {0};
    for (u32 i = from; i + end_skip < shape->arity; i++) {
        ty_buf_push(&parts, shape->flags[i] & TUPLE_ELEMENT_REST ? ty_rest_element_type(me, args.data[i]) : args.data[i]);
    }
    Ty const* ret = parts.len ? ty_union(me, parts.data, parts.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&parts);
    return ret;
}

// Leading elements before the first rest one, trailing ones after the last
static u32 tuple_fixed_length(TupleShape const* shape) {
    u32 n = 0;
    while (n < shape->arity && !(shape->flags[n] & TUPLE_ELEMENT_REST)) n++;
    return n;
}

static u32 tuple_end_fixed_count(TupleShape const* shape) {
    u32 n = 0;
    while (n < shape->arity && !(shape->flags[shape->arity - 1 - n] & TUPLE_ELEMENT_REST)) n++;
    return n;
}

// A rest element of a type variable (`...T`, tsc's ElementFlags.Variadic) rather than of an array type
static bool is_variadic_element(Checker* me, TupleShape const* shape, TyList args, u32 i) {
    return (shape->flags[i] & TUPLE_ELEMENT_REST) && !ty_is_array(me, args.data[i]) && !ty_is_tuple(args.data[i]);
}

// tsc's tupleTypesDefinitelyUnrelated: tuples of arities that can't match, nothing to infer from their elements. `[A,
// B]` against `[T]` (AsArray<T>'s false branch) infers nothing to T. (tsc's minLength counts the required and the
// variadic elements, its fixedLength the elements before the first rest or variadic one)
static bool tuples_definitely_unrelated(Checker* me, Ty const* source, Ty const* target) {
    TupleShape const* source_shape = ty_is_tuple(source) ? source->reference.target->declared.tuple : source->declared.tuple;
    TupleShape const* target_shape = target->reference.target->declared.tuple;
    TyList source_args = ty_is_tuple(source) ? ty_reference_args(me, source) : (TyList){0};
    TyList target_args = ty_reference_args(me, target);
    u32 source_min = 0, target_min = 0;
    bool target_variadic = false;
    for (u32 i = 0; i < source_shape->arity; i++) {
        source_min += !(source_shape->flags[i] & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST)) ||
            is_variadic_element(me, source_shape, source_args, i);
    }
    for (u32 i = 0; i < target_shape->arity; i++) {
        bool variadic = is_variadic_element(me, target_shape, target_args, i);
        target_variadic |= variadic;
        target_min += !(target_shape->flags[i] & (TUPLE_ELEMENT_OPTIONAL | TUPLE_ELEMENT_REST)) || variadic;
    }
    if (!target_variadic && target_min > source_min) return true;
    bool target_variable = tuple_fixed_length(target_shape) < target_shape->arity;
    bool source_variable = tuple_fixed_length(source_shape) < source_shape->arity;
    return !target_variable && (source_variable || tuple_fixed_length(target_shape) < tuple_fixed_length(source_shape));
}

// An array or tuple against a tuple (tsc's inferFromObjectTypes): the fixed elements at the start and at the end pair
// up, what's between goes to the variadic or rest element in the middle of the target
static void infer_to_tuple(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    TupleShape const* target_shape = target->reference.target->declared.tuple;
    TyList target_args = ty_reference_args(me, target);
    u32 target_arity = target_shape->arity;
    bool source_is_tuple = ty_is_tuple(source) || is_empty_tuple(source);
    TupleShape const* source_shape = !source_is_tuple ? nullptr :
        ty_is_tuple(source) ? source->reference.target->declared.tuple : source->declared.tuple;
    TyList source_args = ty_is_tuple(source) ? ty_reference_args(me, source) : (TyList){0};
    u32 source_arity = source_shape ? source_shape->arity : 0;
    // The same structure: element by element (tsc's isTupleTypeStructureMatching)
    if (source_shape && source_arity == target_arity) {
        bool matching = true;
        for (u32 i = 0; i < target_arity && matching; i++) {
            matching = (source_shape->flags[i] & TUPLE_ELEMENT_REST) == (target_shape->flags[i] & TUPLE_ELEMENT_REST);
        }
        if (matching) {
            for (u32 i = 0; i < target_arity; i++) infer_from(me, inf, source_args.data[i], target_args.data[i]);
            return;
        }
    }
    bool target_variable = tuple_fixed_length(target_shape) < target_arity;
    u32 start = source_shape ? tuple_fixed_length(source_shape) : 0;
    if (tuple_fixed_length(target_shape) < start) start = tuple_fixed_length(target_shape);
    u32 end = 0;
    if (source_shape && target_variable) {
        end = tuple_end_fixed_count(source_shape);
        if (tuple_end_fixed_count(target_shape) < end) end = tuple_end_fixed_count(target_shape);
    }
    // (A source shorter than the target's fixed parts: what there is pairs up)
    if (start + end > source_arity) end = source_arity - start;
    if (start + end > target_arity) end = target_arity - start;
    for (u32 i = 0; i < start; i++) infer_from(me, inf, source_args.data[i], target_args.data[i]);
    if (!source_shape || (source_arity - start - end == 1 && (source_shape->flags[start] & TUPLE_ELEMENT_REST))) {
        // A single rest element left in the source goes to every element in the middle of the target
        Ty const* rest = source_shape ? ty_rest_element_type(me, source_args.data[start]) : ty_rest_element_type(me, source);
        for (u32 i = start; i + end < target_arity; i++) {
            infer_from(me, inf, target_shape->flags[i] & TUPLE_ELEMENT_REST ? ty_array(me, rest, false) : rest, target_args.data[i]);
        }
    } else if (target_arity - start - end == 1 && is_variadic_element(me, target_shape, target_args, start)) {
        // `[...infer U, infer V]`: the slice of the source between the fixed parts
        infer_from(me, inf, ty_slice_tuple(me, source_shape, source_args, start, end), target_args.data[start]);
    } else if (target_arity - start - end == 1 && (target_shape->flags[start] & TUPLE_ELEMENT_REST)) {
        Ty const* rest = slice_element_type(me, source_shape, source_args, start, end);
        if (rest) infer_from(me, inf, ty_array(me, rest, false), target_args.data[start]);
    } else if (target_arity - start - end == 0) {
        // Only fixed elements: pair up what the source has in the middle too (arity mismatches, optional elements)
        for (u32 i = start; i + end < target_arity && i + end < source_arity; i++) {
            if (source_shape->flags[i] & TUPLE_ELEMENT_REST) break;
            infer_from(me, inf, source_args.data[i], target_args.data[i]);
        }
    } else if (source_shape && target_arity - start - end == 2 && is_variadic_element(me, target_shape, target_args, start) &&
        is_variadic_element(me, target_shape, target_args, start + 1)
    ) {
        // `[...T, ...U]` in the middle: split by the number of arguments T's rest parameter got (tsc's impliedArity)
        InferParam const* first = inference_param(inf, target_args.data[start]);
        u32 implied = first && first->implied_arity_plus_one ? first->implied_arity_plus_one - 1 : UINT32_MAX;
        // (The first slice skips `end + source_arity - implied` at the end, as tsc's does. Like its slice(), the bounds
        // are clamped: more arguments than the source has elements is all of them for T, and for U whatever the
        // source's rest element has left, sliceTupleType past the fixed elements)
        if (implied != UINT32_MAX) {
            i64 first_skip = (i64)end + (i64)source_arity - (i64)implied;
            if (first_skip < 0) first_skip = 0;
            if ((i64)start + first_skip > (i64)source_arity) first_skip = (i64)source_arity - start;
            u32 second_start = start + implied < source_arity - end ? start + implied : source_arity - end;
            u32 source_fixed = tuple_fixed_length(source_shape);
            if (start + implied > source_fixed && start + implied > second_start) second_start = start + implied;
            infer_from(me, inf, ty_slice_tuple(me, source_shape, source_args, start, (u32)first_skip), target_args.data[start]);
            infer_from(me, inf, ty_slice_tuple(me, source_shape, source_args, second_start, end), target_args.data[start + 1]);
        }
    } else if (source_shape && target_arity - start - end == 2 && (target_shape->flags[start] & TUPLE_ELEMENT_REST) &&
        (target_shape->flags[start + 1] & TUPLE_ELEMENT_REST)
    ) {
        // `[...T, ...rest]` / `[...rest, ...T]` in the middle: T takes as many elements as its constraint, a tuple
        // without rest elements, has (tsc's inferFromObjectTypes)
        bool first_variadic = is_variadic_element(me, target_shape, target_args, start);
        u32 at = first_variadic ? start : start + 1;
        InferParam const* param = first_variadic != is_variadic_element(me, target_shape, target_args, start + 1) ?
            inference_param(inf, target_args.data[at]) : nullptr;
        Ty const* constraint = param ? ty_base_constraint(me, target_args.data[at]) : nullptr;
        TupleShape const* constraint_shape = constraint && ty_is_tuple(constraint) ? constraint->reference.target->declared.tuple : nullptr;
        if (constraint_shape && tuple_fixed_length(constraint_shape) == constraint_shape->arity) {
            u32 implied = constraint_shape->arity;
            if (first_variadic) {
                i64 skip = (i64)source_arity - (i64)start - (i64)implied;
                if (skip < 0) skip = 0;
                if ((i64)start + skip > (i64)source_arity) skip = (i64)source_arity - start;
                infer_from(me, inf, ty_slice_tuple(me, source_shape, source_args, start, (u32)skip), target_args.data[start]);
                Ty const* rest = start + implied + end <= source_arity ?
                    slice_element_type(me, source_shape, source_args, start + implied, end) : nullptr;
                if (rest) infer_from(me, inf, ty_array(me, rest, false), target_args.data[start + 1]);
            } else if (source_arity >= end + implied + start) {
                u32 end_index = source_arity - tuple_end_fixed_count(target_shape);
                u32 start_index = end_index - implied;
                Ty const* rest = slice_element_type(me, source_shape, source_args, start, end + implied);
                if (rest) infer_from(me, inf, ty_array(me, rest, false), target_args.data[start]);
                infer_from(me, inf, tuple_elements(me, source_shape, source_args, start_index, source_arity - end_index),
                    target_args.data[start + 1]);
            }
        }
    }
    for (u32 i = 0; i < end; i++) {
        infer_from(me, inf, source_args.data[source_arity - 1 - i], target_args.data[target_arity - 1 - i]);
    }
}

// tsc's getUnmatchedProperty, optional properties not required: a required property of `target` that `source` doesn't
// have, or with `discriminants` one of a unit type that `source` has another type for
// Whether `target` has a required property `source` lacks (or, with `discriminants`, a unit-typed one whose type the
// source's doesn't have): tsc's getUnmatchedProperty. An intersection source's properties are its members' combined
// (tsc's getPropertyOfType). The source's property type is only read for a unit-typed target property: reading it
// otherwise can be circular (`var crypto: typeof globalThis extends { crypto: infer T } ? ...`)
static bool unmatched_prop_of(Checker* me, Ty const* source, Atom const* name, Ty const* target_type, bool discriminants) {
    Prop const* prop = nullptr;
    if (source->kind == TY_INTERSECTION) {
        if (!ty_find_prop(me, source, name).found) return true;
    } else {
        prop = ty_get_object_prop(me, source, name);
        if (!prop) return true;
    }
    if (!discriminants) return false;
    bool unit = (target_type->flags & (TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL | TYF_NULLABLE)) && target_type->kind != TY_UNION;
    if (!unit) return false;
    Ty const* source_type = prop ? prop_type(me, prop) : ty_find_prop(me, source, name).type;
    return !(source_type->flags & TYF_ANY) && ty_regular(me, source_type) != ty_regular(me, target_type);
}

static bool has_unmatched_prop(Checker* me, Ty const* source, Ty const* target, bool discriminants) {
    if (target->kind == TY_INTERSECTION) {
        // (An intersection's properties are all of its members', optional only where every member's is: tsc's
        // getPropertiesOfType. `{ id: number } & Reference<E>` has the `id` ScalarReference lacks)
        for (u32 m = 0; m < target->composite.types.len; m++) {
            Ty const* part = ty_apparent_type(me, target->composite.types.data[m]);
            if (!(part->flags & TYF_OBJECT)) continue;
            Members const* members = ty_members(me, part);
            for (u32 i = 0; i < members->prop_count; i++) {
                // (Required in one member is required in the intersection: its other members' copies are seen too)
                Prop const* prop = members->props[i];
                if (prop->flags & PROP_OPTIONAL) continue;
                Ty const* target_type = discriminants ? ty_find_prop(me, target, prop->name).type : nullptr;
                if (unmatched_prop_of(me, source, prop->name, target_type, discriminants)) return true;
            }
        }
        return false;
    }
    Members const* members = ty_members(me, target);
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* target_prop = members->props[i];
        if (target_prop->flags & PROP_OPTIONAL) continue;
        Ty const* target_type = discriminants ? prop_type(me, target_prop) : nullptr;
        if (unmatched_prop_of(me, source, target_prop->name, target_type, discriminants)) return true;
    }
    return false;
}

// tsc's typesDefinitelyUnrelated: each has a property the other lacks (or a discriminant it disagrees on), so there's
// nothing to infer from their members (ZodOptional<X> against ZodObject<infer S>: no walk through all their methods)
static bool types_definitely_unrelated(Checker* me, Ty const* source, Ty const* target) {
    return has_unmatched_prop(me, source, target, true) && has_unmatched_prop(me, target, source, false);
}

// Two references to the same generic type: by type argument, with its variances
static void infer_from_type_args(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    TyList source_args = ty_reference_args(me, source);
    TyList target_args = ty_reference_args(me, target);
    for (u32 i = 0; i < source_args.len && i < target_args.len; i++) {
        if (!(target_args.data[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) continue;
        bool flip = ty_type_param_is_contravariant(me, target->reference.target, i);
        if (flip) inf->contravariant = !inf->contravariant;
        infer_from(me, inf, source_args.data[i], target_args.data[i]);
        if (flip) inf->contravariant = !inf->contravariant;
    }
}

static void infer_from_signatures_and_indexes(Checker* me, Inference* inf, Ty const* source, Members const* target_members, u32 index_priority);

static void infer_from_objects(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    if ((ty_is_tuple(source) || is_empty_tuple(source) || ty_is_array(me, source)) && ty_is_tuple(target)) {
        if ((ty_is_tuple(source) || is_empty_tuple(source)) && tuples_definitely_unrelated(me, source, target)) return;
        // (An array against a readonly tuple: the tuple's elements aren't in the array, the array's mutators aren't in
        // the tuple, tsc's typesDefinitelyUnrelated. `(string | number)[]` says nothing about `readonly [K, V]`)
        if (ty_is_array(me, source) && types_definitely_unrelated(me, source, target)) return;
        infer_to_tuple(me, inf, source, target);
        return;
    }
    if (source->kind == TY_REFERENCE && target->kind == TY_REFERENCE && source->reference.target == target->reference.target) {
        infer_from_type_args(me, inf, source, target);
        return;
    }
    if (ty_is_array(me, target) && (ty_is_tuple(source) || ty_is_array(me, source))) {
        // Arrays and tuples against an array (mutable or not): by element, without a walk through every Array method
        Ty const* element = ty_reference_args(me, target).data[0];
        if (ty_is_array(me, source)) {
            infer_from(me, inf, ty_reference_args(me, source).data[0], element);
        } else if (types_definitely_unrelated(me, source, target)) {
            // (A readonly tuple against a mutable array: the array's mutators aren't in the tuple, its elements aren't
            // in the array, tsc's typesDefinitelyUnrelated before inferFromIndexTypes: nothing inferred)
        } else {
            // From the union of its elements, one candidate (tsc gets it from the tuple's number index type): `["a", 1]`
            // against `readonly T[]` infers `"a" | 1`, not two candidates of which the first wins
            TupleShape const* shape = source->reference.target->declared.tuple;
            TyList source_args = ty_reference_args(me, source);
            Ty const** elements = malloc(sizeof(Ty const*) * (source_args.len ? source_args.len : 1));
            panic_if(!elements, "out of memory");
            for (u32 i = 0; i < source_args.len; i++) {
                Ty const* arg = source_args.data[i];
                elements[i] = shape->flags[i] & TUPLE_ELEMENT_REST ? ty_rest_element_type(me, arg) : arg;
            }
            infer_from(me, inf, ty_union(me, elements, source_args.len, nullptr, (TyList){0}), element);
            free(elements);
        }
        return;
    }
    // (`[]` is an array of never: `[] extends Array<infer E>` infers never)
    if (is_empty_tuple(source) && ty_is_array(me, target)) {
        infer_from(me, inf, ty_intrinsic(me, TY_NEVER), ty_reference_args(me, target).data[0]);
        return;
    }

    Members const* target_members = ty_members(me, target);
    Members const* source_mapped_members = source->kind == TY_ANONYMOUS ? ty_members(me, source) : nullptr;
    if (target_members->mapped && source_mapped_members && source_mapped_members->mapped &&
        (source->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)
    ) {
        // Two generic mapped types `{ [P in S]: X }` and `{ [P in T]: Y }`: S to T and X to Y (tsc's
        // inferFromObjectTypes). What tsc infers to the target like to any mapped type after that is of a lower
        // priority, and the shortcuts below aren't
        MappedInfo const* source_mapped = source_mapped_members->mapped;
        MappedInfo const* target_mapped = target_members->mapped;
        infer_from(me, inf, source_mapped->constraint, target_mapped->constraint);
        infer_from(me, inf, ty_mapped_template_fresh(me, source_mapped), ty_mapped_template(me, target_mapped));
        if (source_mapped->name_type && target_mapped->name_type) {
            infer_from(me, inf, source_mapped->name_type, target_mapped->name_type);
        }
        return;
    }
    // (tsc's inferFromObjectTypes: a mapped target infers through its constraint, else from its properties like any)
    if (target_members->mapped && !target_members->mapped->name_type &&
        infer_to_mapped(me, inf, source, target_members->mapped, target_members->mapped->constraint)
    ) {
        return;
    }
    if (!(ty_is_tuple(source) && ty_is_tuple(target)) && types_definitely_unrelated(me, source, target)) return;
    for (u32 i = 0; i < target_members->prop_count; i++) {
        Prop const* target_prop = target_members->props[i];
        FoundProp source_prop = ty_find_prop(me, source, target_prop->name);
        if (!source_prop.found) continue;
        // An optional property's type has its undefined, except under exactOptionalPropertyTypes (tsc's
        // inferFromProperties: removeMissingType of getTypeOfSymbol): `{ a: undefined }` to `{ a?: T }` matches
        // undefined with undefined and infers nothing for T
        Ty const* source_type = ty_remove_missing(me, source_prop.type, source_prop.flags & PROP_OPTIONAL);
        Ty const* target_type = ty_remove_missing(me, prop_type(me, target_prop), target_prop->flags & PROP_OPTIONAL);
        infer_from(me, inf, source_type, target_type);
    }
    // (Between two mapped types, index signatures are much like a homomorphic mapped type's variable: tsc's
    // inferFromIndexTypes infers at HomomorphicMappedType priority then, below a naked type parameter beside it:
    // `{ variable: F }` from KeyValuePair<"variable", F> against KeyValuePair<any, S> = `{ [x: string]: S } | S` infers
    // the whole source for S, not F, veto-party/baum)
    bool both_mapped = source->kind == TY_ANONYMOUS && source->anonymous.mapped && target->kind == TY_ANONYMOUS && target->anonymous.mapped;
    infer_from_signatures_and_indexes(me, inf, source, target_members, both_mapped ? INFER_PRIORITY_HOMOMORPHIC_MAPPED_TYPE : 0);
}

// The signatures and index signatures of infer_from_objects() (tsc's inferFromSignatures and inferFromIndexTypes)
static void infer_from_signatures_and_indexes(Checker* me, Inference* inf, Ty const* source, Members const* target_members, u32 index_priority) {
    Members const* source_members = ty_members(me, source);
    // (An intersection's are its members', see resolve_intersection_members())
    infer_from_signature_lists(
        me, inf, source_members->call, source_members->call_count, target_members->call, target_members->call_count
    );
    infer_from_signature_lists(
        me, inf, source_members->construct, source_members->construct_count,
        target_members->construct, target_members->construct_count
    );
    // (tsc's inferFromIndexTypes)
    bool inferable = inferable_index(me, source, 0);
    for (u32 i = 0; i < target_members->index_count && inferable; i++) {
        IndexInfo const* target_info = &target_members->index[i];
        // An object literal or type literal (or an intersection of them) against `{ [key: string]: T }`: the
        // union of the types of its properties whose keys fit, an optional one's without undefined (tsc's
        // isObjectTypeWithInferableIndex, inferFromIndexTypes)
        TyBuf types = {0};
        TyBuf seen = {0};
        TyList parts = source->kind == TY_INTERSECTION ? source->composite.types : (TyList){ .data = &source, .len = 1 };
        for (u32 p = 0; p < parts.len; p++) {
            Members const* part_members = ty_members(me, parts.data[p]);
            for (u32 j = 0; j < part_members->prop_count; j++) {
                Prop const* prop = part_members->props[j];
                Ty const* key = ty_prop_name_literal(me, prop->name);
                bool applies = target_info->key->flags & TYF_STRING ? !(key->flags & TYF_ES_SYMBOL_LIKE) :
                    ty_specific_index_applies(me, target_info, key);
                bool again = false;
                for (u32 k = 0; k < seen.len && !again; k++) again = seen.data[k] == key;
                if (!applies || again) continue;
                ty_buf_push(&seen, key);
                FoundProp found_prop = parts.len > 1 ? ty_find_prop(me, source, prop->name) :
                    (FoundProp){ .found = true, .type = prop_type(me, prop), .flags = prop->flags };
                if (!found_prop.found) continue;
                // (tsc's removeMissingOrUndefinedType of an optional one)
                Ty const* type = found_prop.type;
                if (found_prop.flags & PROP_OPTIONAL) {
                    type = compiler_options()->exact_optional_property_types ? ty_remove_missing(me, type, true) : ty_without_undefined(me, type);
                }
                ty_buf_push(&types, type);
            }
        }
        // and those of its index signatures that apply to the key
        for (u32 j = 0; j < source_members->index_count; j++) {
            if (index_key_applies(me, source_members->index[j].key, target_info->key)) {
                ty_buf_push(&types, source_members->index[j].value);
            }
        }
        if (types.len) {
            Ty const* union_type = ty_union(me, types.data, types.len, nullptr, (TyList){0});
            if (index_priority) infer_with_priority(me, inf, union_type, target_info->value, index_priority);
            else infer_from(me, inf, union_type, target_info->value);
        }
        ty_buf_drop(&types);
        ty_buf_drop(&seen);
    }
    // The source's index signature that applies to each of the target's keys: a string one to a number key too
    for (u32 i = 0; i < target_members->index_count; i++) {
        IndexInfo const* target_info = &target_members->index[i];
        IndexInfo const* source_info = ty_applicable_index_info(me, source, target_info->key);
        if (!source_info) continue;
        if (index_priority) infer_with_priority(me, inf, source_info->value, target_info->value, index_priority);
        else infer_from(me, inf, source_info->value, target_info->value);
    }
}

// tsc's isTypeIdenticalTo, pointers first (structured types of the same kind can be identical without being the same)
static bool is_identical(Checker* me, Ty const* source, Ty const* target) {
    // (A fresh literal is its regular one: tsc's isTypeRelatedTo, `"skip"` returned to `T | "skip"`)
    source = ty_regular(me, source);
    target = ty_regular(me, target);
    if (source == target) return true;
    if (source->kind == TY_INTRINSIC || source->kind == TY_LITERAL || source->kind == TY_TYPE_PARAM) return false;
    // (An interface and a type literal can be identical: tsc's TypeFlags.Object covers both)
    if (source->kind != target->kind && !(source->flags & target->flags & TYF_OBJECT)) return false;
    return ty_is_related(me, source, target, TY_RELATION_IDENTITY);
}

static bool is_identical_or_base(Checker* me, Ty const* source, Ty const* target) {
    // (missing is only matched by itself, but matches undefined: tsc's isTypeOrBaseIdenticalTo)
    Ty const* missing = ty_intrinsic(me, TY_MISSING);
    if (target == missing) return source == target;
    if (source == missing) return target == ty_intrinsic(me, TY_UNDEFINED);
    return is_identical(me, source, target) ||
        ((target->flags & TYF_STRING) && (source->flags & TYF_STRING_LITERAL)) ||
        ((target->flags & TYF_NUMBER) && (source->flags & TYF_NUMBER_LITERAL));
}

// The declaration an anonymous object type (or an instantiation of one) is the type of: a type literal, mapped type or
// function type node, or a function (tsc's type.symbol), null if none
static void const* anonymous_declaration(Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || ty->anonymous.applies_type_args) return nullptr;
    Ty const* root = ty->anonymous.target ? ty->anonymous.target : ty;
    if (root->kind != TY_ANONYMOUS || root->anonymous.applies_type_args) return nullptr;
    if (root->anonymous.node) return root->anonymous.node;
    // (An object literal's: its symbol, which every instantiation and widening keeps, a spread's result too)
    if (ty->anonymous.object_literal && !ty->anonymous.overload_count && ty->anonymous.literal_expr) return ty->anonymous.literal_expr;
    if (root->anonymous.object_literal && !root->anonymous.overload_count && root->anonymous.literal_expr) return root->anonymous.literal_expr;
    return root->anonymous.function;
}

static bool is_closely_matched(Ty const* source, Ty const* target) {
    // (A tuple's target type has no symbol, so tuples of one arity aren't: `[{ ... }]` to `P | [boolean]` goes to P as
    // directly as to [boolean], tsc's s.symbol === t.symbol; unified's `use(plugin, options)`)
    if (source->kind == TY_REFERENCE && target->kind == TY_REFERENCE && source->reference.target == target->reference.target &&
        !ty_tuple_shape(source)
    ) {
        return true;
    }
    // (Instantiations of one type literal: tsc's isTypeCloselyMatchedBy, s.symbol === t.symbol)
    void const* declaration = anonymous_declaration(source);
    if (declaration && declaration == anonymous_declaration(target)) return true;
    return source->alias && target->alias_args.len && source->alias == target->alias;
}

// tsgo's getTypeDepth: one more than the deepest type argument of a generic alias or reference, a union's or
// intersection's deepest member's, else 0, up to `max`
static u32 type_depth(Checker* me, Ty const* ty, u32 max) {
    if (!max) return 0;
    TyList list = {0};
    if (ty->alias && ty->alias_args.len) list = ty->alias_args;
    else if (ty->kind == TY_REFERENCE) list = ty_reference_args(me, ty);
    else if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        u32 depth = 0;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            u32 d = type_depth(me, ty->composite.types.data[i], max);
            if (d > depth) depth = d;
        }
        return depth;
    }
    if (!list.len) return 0;
    u32 depth = 0;
    for (u32 i = 0; i < list.len; i++) {
        u32 d = type_depth(me, list.data[i], max - 1);
        if (d > depth) depth = d;
    }
    return depth + 1;
}

static void infer_to_union(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    TyList sources = source->kind == TY_UNION ? source->composite.types : (TyList){ .data = &source, .len = 1 };
    TyList targets = target->composite.types;
    bool* source_done = calloc(sources.len + targets.len, sizeof(bool));
    panic_if(!source_done, "out of memory");
    bool* target_done = source_done + sources.len;

    // Identical members are inferred between and cancel out, then closely matched ones (tsc's inferFromMatchingTypes:
    // identical isn't the same, `IK<number>` is identical to `IK<T>` when the alias doesn't use T)
    for (u32 t = 0; t < targets.len; t++) {
        if (target_done[t]) continue;
        for (u32 s = 0; s < sources.len; s++) {
            if (!is_identical_or_base(me, sources.data[s], targets.data[t])) continue;
            infer_from(me, inf, sources.data[s], targets.data[t]);
            source_done[s] = true;
            target_done[t] = true;
        }
    }
    // The closely matched targets by decreasing depth of type arguments, then in union order (tsgo 7.1's
    // compareTypesAndDepth: string[][] to T[][] infers better than to T[]), each from every closely matched source
    {
        u32* order = malloc(sizeof(u32) * (targets.len ? targets.len : 1));
        u32* depths = malloc(sizeof(u32) * (targets.len ? targets.len : 1));
        panic_if(!order || !depths, "out of memory");
        u32 matched = 0;
        for (u32 t = 0; t < targets.len; t++) {
            if (target_done[t]) continue;
            for (u32 s = 0; s < sources.len; s++) {
                if (source_done[s] || !is_closely_matched(sources.data[s], targets.data[t])) continue;
                depths[matched] = type_depth(me, targets.data[t], 3);
                order[matched++] = t;
                break;
            }
        }
        for (u32 i = 1; i < matched; i++) { // (Stable insertion sort: few members)
            u32 t = order[i], d = depths[i], j = i;
            for (; j > 0 && depths[j - 1] < d; j--) {
                order[j] = order[j - 1];
                depths[j] = depths[j - 1];
            }
            order[j] = t;
            depths[j] = d;
        }
        bool* matched_source = calloc(sources.len ? sources.len : 1, sizeof(bool));
        panic_if(!matched_source, "out of memory");
        for (u32 i = 0; i < matched; i++) {
            u32 t = order[i];
            for (u32 s = 0; s < sources.len; s++) {
                if (source_done[s] || !is_closely_matched(sources.data[s], targets.data[t])) continue;
                infer_from(me, inf, sources.data[s], targets.data[t]);
                matched_source[s] = true;
            }
            target_done[t] = true;
        }
        for (u32 s = 0; s < sources.len; s++) source_done[s] |= matched_source[s];
        free(matched_source);
        free(order);
        free(depths);
    }
    u32 sources_left = 0, targets_left = 0;
    for (u32 s = 0; s < sources.len; s++) sources_left += !source_done[s];
    for (u32 t = 0; t < targets.len; t++) targets_left += !target_done[t];
    if (!targets_left) {
        free(source_done);
        return;
    }
    if (!sources_left) {
        // Everything matched: rather than nothing (and a constraint later), the whole source to what is left of the
        // target at a lower priority. `string` to `string | T` infers string for T
        Ty const** left = malloc(sizeof(Ty const*) * targets_left);
        u32 len = 0;
        for (u32 t = 0; t < targets.len; t++) {
            if (!target_done[t]) left[len++] = targets.data[t];
        }
        infer_with_priority(me, inf, source, ty_union(me, left, len, nullptr, (TyList){0}), INFER_PRIORITY_NAKED_TYPE_VARIABLE);
        free(left);
        free(source_done);
        return;
    }
    if (targets_left == 1 && (sources_left < sources.len || targets_left < targets.len)) {
        // What is left of the target is no union any more: inferred to as a whole from what is left of the source
        // (tsc's inferFromTypes goes on with both reduced: `(T & D) | null` from `A | B | null` infers A | B for T)
        Ty const** left = malloc(sizeof(Ty const*) * sources_left);
        u32 len = 0;
        for (u32 s = 0; s < sources.len; s++) {
            if (!source_done[s]) left[len++] = sources.data[s];
        }
        Ty const* rest_target = nullptr;
        for (u32 t = 0; t < targets.len; t++) {
            if (!target_done[t]) rest_target = targets.data[t];
        }
        Ty const* rest_source = ty_union(me, left, len, nullptr, (TyList){0});
        free(left);
        free(source_done);
        // (Not as a new pair: no alias match, no intersection target reduction, tsc's inferFromTypes goes on with
        // both reduced in the same call)
        if (rest_target->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) infer_from_reduced(me, inf, rest_source, rest_target);
        return;
    }

    // To what isn't a naked type parameter first, every source that is left to every such target, remembering which
    // sources that used up for the naked type parameter
    bool* used_up = calloc(sources.len, sizeof(bool));
    panic_if(!used_up, "out of memory");
    bool outer_circularity = inf->circularity;
    inf->circularity = false;
    u32 naked_count = 0;
    Ty const* naked = nullptr;
    for (u32 t = 0; t < targets.len; t++) {
        if (target_done[t]) continue;
        if (inference_param(inf, targets.data[t])) {
            naked = targets.data[t];
            naked_count++;
            continue;
        }
        for (u32 s = 0; s < sources.len; s++) {
            if (source_done[s]) continue;
            // Used up if something was inferred from it as directly as the naked type parameter would be
            u32 saved = inf->hit_priority;
            inf->hit_priority = UINT32_MAX;
            infer_from(me, inf, sources.data[s], targets.data[t]);
            if (inf->hit_priority == inf->priority) used_up[s] = true;
            if (saved < inf->hit_priority) inf->hit_priority = saved;
        }
    }
    bool circularity = inf->circularity;
    inf->circularity |= outer_circularity;
    if (!naked_count) {
        // Every target an intersection with the same single naked type variable: what is left of the source to it, at
        // a lower priority (tsc's getSingleTypeVariableFromIntersectionTypes: `A | B` to `T & (X | Y)` infers A | B)
        Ty const* variable = nullptr;
        bool single = true;
        for (u32 t = 0; t < targets.len && single; t++) {
            if (target_done[t]) continue;
            Ty const* found = nullptr;
            if (targets.data[t]->kind == TY_INTERSECTION) {
                TyList parts = targets.data[t]->composite.types;
                for (u32 i = 0; i < parts.len && !found; i++) if (inference_param(inf, parts.data[i])) found = parts.data[i];
            }
            single = found && (!variable || variable == found);
            variable = found;
        }
        if (single && variable) {
            // (tsc's inferFromTypes has remade the source union of what is left, without its alias)
            Ty const** left = malloc(sizeof(Ty const*) * sources_left);
            u32 len = 0;
            for (u32 s = 0; s < sources.len; s++) if (!source_done[s]) left[len++] = sources.data[s];
            Ty const* rest = ty_union(me, left, len, nullptr, (TyList){0});
            free(left);
            infer_with_priority(me, inf, rest, variable, INFER_PRIORITY_NAKED_TYPE_VARIABLE);
        }
    }
    if (naked_count) {
        Ty const** unmatched = malloc(sizeof(Ty const*) * sources.len);
        u32 unmatched_len = 0;
        for (u32 s = 0; s < sources.len; s++) {
            if (!source_done[s] && !used_up[s]) unmatched[unmatched_len++] = sources.data[s];
        }
        if (naked_count == 1 && !circularity && unmatched_len) {
            infer_from(me, inf, ty_union(me, unmatched, unmatched_len, nullptr, (TyList){0}), naked);
        } else {
            // (Everything matched a member that isn't a naked type parameter: the whole source to the naked one anyway,
            // less directly, tsc's inferToMultipleTypes falling through to its last loop: `() => number` to `T | (()
            // => P)` infers number for P and the function for T, framer-motion's `motion.create(Button)`, whose
            // TagName is then its constraint, not its default)
            // (A walk above was cut short, so which sources were used up isn't known: the whole source, less directly.
            // Less what matched a member above: tsc's inferFromTypes reduces the source before inferToMultipleTypes,
            // `Promise<A> | A` to `T | U | Promise<T | U>` infers A to T and U)
            // (Remade even when nothing matched: tsc's inferFromTypes sets source = getUnionType(sources) for a union
            // target, which drops an alias, `Promise<DooDad>` to `Promise<TResult1 | TResult2>` infers the union)
            Ty const* rest = source;
            if (sources_left < sources.len || (source->kind == TY_UNION && source->alias)) {
                u32 len = 0;
                for (u32 s = 0; s < sources.len; s++) {
                    if (!source_done[s]) unmatched[len++] = sources.data[s];
                }
                rest = ty_union(me, unmatched, len, nullptr, (TyList){0});
            }
            for (u32 t = 0; t < targets.len; t++) {
                if (!target_done[t] && inference_param(inf, targets.data[t])) {
                    infer_with_priority(me, inf, rest, targets.data[t], INFER_PRIORITY_NAKED_TYPE_VARIABLE);
                }
            }
        }
        free(unmatched);
    }
    free(used_up);
    free(source_done);
}

// tsc's inferFromTypes on an intersection target that isn't only non-generic objects: members identical in
// source and target are inferred between and cancel out first (`string & T` from `string & U` infers T from U, not from
// `string & U`; `X & A<T>` from `A<T>` with the inferred T itself infers T: tsc's inferFromMatchingTypes). Returns
// whether anything is left to infer between, in *source / *target
static bool reduce_intersection_target(Checker* me, Inference* inf, Ty const** source, Ty const** target) {
    TyList targets = (*target)->composite.types;
    // (Unless the target is only non-generic object types: `string[] & Iterable<T>` keeps the source's string[])
    bool reduce = false;
    for (u32 i = 0; i < targets.len && !reduce; i++) {
        Ty const* t = targets.data[i];
        bool generic_mapped = t->kind == TY_ANONYMOUS && (t->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && ty_generic_mapped(me, t);
        reduce = !(t->flags & TYF_OBJECT) || generic_mapped;
    }
    if (!reduce) return true;
    TyList sources = (*source)->kind == TY_INTERSECTION ? (*source)->composite.types : (TyList){ .data = source, .len = 1 };
    bool* source_matched = calloc(sources.len + targets.len, sizeof(bool));
    panic_if(!source_matched, "out of memory");
    bool* target_matched = source_matched + sources.len;
    for (u32 j = 0; j < targets.len; j++) {
        for (u32 i = 0; i < sources.len; i++) {
            if (!is_identical(me, sources.data[i], targets.data[j])) continue;
            infer_from(me, inf, sources.data[i], targets.data[j]);
            source_matched[i] = true;
            target_matched[j] = true;
        }
    }
    TyBuf kept_sources = {0};
    TyBuf kept_targets = {0};
    for (u32 i = 0; i < sources.len; i++) if (!source_matched[i]) ty_buf_push(&kept_sources, sources.data[i]);
    for (u32 j = 0; j < targets.len; j++) if (!target_matched[j]) ty_buf_push(&kept_targets, targets.data[j]);
    free(source_matched);
    bool left = kept_sources.len && kept_targets.len;
    if (left && kept_targets.len < targets.len) {
        *source = ty_intersection(me, kept_sources.data, kept_sources.len, nullptr, (TyList){0});
        *target = ty_intersection(me, kept_targets.data, kept_targets.len, nullptr, (TyList){0});
    } else if (left && (*source)->kind == TY_INTERSECTION && (*source)->alias) {
        // (tsc remakes the source with getIntersectionType even when nothing matched: an aliased intersection is
        // inferred unaliased, vue's `components: Comp` infers LC as DefineComponent's members, not `DefineComponent<...>`)
        *source = ty_intersection(me, kept_sources.data, kept_sources.len, nullptr, (TyList){0});
    } else if (left && !compiler_options()->strict_null_checks && ((*source)->flags & TYF_NULLABLE) &&
        ((*source)->xflags & TYX_CONTAINS_WIDENING)
    ) {
        // tsc remakes the source as an intersection, which without strictNullChecks makes a widening null or undefined
        // the plain one (getIntersectionType): `f(null)` to `T & Bear` infers null, not the any a widening null becomes
        *source = ty_intrinsic(me, ((*source)->flags & TYF_UNDEFINED) ? TY_UNDEFINED : TY_NULL);
    } else if (left && ((*source)->flags & TYF_ANY) &&
        ((*source)->alias || (((*source)->flags & TYF_ERROR) && *source != ty_intrinsic(me, TY_ERROR)))
    ) {
        // So does it make an error type named after what didn't resolve, or an any named by an alias, the plain one: an
        // unresolved `Opt` argument to `O & {}` infers any, to a bare O `Opt` (better-auth's `betterAuth(config)`)
        *source = ty_intrinsic(me, ((*source)->flags & TYF_ERROR) ? TY_ERROR : TY_ANY);
    }
    ty_buf_drop(&kept_sources);
    ty_buf_drop(&kept_targets);
    return left;
}

// tsc's isLiteralType (a unit type or a union of them) or `string`
static bool is_literal_or_string(Ty const* ty) {
    if (ty->flags & TYF_STRING) return true;
    TyList parts = ty->kind == TY_UNION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = parts.data[i];
        if (part->kind != TY_LITERAL && !(part->kind == TY_INTRINSIC && (part->flags & TYF_NULLABLE))) return false;
    }
    return true;
}

// tsc's createEmptyObjectTypeFromStringLiteral: a property of type any per string literal, a string index signature
// for `string`
static Ty const* empty_object_from_string_literals(Checker* me, Ty const* ty) {
    TyList parts = ty->kind == TY_UNION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    ObjectLiteralProp* props = calloc(parts.len ? parts.len : 1, sizeof(ObjectLiteralProp));
    panic_if(!props, "out of memory");
    u32 count = 0;
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = parts.data[i];
        if (!(part->flags & TYF_STRING_LITERAL) || part->kind != TY_LITERAL) continue;
        Atom const* name = ty_literal_name(me, part);
        if (!name) continue;
        props[count++] = (ObjectLiteralProp){ .name = name, .type = ty_intrinsic(me, TY_ANY) };
    }
    IndexInfo index = { .key = ty_intrinsic(me, TY_STRING), .value = ty_empty_object_type(me), .param_name = my atoms.x };
    bool string = (ty->flags & TYF_STRING) != 0;
    Ty const* ret = ty_object_literal(me, props, count, string ? &index : nullptr, string ? 1 : 0,
        OBJECT_SYNTHETIC | OBJECT_NO_INFERABLE_INDEX | OBJECT_NO_SYMBOL);
    free(props);
    return ret;
}

// A pair whose walk was cut short: seen again, it is circular too (tsc's invokeOnce visited status)
static void record_circular(Inference* inf, Ty const* source, Ty const* target) {
    if (inf->circular_len == inf->circular_cap) {
        inf->circular_cap = inf->circular_cap ? inf->circular_cap * 2 : 8;
        inf->circular = realloc(inf->circular, sizeof(VisitKey) * inf->circular_cap);
        panic_if(!inf->circular, "out of memory");
    }
    inf->circular[inf->circular_len++] = visited_key(inf, source, target);
}

static bool tsc_could_contain_type_variables(Ty const* ty);

// What tsc's inferFromTypes asks of the source when the target has no type variables that tttt's flag knows of but tsc's
// couldContainTypeVariables doesn't know it (tsc_could_contain_type_variables()): nothing is inferred, but the source's
// property types and signatures' return types matching the target's are evaluated, which may find a declaration in
// flight (TS7022/TS7023: `function g2() { return f2({ a: 1, cb: g2 }); }` against `cb: () => typeof obj`)
static void force_source_parts(Checker* me, Ty const* source, Ty const* target, u32 depth) {
    if (!depth || source == target) return;
    if (source->kind == TY_UNION) {
        for (u32 i = 0; i < source->composite.types.len; i++) force_source_parts(me, source->composite.types.data[i], target, depth - 1);
        return;
    }
    if (target->kind == TY_UNION || target->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < target->composite.types.len; i++) force_source_parts(me, source, target->composite.types.data[i], depth - 1);
        return;
    }
    if (!tsc_could_contain_type_variables(target)) return;
    if (!(source->flags & (TYF_OBJECT | TYF_INTERSECTION))) source = ty_apparent_type(me, source); // (`this`)
    if (!(source->flags & (TYF_OBJECT | TYF_INTERSECTION))) return;
    Members const* targets = ty_members(me, target);
    if (!targets->prop_count && !targets->call_count && !targets->construct_count && !targets->index_count) return;
    Members const* sources = ty_members(me, source);
    // An index signature takes an inferable index of every property (tsc's inferFromIndexTypes, getImplicitIndexInfo)
    if (targets->index_count && inferable_index(me, source, 0)) {
        for (u32 i = 0; i < sources->prop_count; i++) (void)ty_prop_type_of(me, source, sources->props[i]->name);
    }
    for (u32 i = 0; i < targets->prop_count; i++) {
        Prop const* prop = targets->props[i];
        if (!ty_get_prop(me, source, prop->name)) continue;
        Ty const* part = ty_prop_type_of(me, source, prop->name);
        if (part) force_source_parts(me, part, ty_prop_type_of(me, target, prop->name), depth - 1);
    }
    for (int construct = 0; construct < 2; construct++) {
        u32 source_count = construct ? sources->construct_count : sources->call_count;
        u32 target_count = construct ? targets->construct_count : targets->call_count;
        Signature* const* source_sigs = construct ? sources->construct : sources->call;
        Signature* const* target_sigs = construct ? targets->construct : targets->call;
        u32 count = source_count < target_count ? source_count : target_count;
        for (u32 i = 0; i < count; i++) {
            Signature const* source_sig = source_sigs[source_count - count + i];
            Signature const* target_sig = target_sigs[target_count - count + i];
            signature_resolve(me, source_sig);
            signature_resolve(me, target_sig);
            if (sig_res(me, source_sig)->type_params.len) continue;
            // (Only a return type that could have type variables asks for the source's: tsc's applyToReturnTypes)
            Ty const* target_return = signature_return_type(me, target_sig);
            if (!tsc_could_contain_type_variables(target_return)) continue;
            force_source_parts(me, signature_return_type(me, source_sig), target_return, depth - 1);
        }
    }
}

static void infer_from(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    if (!(target->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        // (The kinds first: most targets are neither, and this is hot)
        if (__builtin_expect(target->kind == TY_ANONYMOUS || target->kind == TY_UNION || target->kind == TY_INTERSECTION, 0) &&
            tsc_could_contain_type_variables(target)
        ) {
            force_source_parts(me, source, target, 4);
        }
        return;
    }
    if (ty_is_no_infer(target)) return;
    if (source->alias && source->alias == target->alias) {
        // Two instantiations of one type alias: by type argument, with the alias's variances (tsc). Without type
        // arguments the two are the same type, with nothing to infer
        TyList source_args = source->alias_args;
        TyList target_args = target->alias_args;
        Ty const* source_buf[MAX_TYPE_ARGS];
        Ty const* target_buf[MAX_TYPE_ARGS];
        if (source_args.len != target_args.len) {
            source_args = ty_alias_args_filled(me, source, source_buf);
            target_args = ty_alias_args_filled(me, target, target_buf);
        }
        for (u32 i = 0; i < source_args.len && i < target_args.len; i++) {
            if (!(target_args.data[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) continue;
            bool flip = ty_alias_param_is_contravariant(me, source->alias, i);
            if (flip) inf->contravariant = !inf->contravariant;
            infer_from(me, inf, source_args.data[i], target_args.data[i]);
            if (flip) inf->contravariant = !inf->contravariant;
        }
        return;
    }
    if (target->kind == TY_INTERSECTION && source->kind != TY_UNION && !reduce_intersection_target(me, inf, &source, &target)) return;
    infer_from_reduced(me, inf, source, target);
}

// The rest of tsc's inferFromTypes, once the matching members of a union or an intersection target are taken out: a
// union target's rest goes on here, without the intersection target's (an else branch in tsc)
[[gnu::always_inline]] static inline void infer_from_reduced(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    if (target->kind == TY_SUBSTITUTION || target->kind == TY_INDEXED) target = ty_actual_type_variable(me, target);
    if (target->kind == TY_TYPE_PARAM || (target->kind == TY_INDEXED && inference_param(inf, target))) {
        InferParam* param = inference_param(inf, target);
        if (!param || param->fixed || (source->flags & TYF_NON_INFERRABLE) || source == ty_intrinsic(me, TY_NON_INFERRABLE_ANY)) return;
        if (inf->priority < inf->hit_priority) inf->hit_priority = inf->priority;
        if (!param->has_priority || inf->priority < param->priority) {
            if (param->co_len || param->contra_len) inf->replaced++;
            param->co_len = 0;
            param->contra_len = 0;
            param->co_literals = 0;
            param->top_level = true;
            param->priority = inf->priority;
            param->has_priority = true;
        }
        if (inf->priority == param->priority) {
            if (inf->contravariant && !inf->bivariant) {
                candidates_push(&param->contra, &param->contra_len, source);
            } else {
                u32 before = param->co_len;
                candidates_push(&param->co, &param->co_len, source);
                if (param->co_len > before && source == inf->arg_tuple_literal && before < 64) param->co_literals |= (u64)1 << before;
            }
            if (!(inf->priority & INFER_PRIORITY_RETURN_TYPE) && param->top_level && inf->original_target &&
                !is_param_at_top_level(me, inf->original_target, target)
            ) {
                param->top_level = false;
            }
        }
        return;
    }
    if (target->kind == TY_INDEXED) {
        // To the simplified form of an indexed access too, which may expose bare type parameters: `{ [P in K]: X<P> }[K]`
        // is X<K> (tsc's inferFromTypes)
        Ty const* simplified = ty_simplified(me, target, false);
        if (simplified != target) {
            infer_from(me, inf, source, simplified);
        } else {
            // A generic index doesn't distribute when relating, but here it can: `(Readonly<T> & U)[P]` is
            // `T[P] & U[P]`, exposing the T[P] a reverse mapped type infers to (tsc's inferFromTypes)
            Ty const* index = ty_simplified(me, target->indexed.index, false);
            if (index->kind == TY_TYPE_PARAM || (index->flags & TYF_DEFERRED)) {
                Ty const* object = ty_simplified(me, target->indexed.object, false);
                Ty const* distributed = ty_distribute_index_over_object(me, object, index);
                if (distributed && distributed != target) infer_from(me, inf, source, distributed);
            }
        }
    }
    if (source == target && (source->kind == TY_UNION || source->kind == TY_INTERSECTION)) {
        // The same union or intersection: each member to itself (a generic signature related to its own instantiation
        // infers `T` from `T | undefined`)
        for (u32 i = 0; i < source->composite.types.len; i++) {
            infer_from(me, inf, source->composite.types.data[i], source->composite.types.data[i]);
        }
        return;
    }
    if (inf->depth == INFER_MAX_DEPTH) return;
    // An object pair on the stack is in `visited` too, which stops it below. Another pair is let through once more (tsc
    // has no such guard: its object pairs below stop it, invokeOnce): a union target met again, `b4`'s literal to
    // `RecBox<T> = T | Box<RecBox<T>>`, then finds its object member in progress, a circularity, and infers the
    // source to the naked T at that level too (recursiveConditionalTypes)
    if (!(target->flags & TYF_OBJECT)) {
        u32 seen = 0;
        for (u32 i = 0; i < inf->depth; i++) seen += inf->visiting_sources[i] == source && inf->visiting_targets[i] == target;
        if (seen > 1) return;
    }
    inf->visiting_sources[inf->depth] = source;
    inf->visiting_targets[inf->depth] = target;
    inf->depth++;
    if (source->kind == TY_SUBSTITUTION) {
        infer_from(me, inf, source->substitution.base, target);
        infer_with_priority(me, inf, ty_substitution_intersection(me, source), target, INFER_PRIORITY_SUBSTITUTE_SOURCE);
    } else if (target->kind == TY_CONDITIONAL) {
        // Once per pair, and not while both sides keep nesting (tsc's invokeOnce around inferToConditionalType): `any`
        // to a recursive conditional alias (`ElementsType<Tail, [...]>`) would walk every level of it
        // (A pair seen before passes on whether its walk was cut short, tsc's invokeOnce visited status)
        if (!visited_add(inf, source, target) || inf->object_depth >= INFER_MAX_DEPTH) {
            for (u32 i = 0; i < inf->object_depth && !inf->circularity; i++) {
                inf->circularity = inf->object_sources[i] == source && inf->object_targets[i] == target;
            }
            VisitKey key = visited_key(inf, source, target);
            for (u32 i = 0; i < inf->circular_len && !inf->circularity; i++) inf->circularity = visit_key_eq(inf->circular[i], key);
            inf->depth--;
            return;
        }
        bool outer_circularity = inf->circularity;
        inf->circularity = false;
        unsigned saved_expanding = inf->expanding;
        if (!(inf->expanding & 1) && ty_is_deeply_nested(me, source, inf->object_sources, inf->object_depth, 2)) inf->expanding |= 1;
        if (!(inf->expanding & 2) && ty_is_deeply_nested(me, target, inf->object_targets, inf->object_depth, 2)) inf->expanding |= 2;
        if (inf->expanding == 3) {
            // Cut short: a circularity (tsc's InferencePriority.Circularity), which keeps the sources that led here from
            // counting as used up for a naked type parameter beside it
            inf->expanding = saved_expanding;
            record_circular(inf, source, target);
            inf->circularity = true;
            inf->depth--;
            return;
        }
        inf->object_sources[inf->object_depth] = source;
        inf->object_targets[inf->object_depth] = target;
        inf->object_depth++;
        // (Before a union source is taken apart, like tsc's inferFromTypes: `E` of a union alias infers E itself)
        if (source->kind == TY_CONDITIONAL) {
            infer_from(me, inf, ty_non_distributed(source->conditional.check), target->conditional.check);
            infer_from(me, inf, ty_non_distributed(source->conditional.extends), target->conditional.extends);
            Ty const* source_true = ty_non_distributed(ty_conditional_branch(me, source, true));
            infer_from(me, inf, source_true, ty_conditional_branch(me, target, true));
            Ty const* source_false = ty_non_distributed(ty_conditional_branch(me, source, false));
            infer_from(me, inf, source_false, ty_conditional_branch(me, target, false));
        } else {
            // To both branches, the ones that aren't naked type parameters first, the naked ones less directly (tsc's
            // inferToConditionalType + inferToMultipleTypesWithPriority)
            u32 saved = inf->priority;
            if (inf->contravariant) inf->priority |= INFER_PRIORITY_CONTRAVARIANT_CONDITIONAL;
            Ty const* branches[2] = { ty_conditional_branch(me, target, true), ty_conditional_branch(me, target, false) };
            bool naked = false;
            for (u32 i = 0; i < 2; i++) {
                if (inference_param(inf, branches[i])) {
                    naked = true;
                } else {
                    infer_from(me, inf, source, branches[i]);
                }
            }
            for (u32 i = 0; naked && i < 2; i++) {
                if (inference_param(inf, branches[i])) infer_with_priority(me, inf, source, branches[i], INFER_PRIORITY_NAKED_TYPE_VARIABLE);
            }
            inf->priority = saved;
        }
        inf->object_depth--;
        inf->expanding = saved_expanding;
        if (inf->circularity) record_circular(inf, source, target);
        inf->circularity |= outer_circularity;
    } else if (target->kind == TY_UNION) {
        infer_to_union(me, inf, source, target);
    } else if (target->kind == TY_INTERSECTION) {
        // Members that aren't naked type parameters first; a single naked one (`T & {}`) is inferred to with the
        // naked type variable priority, as it is less specific. Several get nothing (tsc's inferToMultipleTypes)
        u32 naked_count = 0;
        Ty const* naked = nullptr;
        for (u32 i = 0; i < target->composite.types.len; i++) {
            Ty const* member = target->composite.types.data[i];
            if (inference_param(inf, member)) {
                naked = member;
                naked_count++;
            } else {
                infer_from(me, inf, source, member);
            }
        }
        if (naked_count == 1) infer_with_priority(me, inf, source, naked, INFER_PRIORITY_NAKED_TYPE_VARIABLE);
    } else if (target->kind == TY_KEYOF && is_literal_or_string(source)) {
        // A string literal to `keyof T`: T has that property (tsc's createEmptyObjectTypeFromStringLiteral), what
        // several positions say combined
        inf->contravariant = !inf->contravariant;
        infer_with_priority(me, inf, empty_object_from_string_literals(me, source), target->keyof.operand, INFER_PRIORITY_LITERAL_KEYOF);
        inf->contravariant = !inf->contravariant;
    } else if (source->kind == TY_UNION) {
        for (u32 i = 0; i < source->composite.types.len; i++) {
            infer_from(me, inf, source->composite.types.data[i], target);
        }
    } else if (target->kind == TY_KEYOF) {
        if (source->kind == TY_KEYOF) {
            inf->contravariant = !inf->contravariant;
            infer_from(me, inf, source->keyof.operand, target->keyof.operand);
            inf->contravariant = !inf->contravariant;
        }
    } else if (target->kind == TY_INDEXED) {
        if (source->kind == TY_INDEXED) {
            infer_from(me, inf, source->indexed.object, target->indexed.object);
            infer_from(me, inf, source->indexed.index, target->indexed.index);
        } else if (source->flags & (TYF_OBJECT | TYF_INTERSECTION)) {
            // An object infers to its signatures and index signatures, which are those of its apparent type (tsc's
            // inferFromTypes falls through to inferFromObjectTypes): `{ [P in keyof M]: (x: M[P]) => P }[T]` is
            // `(x: M[T]) => T` there, so a function returning "A" infers T = "A". Not to its properties: tsc's
            // inferFromProperties takes getPropertiesOfObjectType, none for an indexed access. `{ [K in Keys]:
            // Spec<K> }[Key]`'s apparent Spec<Key> infers nothing for Key from a Spec<"a"> (openclaw's dreaming.ts).
            // (Only an object type that isn't a reference or mapped type: tsc's own reference and mapped cases don't
            // apply to an indexed access)
            Ty const* apparent = ty_apparent_type(me, target);
            if (apparent != target && apparent->kind == TY_ANONYMOUS && !ty_members(me, apparent)->mapped &&
                !types_definitely_unrelated(me, source, apparent)
            ) {
                infer_from_signatures_and_indexes(me, inf, source, ty_members(me, apparent), 0);
            }
        }
    } else if (source->kind == TY_STRING_MAPPING && target->kind == TY_STRING_MAPPING) {
        if (source->string_mapping.mapping == target->string_mapping.mapping) {
            infer_from(me, inf, source->string_mapping.type, target->string_mapping.type);
        }
    } else if (target->kind == TY_TEMPLATE_LITERAL) {
        infer_to_template(me, inf, source, target);
    } else if (target->flags & TYF_OBJECT) {
        if (source->kind == TY_INTERSECTION) source = ty_reduced(me, source); // (tsc's inferFromTypes)
        // An intersection's apparent type has its type variables' apparent types (tsc's getApparentTypeOfIntersectionType
        // via getTypeWithThisArgument): `T & Box<unknown>` with `T extends Box<T> | undefined` infers from
        // `Box<T> & Box<unknown>`. Not for a conditional type's `infer` (tsc's NoConstraints)
        if (source->kind == TY_INTERSECTION && !inf->always_strict) {
            TyList members = source->composite.types;
            bool variables = false;
            for (u32 i = 0; i < members.len && !variables; i++) {
                variables = members.data[i]->kind == TY_TYPE_PARAM || (members.data[i]->flags & TYF_DEFERRED);
            }
            if (variables) {
                Ty const** parts = malloc(sizeof(Ty const*) * members.len);
                panic_if(!parts, "out of memory");
                bool changed = false;
                for (u32 i = 0; i < members.len; i++) {
                    Ty const* member = members.data[i];
                    parts[i] = member->kind == TY_TYPE_PARAM || (member->flags & TYF_DEFERRED) ? ty_apparent_type(me, member) : member;
                    changed |= parts[i] != member;
                }
                // (The others see the intersection as it was as their `this`, as in apparent_type_of_intersection())
                for (u32 i = 0; changed && i < members.len; i++) {
                    Ty const* member = members.data[i];
                    if (parts[i] != member || (member->kind == TY_REFERENCE && member->reference.this_arg)) continue;
                    parts[i] = ty_with_this_arg(me, member, source);
                }
                Ty const* apparent = changed ? ty_intersection(me, parts, members.len, nullptr, (TyList){0}) : source;
                free(parts);
                if (apparent != source && !(apparent->flags & (TYF_ANY | TYF_UNKNOWN | TYF_NEVER))) {
                    if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) {
                        infer_from(me, inf, apparent, target);
                        inf->depth--;
                        return;
                    }
                    source = apparent;
                }
            }
        }
        if (!(source->flags & (TYF_OBJECT | TYF_INTERSECTION))) {
            Ty const* apparent = ty_apparent_type(me, source);
            if (apparent != source && !(source->flags & (TYF_ANY | TYF_UNKNOWN | TYF_NEVER))) {
                // One that isn't an object starts over: `EB extends Die | Fail<E>` to `Fail<E'>` infers from each member
                // (tsc's inferFromTypes restarting with the apparent source)
                // (Not an unconstrained one's unknown, which infers nothing to an object target: a quarter million
                // times in effect)
                if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) {
                    if (!(apparent->flags & (TYF_UNKNOWN | TYF_ANY | TYF_NEVER))) infer_from(me, inf, apparent, target);
                    inf->depth--;
                    return;
                }
                source = apparent;
            }
        }
        // A generic mapped type over an array is apparently the array (tsc's inferFromTypes takes every source's apparent
        // type: getResolvedApparentTypeOfMappedType), `{ [K in keyof T]: [T[K], undefined] }` to `U[]` infers a tuple
        // (Not to a mapped type, which infers from the mapped source first: tsc's inferFromGenericMappedTypes)
        if (source->kind == TY_ANONYMOUS && source->anonymous.mapped && !(target->kind == TY_ANONYMOUS && target->anonymous.mapped)) {
            Ty const* apparent = ty_apparent_type(me, source);
            if (apparent != source && (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) source = apparent;
        }
        // (An array to an array, mutable or not, too: `T[]` to `readonly U[]`)
        bool same_reference = source->kind == TY_REFERENCE && target->kind == TY_REFERENCE &&
            (source->reference.target == target->reference.target || (ty_is_array(me, source) && ty_is_array(me, target))) &&
            !(source->reference.deferred && target->reference.deferred);
        if (same_reference) {
            // By type argument without going on the stack below: nested references to one type (`Box<Box<T>>`) aren't
            // deeply nested objects (tsc's inferFromTypes; only two deferred references go through invokeOnce). An array
            // on the stack made a nested pair of deferred arrays look deeply nested: tanstack-table's columns array
            // literal to `ReadonlyArray<ColumnDef<...>>` cut its `aggregationFn: readonly Item<F>[]` short
            infer_from_type_args(me, inf, source, target);
        } else if ((source->flags & (TYF_OBJECT | TYF_INTERSECTION)) && !visited_add(inf, source, target)) {
            for (u32 i = 0; i < inf->object_depth && !inf->circularity; i++) {
                inf->circularity = inf->object_sources[i] == source && inf->object_targets[i] == target;
            }
            VisitKey key = visited_key(inf, source, target);
            for (u32 i = 0; i < inf->circular_len && !inf->circularity; i++) inf->circularity = visit_key_eq(inf->circular[i], key);
        } else if (source->flags & (TYF_OBJECT | TYF_INTERSECTION)) {
            u32 saved_hit = inf->hit_priority;
            inf->hit_priority = UINT32_MAX;
            bool outer_circularity = inf->circularity;
            inf->circularity = false;
            unsigned saved_expanding = inf->expanding;
            // (Two of a kind already stop an inference walk, where relations take three: tsc's invokeOnce)
            if (!(inf->expanding & 1) && ty_is_deeply_nested(me, source, inf->object_sources, inf->object_depth, 2)) inf->expanding |= 1;
            if (!(inf->expanding & 2) && ty_is_deeply_nested(me, target, inf->object_targets, inf->object_depth, 2)) inf->expanding |= 2;
            if (inf->expanding != 3) {
                inf->object_sources[inf->object_depth] = source;
                inf->object_targets[inf->object_depth] = target;
                inf->object_depth++;
                // (What it is taken apart into is no array literal, a slice of a tuple that is all of it included)
                Ty const* saved_literal = inf->arg_tuple_literal;
                inf->arg_tuple_literal = nullptr;
                infer_from_objects(me, inf, source, target);
                inf->arg_tuple_literal = saved_literal;
                inf->object_depth--;
            } else {
                inf->circularity = true;
            }
            inf->expanding = saved_expanding;
            if (inf->circularity) record_circular(inf, source, target);
            inf->circularity |= outer_circularity;
            visited_done(inf, source, target, saved_hit);
        }
    }
    inf->depth--;
}

// tsc's inferToTemplateLiteralType: what each placeholder matched. A target of placeholders only that doesn't match
// infers never for each, so that the result doesn't match either (rather than everything collapsing to `string`)
static void infer_to_template(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    u32 len = target->template_literal.types.len;
    Ty const* stack_buf[16];
    Ty const** matches = len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * len);
    panic_if(!matches, "out of memory");
    bool matched = ty_infer_from_template(me, source, target, matches);
    bool all_empty = true;
    for (u32 i = 0; i <= len && all_empty; i++) all_empty = target->template_literal.texts[i]->len == 0;
    if (matched || all_empty) {
        for (u32 i = 0; i < len; i++) {
            Ty const* s = matched ? matches[i] : ty_intrinsic(me, TY_NEVER);
            Ty const* t = target->template_literal.types.data[i];
            if (s->kind == TY_LITERAL && (s->flags & TYF_STRING_LITERAL) && t->kind == TY_TYPE_PARAM) {
                InferParam* param = inference_param(inf, t);
                Ty const* constraint = param ? ty_base_constraint(me, param->param) : nullptr;
                Ty const* literal = constraint ? ty_template_literal_for_constraint(me, s, constraint) : nullptr;
                if (literal) {
                    infer_from(me, inf, literal, t);
                    continue;
                }
            }
            infer_from(me, inf, s, t);
        }
    }
    if (matches != stack_buf) free(matches);
}

void infer_types(Checker* me, Inference* inf, Ty const* source, Ty const* target, u32 priority) {
    infer_types_from(me, inf, source, target, priority, false);
}

static void infer_types_from(Checker* me, Inference* inf, Ty const* source, Ty const* target, u32 priority, bool contravariant) {
    inf->priority = priority;
    inf->contravariant = contravariant;
    inf->bivariant = false;
    inf->original_target = target;
    inf->depth = 0;
    if (inf->visited_len) {
        memset(inf->visited, 0, sizeof(VisitKey) * inf->visited_cap);
        inf->visited_len = 0;
    }
    inf->circular_len = 0;
    infer_from(me, inf, source, target);
}

// Results

// tsc's literalTypesWithSameBaseType: literals (or unions of literals) of one primitive, or members of one enum
static bool literal_base(Checker* me, Ty const* ty, u32* common, Symbol const** enum_decl, bool* first) {
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!literal_base(me, ty->composite.types.data[i], common, enum_decl, first)) return false;
        }
        return ty->composite.types.len > 0;
    }
    // (A template literal or string mapping's base type is string too: tsc's getBaseTypeOfLiteralType)
    if (ty->kind == TY_TEMPLATE_LITERAL || ty->kind == TY_STRING_MAPPING) {
        if (!*first && *enum_decl) return false;
        *first = false;
        *common &= TYF_STRING_LITERAL;
        return *common != 0;
    }
    if (ty->kind != TY_LITERAL) return false;
    Symbol const* decl = ty->flags & TYF_ENUM_LITERAL ? ty->literal.enum_decl : nullptr;
    // (Not a literal that is its own base type: the regular member of a single-member enum, which is the enum)
    if (decl && ty_enum_of_member(me, ty) == ty) return false;
    if (!*first && decl != *enum_decl) return false;
    *enum_decl = decl;
    *first = false;
    *common &= ty->flags;
    return decl || *common != 0;
}

static bool is_literal_of_same_base(Checker* me, TyList types) {
    u32 common = TYF_LITERAL;
    Symbol const* enum_decl = nullptr;
    bool first = true;
    for (u32 i = 0; i < types.len; i++) {
        // (never is no obstacle: `[]`'s element type, a candidate that was only null or undefined)
        if (types.data[i]->flags & TYF_NEVER) continue;
        if (!literal_base(me, types.data[i], &common, &enum_decl, &first)) return false;
    }
    return types.len > 0;
}

// tsc's getSingleCommonSupertype: the leftmost candidate for which no candidate to the right is a strict supertype, if it
// is a strict supertype of all the others; else the leftmost one for which no candidate to the right is a supertype
static Ty const* single_common_supertype(Checker* me, Ty const* const* types, u32 len) {
    Ty const* candidate = types[0];
    for (u32 i = 1; i < len; i++) {
        if (candidate != types[i] && ty_is_related(me, candidate, types[i], TY_RELATION_STRICT_SUBTYPE)) candidate = types[i];
    }
    bool all = true;
    for (u32 i = 0; i < len && all; i++) {
        all = types[i] == candidate || ty_is_related(me, types[i], candidate, TY_RELATION_STRICT_SUBTYPE);
    }
    if (all) return candidate;
    Ty const* ret = types[0];
    for (u32 i = 1; i < len; i++) {
        if (ret != types[i] && ty_is_related(me, ret, types[i], TY_RELATION_SUBTYPE)) ret = types[i];
    }
    return ret;
}

// tsc's getCommonSupertype: the leftmost candidate that every other one is a subtype of. Under strictNullChecks null and
// undefined (whole candidates or members of union ones) stay out of the contest and are added back
static Ty const* common_supertype(Checker* me, Ty const* const* types, u32 len) {
    if (len == 1) return types[0];
    if (!compiler_options()->strict_null_checks) {
        if (is_literal_of_same_base(me, (TyList){ .data = types, .len = len })) return ty_union(me, types, len, nullptr, (TyList){0});
        return single_common_supertype(me, types, len);
    }
    Ty const** primary = calloc(len, sizeof(Ty const*));
    panic_if(!primary, "out of memory");
    u32 nullable_flags = 0;
    for (u32 i = 0; i < len; i++) {
        Ty const* t = types[i];
        nullable_flags |= t->flags & TYF_NULLABLE;
        if (t->kind == TY_UNION && !(t->flags & TYF_BOOLEAN)) {
            TyBuf kept = {0};
            bool dropped = false;
            for (u32 j = 0; j < t->composite.types.len; j++) {
                Ty const* member = t->composite.types.data[j];
                if (member->flags & TYF_NULLABLE) {
                    nullable_flags |= member->flags & TYF_NULLABLE;
                    dropped = true;
                } else {
                    ty_buf_push(&kept, member);
                }
            }
            if (dropped) t = ty_union(me, kept.data, kept.len, nullptr, (TyList){0});
            ty_buf_drop(&kept);
        } else if (t->flags & TYF_NULLABLE) {
            t = ty_intrinsic(me, TY_NEVER);
        }
        primary[i] = t;
    }
    Ty const* ret;
    if (is_literal_of_same_base(me, (TyList){ .data = primary, .len = len })) {
        ret = ty_union(me, primary, len, nullptr, (TyList){0});
    } else {
        ret = single_common_supertype(me, primary, len);
    }
    free(primary);
    // (tsc's getNullableType with the flags of all of them)
    if (nullable_flags) {
        Ty const* parts[3] = { ret };
        u32 count = 1;
        if (nullable_flags & TYF_UNDEFINED) parts[count++] = ty_intrinsic(me, TY_UNDEFINED);
        if (nullable_flags & TYF_NULL) parts[count++] = ty_intrinsic(me, TY_NULL);
        ret = ty_union(me, parts, count, nullptr, (TyList){0});
    }
    return ret;
}

static Ty const* common_subtype(Checker* me, Ty const* const* types, u32 len) {
    Ty const* ret = types[0];
    for (u32 i = 1; i < len; i++) {
        if (ret != types[i] && ty_is_related(me, types[i], ret, TY_RELATION_SUBTYPE)) ret = types[i];
    }
    return ret;
}

// tsc's isObjectOrArrayLiteralType
// (Not a widened object literal: tsc's getWidenedTypeOfObjectLiteral drops ObjectFlags.ObjectLiteral, so a `new
// Map([["a", { a: 1 }]])`'s value type is a candidate like any other)
static bool is_literal_candidate(Inference const* inf, InferParam const* param, u32 index, Ty const* candidate) {
    if (ty_is_unwidened_object_literal(candidate)) return true;
    if (index < 64 && (param->co_literals >> index) & 1) return true;
    for (u32 i = 0; i < inf->array_literal_count; i++) {
        if (inf->array_literals[i] == candidate) return true;
    }
    return false;
}

static bool has_primitive_constraint(Checker* me, Ty const* param) {
    Ty const* constraint = ty_param_constraint(me, param);
    if (!constraint) return false;
    if (constraint->kind == TY_UNION || constraint->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < constraint->composite.types.len; i++) {
            Ty const* part = constraint->composite.types.data[i];
            if ((part->flags & (TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL_LIKE | TYF_LITERAL | TYF_BOOLEAN | TYF_ENUM)) ||
                part->kind == TY_KEYOF
            ) {
                return true;
            }
        }
        return (constraint->flags & TYF_BOOLEAN) != 0;
    }
    return (constraint->flags & (TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL_LIKE | TYF_LITERAL | TYF_BOOLEAN | TYF_ENUM)) ||
        constraint->kind == TY_KEYOF;
}

Ty const* inference_call_result(Checker* me, Inference* inf, u32 i, bool keep_literals) {
    return inference_call_result_fallback(me, inf, i, keep_literals, nullptr);
}

// The intersection of contravariant candidates (tsc's getIntersectionType, which has no shortcut for one type: a lone
// intersection is flattened into its members, so one by an alias is the same intersection without it, `Ctx` printing
// as `Base & { deps: string[] }`)
static Ty const* contra_intersection(Checker* me, Ty const* const* types, u32 len) {
    if (len == 1) return (types[0]->flags & TYF_ANY) ? ty_intersection_of_any(me, types[0]) : ty_intersection_unaliased(me, types[0]);
    return ty_intersection(me, types, len, nullptr, (TyList){0});
}

// `*fallback` (if asked for) gets the inference of the other variance when there are both, which the constraint
// check takes instead of the constraint if it fits (tsc's getInferredType fallbackType)
Ty const* inference_call_result_fallback(Checker* me, Inference* inf, u32 i, bool keep_literals, Ty const** fallback) {
    if (fallback) *fallback = nullptr;
    InferParam* param = &inf->params[i];
    if (param->fixed) return param->fixed;
    Ty const* co = nullptr;
    if (param->co_len) {
        Ty const* candidates_small[8]; // (Most parameters have a few candidates: no allocation for them)
        Ty const** candidates = param->co_len <= c_arraylen(candidates_small) ? candidates_small : malloc(sizeof(Ty const*) * param->co_len);
        // tsc's getCovariantInference: under a primitive constraint the candidates are regular, else (fresh) literals
        // widen if every inference was at the top level and keep_literals doesn't say otherwise
        // (So does a `const` type parameter)
        bool primitive = has_primitive_constraint(me, param->param) || ty_is_const_type_variable(me, param->param, 0);
        bool widen = !keep_literals && !primitive && param->top_level;
        for (u32 j = 0; j < param->co_len; j++) {
            Ty const* candidate = param->co[j];
            // (tsc regularizes an argument's literal when its contextual type has room for it, checkExpressionWithContextualType;
            // tttt looks at the candidates here instead, the contextual type being the type parameter where it's inferred to)
            if (!primitive && widen && (candidate->flags & TYF_FRESH_LITERAL) && ty_is_literal_of_contextual_type(me, candidate, param->param)) {
                candidate = ty_regular(me, candidate);
            }
            candidates[j] = primitive ? ty_regular(me, candidate) : widen ? ty_widened_literal(me, candidate) : candidate;
        }
        // Several object literals are one candidate, their union (tsc's unionObjectAndArrayLiteralCandidates; array
        // literals aren't told apart from other arrays here)
        u32 len = param->co_len;
        if (len > 1) {
            Ty const* literals_small[8];
            Ty const** literals = len <= c_arraylen(literals_small) ? literals_small : malloc(sizeof(Ty const*) * len);
            u32 literal_count = 0;
            for (u32 j = 0; j < len; j++) {
                if (is_literal_candidate(inf, param, j, candidates[j])) literals[literal_count++] = candidates[j];
            }
            if (literal_count) {
                // (Last, like tsc: which decides ties, and which of a literal and a type with more properties wins)
                Ty const* merged = literal_count > 1 ? ty_union_subtype_reduced(me, literals, literal_count) : literals[0];
                u32 kept = 0;
                for (u32 j = 0; j < len; j++) {
                    if (!is_literal_candidate(inf, param, j, candidates[j])) candidates[kept++] = candidates[j];
                }
                candidates[kept++] = merged;
                len = kept;
            }
            if (literals != literals_small) free(literals);
        }
        // (A combining priority unions them with subtype reduction: tsgo's getCovariantInference, UnionReductionSubtype)
        co = (param->priority & INFER_PRIORITY_IMPLIES_COMBINATION) ?
            (len > 1 ? ty_union_subtype_reduced(me, candidates, len) : candidates[0]) :
            common_supertype(me, candidates, len);
        co = ty_widened_nullable(me, ty_widened_objects(me, co)); // (tsc's getWidenedType of the inference)
        if (candidates != candidates_small) free(candidates);
    }
    // (Candidates of a priority that implies combining them are intersected)
    Ty const* contra = !param->contra_len ? nullptr : (param->priority & INFER_PRIORITY_IMPLIES_COMBINATION) ?
        contra_intersection(me, param->contra, param->contra_len) : common_subtype(me, param->contra, param->contra_len);
    if (co && contra) {
        // The covariant inference wins if it fits what the contravariant positions ask for
        // ... and neither its own candidates (it isn't one of a conflicting set) nor those of a type parameter constrained
        // to this one have one it wouldn't cover
        if (fallback) *fallback = co;
        if (co->flags & (TYF_NEVER | TYF_ANY)) return contra;
        bool fits_some = false;
        for (u32 j = 0; j < param->contra_len && !fits_some; j++) {
            fits_some = ty_is_related(me, co, param->contra[j], TY_RELATION_ASSIGNABLE);
        }
        if (!fits_some) return contra;
        for (u32 j = 0; j < inf->len; j++) {
            InferParam const* other = &inf->params[j];
            if (other != param && ty_param_constraint(me, other->param) != param->param) continue;
            for (u32 k = 0; k < other->co_len; k++) {
                if (!ty_is_related(me, other->co[k], co, TY_RELATION_ASSIGNABLE)) return contra;
            }
        }
        if (fallback) *fallback = contra;
        return co;
    }
    return co ? co : contra;
}

Ty const* inference_conditional_result(Checker* me, Inference* inf, u32 i) {
    InferParam* param = &inf->params[i];
    // (Subtype reduced: tsc's getTypeFromInference)
    if (param->co_len == 1) return param->co[0];
    // (Not among reverse mapped types, which tttt resolves eagerly: comparing them structurally runs into their
    // recursion where tsc's stay deferred, reverseMappedTypeRecursiveInference)
    bool reverse = false;
    for (u32 c = 0; c < param->co_len && !reverse; c++) {
        reverse = param->co[c]->kind == TY_ANONYMOUS && param->co[c]->anonymous.is_reverse;
    }
    if (param->co_len) {
        return reverse ? ty_union(me, param->co, param->co_len, nullptr, (TyList){0}) :
            ty_union_subtype_reduced(me, param->co, param->co_len);
    }
    // (tsc's getTypeFromInference: a lone aliased intersection flattened, see contra_intersection())
    if (param->contra_len) return contra_intersection(me, param->contra, param->contra_len);
    return nullptr;
}

// Inferred type arguments

typedef struct ConstraintPass {
    Signature const* sig;
    Inference* inf;
    Ty const** results;
    Ty const* const* fallbacks; // The other variance's inference, see inference_call_result_fallback()
    bool const* inferred;
    uint8_t* state; // Per type parameter: 0 not yet, 1 in progress, 2 done
    TyMapper const* mapper; // What the results say so far
} ConstraintPass;

// A mapper from the inference's type parameters to `results`: the last one made, if it says the same
static TyMapper const* inference_mapper_of(Checker* me, Inference* inf, TyList params, Ty const* const* results) {
    TyMapper const* last = inf->last_mapper;
    if (last && last->len == params.len) {
        bool same = true;
        for (u32 i = 0; i < params.len && same; i++) same = last->sources[i] == params.data[i] && last->targets[i] == results[i];
        if (same) return last;
    }
    inf->last_mapper = ty_mapper_new(me, params, (TyList){ .data = (Ty const**)results, .len = params.len });
    return inf->last_mapper;
}

// The constraint check of inference_mapper() for type parameter i, after those of the others (a constraint that
// mentions one of them is instantiated with its final result; one in progress, a circular constraint, stays as is)
static void constraint_pass_resolve(Checker* me, ConstraintPass* pass, u32 i) {
    if (pass->state[i]) return;
    pass->state[i] = 1;
    TyList params = sig_res(me, pass->sig)->type_params;
    Ty const* constraint = ty_param_constraint(me, params.data[i]);
    // (Silent never, inference_mapper_no_default()'s nothing, fits any constraint)
    if (constraint && constraint != params.data[i] && pass->results[i] != ty_intrinsic(me, TY_SILENT_NEVER)) {
        if (constraint->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
            // (Only those it mentions, as tsc's non-fixing mapper infers them on demand: resolving the others first
            // would show them this one in progress, `D extends Dest<T>` while `T extends Transform<S>` resolves)
            for (u32 j = 0; j < params.len; j++) {
                if (j != i && !pass->state[j] && ty_mentions_any(me, constraint, &params.data[j], 1)) constraint_pass_resolve(me, pass, j);
            }
        }
        Ty const* instantiated = ty_instantiate(me, constraint, pass->mapper);
        // The constraint's `this` is the inference (tsc's getTypeWithThisArgument in getInferredType): a builder
        // `T extends QueryBuilder` fits with the methods it returns from `this`-typed ones
        Ty const* result = pass->results[i];
        bool fits = pass->inferred[i] && (pass->inf->within_relation ?
            ty_is_related_within(me, result, ty_with_this_arg(me, instantiated, result)) :
            ty_is_related(me, result, ty_with_this_arg(me, instantiated, result), TY_RELATION_ASSIGNABLE));
        InferParam const* param = &pass->inf->params[i];
        if (!fits && pass->inferred[i] && result->kind == TY_UNION && param->has_priority &&
            param->priority == INFER_PRIORITY_RETURN_TYPE
        ) {
            // A pure return type inference may fit by dropping the members that don't (tsc 6.0's getInferredType:
            // filteredByConstraint)
            TyBuf kept = {0};
            for (u32 m = 0; m < result->composite.types.len; m++) {
                Ty const* member = result->composite.types.data[m];
                Ty const* bound = ty_with_this_arg(me, instantiated, member);
                if (pass->inf->within_relation ? ty_is_related_within(me, member, bound) :
                    ty_is_related(me, member, bound, TY_RELATION_ASSIGNABLE)
                ) {
                    ty_buf_push(&kept, member);
                }
            }
            if (kept.len) {
                pass->results[i] = ty_union(me, kept.data, kept.len, nullptr, (TyList){0});
                pass->mapper = inference_mapper_of(me, pass->inf, params, pass->results);
                fits = true;
            }
            ty_buf_drop(&kept);
        }
        if (!fits) {
            // (The inference of the other variance if that fits: co `[Ref<string>]` when contra `[string]` doesn't)
            Ty const* fallback = pass->fallbacks[i];
            bool fallback_fits = fallback && (pass->inf->within_relation ?
                ty_is_related_within(me, fallback, ty_with_this_arg(me, instantiated, fallback)) :
                ty_is_related(me, fallback, ty_with_this_arg(me, instantiated, fallback), TY_RELATION_ASSIGNABLE));
            pass->results[i] = fallback_fits ? fallback : instantiated;
            pass->mapper = inference_mapper_of(me, pass->inf, params, pass->results);
        }
    }
    pass->state[i] = 2;
}

// The mapper for what has been inferred so far: common supertypes of the candidates, then defaults, then constraints
// `fixing`: for the contextual type of a context-sensitive argument, which decides the type parameters it mentions
static TyMapper const* inference_mapper_impl(Checker* me, Signature const* sig, Inference* inf, bool fixing, bool no_default);

TyMapper const* inference_mapper(Checker* me, Signature const* sig, Inference* inf, bool fixing) {
    return inference_mapper_impl(me, sig, inf, fixing, false);
}

// What has been inferred so far as a call nested in an argument sees it: type parameters without inferences are silent
// never, which nothing is inferred from, the rest checked against their constraints like inference_mapper() (tsc's
// getMapperFromContext(cloneInferenceContext(outerContext, InferenceFlags.NoDefault)))
TyMapper const* inference_mapper_no_default(Checker* me, Signature const* sig, Inference* inf) {
    return inference_mapper_impl(me, sig, inf, false, true);
}

static TyMapper const* inference_mapper_impl(Checker* me, Signature const* sig, Inference* inf, bool fixing, bool no_default) {
    TyList params = sig_res(me, sig)->type_params;
    // (Of a type predicate, its type: tsc's isTypeParameterAtTopLevelInReturnType)
    TyPredicate const* predicate = signature_predicate(me, sig);
    Ty const* return_type = predicate ? predicate->type : signature_return_type(me, sig);
    // (Scratch: the mappers copy the results)
    enum { SMALL = 8 };
    Ty const* small_results[SMALL], * small_fallbacks[SMALL];
    bool small_inferred[SMALL];
    uint8_t small_state[SMALL];
    bool small = params.len <= SMALL;
    Ty const** results = small ? small_results : malloc(sizeof(Ty const*) * params.len);
    Ty const** fallbacks = small ? small_fallbacks : malloc(sizeof(Ty const*) * params.len);
    bool* inferred = small ? small_inferred : malloc(sizeof(bool) * params.len);
    uint8_t* state = small ? small_state : malloc(params.len);
    memset(inferred, 0, sizeof(bool) * params.len);
    memset(state, 0, params.len);
    for (u32 i = 0; i < params.len; i++) fallbacks[i] = nullptr;
    for (u32 i = 0; i < params.len; i++) {
        // tsc: literals widen unless the type parameter is at the top level of the return type and hasn't been fixed
        // (or the constraint is primitive, see inference_call_result())
        bool keep_literals = !fixing && !inf->params[i].fixed && return_type &&
            is_param_at_top_level(me, return_type, params.data[i]);
        results[i] = inference_call_result_fallback(me, inf, i, keep_literals, &fallbacks[i]);
        inferred[i] = results[i] != nullptr;
        if (!results[i]) results[i] = params.data[i]; // stands for itself until the defaults are in, below
    }
    TyMapper const* mapper = inference_mapper_of(me, inf, params, results);
    for (u32 i = 0; i < params.len; i++) {
        if (inferred[i]) continue;
        if (no_default) {
            results[i] = ty_intrinsic(me, TY_SILENT_NEVER);
            inferred[i] = true; // (Fits any constraint)
            mapper = inference_mapper_of(me, inf, params, results);
            continue;
        }
        Ty const* fallback = ty_param_default(me, params.data[i]);
        if (fallback) inferred[i] = true; // (Only for the constraint check below)
        Ty const* nothing = ty_intrinsic(me, inf->any_default ? TY_ANY : TY_UNKNOWN_); // (tsc's getDefaultTypeArgumentType)
        if (fallback && (fallback->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
            // An earlier type parameter with neither inferences nor a default is its constraint by now (tsc's
            // getInferredType of it, which the default's back-reference mapper asks for):
            // `<C extends Component, S = C["state"]>` without inferences for C is S = Component["state"]
            bool changed = false;
            for (u32 j = 0; j < i; j++) {
                if (inferred[j] || results[j] != nothing) continue;
                Ty const* constraint = ty_param_constraint(me, params.data[j]);
                if (!constraint || constraint == params.data[j]) continue;
                // (Whether or not nothing would satisfy it: tsc's getInferredType takes the constraint when there's
                // no inferred type at all, `<R extends AnyAction, T = R["payload"]>` with AnyAction unresolved)
                // (Not one that mentions this type parameter or a later one: tsc's getInferredType of that one asks
                // for the earlier one again, whose inferred type is unknown by then: `<N extends P, P = N>` is unknown)
                if ((constraint->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
                    ty_mentions_any(me, constraint, params.data + i, params.len - i)
                ) {
                    continue;
                }
                Ty const* instantiated = ty_instantiate(me, constraint, mapper);
                if (instantiated == nothing) continue;
                results[j] = instantiated;
                changed = true;
            }
            // (First: an inferred one's constraint sees them as their constraints already, `<T extends Unresolved, K
            // extends keyof T['P'], M = T['P'][K]>` without inferences for T keeps K's)
            if (changed) mapper = inference_mapper_of(me, inf, params, results);
            changed = false;
            // So is an earlier inference that doesn't satisfy its constraint (getInferredType through the default's
            // non-fixing mapper): `<P extends "a" | "b", R = Cfg[P]>` called with "c" is R = Cfg["a" | "b"]
            for (u32 j = 0; j < i; j++) {
                if (!inferred[j] || ty_param_default(me, params.data[j]) == results[j]) continue;
                if (!inf->params[j].co_len && !inf->params[j].contra_len) continue;
                Ty const* constraint = ty_param_constraint(me, params.data[j]);
                if (!constraint || constraint == params.data[j]) continue;
                // (Not one that mentions a type parameter not known yet, this one or a later one: those resolve
                // through each other in tsc, which the constraint pass below stands for)
                if ((constraint->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
                    ty_mentions_any(me, constraint, params.data + i, params.len - i)
                ) {
                    continue;
                }
                Ty const* instantiated = ty_instantiate(me, constraint, mapper);
                bool fits = inf->within_relation ? ty_is_related_within(me, results[j], ty_with_this_arg(me, instantiated, results[j])) :
                    ty_is_related(me, results[j], ty_with_this_arg(me, instantiated, results[j]), TY_RELATION_ASSIGNABLE);
                if (fits) continue;
                results[j] = instantiated;
                changed = true;
            }
            if (changed) mapper = inference_mapper_of(me, inf, params, results);
        }
        if (fallback && (fallback->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && ty_mentions_any(me, fallback, params.data + i, params.len - i)) {
            // This type parameter or a later one in its own default is unknown (tsc's createBackreferenceMapper):
            // `<T = T>` without inferences is unknown
            Ty const** backref = malloc(sizeof(Ty const*) * params.len);
            panic_if(!backref, "out of memory");
            for (u32 j = 0; j < params.len; j++) backref[j] = j >= i ? ty_intrinsic(me, TY_UNKNOWN_) : results[j];
            results[i] = ty_instantiate(me, fallback, ty_mapper_new(me, params, (TyList){ .data = backref, .len = params.len }));
            free(backref);
        } else {
            results[i] = fallback ? ty_instantiate(me, fallback, mapper) : nothing;
        }
        if (results[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
            // A default that mentions a type parameter that has none itself
            bool self = false;
            for (u32 j = 0; j < params.len; j++) self |= !inferred[j] && results[i] == params.data[j];
            if (self) results[i] = nothing;
        }
        mapper = inference_mapper_of(me, inf, params, results);
    }
    // An inference that doesn't satisfy the constraint is replaced by it, and the argument check reports the rest. So
    // is a type parameter without inferences or a default (tsc's getInferredType: `!inferredType`). The constraint
    // sees the other type parameters as they end up (tsc instantiates it with the non-fixing mapper, which infers them
    // on demand): `<U extends T, T extends A>` without inferences is U = T = A
    ConstraintPass pass = {
        .sig = sig, .inf = inf, .results = results, .fallbacks = fallbacks, .inferred = inferred, .state = state, .mapper = mapper,
    };
    for (u32 i = 0; i < params.len; i++) constraint_pass_resolve(me, &pass, i);
    if (!small) {
        free(results);
        free(fallbacks);
        free(inferred);
        free(state);
    }
    return pass.mapper;
}
