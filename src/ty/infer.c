#include "ty/ty_internal.h"
#include "error.h"

// Type inference: walks a source type and a target type together and collects, for every type parameter being
// inferred that the target mentions, the source types found in its place. Follows what tsc observably does
// (inferTypes/getInferredType): candidates from covariant and contravariant positions are kept apart, candidates of a
// better priority replace worse ones, and a call picks the common supertype of the candidates where a conditional type
// takes their union.

void inference_init(Inference* inf, TyList params) {
    *inf = (Inference){0};
    inf->cap = params.len > 4 ? params.len : 4;
    inf->params = calloc(inf->cap, sizeof(InferParam));
    panic_if(!inf->params, "out of memory");
    for (u32 i = 0; i < params.len; i++) {
        inf->params[inf->len++] = (InferParam){ .param = params.data[i], .top_level = true };
    }
}

void inference_drop(Inference* inf) {
    for (u32 i = 0; i < inf->len; i++) {
        free(inf->params[i].co);
        free(inf->params[i].contra);
    }
    free(inf->params);
    free(inf->visited);
    free(inf->array_literals);
    free(inf->inferred_type_params);
    *inf = (Inference){0};
}

// False if the pair was there already
static bool visited_add(Inference* inf, Ty const* source, Ty const* target) {
    u64 key = ((u64)source->id << 32) | target->id;
    if (!key) return true;
    if ((inf->visited_len + 1) * 2 > inf->visited_cap) {
        u32 cap = inf->visited_cap ? inf->visited_cap * 2 : 64;
        u64* grown = calloc(cap, sizeof(u64));
        panic_if(!grown, "out of memory");
        for (u32 i = 0; i < inf->visited_cap; i++) {
            u64 old = inf->visited[i];
            if (!old) continue;
            u32 at = (u32)((old * 0x9E3779B97F4A7C15ull) >> 32) & (cap - 1);
            while (grown[at]) at = (at + 1) & (cap - 1);
            grown[at] = old;
        }
        free(inf->visited);
        inf->visited = grown;
        inf->visited_cap = cap;
    }
    u32 at = (u32)((key * 0x9E3779B97F4A7C15ull) >> 32) & (inf->visited_cap - 1);
    while (inf->visited[at]) {
        if (inf->visited[at] == key) return false;
        at = (at + 1) & (inf->visited_cap - 1);
    }
    inf->visited[at] = key;
    inf->visited_len++;
    return true;
}

static InferParam* inference_param(Inference* inf, Ty const* ty) {
    // (An indexed access is inferred to by reverse mapped types: `T[K]`, see reverse_mapped_prop_type())
    if (ty->kind != TY_TYPE_PARAM && ty->kind != TY_INDEXED) return nullptr;
    for (u32 i = 0; i < inf->len; i++) {
        if (inf->params[i].param == ty) return &inf->params[i];
    }
    return nullptr;
}

static void candidates_push(Ty const*** list, u32* len, Ty const* candidate) {
    for (u32 i = 0; i < *len; i++) {
        if ((*list)[i] == candidate) return;
    }
    *list = realloc(*list, (*len + 1) * sizeof(Ty const*));
    panic_if(!*list, "out of memory");
    (*list)[(*len)++] = candidate;
}

// tsc's isTypeParameterAtTopLevel, without the conditional type case
static bool is_param_at_top_level(Ty const* type, Ty const* param) {
    if (type == param) return true;
    if (type->kind == TY_UNION || type->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < type->composite.types.len; i++) {
            if (is_param_at_top_level(type->composite.types.data[i], param)) return true;
        }
    }
    return false;
}

static void infer_from(Checker* me, Inference* inf, Ty const* source, Ty const* target);

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
    TyList params = sig->type_params;
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
    if (!compiler_options()->strict_null_checks) return type;
    Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

// The parameters of `sig` from `from` on, as the tuple a rest parameter in that position would receive (tsc's
// getRestTypeAtPosition, less its shortcut for a lone rest parameter)
Ty const* ty_signature_params_tuple(Checker* me, Signature const* sig, u32 from, TyMapper const* mapper) {
    u32 arity = sig->param_count > from ? sig->param_count - from : 0;
    unsigned char* flags = calloc(arity ? arity : 1, 1);
    Atom const** names = calloc(arity ? arity : 1, sizeof(Atom const*));
    Ty const** elements = calloc(arity ? arity : 1, sizeof(Ty const*));
    for (u32 j = 0; j < arity; j++) {
        SigParam const* param = &sig->params[from + j];
        flags[j] = (param->optional ? TUPLE_ELEMENT_OPTIONAL : 0) | (param->rest ? TUPLE_ELEMENT_REST : 0);
        names[j] = param->name;
        elements[j] = ty_instantiate(me, param->optional ? ty_optional_param_type(me, param->type) : param->type, mapper);
    }
    TupleShape shape = { .arity = arity, .flags = flags, .names = names };
    Ty const* target = ty_tuple_target(me, &shape);
    Ty const* ret = arity ? ty_reference(me, target, (TyList){ .data = elements, .len = arity }) : target;
    free(flags);
    free(names);
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
    if (i < sig->param_count && !sig->params[i].rest) return ty_instantiate(me, sig->params[i].type, mapper);
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return nullptr;
    Ty const* rest = ty_instantiate(me, sig->params[sig->param_count - 1].type, mapper);
    if (ty_is_tuple(rest)) {
        TupleShape const* shape = rest->reference.target->declared.tuple;
        TyList elements = ty_reference_args(me, rest);
        u32 j = i + 1 - sig->param_count;
        bool ends_in_rest = shape->arity && (shape->flags[shape->arity - 1] & TUPLE_ELEMENT_REST);
        if (j >= shape->arity) return ends_in_rest ? ty_rest_element_type(me, elements.data[shape->arity - 1]) : nullptr;
        if (shape->flags[j] & TUPLE_ELEMENT_REST) return ty_rest_element_type(me, elements.data[j]);
        return elements.data[j];
    }
    return ty_rest_element_type(me, rest);
}

