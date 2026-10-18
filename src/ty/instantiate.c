#include "scope.h"
#include "ty/ty_internal.h"
#include "error.h"
#include "program.h"

// Instantiation never copies a type's structure. TypeNode parameters map to their targets, interned types get rebuilt
// through their constructors (and so come out as the existing type if there is one), and object types become a
// {target, mapper} pair whose members are instantiated when somebody asks for them.

// tsc's limits
#define MAX_INSTANTIATION_DEPTH 100
#define MAX_TAIL_COUNT 1000

// The branch a conditional type resolved to: when it is a conditional type itself, it doesn't count towards the
// instantiation depth (see Checker.instantiation_tail_count)
Span ty_current_node_span(Checker* me) {
    if (my current_node.expr) return expr_error_span(my current_node.expr);
    return span_valid(my current_node.error_span) ? my current_node.error_span : my current_node.span;
}

void ty_error_at(Checker* me, Span span, unsigned ts_code, cstr message) {
    ErrorCtx* errs = my errs;
    if (errs->phase != ERROR_PHASE_TYPECHECK) {
        cstr_drop(&message);
    } else if (span_valid(span)) {
        error_ctx_error_s(errs, span, ts_code, message);
    } else {
        error_ctx_global_error_s(errs, ts_code, message);
    }
}

void ty_error_at_current_node(Checker* me, unsigned ts_code, cstr message) {
    ty_error_at(me, ty_current_node_span(me), ts_code, message);
}

// The symbol tsc names a type on the instantiation stack by (its alias's, else its own), null for none or an internal one
static Symbol const* instantiated_symbol(Ty const* ty) {
    if (ty->alias) return ty->alias;
    if (ty->kind == TY_REFERENCE) ty = ty->reference.target;
    return ty->kind == TY_DECLARED ? ty->declared.symbol : nullptr;
}

void ty_instantiation_limit_reached(Checker* me) {
    u32 depth = my instantiation_depth < c_arraylen(my instantiation_stack) ? my instantiation_depth : (u32)c_arraylen(my instantiation_stack);
    ty_instantiation_limit_reached_on(me, my instantiation_stack, depth);
}

void ty_instantiation_limit_reached_on(Checker* me, Ty const* const* stack, u32 depth) {
    my instantiation_limit_hits++;
    // tsc's getCircularTypeNames: the types three times on the stack
    // (Counted in one pass, like tsc's typeCounts map: the depth is at most 100, the table has room for all of them)
    Symbol const* names[8];
    u32 name_count = 0;
    struct { Ty const* ty; u32 count; } counts[256] = {0};
    for (u32 i = 0; i < depth && name_count < c_arraylen(names); i++) {
        Ty const* ty = stack[i];
        u32 h = (u32)(((uintptr_t)ty >> 4) * 0x9E3779B1u) & (c_arraylen(counts) - 1);
        while (counts[h].ty && counts[h].ty != ty) h = (h + 1) & (c_arraylen(counts) - 1);
        counts[h].ty = ty;
        if (++counts[h].count != 3) continue;
        Symbol const* symbol = instantiated_symbol(ty);
        if (!symbol || !symbol->name.size) continue;
        bool dup = false;
        for (u32 k = 0; k < name_count && !dup; k++) dup = csview_eq(&names[k]->name, &symbol->name);
        if (!dup) names[name_count++] = symbol;
    }
    if (name_count == 1) {
        ty_error_at_current_node_d(me, 5114, "Instantiations of type '", names[0]->name, "' appear infinitely circular.");
    } else if (name_count > 1) {
        cstr list = cstr_init();
        for (u32 k = 0; k < name_count; k++) {
            if (k) cstr_append(&list, ", ");
            cstr_append(&list, "'");
            cstr_append_sv(&list, names[k]->name);
            cstr_append(&list, "'");
        }
        ty_error_at_current_node_d(me, 5115, "Instantiations of the following types appear infinitely circular: ", list, ".");
        cstr_drop(&list);
    } else {
        ty_error_at_current_node_d(me, 2589, "Type instantiation is excessively deep and possibly infinite.");
    }
}

Ty const* ty_instantiate_tail(Checker* me, Ty const* ty, TyMapper const* mapper) {
    my instantiating_tail = true;
    return ty_instantiate(me, ty, mapper);
}
#define MAX_INSTANTIATION_COUNT 5000000

// A copy of a list for a mapper to keep, the one made before for the same types while it is in the cache
static Ty const* const* mapper_list(Checker* me, Ty const* const* data, u32 len) {
    if (!len) return nullptr;
    u64 hash = len;
    for (u32 i = 0; i < len; i++) hash = (hash ^ (uintptr_t)data[i]) * 0x9e3779b97f4a7c15ull;
    TyList* slot = &my mapper_lists[hash >> 54];
    if (slot->len == len && !memcmp(slot->data, data, sizeof(*data) * len)) return slot->data;
    *slot = ty_list_copy(me, data, len);
    return slot->data;
}

TyMapper const* ty_mapper_new(Checker* me, TyList sources, TyList targets) {
    check(sources.len == targets.len);
    TyMapper* ret = arena_new(&my arena, TyMapper);
    ret->len = sources.len;
    ret->sources = mapper_list(me, sources.data, sources.len);
    ret->targets = mapper_list(me, targets.data, targets.len);
    return ret;
}

// inner, then outer: what inner maps to gets instantiated by outer, and outer's own mappings are carried along
TyMapper const* ty_compose_mappers(Checker* me, TyMapper const* inner, TyMapper const* outer) {
    if (!inner || !inner->len) return outer;
    u32 cap = inner->len + outer->len;
    Ty const** pairs = malloc(sizeof(Ty const*) * cap * 2);
    Ty const** sources = pairs;
    Ty const** targets = pairs + cap;
    u32 len = 0;
    for (u32 i = 0; i < inner->len; i++) {
        sources[len] = inner->sources[i];
        targets[len++] = ty_instantiate(me, inner->targets[i], outer);
    }
    for (u32 i = 0; i < outer->len; i++) {
        bool shadowed = false;
        for (u32 j = 0; j < inner->len; j++) shadowed |= inner->sources[j] == outer->sources[i];
        if (!shadowed) {
            sources[len] = outer->sources[i];
            targets[len++] = outer->targets[i];
        }
    }
    TyMapper const* ret = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    free(pairs);
    return ret;
}

// A conditional type's own `infer` type parameters aren't mapped by whoever instantiates it (tsc's conditional root
// maps its outer type parameters only): the recursive `Split<B, D>` in the true branch of `S extends `${infer A}.${infer
// B}` ? [A, ...Split<B, D>] : [S]` gets the outer B for its check type, not for its own `infer B`
TyList ty_conditional_own_infers(Checker* me, Ty const* root) {
    (void)me;
    return root->conditional.outer ? root->conditional.outer->infers : (TyList){0};
}

TyMapper const* ty_mapper_without(Checker* me, TyMapper const* mapper, TyList params) {
    if (!params.len || !mapper) return mapper;
    u32 dropped = 0;
    for (u32 i = 0; i < mapper->len; i++) {
        for (u32 j = 0; j < params.len; j++) dropped += mapper->sources[i] == params.data[j];
    }
    if (!dropped) return mapper;
    Ty const** pairs = malloc(sizeof(Ty const*) * mapper->len * 2);
    panic_if(!pairs, "out of memory");
    Ty const** sources = pairs;
    Ty const** targets = pairs + mapper->len;
    u32 len = 0;
    for (u32 i = 0; i < mapper->len; i++) {
        bool own = false;
        for (u32 j = 0; j < params.len; j++) own |= mapper->sources[i] == params.data[j];
        if (own) continue;
        sources[len] = mapper->sources[i];
        targets[len++] = mapper->targets[i];
    }
    TyMapper const* ret = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    free(pairs);
    return ret;
}

static TyMapper const* without_own_infers(Checker* me, Ty const* root, TyMapper const* mapper) {
    return ty_mapper_without(me, mapper, ty_conditional_own_infers(me, root));
}

// The mapper with one more (or one replaced) mapping
// The mapper with source mapped to target instead of whatever it mapped it to
TyMapper const* ty_mapper_with(Checker* me, TyMapper const* mapper, Ty const* source, Ty const* target) {
    u32 cap = mapper->len + 1;
    Ty const** pairs = malloc(sizeof(Ty const*) * cap * 2);
    Ty const** sources = pairs;
    Ty const** targets = pairs + cap;
    u32 len = 0;
    for (u32 i = 0; i < mapper->len; i++) {
        if (mapper->sources[i] == source) continue;
        sources[len] = mapper->sources[i];
        targets[len++] = mapper->targets[i];
    }
    sources[len] = source;
    targets[len++] = target;
    TyMapper const* ret = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    free(pairs);
    return ret;
}

