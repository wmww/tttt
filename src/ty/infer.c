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
    if (ty->kind != TY_TYPE_PARAM) return nullptr;
    for (u32 i = 0; i < inf->len; i++) {
        if (inf->params[i].param == ty) return &inf->params[i];
    }
    if (inf->collect_infer_params && ty->param.state->is_infer) {
        if (inf->len == inf->cap) {
            inf->cap *= 2;
            inf->params = realloc(inf->params, inf->cap * sizeof(InferParam));
            panic_if(!inf->params, "out of memory");
        }
        inf->params[inf->len] = (InferParam){ .param = ty, .top_level = true };
        return &inf->params[inf->len++];
    }
    return nullptr;
}

// Every `infer X` the target declares, whether or not the walk with the source reaches it: one that gets no candidate
// still has to be replaced (by `unknown`) for the conditional type to resolve
static void collect_infer_params(Checker* me, Inference* inf, Ty const* target, u32 depth) {
    if (depth > 6 || !(target->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return;
    switch ((TyKind)target->kind) {
        case TY_TYPE_PARAM:
            inference_param(inf, target);
            break;
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < target->composite.types.len; i++) {
                collect_infer_params(me, inf, target->composite.types.data[i], depth + 1);
            }
            break;
        case TY_REFERENCE: {
            TyList args = ty_reference_args(me, target);
            for (u32 i = 0; i < args.len; i++) collect_infer_params(me, inf, args.data[i], depth + 1);
        }   break;
        case TY_ANONYMOUS: {
            Members const* members = ty_members(me, target);
            for (u32 i = 0; i < members->prop_count; i++) {
                collect_infer_params(me, inf, prop_type(me, members->props[i]), depth + 1);
            }
            for (u32 kind = 0; kind < 2; kind++) {
                Signature* const* list = kind ? members->construct : members->call;
                u32 count = kind ? members->construct_count : members->call_count;
                for (u32 i = 0; i < count; i++) {
                    signature_resolve(me, list[i]);
                    for (u32 j = 0; j < list[i]->param_count; j++) {
                        collect_infer_params(me, inf, list[i]->params[j].type, depth + 1);
                    }
                    collect_infer_params(me, inf, signature_return_type(me, list[i]), depth + 1);
                }
            }
            for (u32 i = 0; i < members->index_count; i++) collect_infer_params(me, inf, members->index[i].value, depth + 1);
        }   break;
        case TY_KEYOF:
            collect_infer_params(me, inf, target->keyof.operand, depth + 1);
            break;
        case TY_INDEXED:
            collect_infer_params(me, inf, target->indexed.object, depth + 1);
            collect_infer_params(me, inf, target->indexed.index, depth + 1);
            break;
        default: break;
    }
}

void inference_collect_infer_params(Checker* me, Inference* inf, Ty const* target) {
    collect_infer_params(me, inf, target, 0);
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
    inf->priority = priority > saved ? priority : saved;
    infer_from(me, inf, source, target);
    inf->priority = saved;
}

static u32 candidate_count(Inference const* inf) {
    u32 count = 0;
    for (u32 i = 0; i < inf->len; i++) count += inf->params[i].co_len + inf->params[i].contra_len;
    return count;
}

// A type parameter of the source signature isn't something to infer: it stands for its constraint (tsc's
// getBaseSignature, roughly)
static TyMapper const* base_signature_mapper(Checker* me, Signature const* sig) {
    if (!sig->type_params.len) return nullptr;
    Ty const** targets = arena_new_n(&my arena, Ty const*, sig->type_params.len);
    for (u32 i = 0; i < sig->type_params.len; i++) {
        Ty const* constraint = ty_param_constraint(me, sig->type_params.data[i]);
        targets[i] = constraint && !(constraint->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) ?
            constraint : ty_intrinsic(me, TY_UNKNOWN_);
    }
    return ty_mapper_new(me, sig->type_params, (TyList){ .data = targets, .len = sig->type_params.len });
}

static void infer_from_signatures(Checker* me, Inference* inf, Signature const* source, Signature const* target) {
    signature_resolve(me, source);
    signature_resolve(me, target);
    TyMapper const* base = base_signature_mapper(me, source);
    // Parameters are a contravariant position. Without strictFunctionTypes (and for methods) they compare
    // bivariantly, and tsc infers as if they were covariant
    bool flip = compiler_options()->strict_function_types && !(target->flags & SIGNATURE_METHOD);
    if (flip) inf->contravariant = !inf->contravariant;
    for (u32 i = 0; i < target->param_count; i++) {
        SigParam const* target_param = &target->params[i];
        if (target_param->rest) {
            // `(...args: A) => any`: the rest of the source's parameters as a tuple
            if (i < source->param_count && source->params[i].rest && i + 1 == source->param_count) {
                infer_from(me, inf, ty_instantiate(me, source->params[i].type, base), target_param->type);
            } else {
                u32 arity = source->param_count > i ? source->param_count - i : 0;
                unsigned char* flags = calloc(arity ? arity : 1, 1);
                Atom const** names = calloc(arity ? arity : 1, sizeof(Atom const*));
                Ty const** elements = calloc(arity ? arity : 1, sizeof(Ty const*));
                for (u32 j = 0; j < arity; j++) {
                    SigParam const* source_param = &source->params[i + j];
                    flags[j] = (source_param->optional ? TUPLE_ELEMENT_OPTIONAL : 0) | (source_param->rest ? TUPLE_ELEMENT_REST : 0);
                    names[j] = source_param->name;
                    elements[j] = ty_instantiate(me, source_param->type, base);
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
        Ty const* source_type = source->params[i].rest ?
            ty_rest_element_type(me, source->params[i].type) : source->params[i].type;
        infer_from(me, inf, ty_instantiate(me, source_type, base), target_param->type);
    }
    if (flip) inf->contravariant = !inf->contravariant;
    TyPredicate const* source_predicate = signature_predicate(me, source);
    TyPredicate const* target_predicate = signature_predicate(me, target);
    if (source_predicate && target_predicate && source_predicate->type && target_predicate->type &&
        source_predicate->is_this == target_predicate->is_this && source_predicate->asserts == target_predicate->asserts
    ) {
        infer_from(me, inf, ty_instantiate(me, source_predicate->type, base), target_predicate->type);
        return;
    }
    infer_from(me, inf, ty_instantiate(me, signature_return_type(me, source), base), signature_return_type(me, target));
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

static void infer_from_objects(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    if ((ty_is_tuple(source) || is_empty_tuple(source)) && ty_is_tuple(target)) {
        TupleShape const* source_shape = ty_is_tuple(source) ? source->reference.target->declared.tuple : source->declared.tuple;
        TupleShape const* target_shape = target->reference.target->declared.tuple;
        TyList source_args = ty_is_tuple(source) ? ty_reference_args(me, source) : (TyList){0};
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
    if (target_members->mapped) {
        // `{ [K in keyof T]: X<T[K]> }`. TODO: reverse-mapped inference. For a plain homomorphic `T[K]` template
        // (Partial, Readonly...) the source itself is the candidate
        MappedInfo const* mapped = target_members->mapped;
        if (mapped->modifiers_type && mapped->template->kind == TY_INDEXED &&
            mapped->template->indexed.object == mapped->modifiers_type && mapped->template->indexed.index == mapped->type_param
        ) {
            infer_from(me, inf, source, mapped->modifiers_type);
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
        if (!found && (target_info->key->flags & TYF_STRING) && source->kind == TY_ANONYMOUS && source_members->prop_count) {
            // An object literal against `{ [key: string]: T }`: the union of its property types
            Ty const** types = malloc(sizeof(Ty const*) * source_members->prop_count);
            for (u32 j = 0; j < source_members->prop_count; j++) types[j] = prop_type(me, source_members->props[j]);
            infer_from(me, inf, ty_union(me, types, source_members->prop_count, nullptr, (TyList){0}), target_info->value);
            free(types);
        }
    }
}

static bool is_identical_or_base(Ty const* source, Ty const* target) {
    return source == target ||
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
                    is_identical_or_base(sources.data[s], targets.data[t]) :
                    is_closely_matched(sources.data[s], targets.data[t]);
                if (!matches) continue;
                if (pass == 1) infer_from(me, inf, sources.data[s], targets.data[t]);
                source_done[s] = true;
                target_done[t] = true;
            }
        }
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
            u32 before = candidate_count(inf);
            infer_from(me, inf, sources.data[s], targets.data[t]);
            if (candidate_count(inf) != before) source_done[s] = true;
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

static void infer_from(Checker* me, Inference* inf, Ty const* source, Ty const* target) {
    if (!(target->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return;
    if (target->kind == TY_TYPE_PARAM) {
        InferParam* param = inference_param(inf, target);
        if (!param || param->fixed || (source->flags & TYF_ERROR) || source == ty_intrinsic(me, TY_SILENT_NEVER)) return;
        if (!param->has_priority || inf->priority < param->priority) {
            param->co_len = 0;
            param->contra_len = 0;
            param->top_level = true;
            param->priority = inf->priority;
            param->has_priority = true;
        }
        if (inf->priority == param->priority) {
            if (inf->contravariant) {
                candidates_push(&param->contra, &param->contra_len, source);
            } else {
                candidates_push(&param->co, &param->co_len, source);
            }
            if (inf->priority != INFER_PRIORITY_RETURN_TYPE && param->top_level && inf->original_target &&
                !is_param_at_top_level(inf->original_target, target)
            ) {
                param->top_level = false;
            }
        }
        return;
    }
    if ((target->flags & TYF_DEFERRED) && source->alias && source->alias == target->alias &&
        source->alias_args.len == target->alias_args.len
    ) {
        // Two instantiations of one alias whose structure can't be walked: by type argument. TODO: tsc does this for
        // every alias, with the variances of the alias deciding the direction (there are none for aliases here yet)
        for (u32 i = 0; i < source->alias_args.len; i++) {
            infer_from(me, inf, source->alias_args.data[i], target->alias_args.data[i]);
        }
        return;
    }
    if (inf->depth == INFER_MAX_DEPTH) return;
    for (u32 i = 0; i < inf->depth; i++) {
        if (inf->visiting_sources[i] == source && inf->visiting_targets[i] == target) return;
    }
    inf->visiting_sources[inf->depth] = source;
    inf->visiting_targets[inf->depth] = target;
    inf->depth++;
    if (target->kind == TY_UNION) {
        infer_to_union(me, inf, source, target);
    } else if (target->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < target->composite.types.len; i++) {
            infer_from(me, inf, source, target->composite.types.data[i]);
        }
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
            infer_from_objects(me, inf, source, target);
        }
    }
    inf->depth--;
}

void infer_types(Checker* me, Inference* inf, Ty const* source, Ty const* target, u32 priority) {
    inf->priority = priority;
    inf->contravariant = false;
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
                if (ret != primary[i] && ty_is_related(me, ret, primary[i], TY_RELATION_STRICT_SUBTYPE)) ret = primary[i];
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
        if (ret != types[i] && ty_is_related(me, types[i], ret, TY_RELATION_STRICT_SUBTYPE)) ret = types[i];
    }
    return ret;
}

Ty const* inference_call_result(Checker* me, Inference* inf, u32 i, bool keep_literals) {
    InferParam* param = &inf->params[i];
    if (param->fixed) return param->fixed;
    Ty const* co = nullptr;
    if (param->co_len) {
        Ty const** candidates = malloc(sizeof(Ty const*) * param->co_len);
        bool widen = !keep_literals && param->top_level;
        for (u32 j = 0; j < param->co_len; j++) {
            candidates[j] = widen ? ty_widened_literal(me, param->co[j]) : param->co[j];
        }
        co = param->priority == INFER_PRIORITY_RETURN_TYPE ?
            ty_union(me, candidates, param->co_len, nullptr, (TyList){0}) :
            common_supertype(me, candidates, param->co_len);
        free(candidates);
    }
    Ty const* contra = param->contra_len ? common_subtype(me, param->contra, param->contra_len) : nullptr;
    if (co && contra) {
        // The covariant inference wins if it fits what the contravariant positions ask for
        // ... and no type parameter constrained to this one has a candidate it wouldn't cover
        if (co->flags & (TYF_NEVER | TYF_ANY)) return contra;
        bool fits_some = false;
        for (u32 j = 0; j < param->contra_len; j++) {
            fits_some |= ty_is_related(me, co, param->contra[j], TY_RELATION_ASSIGNABLE);
        }
        if (!fits_some) return contra;
        for (u32 j = 0; j < inf->len; j++) {
            InferParam const* other = &inf->params[j];
            if (other == param || ty_param_constraint(me, other->param) != param->param) continue;
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