// From the `this` and parameter types of `source` (instantiated with `mapper`) to those of `target` (tsc's
// applyToParameterTypes)
static void infer_from_params(
    Checker* me, Inference* inf, InferStep step, Signature const* source, Signature const* target, TyMapper const* mapper
) {
    if (source->this_type && target->this_type) {
        infer_step(me, inf, step, ty_instantiate(me, source->this_type, mapper), target->this_type);
    }
    for (u32 i = 0; i < target->param_count; i++) {
        SigParam const* target_param = &target->params[i];
        if (target_param->rest) {
            // `(...args: A) => any`: the rest of the source's parameters as a tuple
            if (i < source->param_count && source->params[i].rest && i + 1 == source->param_count) {
                infer_step(me, inf, step, ty_instantiate(me, source->params[i].type, mapper), target_param->type);
            } else {
                infer_step(me, inf, step, ty_signature_params_tuple(me, source, i, mapper), target_param->type);
            }
            break;
        }
        Ty const* source_type = source_type_at(me, source, i, mapper);
        if (!source_type) break;
        infer_step(me, inf, step, source_type, target_param->type);
    }
}

// From the return type (or type predicate) of `source` to that of `target` (tsc's applyToReturnTypes)
static void infer_from_returns(
    Checker* me, Inference* inf, InferStep step, Signature const* source, Signature const* target, TyMapper const* mapper
) {
    TyPredicate const* source_predicate = signature_predicate(me, source);
    TyPredicate const* target_predicate = signature_predicate(me, target);
    if (source_predicate && target_predicate && source_predicate->type && target_predicate->type &&
        source_predicate->is_this == target_predicate->is_this && source_predicate->asserts == target_predicate->asserts
    ) {
        infer_step(me, inf, step, ty_instantiate(me, source_predicate->type, mapper), target_predicate->type);
        return;
    }
    infer_step(me, inf, step, ty_instantiate(me, signature_return_type(me, source), mapper), signature_return_type(me, target));
}

static void infer_from_signatures(Checker* me, Inference* inf, Signature const* source, Signature const* target) {
    signature_resolve(me, source);
    signature_resolve(me, target);
    if (target->type_params.len) {
        // The target's own type parameters are any (tsc's getErasedSignature): they aren't what is being inferred, and
        // walking into them would infer to them (`map<M>` of `N1<X>` compared with itself)
        Ty const* stack_anys[16];
        Ty const** anys = target->type_params.len <= c_arraylen(stack_anys) ? stack_anys : malloc(sizeof(Ty const*) * target->type_params.len);
        panic_if(!anys, "out of memory");
        for (u32 i = 0; i < target->type_params.len; i++) anys[i] = ty_intrinsic(me, TY_ANY);
        target = ty_signature_instantiation(me, target, (TyList){ .data = anys, .len = target->type_params.len });
        if (anys != stack_anys) free(anys);
        signature_resolve(me, target);
    }
    TyMapper const* base = base_signature_mapper(me, source);
    // Parameters are a contravariant position. Without strictFunctionTypes they compare bivariantly and tsc infers as
    // if they were covariant; the parameters of a method (and everything nested in them) are bivariant too
    bool flip = compiler_options()->strict_function_types;
    bool saved_bivariant = inf->bivariant;
    inf->bivariant |= (target->flags & SIGNATURE_METHOD) != 0;
    if (flip) inf->contravariant = !inf->contravariant;
    infer_from_params(me, inf, (InferStep){0}, source, target, base);
    if (flip) inf->contravariant = !inf->contravariant;
    inf->bivariant = saved_bivariant;
    infer_from_returns(me, inf, (InferStep){0}, source, target, base);
}

// Signature instantiation