static TyCondBranch instantiate_branch(Checker* me, TyCondBranch const* branch, TyMapper const* mapper) {
    TyCondBranch ret = *branch;
    if ((branch->node || branch->source) && branch->state != LAZY_STATE_DONE) {
        ret.mapper = ty_compose_mappers(me, branch->mapper, mapper);
        ret.state = LAZY_STATE_NOT_STARTED;
    } else if (branch->type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
        ret = (TyCondBranch){ .source = branch->type, .mapper = mapper };
    }
    return ret;
}

static Ty const* conditional_from_root(Checker* me, Ty const* root, Ty const* check_type, TyMapper const* mapper) {
    TyCondBranch if_true = instantiate_branch(me, &root->conditional.branches[1], mapper);
    TyCondBranch if_false = instantiate_branch(me, &root->conditional.branches[0], mapper);
    return ty_conditional(
        me, check_type, ty_instantiate(me, root->conditional.extends, mapper), &if_true, &if_false, root, mapper,
        root->conditional.distributive, root->conditional.check_tuples, ty_conditional_own_infers(me, root)
    );
}

static TyList instantiate_list(Checker* me, TyList list, TyMapper const* mapper, Ty const** buf, u32 buf_len) {
    // Returns the list itself if nothing changed. buf must hold list.len entries
    check(list.len <= buf_len);
    bool changed = false;
    for (u32 i = 0; i < list.len; i++) {
        buf[i] = ty_instantiate(me, list.data[i], mapper);
        changed |= buf[i] != list.data[i];
    }
    return changed ? (TyList){ .data = buf, .len = list.len } : list;
}

// A type's alias arguments instantiated: in buf (16 entries) when they fit, else in the arena (an alias with more
// type parameters than that is rare: vue's ComponentOptionsBase has 17)
static TyList instantiate_alias_args(Checker* me, Ty const* ty, TyMapper const* mapper, Ty const** buf) {
    if (ty->alias_args.len > 16) buf = arena_new_n(&my arena, Ty const*, ty->alias_args.len);
    return instantiate_list(me, ty->alias_args, mapper, buf, ty->alias_args.len);
}

// Whether every constituent of the constraint of a mapped type's type variable is an array or tuple (tsc's everyType
// isArrayOrTupleType of getConstraintOfTypeParameter)
static bool mapped_variable_is_list(Checker* me, Ty const* variable) {
    Ty const* constraint = ty_param_constraint(me, variable);
    if (!constraint) return false;
    // (The empty tuple `[]` included: its tuple target, `T extends readonly unknown[] | []` in Promise.all)
    if (constraint->kind != TY_UNION) return ty_is_array(me, constraint) || ty_tuple_shape(constraint);
    for (u32 i = 0; i < constraint->composite.types.len; i++) {
        Ty const* part = constraint->composite.types.data[i];
        if (!ty_is_array(me, part) && !ty_tuple_shape(part)) return false;
    }
    return true;
}

// The type variable of a homomorphic mapped type (`T` of `{ [P in keyof T]: X }`, tsc's getHomomorphicTypeVariable)
// or of an instance of one (of its root: the instance's modifiers type is it through the instance's mapper), else null
static Ty const* homomorphic_variable(Checker* me, Ty const* ty, MappedInfo const** mapped_out) {
    // Asking for the members of every object type that gets instantiated would defeat their laziness, so look at the
    // syntax first
    Ty const* root = ty->anonymous.target ? ty->anonymous.target : ty;
    // (An instantiation expression's target is any object type: an interface's `typeof f<T>` too)
    if (root->kind != TY_ANONYMOUS) return nullptr;
    TypeNode const* node = root->anonymous.node;
    if (!node || node->tag != TYPE_INTERFACE || vec_interface_member_size(&node->interface.members) != 1) return nullptr;
    InterfaceMember const* member = vec_interface_member_at(&node->interface.members, 0);
    if (member->key.tag != INTERFACE_KEY_MAPPED || member->key.mapped->in_type.tag != TYPE_KEYOF) return nullptr;
    MappedInfo const* mapped = ty_mapped_info_unexpanded(me, root);
    // (tsc's getHomomorphicTypeVariable looks at the lowered constraint: a `keyof T` substituted in a true branch of
    // `keyof T extends ...` isn't homomorphic, but a type parameter substituted in a true branch is still the one)
    // It's the lowered constraint's operand, not the modifiers type: `keyof (T & Required<T>)` lowers to `keyof T`
    // (vue's LooseRequired), homomorphic over T
    if (!mapped || !mapped->modifiers_type || mapped->constraint->kind != TY_KEYOF) return nullptr;
    Ty const* variable = ty_actual_type_variable(me, mapped->constraint->keyof.operand);
    if (variable->kind != TY_TYPE_PARAM) return nullptr;
    if (mapped_out) *mapped_out = mapped;
    return variable;
}

// tsc's getHomomorphicTypeVariable: the type parameter a mapped type maps over (`keyof Helper<T>` of a mapped Helper
// lowers to `keyof T`), null if it isn't homomorphic
Ty const* ty_mapped_homomorphic_variable(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS) return nullptr;
    return homomorphic_variable(me, ty, nullptr);
}

// Whether `ty` instantiated with `mapper` may be something a homomorphic mapped type over it maps other than as an
// object: a union, an array or tuple, a primitive. Doesn't instantiate object types to find out: an instance of a
// homomorphic mapped type is one of those only if its own type variable becomes one
static bool may_map_specially(Checker* me, Ty const* ty, TyMapper const* mapper, u32 depth) {
    switch (ty->kind) {
        case TY_DECLARED:
            return ty->declared.kind == TY_DECLARED_TUPLE_TARGET;
        case TY_REFERENCE:
            return ty_is_array(me, ty) || ty_is_tuple(ty);
        case TY_ANONYMOUS: {
            Ty const* variable = homomorphic_variable(me, ty, nullptr);
            if (!variable) return false;
            if (depth >= 8) return true;
            Ty const* inner = ty->anonymous.target ? ty_instantiate(me, variable, ty->anonymous.mapper) : variable;
            return may_map_specially(me, inner, mapper, depth + 1);
        }
        case TY_TYPE_PARAM: {
            Ty const* instantiated = ty_instantiate(me, ty, mapper);
            return instantiated == ty || may_map_specially(me, instantiated, nullptr, depth + 1);
        }
        default:
            return true;
    }
}

// tsc's removeMissingOrUndefinedType: under exactOptionalPropertyTypes only the missing type goes, a written undefined
// stays (`Required<[(string | undefined)?]>` takes undefined)
static Ty const* remove_missing_or_undefined(Checker* me, Ty const* ty) {
    if (compiler_options()->exact_optional_property_types) return ty_remove_missing(me, ty, true);
    return ty_without_undefined(me, ty);
}

// A `?` mapped type adds undefined to what an array or rest element maps to under strictNullChecks (tsc's
// instantiateMappedTypeTemplate for instantiateMappedArrayType/instantiateMappedTupleType): Partial<string[]> is
// (string | undefined)[]. A `-?` one takes it out of an array's elements (`is_optional`: they are)
static Ty const* mapped_element_optionality(Checker* me, MappedInfo const* mapped, Ty const* element, bool is_optional) {
    if (!compiler_options()->strict_null_checks) return element;
    if (mapped->optional < 0 && is_optional) return remove_missing_or_undefined(me, element);
    if (mapped->optional <= 0) return element;
    if (element->flags & (TYF_UNDEFINED | TYF_VOID | TYF_ANY | TYF_UNKNOWN)) return element;
    if (element->kind == TY_UNION) {
        for (u32 i = 0; i < element->composite.types.len; i++) {
            if (element->composite.types.data[i]->flags & (TYF_UNDEFINED | TYF_VOID)) return element;
        }
    }
    // (The missing type under exactOptionalPropertyTypes: tsc's getTemplateTypeFromMappedType adds it with
    // addOptionality, which is what reverse inference through Partial<O> takes back out)
    Ty const* parts[2] = { element, ty_undefined_or_missing(me) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

// What a homomorphic mapped type makes of an array or a tuple (tsc's instantiateMappedArrayType and
// instantiateMappedTupleType), null for anything else
static Ty const* mapped_over_array_or_tuple(
    Checker* me, Ty const* root, MappedInfo const* mapped, Ty const* variable, TyMapper const* mapper, Ty const* list
) {
    if (ty_is_array(me, list)) {
        bool readonly = mapped->readonly ? mapped->readonly > 0 : list->reference.target == my readonly_array_target;
        TyMapper const* element_mapper = ty_mapper_with(me, mapper, mapped->type_param, ty_intrinsic(me, TY_NUMBER));
        Ty const* element = mapped_element_optionality(me, mapped, ty_instantiate(me, ty_mapped_template(me, mapped), element_mapper), true);
        // (An error element makes the whole the error type: `Promise.all` of an unresolved import's results is
        // `Promise<any>`, and callbacks of a method of the awaited any get no contextual type)
        return element->flags & TYF_ERROR ? ty_intrinsic(me, TY_ERROR) : ty_array(me, element, readonly);
    }
    if (!ty_is_tuple(list)) return nullptr;
    TupleShape const* shape = list->reference.target->declared.tuple;
    TyList elements = ty_reference_args(me, list);
    Ty const* stack_elements[32];
    unsigned char stack_flags[32];
    bool small = shape->arity <= 32;
    Ty const** mapped_elements = small ? stack_elements : malloc(sizeof(Ty const*) * shape->arity);
    unsigned char* flags = small ? stack_flags : malloc(shape->arity);
    panic_if(!mapped_elements || !flags, "out of memory");
    Ty const* ret = nullptr;
    // Past the first rest element, each element maps as the element of an array of it, a variadic one as a whole
    // (tsc's instantiateMappedTupleType)
    bool past_rest = false;
    for (u32 i = 0; i < shape->arity; i++) {
        past_rest |= (shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
        if (past_rest) {
            Ty const* element = elements.data[i];
            bool rest = (shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
            flags[i] = shape->flags[i];
            if (rest && element->kind == TY_TYPE_PARAM) {
                // (tsc's prependTypeMapping is a merged mapper, the outer mapper applied to what the variable maps to:
                // a variadic element that the outer mapper maps again is mapped again, `Foo<[...E, "abc"]>` over
                // `{ [P in keyof E]: ... }` recursing until TS2589)
                Ty const* target = element;
                for (u32 j = 0; mapper && j < mapper->len; j++) {
                    if (mapper->sources[j] == element) {
                        target = mapper->targets[j];
                        break;
                    }
                }
                mapped_elements[i] = ty_instantiate(me, root, ty_mapper_with(me, mapper, variable, target));
                continue;
            }
            Ty const* array = rest ? element : ty_array(me, element, false);
            if (!ty_is_array(me, array)) goto done;
            TyMapper const* element_mapper = ty_mapper_with(me, ty_mapper_with(me, mapper, variable, array),
                mapped->type_param, ty_intrinsic(me, TY_NUMBER));
            Ty const* mapped_element = mapped_element_optionality(me, mapped, ty_instantiate(me, ty_mapped_template(me, mapped), element_mapper), false);
            // (tsc maps the array, which an error element makes the error type, whose element type is none: unknown,
            // `Writeable<[X, ...X[]]>` of an unresolved X is `[X, ...unknown[]]`)
            if (mapped_element->flags & TYF_ERROR) mapped_element = ty_intrinsic(me, TY_UNKNOWN_);
            mapped_elements[i] = rest ? ty_array(me, mapped_element, false) : mapped_element;
            // (The modifiers apply to a fixed element after the rest one too: `Partial<[...T, number]>` ends in
            // `(number | undefined)?`, tsc's newElementFlags)
            if (!rest && mapped->optional > 0) flags[i] |= TUPLE_ELEMENT_OPTIONAL;
            if (!rest && mapped->optional < 0) flags[i] &= ~(unsigned)TUPLE_ELEMENT_OPTIONAL;
            continue;
        }
        cstr index = cstr_d(i);
        Ty const* key = ty_string_literal(me, cstr_sv(&index));
        cstr_drop(&index);
        Ty const* element = ty_instantiate(me, ty_mapped_template(me, mapped), ty_mapper_with(me, mapper, mapped->type_param, key));
        flags[i] = shape->flags[i];
        // `-?` takes undefined out of an element that was optional (tsc's instantiateMappedTypeTemplate), and an
        // optional element has undefined added like a written one (createNormalizedTupleType's addOptionality)
        if (mapped->optional < 0 && (flags[i] & TUPLE_ELEMENT_OPTIONAL) && compiler_options()->strict_null_checks) {
            element = remove_missing_or_undefined(me, element);
        }
        if (mapped->optional > 0) flags[i] |= TUPLE_ELEMENT_OPTIONAL;
        if (mapped->optional < 0) flags[i] &= ~(unsigned)TUPLE_ELEMENT_OPTIONAL;
        mapped_elements[i] = flags[i] & TUPLE_ELEMENT_OPTIONAL ? ty_optional_prop_type(me, element) : element;
    }
    TupleShape new_shape = {
        .arity = shape->arity,
        .readonly = mapped->readonly ? mapped->readonly > 0 : shape->readonly,
        .flags = flags,
        .names = shape->names,
        .labels = shape->labels,
    };
    // (Like an array's, an error element makes the whole the error type; tsc compares with errorType itself)
    for (u32 i = 0; i < shape->arity; i++) {
        if (mapped_elements[i] == ty_intrinsic(me, TY_ERROR)) {
            ret = mapped_elements[i];
            goto done;
        }
    }
    Ty const* target = ty_tuple_target(me, &new_shape);
    ret = !shape->arity ? target : ty_reference(me, target, (TyList){ .data = mapped_elements, .len = shape->arity });
done:
    if (!small) {
        free(mapped_elements);
        free(flags);
    }
    return ret;
}

// A homomorphic mapped type (`{ [P in keyof T]: X }`) instantiated with an array or tuple for T is an array or tuple of
// the template, not an object with the array methods mapped, with a union for T a union, and with a primitive for T the
// primitive (tsc's instantiateMappedType). Null if this isn't that
static Ty const* instantiate_mapped_over_list(Checker* me, Ty const* ty, TyMapper const* mapper) {
    Ty const* root = ty->anonymous.target ? ty->anonymous.target : ty;
    MappedInfo const* mapped = nullptr;
    Ty const* variable = homomorphic_variable(me, ty, &mapped);
    if (!variable) return nullptr;
    // Like tsc (instantiateMappedType of the target with the combined mapper), an instance is looked at through its
    // root: in `type DP<T> = { [P in keyof T]?: DP<T[P]> }`, the inner `DP<T[P]>` is an instance whose modifiers type
    // is `T[P]`, and it still distributes once T[P] becomes `string | null`
    // (The instance's mapper, then this one: composing them is only worth it when the result distributes, which is
    // rare, and composing eagerly was 40% of valibot)
    TyMapper const* outer = mapper;
    Ty const* inner = root != ty ? ty_instantiate(me, variable, ty->anonymous.mapper) : variable;
    // Instantiating an object type eagerly only to find it stays one was a third of valibot
    if (root != ty && !may_map_specially(me, inner, mapper, 0)) return nullptr;
    Ty const* list = ty_instantiate(me, inner, mapper);
    if (list == variable) return nullptr;
    if (list->kind == TY_UNION || list->kind == TY_INTERSECTION) list = ty_reduced(me, list); // (tsc's instantiateMappedType)
    u32 const mappable = TYF_ANY | TYF_UNKNOWN | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_OBJECT | TYF_INTERSECTION;
    // (tsc's instantiateConstituent: of the deferred types only the instantiable non-primitive ones, an indexed access, a
    // conditional or substitution type, are mapped; `keyof X`, a generic template literal or string mapping, and the
    // error type, stay what they are)
#define MAPS(t) (((t)->flags & mappable) && !((t)->flags & TYF_ERROR) && \
    (!((t)->flags & TYF_DEFERRED) || (t)->kind == TY_INDEXED || (t)->kind == TY_CONDITIONAL || (t)->kind == TY_SUBSTITUTION || \
        ((t)->flags & (TYF_TYPE_PARAM | TYF_OBJECT | TYF_INTERSECTION))))
    // (The empty tuple is its tuple target, not a reference)
    TupleShape const* empty = list->kind == TY_DECLARED ? ty_tuple_shape(list) : nullptr;
    if (empty && !mapped->name_type) {
        TupleShape new_shape = { .readonly = mapped->readonly ? mapped->readonly > 0 : empty->readonly };
        return ty_tuple_target(me, &new_shape);
    }
    if (list->kind != TY_UNION && !MAPS(list)) return list;
    // any for a T constrained to arrays or tuples maps to an array too: `{ [K in keyof T]: X }` with `T extends
    // readonly unknown[]` is `X[]` for any (tsc's instantiateConstituent)
    bool any_as_array = list->kind == TY_ANY && !mapped->name_type && mapped_variable_is_list(me, variable);
    // (With an `as` clause, arrays and tuples map like any object)
    if (list->kind != TY_UNION && !any_as_array && ((list->kind != TY_REFERENCE && list->kind != TY_INTERSECTION) || mapped->name_type)) return nullptr;
    // (An intersection only maps when all its members are arrays or tuples: decided before composing the mappers,
    // which most intersections would pay for nothing)
    if (list->kind == TY_INTERSECTION) {
        TyList members = list->composite.types;
        for (u32 i = 0; i < members.len; i++) {
            if (!ty_is_array(me, members.data[i]) && !ty_is_tuple(members.data[i])) return nullptr;
        }
    }
    if (root != ty) mapper = ty_compose_mappers(me, ty->anonymous.mapper, mapper);
    if (any_as_array) {
        TyMapper const* element_mapper = ty_mapper_with(me, mapper, mapped->type_param, ty_intrinsic(me, TY_NUMBER));
        return ty_array(me, ty_instantiate(me, ty_mapped_template(me, mapped), element_mapper), mapped->readonly > 0);
    }
    if (list->kind == TY_UNION) {
        // Distributes over a union (tsc's mapTypeWithAlias): `Mutable<A | B>` is `Mutable<A> | Mutable<B>` by the
        // name it was written with. Primitives map to themselves
        u32 len = list->composite.types.len;
        Ty const** parts = malloc(sizeof(Ty const*) * len);
        panic_if(!parts, "out of memory");
        for (u32 i = 0; i < len; i++) {
            Ty const* part = list->composite.types.data[i];
            parts[i] = MAPS(part) ? ty_instantiate(me, root, ty_mapper_with(me, mapper, variable, part)) : part;
        }
        Ty const* alias_buf[16];
        TyList alias_args = instantiate_alias_args(me, ty, outer, alias_buf);
        Ty const* ret = ty_union(me, parts, len, ty->alias, alias_args);
        free(parts);
        return ret;
    }
    // An intersection of arrays and tuples maps each of them (tsc's isArrayOrTupleOrIntersection)
    if (list->kind == TY_INTERSECTION && !mapped->name_type) {
        TyList members = list->composite.types;
        bool lists = true;
        for (u32 i = 0; i < members.len && lists; i++) lists = ty_is_array(me, members.data[i]) || ty_is_tuple(members.data[i]);
        if (!lists) return nullptr;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        panic_if(!parts, "out of memory");
        Ty const* ret = nullptr;
        u32 i = 0;
        for (; i < members.len; i++) {
            // (An array's elements are the template with T the array, a tuple's T[K] with T the intersection)
            Ty const* member = members.data[i];
            TyMapper const* member_mapper = ty_is_array(me, member) ? ty_mapper_with(me, mapper, variable, member) : mapper;
            parts[i] = mapped_over_array_or_tuple(me, root, mapped, variable, member_mapper, member);
            if (!parts[i]) break;
        }
        if (i == members.len) ret = ty_intersection(me, parts, members.len, nullptr, (TyList){0});
        free(parts);
        return ret;
    }
#undef MAPS
    return mapped_over_array_or_tuple(me, root, mapped, variable, mapper, list);
}

// An object literal's type (ty_object_literal_of()) has its members already, and doesn't know which type parameters
// they mention (free_params_unknown): an instantiation whose members all come out as they are is the type itself.
// tsc gets there through the literal's outer type parameters, none of which it possibly references
// (getObjectTypeInstantiation). Otherwise every mapper made a new, equal instance, and everything built on it (a
// table type from a columns literal) was made again too: drizzle's query builders ran into TS2589 on those copies
static bool literal_instance_is_itself(Checker* me, Ty const* target, TyMapper const* mapper) {
    if (!target->anonymous.free_params_unknown || target->anonymous.node || target->anonymous.function ||
        target->anonymous.overload_count || (target->anonymous.merged_value && !target->anonymous.is_reverse) || target->anonymous.applies_type_args) {
        return false;
    }
    TyObjectState const* state = target->anonymous.state;
    if (!state || state->members_state != LAZY_STATE_DONE) return false;
    Members const* members = state->members;
    if (members->call_count || members->construct_count || members->mapped || members_rare(members)->base_count) return false;
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        if (prop->type_state != LAZY_STATE_DONE || !prop->type) return false;
        if (prop->write_type_known) {
            Ty const* write = intern_get(&my links, (u64[]){ KEY_PROP_WRITE_TYPE, (u64)(uintptr_t)prop }, 2);
            if (write && write != prop->type) return false;
        }
        if (ty_instantiate(me, prop->type, mapper) != prop->type) return false;
    }
    for (u32 i = 0; i < members->index_count; i++) {
        IndexInfo const* index = &members->index[i];
        if (ty_instantiate(me, index->key, mapper) != index->key || ty_instantiate(me, index->value, mapper) != index->value) return false;
    }
    return true;
}

static bool ty_list_contains(TyList list, Ty const* ty) {
    for (u32 i = 0; i < list.len; i++) if (list.data[i] == ty) return true;
    return false;
}

// A type parameter declared within `span` (see instantiate_anonymous())
static bool declared_inside(Ty const* source, Span span) {
    if (!span.buf || source->kind != TY_TYPE_PARAM || !source->param.symbol) return false;
    return span_contains_span(span, source->param.symbol->name_span);
}

// The type parameters from outside that the overloads of a function symbol mention, false if not known: every overload
// a declaration whose free parameters ty_function_free_params() knows (the implementation isn't a signature of it)
static bool merged_overloads_free_params(Checker* me, Symbol const* symbol, TyList* out) {
    if (!(symbol->flags & SYMBOL_FUNCTION)) return false;
    Ty const** entries;
    u32 count = ty_merged_value_entries(me, symbol, &entries);
    bool known = count > 0;
    TyBuf params = {0};
    for (u32 i = 0; i < count && known; i++) {
        Ty const* entry = entries[i];
        if (entry->kind != TY_ANONYMOUS || !entry->anonymous.function) {
            known = false;
            break;
        }
        FunctionImpl const* function = entry->anonymous.function;
        if (function->has_body) continue;
        TyList own;
        if (!ty_function_free_params(me, function, &own)) {
            known = false;
            break;
        }
        for (u32 j = 0; j < own.len; j++) {
            bool seen = false;
            for (u32 k = 0; k < params.len && !seen; k++) seen = params.data[k] == own.data[j];
            if (!seen) ty_buf_push(&params, own.data[j]);
        }
    }
    free(entries);
    if (known) *out = ty_list_copy(me, params.data, params.len);
    ty_buf_drop(&params);
    return known;
}

static Ty const* instantiate_anonymous(Checker* me, Ty const* ty, TyMapper const* mapper) {
    if (ty->anonymous.applies_type_args) {
        // An instantiation expression whose type arguments mention the type parameters: `typeof C<O>` in `type R<O>`
        // is `typeof C<string>` for R<string>. An object type that may mention them too (a function type: whose free
        // type parameters aren't known) keeps the outer mapping alongside the type arguments, so its signatures, whose
        // own type parameters the arguments replace, get both (`typeof raise<T>` in `interface Enq<T>`)
        TyMapper const* args = ty->anonymous.mapper;
        bool changed = false;
        for (u32 i = 0; i < args->len && !changed; i++) changed = ty_instantiate(me, args->targets[i], mapper) != args->targets[i];
        Ty const* target = ty->anonymous.target;
        bool outer = ty_instantiate(me, target, mapper) != target;
        if (!changed && !outer) return ty;
        u32 cap = args->len + (outer ? mapper->len : 0);
        Ty const** sources = malloc(sizeof(Ty const*) * cap * 2);
        panic_if(!sources, "out of memory");
        Ty const** targets = sources + cap;
        u32 len = 0;
        for (u32 i = 0; i < args->len; i++) {
            sources[len] = args->sources[i];
            targets[len++] = ty_instantiate(me, args->targets[i], mapper);
        }
        for (u32 i = 0; outer && i < mapper->len; i++) {
            bool shadowed = mapper->sources[i] == mapper->targets[i];
            for (u32 j = 0; j < args->len; j++) shadowed |= args->sources[j] == mapper->sources[i];
            if (shadowed) continue;
            sources[len] = mapper->sources[i];
            targets[len++] = mapper->targets[i];
        }
        TyMapper const* applied = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
        free(sources);
        bool query = intern_get(&my links, (u64[]){ KEY_INSTANTIATION_QUERY, ty->id }, 2) != nullptr;
        return ty_apply_type_arguments_mapper_of(me, target, applied, query ? ty : nullptr);
    }
    Ty const* as_list = instantiate_mapped_over_list(me, ty, mapper);
    if (as_list) return as_list;
    // A mapped type whose constraint becomes the wildcard is the wildcard (tsc's instantiateMappedType)
    bool maps_to_wildcard = false;
    for (u32 i = 0; i < mapper->len && !maps_to_wildcard; i++) maps_to_wildcard = mapper->targets[i] == ty_intrinsic(me, TY_WILDCARD);
    if (maps_to_wildcard) {
        Ty const* root = ty->anonymous.target ? ty->anonymous.target : ty;
        MappedInfo const* mapped = root->anonymous.node && root->anonymous.node->tag == TYPE_INTERFACE ? ty_members(me, root)->mapped : nullptr;
        if (mapped) {
            // (Of a homomorphic one, `[K in keyof T]`, the type variable T: tsc's getHomomorphicTypeVariable)
            Ty const* constraint = mapped->constraint->kind == TY_KEYOF ? mapped->constraint->keyof.operand : mapped->constraint;
            if (ty->anonymous.mapper) constraint = ty_instantiate(me, constraint, ty->anonymous.mapper);
            if (ty_instantiate(me, constraint, mapper) == ty_intrinsic(me, TY_WILDCARD)) return ty_intrinsic(me, TY_WILDCARD);
        }
    }
    Ty const* target = ty->anonymous.target ? ty->anonymous.target : ty;
    TyMapper const* inner = ty->anonymous.mapper;

    // The flat composition of the mapper this type already has with the new one: inner's targets get instantiated,
    // and whatever else the new mapper maps is carried along, because the target may mention those parameters too.
    // Only the type parameters the target's node mentions are kept (like tsc), so `{ v: T }` under {T: string, U: X}
    // and under {T: string, U: Y} is one type, and a node that mentions none is never instantiated at all.
    u32 cap = (inner ? inner->len : 0) + mapper->len;
    Ty const* stack_buf[32];
    Ty const** pairs = cap * 2 <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * cap * 2);
    Ty const** sources = pairs;
    Ty const** targets = pairs + cap;
    u32 len = 0;
    TyList free_params = target->anonymous.free_params;
    bool free_params_unknown = target->anonymous.free_params_unknown;
    // A function expression's type mentions what its resolved signature does (tsc: its outer type parameters), not
    // whatever the mapper has: a callback in a class method otherwise got a new instance for every composed mapper
    // that reached it (kysely's builders, docmost)
    if (free_params_unknown && (target->anonymous.function || target->anonymous.object_literal) &&
        ty_function_type_mentions(me, target, &free_params)
    ) {
        free_params_unknown = false;
    }
    // Whatever it mentions, it doesn't mention a type parameter declared inside it from outside: those are bound in it
    // (a method's own), so what maps them doesn't make another instance (tsc's getObjectTypeInstantiation keys on the
    // outer type parameters of the declaration). A literal whose method returns the literal's variable: a call of the
    // method maps the method's type parameter
    Span own = {0};
    if (free_params_unknown) {
        if (target->anonymous.function) own = target->anonymous.function->full_span;
        else if (target->anonymous.object_literal && !target->anonymous.overload_count && target->anonymous.literal_expr) {
            own = expr_outer_span(target->anonymous.literal_expr);
        }
    }
    // A declared function's or method's whose signature is all annotations by the outer type parameters those mention
    // (tsc's getObjectTypeInstantiation filters a method's by isTypeParameterPossiblyReferenced): every `() => void`
    // method of ts-morph's Node<T> was a new type for every Node<X>, relating two of its subclasses member by member
    if (free_params_unknown && target->anonymous.function && !target->anonymous.function->is_expression &&
        !target->anonymous.node && !target->anonymous.overload_count && !target->anonymous.is_reverse &&
        !target->anonymous.merged_value && !target->anonymous.applies_type_args &&
        ty_function_free_params(me, target->anonymous.function, &free_params)
    ) {
        free_params_unknown = false;
    }
    // An overloaded function's (its signatures all annotations): what its overloads mention, none for a file's or
    // namespace's function (tsc's getObjectTypeInstantiation: no outer type parameters, the type itself)
    TyList overload_params = {0};
    if (free_params_unknown && !target->anonymous.function && !target->anonymous.is_reverse &&
        target->anonymous.merged_value && !target->anonymous.applies_type_args &&
        merged_overloads_free_params(me, target->anonymous.merged_value, &overload_params)
    ) {
        free_params = overload_params;
        free_params_unknown = false;
    }
    for (u32 i = 0; inner && i < inner->len; i++) {
        // (What the node doesn't mention is dropped below anyway: not instantiated, like tsc's getObjectTypeInstantiation
        // maps only the outer type parameters it mentions)
        if (!free_params_unknown && !ty_list_contains(free_params, inner->sources[i])) continue;
        if (declared_inside(inner->sources[i], own)) continue;
        Ty const* mapped = ty_instantiate(me, inner->targets[i], mapper);
        if (mapped != inner->sources[i]) { // Identity mappings say nothing
            sources[len] = inner->sources[i];
            targets[len++] = mapped;
        }
    }
    for (u32 i = 0; i < mapper->len; i++) {
        bool shadowed = mapper->sources[i] == mapper->targets[i];
        if (!shadowed && !free_params_unknown && !ty_list_contains(free_params, mapper->sources[i])) continue;
        if (declared_inside(mapper->sources[i], own)) continue;
        for (u32 j = 0; inner && j < inner->len && !shadowed; j++) shadowed = inner->sources[j] == mapper->sources[i];
        if (!shadowed) {
            sources[len] = mapper->sources[i];
            targets[len++] = mapper->targets[i];
        }
    }
    u32 flags = TYF_OBJECT | (free_params_unknown ? TYF_COULD_CONTAIN_TYPE_VARIABLES : 0);
    if (!free_params_unknown) {
        u32 kept = 0;
        for (u32 i = 0; i < len; i++) {
            bool mentioned = false;
            for (u32 j = 0; j < free_params.len; j++) mentioned |= free_params.data[j] == sources[i];
            if (mentioned) {
                sources[kept] = sources[i];
                targets[kept++] = targets[i];
            }
        }
        len = kept;
        // What's left generic about the result: mapped-to types that are, and mentioned parameters nothing maps
        for (u32 j = 0; j < free_params.len; j++) {
            bool mapped = false;
            for (u32 i = 0; i < len; i++) {
                if (sources[i] == free_params.data[j]) {
                    mapped = true;
                    flags |= targets[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
                }
            }
            if (!mapped) flags |= TYF_COULD_CONTAIN_TYPE_VARIABLES;
        }
    }

    Ty const* ret = target;
    if (len) {
        u64 stack_key[64];
        u32 count = 2 + len;
        u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
        key[0] = KEY_ANONYMOUS_INSTANCE;
        key[1] = target->id;
        for (u32 i = 0; i < len; i++) key[2 + i] = ty_id_pair(sources[i], targets[i]);
        ret = intern_get(&my types, key, count);
        if (!ret) {
            TyMapper const* composed = ty_mapper_new(
                me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len }
            );
            Ty* fresh = ty_alloc(me, TY_ANONYMOUS, flags);
            fresh->anonymous.target = target;
            fresh->anonymous.mapped = target->anonymous.mapped;
            fresh->anonymous.mapper = composed;
            fresh->anonymous.ctx = target->anonymous.ctx;
            fresh->anonymous.state = arena_new(&my arena, TyObjectState);
            fresh->alias = target->alias;
            if (target->alias_args.len) {
                Ty const** alias_args = arena_new_n(&my arena, Ty const*, target->alias_args.len);
                for (u32 i = 0; i < target->alias_args.len; i++) {
                    alias_args[i] = ty_instantiate(me, target->alias_args.data[i], composed);
                }
                fresh->alias_args = (TyList){ .data = alias_args, .len = target->alias_args.len };
                for (u32 i = 0; i < target->alias_args.len; i++) fresh->flags |= alias_args[i]->flags & TYF_NON_INFERRABLE;
            }
            // Instantiating the alias arguments may have grown the table, so the slot is looked up again
            *intern_slot(me, &my types, key, count) = fresh;
            ret = fresh;
            // (Checked with the instance in place: a literal whose members lead back to it gets that instance, so
            // it's a new one)
            if (!inner && literal_instance_is_itself(me, target, composed)) {
                *intern_slot(me, &my types, key, count) = target;
                ret = target;
            }
        }
        if (key != stack_key) free(key);
    }
    if (pairs != stack_buf) free(pairs);
    if (ty->anonymous.realiased && ret != ty) {
        Ty const* alias_buf[16];
        TyList alias_args = instantiate_alias_args(me, ty, mapper, alias_buf);
        ret = ty_realias(me, ret, ty->alias, alias_args);
    }
    return ret;
}

// `type N = Readonly<{ a: 1 }>`: an instance of a homomorphic mapped type made for an alias's body, which keeps the
// mapped type's own name (tsc's instantiateMappedType doesn't pass the new alias on to an object), but is its own type:
// tsc's getObjectTypeInstantiation cached it under the new alias, so `N | Readonly<{ a: 1 }>` written elsewhere has two
// members. Instantiated (when its arguments are generic) it is the ordinary instance again, through ty_realias().
// HACK: tsc instantiates it whenever couldContainTypeVariables says its arguments could change, which a type literal
// always can, and gets the ordinary instance then too (an instantiated signature returning N doesn't reuse its
// annotation). tttt's TYF_COULD_CONTAIN_TYPE_VARIABLES is exact and doubles as "generic" in member resolution, so
// that isn't emulated (hacks.md)
Ty const* ty_alias_body_mapped_instance(Checker* me, Ty const* ty, Symbol const* alias, TyList alias_args) {
    if (ty->kind != TY_ANONYMOUS || !ty->anonymous.mapped || !ty->anonymous.target || ty->anonymous.realiased || !alias ||
        ty->alias == alias
    ) {
        return ty;
    }
    u64 stack_key[40];
    u32 count = 4 + alias_args.len;
    if (count > c_arraylen(stack_key)) return ty;
    stack_key[0] = KEY_ANONYMOUS_INSTANCE;
    stack_key[1] = ty->id;
    stack_key[2] = UINT64_MAX - 2; // Not a type id: tells these apart from ordinary instantiations and ty_realias()'s
    stack_key[3] = (u64)(uintptr_t)alias;
    for (u32 i = 0; i < alias_args.len; i++) stack_key[4 + i] = alias_args.data[i]->id;
    Ty const* ret = intern_get(&my types, stack_key, count);
    if (!ret) {
        Ty* fresh = ty_alloc(me, ty->kind, ty->flags);
        fresh->anonymous = ty->anonymous;
        // (Instantiated, ty_realias() puts back the name it has, the mapped type's own: the ordinary instance)
        fresh->anonymous.realiased = true;
        fresh->anonymous.state = arena_new(&my arena, TyObjectState);
        fresh->alias = ty->alias;
        fresh->alias_args = ty->alias_args;
        *intern_slot(me, &my types, stack_key, count) = fresh;
        ret = fresh;
    }
    return ret;
}

// `type Bar = Omit<Foo, "c">`: the instantiation of a generic alias written directly as the body of another alias goes
// by the new name (tsc passes the new alias symbol to getTypeAliasInstantiation). Same members, its own identity
Ty const* ty_realias(Checker* me, Ty const* ty, Symbol const* alias, TyList alias_args) {
    bool same_args = ty->alias_args.len == alias_args.len;
    for (u32 i = 0; same_args && i < alias_args.len; i++) same_args = ty->alias_args.data[i] == alias_args.data[i];
    if (ty->alias == alias && same_args) return ty;
    // The alias's own body with other type arguments, which it doesn't use (`type Foo<T> = { a: 1 }`, Foo<number>):
    // an instantiation of its own that prints with them (tsc's getObjectTypeInstantiation keys on the alias type
    // arguments too)
    // (A union of the same alias with other arguments too: `G` through an import, alias arguments `[]`, next to
    // `G<string, number>`)
    // (One an intersection distributed into is that intersection made again by the new name, the same distribution
    // with the intersection as its origin: tsc's instantiateTypeWorker passes the alias to getIntersectionType, so
    // `type S = Rec<string>` of `Rec<I> = Base<I> & (A | B)` prints `Base & (A | B)` where S isn't accessible)
    Ty const* origin = ty->kind == TY_UNION ? ty->composite.origin : nullptr;
    if (origin && origin->kind == TY_INTERSECTION) {
        Ty const* ret = ty_intersection(me, origin->composite.types.data, origin->composite.types.len, alias, alias_args);
        if (ret->kind == TY_UNION && ret->alias == alias) return ret;
    }
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        return ty_union(me, ty->composite.types.data, ty->composite.types.len, alias, alias_args);
    }
    if (ty->kind == TY_INTERSECTION) {
        return ty_intersection(me, ty->composite.types.data, ty->composite.types.len, alias, alias_args);
    }
    if (ty->alias == alias) {
        // (A deferred type too: `C<T>` through an import is not `C<T, {}>`, tsc's getTypeAliasInstantiation keys on the
        // alias type arguments)
        bool deferred = ty->kind == TY_INDEXED || ty->kind == TY_CONDITIONAL || (ty->kind == TY_REFERENCE && ty->reference.deferred);
        if (!deferred && (ty->kind != TY_ANONYMOUS || ty->anonymous.applies_type_args || ty->anonymous.function)) return ty;
    } else {
        // (A deferred reference, `type L1<V> = L2<V>` of `type L2<V> = L3<V>`, goes by the new name too: tsc's
        // getObjectTypeInstantiation keys it on the alias)
        bool deferred = ty->kind == TY_INDEXED || ty->kind == TY_CONDITIONAL || (ty->kind == TY_REFERENCE && ty->reference.deferred);
        if (!deferred && (ty->kind != TY_ANONYMOUS || (!ty->anonymous.target && !ty->anonymous.realiased))) {
            return ty;
        }
    }
    u64 stack_key[40];
    u32 count = 4 + alias_args.len;
    if (count > c_arraylen(stack_key)) return ty;
    stack_key[0] = KEY_ANONYMOUS_INSTANCE;
    stack_key[1] = ty->id;
    stack_key[2] = UINT64_MAX - 1; // Not a type id: tells these apart from ordinary instantiations
    stack_key[3] = (u64)(uintptr_t)alias;
    for (u32 i = 0; i < alias_args.len; i++) stack_key[4 + i] = alias_args.data[i]->id;
    Ty const* ret = intern_get(&my types, stack_key, count);
    if (!ret) {
        Ty* fresh = ty_alloc(me, ty->kind, ty->flags);
        if (ty->kind == TY_INDEXED) {
            fresh->indexed = ty->indexed;
        } else if (ty->kind == TY_CONDITIONAL) {
            // The root stays the unnamed one; instantiation puts the name back on (instantiate_inner)
            fresh->conditional = ty->conditional;
        } else if (ty->kind == TY_REFERENCE) {
            fresh->reference = ty->reference; // (The same node and mapper: the same arguments)
            fresh->reference.state = arena_new(&my arena, TyObjectState);
        } else {
            fresh->anonymous = ty->anonymous;
            fresh->anonymous.realiased = true;
            fresh->anonymous.state = arena_new(&my arena, TyObjectState);
        }
        fresh->alias = alias;
        fresh->alias_args = ty_list_copy(me, alias_args.data, alias_args.len);
        *intern_slot(me, &my types, stack_key, count) = fresh;
        ret = fresh;
    }
    return ret;
}