Signature const* ty_signature_instantiation(Checker* me, Signature const* sig, TyList args) {
    signature_resolve(me, sig);
    check(args.len == sig->type_params.len);
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
        .mapper = ty_mapper_new(me, sig->type_params, (TyList){ .data = targets, .len = args.len }),
    };
    *slot = ret;
    signature_resolve(me, ret);
    ret->type_params = (TyList){0};
    return ret;
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
// an earlier one of the list) replaced by a copy named `T_1`, `T_2`... whose constraint refers to the renamed ones
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
            Atom const* fresh_name = name;
            for (u32 n = 1; names_type_param(all, all_len, fresh_name); n++) {
                cstr text = cstr_d(atom_sv(name), "_", n);
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
    signature_resolve(me, sig);
    signature_resolve(me, contextual);
    TyList unique = unique_type_params(me, inf, sig->type_params);
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
    *ret = *sig;
    ret->type_params = type_params;
    return ret;
}

Signature const* ty_instantiate_signature_in_context_of(
    Checker* me, Signature const* sig, Signature const* contextual, TyMapper const* context_mapper
) {
    signature_resolve(me, sig);
    signature_resolve(me, contextual);
    Inference inf;
    inference_init(&inf, sig->type_params);
    inf.within_relation = true;
    // (Parameters to parameters, covariantly: the contextual signature's parameter types are what `sig`'s receive)
    infer_from_params(me, &inf, (InferStep){ .top = true, .priority = INFER_PRIORITY_NORMAL }, contextual, sig, context_mapper);
    if (!context_mapper) {
        infer_from_returns(me, &inf, (InferStep){ .top = true, .priority = INFER_PRIORITY_RETURN_TYPE }, contextual, sig, nullptr);
    }
    TyMapper const* mapper = inference_mapper(me, sig, &inf, false);
    inference_drop(&inf);
    return ty_signature_instantiation(me, sig, (TyList){ .data = mapper->targets, .len = sig->type_params.len });
}

static void infer_from_signature_lists(
    Checker* me, Inference* inf, Signature* const* sources, u32 source_count, Signature* const* targets, u32 target_count
) {
    // Matched up from the end: for overloads the last one is the most general
    u32 count = source_count < target_count ? source_count : target_count;
    for (u32 i = 0; i < count; i++) {
        infer_from_signatures(me, inf, sources[source_count - count + i], targets[target_count - count + i]);
    }
}

static bool is_empty_tuple(Ty const* ty) {
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_TUPLE_TARGET;
}

// What a property of type `source` says about `T[K]` in the template of `{ [K in keyof T]: Template }` (tsc's
// inferReverseMappedType): the template is inferred to with `T[K]` as the one type variable
static Ty const* reverse_mapped_prop_type(Checker* me, Ty const* source, MappedInfo const* mapped, Ty const* operand) {
    Ty const* indexed = ty_indexed(me, operand, mapped->type_param);
    Inference inf;
    inference_init(&inf, (TyList){ .data = &indexed, .len = 1 });
    infer_types(me, &inf, source, mapped->template, INFER_PRIORITY_NORMAL);
    Ty const* ret = inference_conditional_result(me, &inf, 0);
    inference_drop(&inf);
    return ret ? ret : ty_intrinsic(me, TY_UNKNOWN_);
}

// The T for which `{ [K in keyof T]: Template }` would be `source` (tsc's createReverseMappedType), eagerly. Null if
// the source isn't an object with properties or a string index
static Ty const* reverse_mapped_type(Checker* me, Ty const* source, MappedInfo const* mapped, Ty const* operand) {
    if (!(source->flags & TYF_OBJECT) || ty_is_array(me, source) || ty_is_tuple(source)) return nullptr; // TODO: arrays, tuples
    Members const* members = ty_members(me, source);
    IndexInfo string_index = {0};
    bool has_string_index = false;
    for (u32 i = 0; i < members->index_count; i++) {
        if (members->index[i].key->flags & TYF_STRING) {
            string_index = members->index[i];
            has_string_index = true;
        }
    }
    if (!members->prop_count && !has_string_index) return nullptr;
    // A recursive mapped type (`{ [K in keyof V]: Foo<V[K]> }`) reverse maps its property types again, all the way
    // down: tsc's reverse mapped types resolve their members lazily, these are built right away and cut off
    if (my reverse_mapped_depth >= 4) return nullptr;
    my reverse_mapped_depth++;
    ObjectLiteralProp* props = calloc(members->prop_count + 1, sizeof(ObjectLiteralProp));
    panic_if(!props, "out of memory");
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        u32 flags = prop->flags & (PROP_OPTIONAL | PROP_READONLY);
        if (mapped->optional > 0) flags &= ~(u32)PROP_OPTIONAL; // `?` in the mapped type: T's property needn't be
        if (mapped->readonly > 0) flags &= ~(u32)PROP_READONLY;
        props[i] = (ObjectLiteralProp){
            .name = prop->name,
            .flags = flags,
            .type = reverse_mapped_prop_type(me, prop_type(me, prop), mapped, operand),
        };
    }
    if (has_string_index) string_index.value = reverse_mapped_prop_type(me, string_index.value, mapped, operand);
    Ty const* ret = ty_object_literal(me, props, members->prop_count, &string_index, has_string_index ? 1 : 0, OBJECT_SYNTHETIC);
    free(props);
    my reverse_mapped_depth--;
    return ret;
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
        Members const* members = ty_members(me, source);
        u32 count = members->prop_count + members->index_count;
        if (!count) return true;
        Ty const** types = malloc(sizeof(Ty const*) * count);
        panic_if(!types, "out of memory");
        for (u32 i = 0; i < members->prop_count; i++) types[i] = prop_type(me, members->props[i]);
        for (u32 i = 0; i < members->index_count; i++) types[members->prop_count + i] = members->index[i].value;
        infer_from(me, inf, ty_union(me, types, count, nullptr, (TyList){0}), mapped->template);
        free(types);
        return true;
    }
    return false;
}