// What an instantiation of a type operator made by an alias goes by: the alias, with its arguments instantiated
static Ty const* instantiate_alias(Checker* me, Ty const* ty, Ty const* result, TyMapper const* mapper) {
    if (!ty->alias) return result;
    Ty const* alias_buf[16];
    TyList alias_args = instantiate_alias_args(me, ty, mapper, alias_buf);
    return ty_realias(me, result, ty->alias, alias_args);
}

// A distributive conditional type instantiated once per member of union `check_type`, with the check type parameter
// mapped to that member everywhere. tsc's mapType: over the members of the union's origin, a named union among them
// mapped the same way, so one whose members all map to themselves is still itself (`Exclude<Color | undefined,
// undefined>` is `Color`), and so is the whole union, origin and all (`Extract<keyof M, string>` is still `keyof M`)
static Ty const* distribute_conditional(Checker* me, Ty const* root, TyMapper const* combined, Ty const* check_type) {
    Ty const* origin = check_type->composite.origin;
    TyList members = origin && origin->kind == TY_UNION ? origin->composite.types : check_type->composite.types;
    Ty const* small[8];
    Ty const** results = members.len <= c_arraylen(small) ? small : malloc(sizeof(Ty const*) * members.len);
    bool changed = false;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        if (member->kind == TY_UNION) {
            results[i] = distribute_conditional(me, root, combined, member);
        } else {
            TyMapper const* member_mapper = ty_mapper_with(me, combined, ty_non_distributed(root->conditional.check), member);
            results[i] = conditional_from_root(me, root, member, member_mapper);
            // A member that stays deferred goes by the root's alias (tsc's getConditionalType: root.aliasSymbol with
            // its arguments instantiated): `Awaited<T | Yadda>` is `Yadda | Awaited<T>`
            if (results[i]->kind == TY_CONDITIONAL && !results[i]->alias && results[i]->conditional.root == root) {
                Ty const* root_aliased = root->conditional.root_aliased;
                if (root_aliased) results[i] = instantiate_alias(me, root_aliased, results[i], results[i]->conditional.mapper);
            }
        }
        changed |= results[i] != member;
    }
    Ty const* ret = changed ? ty_union(me, results, members.len, nullptr, (TyList){0}) : check_type;
    if (results != small) free(results);
    return ret;
}

// Start over from the conditional type as written, with everything that has happened to it since
static Ty const* resolve_conditional_instance(Checker* me, Ty const* root, TyMapper const* combined, bool* distributed) {
    Ty const* check_type = ty_instantiate(me, ty_actual_type_variable(me, root->conditional.check), combined);
    if (root->conditional.distributive && root->conditional.check->kind == TY_TYPE_PARAM &&
        check_type != ty_non_distributed(root->conditional.check)
    ) {
        // (The distribution type is reduced, tsc's getConditionalTypeInstantiation)
        if ((check_type->kind == TY_UNION || check_type->kind == TY_INTERSECTION)) check_type = ty_reduced(me, check_type);
        // Distribution: the whole conditional type is instantiated once per member, with the check type
        // parameter mapped to that member everywhere
        if (check_type->flags & TYF_NEVER) return check_type;
        if (check_type->kind == TY_UNION) {
            *distributed = true;
            return distribute_conditional(me, root, combined, check_type);
        }
    }
    return conditional_from_root(me, root, check_type, combined);
}

// Like tsc's getConditionalTypeInstantiation: the mapper is cut down to the root's outer type parameters, which is
// also what the result is cached by, so the same conditional type under another mapper that agrees on those is one
// lookup, and mappers don't grow with every level of nesting
static Ty const* instantiate_conditional(Checker* me, Ty const* ty, TyMapper const* mapper, bool* distributed) {
    Ty const* root = ty->conditional.root;
    TyConditionalOuter const* outer = ty_conditional_outer(me, root);
    if (!outer || outer->unknown) {
        TyMapper const* combined = without_own_infers(me, root, ty_compose_mappers(me, ty->conditional.mapper, mapper));
        return resolve_conditional_instance(me, root, combined, distributed);
    }

    u32 len = outer->params.len;
    u64 stack_key[34];
    Ty const* stack_targets[32];
    // (Two target ids per word)
    u32 key_len = 2 + (len + 1) / 2;
    u64* key = key_len <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * key_len);
    Ty const** targets = len <= c_arraylen(stack_targets) ? stack_targets : malloc(sizeof(Ty const*) * len);
    key[0] = my for_constraint_root == root ? KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT : KEY_CONDITIONAL_INSTANCE;
    key[1] = root->id;
    for (u32 i = 0; i < len; i++) {
        // What the parameter maps to through this conditional type's mapper and then the new one: no need to compose
        // the two, which would instantiate every one of the first's targets
        targets[i] = ty_instantiate(me, ty_instantiate(me, outer->params.data[i], ty->conditional.mapper), mapper);
        if (i % 2) key[2 + i / 2] |= (u64)targets[i]->id << 32;
        else key[2 + i / 2] = targets[i]->id;
    }
    Ty const* const* cached = intern_get(&my types, key, key_len);
    if (cached && (uintptr_t)cached[2]) {
        // One cut short by the limits stays what it is, like tsgo's (root.instantiations keeps the errorType), and
        // isn't reported again: the hit was reported where it happened (from a shallower stack a report would name
        // other types, TS2589/TS5114 next to tsgo's one TS5115). Users are only told it was cut short
        my instantiation_limit_hits++;
    }
    Ty const* ret = nullptr;
    if (cached) {
        ret = cached[0];
        *distributed = (bool)(uintptr_t)cached[1];
    } else {
        // Only what changes: identity mappings say nothing
        Ty const** sources = malloc(sizeof(Ty const*) * (len ? len * 2 : 1));
        Ty const** kept = sources + len;
        u32 kept_len = 0;
        for (u32 i = 0; i < len; i++) {
            // (Except for a type parameter with a distributed twin: mapping it to itself maps the twin back to it, as
            // tsgo's newTypeMapper of all the outer type parameters does, plan 57)
            Ty const* param = outer->params.data[i];
            if (targets[i] == param && !(param->kind == TY_TYPE_PARAM && param->param.state->distributed)) continue;
            sources[kept_len] = outer->params.data[i];
            kept[kept_len++] = targets[i];
        }
        TyMapper const* restricted = ty_mapper_new(
            me, (TyList){ .data = sources, .len = kept_len }, (TyList){ .data = kept, .len = kept_len }
        );
        free(sources);
        u32 limit_hits = my instantiation_limit_hits;
        ret = resolve_conditional_instance(me, root, restricted, distributed);
        // A result cut short by the instantiation limits (TS2589) isn't what this instantiation is: it is remembered only
        // while the count that ran out lasts, which keeps a runaway recursion from recomputing it exponentially
        bool cut_short = my instantiation_limit_hits != limit_hits;
        Ty const** entry = arena_new_n(&my arena, Ty const*, 3);
        entry[0] = ret;
        entry[1] = (Ty const*)(uintptr_t)*distributed;
        entry[2] = (Ty const*)(uintptr_t)(cut_short ? (uintptr_t)my instantiation_epoch + 1 : 0);
        *intern_slot(me, &my types, key, key_len) = entry;
    }
    if (key != stack_key) free(key);
    if (targets != stack_targets) free(targets);
    return ret;
}