// tsc's isObjectTypeWithInferableIndex, as far as tttt can tell: anonymous object types (object and type literals,
// mapped types) without signatures, and intersections of them
static bool inferable_index(Checker* me, Ty const* ty, u32 depth) {
    if (ty->kind == TY_INTERSECTION && depth < 4) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!inferable_index(me, ty->composite.types.data[i], depth + 1)) return false;
        }
        return true;
    }
    if (ty->kind != TY_ANONYMOUS) return false;
    Members const* members = ty_members(me, ty);
    return !members->call_count && !members->construct_count;
}

static void infer_from_objects(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    if ((ty_is_tuple(source) || is_empty_tuple(source)) && ty_is_tuple(target)) {
        TupleShape const* source_shape = ty_is_tuple(source) ? source->reference.target->declared.tuple : source->declared.tuple;
        TupleShape const* target_shape = target->reference.target->declared.tuple;
        TyList source_args = ty_is_tuple(source) ? ty_reference_args(me, source) : (TyList){0};
        TyList target_args = ty_reference_args(me, target);
        for (u32 i = 0; i < target_shape->arity; i++) {
            if (target_shape->flags[i] & TUPLE_ELEMENT_REST) {
                // `[infer H, ...infer R]`: a trailing rest element gets what's left of the source, as a mutable tuple
                // (tsc's sliceTupleType)
                if (i + 1 != target_shape->arity || i > source_shape->arity) break;
                u32 arity = source_shape->arity - i;
                TupleShape shape = {
                    .arity = arity,
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
            if (!(target_args.data[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) continue;
            bool flip = ty_type_param_is_contravariant(me, target->reference.target, i);
            if (flip) inf->contravariant = !inf->contravariant;
            infer_from(me, inf, source_args.data[i], target_args.data[i]);
            if (flip) inf->contravariant = !inf->contravariant;
        }
        return;
    }
    if (ty_is_array(me, target) && (ty_is_tuple(source) || ty_is_array(me, source))) {
        // Arrays and tuples against an array (mutable or not): by element, without a walk through every Array method
        Ty const* element = ty_reference_args(me, target).data[0];
        if (ty_is_array(me, source)) {
            infer_from(me, inf, ty_reference_args(me, source).data[0], element);
        } else {
            TupleShape const* shape = source->reference.target->declared.tuple;
            TyList source_args = ty_reference_args(me, source);
            for (u32 i = 0; i < source_args.len; i++) {
                Ty const* arg = source_args.data[i];
                if (shape->flags[i] & TUPLE_ELEMENT_REST) arg = ty_rest_element_type(me, arg);
                infer_from(me, inf, arg, element);
            }
        }
        return;
    }
    if (is_empty_tuple(source) && ty_is_array(me, target)) return;

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
        infer_from(me, inf, source_mapped->template, target_mapped->template);
        if (source_mapped->name_type && target_mapped->name_type) {
            infer_from(me, inf, source_mapped->name_type, target_mapped->name_type);
        }
        return;
    }
    if (target_members->mapped) {
        MappedInfo const* mapped = target_members->mapped;
        if (mapped->modifiers_type && mapped->template->kind == TY_INDEXED &&
            mapped->template->indexed.object == mapped->modifiers_type && mapped->template->indexed.index == mapped->type_param
        ) {
            // A plain homomorphic `T[K]` template (Partial, Readonly...): the source itself is the candidate, as the
            // reverse mapped type it stands for (so a naked T elsewhere wins: tsc's inferToMappedType)
            infer_with_priority(me, inf, source, mapped->modifiers_type, INFER_PRIORITY_HOMOMORPHIC_MAPPED_TYPE);
        } else if (!mapped->name_type) {
            infer_to_mapped(me, inf, source, mapped, mapped->constraint);
        }
        return;
    }
    for (u32 i = 0; i < target_members->prop_count; i++) {
        Prop const* target_prop = target_members->props[i];
        FoundProp source_prop = ty_find_prop(me, source, target_prop->name);
        if (source_prop.found) infer_from(me, inf, source_prop.type, prop_type(me, target_prop));
    }
    Members const* source_members = ty_members(me, source);
    infer_from_signature_lists(
        me, inf, source_members->call, source_members->call_count, target_members->call, target_members->call_count
    );
    infer_from_signature_lists(
        me, inf, source_members->construct, source_members->construct_count,
        target_members->construct, target_members->construct_count
    );
    for (u32 i = 0; i < target_members->index_count; i++) {
        IndexInfo const* target_info = &target_members->index[i];
        bool found = false;
        for (u32 j = 0; j < source_members->index_count; j++) {
            if (source_members->index[j].key == target_info->key) {
                infer_from(me, inf, source_members->index[j].value, target_info->value);
                found = true;
            }
        }
        if (!found && inferable_index(me, source, 0)) {
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
                    Ty const* type = found_prop.type;
                    if ((found_prop.flags & PROP_OPTIONAL) && !compiler_options()->exact_optional_property_types) {
                        type = ty_without_undefined(me, type);
                    }
                    ty_buf_push(&types, type);
                }
            }
            if (types.len) infer_from(me, inf, ty_union(me, types.data, types.len, nullptr, (TyList){0}), target_info->value);
            ty_buf_drop(&types);
            ty_buf_drop(&seen);
        }
    }
}

// tsc's isTypeIdenticalTo, pointers first (structured types of the same kind can be identical without being the same)
static bool is_identical(Checker* me, Ty const* source, Ty const* target) {
    if (source == target) return true;
    if (source->kind != target->kind || source->kind == TY_INTRINSIC || source->kind == TY_LITERAL || source->kind == TY_TYPE_PARAM) {
        return false;
    }
    return ty_is_related(me, source, target, TY_RELATION_IDENTITY);
}

static bool is_identical_or_base(Checker* me, Ty const* source, Ty const* target) {
    return is_identical(me, source, target) ||
        ((target->flags & TYF_STRING) && (source->flags & TYF_STRING_LITERAL)) ||
        ((target->flags & TYF_NUMBER) && (source->flags & TYF_NUMBER_LITERAL));
}

static bool is_closely_matched(Ty const* source, Ty const* target) {
    if (source->kind == TY_REFERENCE && target->kind == TY_REFERENCE && source->reference.target == target->reference.target) {
        return true;
    }
    return source->alias && target->alias_args.len && source->alias == target->alias;
}

static void infer_to_union(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    TyList sources = source->kind == TY_UNION ? source->composite.types : (TyList){ .data = &source, .len = 1 };
    TyList targets = target->composite.types;
    bool* source_done = calloc(sources.len + targets.len, sizeof(bool));
    panic_if(!source_done, "out of memory");
    bool* target_done = source_done + sources.len;

    // Identical members cancel out, then closely matched ones are inferred between and cancel out
    for (u32 pass = 0; pass < 2; pass++) {
        for (u32 t = 0; t < targets.len; t++) {
            if (target_done[t]) continue;
            for (u32 s = 0; s < sources.len; s++) {
                if (source_done[s] && pass == 1) continue;
                bool matches = pass == 0 ?
                    is_identical_or_base(me, sources.data[s], targets.data[t]) :
                    is_closely_matched(sources.data[s], targets.data[t]);
                if (!matches) continue;
                if (pass == 1) infer_from(me, inf, sources.data[s], targets.data[t]);
                source_done[s] = true;
                target_done[t] = true;
            }
        }
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

    // To what isn't a naked type parameter first, remembering which sources that used up
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
            if (inf->hit_priority == inf->priority) source_done[s] = true;
            if (saved < inf->hit_priority) inf->hit_priority = saved;
        }
    }
    if (naked_count) {
        Ty const** unmatched = malloc(sizeof(Ty const*) * sources.len);
        u32 unmatched_len = 0;
        for (u32 s = 0; s < sources.len; s++) {
            if (!source_done[s]) unmatched[unmatched_len++] = sources.data[s];
        }
        if (naked_count == 1) {
            if (unmatched_len) infer_from(me, inf, ty_union(me, unmatched, unmatched_len, nullptr, (TyList){0}), naked);
        } else {
            for (u32 t = 0; t < targets.len; t++) {
                if (!target_done[t] && inference_param(inf, targets.data[t])) {
                    infer_with_priority(me, inf, source, targets.data[t], INFER_PRIORITY_NAKED_TYPE_VARIABLE);
                }
            }
        }
        free(unmatched);
    }
    free(source_done);
}