static Ty const* instantiate_inner(Checker* me, Ty const* ty, TyMapper const* mapper, bool tail) {
    Ty const* stack_buf[16];
    switch ((TyKind)ty->kind) {
        case TY_INTRINSIC:
        case TY_LITERAL:
        case TY_DECLARED:
            return ty;

        case TY_TYPE_PARAM: {
            // (A distributed type parameter is mapped like its original: tsgo's getMappedType)
            Ty const* key = ty_non_distributed(ty);
            for (u32 i = 0; i < mapper->len; i++) {
                if (mapper->sources[i] == key) return mapper->targets[i];
            }
            return ty;
        }

        default:
            break;
    }
    // The instantiation stack, for TS5114 (tsc's instantiationStack): recorded here, out of ty_instantiate()'s hot
    // path, for the types that instantiate others (a leaf never has a deeper frame above it). A tail call keeps its
    // caller's entry (tsc's getConditionalType resolves it in a loop, inside the instantiation of the conditional type
    // it started with)
    if (my instantiation_depth && !tail) my instantiation_stack[my instantiation_depth - 1] = ty;
    switch ((TyKind)ty->kind) {
        case TY_INTRINSIC:
        case TY_LITERAL:
        case TY_DECLARED:
        case TY_TYPE_PARAM:
            impossible;

        case TY_REFERENCE: {
            if (ty->reference.deferred) {
                // Another deferred reference, whether or not this one's arguments are resolved by now (tsc's
                // getObjectTypeInstantiation): making an ordinary one of it would instantiate the arguments, and a
                // recursive alias through them, depending on who asked first
                TyDeferredRef const* deferred = ty->reference.deferred;
                Ty const* alias_buf[16];
                TyList alias_args = instantiate_alias_args(me, ty, mapper, alias_buf);
                return ty_deferred_reference(
                    me, deferred->node, deferred->ctx, ty_compose_mappers(me, deferred->mapper, mapper), ty->alias, alias_args
                );
            }
            TyList args = ty->reference.args;
            Ty const** buf = args.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * args.len);
            TyList mapped = instantiate_list(me, args, mapper, buf, args.len);
            Ty const* this_arg = ty->reference.this_arg;
            Ty const* mapped_this = this_arg ? ty_instantiate(me, this_arg, mapper) : nullptr;
            Ty const* ret = mapped.data == args.data && mapped_this == this_arg ? ty :
                ty_reference_with_this(me, ty->reference.target, mapped, mapped_this);
            if (buf != stack_buf) free(buf);
            return ret;
        }

        case TY_ANONYMOUS:
            return instantiate_anonymous(me, ty, mapper);

        case TY_UNION:
        case TY_INTERSECTION: {
            // A union with a union or intersection origin is that origin instantiated (tsc's instantiateTypeWorker):
            // `T & ({} | undefined)` stays one for T = U
            Ty const* origin = ty->kind == TY_UNION ? ty->composite.origin : nullptr;
            if (origin && origin->kind != TY_UNION && origin->kind != TY_INTERSECTION) origin = nullptr;
            TyList types = origin ? origin->composite.types : ty->composite.types;
            Ty const** buf = types.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * types.len);
            TyList mapped = instantiate_list(me, types, mapper, buf, types.len);
            Ty const* alias_buf[16];
            TyList alias_args = instantiate_alias_args(me, ty, mapper, alias_buf);
            Ty const* ret = ty;
            if (mapped.data != types.data || alias_args.data != ty->alias_args.data) {
                ret = ty->kind == TY_UNION && !(origin && origin->kind == TY_INTERSECTION) ?
                    ty_union(me, mapped.data, mapped.len, ty->alias, alias_args) :
                    ty_intersection(me, mapped.data, mapped.len, ty->alias, alias_args);
            }
            if (buf != stack_buf) free(buf);
            return ret;
        }

        case TY_KEYOF:
            return ty_keyof(me, ty_instantiate(me, ty->keyof.operand, mapper));

        case TY_TEMPLATE_LITERAL: {
            TyList types = ty->template_literal.types;
            Ty const** buf = types.len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * types.len);
            TyList mapped = instantiate_list(me, types, mapper, buf, types.len);
            Ty const* ret = mapped.data == types.data ? ty : ty_template_literal(me, ty->template_literal.texts, mapped.data, mapped.len);
            if (buf != stack_buf) free(buf);
            return ret;
        }

        case TY_EVOLVING_ARRAY:
            impossible; // (Never leaves a flow query)

        case TY_STRING_MAPPING:
            return ty_string_mapping(
                me, ty->string_mapping.mapping, ty->string_mapping.symbol, ty_instantiate(me, ty->string_mapping.type, mapper)
            );

        case TY_INDEXED: {
            Ty const* index = ty_instantiate(me, ty->indexed.index, mapper);
            Ty const* object = ty_instantiate(me, ty->indexed.object, mapper);
            Ty const* ret = ty_indexed_instantiated(me, object, index, ty->indexed.include_undefined);
            // (A union of what the members of a union index give goes by the alias too: tsc's
            // getIndexedAccessTypeOrUndefined passes it to getUnionType)
            bool distributed = index->kind == TY_UNION && ret->kind == TY_UNION;
            // (Only the deferred access of these: a property's type that is an indexed access itself, `I[3]` read out
            // of `{ n: I[3] }["n"]`, is that type, getIndexedAccessType names only the IndexedAccessType it creates)
            bool deferred = ret->kind == TY_INDEXED && ret->indexed.object == object && ret->indexed.index == index;
            return deferred || distributed ? instantiate_alias(me, ty, ret, mapper) : ret;
        }

        case TY_SUBSTITUTION: {
            // (tsc's instantiateType) It stays a substitution only while the base is a type variable: otherwise it is
            // the base if that satisfies the constraint the same way the true branch was taken, else both
            Ty const* base = ty_instantiate(me, ty->substitution.base, mapper);
            if (ty_is_no_infer(ty)) return ty_no_infer(me, base);
            Ty const* constraint = ty_instantiate(me, ty->substitution.constraint, mapper);
            bool variable = base->kind == TY_TYPE_PARAM || base->kind == TY_INDEXED;
            if (variable && (ty_is_generic_object(me, constraint) || ty_is_generic_index(constraint))) {
                return ty_substitution(me, base, constraint);
            }
            if ((constraint->flags & (TYF_ANY | TYF_UNKNOWN)) || ty_definitely_assignable(me, base, constraint)) return base;
            if (variable) return ty_substitution(me, base, constraint);
            Ty const* both[2] = { constraint, base };
            return ty_intersection(me, both, 2, nullptr, (TyList){0});
        }

        case TY_CONDITIONAL: {
            bool distributed = false;
            Ty const* ret = instantiate_conditional(me, ty, mapper, &distributed);
            // Only a deferred one goes by the alias it was declared with (tsc's getConditionalType); a distribution
            // is a plain union unless an alias declaration names it (`type A = Awaited<X | Y>`, lower.c)
            // A deferred conditional type of another root (the branch it resolved to, `Assume<X, U>` of
            // `Equal<A, true> extends true ? Assume<X, U> : X`) keeps its own alias
            Ty const* root = ty->conditional.root ? ty->conditional.root : ty;
            bool same_root = ret->kind == TY_CONDITIONAL && (ret->conditional.root ? ret->conditional.root : ret) == root;
            if (distributed || !same_root) return ret;
            // (tsc's getConditionalType names it by its root's alias, instantiated from the root, whatever alias this
            // one had: `type W<O> = Extract<O, string>` declares W<O>, but W<X> is Extract<X, string>)
            // (Only one that goes by an alias asks: one without keeps none, as before)
            Ty const* root_aliased = ty->alias ? root->conditional.root_aliased : nullptr;
            if (root_aliased) {
                // (The alias arguments through the mapper the result was made from the root with, tsc's
                // instantiateTypes(root.aliasTypeArguments, mapper): instantiating this one's own alias arguments, or
                // composing its mapper with the new one, would instantiate whole types each time)
                return instantiate_alias(me, root_aliased, ret, ret->conditional.mapper);
            }
            return instantiate_alias(me, ty, ret, mapper);
        }
    }
    impossible;
}