// tsc's inferFromTypes on an intersection target with a type variable of the inference in it: members identical in
// source and target cancel out first (`string & T` from `string & U` infers T from U, not from `string & U`). Returns
// whether anything is left to infer between, in *source / *target
static bool reduce_intersection_target(Checker* me, Inference* inf, Ty const** source, Ty const** target) {
    TyList targets = (*target)->composite.types;
    bool any_param = false;
    for (u32 i = 0; i < targets.len && !any_param; i++) any_param = inference_param(inf, targets.data[i]) != nullptr;
    if (!any_param) return true;
    TyList sources = (*source)->kind == TY_INTERSECTION ? (*source)->composite.types : (TyList){ .data = source, .len = 1 };
    TyBuf kept_sources = {0};
    TyBuf kept_targets = {0};
    for (u32 i = 0; i < sources.len; i++) {
        bool matched = false;
        for (u32 j = 0; j < targets.len && !matched; j++) matched = is_identical(me, sources.data[i], targets.data[j]);
        if (!matched) ty_buf_push(&kept_sources, sources.data[i]);
    }
    for (u32 j = 0; j < targets.len; j++) {
        bool matched = false;
        for (u32 i = 0; i < sources.len && !matched; i++) matched = is_identical(me, sources.data[i], targets.data[j]);
        if (!matched) ty_buf_push(&kept_targets, targets.data[j]);
    }
    bool left = kept_sources.len && kept_targets.len;
    if (left && kept_targets.len < targets.len) {
        *source = ty_intersection(me, kept_sources.data, kept_sources.len, nullptr, (TyList){0});
        *target = ty_intersection(me, kept_targets.data, kept_targets.len, nullptr, (TyList){0});
    }
    ty_buf_drop(&kept_sources);
    ty_buf_drop(&kept_targets);
    return left;
}

static void infer_from(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    if (!(target->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return;
    if (source->alias && source->alias == target->alias) {
        // Two instantiations of one type alias: by type argument, with the alias's variances (tsc). Without type
        // arguments the two are the same type, with nothing to infer
        for (u32 i = 0; i < source->alias_args.len && i < target->alias_args.len; i++) {
            if (!(target->alias_args.data[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) continue;
            bool flip = ty_alias_param_is_contravariant(me, source->alias, i);
            if (flip) inf->contravariant = !inf->contravariant;
            infer_from(me, inf, source->alias_args.data[i], target->alias_args.data[i]);
            if (flip) inf->contravariant = !inf->contravariant;
        }
        return;
    }
    if (target->kind == TY_INTERSECTION && source->kind != TY_UNION && !reduce_intersection_target(me, inf, &source, &target)) return;
    if (target->kind == TY_TYPE_PARAM || (target->kind == TY_INDEXED && inference_param(inf, target))) {
        InferParam* param = inference_param(inf, target);
        if (!param || param->fixed || (source->flags & TYF_ERROR) || (source->flags & TYF_NON_INFERRABLE)) return;
        if (inf->priority < inf->hit_priority) inf->hit_priority = inf->priority;
        if (!param->has_priority || inf->priority < param->priority) {
            param->co_len = 0;
            param->contra_len = 0;
            param->top_level = true;
            param->priority = inf->priority;
            param->has_priority = true;
        }
        if (inf->priority == param->priority) {
            if (inf->contravariant && !inf->bivariant) {
                candidates_push(&param->contra, &param->contra_len, source);
            } else {
                candidates_push(&param->co, &param->co_len, source);
            }
            if (!(inf->priority & INFER_PRIORITY_RETURN_TYPE) && param->top_level && inf->original_target &&
                !is_param_at_top_level(inf->original_target, target)
            ) {
                param->top_level = false;
            }
        }
        return;
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
    // An object pair on the stack is in `visited` too, which stops it below
    for (u32 i = 0; i < inf->depth && !(target->flags & TYF_OBJECT); i++) {
        if (inf->visiting_sources[i] == source && inf->visiting_targets[i] == target) return;
    }
    inf->visiting_sources[inf->depth] = source;
    inf->visiting_targets[inf->depth] = target;
    inf->depth++;
    if (target->kind == TY_UNION) {
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
        }
    } else if (source->kind == TY_STRING_MAPPING && target->kind == TY_STRING_MAPPING) {
        if (source->string_mapping.mapping == target->string_mapping.mapping) {
            infer_from(me, inf, source->string_mapping.type, target->string_mapping.type);
        }
    } else if (target->kind == TY_TEMPLATE_LITERAL) {
        infer_to_template(me, inf, source, target);
    } else if (target->kind == TY_CONDITIONAL) {
        if (source->kind == TY_CONDITIONAL) {
            infer_from(me, inf, source->conditional.check, target->conditional.check);
            infer_from(me, inf, source->conditional.extends, target->conditional.extends);
            infer_from(me, inf, ty_conditional_branch(me, source, true), ty_conditional_branch(me, target, true));
            infer_from(me, inf, ty_conditional_branch(me, source, false), ty_conditional_branch(me, target, false));
        } else {
            infer_from(me, inf, source, ty_conditional_branch(me, target, true));
            infer_from(me, inf, source, ty_conditional_branch(me, target, false));
        }
    } else if (target->flags & TYF_OBJECT) {
        if (!(source->flags & (TYF_OBJECT | TYF_INTERSECTION))) {
            Ty const* apparent = ty_apparent_type(me, source);
            if (apparent != source && !(source->flags & (TYF_ANY | TYF_UNKNOWN | TYF_NEVER))) source = apparent;
        }
        if ((source->flags & (TYF_OBJECT | TYF_INTERSECTION)) && visited_add(inf, source, target)) {
            unsigned saved_expanding = inf->expanding;
            if (!(inf->expanding & 1) && ty_is_deeply_nested(source, inf->object_sources, inf->object_depth)) inf->expanding |= 1;
            if (!(inf->expanding & 2) && ty_is_deeply_nested(target, inf->object_targets, inf->object_depth)) inf->expanding |= 2;
            if (inf->expanding != 3) {
                inf->object_sources[inf->object_depth] = source;
                inf->object_targets[inf->object_depth] = target;
                inf->object_depth++;
                infer_from_objects(me, inf, source, target);
                inf->object_depth--;
            }
            inf->expanding = saved_expanding;
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
        memset(inf->visited, 0, sizeof(u64) * inf->visited_cap);
        inf->visited_len = 0;
    }
    infer_from(me, inf, source, target);
}

// Results

// tsc's literalTypesWithSameBaseType: literals (or unions of literals) of one primitive, or members of one enum
static bool literal_base(Ty const* ty, u32* common, Symbol const** enum_decl, bool* first) {
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!literal_base(ty->composite.types.data[i], common, enum_decl, first)) return false;
        }
        return ty->composite.types.len > 0;
    }
    if (ty->kind != TY_LITERAL) return false;
    Symbol const* decl = ty->flags & TYF_ENUM_LITERAL ? ty->literal.enum_decl : nullptr;
    if (!*first && decl != *enum_decl) return false;
    *enum_decl = decl;
    *first = false;
    *common &= ty->flags;
    return decl || *common != 0;
}

static bool is_literal_of_same_base(TyList types) {
    u32 common = TYF_LITERAL;
    Symbol const* enum_decl = nullptr;
    bool first = true;
    for (u32 i = 0; i < types.len; i++) {
        if (!literal_base(types.data[i], &common, &enum_decl, &first)) return false;
    }
    return types.len > 0;
}

// tsc's getCommonSupertype: the leftmost candidate that every other one is a subtype of. null and undefined stay out
// of the contest and are added back
static Ty const* common_supertype(Checker* me, Ty const* const* types, u32 len) {
    if (len == 1) return types[0];
    Ty const** primary = malloc(sizeof(Ty const*) * len * 2);
    Ty const** nullable = primary + len;
    u32 primary_len = 0;
    u32 nullable_len = 0;
    for (u32 i = 0; i < len; i++) {
        if (types[i]->flags & TYF_NULLABLE) nullable[nullable_len++] = types[i]; else primary[primary_len++] = types[i];
    }
    Ty const* ret;
    if (!primary_len) {
        ret = ty_union(me, types, len, nullptr, (TyList){0});
    } else {
        if (is_literal_of_same_base((TyList){ .data = primary, .len = primary_len })) {
            ret = ty_union(me, primary, primary_len, nullptr, (TyList){0});
        } else {
            ret = primary[0];
            for (u32 i = 1; i < primary_len; i++) {
                if (ret != primary[i] && ty_is_related(me, ret, primary[i], TY_RELATION_SUBTYPE)) ret = primary[i];
            }
        }
        if (nullable_len && compiler_options()->strict_null_checks) {
            nullable[nullable_len++] = ret; // there's room: at least one slot of `primary` isn't nullable
            ret = ty_union(me, nullable, nullable_len, nullptr, (TyList){0});
        }
    }
    free(primary);
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
static bool is_literal_candidate(Inference const* inf, Ty const* candidate) {
    if (candidate->kind == TY_ANONYMOUS && candidate->anonymous.object_literal) return true;
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
    InferParam* param = &inf->params[i];
    if (param->fixed) return param->fixed;
    Ty const* co = nullptr;
    if (param->co_len) {
        Ty const** candidates = malloc(sizeof(Ty const*) * param->co_len);
        // tsc's getCovariantInference: under a primitive constraint the candidates are regular, else (fresh) literals
        // widen if every inference was at the top level and keep_literals doesn't say otherwise
        // (So does a `const` type parameter)
        bool primitive = has_primitive_constraint(me, param->param) || ty_is_const_type_variable(me, param->param, 0);
        bool widen = !keep_literals && !primitive && param->top_level;
        for (u32 j = 0; j < param->co_len; j++) {
            Ty const* candidate = param->co[j];
            candidates[j] = primitive ? ty_regular(me, candidate) : widen ? ty_widened_literal(me, candidate) : candidate;
        }
        // Several object literals are one candidate, their union (tsc's unionObjectAndArrayLiteralCandidates; array
        // literals aren't told apart from other arrays here)
        u32 len = param->co_len;
        if (len > 1) {
            Ty const** literals = malloc(sizeof(Ty const*) * len);
            u32 literal_count = 0;
            for (u32 j = 0; j < len; j++) {
                if (is_literal_candidate(inf, candidates[j])) literals[literal_count++] = candidates[j];
            }
            if (literal_count > 1) {
                // (Where the first one was: tsc puts it last, which only matters for ties)
                Ty const* merged = ty_union_subtype_reduced(me, literals, literal_count);
                u32 kept = 0;
                bool placed = false;
                for (u32 j = 0; j < len; j++) {
                    bool literal = is_literal_candidate(inf, candidates[j]);
                    if (!literal) candidates[kept++] = candidates[j];
                    else if (!placed) candidates[kept++] = merged, placed = true;
                }
                len = kept;
            }
            free(literals);
        }
        co = (param->priority & INFER_PRIORITY_RETURN_TYPE) ?
            ty_union(me, candidates, len, nullptr, (TyList){0}) :
            common_supertype(me, candidates, len);
        co = ty_widened_objects(me, co); // (tsc's getWidenedType of the inference)
        free(candidates);
    }
    Ty const* contra = param->contra_len ? common_subtype(me, param->contra, param->contra_len) : nullptr;
    if (co && contra) {
        // The covariant inference wins if it fits what the contravariant positions ask for
        // ... and neither its own candidates (it isn't one of a conflicting set) nor those of a type parameter constrained
        // to this one have one it wouldn't cover
        if (co->flags & (TYF_NEVER | TYF_ANY)) return contra;
        bool fits_some = false;
        for (u32 j = 0; j < param->contra_len; j++) {
            fits_some |= ty_is_related(me, co, param->contra[j], TY_RELATION_ASSIGNABLE);
        }
        if (!fits_some) return contra;
        for (u32 j = 0; j < inf->len; j++) {
            InferParam const* other = &inf->params[j];
            if (other != param && ty_param_constraint(me, other->param) != param->param) continue;
            for (u32 k = 0; k < other->co_len; k++) {
                if (!ty_is_related(me, other->co[k], co, TY_RELATION_ASSIGNABLE)) return contra;
            }
        }
        return co;
    }
    return co ? co : contra;
}

Ty const* inference_conditional_result(Checker* me, Inference* inf, u32 i) {
    InferParam* param = &inf->params[i];
    if (param->co_len) return ty_union(me, param->co, param->co_len, nullptr, (TyList){0});
    if (param->contra_len) return ty_intersection(me, param->contra, param->contra_len, nullptr, (TyList){0});
    return nullptr;
}

// Inferred type arguments

// The mapper for what has been inferred so far: common supertypes of the candidates, then defaults, then constraints
// `fixing`: for the contextual type of a context-sensitive argument, which decides the type parameters it mentions
TyMapper const* inference_mapper(Checker* me, Signature const* sig, Inference* inf, bool fixing) {
    TyList params = sig->type_params;
    Ty const* return_type = signature_return_type(me, sig);
    Ty const** results = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    bool* inferred = calloc(params.len ? params.len : 1, sizeof(bool));
    for (u32 i = 0; i < params.len; i++) {
        // tsc: literals widen unless the type parameter is at the top level of the return type and hasn't been fixed
        // (or the constraint is primitive, see inference_call_result())
        bool keep_literals = !fixing && !inf->params[i].fixed && is_param_at_top_level(return_type, params.data[i]);
        results[i] = inference_call_result(me, inf, i, keep_literals);
        inferred[i] = results[i] != nullptr;
        if (!results[i]) results[i] = params.data[i]; // stands for itself until the defaults are in, below
    }
    TyMapper const* mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    for (u32 i = 0; i < params.len; i++) {
        if (inferred[i]) continue;
        Ty const* fallback = ty_param_default(me, params.data[i]);
        results[i] = fallback ? ty_instantiate(me, fallback, mapper) : ty_intrinsic(me, TY_UNKNOWN_);
        if (results[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
            // A default that mentions a type parameter that has none itself
            bool self = false;
            for (u32 j = 0; j < params.len; j++) self |= !inferred[j] && results[i] == params.data[j];
            if (self) results[i] = ty_intrinsic(me, TY_UNKNOWN_);
        }
        mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    }
    // An inference that doesn't satisfy the constraint is replaced by it, and the argument check reports the rest
    bool changed = false;
    for (u32 i = 0; i < params.len; i++) {
        Ty const* constraint = ty_param_constraint(me, params.data[i]);
        if (!constraint || constraint == params.data[i]) continue;
        Ty const* instantiated = ty_instantiate(me, constraint, mapper);
        bool fits = inf->within_relation ? ty_is_related_within(me, results[i], instantiated) :
            ty_is_related(me, results[i], instantiated, TY_RELATION_ASSIGNABLE);
        if (!fits) {
            results[i] = instantiated;
            changed = true;
        }
    }
    if (changed) mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    free(inferred);
    return mapper;
}