// What the types instantiated with a mapper gave while an instantiation with it is in progress (tsc's
// activeTypeMappersCaches): `N<N<N<P>>>` mentions its argument several times at each level (`keyof X`, `X[K]`, the alias
// argument), and instantiating it again for each would be exponential in the nesting
typedef struct ActiveMapper {
    TyMapper const* mapper;
    u64* keys; // Type id and for_constraint_root id, 0 for an empty slot
    Ty const** values;
    u32* used; // The slots in use, to clear only those
    u32 cap, len;
} ActiveMapper;

#define MAX_ACTIVE_MAPPERS 64

void ty_active_mappers_drop(Checker* me) {
    for (u32 i = 0; i < my active_mapper_cap; i++) {
        free(my active_mappers[i].keys);
        free(my active_mappers[i].values);
        free(my active_mappers[i].used);
    }
    free(my active_mappers);
}

static inline u64 active_key(Checker* me, Ty const* ty) {
    return ((u64)(my for_constraint_root ? my for_constraint_root->id : 0) << 32 | ty->id) + 1;
}

static Ty const** active_slot(ActiveMapper* am, u64 key, bool insert) {
    if (!am->cap) {
        if (!insert) return nullptr;
        am->cap = 64;
        am->keys = calloc(am->cap, sizeof(u64));
        am->values = malloc(sizeof(Ty const*) * am->cap);
        am->used = malloc(sizeof(u32) * am->cap);
        panic_if(!am->keys || !am->values || !am->used, "out of memory");
    } else if (insert && (am->len + 1) * 4 > am->cap * 3) {
        u32 old_cap = am->cap;
        u64* old_keys = am->keys;
        Ty const** old_values = am->values;
        am->cap *= 2;
        am->keys = calloc(am->cap, sizeof(u64));
        am->values = malloc(sizeof(Ty const*) * am->cap);
        free(am->used);
        am->used = malloc(sizeof(u32) * am->cap);
        panic_if(!am->keys || !am->values || !am->used, "out of memory");
        u32 n = 0;
        for (u32 i = 0; i < old_cap; i++) {
            if (!old_keys[i]) continue;
            u32 j = (u32)(old_keys[i] * 0x9E3779B97F4A7C15ull >> 32) & (am->cap - 1);
            while (am->keys[j]) j = (j + 1) & (am->cap - 1);
            am->keys[j] = old_keys[i];
            am->values[j] = old_values[i];
            am->used[n++] = j;
        }
        free(old_keys);
        free(old_values);
    }
    u32 j = (u32)(key * 0x9E3779B97F4A7C15ull >> 32) & (am->cap - 1);
    while (am->keys[j]) {
        if (am->keys[j] == key) return &am->values[j];
        j = (j + 1) & (am->cap - 1);
    }
    if (!insert) return nullptr;
    am->keys[j] = key;
    am->used[am->len++] = j;
    return &am->values[j];
}

Ty const* ty_instantiate_mapped(Checker* me, Ty const* ty, TyMapper const* mapper, bool tail) {
    if (tail && ty->kind == TY_CONDITIONAL) {
        if (my instantiation_tail_count >= MAX_TAIL_COUNT || my instantiation_count >= MAX_INSTANTIATION_COUNT) {
            ty_instantiation_limit_reached(me);
            return ty_intrinsic(me, TY_ERROR);
        }
        // (Only conditional types with an alias count, like tsc's canTailRecurse: `A extends X ? (B extends Y ? Z :
        // Alias<...>) : W` is one step per Alias)
        u32 counts = ty->alias != nullptr;
        my instantiation_tail_count += counts;
        my instantiation_count++;
        Ty const* ret = instantiate_inner(me, ty, mapper, true);
        my instantiation_tail_count -= counts;
        return ret;
    }
    if (my instantiation_depth >= MAX_INSTANTIATION_DEPTH || my instantiation_count >= MAX_INSTANTIATION_COUNT) {
        ty_instantiation_limit_reached(me);
        return ty_intrinsic(me, TY_ERROR);
    }
    // A mapper already being instantiated with further out: what this type gave with it before (tsc's
    // instantiateTypeWithAlias). The outermost call itself isn't remembered, its cache goes when it returns
    // (Not a type parameter: that's a lookup in the mapper, cheaper than the cache)
    i32 active = -1;
    for (u32 i = ty->kind == TY_TYPE_PARAM ? 0 : my active_mapper_count; i-- > 0;) {
        if (my active_mappers[i].mapper == mapper) {
            active = (i32)i;
            break;
        }
    }
    u64 key = 0;
    bool pushed = false;
    if (active >= 0) {
        key = active_key(me, ty);
        Ty const** slot = active_slot(&my active_mappers[active], key, false);
        if (slot) return *slot;
    } else if (ty->kind != TY_TYPE_PARAM && my active_mapper_count < MAX_ACTIVE_MAPPERS) {
        if (my active_mapper_count == my active_mapper_cap) {
            u32 cap = my active_mapper_cap ? my active_mapper_cap * 2 : 8;
            my active_mappers = realloc(my active_mappers, sizeof(ActiveMapper) * cap);
            panic_if(!my active_mappers, "out of memory");
            memset(my active_mappers + my active_mapper_cap, 0, sizeof(ActiveMapper) * (cap - my active_mapper_cap));
            my active_mapper_cap = cap;
        }
        my active_mappers[my active_mapper_count++].mapper = mapper;
        pushed = true;
    }
    my instantiation_depth++;
    my instantiation_count++;
    // Tail calls are counted per conditional type being resolved (tsc's tailCount is local to getConditionalType): what
    // one instantiates along the way starts over
    u32 tail_count = my instantiation_tail_count;
    my instantiation_tail_count = 0;
    u32 limit_hits = my instantiation_limit_hits;
    Ty const* ret = instantiate_inner(me, ty, mapper, false);
    my instantiation_tail_count = tail_count;
    my instantiation_depth--;
    if (pushed) {
        ActiveMapper* am = &my active_mappers[--my active_mapper_count];
        am->mapper = nullptr;
        if (am->cap > 4096) { // (A big one isn't cleared at every outermost instantiation)
            free(am->keys);
            free(am->values);
            free(am->used);
            *am = (ActiveMapper){0};
        } else {
            for (u32 i = 0; i < am->len; i++) am->keys[am->used[i]] = 0;
            am->len = 0;
        }
    } else if (active >= 0 && my instantiation_limit_hits == limit_hits) {
        // (Not one cut short by the limits: whoever asks again runs into them too, and the conditional instances on
        // the way know theirs aren't what they are, instantiate_conditional())
        *active_slot(&my active_mappers[active], key, true) = ret;
    }
    return ret;
}
