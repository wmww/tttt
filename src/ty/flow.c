#include "ty_internal.h"
#include "../flow.h"
#include "../function_impl.h"
#include "../binding.h"
#include "../statement.h"
#include "../scope.h"
#include "../compiler_options.h"
#include "../program.h"
#include "../source_file.h"

// Queries on the control flow graph (flow.h, plan 05). The graph is read-only and may be shared by several checkers,
// so everything that is remembered is remembered in the Checker.
//
// First reachability (tsc's isReachableFlowNode()), then narrowing (getFlowTypeOfReference()).

// try/finally: the antecedents a label has while walking back through a reduce label
typedef struct FlowLabelOverride {
    struct FlowLabelOverride const* outer;
    FlowNode const* label;
    FlowList const* antecedents;
} FlowLabelOverride;

static FlowList const* label_antecedents(FlowNode const* label, FlowLabelOverride const* overrides) {
    for (FlowLabelOverride const* i = overrides; i; i = i->outer) {
        if (i->label == label) return i->antecedents;
    }
    return label->antecedents;
}

static CallEffects call_effects(Checker* me, Expr const* call_expr, bool is_statement);

// `assert(false)`, `assert(false && x)`, `assert(false || false)`
static bool is_false_expr(Expr const* expr) {
    if (expr->tag == EXPR_LITERAL) return expr->literal.tag == VALUE_BOOLEAN && !expr->literal.boolean;
    if (expr->tag != EXPR_BINARY_OP) return false;
    BinaryOpExpr const* binary = expr->binary_op;
    if (binary->op == BINARY_OP_AND_AND) return is_false_expr(&binary->left) || is_false_expr(&binary->right);
    if (binary->op == BINARY_OP_OR_OR) return is_false_expr(&binary->left) && is_false_expr(&binary->right);
    return false;
}

static bool is_exhaustive_switch(Checker* me, SwitchStatement const* switch_);

static FlowReachability reachability_walk(Checker* me, FlowNode const* flow, FlowLabelOverride const* overrides, bool use_cache) {
    while (true) {
        if (flow == my last_reachability_flow) return (FlowReachability)my last_reachability;
        u32 flags = flow->flags;
        if (flags & FLOW_SHARED && use_cache && !overrides) {
            uintptr_t cached = (uintptr_t)ty_link_get(me, KEY_FLOW_REACHABLE, flow);
            FlowReachability ret = cached ? (FlowReachability)(cached - 1) : reachability_walk(me, flow, overrides, false);
            if (!cached) ty_link_set(me, KEY_FLOW_REACHABLE, flow, (void const*)((uintptr_t)ret + 1));
            return ret;
        }
        use_cache = true;
        if (flags & (FLOW_ASSIGNMENT | FLOW_CONDITION | FLOW_ARRAY_MUTATION)) {
            flow = flow->antecedent;
        } else if (flags & FLOW_CALL) {
            Expr const* call_expr = flow->node;
            CallEffects effects = call_effects(me, call_expr, true);
            if (effects.returns_never) return FLOW_REACH_NO;
            if (effects.has_predicate && effects.predicate.asserts && !effects.predicate.is_this && !effects.predicate.type &&
                effects.predicate.param_index >= 0 && effects.predicate.param_index < vec_expr_size(&call_expr->call->args)
            ) {
                if (is_false_expr(vec_expr_at(&call_expr->call->args, effects.predicate.param_index))) return FLOW_REACH_NO;
            }
            flow = flow->antecedent;
        } else if (flags & FLOW_BRANCH_LABEL) {
            FlowList const* antecedents = label_antecedents(flow, overrides);
            FlowReachability ret = FLOW_REACH_NO;
            for (u32 i = 0; antecedents && i < antecedents->count && ret != FLOW_REACH_YES; i++) {
                FlowReachability branch = reachability_walk(me, antecedents->items[i], overrides, true);
                if (branch != FLOW_REACH_NO) ret = branch;
            }
            return ret;
        } else if (flags & FLOW_LOOP_LABEL) {
            // A loop is reachable if the way to its top is
            FlowList const* antecedents = label_antecedents(flow, overrides);
            if (!antecedents || !antecedents->count) return FLOW_REACH_NO;
            flow = antecedents->items[0];
        } else if (flags & FLOW_SWITCH_CLAUSE) {
            FlowSwitchClause const* clause = flow->node;
            if (clause->clause_start == clause->clause_end && is_exhaustive_switch(me, clause->switch_)) return FLOW_REACH_NO;
            flow = flow->antecedent;
        } else if (flags & FLOW_REDUCE_LABEL) {
            FlowReduceLabel const* reduce = flow->node;
            my last_reachability_flow = nullptr; // (Unreliable once labels are overridden)
            FlowLabelOverride override = { overrides, reduce->target, reduce->antecedents };
            FlowReachability ret = reachability_walk(me, flow->antecedent, &override, true);
            return ret;
        } else {
            if (flags & FLOW_UNREACHABLE) return FLOW_REACH_NO;
            return FLOW_REACH_YES;
        }
    }
}

FlowReachability ty_flow_reachability(Checker* me, FlowNode const* flow) {
    if (!flow) return FLOW_REACH_NO;
    FlowReachability ret = reachability_walk(me, flow, nullptr, true);
    my last_reachability_flow = flow;
    my last_reachability = (unsigned char)ret;
    return ret;
}

FlowReachability ty_function_end_reachability(Checker* me, FunctionImpl const* function) {
    return ty_flow_reachability(me, function_impl_end_flow(function));
}

// Narrowing: the type of a reference at a flow node (tsc's getFlowTypeOfReference(), whose structure this follows
// closely because the `.types` baseline prints every narrowed type).

typedef struct FlowType {
    Ty const* type;
    bool incomplete; // Computed while a loop it is in hasn't reached its fixpoint: not to be remembered
} FlowType;

typedef struct FlowQuery {
    Checker* me;
    Expr const* reference;
    Symbol const* symbol; // Identifier references: the variable
    bool identifier; // The reference is an identifier (not a keyword) under its `!`s and `satisfies`, `symbol` its target
    // A binding pattern as a pseudo reference (reference is then a placeholder that matches nothing): what its sibling
    // elements' discriminants say about the destructured value (tsc's getNarrowedTypeOfSymbol)
    Binding const* pattern;
    // A function as a pseudo reference, the same for its parameters (tsc's getNarrowedTypeOfSymbol of a parameter of a
    // function contextually typed by a rest parameter of a union of tuples)
    FunctionImpl const* parameters_of;
    Ty const* declared;
    Ty const* initial;
    u32 key; // 0: none (not computed, or the reference has none)
    bool key_set;
    u32 shared_start;
    // query_matches(): access_name() of the reference's dot accesses by depth (`a.b.c`: c, b), bit i of ref_names_set
    // when ref_names[i] is
    Atom const* ref_names[4];
    uint8_t ref_names_set;
    // query_subset_of()'s last answers for a big union target (q->declared, q->initial): asked at every discriminant
    // and every branch label, mostly about the same few types
    struct { Ty const* source; Ty const* target; bool result; } subset_memo[2];
    u32 depth;
    bool left_container; // The walk continued outside of the function the reference is in
    FlowLabelOverride const* overrides;
    bool substitutes_constraints; // See Checker.flow_substitutes_constraints
} FlowQuery;

// An incomplete never (narrowed away while a loop is still at a partial type, or a label's union of such) is the
// silent never, which reports nothing when operators are applied to it: control could get here once the loop's type
// is complete (tsc's newFlowType; a guard in an `if` whose other branch calls a `never` function joins it at the label)
static FlowType new_flow_type(FlowQuery* q, Ty const* type, bool incomplete) {
    if (incomplete && (type->flags & TYF_NEVER)) type = ty_intrinsic(q->me, TY_SILENT_NEVER);
    return (FlowType){ type, incomplete };
}

static FlowType type_at_flow_node(FlowQuery* q, FlowNode const* flow);
static Ty const* narrow_type(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_true, bool nullish);
static bool is_type_subset_of(Ty const* source, Ty const* target);
static bool query_subset_of(FlowQuery* q, Ty const* source, Ty const* target);

static bool is_matching_reference(Checker* me, Expr const* source, Expr const* target);
static bool query_matches(FlowQuery* q, Expr const* target);
static Expr const* reference_candidate(Expr const* expr);

// Evolving arrays (tsc's EvolvingArrayType, TY_EVOLVING_ARRAY): what an auto-typed variable (`let x = []`, `let x;`
// after `x = []`) is while control flow sees elements pushed. They never leave a flow query: ty_flow_type() gives the
// final array type

static bool is_auto(Checker* me, Ty const* declared) {
    return declared == ty_intrinsic(me, TY_AUTO) || (declared == my auto_array && declared);
}

static Ty const* evolving_array(Checker* me, Ty const* element) {
    u64 key[2] = { KEY_EVOLVING_ARRAY, element->id };
    Ty const** slot = (Ty const**)intern_slot(me, &my links, key, 2);
    if (!*slot) {
        Ty* ty = ty_alloc(me, TY_EVOLVING_ARRAY, TYF_OBJECT);
        ty->evolving_array.element = element;
        *slot = ty;
    }
    return *slot;
}

static Ty const* evolving_element(Checker* me, Ty const* ty) {
    return ty->kind == TY_EVOLVING_ARRAY ? ty->evolving_array.element : ty_intrinsic(me, TY_NEVER);
}

// tsc's finalizeEvolvingArrayType: the array type of what was pushed, subtype reduced (the auto array if nothing was)
static Ty const* final_array_type(Checker* me, Ty const* ty) {
    if (ty->kind != TY_EVOLVING_ARRAY) return ty;
    if (!__atomic_load_n(&ty->evolving_array.final_array, __ATOMIC_ACQUIRE)) {
        Ty const* element = ty->evolving_array.element;
        Ty const* final_array;
        if (element->flags & TYF_NEVER) {
            final_array = ty_auto_array(me);
        } else {
            if (element->kind == TY_UNION) {
                element = ty_union_subtype_reduced(me, element->composite.types.data, element->composite.types.len);
            }
            final_array = ty_array(me, element, false);
        }
        // (A cache; with --shared-graph another checker's may be there first, the same)
        Ty const* unset = nullptr;
        __atomic_compare_exchange_n(&((Ty*)ty)->evolving_array.final_array, &unset, final_array, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    }
    return ty->evolving_array.final_array;
}

static Ty const* flow_context_free_type(Checker* me, Expr const* expr);

// tsc's addEvolvingArrayElementType: no subtype reduction until the final array type
static Ty const* add_evolving_element(Checker* me, Ty const* evolving, Expr const* expr) {
    Ty const* element = ty_regular_object_literal(me, ty_regular(me, ty_base_type_of_literal(me, flow_context_free_type(me, expr))));
    Ty const* current = evolving->evolving_array.element;
    if (is_type_subset_of(element, current)) return evolving;
    Ty const* types[2] = { current, element };
    return evolving_array(me, ty_union(me, types, 2, nullptr, (TyList){0}));
}

// tsc's recombineUnknownType: `{} | null | undefined` made of the `{}` narrowing `unknown` makes is `unknown` again
static Ty const* recombine_unknown(Checker* me, Ty const* ty) {
    if (ty->kind != TY_UNION || ty->composite.types.len != 3 || !compiler_options()->strict_null_checks) return ty;
    Ty const* parts[3] = { ty_unknown_empty_object_type(me), ty_intrinsic(me, TY_NULL), ty_intrinsic(me, TY_UNDEFINED) };
    return ty == ty_union(me, parts, 3, nullptr, (TyList){0}) ? ty_intrinsic(me, TY_UNKNOWN_) : ty;
}

// tsc's getUnionOrEvolvingArrayType: evolving arrays (and nevers) join into an evolving array, anything else makes
// them final
static Ty const* union_or_evolving(Checker* me, Ty const* const* types, u32 len, bool subtype_reduction) {
    bool has_evolving = false;
    bool all_evolving = true;
    for (u32 i = 0; i < len; i++) {
        if (types[i]->kind == TY_EVOLVING_ARRAY) has_evolving = true;
        else if (!(types[i]->flags & TYF_NEVER)) all_evolving = false;
    }
    if (!has_evolving) {
        return recombine_unknown(me, subtype_reduction ? ty_union_subtype_reduced(me, types, len) : ty_union(me, types, len, nullptr, (TyList){0}));
    }
    TyBuf mapped = {0};
    for (u32 i = 0; i < len; i++) {
        ty_buf_push(&mapped, all_evolving ? evolving_element(me, types[i]) : final_array_type(me, types[i]));
    }
    Ty const* ret;
    if (all_evolving) {
        ret = evolving_array(me, ty_union(me, mapped.data, mapped.len, nullptr, (TyList){0}));
    } else {
        ret = recombine_unknown(me, subtype_reduction ? ty_union_subtype_reduced(me, mapped.data, mapped.len) :
            ty_union(me, mapped.data, mapped.len, nullptr, (TyList){0}));
    }
    ty_buf_drop(&mapped);
    return ret;
}

// tsc's isTypeAssignableToKind(type, NumberLike): what the index of `x[i] = v` must be to push v
static bool is_number_like_index(Checker* me, Expr const* index) {
    Ty const* ty = ty_flow_expr_type(me, index);
    return (ty->flags & (TYF_NUMBER | TYF_NUMBER_LITERAL)) || ty_is_related(me, ty, ty_intrinsic(me, TY_NUMBER), TY_RELATION_ASSIGNABLE);
}

bool ty_is_evolving_array_operation_target(Checker* me, Expr const* reference) {
    if (reference->tag == EXPR_PROPERTY) return reference->property->evolving_access;
    if (reference->tag != EXPR_IDENTIFIER) return false;
    switch ((EvolvingArrayOp)reference->identifier.evolving_op) {
        case EVOLVING_OP_NONE: return false;
        case EVOLVING_OP_ACCESS: return true;
        case EVOLVING_OP_ELEMENT_ASSIGNMENT: return is_number_like_index(me, identifier_evolving_index(&reference->identifier));
    }
    impossible;
}

// tsc's getTypeAtFlowArrayMutation. False: the node isn't about the reference
static bool type_at_array_mutation(FlowQuery* q, FlowNode const* flow, FlowType* out) {
    Checker* me = q->me;
    if (!is_auto(me, q->declared)) return false;
    Expr const* node = flow->node;
    Expr const* object;
    if (node->tag == EXPR_CALL) {
        Expr const* callee = &node->call->callable;
        while (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
        object = &callee->property->expr;
    } else {
        object = &node->binary_op->left.property->expr;
    }
    if (!query_matches(q, reference_candidate(object))) return false;
    FlowType before = type_at_flow_node(q, flow->antecedent);
    Ty const* evolved = before.type;
    if (evolved->kind == TY_EVOLVING_ARRAY) {
        if (node->tag == EXPR_CALL) {
            for_each (i, vec_expr, &node->call->args) evolved = add_evolving_element(me, evolved, i);
        } else if (is_number_like_index(me, &node->binary_op->left.property->index)) {
            evolved = add_evolving_element(me, evolved, &node->binary_op->right);
        }
    }
    *out = new_flow_type(q, evolved, before.incomplete);
    return true;
}

// Type facts (tsc's TypeFacts, bit for bit)

enum {
    TF_TYPEOF_EQ_STRING = 1 << 0,
    TF_TYPEOF_EQ_NUMBER = 1 << 1,
    TF_TYPEOF_EQ_BIGINT = 1 << 2,
    TF_TYPEOF_EQ_BOOLEAN = 1 << 3,
    TF_TYPEOF_EQ_SYMBOL = 1 << 4,
    TF_TYPEOF_EQ_OBJECT = 1 << 5,
    TF_TYPEOF_EQ_FUNCTION = 1 << 6,
    TF_TYPEOF_EQ_HOST_OBJECT = 1 << 7,
    TF_TYPEOF_NE_STRING = 1 << 8,
    TF_TYPEOF_NE_NUMBER = 1 << 9,
    TF_TYPEOF_NE_BIGINT = 1 << 10,
    TF_TYPEOF_NE_BOOLEAN = 1 << 11,
    TF_TYPEOF_NE_SYMBOL = 1 << 12,
    TF_TYPEOF_NE_OBJECT = 1 << 13,
    TF_TYPEOF_NE_FUNCTION = 1 << 14,
    TF_TYPEOF_NE_HOST_OBJECT = 1 << 15,
    TF_EQ_UNDEFINED = 1 << 16,
    TF_EQ_NULL = 1 << 17,
    TF_EQ_UNDEFINED_OR_NULL = 1 << 18,
    TF_NE_UNDEFINED = 1 << 19,
    TF_NE_NULL = 1 << 20,
    TF_NE_UNDEFINED_OR_NULL = 1 << 21,
    TF_TRUTHY = 1 << 22,
    TF_FALSY = 1 << 23,
    TF_IS_UNDEFINED = 1 << 24,
    TF_IS_NULL = 1 << 25,
    TF_IS_UNDEFINED_OR_NULL = TF_IS_UNDEFINED | TF_IS_NULL,
    TF_ALL = (1 << 27) - 1,

    TF_ALL_TYPEOF_NE = TF_TYPEOF_NE_STRING | TF_TYPEOF_NE_NUMBER | TF_TYPEOF_NE_BIGINT | TF_TYPEOF_NE_BOOLEAN |
        TF_TYPEOF_NE_SYMBOL | TF_TYPEOF_NE_OBJECT | TF_TYPEOF_NE_FUNCTION | TF_TYPEOF_NE_HOST_OBJECT,
    TF_NOT_NULLISH = TF_NE_UNDEFINED | TF_NE_NULL | TF_NE_UNDEFINED_OR_NULL,
    TF_MAYBE_NULLISH = TF_EQ_UNDEFINED | TF_EQ_NULL | TF_EQ_UNDEFINED_OR_NULL | TF_FALSY, // What non-strict adds
    TF_OR_FACTS_MASK = TF_TYPEOF_EQ_FUNCTION | TF_TYPEOF_NE_OBJECT,
    TF_AND_FACTS_MASK = TF_ALL & ~TF_OR_FACTS_MASK,
};

// The facts of a primitive whose typeof is `eq`: never nullish, and in non-strict mode possibly nullish after all
static u32 primitive_facts(u32 eq, bool truthy, bool falsy) {
    u32 ne = TF_ALL_TYPEOF_NE & ~(eq << 8);
    u32 facts = eq | ne | TF_NOT_NULLISH;
    if (!compiler_options()->strict_null_checks) return facts | TF_MAYBE_NULLISH | (truthy ? TF_TRUTHY : 0);
    return facts | (truthy ? TF_TRUTHY : 0) | (falsy ? TF_FALSY : 0);
}

// tsc's isEmptyAnonymousObjectType (ty_is_empty_anonymous_object())
static bool is_empty_anonymous_object(Checker* me, Ty const* ty) { return ty_is_empty_anonymous_object(me, ty); }

static bool is_function_object(Checker* me, Ty const* ty) {
    Members const* members = ty_members(me, ty);
    if (members->call_count || members->construct_count) return true;
    Ty const* function = ty_global_function_type(me);
    return function && (ty == function || (
        ty_get_prop(me, ty, my atoms.bind) && ty_is_related(me, ty, function, TY_RELATION_SUBTYPE)
    ));
}

static u32 type_facts(Checker* me, Ty const* ty, u32 mask) {
    bool strict = compiler_options()->strict_null_checks;
    u32 flags = ty->flags;
    // Generic or not, like tsc's getTypeFacts: a mapping can be empty, a template like a non-empty literal
    if (flags & TYF_STRING_MAPPING) return primitive_facts(TF_TYPEOF_EQ_STRING, true, true);
    if (flags & TYF_TEMPLATE_LITERAL) return primitive_facts(TF_TYPEOF_EQ_STRING, true, false);
    // (A substitution type, `NoInfer<X>` of an object X included, by its base constraint: the intersection of its base
    // and constraint, tsc's getTypeFacts of an Instantiable type through computeBaseConstraint)
    if (ty->kind == TY_SUBSTITUTION) return type_facts(me, ty_substitution_intersection(me, ty), mask);
    if (flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
        Ty const* constraint = ty_base_constraint(me, ty);
        if (!constraint || constraint == ty || (constraint->flags & (TYF_TYPE_PARAM | TYF_DEFERRED))) {
            return TF_ALL & ~TF_IS_UNDEFINED_OR_NULL;
        }
        return type_facts(me, constraint, mask);
    }
    if (flags & TYF_UNION) {
        u32 facts = 0;
        for (u32 i = 0; i < ty->composite.types.len; i++) facts |= type_facts(me, ty->composite.types.data[i], mask);
        return facts;
    }
    if (flags & TYF_INTERSECTION) {
        // A generic one goes by its base constraint as a whole (`string & T` with an object constraint is a string)
        bool generic = false;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* t = ty->composite.types.data[i];
            // (A substitution type of a non-generic type, `NoInfer<X>` of an object X, isn't generic: its facts are
            // its intersection's, see above)
            if (t->kind == TY_SUBSTITUTION && !(ty_substitution_intersection(me, t)->flags & (TYF_TYPE_PARAM | TYF_DEFERRED))) continue;
            generic |= (t->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) != 0;
        }
        if (generic) {
            Ty const* constraint = ty_base_constraint(me, ty);
            if (!constraint) return TF_ALL & ~TF_IS_UNDEFINED_OR_NULL;
            if (constraint != ty) return type_facts(me, constraint, mask);
        }
        // Object types next to a primitive are presumably tags (`string & { __brand: true }`)
        bool ignore_objects = false;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            ignore_objects |= ty->composite.types.data[i]->kind == TY_INTRINSIC || ty->composite.types.data[i]->kind == TY_LITERAL;
        }
        u32 ored = 0;
        u32 anded = TF_ALL;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* t = ty->composite.types.data[i];
            if (ignore_objects && (t->flags & TYF_OBJECT)) continue;
            u32 f = type_facts(me, t, mask);
            ored |= f;
            anded &= f;
        }
        return (ored & TF_OR_FACTS_MASK) | (anded & TF_AND_FACTS_MASK);
    }
    if (flags & TYF_STRING) return primitive_facts(TF_TYPEOF_EQ_STRING, true, true);
    if (flags & TYF_STRING_LITERAL) {
        bool empty = !(flags & TYF_ENUM) && ty->literal.string && ty->literal.string->len == 0;
        return primitive_facts(TF_TYPEOF_EQ_STRING, !empty, empty);
    }
    if (flags & (TYF_NUMBER | TYF_ENUM)) return primitive_facts(TF_TYPEOF_EQ_NUMBER, true, true);
    if (flags & TYF_NUMBER_LITERAL) {
        bool zero = ty->literal.value.tag == VALUE_NUMBER && ty->literal.value.number == 0;
        return primitive_facts(TF_TYPEOF_EQ_NUMBER, !zero, zero);
    }
    if (flags & TYF_BIGINT) return primitive_facts(TF_TYPEOF_EQ_BIGINT, true, true);
    if (flags & TYF_BIGINT_LITERAL) {
        bool zero = !value_to_boolean(&ty->literal.value);
        return primitive_facts(TF_TYPEOF_EQ_BIGINT, !zero, zero);
    }
    if (flags & TYF_BOOLEAN_LITERAL) {
        bool is_false = !ty->literal.value.boolean;
        return primitive_facts(TF_TYPEOF_EQ_BOOLEAN, !is_false, is_false);
    }
    if (flags & TYF_OBJECT) {
        u32 possible = TF_ALL & ~TF_IS_UNDEFINED_OR_NULL;
        if (!(mask & possible)) return 0;
        u32 nullish = strict ? 0 : TF_MAYBE_NULLISH;
        // (What every object type has or lacks, empty or a function or not, without resolving its members to tell)
        u32 const settled = TF_NOT_NULLISH | TF_TRUTHY | TF_IS_UNDEFINED_OR_NULL | TF_EQ_UNDEFINED | TF_EQ_NULL | TF_EQ_UNDEFINED_OR_NULL;
        if (strict && !(mask & ~settled)) {
            // (Except for a class or interface whose bases are being lowered: tsc's isFunctionObjectType resolves its
            // members, which finds them circular, TS2310 for `interface Id extends Pick<typeof x, "a">` with
            // `x = yd && 1`, yd an Id)
            if (my bases_frames && ty_bases_in_flight(me, ty)) (void)ty_members(me, ty);
            return (TF_NOT_NULLISH | TF_TRUTHY) & mask;
        }
        if (is_empty_anonymous_object(me, ty)) {
            return strict ? TF_ALL & ~(TF_EQ_UNDEFINED | TF_EQ_NULL | TF_EQ_UNDEFINED_OR_NULL | TF_IS_UNDEFINED_OR_NULL) :
                TF_ALL & ~TF_IS_UNDEFINED_OR_NULL;
        }
        if (is_function_object(me, ty)) {
            return TF_TYPEOF_EQ_FUNCTION | TF_TYPEOF_EQ_HOST_OBJECT | (TF_ALL_TYPEOF_NE & ~(TF_TYPEOF_NE_FUNCTION | TF_TYPEOF_NE_HOST_OBJECT)) |
                TF_NOT_NULLISH | TF_TRUTHY | nullish;
        }
        return TF_TYPEOF_EQ_OBJECT | TF_TYPEOF_EQ_HOST_OBJECT | (TF_ALL_TYPEOF_NE & ~(TF_TYPEOF_NE_OBJECT | TF_TYPEOF_NE_HOST_OBJECT)) |
            TF_NOT_NULLISH | TF_TRUTHY | nullish;
    }
    if (flags & TYF_VOID) return TF_ALL_TYPEOF_NE | TF_EQ_UNDEFINED | TF_EQ_UNDEFINED_OR_NULL | TF_NE_NULL | TF_FALSY;
    if (flags & TYF_UNDEFINED) {
        return TF_ALL_TYPEOF_NE | TF_EQ_UNDEFINED | TF_EQ_UNDEFINED_OR_NULL | TF_NE_NULL | TF_FALSY | TF_IS_UNDEFINED;
    }
    if (flags & TYF_NULL) {
        return TF_TYPEOF_EQ_OBJECT | (TF_ALL_TYPEOF_NE & ~TF_TYPEOF_NE_OBJECT) | TF_EQ_NULL | TF_EQ_UNDEFINED_OR_NULL |
            TF_NE_UNDEFINED | TF_FALSY | TF_IS_NULL;
    }
    if (flags & TYF_ES_SYMBOL_LIKE) {
        u32 facts = TF_TYPEOF_EQ_SYMBOL | (TF_ALL_TYPEOF_NE & ~TF_TYPEOF_NE_SYMBOL) | TF_NOT_NULLISH | TF_TRUTHY;
        return strict ? facts : facts | TF_MAYBE_NULLISH;
    }
    if (flags & TYF_NON_PRIMITIVE) {
        u32 facts = TF_TYPEOF_EQ_OBJECT | TF_TYPEOF_EQ_HOST_OBJECT | (TF_ALL_TYPEOF_NE & ~(TF_TYPEOF_NE_OBJECT | TF_TYPEOF_NE_HOST_OBJECT)) |
            TF_NOT_NULLISH | TF_TRUTHY;
        return strict ? facts : facts | TF_MAYBE_NULLISH;
    }
    if (flags & TYF_NEVER) return 0;
    return TF_ALL & ~TF_IS_UNDEFINED_OR_NULL; // any, unknown
}

static bool has_type_facts(Checker* me, Ty const* ty, u32 mask) {
    return (type_facts(me, ty, mask) & mask) != 0;
}

// Filtering and mapping over the members of a union

typedef bool (*TyFilterFn)(Checker* me, Ty const* ty, void const* arg);
typedef Ty const* (*TyMapFn)(Checker* me, Ty const* ty, void const* arg);

static Ty const* filter_type(Checker* me, Ty const* ty, TyFilterFn keep, void const* arg) {
    if (ty->kind != TY_UNION) {
        return (ty->flags & TYF_NEVER) || keep(me, ty, arg) ? ty : ty_intrinsic(me, TY_NEVER);
    }
    TyList members = ty->composite.types;
    TyBuf kept = {0};
    for (u32 i = 0; i < members.len; i++) {
        if (keep(me, members.data[i], arg)) ty_buf_push(&kept, members.data[i]);
    }
    Ty const* ret = ty_union_filtered(me, ty, kept.data, kept.len);
    ty_buf_drop(&kept);
    return ret;
}

static Ty const* map_type(Checker* me, Ty const* ty, TyMapFn map, void const* arg) {
    if (ty->flags & TYF_NEVER) return ty;
    if (ty->kind != TY_UNION) return map(me, ty, arg);
    // (Over what it was built as, named unions mapped as a whole: one that maps to itself keeps its name, tsc's mapType)
    Ty const* origin = ty->composite.origin;
    bool by_origin = origin && origin->kind == TY_UNION;
    TyList members = by_origin ? origin->composite.types : ty->composite.types;
    TyBuf mapped = {0};
    bool changed = false;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* t = by_origin && members.data[i]->kind == TY_UNION ? map_type(me, members.data[i], map, arg) : map(me, members.data[i], arg);
        changed |= t != members.data[i];
        ty_buf_push(&mapped, t);
    }
    Ty const* ret = changed ? ty_union(me, mapped.data, mapped.len, nullptr, (TyList){0}) : ty;
    ty_buf_drop(&mapped);
    return ret;
}

static bool keep_with_facts(Checker* me, Ty const* ty, void const* arg) {
    return has_type_facts(me, ty, *(u32 const*)arg);
}

static Ty const* type_with_facts(Checker* me, Ty const* ty, u32 facts) {
    return filter_type(me, ty, keep_with_facts, &facts);
}

static Ty const* union_of(Checker* me, Ty const* a, Ty const* b) {
    Ty const* types[2] = { a, b };
    return ty_union(me, types, 2, nullptr, (TyList){0});
}

// `T & {}` for what may still be nullish (a type parameter, `unknown` was dealt with before)
static Ty const* map_non_nullable(Checker* me, Ty const* ty, void const* arg) {
    (void)arg;
    if (!has_type_facts(me, ty, TF_EQ_UNDEFINED_OR_NULL)) return ty;
    // The lib's `NonNullable<T>` (`T & {}`) when there is one, which is how it prints (tsc's
    // getGlobalNonNullableTypeInstantiation)
    Symbol const* alias = ty_global_type_alias(me, c_sv("NonNullable"), 1);
    if (alias) return ty_alias_instance(me, alias, (TyList){ .data = &ty, .len = 1 });
    Ty const* types[2] = { ty, ty_empty_object_type(me) };
    return ty_intersection(me, types, 2, nullptr, (TyList){0});
}

typedef struct RemoveNullableArg {
    u32 target_facts;
    u32 other_facts; // Zero if the type includes the other nullable already
    Ty const* empty;
    Ty const* empty_and_other;
} RemoveNullableArg;

static Ty const* map_remove_nullable(Checker* me, Ty const* ty, void const* arg) {
    RemoveNullableArg const* remove = arg;
    if (!has_type_facts(me, ty, remove->target_facts)) return ty;
    bool could_be_other = remove->other_facts && has_type_facts(me, ty, remove->other_facts);
    Ty const* types[2] = { ty, could_be_other ? remove->empty_and_other : remove->empty };
    return ty_intersection(me, types, 2, nullptr, (TyList){0});
}

// tsc's removeNullableByIntersection: each member that could be the nullable is intersected with `{}`, or with
// `{} | other` when it could also be the other nullable and the type doesn't include that one already (`x !== null`
// of a T is `T & ({} | undefined)`)
static Ty const* remove_nullable_by_intersection(Checker* me, Ty const* ty, u32 target_facts, u32 other_facts, u32 other_includes, Ty const* other) {
    u32 facts = type_facts(me, ty, TF_EQ_UNDEFINED | TF_EQ_NULL | TF_IS_UNDEFINED | TF_IS_NULL);
    if (!(facts & target_facts)) return ty;
    Ty const* empty = ty_empty_object_type(me);
    Ty const* parts[2] = { empty, other };
    RemoveNullableArg arg = {
        .target_facts = target_facts, .other_facts = facts & other_includes ? 0 : other_facts, .empty = empty,
        .empty_and_other = ty_union(me, parts, 2, nullptr, (TyList){0}),
    };
    return map_type(me, ty, map_remove_nullable, &arg);
}

// tsc's getAdjustedTypeWithFacts(): `unknown` is `{} | null | undefined` while it is filtered
static Ty const* adjusted_type_with_facts(Checker* me, Ty const* ty, u32 facts) {
    bool strict = compiler_options()->strict_null_checks;
    bool was_unknown = strict && (ty->flags & TYF_UNKNOWN);
    if (was_unknown) {
        Ty const* parts[3] = { ty_unknown_empty_object_type(me), ty_intrinsic(me, TY_NULL), ty_intrinsic(me, TY_UNDEFINED) };
        Ty const* unknown_union = ty_union(me, parts, 3, nullptr, (TyList){0});
        Ty const* filtered = type_with_facts(me, unknown_union, facts);
        return filtered == unknown_union ? ty : filtered;
    }
    Ty const* reduced = type_with_facts(me, ty, facts);
    if (!strict) return reduced;
    switch (facts) {
        case TF_NE_UNDEFINED:
            return remove_nullable_by_intersection(me, reduced, TF_EQ_UNDEFINED, TF_EQ_NULL, TF_IS_NULL, ty_intrinsic(me, TY_NULL));
        case TF_NE_NULL:
            return remove_nullable_by_intersection(me, reduced, TF_EQ_NULL, TF_EQ_UNDEFINED, TF_IS_UNDEFINED, ty_intrinsic(me, TY_UNDEFINED));
        case TF_NE_UNDEFINED_OR_NULL:
        case TF_TRUTHY:
            return map_type(me, reduced, map_non_nullable, nullptr);
        default:
            return reduced;
    }
}

// What the operators in expr.c make of their operands
Ty const* ty_non_nullish_part(Checker* me, Ty const* ty) { return adjusted_type_with_facts(me, ty, TF_NE_UNDEFINED_OR_NULL); }
// tsc's getNonNullableTypeIfNeeded: a type that can't be null or undefined stays as it is (an unconstrained T too)
Ty const* ty_non_nullable_if_needed(Checker* me, Ty const* ty) {
    return has_type_facts(me, ty, TF_IS_UNDEFINED_OR_NULL) ? ty_non_nullable(me, ty) : ty;
}
Ty const* ty_remove_definitely_falsy(Checker* me, Ty const* ty) { return type_with_facts(me, ty, TF_TRUTHY); }
Ty const* ty_with_ne_undefined_facts(Checker* me, Ty const* ty) { return type_with_facts(me, ty, TF_NE_UNDEFINED); }
bool ty_can_be_truthy(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_TRUTHY); }
bool ty_can_be_falsy(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_FALSY); }
bool ty_can_be_nullish(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_EQ_UNDEFINED_OR_NULL); }
bool ty_can_be_undefined(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_IS_UNDEFINED); }
bool ty_can_equal_undefined(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_EQ_UNDEFINED); }
bool ty_can_be_null(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_IS_NULL); }

unsigned ty_undefined_or_null_facts(Checker* me, Ty const* ty) {
    return type_facts(me, ty, TF_IS_UNDEFINED_OR_NULL) & TF_IS_UNDEFINED_OR_NULL;
}

static Ty const* base_type_of_literal(Checker* me, Ty const* ty) { return ty_base_type_of_literal(me, ty); }

static bool is_unit_type(Ty const* ty) {
    if (ty->flags & TYF_ENUM) return false;
    return (ty->flags & (TYF_LITERAL | TYF_UNIQUE_ES_SYMBOL | TYF_UNDEFINED | TYF_NULL)) && ty->kind != TY_UNION;
}

// References

static Expr const* skip_reference_wrappers(Expr const* expr) {
    while (true) {
        if (expr->tag == EXPR_UNARY_OP && expr->unary_op->op == UNARY_OP_RIGHT_BANG) expr = &expr->unary_op->expr;
        else if (expr->tag == EXPR_AS_TYPE && expr->as_type->is_satisfies) expr = &expr->as_type->expr;
        else return expr;
    }
}

// tsc's getReferenceCandidate(): what a condition is about, `(x = e)` and `(a, x)` being about x
static Expr const* reference_candidate(Expr const* expr) {
    while (true) {
        expr = skip_reference_wrappers(expr);
        if (expr->tag != EXPR_BINARY_OP) return expr;
        switch (expr->binary_op->op) {
            case BINARY_OP_EQUAL:
            case BINARY_OP_OR_OR_EQUAL:
            case BINARY_OP_AND_AND_EQUAL:
            case BINARY_OP_EH_EH_EQUAL:
                expr = &expr->binary_op->left;
                break;
            case BINARY_OP_COMMA:
                expr = &expr->binary_op->right;
                break;
            default:
                return expr;
        }
    }
}

// `E.A`, `ns.key` (tsc's tryGetNameFromEntityNameExpression): a dotted name of an enum member or a const
static bool is_constant_entity_name(Checker* me, Expr const* key) {
    if (key->tag != EXPR_PROPERTY || key->property->form != PROPERTY_EXPR_DOT || key->property->optional_chain) return false;
    Expr const* root = &key->property->expr;
    while (root->tag == EXPR_PROPERTY && root->property->form == PROPERTY_EXPR_DOT) root = &root->property->expr;
    if (root->tag != EXPR_IDENTIFIER || root->identifier.keyword) return false;
    // (tsc resolves the name as an entity first: a namespace, module or enum, never a variable's property)
    Symbol const* target = root->identifier.target;
    if (!target || !(target->flags & (SYMBOL_VALUE_MODULE | SYMBOL_REGULAR_ENUM | SYMBOL_CONST_ENUM))) return false;
    Ty const* type = ty_flow_expr_type(me, key);
    if (type->kind == TY_LITERAL && (type->flags & TYF_ENUM_LITERAL)) return true;
    Atom const* name = checker_atom(me, span_sv(expr_static_property_name(key)));
    Prop const* prop = ty_get_prop(me, ty_flow_expr_type(me, &key->property->expr), name);
    return prop && prop->decl->symbol && prop->decl->symbol->is_const;
}

// An annotated const whose annotation names no property (`const k: never = Symbol()`): its initializer's type may
// (tsc's tryGetNameFromEntityNameExpression tries the annotation, then getTypeOfExpression(initializer))
static Atom const* annotated_const_initializer_name(Checker* me, Binding const* binding) {
    if (!binding || binding->tag != BINDING_IDENTIFIER || !binding->has_explicit_type || !binding->source ||
        binding->source->tag != LAZY_TYPE_INITIALIZED_BINDING
    ) {
        return nullptr;
    }
    InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
    if (!initialized->value || initialized->value->tag == EXPR_IMPLICIT_ANY) return nullptr;
    bool is_number = false;
    return ty_property_name_of_key(me, ty_flow_expr_type(me, initialized->value), &is_number);
}

// The name an access goes by: `a.b`, `a["b"]`, `a[0]`, `a[c]` for a `const c` of a literal type
static Atom const* access_name(Checker* me, PropertyExpr const* access) {
    if (property_private_class(access)) return ty_literal_name(me, ty_access_key_type(me, access));
    if (access->form == PROPERTY_EXPR_DOT) {
        Span name = property_name(access);
        // (A `#x` no class declares is named by its text here, see ty_access_key_type())
        if (name.size && name.buf[0] == '#') return checker_atom(me, span_sv(name));
        return ty_literal_name(me, ty_access_key_type(me, access));
    }
    Expr const* key = &access->index;
    if (key->tag == EXPR_LITERAL) {
        if (key->literal.tag != VALUE_STRING && key->literal.tag != VALUE_NUMBER) return nullptr;
    } else if (key->tag == EXPR_IDENTIFIER) {
        Symbol const* symbol = key->identifier.target;
        if (!symbol || !symbol->is_const || key->identifier.keyword) return nullptr;
        // (A for-in/of variable has no initializer to name it: tsc's tryGetNameFromEntityNameExpression)
        Binding const* binding = (symbol->flags & SYMBOL_VARIABLE) ? symbol_find_node(symbol, SYMBOL_DECL_VARIABLE) : nullptr;
        Binding const* root = binding;
        while (root && root->parent) root = root->parent;
        if (root && root->source && root->source->tag == LAZY_TYPE_FOR_EACH_LOOP_VAR) return nullptr;
        // Nor a binding element (`const { k } = o`): it has no annotation, and tsc's tryGetNameFromEntityNameExpression
        // leaves out its initializer, whose type needn't be its own and may be in flight (`const { name } = tag.value`
        // in a loop that assigns `depth[name] = ...` and `tag = tags.next()`: matching `tags.next` against `depth[name]`
        // would ask for name's type, through tag's, TS7022)
        if (binding && binding != root) return nullptr;
        // (tsc's tryGetNameFromEntityNameExpression only reads the initializer of a declaration before the use: the
        // type of one after it may depend on this very reference, `bar[id]++; const id = foo.bar`)
        Span declared = symbol->name_span;
        if (span_source(declared) == span_source(key->inner_span) && span_start_ptr(declared) > span_start_ptr(key->inner_span)) {
            return nullptr;
        }
        // An unannotated `const k = e` goes by e's type, not k's declared type (tsc's getTypeOfExpression(initializer)):
        // the reference may be narrowed while k's own initializer is being checked, `const idx = this.a.findIndex(...)`
        // in a loop that assigns `this.a[idx] = ...`, and asking for k's type there is a circularity tsc doesn't have
        if (binding == root && binding && binding->tag == BINDING_IDENTIFIER && !binding->has_explicit_type &&
            binding->source && binding->source->tag == LAZY_TYPE_INITIALIZED_BINDING
        ) {
            InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
            if (initialized->value && initialized->value->tag != EXPR_IMPLICIT_ANY) {
                bool is_number = false;
                return ty_property_name_of_key(me, ty_flow_expr_type(me, initialized->value), &is_number);
            }
        }
        bool is_number = false;
        Atom const* name = ty_property_name_of_key(me, ty_flow_expr_type(me, key), &is_number);
        return name ? name : annotated_const_initializer_name(me, binding);
    } else if (!is_constant_entity_name(me, key)) {
        return nullptr;
    }
    // (An enum member declared after the use names nothing: tsc's tryGetNameFromEntityNameExpression reads an enum
    // member's initializer only if isBlockScopedNameDeclaredBeforeUse, `o[E.A]` above `const enum E { A = "a" }`)
    if (key->tag == EXPR_PROPERTY && key->property->expr.tag == EXPR_IDENTIFIER && key->property->expr.identifier.target &&
        (key->property->expr.identifier.target->flags & SYMBOL_ENUM)
    ) {
        Span declared = key->property->expr.identifier.target->name_span;
        if (span_source(declared) == span_source(key->inner_span) && span_start_ptr(declared) > span_start_ptr(key->inner_span)) {
            return nullptr;
        }
    }
    bool is_number = false;
    Atom const* name = ty_property_name_of_key(me, ty_flow_expr_type(me, key), &is_number);
    if (name || key->tag != EXPR_PROPERTY) return name;
    // (`ns.key` of an annotated const whose annotation names nothing: its initializer, below)
    Atom const* member = checker_atom(me, span_sv(expr_static_property_name(key)));
    Prop const* prop = ty_get_prop(me, ty_flow_expr_type(me, &key->property->expr), member);
    Symbol const* symbol = prop && prop->decl->symbol && prop->decl->symbol->is_const ? prop->decl->symbol : nullptr;
    Binding const* binding = symbol && (symbol->flags & SYMBOL_VARIABLE) ? symbol_find_node(symbol, SYMBOL_DECL_VARIABLE) : nullptr;
    if (!binding || binding->parent) return nullptr;
    Span declared = symbol->name_span;
    if (span_source(declared) == span_source(key->inner_span) && span_start_ptr(declared) > span_start_ptr(key->inner_span)) {
        return nullptr;
    }
    return annotated_const_initializer_name(me, binding);
}

// `x[k]` where k has no literal type but always names the same thing: a const, or a parameter or `let` never assigned
// (tsc's isConstantVariable || isParameterOrMutableLocalVariable && !isSymbolAssigned). Null otherwise
static Symbol const* access_key_symbol(PropertyExpr const* access) {
    if (access->form != PROPERTY_EXPR_BRACKET) return nullptr;
    Expr const* key = &access->index;
    if (key->tag != EXPR_IDENTIFIER || key->identifier.keyword) return nullptr;
    Symbol const* symbol = key->identifier.target;
    // (Not an imported one, nor an exported one: tsc's getResolvedSymbol is the alias, or the local export symbol, which
    // aren't variables to isConstantVariable)
    if (!symbol || key->identifier.symbol != symbol || symbol->exported) return nullptr;
    if (symbol->is_const) return symbol;
    // (A `let` of a script's top level is a global, not a mutable local: tsc's isMutableLocalVariableDeclaration)
    bool mutable_local = symbol->is_local && (symbol->flags & SYMBOL_BLOCK_SCOPED_VARIABLE);
    return (symbol->is_parameter || mutable_local) && !symbol_assignments(symbol)->is_assigned ? symbol : nullptr;
}

static bool is_matching_reference(Checker* me, Expr const* source, Expr const* target) {
    source = skip_reference_wrappers(source);
    target = skip_reference_wrappers(target);
    if (target->tag == EXPR_BINARY_OP) {
        // `(x = e)` and `(a, x)` match what x matches
        BinaryOp op = target->binary_op->op;
        if (op == BINARY_OP_EQUAL) return is_matching_reference(me, source, &target->binary_op->left);
        if (op == BINARY_OP_COMMA) return is_matching_reference(me, source, &target->binary_op->right);
        return false;
    }
    // A reference like `(a, x).y` is about x.y
    if (source->tag == EXPR_BINARY_OP) {
        return source->binary_op->op == BINARY_OP_COMMA && is_matching_reference(me, &source->binary_op->right, target);
    }
    if (source->tag != target->tag) return false;
    switch (source->tag) {
        case EXPR_IDENTIFIER:
            if (source->identifier.keyword || target->identifier.keyword) {
                // (`import` is only ever a reference as the object of `import.meta`)
                return source->identifier.keyword == target->identifier.keyword;
            }
            return source->identifier.target && source->identifier.target == target->identifier.target;
        case EXPR_PROPERTY: {
            Atom const* source_name = access_name(me, source->property);
            if (source_name) {
                if (source_name != access_name(me, target->property)) return false;
            } else {
                Symbol const* key = access_key_symbol(source->property);
                if (!key || key != access_key_symbol(target->property)) return false;
            }
            return is_matching_reference(me, &source->property->expr, &target->property->expr);
        }
        default:
            return false;
    }
}

// is_matching_reference(source, target) with source the query's reference `level` property accesses down: the names
// of its dot accesses (no type is asked for them) are remembered in the query
[[gnu::noinline]] static bool query_matches_at(FlowQuery* q, Expr const* source, Expr const* target, u32 level) {
    Checker* me = q->me;
    source = skip_reference_wrappers(source);
    target = skip_reference_wrappers(target);
    if (target->tag == EXPR_BINARY_OP) {
        BinaryOp op = target->binary_op->op;
        if (op == BINARY_OP_EQUAL) return query_matches_at(q, source, &target->binary_op->left, level);
        if (op == BINARY_OP_COMMA) return query_matches_at(q, source, &target->binary_op->right, level);
        return false;
    }
    if (source->tag != EXPR_PROPERTY || target->tag != EXPR_PROPERTY) return is_matching_reference(me, source, target);
    Atom const* source_name;
    if (level < c_arraylen(q->ref_names) && source->property->form == PROPERTY_EXPR_DOT) {
        if (!(q->ref_names_set & (1u << level))) {
            q->ref_names[level] = access_name(me, source->property);
            q->ref_names_set |= (uint8_t)(1u << level);
        }
        source_name = q->ref_names[level];
    } else {
        source_name = access_name(me, source->property);
    }
    if (source_name) {
        if (source_name != access_name(me, target->property)) return false;
    } else {
        Symbol const* key = access_key_symbol(source->property);
        if (!key || key != access_key_symbol(target->property)) return false;
    }
    return query_matches_at(q, &source->property->expr, &target->property->expr, level + 1);
}

// is_matching_reference(q->reference, target): an identifier reference's side is known (FlowQuery.identifier)
static bool query_matches(FlowQuery* q, Expr const* target) {
    if (!q->identifier) return query_matches_at(q, q->reference, target, 0);
    while (true) {
        target = skip_reference_wrappers(target);
        if (target->tag != EXPR_BINARY_OP) break;
        // `(x = e)` and `(a, x)` match what x matches
        BinaryOp op = target->binary_op->op;
        if (op == BINARY_OP_EQUAL) target = &target->binary_op->left;
        else if (op == BINARY_OP_COMMA) target = &target->binary_op->right;
        else return false;
    }
    return target->tag == EXPR_IDENTIFIER && !target->identifier.keyword && q->symbol && target->identifier.target == q->symbol;
}

static bool symbol_matches_reference(Expr const* reference, Symbol const* symbol) {
    reference = skip_reference_wrappers(reference);
    return reference->tag == EXPR_IDENTIFIER && !reference->identifier.keyword && reference->identifier.target == symbol;
}

// `x.y.z` is about `x.y` and `x`
// (tsc's containsMatchingReference walks access expressions only: `x.a!.b` and `(x.a).b` stop at `x.a!` and `(x.a)`,
// so an assignment to x leaves their narrowing alone. Parentheses are a flag on the node here)
static bool contains_matching_reference(Checker* me, Expr const* source, Expr const* target) {
    source = skip_reference_wrappers(source);
    while (source->tag == EXPR_PROPERTY) {
        source = &source->property->expr;
        if (is_matching_reference(me, skip_reference_wrappers(source), target)) return true;
        if (expr_is_perenthisized(source)) return false;
    }
    return false;
}

static bool contains_symbol_reference(Expr const* source, Symbol const* symbol) {
    source = skip_reference_wrappers(source);
    while (source->tag == EXPR_PROPERTY) {
        source = &source->property->expr;
        if (symbol_matches_reference(skip_reference_wrappers(source), symbol)) return true;
        if (expr_is_perenthisized(source)) return false;
    }
    return false;
}

// `a?.b.c` contains `a`: if the chain isn't undefined, a isn't nullish
// (tsc's isOptionalChain of `source`. `(x?.a)` is a parenthesized expression, no chain either, where tsc hasn't skipped the
// parentheses (getReferenceCandidate, narrowType and a switch's expression do: `parens_skipped`), which tttt keeps as
// a flag on the node)
static bool chain_contains_reference(Checker* me, Expr const* source, Expr const* target, bool parens_skipped) {
    if (!parens_skipped && expr_is_perenthisized(source)) return false;
    while (expr_is_optional_chain(source)) {
        source = source->tag == EXPR_PROPERTY ? &source->property->expr :
            source->tag == EXPR_CALL ? &source->call->callable : &source->unary_op->expr;
        while (source->tag == EXPR_GENERIC_PARAMS) source = &source->generic_params->expr;
        if (is_matching_reference(me, source, target)) return true;
    }
    return false;
}

static bool optional_chain_contains_reference(Checker* me, Expr const* source, Expr const* target) {
    return chain_contains_reference(me, source, target, false);
}

// tsc's getReferenceCandidate: an assignment's target, a comma's right operand, parentheses skipped (a flag here), but
// not the `!` and `satisfies` reference_candidate() skips too
static Expr const* assignment_candidate(Expr const* expr) {
    while (expr->tag == EXPR_BINARY_OP) {
        BinaryOp op = expr->binary_op->op;
        if (op == BINARY_OP_EQUAL || op == BINARY_OP_OR_OR_EQUAL || op == BINARY_OP_AND_AND_EQUAL || op == BINARY_OP_EH_EH_EQUAL) {
            expr = &expr->binary_op->left;
        } else if (op == BINARY_OP_COMMA) {
            expr = &expr->binary_op->right;
        } else {
            break;
        }
    }
    return expr;
}

// The key of a reference for the loop caches: small ids interned from (symbol | this, property names, declared type,
// initial type). 0: the reference has no key (a call or a computed key in it) and isn't narrowed through loops
static u32 reference_key_words(Checker* me, Expr const* expr, u64* words, u32 cap) {
    expr = skip_reference_wrappers(expr);
    switch (expr->tag) {
        case EXPR_IDENTIFIER:
            if (cap < 1) return 0;
            if (expr->identifier.keyword == KEYWORD_IDENTIFIER_THIS) words[0] = 1;
            else if (expr->identifier.keyword == KEYWORD_IDENTIFIER_SUPER) words[0] = 2;
            else if (expr->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT) words[0] = 3; // `import.meta`
            else if (!expr->identifier.keyword && expr->identifier.target) words[0] = (u64)(uintptr_t)expr->identifier.target;
            else return 0;
            return 1;
        case EXPR_PROPERTY: {
            u32 count = reference_key_words(me, &expr->property->expr, words, cap);
            Atom const* name = access_name(me, expr->property);
            Symbol const* key = name ? nullptr : access_key_symbol(expr->property);
            if (!count || !(name || key) || count == cap) return 0;
            words[count] = name ? (u64)(uintptr_t)name : (u64)(uintptr_t)key | 1;
            return count + 1;
        }
        default:
            return 0;
    }
}

static u32 query_key(FlowQuery* q) {
    if (q->key_set) return q->key;
    Checker* me = q->me;
    q->key_set = true;
    u64 words[24] = { KEY_FLOW_REFERENCE, q->declared->id, q->initial->id };
    u32 count;
    if (q->pattern || q->parameters_of) {
        // A destructuring pattern's or a function's parameters' query (a placeholder reference): keyed by what it
        // narrows, so loops narrow it too
        words[3] = q->pattern ? 1 : 2;
        words[4] = q->pattern ? (u64)(uintptr_t)q->pattern : (u64)(uintptr_t)q->parameters_of;
        count = 2;
    } else {
        count = reference_key_words(me, q->reference, words + 3, c_arraylen(words) - 3);
    }
    if (!count) return 0;
    void const** slot = intern_slot(me, &my links, words, count + 3);
    if (!*slot) *slot = (void const*)(uintptr_t)++my flow_key_count;
    q->key = (u32)(uintptr_t)*slot;
    return q->key;
}

// Expression types for the engine. A finished memo is as good as it gets. Anything else may be in the middle of
// being evaluated further up the stack (`x = f(x)` in a loop asks for x, which asks the loop, which asks for the
// type of `f(x)`), and what it evaluates to now may depend on incomplete loop types: it is evaluated without touching
// the memos, all the way down. Termination comes from the loop machinery, like in tsc, which memoises no expression types
static bool in_optional_chain(Expr const* expr) {
    for (;;) {
        if (expr->tag == EXPR_PROPERTY) {
            if (expr->property->optional_chain) return true;
            expr = &expr->property->expr;
        } else if (expr->tag == EXPR_CALL) {
            if (expr->call->optional_chain) return true;
            expr = &expr->call->callable;
        } else {
            return false;
        }
    }
}

// A call of a function with a single non-generic signature is what the signature returns, arguments unseen: that's
// what lets `x = f(y)` in a loop not depend on `y`'s type (tsc's getQuickTypeOfExpression). Null: no shortcut. The
// flow engine asks for the callee through its own cache (`in_flow`), which doesn't remember loop-incomplete types
bool ty_quick_call_eligible(Expr const* expr) {
    if (expr->tag != EXPR_CALL) return false;
    CallExpr const* call = expr->call;
    if (call->is_extends || call->callable.tag == EXPR_GENERIC_PARAMS) return false;
    // A chain is one only as `f?.(x)` of a callee that isn't nullish (tsc's getReturnTypeOfSingleNonGenericSignatureOfCallChain
    // asks the callee's own type, of which a nullish part has no signature; then nothing short-circuits):
    // `document.elementFromPoint?.(x, y)` (openscience's create-auto-scroll.tsx, a loop's TS7022 otherwise)
    if (in_optional_chain(expr) && (!call->optional_chain || in_optional_chain(&call->callable))) return false;
    // (tsc's isSymbolOrSymbolForCall and isRequireCall: `Symbol()`, `Symbol.for()` and `require()`)
    Expr const* name_expr = &call->callable;
    if (name_expr->tag == EXPR_PROPERTY && span_equals(property_key_inner_span(name_expr->property), "for")) {
        name_expr = &name_expr->property->expr;
    }
    if (name_expr->tag == EXPR_IDENTIFIER && !call->uses_new) {
        csview name = span_sv(name_expr->inner_span);
        if (csview_equals(name, "Symbol") || (name_expr == &call->callable && csview_equals(name, "require"))) {
            return false;
        }
    }
    return true;
}

Ty const* ty_quick_call_return(Checker* me, CallExpr const* call, Ty const* callee) {
    if (!(callee->flags & TYF_OBJECT)) return nullptr;
    // (tsgo's getSingleSignature with allowMembers: the callee may have properties and index signatures, a class its
    // `prototype`. So `new A()` of an abstract class is A here, though checking it is an error)
    Members const* members = ty_members(me, callee);
    u32 count = call->uses_new ? members->construct_count : members->call_count;
    u32 other = call->uses_new ? members->call_count : members->construct_count;
    if (count != 1 || other) return nullptr;
    Signature const* sig = call->uses_new ? members->construct[0] : members->call[0];
    // A TypeScript function declared with a return type and no type parameters of its own: that type, its parameters
    // untyped (tsc's getSignatureFromDeclaration types none, getReturnTypeOfSignature reads the annotation). Typing them
    // can reach back to what asks: `const active = activate(plan)` in a callback of FeaturePlan's initializer, with
    // `activate(plan: Pick<z.infer<typeof FeaturePlan>, ...>): Set<string>` (synergy's TS7022)
    FunctionImpl const* declared = sig->function;
    if (declared && !sig_target(me, sig) && !declared->is_type && !declared->is_method && declared->ret_explicit && !call->uses_new &&
        !vec_generic_item_size(&declared->generic.items) && !function_impl_rare(declared)->jsdoc_signature_type &&
        span_source(declared->full_span) && !span_source(declared->full_span)->is_js
    ) {
        return ty_signature_declared_return_type(me, sig);
    }
    SigResolved const* sig_rs = signature_resolve(me, sig);
    if (sig_rs->type_params.len) return nullptr;
    return signature_return_type(me, sig);
}

Ty const* ty_quick_expr_type(Checker* me, Expr const* expr, bool in_flow) {
    if (expr->tag == EXPR_UNARY_OP && expr->unary_op->op == UNARY_OP_AWAIT) {
        Ty const* quick = ty_quick_expr_type(me, &expr->unary_op->expr, in_flow);
        return quick ? ty_awaited(me, quick) : nullptr;
    }
    // (A type assertion is its type, its operand unchecked)
    if (expr->tag == EXPR_AS_TYPE && !expr->as_type->is_satisfies && !as_type_is_const(expr->as_type)) {
        return ty_from_node(me, &expr->as_type->type, (LowerCtx){ .generic = true });
    }
    if (!ty_quick_call_eligible(expr)) return nullptr;
    CallExpr const* call = expr->call;
    if (!call->uses_new) ty_assign_iife_parameter_types(me, call);
    Ty const* callee = in_flow ? ty_flow_expr_type(me, &call->callable) : ty_expr_type(me, &call->callable);
    return ty_quick_call_return(me, call, callee);
}

static Ty const* flow_expr_type_in(Checker* me, Expr const* expr, BinaryOpExpr const* assignment);

Ty const* ty_flow_expr_type(Checker* me, Expr const* expr) {
    return flow_expr_type_in(me, expr, nullptr);
}

Ty const* ty_flow_assigned_value_type(Checker* me, BinaryOpExpr const* assignment) {
    return flow_expr_type_in(me, &assignment->right, assignment);
}

// assignment: expr is its value, checked with the contextual type its target gives
static Ty const* flow_expr_type_in(Checker* me, Expr const* expr, BinaryOpExpr const* assignment) {
    // (The quick type of an immediately invoked function is its return type, whose return expressions the
    // assignment's target contextually types)
    if (assignment && expr->tag == EXPR_CALL && !expr->call->uses_new && expr->call->callable.tag == EXPR_FUNCTION &&
        expr->call->callable.function->is_iife
    ) {
        ty_seed_iife_return_context(me, expr->call, ty_assigned_value_context(me, assignment));
    }
    Ty const* quick = ty_quick_expr_type(me, expr, true);
    if (quick) return quick;
    LazyType* lazy = (LazyType*)&expr->lazy_type;
    if (lazy->tag == LAZY_TYPE_EXPR && lazy->slot) {
        LazyMemo* memo = lazy_type_state_peek(lazy);
        if (memo && memo->done) return lazy_memo_evaluated(memo);
    }
    // With no loop in progress what comes out is complete, and remembered like in tsc's flowTypeCache (not in the
    // memo: nobody gave this evaluation a contextual type). Without it a chain of `x = x + x` doubles at every link
    // While a loop is in progress the cache is only good for the walk of one back edge (flow_expr_generation)
    bool complete = my flow_loop_count == my flow_loop_start;
    // (The low bit: with the assignment's context, Expr being aligned)
    u64 key[3] = { KEY_FLOW_EXPR, (u64)(uintptr_t)expr | (assignment != nullptr), complete ? 0 : my flow_expr_generation };
    Ty const* cached = intern_get(&my links, key, 3);
    if (cached) return cached;
    TAINT_FLOW();
    my flow_uncached++;
    Ty const* contextual = assignment ? ty_assigned_value_context(me, assignment) : nullptr;
    Ty const* ret = contextual ? ty_check_expr(me, expr, contextual) : ty_compute_expr_without_context(me, expr);
    TAINT_FLOW();
    my flow_uncached--;
    *intern_slot(me, &my links, key, 3) = ret;
    return ret;
}

// tsc's getContextFreeTypeOfExpression, what a pushed value adds to an evolving array: checked with the contextual
// type any, context-sensitive functions left out. A generic call infers from that any through its return type: babel's
// `children.push(this.parseLiteral(value, "JSXText"))` (`parseLiteral<T extends Node>(v: any, t: T["type"]): T`)
// pushes any, not Node
static Ty const* flow_context_free_type(Checker* me, Expr const* expr) {
    Ty const* quick = ty_quick_expr_type(me, expr, true);
    if (quick) return quick;
    bool complete = my flow_loop_count == my flow_loop_start;
    u64 key[3] = { KEY_FLOW_CONTEXT_FREE_EXPR, (u64)(uintptr_t)expr, complete ? 0 : my flow_expr_generation };
    Ty const* cached = intern_get(&my links, key, 3);
    if (cached) return cached;
    TAINT_FLOW();
    my flow_uncached++;
    Ty const* ret = ty_check_expr_skipping_sensitive(me, expr, ty_intrinsic(me, TY_ANY), false);
    TAINT_FLOW();
    my flow_uncached--;
    *intern_slot(me, &my links, key, 3) = ret;
    return ret;
}

// Assignments

static bool assigned_is_assignable(Checker* me, Ty const* member, void const* arg) {
    Ty const* assigned = arg;
    if (assigned->kind == TY_UNION) {
        for (u32 i = 0; i < assigned->composite.types.len; i++) {
            if (ty_is_related(me, assigned->composite.types.data[i], member, TY_RELATION_ASSIGNABLE)) return true;
        }
        return false;
    }
    return ty_is_related(me, assigned, member, TY_RELATION_ASSIGNABLE);
}

static Ty const* map_fresh(Checker* me, Ty const* ty, void const* arg) {
    (void)arg;
    return ty_fresh_literal(me, ty);
}

// What is left of a declared union type after `assigned` was assigned to it. Cached like tsc's getAssignmentReducedType
// (every assignment to a variable of a big union type is related to each of its members)
static Ty const* assignment_reduced_type(Checker* me, Ty const* declared, Ty const* assigned) {
    if (declared == assigned || (assigned->flags & TYF_NEVER)) return assigned;
    u64 key[3] = { KEY_ASSIGNMENT_REDUCED, declared->id, assigned->id };
    Ty const* cached = intern_get(&my links, key, 3);
    if (cached) return cached;
    Ty const* reduced = filter_type(me, declared, assigned_is_assignable, assigned);
    // `flag = true` leaves a fresh `true`, which widens again: `let copy = flag` is boolean
    if (ty_is_fresh(assigned) && (assigned->flags & TYF_BOOLEAN_LITERAL)) reduced = map_type(me, reduced, map_fresh, nullptr);
    // (An erroneous assignment doesn't narrow)
    Ty const* ret = ty_is_related(me, assigned, reduced, TY_RELATION_ASSIGNABLE) ? reduced : declared;
    *intern_slot(me, &my links, key, 3) = ret;
    return ret;
}

static Ty const* for_each_element_type(Checker* me, ForEachLoopStatement const* for_each) {
    // (A for-in variable is assigned a string, whatever its declared `Extract<keyof X, string>`: tsc's
    // getInitialTypeOfVariableDeclaration and getAssignedType for a ForInStatement)
    if (for_each->keyword == FOR_EACH_LOOP_IN) return ty_intrinsic(me, TY_STRING);
    return lazy_type_get((LazyType*)&for_each->var_type);
}

// The type the value at the end of a destructuring path has, null if it can't be told
static Ty const* pattern_path_type(Checker* me, FlowAssign const* assign, FlowPatternPath const* path) {
    if (path->plain_default) return ty_flow_expr_type(me, path->default_value);
    Ty const* object;
    if (path->parent) {
        object = pattern_path_type(me, assign, path->parent);
    } else if (assign->for_each) {
        object = for_each_element_type(me, assign->for_each);
    } else {
        object = ty_flow_expr_type(me, &assign->assignment->binary_op->right);
    }
    if (!object || (object->flags & TYF_ANY)) return object;
    Ty const* ret = nullptr;
    switch (path->kind) {
        case FLOW_PATH_ELEMENT: {
            // (never iterates to nothing: tsc's getTypeOfDestructuredArrayElement falls back to the error type)
            if (object->flags & TYF_NEVER) return ty_intrinsic(me, TY_ERROR);
            if (!ty_extends_array(me, object)) {
                // (Another iterable: what it iterates to, tsc's includeUndefinedInIndexSignature(checkIteratedTypeOrElementType))
                Ty const* element = ty_iterated_type_unreported(me, ITERATION_DESTRUCTURING, object);
                ret = element ? ty_possibly_out_of_bounds(me, element) : ty_intrinsic(me, TY_ERROR);
                break;
            }
            Value index = value_number(path->index);
            // (noUncheckedIndexedAccess's undefined: tsc's getTypeOfDestructuredArrayElement)
            ret = ty_access_reading(me, object, ty_literal(me, &index));
            if (!ret) ret = ty_access_reading(me, object, ty_intrinsic(me, TY_NUMBER));
            break;
        }
        case FLOW_PATH_PROPERTY: {
            ObjectExprItem const* item = path->item;
            // (undefined and null have no properties: the union has none, tsc's getTypeOfDestructuredProperty gives
            // the error type, which leaves the declared type)
            if (compiler_options()->strict_null_checks && object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) {
                for (u32 i = 0; i < object->composite.types.len; i++) {
                    if (object->composite.types.data[i]->flags & TYF_NULLABLE) return nullptr;
                }
            }
            if (item->computed_key) {
                // A computed name only if it has a literal type (tsc's getTypeOfDestructuredProperty)
                Ty const* key = ty_regular(me, ty_flow_expr_type(me, item->computed_key));
                bool is_number = false;
                if (!ty_property_name_of_key(me, key, &is_number)) return nullptr;
                ret = ty_access_reading(me, object, key);
                break;
            }
            ret = ty_access_reading(me, object, ty_string_literal(me, cstr_sv(&item->name_string)));
            break;
        }
        case FLOW_PATH_REST: {
            // `[...a] = e`: an array of what e iterates to (tsc's getTypeOfDestructuredSpreadExpression); `{ ...a } = e`
            // is tsc's errorType (getAssignedType has no case for a spread assignment): it leaves a declared type as it
            // is, and an auto-typed `let a` is any from there on, not implicitly any where unassigned (TS7034)
            if (path->pattern->tag != EXPR_ARRAY) return ty_intrinsic(me, TY_ERROR);
            Ty const* element = ty_iterated_type_unreported(me, ITERATION_DESTRUCTURING, object);
            return ty_array(me, element ? element : ty_intrinsic(me, TY_ERROR), false);
        }
    }
    if (ret && path->default_value) {
        Ty const* defined = type_with_facts(me, ret, TF_NE_UNDEFINED);
        ret = union_of(me, defined, ty_flow_expr_type(me, path->default_value));
    }
    return ret;
}

// The initializer of a declaration's root binding, null if it has none
static InitializedBinding const* root_initializer(Binding const* root) {
    if (root->parent || !root->source || root->source->tag != LAZY_TYPE_INITIALIZED_BINDING) return nullptr;
    InitializedBinding const* initialized = c_container_of(root->source, InitializedBinding, lazy_type);
    return initialized_binding_value(initialized)->tag == EXPR_IMPLICIT_ANY ? nullptr : initialized;
}

// What a declaration assigns to a binding (tsc's getInitialType): the root's initializer, and an element of an
// annotated pattern is the element of that, not of the annotation (`let [x]: [string | number] = [1]` is number); an
// element with a default has the default's type as it is, not widened (`for (var { y = true } of a)` assigns true)
static Ty const* initial_binding_type(FlowQuery* q, Binding const* binding) {
    Checker* me = q->me;
    // (A for-in variable is a string to begin with, see for_each_element_type())
    if (!binding->parent && binding->tag == BINDING_IDENTIFIER && binding->source && binding->source->tag == LAZY_TYPE_FOR_EACH_LOOP_VAR &&
        c_container_of(binding->source, ForEachLoopStatement, var_type)->keyword == FOR_EACH_LOOP_IN
    ) {
        return ty_intrinsic(me, TY_STRING);
    }
    if (!binding->parent && binding->source && binding->source->tag == LAZY_TYPE_FOR_EACH_LOOP_VAR &&
        binding->tag != BINDING_IDENTIFIER
    ) {
        return for_each_loop_statement_pattern_source_type(c_container_of(binding->source, ForEachLoopStatement, var_type));
    }
    if (!binding->parent) {
        InitializedBinding const* initialized = root_initializer(binding);
        if (!initialized) return ty_binding_assigned_type(me, binding);
        if (binding->has_explicit_type) {
            // Typed by the annotation, like in the walk (see FLOW_ASSIGN_EXPR below). Also when the walk hasn't checked
            // the declaration yet (tsc's getTypeOfInitializer -> checkExpression finds the contextual type up the
            // tree): a call's effects resolved from a function's end reachability, before its statements are checked,
            // may read the variable, and `const image: U = { type: E.A, ... }` without its context widens `E.A` to E,
            // which no member of U takes (vscode stateToProgressAdapter.test.ts with node's `assert.deepStrictEqual`)
            // (Its quick type first, tsc's getTypeOfExpression: a call of one non-generic signature is what that
            // returns, its arguments unchecked, `const x: A | B = pick(later())` with later() reading x)
            Ty const* quick = ty_quick_expr_type(me, initialized_binding_value(initialized), true);
            if (quick) return quick;
            Ty const* annotated = ty_from_node(me, binding_type(binding), (LowerCtx){ .generic = true });
            return ty_check_expr(me, initialized_binding_value(initialized), annotated);
        }
        return ty_flow_expr_type(me, initialized_binding_value(initialized));
    }
    Binding const* parent = binding->parent;
    Binding const* root = parent;
    while (root->parent) root = root->parent;
    bool for_each = root->source && root->source->tag == LAZY_TYPE_FOR_EACH_LOOP_VAR;
    bool some_default = false;
    for (Binding const* i = binding; i->parent; i = i->parent) some_default |= i->default_value != nullptr;
    if (binding->rest || (!(root->has_explicit_type && root_initializer(root)) && !(some_default && (for_each || root_initializer(root))))) {
        return ty_binding_assigned_type(me, binding);
    }
    Ty const* object = initial_binding_type(q, parent);
    Ty const* key;
    if (parent->tag == BINDING_ARRAY) {
        Value index = value_number(binding->parent_index);
        key = ty_literal(me, &index);
    } else {
        ObjectBindingItem const* item = binding->parent_item;
        key = item->private_name ? ty_intrinsic(me, TY_NEVER) :
            item->computed_key ? ty_binding_computed_key_type(me, item) : ty_literal(me, &item->source);
    }
    Ty const* ret = nullptr;
    // Past the end of a tuple without a rest element: undefined (tsc's getTupleElementTypeOutOfStartCount)
    TupleShape const* shape = parent->tag == BINDING_ARRAY ? ty_tuple_shape(object) : nullptr;
    if (shape && binding->parent_index >= shape->arity) {
        bool has_rest = false;
        for (u32 i = 0; i < shape->arity; i++) has_rest |= (shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
        if (!has_rest) ret = ty_intrinsic(me, TY_UNDEFINED);
    }
    // (A union with undefined or null has neither the property nor an iterated element: tsc's
    // getTypeOfDestructuredProperty and getTypeOfDestructuredArrayElement give the error type, which leaves the declared
    // type: `let [, a]: string[] | number[] = s.match(r)` reads a as string | number)
    if (!ret && compiler_options()->strict_null_checks && object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) {
        for (u32 i = 0; i < object->composite.types.len; i++) {
            if (object->composite.types.data[i]->flags & TYF_NULLABLE) return ty_binding_assigned_type(me, binding);
        }
    }
    if (!ret) ret = object->flags & TYF_ANY ? nullptr : ty_access_reading(me, object, key);
    if (!ret) return ty_binding_assigned_type(me, binding);
    // (tsc's getTypeWithDefault)
    if (binding->default_value) {
        Ty const* defined = type_with_facts(me, ret, TF_NE_UNDEFINED);
        ret = union_of(me, defined, ty_flow_expr_type(me, binding->default_value));
    }
    return ret;
}

static Ty const* initial_or_assigned_type_raw(FlowQuery* q, FlowAssign const* assign);

// tsc's getInitialOrAssignedType: narrowable like the reference (getNarrowableTypeForReference), so `let cur = row` with
// `row: T` of a union constraint, read as that constraint where the context isn't generic, is assigned the constraint,
// which is the declared type then: nothing to reduce
static Ty const* initial_or_assigned_type(FlowQuery* q, FlowAssign const* assign) {
    // (tsc's getTypeOfExpression of the value: CheckModeTypeOnly, see Checker.type_only_root)
    Checker* me = q->me;
    Expr const* outer_root = my type_only_root;
    if (assign->kind == FLOW_ASSIGN_EXPR) my type_only_root = &assign->assignment->binary_op->right;
    else if (assign->kind == FLOW_ASSIGN_DECLARATION && root_initializer(assign->binding)) {
        my type_only_root = initialized_binding_value(root_initializer(assign->binding));
    }
    Ty const* ret = initial_or_assigned_type_raw(q, assign);
    my type_only_root = outer_root;
    return q->substitutes_constraints ? ty_substitute_constraints(q->me, ret) : ret;
}

// tsc's getQuickTypeOfExpression of an assigned call checks its callee not to be nullish (checkNonNullExpression), and
// reports what it finds: `x = f()` that a read of `x` narrows by is TS18048 'f' is possibly 'undefined' besides the
// call's own TS2722. Once per assignment, into the checker's diagnostics (a variable declaration's is
// initialized_binding_typecheck()'s)
static void check_assigned_callee(Checker* me, Expr const* assignment, Expr const* value) {
    if (!my errs || my errs->phase != ERROR_PHASE_TYPECHECK || !compiler_options()->strict_null_checks) return;
    if (value->tag == EXPR_UNARY_OP && value->unary_op->op == UNARY_OP_AWAIT) value = &value->unary_op->expr;
    if (value->tag != EXPR_CALL || value->call->uses_new || value->call->is_extends || expr_is_optional_chain(value) ||
        expr_is_require_call(value)
    ) {
        return;
    }
    Expr const* callee = &value->call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER) return;
    void const** slot = intern_slot(me, &my links, (u64[]){ KEY_QUICK_CALLEE_CHECKED, (u64)(uintptr_t)assignment }, 2);
    if (*slot) return;
    *slot = assignment;
    ty_check_non_null_type(me, ty_flow_expr_type(me, callee), callee, my errs);
}

bool ty_flow_partial_reports_at(Checker* me, Span span) {
    return my checking_file && my errs && my errs->phase == ERROR_PHASE_TYPECHECK && span_source(span) == my checking_file;
}

void ty_flow_partial_no_property_mark(Checker* me, PropertyExpr const* access) {
    *intern_slot(me, &my links, (u64[]){ KEY_FLOW_PARTIAL_NO_PROPERTY, (u64)(uintptr_t)access }, 2) = access;
}

bool ty_flow_partial_no_property(Checker* me, PropertyExpr const* access) {
    return intern_get(&my links, (u64[]){ KEY_FLOW_PARTIAL_NO_PROPERTY, (u64)(uintptr_t)access }, 2) != nullptr;
}

static Ty const* initial_or_assigned_type_raw(FlowQuery* q, FlowAssign const* assign) {
    Checker* me = q->me;
    switch (assign->kind) {
        case FLOW_ASSIGN_DECLARATION:
            return initial_binding_type(q, assign->binding);
        case FLOW_ASSIGN_EXPR: {
            // A function expression is typed by its target, like the walk over the statement does (tsc finds the
            // contextual type up the tree, so asking bottom-up gets the same). Anything else goes without context
            BinaryOpExpr const* assignment = assign->assignment->binary_op;
            if (assignment->op == BINARY_OP_EQUAL && ty_expr_is_context_sensitive(&assignment->right)) {
                return ty_expr_type(me, assign->assignment);
            }
            // The rest are checked with the context their target gives them too, like in the walk, unless the target
            // is auto-typed (its type is what's being worked out: it gives no context). A logical assignment's too
            // (tsc's getContextualTypeForBinaryOperand): `ctx.aug ??= new Set()` is a Set<string> even when a flow
            // query gets there before the walk has checked it (a caller inferring the function's return type)
            bool assigns = assignment->op == BINARY_OP_EQUAL || assignment->op == BINARY_OP_OR_OR_EQUAL ||
                assignment->op == BINARY_OP_AND_AND_EQUAL || assignment->op == BINARY_OP_EH_EH_EQUAL;
            check_assigned_callee(me, assign->assignment, &assignment->right);
            if (assigns && !is_auto(me, q->declared)) return ty_flow_assigned_value_type(me, assignment);
            return ty_flow_expr_type(me, &assignment->right);
        }
        case FLOW_ASSIGN_FOR_EACH:
            return for_each_element_type(me, assign->for_each);
        case FLOW_ASSIGN_PATTERN: {
            Ty const* ret = pattern_path_type(me, assign, assign->path);
            return ret ? ret : q->declared;
        }
        case FLOW_ASSIGN_UPDATE:
            return ty_intrinsic(me, TY_NUMBER);
        case FLOW_ASSIGN_DELETE:
            return ty_intrinsic(me, TY_UNDEFINED);
    }
    impossible;
}

static bool assign_matches(FlowQuery* q, FlowAssign const* assign) {
    if (assign->kind == FLOW_ASSIGN_DECLARATION) {
        // (symbol_matches_reference(), worked out once per query)
        return assign->binding->tag == BINDING_IDENTIFIER && q->identifier && q->symbol == assign->binding->symbol;
    }
    return assign->target && query_matches(q, assign->target);
}

static bool assign_contains(FlowQuery* q, FlowAssign const* assign) {
    if (assign->kind == FLOW_ASSIGN_DECLARATION) {
        return assign->binding->tag == BINDING_IDENTIFIER && contains_symbol_reference(q->reference, assign->binding->symbol);
    }
    return assign->target && contains_matching_reference(q->me, q->reference, assign->target);
}

// A JavaScript or const variable declared with a function expression, which assignments to its properties extend
static bool is_expando_function_declaration(Binding const* binding) {
    if (binding->tag != BINDING_IDENTIFIER || binding->parent || !binding->source || binding->source->tag != LAZY_TYPE_INITIALIZED_BINDING) {
        return false;
    }
    Symbol const* symbol = binding->symbol;
    if (!symbol) return false;
    bool js = span_source(binding->content_span) && span_source(binding->content_span)->is_js;
    if (!js && !symbol->is_const) return false;
    InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
    return initialized_binding_value(initialized)->tag == EXPR_FUNCTION && !expr_is_perenthisized(initialized_binding_value(initialized));
}

static bool assign_is_compound(FlowAssign const* assign) {
    if (assign->kind == FLOW_ASSIGN_UPDATE) return true;
    if (assign->kind != FLOW_ASSIGN_EXPR) return false;
    switch (assign->assignment->binary_op->op) {
        case BINARY_OP_EQUAL:
        case BINARY_OP_OR_OR_EQUAL:
        case BINARY_OP_AND_AND_EQUAL:
        case BINARY_OP_EH_EH_EQUAL:
            return false;
        default:
            return true;
    }
}

// tsc's isEmptyArrayAssignment: `let x = []`, `x = []`
static bool is_empty_array_assignment(FlowAssign const* assign) {
    Expr const* right = nullptr;
    if (assign->kind == FLOW_ASSIGN_EXPR) {
        right = &assign->assignment->binary_op->right;
    } else if (assign->kind == FLOW_ASSIGN_DECLARATION && assign->binding && assign->binding->tag == BINDING_IDENTIFIER &&
        assign->binding->source && assign->binding->source->tag == LAZY_TYPE_INITIALIZED_BINDING
    ) {
        right = initialized_binding_value(c_container_of(assign->binding->source, InitializedBinding, lazy_type));
    }
    return right && right->tag == EXPR_ARRAY && !vec_expr_size(&right->array);
}

// False: the assignment has nothing to do with the reference
static bool type_at_flow_assignment(FlowQuery* q, FlowNode const* flow, FlowType* out) {
    Checker* me = q->me;
    FlowAssign const* assign = flow->node;
    if (assign_matches(q, assign)) {
        if (ty_flow_reachability(me, flow) == FLOW_REACH_NO) {
            *out = (FlowType){ ty_intrinsic(me, TY_UNREACHABLE_NEVER), false };
            return true;
        }
        if (assign_is_compound(assign)) {
            FlowType before = type_at_flow_node(q, flow->antecedent);
            *out = new_flow_type(q, base_type_of_literal(me, before.type), before.incomplete);
            return true;
        }
        Ty const* declared = q->declared;
        if (is_auto(me, declared)) {
            // `let x;` and `let x = []` are what was assigned last, an evolving array if that was `[]`
            if (is_empty_array_assignment(assign)) {
                *out = (FlowType){ evolving_array(me, ty_intrinsic(me, TY_NEVER)), false };
                return true;
            }
            Ty const* assigned = ty_widened_literal(me, initial_or_assigned_type(q, assign));
            if (declared != ty_intrinsic(me, TY_AUTO) && !ty_is_related(me, assigned, declared, TY_RELATION_ASSIGNABLE)) {
                assigned = ty_array(me, ty_intrinsic(me, TY_ANY), false);
            }
            *out = (FlowType){ assigned, false };
            return true;
        }
        // (`x ||= e` is a definite assignment, not a compound one: tsc's getAssignmentTargetKind)
        if (assign->kind == FLOW_ASSIGN_EXPR && assign->assignment->binary_op->op == BINARY_OP_EQUAL) {
            // `x = x + 1` (tsc's isInCompoundLikeAssignment)
            Expr const* left = &assign->assignment->binary_op->left;
            bool compound_like = (left->tag == EXPR_IDENTIFIER && left->identifier.compound_like_target) ||
                (left->tag == EXPR_PROPERTY && left->property->compound_like_target);
            if (compound_like) declared = base_type_of_literal(me, declared);
        }
        if (declared->kind == TY_UNION) {
            // Only a union can be narrowed by what is assigned, so only then is the assigned type asked for
            *out = (FlowType){ assignment_reduced_type(me, declared, initial_or_assigned_type(q, assign)), false };
        } else {
            *out = (FlowType){ declared, false };
        }
        return true;
    }
    if (assign_contains(q, assign)) {
        // `f.x` of a variable initialized with a function expression may be an expando of it: back to before the
        // function (tsc's getTypeAtFlowAssignment with getDeclaredExpandoInitializer)
        if (assign->kind == FLOW_ASSIGN_DECLARATION && is_expando_function_declaration(assign->binding)) {
            *out = type_at_flow_node(q, flow->antecedent);
            return true;
        }
        if (ty_flow_reachability(me, flow) == FLOW_REACH_NO) {
            *out = (FlowType){ ty_intrinsic(me, TY_UNREACHABLE_NEVER), false };
            return true;
        }
        *out = (FlowType){ q->declared, false };
        return true;
    }
    // `for (const k in ref)` acts as a non-null on ref (tsc's getTypeAtFlowAssignment: getNonNullableTypeIfNeeded)
    if (assign->kind == FLOW_ASSIGN_DECLARATION && assign->for_each) {
        Expr const* object = &assign->for_each->iterable;
        if (query_matches(q, object) || optional_chain_contains_reference(me, object, q->reference)) {
            FlowType before = type_at_flow_node(q, flow->antecedent);
            Ty const* type = final_array_type(me, before.type);
            type = ty_non_nullable_if_needed(me, type);
            *out = new_flow_type(q, type, before.incomplete);
            return true;
        }
    }
    return false;
}

// Calls: type predicates, assertions, functions that never return

static bool symbol_has_explicit_type(Symbol const* symbol) {
    if (symbol->flags & (SYMBOL_FUNCTION | SYMBOL_CLASS | SYMBOL_ENUM | SYMBOL_VALUE_MODULE)) return true;
    // An expando assigned a function with a return type annotation (tsc's isExpandoPropertyFunctionWithReturnTypeAnnotation)
    if ((symbol->flags & SYMBOL_PROPERTY) && symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_ASSIGNMENT) {
        Expr const* value = symbol->decls[0].node;
        return value && value->tag == EXPR_FUNCTION && value->function->ret_explicit;
    }
    if (!(symbol->flags & SYMBOL_VARIABLE)) return false;
    Binding const* binding = symbol_find_node(symbol, SYMBOL_DECL_VARIABLE);
    if (!binding || binding->parent) return false;
    // (Only an annotation, a JSDoc `@type` too: tsgo's isDeclarationWithExplicitTypeAnnotation has no JavaScript
    // function initializer with a `@returns` any more)
    return binding->has_explicit_type;
}

static bool intersection_prop_non_uniform(Checker* me, Ty const* intersection, Atom const* name);

static bool prop_has_explicit_type(Checker* me, Prop const* prop) {
    // A mapped type's property asks the property it was mapped from (tsc's syntheticOrigin), none: not explicit. Over an
    // intersection whose members declare it separately, that is the intersection's property, which has no value
    // declaration: not explicit either (`Id<{ a: A } & { a: A }>`)
    for (Prop const* p = prop; p; p = p->target) {
        if (!p->mapped) continue;
        PropDeclRare const* rare = prop_decl_rare(p->decl);
        Prop const* origin = rare->mapped_from ? rare->mapped_from : rare->remapped_from;
        if (origin && rare->mapped_intersection && intersection_prop_non_uniform(me, rare->mapped_intersection, origin->name)) return false;
        return origin && prop_has_explicit_type(me, origin);
    }
    while (prop->target) prop = prop->target;
    if (!prop->name) return false;
    if (prop->flags & PROP_METHOD) return true; // (tsc: a method's type is always known)
    if (prop->decl->symbol) return symbol_has_explicit_type(prop->decl->symbol);
    if (prop->flags & (PROP_GETTER | PROP_SETTER)) return prop->decl->function->ret_explicit;
    return prop->decl->node || prop->decl->function || prop_decl_rare(prop->decl)->overload_count;
}

// An intersection's property that members declare separately: tsc's createUnionOrIntersectionProperty gives the synthetic
// symbol no value declaration (hasNonUniformValueDeclaration), so it has no explicit type (getExplicitTypeOfSymbol):
// `both.check(x)` with `both: P & Q`, each declaring `check`, is TS2775
static bool intersection_prop_non_uniform(Checker* me, Ty const* intersection, Atom const* name) {
    PropDecl const* first = nullptr;
    for (u32 i = 0; i < intersection->composite.types.len; i++) {
        Ty const* member = ty_apparent_type(me, intersection->composite.types.data[i]);
        if (!(member->flags & TYF_OBJECT)) continue;
        Prop const* prop = ty_get_prop(me, member, name);
        if (!prop) continue;
        while (prop->target) prop = prop->target;
        if (!first) first = prop->decl;
        else if (prop->decl != first) return true;
    }
    return false;
}

// tsc's getTypeOfDottedName(): the type of the callee of a call statement from declarations alone, without control
// flow analysis (which is in the middle of asking). Null: some name on the way has no explicit type, and the call is
// taken to have no effect (tsc reports TS2775/TS2776 for an assertion like that)
// `resolving`: the for-of loops whose variable is being typed further up, a loop iterating its own variable (`for
// (const a of a)`, or `var`s iterating each other) has no explicit type (tsc's resolvingExplicitTypeOfSymbol)
typedef struct ResolvingLoop {
    ForEachLoopStatement const* loop;
    struct ResolvingLoop const* next;
} ResolvingLoop;

static Ty const* type_of_dotted_name_resolving(Checker* me, Expr const* expr, ResolvingLoop const* resolving);

// The symbols (identifiers' Symbols, properties' Props) whose explicit type type_of_dotted_name() is reading further up
// the stack: asked again meanwhile, one has none (tsc's resolvingExplicitTypeOfSymbol). `const xs: ReturnType<typeof
// f>[] = []` with `xs.push(x)` in f before f's `return x`: the flow analysis of x asks whether the push is an assertion
// while xs's type is being read for an outer one, and the push has no effects (openclaw's providerHolds)
// THREADSAFETY: a checker runs on one thread, and the stack unwinds before it moves on
static thread_local void const* explicit_resolving[64];
static thread_local u32 explicit_resolving_len;

static bool explicit_resolving_enter(void const* key) {
    for (u32 i = 0; i < explicit_resolving_len; i++) {
        if (explicit_resolving[i] == key) return false;
    }
    if (explicit_resolving_len < c_arraylen(explicit_resolving)) explicit_resolving[explicit_resolving_len] = key;
    explicit_resolving_len++;
    return true;
}

static void explicit_resolving_leave(void) {
    explicit_resolving_len--;
}

static Ty const* type_of_dotted_name(Checker* me, Expr const* expr) {
    return type_of_dotted_name_resolving(me, expr, nullptr);
}

static Ty const* type_of_dotted_name_resolving(Checker* me, Expr const* expr, ResolvingLoop const* resolving) {
    expr = skip_reference_wrappers(expr);
    if (expr->tag == EXPR_IDENTIFIER) {
        u32 keyword = expr->identifier.keyword;
        if (keyword == KEYWORD_IDENTIFIER_SUPER) return ty_identifier_reference_type(me, &expr->identifier);
        if (keyword == KEYWORD_IDENTIFIER_THIS) {
            // Only a declared `this`: a `this` parameter's or a class's (tsc's getExplicitThisType), or the one the
            // contextual signature's `this` parameter gave the function once it was contextually typed (tsc's
            // assignContextualParameterTypes sets the signature's thisParameter), not one a call inferring now fixes
            ThisKind kind = expr->identifier.this_kind;
            if (kind == THIS_ANY && expr->identifier.this_in_parameter_default) return nullptr;
            if (kind == THIS_ANY && identifier_this_function(&expr->identifier)) {
                u64 this_key[2] = { KEY_CONTEXT_THIS, (u64)(uintptr_t)identifier_this_function(&expr->identifier) };
                return intern_get(&my links, this_key, 2);
            }
            bool is_explicit = kind == THIS_PARAMETER || kind == THIS_CLASS_INSTANCE || kind == THIS_CLASS_STATIC;
            return is_explicit ? ty_identifier_reference_type(me, &expr->identifier) : nullptr;
        }
        Symbol const* symbol = expr->identifier.target;
        if (keyword || !symbol) return nullptr;
        // A named import of a property of what `export =` names (`import { assert } from "chai"`): that property's
        // symbol, explicit if its declaration is (tsc's getExternalModuleMember: symbolFromVariable)
        if (symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_EXPORT_EQUALS_PROPERTY) {
            Ty const* assigned = ty_apparent_type(me, ty_type_of_symbol(me, symbol->decls[0].node));
            if (!(assigned->flags & TYF_OBJECT) && assigned->kind != TY_INTERSECTION) return nullptr;
            Atom const* name = checker_atom(me, symbol->name);
            Prop const* prop = assigned->kind == TY_INTERSECTION ? ty_property_of_type(me, assigned, name) : ty_get_prop(me, assigned, name);
            if (!prop || !prop_has_explicit_type(me, prop)) return nullptr;
            return ty_identifier_reference_type(me, &expr->identifier);
        }
        if (!symbol_has_explicit_type(symbol)) {
            // A `for (const x of xs)` variable without annotation: what the dotted name it iterates yields (tsc's
            // getExplicitTypeOfSymbol)
            Binding const* binding = symbol->flags & SYMBOL_VARIABLE ? symbol_find_node(symbol, SYMBOL_DECL_VARIABLE) : nullptr;
            if (!binding || binding->parent || binding->tag != BINDING_IDENTIFIER || !binding->source ||
                binding->source->tag != LAZY_TYPE_FOR_EACH_LOOP_VAR
            ) {
                return nullptr;
            }
            ForEachLoopStatement const* loop = c_container_of(binding->source, ForEachLoopStatement, var_type);
            if (loop->keyword != FOR_EACH_LOOP_OF) return nullptr;
            for (ResolvingLoop const* r = resolving; r; r = r->next) {
                if (r->loop == loop) return nullptr;
            }
            ResolvingLoop link = { loop, resolving };
            Ty const* iterable = type_of_dotted_name_resolving(me, &loop->iterable, &link);
            if (!iterable) return nullptr;
            return ty_iterated_type_unreported(me, loop->is_await ? ITERATION_FOR_AWAIT_OF : ITERATION_FOR_OF, iterable);
        }
        if (!explicit_resolving_enter(symbol)) return nullptr;
        Ty const* ret = ty_identifier_reference_type(me, &expr->identifier);
        explicit_resolving_leave();
        return ret;
    }
    if (expr->tag != EXPR_PROPERTY) return nullptr;
    if (ty_is_import_meta(expr)) return ty_import_meta_expr_type(me, expr);
    Ty const* object = type_of_dotted_name_resolving(me, &expr->property->expr, resolving);
    Atom const* name = object ? access_name(me, expr->property) : nullptr;
    if (!name) return nullptr;
    object = ty_apparent_type(me, object);
    // (tsc's getPropertyOfType: of an intersection too, `assert.strict.equal` with `strict: Omit<...> & { equal }`)
    if (!(object->flags & TYF_OBJECT) && object->kind != TY_INTERSECTION) return nullptr;
    Prop const* prop = object->kind == TY_INTERSECTION ? ty_property_of_type(me, object, name) : ty_get_prop(me, object, name);
    if (!prop || !prop_has_explicit_type(me, prop)) return nullptr;
    if (object->kind == TY_INTERSECTION && intersection_prop_non_uniform(me, object, name)) return nullptr;
    // (An optional one has undefined in its type, an optional method's too: tsc's getTypeOfSymbol. Not callable then)
    if (!explicit_resolving_enter(prop)) return nullptr;
    Ty const* ret = prop_type(me, prop);
    explicit_resolving_leave();
    return ret;
}

// tsc's isDottedName: `a`, `this`, `super`, `a.b.c`
static bool is_dotted_name(Expr const* expr) {
    if (expr->tag == EXPR_IDENTIFIER) return true;
    return expr->tag == EXPR_PROPERTY && expr->property->form == PROPERTY_EXPR_DOT && is_dotted_name(&expr->property->expr);
}

// The leftmost name of a dotted name that type_of_dotted_name() can't type, with where it's declared
static Expr const* dotted_name_without_type(Checker* me, Expr const* expr) {
    if (expr->tag == EXPR_PROPERTY) {
        Expr const* inner = dotted_name_without_type(me, &expr->property->expr);
        if (inner) return inner;
    }
    return type_of_dotted_name(me, expr) ? nullptr : expr;
}

// `origin`: the name of the property a mapped one was mapped from, which the related information names (tsc's
// symbolToString of the syntheticOrigin), else unchanged
static Span dotted_name_declaration(Checker* me, Expr const* expr, Atom const** origin_name) {
    if (expr->tag == EXPR_IDENTIFIER) {
        Symbol const* symbol = expr->identifier.target;
        Binding const* binding = symbol && (symbol->flags & SYMBOL_VARIABLE) ? symbol_find_node(symbol, SYMBOL_DECL_VARIABLE) : nullptr;
        return binding ? binding->content_span : span_null;
    }
    Ty const* object = type_of_dotted_name(me, &expr->property->expr);
    Atom const* name = object ? access_name(me, expr->property) : nullptr;
    if (!name) return span_null;
    object = ty_apparent_type(me, object);
    if (!(object->flags & TYF_OBJECT) && object->kind != TY_INTERSECTION) return span_null;
    Prop const* prop = object->kind == TY_INTERSECTION ? ty_property_of_type(me, object, name) : ty_get_prop(me, object, name);
    // (A mapped type's property has no value declaration in tsc: the property it was mapped from says where, tsc's
    // getExplicitTypeOfSymbol of the syntheticOrigin. Nor an intersection's that members declare separately, see
    // intersection_prop_non_uniform())
    if (object->kind == TY_INTERSECTION && intersection_prop_non_uniform(me, object, name)) return span_null;
    for (u32 depth = 0; prop && depth < 16; depth++) {
        Prop const* p = prop;
        while (p && !p->mapped) p = p->target;
        if (!p) break;
        PropDeclRare const* rare = prop_decl_rare(p->decl);
        Prop const* origin = rare->mapped_from ? rare->mapped_from : rare->remapped_from;
        if (!origin || (rare->mapped_intersection && intersection_prop_non_uniform(me, rare->mapped_intersection, origin->name))) {
            return span_null;
        }
        prop = origin;
        *origin_name = origin->name;
    }
    while (prop && prop->target && !span_valid(prop->decl->name_span)) prop = prop->target;
    return prop ? prop->decl->name_span : span_null;
}

// tsc's checkCallExpression on an assertion call statement: its callee must be a dotted name (TS2776) whose names all
// have explicit types (TS2775), or it asserts nothing
void ty_check_assertion_call_statement(Checker* me, Expr const* expr, ErrorCtx* errs) {
    // (Nor in an optional chain, `checker?.assert(x)`: the call's type is `void | undefined` there, tsc's returnType
    // is no void)
    if (expr->tag != EXPR_CALL || expr_is_optional_chain(expr) || expr->call->uses_new) return;
    // (A call that fit no signature has the candidate for the failure: `assert(x, extra)` is TS2554 and TS2775)
    Signature const* sig = ty_call_resolved_signature(me, expr->call);
    if (!sig) sig = ty_call_failure_signature(me, expr->call);
    if (!sig || !signature_predicate(me, sig) || !(signature_return_type(me, sig)->flags & TYF_VOID)) return;
    Expr const* callee = &expr->call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    Span span = expr_outer_span(callee);
    if (!is_dotted_name(callee)) {
        error_ctx_error_d(errs, span, 2776, "Assertions require the call target to be an identifier or qualified name.");
        return;
    }
    CallEffects effects = call_effects(me, expr, true);
    if (effects.has_predicate || effects.returns_never) return;
    Error* error = error_ctx_error_d(errs, span, 2775,
        "Assertions require every name in the call target to be declared with an explicit type annotation.");
    Expr const* missing = dotted_name_without_type(me, callee);
    Atom const* origin_name = nullptr;
    Span declaration = missing ? dotted_name_declaration(me, missing, &origin_name) : span_null;
    if (error && span_valid(declaration)) {
        Span name = missing->tag == EXPR_PROPERTY ? property_key_inner_span(missing->property) : missing->inner_span;
        csview shown = origin_name ? atom_sv(origin_name) : span_sv(name);
        error_add_related_d(error, declaration, 2782, "'", shown, "' needs an explicit type annotation.");
    }
}

// tsc's getEffectsSignature(), remembered per call like there
static CallEffects call_effects(Checker* me, Expr const* call_expr, bool is_statement) {
    u32 kind = is_statement ? KEY_FLOW_CALL : KEY_FLOW_CALL_IN_EXPR;
    // (Most calls have no effects: that's a bit in the call's memo, see LazyMemo.no_call_effects)
    LazyType* memo_of = call_expr->lazy_type.slot ? (LazyType*)&call_expr->lazy_type : nullptr;
    LazyMemo const* memo = memo_of ? lazy_type_state_peek(memo_of) : nullptr;
    if (memo && (is_statement ? memo->no_call_effects : memo->no_call_effects_in_expr)) return (CallEffects){0};
    CallEffects const* cached = ty_link_get(me, kind, call_expr);
    if (cached) return *cached;
    CallExpr const* call = call_expr->call;
    Expr const* callee_expr = &call->callable;
    if (callee_expr->tag == EXPR_GENERIC_PARAMS) callee_expr = &callee_expr->generic_params->expr;
    Ty const* callee = nullptr;
    if (is_statement) {
        callee = type_of_dotted_name(me, callee_expr);
    } else if (callee_expr->tag != EXPR_IDENTIFIER || callee_expr->identifier.keyword != KEYWORD_IDENTIFIER_SUPER) {
        callee = ty_flow_expr_type(me, callee_expr);
        // (tsc's checkNonNullExpression, which reports too: `if (f(x))` with `f` possibly undefined is TS18048 at `f`
        // besides the call's TS2722. In an optional chain the callee's own `?.` takes its nullishness out first, the
        // chain's marker comes off otherwise (getOptionalExpressionType): `a?.o()` with `o?: () => T` is TS18048 at
        // `a.o`, `a?.o?.()` isn't)
        if (expr_is_optional_chain(call_expr)) callee = ty_optional_expression_type(me, callee, call->optional_chain, nullptr);
        if (my errs && my errs->phase == ERROR_PHASE_TYPECHECK) {
            Ty const* checked = ty_check_non_null_type(me, callee, callee_expr, my errs);
            callee = checked ? checked : ty_non_nullable(me, callee);
        } else {
            callee = ty_non_nullable_if_needed(me, callee);
        }
    }
    TAINT_FLOW();
    my flow_uncached++;
    CallEffects effects = ty_call_effects(me, callee, call);
    TAINT_FLOW();
    my flow_uncached--;
    TyPredicate const* pr = &effects.predicate;
    bool none = !effects.has_predicate && !effects.returns_never && !pr->is_this && !pr->asserts && !pr->param_index &&
        !pr->param_name && !pr->type;
    if (memo_of && none) {
        LazyMemo* fresh = lazy_type_memo(memo_of);
        if (is_statement) fresh->no_call_effects = true;
        else fresh->no_call_effects_in_expr = true;
        return effects;
    }
    CallEffects* stored = arena_new(&my arena, CallEffects);
    *stored = effects;
    ty_link_set(me, kind, call_expr, stored);
    return effects;
}

static Ty const* non_nullable_constraint_or(Checker* me, Ty const* ty) {
    Ty const* constraint = ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED) ? ty_base_constraint(me, ty) : nullptr;
    return constraint ? constraint : ty;
}

// tsc's isTypeDerivedFrom(): what `instanceof` goes by, the classes something is made of rather than its shape
bool ty_is_type_derived_from(Checker* me, Ty const* source, Ty const* target) {
    if (source->kind == TY_UNION) {
        for (u32 i = 0; i < source->composite.types.len; i++) {
            if (!ty_is_type_derived_from(me, source->composite.types.data[i], target)) return false;
        }
        return true;
    }
    if (target->kind == TY_UNION) {
        for (u32 i = 0; i < target->composite.types.len; i++) {
            if (ty_is_type_derived_from(me, source, target->composite.types.data[i])) return true;
        }
        return false;
    }
    if (source->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < source->composite.types.len; i++) {
            if (ty_is_type_derived_from(me, source->composite.types.data[i], target)) return true;
        }
        return false;
    }
    if (source->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
        Ty const* constraint = ty_base_constraint(me, source);
        if (!constraint || constraint == source) constraint = ty_intrinsic(me, TY_UNKNOWN_);
        return ty_is_type_derived_from(me, constraint, target);
    }
    if (is_empty_anonymous_object(me, target)) return (source->flags & (TYF_OBJECT | TYF_NON_PRIMITIVE)) != 0;
    if (target == ty_global_object_type(me)) {
        return (source->flags & (TYF_OBJECT | TYF_NON_PRIMITIVE)) && !is_empty_anonymous_object(me, source);
    }
    if (target == ty_global_function_type(me)) return (source->flags & TYF_OBJECT) && is_function_object(me, source);
    if (!(source->flags & TYF_OBJECT) || !(target->flags & TYF_OBJECT)) return false;
    if (ty_has_base_type(me, source, target)) return true;
    // (A tuple's base type is the array of its elements, readonly or not: `[number] instanceof Array`. Tuples of
    // one shape share their target, which the check above finds)
    if (ty_is_tuple(source)) {
        source = ty_tuple_as_array(me, source);
        if (ty_has_base_type(me, source, target)) return true;
    }
    // Whatever derives from ReadonlyArray is an Array as far as `instanceof Array` can tell
    Ty const* readonly_array = ty_array(me, ty_intrinsic(me, TY_UNKNOWN_), true);
    if (!ty_is_array(me, target) || target->reference.target == readonly_array->reference.target) return false;
    return ty_has_base_type(me, source, readonly_array);
}

static bool is_strict_subtype(Checker* me, Ty const* source, Ty const* target) {
    return ty_is_related(me, source, target, TY_RELATION_STRICT_SUBTYPE);
}

static bool is_subtype(Checker* me, Ty const* source, Ty const* target) {
    return ty_is_related(me, source, target, TY_RELATION_SUBTYPE);
}

typedef struct {
    Ty const* type;  // The type being narrowed
    Ty const* c;  // The member of the candidate being mapped
    bool check_derived;
} NarrowArg;

// A member t of the narrowed type against the candidate member c: the more specific of the two if they're directly
// related. When they're related both ways, a type predicate prefers c (the asserted type) and `instanceof` prefers t
// (generics aren't reflected in prototype object types)
static Ty const* narrow_directly_related(Checker* me, Ty const* t, void const* arg) {
    NarrowArg const* a = arg;
    Ty const* c = a->c;
    if (a->check_derived) {
        return ty_is_type_derived_from(me, t, c) ? t : ty_is_type_derived_from(me, c, t) ? c : ty_intrinsic(me, TY_NEVER);
    }
    return is_strict_subtype(me, t, c) ? t
        : is_strict_subtype(me, c, t)  ? c
        : is_subtype(me, t, c)         ? t
        : is_subtype(me, c, t)         ? c
                                       : ty_intrinsic(me, TY_NEVER);
}

// Some member of `ty` is instantiable (tsc's maybeTypeOfKind(t, TypeFlags.Instantiable))
static bool maybe_instantiable(Ty const* ty) {
    return ty_maybe_type_of_kind(ty, TYF_TYPE_PARAM | TYF_DEFERRED | TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING);
}

// A generic member (`T`, `T & {}`) whose constraint the candidate member fits is both
static Ty const* narrow_generic_related(Checker* me, Ty const* t, void const* arg) {
    NarrowArg const* a = arg;
    if (!maybe_instantiable(t)) return ty_intrinsic(me, TY_NEVER);
    Ty const* constraint = ty_base_constraint(me, t);
    if (!constraint || constraint == t) constraint = ty_intrinsic(me, TY_UNKNOWN_);
    if (!(a->check_derived ? ty_is_type_derived_from(me, a->c, constraint) : is_subtype(me, a->c, constraint))) {
        return ty_intrinsic(me, TY_NEVER);
    }
    Ty const* both[2] = { t, a->c };
    return ty_intersection(me, both, 2, nullptr, (TyList){0});
}

// One member of the candidate of getNarrowedType(): the members of `type` that are it, or that it is. Built with
// map_type() like tsc's mapType(), so a union whose members all map to themselves stays the same (aliased) type
static Ty const* narrowed_by_candidate_member(Checker* me, Ty const* c, void const* arg) {
    NarrowArg a = *(NarrowArg const*)arg;
    a.c = c;
    Ty const* type = a.type;
    // Only the member with the candidate's key, if the union has a key property (tsc's getNarrowedTypeWorker)
    Atom const* key = type->kind == TY_UNION ? ty_union_key_property(me, type) : nullptr;
    Ty const* discriminant = key ? ty_prop_type_of(me, c, key) : nullptr;
    Ty const* matching = discriminant ? ty_union_member_for_key(me, type, discriminant) : nullptr;
    Ty const* direct = map_type(me, matching ? matching : type, narrow_directly_related, &a);
    if (!(direct->flags & TYF_NEVER)) return direct;
    return map_type(me, type, narrow_generic_related, &a);
}

static bool is_not_subset_of(Checker* me, Ty const* member, void const* arg) {
    (void)me;
    return !is_type_subset_of(member, arg);
}

static bool is_not_derived_from(Checker* me, Ty const* member, void const* arg) {
    return !ty_is_type_derived_from(me, member, arg);
}

static Ty const* narrowed_type_uncached(Checker* me, Ty const* type, Ty const* candidate, bool assume_true, bool check_derived);

// tsc's getNarrowedType(): what a type predicate or `instanceof` makes of a type. Cached like tsc does (the same guard
// on the same declared type is typical: `isIdentifier(node)` all over a code base)
static Ty const* narrowed_type(Checker* me, Ty const* type, Ty const* candidate, bool assume_true, bool check_derived) {
    u64 key[4] = { KEY_FLOW_NARROWED, type->id, candidate->id, (u64)assume_true | (u64)check_derived << 1 };
    Ty const* cached = intern_get(&my links, key, 4);
    if (cached) return cached;
    Ty const* ret = narrowed_type_uncached(me, type, candidate, assume_true, check_derived);
    *intern_slot(me, &my links, key, 4) = ret;
    return ret;
}

static Ty const* narrowed_type_uncached(Checker* me, Ty const* type, Ty const* candidate, bool assume_true, bool check_derived) {
    if (!assume_true) {
        if (type == candidate) return ty_intrinsic(me, TY_NEVER);
        if (check_derived) return filter_type(me, type, is_not_derived_from, candidate);
        // (unknown as `{} | null | undefined`, tsc's unknownUnionType: just unknown without strictNullChecks)
        bool was_unknown = (type->flags & TYF_UNKNOWN) != 0;
        if (was_unknown && compiler_options()->strict_null_checks) {
            Ty const* parts[3] = { ty_unknown_empty_object_type(me), ty_intrinsic(me, TY_NULL), ty_intrinsic(me, TY_UNDEFINED) };
            type = ty_union(me, parts, 3, nullptr, (TyList){0});
        }
        Ty const* true_type = narrowed_type(me, type, candidate, true, false);
        Ty const* rest = filter_type(me, type, is_not_subset_of, true_type);
        return was_unknown && rest == type ? ty_intrinsic(me, TY_UNKNOWN_) : rest;
    }
    if ((type->flags & (TYF_ANY | TYF_UNKNOWN)) || type == candidate) return candidate;
    NarrowArg arg = { .type = type, .check_derived = check_derived };
    Ty const* narrowed = map_type(me, candidate, narrowed_by_candidate_member, &arg);
    if (!(narrowed->flags & TYF_NEVER)) return narrowed;
    if (is_subtype(me, candidate, type)) return candidate;
    if (ty_is_related(me, type, candidate, TY_RELATION_ASSIGNABLE)) return type;
    if (ty_is_related(me, candidate, type, TY_RELATION_ASSIGNABLE)) return candidate;
    Ty const* both[2] = { type, candidate };
    return ty_intersection(me, both, 2, nullptr, (TyList){0});
}

// Discriminants: `x.kind === "a"` says which members of the union x can be

static bool is_literal_type(Ty const* ty) {
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!is_unit_type(ty->composite.types.data[i]) && !(ty->composite.types.data[i]->flags & TYF_ENUM)) return false;
        }
        return true;
    }
    return is_unit_type(ty);
}

static Ty const* prop_type_by_name(Checker* me, Ty const* ty, Atom const* name) {
    return ty_access(me, ty, ty_prop_name_literal(me, name));
}

// tsc's isDiscriminantProperty(): the members of the union have different types for it, a literal type among them
static bool is_discriminant_property(Checker* me, Ty const* type, Atom const* name) {
    if (type->kind != TY_UNION) return false;
    u64 key[3] = { KEY_FLOW_DISCRIMINANT, type->id, (u64)(uintptr_t)name };
    InternMiss miss;
    uintptr_t cached = (uintptr_t)intern_lookup(&my links, key, 3, &miss);
    if (cached) return cached == 2;
    Ty const* first = nullptr;
    bool non_uniform = false;
    bool has_literal = false;
    bool generic = false;
    // tsc's createUnionOrIntersectionProperty looks up the index signature of a member without the property (nullish
    // ones too) with the name's literal type, made then, before any member's property type: which decides union
    // order (relating `Window & typeof globalThis` to a union makes the literals of Window's names). (A unique
    // symbol's name is late-bound: nothing made)
    if (!ty_unique_symbol_of_name(me, name)) {
        for (u32 i = 0; i < type->composite.types.len; i++) {
            Ty const* member = type->composite.types.data[i];
            if (ty_reduced(me, member)->flags & TYF_NEVER) continue;
            Ty const* holder = member->flags & (TYF_OBJECT | TYF_INTERSECTION) ? member : ty_apparent_type(me, member);
            if (!(member->flags & (TYF_NULLABLE | TYF_VOID)) && ty_find_prop(me, holder, name).found) continue;
            ty_prop_name_literal(me, name);
            break;
        }
    }
    for (u32 i = 0; i < type->composite.types.len; i++) {
        Ty const* member = type->composite.types.data[i];
        if (member->flags & (TYF_NULLABLE | TYF_VOID)) continue;
        // (A member that is never has no properties: tsc's createUnionOrIntersectionProperty through getPropertyOfType)
        if (ty_reduced(me, member)->flags & TYF_NEVER) continue;
        // (Its own properties only: an index signature's type takes no part in the uniformity or the literal parts,
        // tsc's createUnionOrIntersectionProperty only counts it among the property's types)
        Ty const* holder = member->flags & (TYF_OBJECT | TYF_INTERSECTION) ? member : ty_apparent_type(me, member);
        if (!ty_find_prop(me, holder, name).found) continue;
        Ty const* prop = prop_type_by_name(me, member, name);
        if (!prop) continue;
        if (!first) first = prop;
        non_uniform |= prop != first;
        // (A pattern template literal is a literal part too: tsc's isPatternLiteralType in createUnionOrIntersectionProperty)
        has_literal |= is_literal_type(prop) || (prop->kind == TY_TEMPLATE_LITERAL && ty_is_pattern_literal(prop));
        generic |= (prop->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) != 0;
    }
    bool ret = non_uniform && has_literal && !generic;
    *intern_insert(me, &my links, &miss, key, 3) = (void const*)(uintptr_t)(ret ? 2 : 1);
    return ret;
}

// Key properties (tsc's getKeyPropertyName and mapTypesByKeyProperty): a big union of objects told apart by a unit
// property maps the property's values to its members, which narrowing and relations look up instead of trying every
// member. Stored in the links table per union: KEY_UNION_KEY_NAME and KEY_UNION_KEY_MEMBER

Ty const* ty_prop_type_of(Checker* me, Ty const* ty, Atom const* name) {
    Ty const* holder = ty->flags & (TYF_OBJECT | TYF_INTERSECTION) ? ty : ty_apparent_type(me, ty);
    // (An intersection has no members of its own: its property is the intersection of its members')
    if (holder->kind == TY_INTERSECTION) {
        FoundProp found = ty_find_prop(me, holder, name);
        return found.found ? found.type : nullptr;
    }
    Prop const* prop = ty_get_prop(me, holder, name);
    return prop ? prop_type(me, prop) : nullptr;
}

// Whether `ty` has the property `name`, the apparent members included (tsc's getPropertyOfType: Function's of a type
// with signatures, Object's)
bool ty_has_prop(Checker* me, Ty const* ty, Atom const* name) {
    return ty_find_prop(me, ty, name).found;
}

static bool is_key_holder(Ty const* ty) {
    return ty->flags & (TYF_OBJECT | TYF_TYPE_PARAM | TYF_DEFERRED);
}

typedef struct KeyEntry {
    Ty const* literal;
    u32 member;
} KeyEntry;

static int key_entry_cmp(void const* a, void const* b) {
    KeyEntry const* x = a;
    KeyEntry const* y = b;
    if (x->literal->id != y->literal->id) return x->literal->id < y->literal->id ? -1 : 1;
    return x->member < y->member ? -1 : x->member > y->member;
}

// Maps the values of `name` to members, if enough members have values of their own (tsc's mapTypesByKeyProperty)
static bool map_by_key_property(Checker* me, Ty const* ty, Atom const* name) {
    TyList members = ty->composite.types;
    KeyEntry* entries = nullptr;
    u32 len = 0, cap = 0;
    bool mapped = true;
    for (u32 i = 0; i < members.len && mapped; i++) {
        Ty const* member = members.data[i];
        if (!(member->flags & (TYF_OBJECT | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED))) continue;
        Ty const* discriminant = ty_prop_type_of(me, member, name);
        if (!discriminant || !is_literal_type(discriminant)) {
            mapped = false;
            break;
        }
        TyList values = discriminant->kind == TY_UNION ? discriminant->composite.types : (TyList){ .data = &discriminant, .len = 1 };
        for (u32 j = 0; j < values.len; j++) {
            if (len == cap) {
                cap = cap ? cap * 2 : 32;
                entries = realloc(entries, sizeof(KeyEntry) * cap);
                panic_if(!entries, "out of memory");
            }
            entries[len++] = (KeyEntry){ ty_regular(me, values.data[j]), i };
        }
    }
    if (!mapped) {
        free(entries);
        return false;
    }
    qsort(entries, len, sizeof(KeyEntry), key_entry_cmp);
    // A value two members have maps to no member, and the second of them doesn't count. The third and later still do:
    // tsc only marks a duplicate while the value still maps to a member (`existing !== unknownType`), so four members
    // with one value count three times
    bool* duplicate = calloc(members.len, sizeof(bool));
    panic_if(!duplicate, "out of memory");
    for (u32 i = 1; i < len; i++) {
        if (entries[i].literal == entries[i - 1].literal && (i == 1 || entries[i - 2].literal != entries[i].literal)) {
            duplicate[entries[i].member] = true;
        }
    }
    u32 count = 0;
    bool* has_value = calloc(members.len, sizeof(bool));
    panic_if(!has_value, "out of memory");
    for (u32 i = 0; i < len; i++) has_value[entries[i].member] = true;
    for (u32 i = 0; i < members.len; i++) count += has_value[i] && !duplicate[i];
    mapped = count >= 10 && count * 2 >= members.len;
    for (u32 i = 0; i < len && mapped; i++) {
        bool shared = (i > 0 && entries[i - 1].literal == entries[i].literal) ||
            (i + 1 < len && entries[i + 1].literal == entries[i].literal);
        if (i > 0 && entries[i - 1].literal == entries[i].literal) continue;
        u64 key[3] = { KEY_UNION_KEY_MEMBER, ty->id, entries[i].literal->id };
        *intern_slot(me, &my links, key, 3) = shared ? ty_intrinsic(me, TY_UNKNOWN_) : members.data[entries[i].member];
    }
    free(has_value);
    free(duplicate);
    free(entries);
    return mapped;
}

static Atom const* key_property_uncached(Checker* me, Ty const* ty) {
    TyList members = ty->composite.types;
    if (members.len < 10) return nullptr;
    u32 holders = 0;
    for (u32 i = 0; i < members.len; i++) holders += is_key_holder(members.data[i]);
    if (holders < 10) return nullptr;
    // The first property of a unit type in one of the members
    Atom const* name = nullptr;
    for (u32 i = 0; i < members.len && !name; i++) {
        Ty const* member = members.data[i];
        if (!is_key_holder(member)) continue;
        Members const* props = ty_members(me, member->flags & TYF_OBJECT ? member : ty_apparent_type(me, member));
        for (u32 j = 0; j < props->prop_count && !name; j++) {
            if (ty_is_unit(prop_type(me, members_prop(me, props, j)))) name = members_prop_name(props, j);
        }
    }
    return name && map_by_key_property(me, ty, name) ? name : nullptr;
}

Atom const* ty_union_key_property(Checker* me, Ty const* ty) {
    if (ty->kind != TY_UNION || ty->composite.types.len < 10) return nullptr;
    u64 key[2] = { KEY_UNION_KEY_NAME, ty->id };
    void const* cached = intern_get(&my links, key, 2);
    if (cached) return cached == ty ? nullptr : cached;
    // (While it is worked out, the union has none: a marker of its own, KEY_IN_FLIGHT, not a placeholder in the memo's
    // slot, which a memo flush could publish)
    u64 flight_key[3] = { KEY_IN_FLIGHT, KEY_UNION_KEY_NAME, ty->id };
    if (__builtin_expect(intern_get(&my links, flight_key, 3) != nullptr, 0)) {
        TAINT_EVENT(TAINT_IN_FLIGHT_SLOT);
        return nullptr;
    }
    *intern_slot(me, &my links, flight_key, 3) = ty;
    Atom const* name = key_property_uncached(me, ty);
    *intern_slot(me, &my links, flight_key, 3) = nullptr;
    *intern_slot(me, &my links, key, 2) = name ? (void const*)name : (void const*)ty;
    return name;
}

Ty const* ty_union_member_for_key(Checker* me, Ty const* ty, Ty const* key) {
    u64 slot_key[3] = { KEY_UNION_KEY_MEMBER, ty->id, ty_regular(me, key)->id };
    Ty const* ret = intern_get(&my links, slot_key, 3);
    return ret && !(ret->flags & TYF_UNKNOWN) ? ret : nullptr;
}

// `const k = x.kind`: the initializer of a const variable without a type annotation, if it is an access (tsc's
// getCandidateDiscriminantPropertyAccess)
static Expr const* aliased_access(Expr const* expr) {
    if (expr->tag != EXPR_IDENTIFIER) return nullptr;
    Symbol const* symbol = expr->identifier.target;
    if (!symbol || !symbol->is_const || symbol->decl_count != 1 || symbol->decls[0].kind != SYMBOL_DECL_VARIABLE) return nullptr;
    Binding const* binding = symbol->decls[0].node;
    if (binding->parent || binding->has_explicit_type || !binding->source || binding->source->tag != LAZY_TYPE_INITIALIZED_BINDING) {
        return nullptr;
    }
    InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
    return initialized_binding_value(initialized)->tag == EXPR_PROPERTY ? initialized_binding_value(initialized) : nullptr;
}

bool ty_is_discriminant_property(Checker* me, Ty const* type, Atom const* name) {
    return is_discriminant_property(me, type, name);
}

// What names a discriminant property of the reference: an access `reference.name` (null for a destructured one), and
// the name (null if nothing does)
typedef struct DiscriminantAccess {
    PropertyExpr const* access;
    Atom const* name;
} DiscriminantAccess;

// The position of a parameter (without an initializer, not `...`, `this` not counted) of `function` that `symbol` is
static bool parameter_index(FunctionImpl const* function, Symbol const* symbol, u32* index) {
    u32 at = 0;
    for_each (i, vec_function_arg, &function->args) {
        Binding const* binding = &i->initializer.binding;
        if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
        if (binding->tag == BINDING_IDENTIFIER && binding->symbol == symbol) {
            *index = at;
            return !i->rest && initialized_binding_value(&i->initializer)->tag == EXPR_IMPLICIT_ANY;
        }
        at++;
    }
    return false;
}

// The property an element of `pattern` destructures, if `symbol` is one without a default or `...`
static Atom const* pattern_element_name(Checker* me, Binding const* pattern, Symbol const* symbol) {
    // (By its value declaration, the first: a `var kind` redeclaring a parameter's element keeps it, tsc's
    // getCandidateDiscriminantPropertyAccess)
    if (!symbol || !symbol->decl_count || symbol->decls[0].kind != SYMBOL_DECL_VARIABLE) return nullptr;
    Binding const* binding = symbol->decls[0].node;
    if (binding->parent != pattern || binding->default_value || binding->rest) return nullptr;
    if (pattern->tag == BINDING_ARRAY) {
        return ty_index_atom(me, (u32)binding->parent_index);
    }
    ObjectBindingItem const* item = binding->parent_item;
    if (!item || item->computed_key) return nullptr;
    // (`{ 0: tag }` names element 0, like `[tag]`)
    if (item->source.tag == VALUE_NUMBER) {
        f64 n = item->source.number;
        return n >= 0 && n < 4294967295.0 && n == (f64)(u32)n ? ty_index_atom(me, (u32)n) : nullptr;
    }
    if (item->source.tag != VALUE_STRING) return nullptr;
    return checker_atom(me, cstr_sv(&item->source.string));
}

// `const { kind } = x`, `const { kind: k } = x.y`, `const [tag] = x`: a const element of an unannotated variable's pattern,
// without a default, whose initializer is the reference (tsc's getCandidateDiscriminantPropertyAccess). The name
static Atom const* destructured_alias_name(FlowQuery* q, Expr const* expr) {
    Checker* me = q->me;
    if (expr->tag != EXPR_IDENTIFIER || expr->identifier.keyword) return nullptr;
    Symbol const* symbol = expr->identifier.target;
    if (!symbol || !symbol->is_const || symbol->decl_count != 1 || symbol->decls[0].kind != SYMBOL_DECL_VARIABLE) return nullptr;
    Binding const* binding = symbol->decls[0].node;
    Binding const* pattern = binding->parent;
    if (!pattern || pattern->parent || binding->default_value || binding->rest) return nullptr;
    if (pattern->has_explicit_type || !pattern->source || pattern->source->tag != LAZY_TYPE_INITIALIZED_BINDING) return nullptr;
    Atom const* name = pattern_element_name(me, pattern, symbol);
    if (!name) return nullptr;
    InitializedBinding const* initialized = c_container_of(pattern->source, InitializedBinding, lazy_type);
    Expr const* init = initialized_binding_value(initialized);
    if (init->tag != EXPR_IDENTIFIER && init->tag != EXPR_PROPERTY) return nullptr;
    if (!query_matches(q, init)) return nullptr;
    return name;
}

// The type whose properties tell whether a name is a discriminant: the declared type while the computed one is a subset
// of it, `!success || !data` of a destructured union still finds `data` one where `!success` left a single member
// (tsc's getDiscriminantPropertyAccess)
static Ty const* discriminant_candidate(FlowQuery* q, Ty const* computed) {
    return q->declared->kind == TY_UNION && query_subset_of(q, computed, q->declared) ? q->declared : computed;
}

// tsc's getDiscriminantPropertyAccess(): `expr` if it is `reference.name` for a discriminant property, or a const alias
// of that (`const k = x.kind`, `const { kind } = x`)
static DiscriminantAccess discriminant_access(FlowQuery* q, Expr const* expr, Ty const* computed) {
    Checker* me = q->me;
    DiscriminantAccess none = {0};
    if (q->declared->kind != TY_UNION && computed->kind != TY_UNION) return none;
    expr = skip_reference_wrappers(expr);
    if (q->parameters_of) {
        u32 index = 0;
        Atom const* name = nullptr;
        Symbol const* symbol = expr->tag == EXPR_IDENTIFIER && !expr->identifier.keyword ? expr->identifier.target : nullptr;
        if (symbol && symbol->is_parameter && parameter_index(q->parameters_of, symbol, &index)) {
            name = ty_index_atom(me, (u32)index);
        }
        return name && is_discriminant_property(me, discriminant_candidate(q, computed), name) ?
            (DiscriminantAccess){ nullptr, name } : none;
    }
    if (q->pattern) {
        // An element of the pattern, without a default (tsc's getCandidateDiscriminantPropertyAccess)
        Atom const* name = expr->tag == EXPR_IDENTIFIER ? pattern_element_name(me, q->pattern, expr->identifier.target) : nullptr;
        return name && is_discriminant_property(me, discriminant_candidate(q, computed), name) ?
            (DiscriminantAccess){ nullptr, name } : none;
    }
    Expr const* aliased = aliased_access(expr);
    Atom const* destructured = aliased ? nullptr : destructured_alias_name(q, expr);
    if (aliased) expr = aliased;
    Ty const* type = discriminant_candidate(q, computed);
    if (destructured) {
        return is_discriminant_property(me, type, destructured) ? (DiscriminantAccess){ nullptr, destructured } : none;
    }
    if (expr->tag != EXPR_PROPERTY || !query_matches(q, &expr->property->expr)) return none;
    Atom const* name = access_name(me, expr->property);
    if (!name) return none;
    return is_discriminant_property(me, type, name) ? (DiscriminantAccess){ expr->property, name } : none;
}

typedef Ty const* (*NarrowPropFn)(FlowQuery* q, Ty const* prop_type, void const* arg);

typedef struct DiscriminantFilter {
    Atom const* name;
    Ty const* narrowed;
} DiscriminantFilter;

static bool keep_by_discriminant(Checker* me, Ty const* member, void const* arg) {
    DiscriminantFilter const* filter = arg;
    Ty const* discriminant = prop_type_by_name(me, member, filter->name);
    // (A tuple without that element has its number index signature: tsc's getTypeOfPropertyOrIndexSignatureOfType,
    // `[5]` isn't kept by `y === 2` of `[x, y]: [1, 2] | [5]`)
    if (!discriminant && ty_tuple_shape(member) && ty_is_numeric_name(filter->name)) {
        discriminant = ty_access(me, member, ty_intrinsic(me, TY_NUMBER));
        // (addOptionality: an index signature's value may be missing)
        if (discriminant) discriminant = union_of(me, discriminant, ty_intrinsic(me, TY_UNDEFINED));
    }
    if (!discriminant) discriminant = ty_intrinsic(me, TY_UNKNOWN_);
    if ((discriminant->flags & TYF_NEVER) || (filter->narrowed->flags & TYF_NEVER)) return false;
    return ty_is_comparable_either_way(me, filter->narrowed, discriminant);
}

// The members of `type` whose property could be what `narrow` leaves of the property's type
static Ty const* narrow_by_discriminant(FlowQuery* q, Ty const* type, DiscriminantAccess found, NarrowPropFn narrow, void const* arg) {
    Checker* me = q->me;
    Atom const* name = found.name;
    PropertyExpr const* access = found.access;
    if (!name) return type;
    bool optional_chain = access && access->optional_chain;
    bool remove_nullable = compiler_options()->strict_null_checks && has_type_facts(me, type, TF_EQ_UNDEFINED_OR_NULL) &&
        (optional_chain || (access && access->expr.tag == EXPR_UNARY_OP && access->expr.unary_op->op == UNARY_OP_RIGHT_BANG &&
            // (Not `(o!).k`: tsc's isNonNullAccess sees a parenthesized expression there)
            !expr_is_perenthisized(&access->expr)));
    Ty const* holder = remove_nullable ? type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL) : type;
    // (`x!.kind` of an x that is only null here: never has no properties, tsc's getTypeOfPropertyOfType finds none and
    // doesn't narrow)
    if (holder->flags & TYF_NEVER) return type;
    // (A nullable member has no such property: the union's property is partial, tsc's getTypeOfPropertyOfType finds
    // none and doesn't narrow)
    if (holder->kind == TY_UNION) {
        for (u32 i = 0; i < holder->composite.types.len; i++) {
            if (holder->composite.types.data[i]->flags & (TYF_NULLABLE | TYF_VOID)) return type;
        }
    }
    Ty const* prop = prop_type_by_name(me, holder, name);
    // (Tuples of different lengths: one without the element counts its rest element, else undefined, tsc's
    // createUnionOrIntersectionProperty with a tuple's index signature)
    if (!prop && holder->kind == TY_UNION && ty_is_numeric_name(name)) {
        TyBuf parts = {0};
        bool all = true;
        for (u32 i = 0; i < holder->composite.types.len && all; i++) {
            Ty const* member = holder->composite.types.data[i];
            Ty const* element = prop_type_by_name(me, member, name);
            TupleShape const* shape = element ? nullptr : ty_tuple_shape(member);
            if (!element && shape) {
                bool rest = shape->arity && (shape->flags[shape->arity - 1] & TUPLE_ELEMENT_REST);
                element = rest ? ty_access(me, member, ty_intrinsic(me, TY_NUMBER)) : ty_intrinsic(me, TY_UNDEFINED);
            }
            if (element) ty_buf_push(&parts, element);
            else all = false;
        }
        if (all && parts.len) prop = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
    }
    if (!prop) return type;
    if (remove_nullable && optional_chain) prop = union_of(me, prop, ty_intrinsic(me, TY_UNDEFINED));
    DiscriminantFilter filter = { name, narrow(q, prop, arg) };
    return filter_type(me, type, keep_by_discriminant, &filter);
}

static bool is_not_type(Checker* me, Ty const* member, void const* arg) {
    (void)me;
    return member != arg;
}

// `x.kind === k` with kind the key property of x's union: the member for k (tsc's narrowTypeByDiscriminantProperty).
// Null if that doesn't decide it
static Ty const* narrow_by_key_equality(
    FlowQuery* q, Ty const* type, DiscriminantAccess access, BinaryOp op, Expr const* value, bool assume_true
) {
    Checker* me = q->me;
    if (op != BINARY_OP_TRIPLE_EQUAL && op != BINARY_OP_TRIPLE_NOT_EQUAL) return nullptr;
    Atom const* key = ty_union_key_property(me, type);
    if (!key || key != access.name) return nullptr;
    Ty const* candidate = ty_union_member_for_key(me, type, ty_flow_expr_type(me, value));
    if (!candidate) return nullptr;
    if ((op == BINARY_OP_TRIPLE_EQUAL) == assume_true) return candidate;
    Ty const* candidate_key = ty_prop_type_of(me, candidate, key);
    return candidate_key && ty_is_unit(candidate_key) ? filter_type(me, type, is_not_type, candidate) : type;
}

static Ty const* narrow_prop_by_facts(FlowQuery* q, Ty const* prop_type, void const* arg) {
    return type_with_facts(q->me, prop_type, *(u32 const*)arg);
}

// Conditions

static Ty const* narrow_by_truthiness(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_true) {
    Checker* me = q->me;
    if (query_matches(q, expr)) {
        return adjusted_type_with_facts(me, type, assume_true ? TF_TRUTHY : TF_FALSY);
    }
    if (compiler_options()->strict_null_checks && assume_true && chain_contains_reference(me, expr, q->reference, true)) {
        type = adjusted_type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL);
    }
    DiscriminantAccess access = discriminant_access(q, expr, type);
    if (access.name) {
        u32 facts = assume_true ? TF_TRUTHY : TF_FALSY;
        return narrow_by_discriminant(q, type, access, narrow_prop_by_facts, &facts);
    }
    return type;
}

static Ty const* narrow_by_optionality(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_present) {
    Checker* me = q->me;
    if (query_matches(q, expr)) {
        return adjusted_type_with_facts(me, type, assume_present ? TF_NE_UNDEFINED_OR_NULL : TF_EQ_UNDEFINED_OR_NULL);
    }
    DiscriminantAccess access = discriminant_access(q, expr, type);
    if (access.name) {
        u32 facts = assume_present ? TF_NE_UNDEFINED_OR_NULL : TF_EQ_UNDEFINED_OR_NULL;
        return narrow_by_discriminant(q, type, access, narrow_prop_by_facts, &facts);
    }
    return type;
}

static bool is_comparable_to(Checker* me, Ty const* member, void const* arg) {
    Ty const* value = arg;
    return ty_is_comparable_either_way(me, member, value);
}

// tsc's isCoercibleUnderDoubleEquals: a number, string or boolean literal member may `==` a number, string or boolean
// (not a literal: `"bar" == "foo"` is false)
static bool is_coercible_under_double_equals(Ty const* source, Ty const* target) {
    return (source->flags & (TYF_NUMBER | TYF_STRING | TYF_BOOLEAN_LITERAL)) &&
        ((target->kind == TY_INTRINSIC && (target->flags & (TYF_NUMBER | TYF_STRING))) || (target->flags & TYF_BOOLEAN));
}

static bool is_comparable_or_coercible(Checker* me, Ty const* member, void const* arg) {
    Ty const* value = arg;
    return ty_is_comparable_either_way(me, member, value) || is_coercible_under_double_equals(member, value);
}

// tsc's isUnitLikeType: a unit type, or an intersection with one (a tagged literal, `"a" & { tag: 1 }`), of the base
// constraint
// (The slow part, for members is_unit_type() says no to: hot in the narrowing of big unions)
[[gnu::noinline]] static bool is_unit_like_type(Checker* me, Ty const* ty) {
    if (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
        Ty const* constraint = ty_base_constraint(me, ty);
        if (constraint) ty = constraint;
    }
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_unit_type(ty->composite.types.data[i])) return true;
        }
        return false;
    }
    return is_unit_type(ty);
}

static bool is_not_unit_comparable_to(Checker* me, Ty const* member, void const* arg) {
    Ty const* value = arg;
    // (A computed enum is a unit type to tsc's isUnitLikeType here: `x !== 1` takes out an `EE` it's comparable to)
    bool unit_like = is_unit_type(member) || (member->kind != TY_UNION && (member->flags & TYF_ENUM)) ||
        ((member->flags & (TYF_TYPE_PARAM | TYF_DEFERRED | TYF_INTERSECTION)) && is_unit_like_type(me, member));
    return !(unit_like && ty_is_comparable_either_way(me, member, value));
}

// `type` narrowed by `!== value`, a unit type. Memoized for unions: `node.kind !== SyntaxKind.X` filters a union of
// hundreds of enum members, a cache miss per member (TypeScript's compiler: 3% of its instructions, the most data cache
// misses of any function)
static Ty const* without_unit(Checker* me, Ty const* type, Ty const* value) {
    if (type->kind != TY_UNION) return filter_type(me, type, is_not_unit_comparable_to, value);
    u64 key[3] = { KEY_FLOW_NOT_UNIT, type->id, value->id };
    InternMiss miss;
    Ty const* ret = intern_lookup(&my links, key, 3, &miss);
    if (ret) return ret;
    ret = filter_type(me, type, is_not_unit_comparable_to, value);
    *intern_insert(me, &my links, &miss, key, 3) = ret;
    return ret;
}

typedef struct ExtractKindArg {
    Ty const* literals;
} ExtractKindArg;

static bool keep_flags(Checker* me, Ty const* ty, void const* arg) {
    (void)me;
    return (ty->flags & *(u32 const*)arg) != 0;
}

// `string` narrowed by `"a" | "b"` is `"a" | "b"`
static u32 union_flags(Ty const* ty) {
    if (ty->kind != TY_UNION) return ty->flags;
    u32 flags = 0;
    for (u32 i = 0; i < ty->composite.types.len; i++) flags |= ty->composite.types.data[i]->flags;
    return flags;
}

// tsc's replacePrimitivesWithLiterals, member by member
static Ty const* map_primitive_to_literals(Checker* me, Ty const* ty, void const* arg) {
    Ty const* literals = arg;
    u32 kinds = 0;
    if (ty->kind == TY_INTRINSIC && (ty->flags & TYF_STRING)) {
        kinds = TYF_STRING | TYF_STRING_LITERAL | TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING;
    } else if (ty->kind == TY_TEMPLATE_LITERAL && ty_is_pattern_literal(ty)) {
        // (A pattern `foo-${string}` compared with "foo-test" is that literal)
        if (union_flags(literals) & (TYF_STRING | TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) return ty;
        kinds = TYF_STRING_LITERAL;
    } else if (ty->kind == TY_INTRINSIC && (ty->flags & TYF_NUMBER)) {
        kinds = TYF_NUMBER | TYF_NUMBER_LITERAL;
    } else if (ty->kind == TY_INTRINSIC && (ty->flags & TYF_BIGINT)) {
        kinds = TYF_BIGINT | TYF_BIGINT_LITERAL;
    }
    if (!kinds) return ty;
    Ty const* extracted = filter_type(me, literals, keep_flags, &kinds);
    return extracted->flags & TYF_NEVER ? ty : extracted;
}

static Ty const* replace_primitives_with_literals(Checker* me, Ty const* with_primitives, Ty const* with_literals) {
    if ((union_flags(with_primitives) & (TYF_STRING | TYF_TEMPLATE_LITERAL | TYF_NUMBER | TYF_BIGINT)) &&
        (union_flags(with_literals) & (TYF_STRING_LITERAL | TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING | TYF_NUMBER_LITERAL |
            TYF_BIGINT_LITERAL))
    ) {
        return map_type(me, with_primitives, map_primitive_to_literals, with_literals);
    }
    return with_primitives;
}

static bool some_member_is_empty_object(Checker* me, Ty const* ty) {
    if (ty->kind != TY_UNION) return is_empty_anonymous_object(me, ty);
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (is_empty_anonymous_object(me, ty->composite.types.data[i])) return true;
    }
    return false;
}

// tsc's Primitive type flags on a non-union (literals, enums, unique symbols, templates, void-likes, null)
static bool is_primitive_value(Ty const* ty) {
    return !(ty->flags & (TYF_ANY | TYF_UNKNOWN | TYF_NEVER | TYF_NON_PRIMITIVE | TYF_OBJECT | TYF_TYPE_PARAM |
        TYF_INTERSECTION | TYF_DEFERRED));
}

// tsc's isUniformUnionType: a union of primitives only (tsc's PrimitiveUnion: no object, any, unknown, void,
// intersection or instantiable member) whose enum members all come from one enum and aren't mixed with string or
// number literals
static bool is_uniform_union(Ty const* ty) {
    if (ty->kind != TY_UNION) return false;
    Symbol const* enum_decl = nullptr;
    bool has_literal = false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        Ty const* member = ty->composite.types.data[i];
        if (member->flags & (TYF_ANY | TYF_UNKNOWN | TYF_VOID | TYF_NEVER | TYF_OBJECT | TYF_INTERSECTION |
            TYF_TYPE_PARAM | TYF_DEFERRED | TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) {
            return false;
        }
        if (member->kind == TY_LITERAL && (member->flags & (TYF_ENUM_LITERAL | TYF_ENUM))) {
            if (has_literal) return false;
            if (!enum_decl) enum_decl = member->literal.enum_decl;
            else if (enum_decl != member->literal.enum_decl) return false;
        } else if (member->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) {
            if (enum_decl) return false;
            has_literal = true;
        }
    }
    return true;
}

// tsc's unionContainsType (matchSymbol false) for a non-nullable value: the member itself, or the primitive it's a
// literal of
static bool union_contains_value(Checker* me, Ty const* union_ty, Ty const* value) {
    TyList members = union_ty->composite.types;
    Ty const* primitive = nullptr;
    if (value->flags & TYF_STRING_LITERAL) primitive = ty_intrinsic(me, TY_STRING);
    else if (value->flags & (TYF_ENUM | TYF_NUMBER_LITERAL)) primitive = ty_intrinsic(me, TY_NUMBER);
    else if (value->flags & TYF_BIGINT_LITERAL) primitive = ty_intrinsic(me, TY_BIGINT);
    for (u32 i = 0; i < members.len; i++) {
        if (members.data[i] == value || members.data[i] == primitive) return true;
    }
    return false;
}

static Ty const* narrow_by_equality(FlowQuery* q, Ty const* type, BinaryOp op, Expr const* value, bool assume_true) {
    Checker* me = q->me;
    if (type->flags & TYF_ANY) return type;
    if (op == BINARY_OP_NOT_EQUAL || op == BINARY_OP_TRIPLE_NOT_EQUAL) assume_true = !assume_true;
    Ty const* value_type = ty_flow_expr_type(me, value);
    bool double_equals = op == BINARY_OP_DOUBLE_EQUAL || op == BINARY_OP_NOT_EQUAL;
    if (value_type->flags & TYF_NULLABLE && value_type->kind != TY_UNION) {
        if (!compiler_options()->strict_null_checks) return type;
        u32 facts = double_equals ? (assume_true ? TF_EQ_UNDEFINED_OR_NULL : TF_NE_UNDEFINED_OR_NULL) :
            value_type->flags & TYF_NULL ? (assume_true ? TF_EQ_NULL : TF_NE_NULL) :
            (assume_true ? TF_EQ_UNDEFINED : TF_NE_UNDEFINED);
        return adjusted_type_with_facts(me, type, facts);
    }
    if (assume_true) {
        if (!double_equals && ((type->flags & TYF_UNKNOWN) || some_member_is_empty_object(me, type))) {
            // (Primitives: `boolean` and enums, unions of them, too; not any, unknown or never, which aren't tsc's
            // Primitive | NonPrimitive: `{}` compared with an any stays `{}`)
            if ((value_type->kind == TY_INTRINSIC && !(value_type->flags & (TYF_ANY | TYF_UNKNOWN | TYF_NEVER))) ||
                value_type->kind == TY_LITERAL || (value_type->flags & TYF_BOOLEAN) ||
                ty_is_enum_union(value_type) || is_empty_anonymous_object(me, value_type)
            ) {
                return value_type;
            }
            if (value_type->flags & TYF_OBJECT) return ty_intrinsic(me, TY_NON_PRIMITIVE);
        }
        // A union of primitives with a member the value is (or a primitive it's a literal of) narrows to the value's
        // regular twin: `u === "a"` with u a `string | undefined` is a regular "a", which a return type or a `let`
        // then keeps (`string` alone goes the filtering way below and keeps the fresh one, which widens)
        if (!double_equals && value_type->kind != TY_UNION && is_primitive_value(value_type) && is_uniform_union(type)) {
            Ty const* regular = ty_regular(me, value_type);
            if (union_contains_value(me, type, regular)) return regular;
        }
        Ty const* filtered = filter_type(me, type, double_equals ? is_comparable_or_coercible : is_comparable_to, value_type);
        return replace_primitives_with_literals(me, filtered, value_type);
    }
    if (is_unit_type(value_type)) return without_unit(me, type, value_type);
    return type;
}

typedef struct TypeofArg {
    Ty const* implied;
    u32 facts;
} TypeofArg;

static Ty const* map_by_type_facts(Checker* me, Ty const* ty, void const* arg) {
    TypeofArg const* a = arg;
    // A subtype of the implied type stays or goes by its facts (functions are subtypes of `object`, but their typeof
    // isn't "object"); a supertype (`unknown`, `{}`) becomes the implied type; anything else may overlap
    if (ty_is_related(me, ty, a->implied, TY_RELATION_STRICT_SUBTYPE)) {
        return has_type_facts(me, ty, a->facts) ? ty : ty_intrinsic(me, TY_NEVER);
    }
    if (ty_is_related(me, a->implied, ty, TY_RELATION_SUBTYPE)) return a->implied;
    if (!has_type_facts(me, ty, a->facts)) return ty_intrinsic(me, TY_NEVER);
    Ty const* types[2] = { ty, a->implied };
    return ty_intersection(me, types, 2, nullptr, (TyList){0});
}

static Ty const* narrow_by_type_facts(Checker* me, Ty const* type, Ty const* implied, u32 facts) {
    if (!implied) return type;
    TypeofArg arg = { implied, facts };
    return map_type(me, type, map_by_type_facts, &arg);
}

static Ty const* narrow_by_type_name(Checker* me, Ty const* type, csview name) {
    if (csview_equals(name, "string")) return narrow_by_type_facts(me, type, ty_intrinsic(me, TY_STRING), TF_TYPEOF_EQ_STRING);
    if (csview_equals(name, "number")) return narrow_by_type_facts(me, type, ty_intrinsic(me, TY_NUMBER), TF_TYPEOF_EQ_NUMBER);
    if (csview_equals(name, "bigint")) return narrow_by_type_facts(me, type, ty_intrinsic(me, TY_BIGINT), TF_TYPEOF_EQ_BIGINT);
    if (csview_equals(name, "boolean")) return narrow_by_type_facts(me, type, ty_intrinsic(me, TY_BOOLEAN), TF_TYPEOF_EQ_BOOLEAN);
    if (csview_equals(name, "symbol")) return narrow_by_type_facts(me, type, ty_intrinsic(me, TY_ES_SYMBOL), TF_TYPEOF_EQ_SYMBOL);
    if (csview_equals(name, "object")) {
        if (type->flags & TYF_ANY) return type;
        Ty const* object = narrow_by_type_facts(me, type, ty_intrinsic(me, TY_NON_PRIMITIVE), TF_TYPEOF_EQ_OBJECT);
        return union_of(me, object, narrow_by_type_facts(me, type, ty_intrinsic(me, TY_NULL), TF_EQ_NULL));
    }
    if (csview_equals(name, "function")) {
        if (type->flags & TYF_ANY) return type;
        return narrow_by_type_facts(me, type, ty_global_function_type(me), TF_TYPEOF_EQ_FUNCTION);
    }
    if (csview_equals(name, "undefined")) return narrow_by_type_facts(me, type, ty_intrinsic(me, TY_UNDEFINED), TF_EQ_UNDEFINED);
    return narrow_by_type_facts(me, type, ty_intrinsic(me, TY_NON_PRIMITIVE), TF_TYPEOF_EQ_HOST_OBJECT);
}

static u32 typeof_ne_facts(csview name) {
    if (csview_equals(name, "string")) return TF_TYPEOF_NE_STRING;
    if (csview_equals(name, "number")) return TF_TYPEOF_NE_NUMBER;
    if (csview_equals(name, "bigint")) return TF_TYPEOF_NE_BIGINT;
    if (csview_equals(name, "boolean")) return TF_TYPEOF_NE_BOOLEAN;
    if (csview_equals(name, "symbol")) return TF_TYPEOF_NE_SYMBOL;
    if (csview_equals(name, "undefined")) return TF_NE_UNDEFINED;
    if (csview_equals(name, "object")) return TF_TYPEOF_NE_OBJECT;
    if (csview_equals(name, "function")) return TF_TYPEOF_NE_FUNCTION;
    return TF_TYPEOF_NE_HOST_OBJECT;
}

typedef struct TypeofPropArg {
    csview name;
    bool assume_true;
} TypeofPropArg;

static Ty const* narrow_prop_by_typeof(FlowQuery* q, Ty const* prop_type, void const* arg) {
    TypeofPropArg const* a = arg;
    return a->assume_true ? narrow_by_type_name(q->me, prop_type, a->name) :
        adjusted_type_with_facts(q->me, prop_type, typeof_ne_facts(a->name));
}

static Ty const* narrow_by_typeof(FlowQuery* q, Ty const* type, Expr const* typeof_expr, BinaryOp op, Expr const* literal, bool assume_true) {
    Checker* me = q->me;
    if (op == BINARY_OP_NOT_EQUAL || op == BINARY_OP_TRIPLE_NOT_EQUAL) assume_true = !assume_true;
    Expr const* target = reference_candidate(&typeof_expr->unary_op->expr);
    csview name = cstr_sv(&literal->literal.string);
    if (!query_matches(q, target)) {
        if (compiler_options()->strict_null_checks &&
            chain_contains_reference(me, assignment_candidate(&typeof_expr->unary_op->expr), q->reference, true) &&
            assume_true == !csview_equals(name, "undefined")
        ) {
            type = adjusted_type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL);
        }
        DiscriminantAccess access = discriminant_access(q, target, type);
        if (access.name) {
            TypeofPropArg arg = { name, assume_true };
            return narrow_by_discriminant(q, type, access, narrow_prop_by_typeof, &arg);
        }
        return type;
    }
    return assume_true ? narrow_by_type_name(me, type, name) : adjusted_type_with_facts(me, type, typeof_ne_facts(name));
}

static bool every_member_has_flags(Ty const* ty, u32 flags, bool want) {
    if (ty->kind != TY_UNION) return ((ty->flags & flags) != 0) == want;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (((ty->composite.types.data[i]->flags & flags) != 0) != want) return false;
    }
    return true;
}

// `obj?.foo === value`: obj isn't nullish where the comparison can only hold for a chain that got to the end
static Ty const* narrow_by_optional_chain_containment(FlowQuery* q, Ty const* type, BinaryOp op, Expr const* value, bool assume_true) {
    Checker* me = q->me;
    bool equals = op == BINARY_OP_DOUBLE_EQUAL || op == BINARY_OP_TRIPLE_EQUAL;
    u32 nullable = op == BINARY_OP_DOUBLE_EQUAL || op == BINARY_OP_NOT_EQUAL ? TYF_NULLABLE : TYF_UNDEFINED;
    Ty const* value_type = ty_flow_expr_type(me, value);
    bool remove = (equals != assume_true && every_member_has_flags(value_type, nullable, true)) ||
        (equals == assume_true && every_member_has_flags(value_type, TYF_ANY | TYF_UNKNOWN | nullable, false));
    return remove ? adjusted_type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL) : type;
}

static bool is_typeof_expr(Expr const* expr) {
    return expr->tag == EXPR_UNARY_OP && expr->unary_op->op == UNARY_OP_TYPEOF;
}

static bool is_string_literal_expr(Expr const* expr) {
    return expr->tag == EXPR_LITERAL && expr->literal.tag == VALUE_STRING;
}

typedef struct EqualityPropArg {
    BinaryOp op;
    Expr const* value;
    bool assume_true;
} EqualityPropArg;

static Ty const* narrow_prop_by_equality(FlowQuery* q, Ty const* prop_type, void const* arg) {
    EqualityPropArg const* a = arg;
    return narrow_by_equality(q, prop_type, a->op, a->value, a->assume_true);
}

static Ty const* constructor_instance_type(Checker* me, Ty const* constructor, void const* arg) {
    (void)arg;
    Ty const* prototype = ty_access(me, constructor, ty_string_literal(me, c_sv("prototype")));
    if (prototype && !(prototype->flags & TYF_ANY)) return prototype;
    Ty const* apparent = ty_apparent_type(me, constructor);
    if ((apparent->flags & TYF_OBJECT) || apparent->kind == TY_INTERSECTION) {
        Members const* members = ty_members(me, apparent);
        if (members->construct_count) {
            TyBuf results = {0};
            for (u32 i = 0; i < members->construct_count; i++) {
                ty_buf_push(&results, signature_return_type(me, ty_erased_signature(me, members->construct[i])));
            }
            Ty const* ret = ty_union(me, results.data, results.len, nullptr, (TyList){0});
            ty_buf_drop(&results);
            return ret;
        }
    }
    return ty_empty_object_type(me);
}

// What the `[Symbol.hasInstance]` method of the right operand of `instanceof` says its parameter is, if its signature
// has a type predicate about it (tsc's getEffectsSignature of an InstanceofExpression). A union's members' predicates
// make a union (tsc's union signatures). A generic method is only instantiated with `this` (`<T>(this: T, v)`).
// TODO: overloads and other generic methods, which tsc resolves as a call with the left operand
static Ty const* has_instance_predicate_type(Checker* me, Ty const* right_type, Atom const* name) {
    if (right_type->kind == TY_UNION) {
        TyBuf types = {0};
        for (u32 i = 0; i < right_type->composite.types.len; i++) {
            Ty const* part = has_instance_predicate_type(me, right_type->composite.types.data[i], name);
            if (!part) {
                ty_buf_drop(&types);
                return nullptr;
            }
            ty_buf_push(&types, part);
        }
        Ty const* ret = ty_union(me, types.data, types.len, nullptr, (TyList){0});
        ty_buf_drop(&types);
        return ret;
    }
    Ty const* apparent = ty_apparent_type(me, right_type);
    if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return nullptr;
    FoundProp prop = ty_find_prop(me, apparent, name);
    if (!prop.found || !(prop.type->flags & TYF_OBJECT)) return nullptr;
    Members const* members = ty_members(me, prop.type);
    if (members->call_count != 1) return nullptr;
    Signature const* sig = members->call[0];
    SigResolved const* sig_rs = signature_resolve(me, sig);
    if (sig_rs->type_params.len) {
        if (sig_rs->type_params.len != 1 || sig_rs->this_type != sig_rs->type_params.data[0]) return nullptr;
        sig = ty_signature_instantiation(me, sig, (TyList){ .data = &right_type, .len = 1 });
    }
    TyPredicate const* predicate = signature_predicate(me, sig);
    return predicate && !predicate->is_this && !predicate->asserts && predicate->param_index == 0 ? predicate->type : nullptr;
}

static Ty const* narrow_by_instanceof(FlowQuery* q, Ty const* type, BinaryOpExpr const* binary, bool assume_true) {
    Checker* me = q->me;
    Expr const* left = reference_candidate(&binary->left);
    if (!query_matches(q, left)) {
        if (assume_true && compiler_options()->strict_null_checks && chain_contains_reference(me, assignment_candidate(&binary->left), q->reference, true)) {
            return adjusted_type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL);
        }
        return type;
    }
    Ty const* right_type = ty_flow_expr_type(me, &binary->right);
    // (Without a lib, the empty object type: tsc's getGlobalType, which anything object derives from)
    Ty const* object = ty_global_object_type(me);
    Ty const* function = ty_global_function_type(me);
    if (!object) object = ty_empty_object_type(me);
    if (!function) function = ty_empty_object_type(me);
    if (!ty_is_type_derived_from(me, right_type, object)) return type;
    Atom const* has_instance = ty_well_known_symbol_name(me, "hasInstance");
    Ty const* predicate = has_instance ? has_instance_predicate_type(me, right_type, has_instance) : nullptr;
    if (predicate) return narrowed_type(me, type, predicate, assume_true, true);
    if (!ty_is_type_derived_from(me, right_type, function)) return type;
    Ty const* instance = map_type(me, right_type, constructor_instance_type, nullptr);
    if ((type->flags & TYF_ANY) && (instance == object || instance == function)) return type;
    if (!assume_true && !((instance->flags & TYF_OBJECT) && !is_empty_anonymous_object(me, instance))) return type;
    return narrowed_type(me, type, instance, assume_true, true);
}

typedef struct PresenceArg {
    Atom const* name;
    bool assume_true;
} PresenceArg;

// Whether every member of a union apparent type has the property or an index signature for it (tsc's getPropertyOfType
// of the union finds a synthetic property): 0 no, 1 yes, 2 yes but partial (a member's index signature has it,
// CheckFlags.WritePartial)
static int union_has_prop(Checker* me, Ty const* apparent, Atom const* name) {
    int ret = 1;
    for (u32 i = 0; i < apparent->composite.types.len; i++) {
        Ty const* member = ty_apparent_type(me, apparent->composite.types.data[i]);
        if (!(member->flags & (TYF_OBJECT | TYF_INTERSECTION))) return 0;
        if (ty_find_prop(me, member, name).found) continue;
        if (!prop_type_by_name(me, member, name)) return 0;
        ret = 2;
    }
    return ret;
}

static bool is_presence_possible(Checker* me, Ty const* member, void const* arg) {
    PresenceArg const* a = arg;
    Ty const* apparent = ty_apparent_type(me, member);
    // (A type parameter constrained to a union: the union's property)
    if (apparent->kind == TY_UNION && !(apparent->flags & TYF_BOOLEAN)) {
        int found = union_has_prop(me, apparent, a->name);
        return found == 2 || (found ? a->assume_true : !a->assume_true);
    }
    if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return !a->assume_true;
    FoundProp prop = ty_find_prop(me, apparent, a->name);
    if (prop.found) return (prop.flags & PROP_OPTIONAL) || a->assume_true;
    bool has_index = prop_type_by_name(me, apparent, a->name) != nullptr;
    return has_index || !a->assume_true;
}

// `'a' in x` and `x.hasOwnProperty('a')` say whether `x.a`'s missing is there (tsc's narrowTypeByBinaryExpression and
// narrowTypeByCallExpression with containsMissingType)
static Ty const* narrow_missing_by_presence(FlowQuery* q, Ty const* type, Expr const* object, Atom const* name, bool assume_true) {
    Checker* me = q->me;
    if (!name || !ty_contains_missing(me, type)) return type;
    Expr const* reference = skip_reference_wrappers(q->reference);
    if (reference->tag != EXPR_PROPERTY || !is_matching_reference(me, &reference->property->expr, object)) return type;
    if (access_name(me, reference->property) != name) return type;
    return type_with_facts(me, type, assume_true ? TF_NE_UNDEFINED : TF_EQ_UNDEFINED);
}

static Ty const* narrow_by_in(FlowQuery* q, Ty const* type, BinaryOpExpr const* binary, bool assume_true) {
    Checker* me = q->me;
    Expr const* target = reference_candidate(&binary->right);
    if (binary->left.tag != EXPR_PRIVATE_NAME && ty_contains_missing(me, type)) {
        bool number = false;
        Atom const* name = ty_property_name_of_key(me, ty_flow_expr_type(me, &binary->left), &number);
        Ty const* narrowed = narrow_missing_by_presence(q, type, target, name, assume_true);
        if (narrowed != type) return narrowed;
    }
    if (!query_matches(q, target)) return type;
    if (binary->left.tag == EXPR_PRIVATE_NAME) {
        // `#x in obj` says obj is an instance of the class that declares `#x` (its constructor for a static `#x`: tsc's
        // narrowTypeByPrivateIdentifierInInExpression)
        Ty const* owner = ty_private_name_owner(me, &binary->left, nullptr);
        return owner ? narrowed_type(me, type, owner, assume_true, true) : type;
    }
    bool is_number = false;
    Ty const* name_type = ty_regular(me, ty_flow_expr_type(me, &binary->left));
    Atom const* name = ty_property_name_of_key(me, name_type, &is_number);
    if (!name || (type->flags & TYF_ANY)) return type;
    bool is_known = false;
    TyList members = type->kind == TY_UNION ? type->composite.types : (TyList){ .data = &type, .len = 1 };
    for (u32 i = 0; i < members.len && !is_known; i++) {
        Ty const* apparent = ty_apparent_type(me, members.data[i]);
        is_known = apparent->kind == TY_UNION && !(apparent->flags & TYF_BOOLEAN) && members.data[i]->kind != TY_UNION ?
            union_has_prop(me, apparent, name) != 0 :
            (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) &&
            (ty_find_prop(me, apparent, name).found || prop_type_by_name(me, apparent, name));
    }
    if (!is_known) {
        // A property no member declares: `x & Record<"a", unknown>` (tsc's narrowTypeByInKeyword)
        if (!assume_true) return type;
        Symbol const* record = ty_global_type_alias_reported(me, c_sv("Record"), 2);
        if (!record) return type;
        Ty const* args[2] = { name_type, ty_intrinsic(me, TY_UNKNOWN_) };
        Ty const* parts[2] = { type, ty_alias_instance(me, record, (TyList){ .data = args, .len = 2 }) };
        return ty_intersection(me, parts, 2, nullptr, (TyList){0});
    }
    PresenceArg arg = { name, assume_true };
    return filter_type(me, type, is_presence_possible, &arg);
}

// `x.constructor` or `x["constructor"]` of the reference (tsc's isMatchingConstructorReference)
static bool is_matching_constructor_reference(FlowQuery* q, Expr const* expr) {
    Checker* me = q->me;
    expr = skip_reference_wrappers(expr);
    if (expr->tag != EXPR_PROPERTY) return false;
    PropertyExpr const* access = expr->property;
    if (property_private_class(access)) return false;
    // (An identifier reference's match asks for no type: first, it's cheaper than the name)
    if (q->identifier && !query_matches(q, &access->expr)) return false;
    bool named = (access->form != PROPERTY_EXPR_BRACKET ||
        (access->index.tag == EXPR_LITERAL && access->index.literal.tag == VALUE_STRING)) &&
        access_name(me, access) == my atoms.constructor;
    return named && query_matches(q, &access->expr);
}

static bool is_constructed_by(Checker* me, Ty const* source, void const* arg) {
    Ty const* target = arg;
    bool source_class = source->kind == TY_DECLARED && ty_declared_is_class(source);
    bool target_class = target->kind == TY_DECLARED && ty_declared_is_class(target);
    if (source_class || target_class) {
        // (tsc compares symbols: a class's `this` type has the class's)
        if (source->kind == TY_TYPE_PARAM && source->param.state->is_this && target_class) {
            Ty const* owner = ty_param_constraint_published(source->param.state); // (`this`'s, made with it)
            if (owner && owner->kind == TY_REFERENCE) owner = owner->reference.target;
            return owner && owner->kind == TY_DECLARED && owner->declared.symbol == target->declared.symbol;
        }
        return source_class && target_class && source->declared.symbol == target->declared.symbol;
    }
    return is_subtype(me, source, target);
}

// `x.constructor === C`: x is what C's prototype is (tsc's narrowTypeByConstructor)
static Ty const* narrow_by_constructor(FlowQuery* q, Ty const* type, BinaryOp op, Expr const* identifier, bool assume_true) {
    Checker* me = q->me;
    bool equals = op == BINARY_OP_DOUBLE_EQUAL || op == BINARY_OP_TRIPLE_EQUAL;
    if (equals != assume_true) return type;
    Ty const* identifier_type = ty_flow_expr_type(me, identifier);
    // A function (tsc's isFunctionType: an object type with call signatures) or a constructor (isConstructorType:
    // construct signatures, of an intersection too)
    bool function = (identifier_type->flags & TYF_OBJECT) && ty_members(me, identifier_type)->call_count;
    Ty const* apparent = ty_apparent_type(me, identifier_type);
    bool constructor = (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_members(me, apparent)->construct_count;
    if (!function && !constructor) return type;
    Ty const* candidate = ty_prop_type_of(me, identifier_type, my atoms.prototype);
    if (!candidate || (candidate->flags & TYF_ANY)) return type;
    if (candidate == ty_global_object_type(me) || candidate == ty_global_function_type(me)) return type;
    if (type->flags & TYF_ANY) return candidate;
    return filter_type(me, type, is_constructed_by, candidate);
}

static bool is_boolean_literal_expr(Expr const* expr) {
    return expr->tag == EXPR_LITERAL && expr->literal.tag == VALUE_BOOLEAN;
}

static Ty const* narrow_by_binary(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_true) {
    Checker* me = q->me;
    BinaryOpExpr const* binary = expr->binary_op;
    switch (binary->op) {
        case BINARY_OP_EQUAL:
        case BINARY_OP_OR_OR_EQUAL:
        case BINARY_OP_AND_AND_EQUAL:
        case BINARY_OP_EH_EH_EQUAL:
            return narrow_by_truthiness(q, narrow_type(q, type, &binary->right, assume_true, false), &binary->left, assume_true);
        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL: {
            Expr const* left = reference_candidate(&binary->left);
            Expr const* right = reference_candidate(&binary->right);
            if (is_typeof_expr(left) && is_string_literal_expr(right)) return narrow_by_typeof(q, type, left, binary->op, right, assume_true);
            if (is_typeof_expr(right) && is_string_literal_expr(left)) return narrow_by_typeof(q, type, right, binary->op, left, assume_true);
            if (query_matches(q, left)) return narrow_by_equality(q, type, binary->op, right, assume_true);
            if (query_matches(q, right)) return narrow_by_equality(q, type, binary->op, left, assume_true);
            if (compiler_options()->strict_null_checks) {
                if (chain_contains_reference(me, assignment_candidate(&binary->left), q->reference, true)) {
                    type = narrow_by_optional_chain_containment(q, type, binary->op, right, assume_true);
                } else if (chain_contains_reference(me, assignment_candidate(&binary->right), q->reference, true)) {
                    type = narrow_by_optional_chain_containment(q, type, binary->op, left, assume_true);
                }
            }
            DiscriminantAccess left_access = discriminant_access(q, left, type);
            DiscriminantAccess right_access = left_access.name ? (DiscriminantAccess){0} : discriminant_access(q, right, type);
            if (left_access.name || right_access.name) {
                EqualityPropArg arg = { binary->op, left_access.name ? right : left, assume_true };
                DiscriminantAccess access = left_access.name ? left_access : right_access;
                Ty const* by_key = narrow_by_key_equality(q, type, access, binary->op, arg.value, assume_true);
                if (by_key) return by_key;
                return narrow_by_discriminant(q, type, access, narrow_prop_by_equality, &arg);
            }
            if (is_matching_constructor_reference(q, left)) return narrow_by_constructor(q, type, binary->op, right, assume_true);
            if (is_matching_constructor_reference(q, right)) return narrow_by_constructor(q, type, binary->op, left, assume_true);
            // `isFoo(x) === true`
            bool equals = binary->op == BINARY_OP_DOUBLE_EQUAL || binary->op == BINARY_OP_TRIPLE_EQUAL;
            if (is_boolean_literal_expr(right) && left->tag != EXPR_PROPERTY) {
                return narrow_type(q, type, left, (assume_true == equals) == right->literal.boolean, false);
            }
            if (is_boolean_literal_expr(left) && right->tag != EXPR_PROPERTY) {
                return narrow_type(q, type, right, (assume_true == equals) == left->literal.boolean, false);
            }
            return type;
        }
        case BINARY_OP_INSTANCEOF:
            return narrow_by_instanceof(q, type, binary, assume_true);
        case BINARY_OP_IN:
            return narrow_by_in(q, type, binary, assume_true);
        case BINARY_OP_COMMA:
            return narrow_type(q, type, &binary->right, assume_true, false);
        // Broken down into conditions of their own by the builder, except inside something that isn't
        case BINARY_OP_AND_AND:
            return assume_true ?
                narrow_type(q, narrow_type(q, type, &binary->left, true, false), &binary->right, true, false) :
                union_of(me, narrow_type(q, type, &binary->left, false, false), narrow_type(q, type, &binary->right, false, false));
        case BINARY_OP_OR_OR:
            return assume_true ?
                union_of(me, narrow_type(q, type, &binary->left, true, false), narrow_type(q, type, &binary->right, true, false)) :
                narrow_type(q, narrow_type(q, type, &binary->left, false, false), &binary->right, false, false);
        default:
            return type;
    }
}

// The argument a predicate of the call is about: `this is T` is about the object the method is called on
static Expr const* predicate_argument(TyPredicate const* predicate, CallExpr const* call) {
    if (predicate->is_this) {
        Expr const* callee = &call->callable;
        if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
        callee = skip_reference_wrappers(callee);
        return callee->tag == EXPR_PROPERTY ? &callee->property->expr : nullptr;
    }
    if (predicate->param_index < 0 || predicate->param_index >= vec_expr_size(&call->args)) return nullptr;
    return vec_expr_at(&call->args, predicate->param_index);
}

typedef struct PredicatePropArg {
    Ty const* candidate;
    bool assume_true;
} PredicatePropArg;

static Ty const* narrow_prop_by_predicate(FlowQuery* q, Ty const* prop_type, void const* arg) {
    PredicatePropArg const* a = arg;
    return narrowed_type(q->me, prop_type, a->candidate, a->assume_true, false);
}

static bool every_member_is_nullable(Ty const* ty) {
    return every_member_has_flags(ty, TYF_NULLABLE, true);
}

static Ty const* narrow_by_predicate(FlowQuery* q, Ty const* type, TyPredicate const* predicate, CallExpr const* call, bool assume_true) {
    Checker* me = q->me;
    Ty const* candidate = predicate->type;
    if (!candidate) return type;
    if ((type->flags & TYF_ANY) && (candidate == ty_global_object_type(me) || candidate == ty_global_function_type(me))) return type;
    Expr const* argument = predicate_argument(predicate, call);
    if (!argument) return type;
    if (argument->tag == EXPR_SPREAD) return type;
    if (query_matches(q, argument)) return narrowed_type(me, type, candidate, assume_true, false);
    if (compiler_options()->strict_null_checks && optional_chain_contains_reference(me, argument, q->reference) &&
        (assume_true ? !has_type_facts(me, candidate, TF_EQ_UNDEFINED) : every_member_is_nullable(candidate))
    ) {
        type = adjusted_type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL);
    }
    // (A parenthesized argument is no access expression to tsc's getDiscriminantPropertyAccess: `assertA((x.kind))`)
    DiscriminantAccess access = predicate->is_this || !expr_is_perenthisized(argument) ?
        discriminant_access(q, argument, type) : (DiscriminantAccess){0};
    if (access.name) {
        PredicatePropArg arg = { candidate, assume_true };
        return narrow_by_discriminant(q, type, access, narrow_prop_by_predicate, &arg);
    }
    return type;
}

// Whether a call could say something about the reference: one of its arguments is it, or it is called on it
static bool has_matching_argument(FlowQuery* q, CallExpr const* call) {
    Checker* me = q->me;
    for_each (i, vec_expr, &call->args) {
        if (query_matches(q, i) || contains_matching_reference(me, q->reference, i) ||
            optional_chain_contains_reference(me, i, q->reference)
        ) {
            return true;
        }
    }
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    // (A method called on it: `o.m(x)` only, not `o[k](x)` nor `(o.m)(x)`, tsc's
    // `expression.expression.kind === SyntaxKind.PropertyAccessExpression`)
    if (callee->tag != EXPR_PROPERTY || callee->property->form != PROPERTY_EXPR_DOT || expr_is_perenthisized(callee)) return false;
    Expr const* object = &callee->property->expr;
    return query_matches(q, object) || contains_matching_reference(me, q->reference, object);
}

static Ty const* narrow_by_call(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_true) {
    CallExpr const* call = expr->call;
    if (call->uses_new) return type;
    // A type predicate about the reference first (tsc's narrowTypeByCallExpression): `this is H` of an own
    // `hasOwnProperty` decides, the property's presence isn't looked at then
    // (An optional call that didn't happen says nothing. A statement's, `x?.m(y);` met as the optional chain's
    // condition, goes by declared types like its call node: the flow type of the callee can be what is in flight,
    // openclaw's agent-tools.read.ts TS7022)
    if (has_matching_argument(q, call) && (assume_true || !expr_is_optional_chain(expr))) {
        CallEffects effects = call_effects(q->me, expr, expr->call->is_statement);
        if (effects.has_predicate && !effects.predicate.asserts) return narrow_by_predicate(q, type, &effects.predicate, call, assume_true);
    }
    // `x.hasOwnProperty("a")` says whether `x.a`'s missing is there: a property access called with a string literal,
    // neither parenthesized (tsc's IsPropertyAccessExpression, IsStringLiteralLike; the binder makes no condition of
    // `(x.hasOwnProperty)("a")`)
    if (ty_contains_missing(q->me, type) && call->callable.tag == EXPR_PROPERTY && !expr_is_perenthisized(&call->callable) &&
        call->callable.property->form == PROPERTY_EXPR_DOT && vec_expr_size(&call->args) == 1 &&
        vec_expr_at(&call->args, 0)->tag == EXPR_LITERAL && vec_expr_at(&call->args, 0)->literal.tag == VALUE_STRING &&
        !expr_is_perenthisized(vec_expr_at(&call->args, 0))
    ) {
        Atom const* method = access_name(q->me, call->callable.property);
        if (method && csview_equals(atom_sv(method), "hasOwnProperty")) {
            Ty const* key = ty_flow_expr_type(q->me, vec_expr_at(&call->args, 0));
            bool number = false;
            Atom const* name = ty_property_name_of_key(q->me, key, &number);
            return narrow_missing_by_presence(q, type, reference_candidate(&call->callable.property->expr), name, assume_true);
        }
    }
    return type;
}

// What is known after `assert(expr)` returned
static Ty const* narrow_by_assertion(FlowQuery* q, Ty const* type, Expr const* expr) {
    Checker* me = q->me;
    if (is_boolean_literal_expr(expr) && !expr->literal.boolean) return ty_intrinsic(me, TY_UNREACHABLE_NEVER);
    if (expr->tag == EXPR_BINARY_OP && expr->binary_op->op == BINARY_OP_AND_AND) {
        return narrow_by_assertion(q, narrow_by_assertion(q, type, &expr->binary_op->left), &expr->binary_op->right);
    }
    if (expr->tag == EXPR_BINARY_OP && expr->binary_op->op == BINARY_OP_OR_OR) {
        return union_of(me, narrow_by_assertion(q, type, &expr->binary_op->left), narrow_by_assertion(q, type, &expr->binary_op->right));
    }
    return narrow_type(q, type, expr, true, false);
}

// tsc's isConstantReference of an identifier: a const variable, or a parameter, catch variable or local `let` that is
// never assigned, or a named function expression's own name. Not a `var`, an exported or global `let`, nor a function,
// class or import: `const ok = typeof window === "object"; if (ok) return; window.x` leaves `window` alone
static bool symbol_is_constant_variable(Symbol const* symbol) {
    if (symbol->is_const) return true;
    if (symbol_assignments(symbol)->is_assigned) return false;
    return symbol->is_parameter || (symbol->is_local && (symbol->flags & SYMBOL_BLOCK_SCOPED_VARIABLE));
}

// The declared type of a constant reference (see below), null if it isn't one
static Ty const* constant_reference_type(Checker* me, Expr const* reference, u32 depth) {
    if (depth > 8) return nullptr;
    reference = skip_reference_wrappers(reference);
    // (The whole reference's own type isn't needed, only its object's: asking for it would make `if (c)` in a
    // constructor circular for `this.x`, whose declared type is the constructor's flow type of it)
    Ty const* constant = ty_intrinsic(me, TY_UNKNOWN_);
    if (reference->tag == EXPR_IDENTIFIER) {
        if (reference->identifier.keyword == KEYWORD_IDENTIFIER_THIS) {
            return depth ? ty_identifier_reference_type(me, &reference->identifier) : constant;
        }
        Symbol const* symbol = reference->identifier.target;
        if (reference->identifier.keyword || !symbol || !symbol_is_constant_variable(symbol)) return nullptr;
        // (The object of a property is its type there, narrowed: tsc reads the symbol the access resolved to, on the
        // narrowed type, `isVarDecl(node)` then `node.name` is VariableDeclaration's readonly name)
        return depth ? ty_expr_type(me, reference) : constant;
    }
    if (reference->tag != EXPR_PROPERTY) return nullptr;
    if (ty_is_import_meta(reference)) return ty_import_meta_expr_type(me, reference);
    Ty const* object = constant_reference_type(me, &reference->property->expr, depth + 1);
    Atom const* name = object ? access_name(me, reference->property) : nullptr;
    if (!name) return nullptr;
    // A property of a union is readonly if it is in some member
    object = ty_apparent_type(me, object);
    TyList parts = object->kind == TY_UNION ? object->composite.types : (TyList){ .data = &object, .len = 1 };
    bool readonly = false;
    for (u32 i = 0; i < parts.len && !readonly; i++) {
        Ty const* part = ty_apparent_type(me, parts.data[i]);
        if (part->flags & TYF_OBJECT) {
            Prop const* prop = ty_get_prop(me, part, name);
            if (prop) {
                readonly = (prop->flags & PROP_READONLY) != 0;
                continue;
            }
        }
        FoundProp prop = ty_find_prop(me, part, name);
        readonly = prop.found && (prop.flags & PROP_READONLY);
    }
    if (!readonly) return nullptr;
    if (!depth) return constant;
    Ty const* type = ty_access(me, object, ty_prop_name_literal(me, name));
    return type ? type : ty_intrinsic(me, TY_UNKNOWN_);
}

// A reference that stays what it is (tsc's isConstantReference): `this`, a const variable, a parameter or `let` that
// is never assigned, or a readonly property of one of those
static bool is_constant_reference(Checker* me, Expr const* reference) {
    return constant_reference_type(me, reference, 0) != nullptr;
}

// `const ok = typeof x === "string"; if (ok) x`: the initializer of a const variable without a type annotation that
// a condition consists of, if the reference can be narrowed through it (tsc inlines up to five levels)
static Expr const* aliased_condition(FlowQuery* q, Expr const* condition) {
    if (q->me->flow_inline_level >= 5 || query_matches(q, condition)) return nullptr;
    Symbol const* symbol = condition->identifier.target;
    if (!symbol || !symbol->is_const || symbol->decl_count != 1 || symbol->decls[0].kind != SYMBOL_DECL_VARIABLE) return nullptr;
    Binding const* binding = symbol->decls[0].node;
    if (binding->parent || binding->has_explicit_type || !binding->source || binding->source->tag != LAZY_TYPE_INITIALIZED_BINDING) {
        return nullptr;
    }
    InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
    // (A pattern pseudo reference is constant: ty_destructured_narrowed_type() only asks for one that is)
    if (initialized_binding_value(initialized)->tag == EXPR_IMPLICIT_ANY ||
        (!q->pattern && !q->parameters_of && !is_constant_reference(q->me, q->reference))
    ) {
        return nullptr;
    }
    return initialized_binding_value(initialized);
}

static Ty const* narrow_type(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_true, bool nullish) {
    if (nullish) return narrow_by_optionality(q, type, expr, assume_true);
    switch (expr->tag) {
        case EXPR_IDENTIFIER: {
            if (expr->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT) return type;
            Expr const* aliased = aliased_condition(q, expr);
            if (aliased) {
                q->me->flow_inline_level++;
                Ty const* ret = narrow_type(q, type, aliased, assume_true, false);
                q->me->flow_inline_level--;
                return ret;
            }
            return narrow_by_truthiness(q, type, expr, assume_true);
        }
        case EXPR_PROPERTY:
            return narrow_by_truthiness(q, type, expr, assume_true);
        case EXPR_CALL:
            return narrow_by_call(q, type, expr, assume_true);
        case EXPR_AS_TYPE:
            return expr->as_type->is_satisfies ? narrow_type(q, type, &expr->as_type->expr, assume_true, false) : type;
        case EXPR_BINARY_OP:
            return narrow_by_binary(q, type, expr, assume_true);
        case EXPR_UNARY_OP:
            if (expr->unary_op->op == UNARY_OP_RIGHT_BANG) return narrow_type(q, type, &expr->unary_op->expr, assume_true, false);
            if (expr->unary_op->op == UNARY_OP_LEFT_BANG) return narrow_type(q, type, &expr->unary_op->expr, !assume_true, false);
            return type;
        default:
            return type;
    }
}

// Switch statements

static SwitchCase const* switch_case_at(SwitchStatement const* switch_, u32 i) {
    return vec_switch_case_at(&switch_->cases, i);
}

static u32 switch_case_count(SwitchStatement const* switch_) {
    return (u32)vec_switch_case_size(&switch_->cases);
}

// The (regular) type of every case expression, never for `default`
static void switch_clause_types(Checker* me, SwitchStatement const* switch_, TyBuf* out) {
    for_each (i, vec_switch_case, &switch_->cases) {
        ty_buf_push(out, i->is_pattern ? ty_regular(me, ty_flow_expr_type(me, &i->pattern)) : ty_intrinsic(me, TY_NEVER));
    }
}

static bool buf_contains(TyBuf const* buf, Ty const* ty) {
    for (u32 i = 0; i < buf->len; i++) {
        if (buf->data[i] == ty) return true;
    }
    return false;
}

static bool is_not_case_unit(Checker* me, Ty const* member, void const* arg) {
    TyBuf const* switch_types = arg;
    Ty const* unit = ty_regular(me, member);
    if (unit->flags & TYF_UNDEFINED) unit = ty_intrinsic(me, TY_UNDEFINED); // (missing is undefined here)
    if (member->kind == TY_INTERSECTION) {
        // `"a" & Tag`
        for (u32 i = 0; i < member->composite.types.len; i++) {
            if (is_unit_type(member->composite.types.data[i])) unit = member->composite.types.data[i];
        }
    }
    // (A computed enum is a unit type to tsc's isUnitLikeType, comparable to every number case)
    if (!is_unit_type(unit) && !(unit->kind != TY_UNION && (unit->flags & TYF_ENUM))) return true;
    if (buf_contains(switch_types, unit)) return false;
    // (A case whose unit type is comparable to it covers it too, `case E.cat` for "cat": tsc 6.0)
    for (u32 i = 0; i < switch_types->len; i++) {
        Ty const* t = switch_types->data[i];
        if (is_unit_type(t) && ty_is_comparable_either_way(me, t, unit)) return false;
    }
    return true;
}

static Ty const* narrow_by_switch_on_discriminant(FlowQuery* q, Ty const* type, FlowSwitchClause const* clause) {
    Checker* me = q->me;
    TyBuf switch_types = {0};
    switch_clause_types(me, clause->switch_, &switch_types);
    if (!switch_types.len) return type;
    bool has_default = clause->clause_start == clause->clause_end;
    for (u32 i = clause->clause_start; i < clause->clause_end; i++) has_default |= (switch_types.data[i]->flags & TYF_NEVER) != 0;
    // `unknown` without a default: the cases' types, an object one as `object` (tsc's narrowTypeBySwitchOnDiscriminant)
    if ((type->flags & TYF_UNKNOWN) && !has_default) {
        u32 len = clause->clause_end - clause->clause_start;
        Ty const** ground = malloc(sizeof(Ty const*) * (len ? len : 1));
        panic_if(!ground, "out of memory");
        bool keep = true;
        for (u32 i = 0; i < len && keep; i++) {
            Ty const* t = switch_types.data[clause->clause_start + i];
            bool primitive = (t->kind == TY_INTRINSIC && !(t->flags & (TYF_ANY | TYF_UNKNOWN))) || t->kind == TY_LITERAL ||
                (t->flags & TYF_BOOLEAN) || ty_is_enum_union(t);
            if (primitive) ground[i] = t;
            else if (t->flags & TYF_OBJECT) ground[i] = ty_intrinsic(me, TY_NON_PRIMITIVE);
            else keep = false;
        }
        Ty const* ret = keep ? ty_union(me, ground, len, nullptr, (TyList){0}) : type;
        free(ground);
        ty_buf_drop(&switch_types);
        return ret;
    }
    Ty const* discriminant = ty_union(me, switch_types.data + clause->clause_start, clause->clause_end - clause->clause_start, nullptr, (TyList){0});
    Ty const* case_type = discriminant->flags & TYF_NEVER ? discriminant :
        replace_primitives_with_literals(me, filter_type(me, type, is_comparable_to, discriminant), discriminant);
    Ty const* ret = case_type;
    if (has_default) {
        Ty const* default_type = filter_type(me, type, is_not_case_unit, &switch_types);
        ret = case_type->flags & TYF_NEVER ? default_type : union_of(me, case_type, default_type);
    }
    ty_buf_drop(&switch_types);
    return ret;
}

// `switch (x.kind)` with kind the key property of x's union: the members for the clause's cases (tsc's
// narrowTypeBySwitchOnDiscriminantProperty). Null if some case has no member of its own
static Ty const* narrow_by_key_switch(FlowQuery* q, Ty const* type, DiscriminantAccess access, FlowSwitchClause const* clause) {
    Checker* me = q->me;
    if (clause->clause_start >= clause->clause_end) return nullptr;
    Atom const* key = ty_union_key_property(me, type);
    if (!key || key != access.name) return nullptr;
    TyBuf switch_types = {0};
    switch_clause_types(me, clause->switch_, &switch_types);
    TyBuf candidates = {0};
    bool all = true;
    for (u32 i = clause->clause_start; i < clause->clause_end && all; i++) {
        Ty const* candidate = ty_union_member_for_key(me, type, switch_types.data[i]);
        if (candidate) ty_buf_push(&candidates, candidate);
        else all = false;
    }
    Ty const* ret = all ? ty_union(me, candidates.data, candidates.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&candidates);
    ty_buf_drop(&switch_types);
    return ret;
}

static Ty const* narrow_prop_by_switch(FlowQuery* q, Ty const* prop_type, void const* arg) {
    return narrow_by_switch_on_discriminant(q, prop_type, arg);
}

// The string of every case of a `switch (typeof x)`, false if some case isn't a string literal
static bool typeof_switch_has_witnesses(SwitchStatement const* switch_) {
    for_each (i, vec_switch_case, &switch_->cases) {
        if (i->is_pattern && !is_string_literal_expr(&i->pattern)) return false;
    }
    return true;
}

// A case whose string an earlier case has: no witness (tsc's getSwitchClauseTypeOfWitnesses)
static bool typeof_case_is_duplicate(SwitchStatement const* switch_, u32 index) {
    SwitchCase const* c = switch_case_at(switch_, index);
    if (!c->is_pattern) return false;
    for (u32 i = 0; i < index; i++) {
        SwitchCase const* other = switch_case_at(switch_, i);
        if (other->is_pattern && cstr_eq(&other->pattern.literal.string, &c->pattern.literal.string)) return true;
    }
    return false;
}

// What is known when none of the cases outside of [start, end) matched
static u32 typeof_switch_not_equal_facts(SwitchStatement const* switch_, u32 start, u32 end) {
    u32 facts = 0;
    for_each (i, vec_switch_case, &switch_->cases) {
        if (i->is_pattern && ((u32)i_index < start || (u32)i_index >= end) && !typeof_case_is_duplicate(switch_, (u32)i_index)) {
            facts |= typeof_ne_facts(cstr_sv(&i->pattern.literal.string));
        }
    }
    return facts;
}

static bool has_all_facts(Checker* me, Ty const* member, void const* arg) {
    u32 facts = *(u32 const*)arg;
    return (type_facts(me, member, facts) & facts) == facts;
}

static Ty const* narrow_by_switch_on_typeof(FlowQuery* q, Ty const* type, FlowSwitchClause const* clause) {
    Checker* me = q->me;
    SwitchStatement const* switch_ = clause->switch_;
    if (!typeof_switch_has_witnesses(switch_)) return type;
    bool has_default = clause->clause_start == clause->clause_end;
    for (u32 i = clause->clause_start; i < clause->clause_end; i++) has_default |= !switch_case_at(switch_, i)->is_pattern;
    if (has_default) {
        u32 facts = typeof_switch_not_equal_facts(switch_, clause->clause_start, clause->clause_end);
        return filter_type(me, type, has_all_facts, &facts);
    }
    TyBuf parts = {0};
    for (u32 i = clause->clause_start; i < clause->clause_end; i++) {
        // (A repeated case can't be reached by its string: never)
        ty_buf_push(&parts, typeof_case_is_duplicate(switch_, i) ? ty_intrinsic(me, TY_NEVER) :
            narrow_by_type_name(me, type, cstr_sv(&switch_case_at(switch_, i)->pattern.literal.string)));
    }
    Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

static Ty const* narrow_by_switch_on_true(FlowQuery* q, Ty const* type, FlowSwitchClause const* clause) {
    Checker* me = q->me;
    SwitchStatement const* switch_ = clause->switch_;
    u32 count = switch_case_count(switch_);
    bool has_default = clause->clause_start == clause->clause_end;
    for (u32 i = clause->clause_start; i < clause->clause_end; i++) has_default |= !switch_case_at(switch_, i)->is_pattern;
    // The cases before didn't match
    for (u32 i = 0; i < clause->clause_start; i++) {
        SwitchCase const* c = switch_case_at(switch_, i);
        if (c->is_pattern) type = narrow_type(q, type, &c->pattern, false, false);
    }
    if (has_default) {
        for (u32 i = clause->clause_end; i < count; i++) {
            SwitchCase const* c = switch_case_at(switch_, i);
            if (c->is_pattern) type = narrow_type(q, type, &c->pattern, false, false);
        }
        return type;
    }
    TyBuf parts = {0};
    for (u32 i = clause->clause_start; i < clause->clause_end; i++) {
        ty_buf_push(&parts, narrow_type(q, type, &switch_case_at(switch_, i)->pattern, true, false));
    }
    Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

static FlowType type_at_switch_clause(FlowQuery* q, FlowNode const* flow) {
    Checker* me = q->me;
    FlowSwitchClause const* clause = flow->node;
    Expr const* needle = skip_reference_wrappers(&clause->switch_->needle);
    FlowType before = type_at_flow_node(q, flow->antecedent);
    // (tsc narrows an evolving array as it is, an object type without members: as its final array type here, and
    // what isn't narrowed stays evolving)
    Ty const* type = final_array_type(me, before.type);
    if (query_matches(q, needle)) {
        type = narrow_by_switch_on_discriminant(q, type, clause);
    } else if (is_typeof_expr(needle) && query_matches(q, &needle->unary_op->expr)) {
        type = narrow_by_switch_on_typeof(q, type, clause);
    } else if (needle->tag == EXPR_LITERAL && needle->literal.tag == VALUE_BOOLEAN && needle->literal.boolean) {
        type = narrow_by_switch_on_true(q, type, clause);
    } else {
        // `switch (x?.kind)`: in clauses whose cases are all something other than undefined, x isn't nullish (tsc's
        // narrowTypeBySwitchOptionalChainContainment)
        // `switch (typeof x?.kind)` likewise, in clauses whose cases are all something other than "undefined"
        bool contains = false, of_typeof = false;
        if (compiler_options()->strict_null_checks && clause->clause_start != clause->clause_end) {
            contains = chain_contains_reference(me, &clause->switch_->needle, q->reference, true);
            of_typeof = !contains && is_typeof_expr(needle) &&
                optional_chain_contains_reference(me, &needle->unary_op->expr, q->reference);
        }
        if (contains || of_typeof) {
            TyBuf switch_types = {0};
            switch_clause_types(me, clause->switch_, &switch_types);
            bool every = true;
            for (u32 i = clause->clause_start; i < clause->clause_end; i++) {
                Ty const* case_type = switch_types.data[i];
                every &= of_typeof ? !(case_type->flags & TYF_NEVER) && !((case_type->flags & TYF_STRING_LITERAL) &&
                    case_type->literal.string && csview_equals(atom_sv(case_type->literal.string), "undefined")) :
                    !(case_type->flags & (TYF_UNDEFINED | TYF_NEVER));
            }
            ty_buf_drop(&switch_types);
            if (every) type = type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL);
        }
        DiscriminantAccess access = discriminant_access(q, needle, type);
        Ty const* by_key = access.name ? narrow_by_key_switch(q, type, access, clause) : nullptr;
        if (by_key) type = by_key;
        else if (access.name) type = narrow_by_discriminant(q, type, access, narrow_prop_by_switch, clause);
    }
    if (type == final_array_type(me, before.type)) return before;
    return new_flow_type(q, type, before.incomplete);
}

// tsc's isExhaustiveSwitchStatement(): the cases cover everything the needle can be
static bool is_exhaustive_switch(Checker* me, SwitchStatement const* switch_) {
    u64 key[2] = { KEY_FLOW_EXHAUSTIVE, (u64)(uintptr_t)switch_ };
    uintptr_t cached = (uintptr_t)intern_get(&my links, key, 2);
    if (cached) return cached == 2; // (3, being worked out: a circularity, which tsc answers with "no" too)
    void const** slot = intern_slot(me, &my links, key, 2);
    *slot = (void const*)(uintptr_t)3;
    bool ret = false;
    Expr const* needle = skip_reference_wrappers(&switch_->needle);
    if (is_typeof_expr(needle)) {
        if (typeof_switch_has_witnesses(switch_)) {
            Ty const* operand = non_nullable_constraint_or(me, ty_flow_expr_type(me, &needle->unary_op->expr));
            // (A union's base constraint is its members': `X | Y` of type parameters)
            if (operand->kind == TY_UNION && (operand->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
                TyBuf parts = {0};
                for (u32 i = 0; i < operand->composite.types.len; i++) {
                    ty_buf_push(&parts, non_nullable_constraint_or(me, operand->composite.types.data[i]));
                }
                operand = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
                ty_buf_drop(&parts);
            }
            u32 facts = typeof_switch_not_equal_facts(switch_, 0, 0);
            if (operand->flags & (TYF_ANY | TYF_UNKNOWN)) {
                // (tsc's AllTypeofNE: the seven names and "undefined")
                u32 all = (TF_ALL_TYPEOF_NE & ~TF_TYPEOF_NE_HOST_OBJECT) | TF_NE_UNDEFINED;
                ret = (facts & all) == all;
            } else {
                ret = (filter_type(me, operand, has_all_facts, &facts)->flags & TYF_NEVER) != 0;
            }
        }
    } else {
        // (Of a type parameter its constraint's: tsgo 7.1's getBaseConstraintOrType, `T extends keyof X`)
        // (Of the expression as written: `switch (kind!)` is over the non-null type, tsc's checkExpressionCached of
        // node.Expression())
        Ty const* type = ty_flow_expr_type(me, &switch_->needle);
        if (type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
            if (type->kind == TY_UNION) {
                TyBuf parts = {0};
                for (u32 i = 0; i < type->composite.types.len; i++) {
                    ty_buf_push(&parts, non_nullable_constraint_or(me, type->composite.types.data[i]));
                }
                type = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
                ty_buf_drop(&parts);
            } else {
                type = non_nullable_constraint_or(me, type);
            }
        }
        type = ty_regular(me, type);
        if (is_literal_type(type) || (type->flags & TYF_BOOLEAN)) {
            TyBuf switch_types = {0};
            switch_clause_types(me, switch_, &switch_types);
            ret = switch_types.len > 0;
            for (u32 i = 0; i < switch_types.len && ret; i++) {
                ret = is_unit_type(switch_types.data[i]) || (switch_types.data[i]->flags & TYF_NEVER);
            }
            TyList members = type->kind == TY_UNION ? type->composite.types : (TyList){ .data = &type, .len = 1 };
            for (u32 i = 0; i < members.len && ret; i++) ret = buf_contains(&switch_types, members.data[i]);
            ty_buf_drop(&switch_types);
        }
    }
    *intern_slot(me, &my links, key, 2) = (void const*)(uintptr_t)(ret ? 2 : 1);
    return ret;
}

static FlowType type_at_flow_condition(FlowQuery* q, FlowNode const* flow) {
    FlowType before = type_at_flow_node(q, flow->antecedent);
    if (before.type->flags & TYF_NEVER) return before;
    bool assume_true = (flow->flags & FLOW_TRUE_CONDITION) != 0;
    Ty const* non_evolving = final_array_type(q->me, before.type);
    Ty const* narrowed = narrow_type(q, non_evolving, flow->node, assume_true, (flow->flags & FLOW_NULLISH) != 0);
    if (narrowed == non_evolving) return before;
    // (A guard that leaves nothing of an incomplete type is the silent never: new_flow_type())
    return new_flow_type(q, narrowed, before.incomplete);
}

// Labels

static bool is_type_subset_of(Ty const* source, Ty const* target) {
    if (source == target || (source->flags & TYF_NEVER)) return true;
    if (target->kind != TY_UNION) return false;
    TyList members = target->composite.types;
    if (source->kind == TY_UNION) {
        // Both sorted by id
        u32 j = 0;
        for (u32 i = 0; i < source->composite.types.len; i++) {
            while (j < members.len && members.data[j] != source->composite.types.data[i]) j++;
            if (j == members.len) return false;
        }
        return true;
    }
    for (u32 i = 0; i < members.len; i++) {
        if (members.data[i] == source) return true;
    }
    return false;
}

// is_type_subset_of() for the query's own types, remembered when the target union is big
static bool query_subset_of(FlowQuery* q, Ty const* source, Ty const* target) {
    if (source == target || target->kind != TY_UNION || target->composite.types.len < 16 || source->kind != TY_UNION) {
        return is_type_subset_of(source, target);
    }
    u32 slot = target == q->initial;
    if (q->subset_memo[slot].source == source && q->subset_memo[slot].target == target) return q->subset_memo[slot].result;
    bool result = is_type_subset_of(source, target);
    q->subset_memo[slot].source = source;
    q->subset_memo[slot].target = target;
    q->subset_memo[slot].result = result;
    return result;
}

static void push_if_unique(TyBuf* buf, Ty const* ty) {
    for (u32 i = 0; i < buf->len; i++) {
        if (buf->data[i] == ty) return;
    }
    ty_buf_push(buf, ty);
}

static Ty const* union_at_label(FlowQuery* q, TyBuf const* types, bool subtype_reduction) {
    Checker* me = q->me;
    Ty const* result = is_auto(me, q->declared) ? union_or_evolving(me, types->data, types->len, subtype_reduction) :
        recombine_unknown(me, subtype_reduction ? ty_union_subtype_reduced(me, types->data, types->len) :
            ty_union(me, types->data, types->len, nullptr, (TyList){0}));
    // The same union as the declared type goes by the declared type's name
    Ty const* declared = q->declared;
    if (result != declared && result->kind == TY_UNION && declared->kind == TY_UNION &&
        result->composite.types.len == declared->composite.types.len &&
        !memcmp(result->composite.types.data, declared->composite.types.data, sizeof(Ty const*) * result->composite.types.len)
    ) {
        return declared;
    }
    return result;
}

static FlowType type_at_branch_label(FlowQuery* q, FlowList const* antecedents) {
    TyBuf types = {0};
    bool subtype_reduction = false;
    bool seen_incomplete = false;
    FlowType ret;
    FlowNode const* bypass = nullptr;
    for (u32 i = 0; i < antecedents->count; i++) {
        FlowNode const* antecedent = antecedents->items[i];
        if (!bypass && (antecedent->flags & FLOW_SWITCH_CLAUSE)) {
            // "No case matched" of a switch that may be exhaustive: looked at last, and only if it could add something
            FlowSwitchClause const* clause = antecedent->node;
            if (clause->clause_start == clause->clause_end) {
                bypass = antecedent;
                continue;
            }
        }
        FlowType branch = type_at_flow_node(q, antecedent);
        // Every other branch can only add subtypes of the declared type
        if (branch.type == q->declared && q->declared == q->initial) {
            ty_buf_drop(&types);
            return (FlowType){ branch.type, false };
        }
        push_if_unique(&types, branch.type);
        if (!query_subset_of(q, branch.type, q->initial)) subtype_reduction = true;
        seen_incomplete |= branch.incomplete;
    }
    if (bypass) {
        FlowType branch = type_at_flow_node(q, bypass);
        FlowSwitchClause const* clause = bypass->node;
        if (!(branch.type->flags & TYF_NEVER) && !buf_contains(&types, branch.type) && !is_exhaustive_switch(q->me, clause->switch_)) {
            if (branch.type == q->declared && q->declared == q->initial) {
                ty_buf_drop(&types);
                return (FlowType){ branch.type, false };
            }
            ty_buf_push(&types, branch.type);
            if (!query_subset_of(q, branch.type, q->initial)) subtype_reduction = true;
            seen_incomplete |= branch.incomplete;
        }
    }
    ret = new_flow_type(q, union_at_label(q, &types, subtype_reduction), seen_incomplete);
    ty_buf_drop(&types);
    return ret;
}

static void loop_stack_push(Checker* me, FlowNode const* flow, u32 key, TyBuf* types) {
    if (my flow_loop_count == my flow_loop_cap) {
        my flow_loop_cap = my flow_loop_cap ? my flow_loop_cap * 2 : 16;
        my flow_loops = realloc(my flow_loops, sizeof(*my flow_loops) * my flow_loop_cap);
        panic_if(!my flow_loops, "out of memory");
    }
    my flow_loops[my flow_loop_count++] = (FlowLoopEntry){ flow, key, types };
}

static FlowType type_at_loop_label(FlowQuery* q, FlowNode const* flow, FlowList const* antecedents) {
    Checker* me = q->me;
    u32 key = query_key(q);
    if (!key) return (FlowType){ q->declared, false };
    u64 cache_key[3] = { KEY_FLOW_LOOP, (u64)(uintptr_t)flow, key };
    Ty const* cached = intern_get(&my links, cache_key, 3);
    if (cached) return (FlowType){ cached, false };
    // Already being worked on (we got here through a back edge): the union of what the branches gave so far,
    // incomplete. An entry without types is an inner loop that the back edge of an outer loop reached: it starts over
    // and terminates because the outer loop is in progress and its first antecedent never loops
    for (u32 i = my flow_loop_start; i < my flow_loop_count; i++) {
        FlowLoopEntry const* entry = &my flow_loops[i];
        if (entry->flow == flow && entry->key == key && entry->types->len) {
            Ty const* so_far = is_auto(me, q->declared) ? union_or_evolving(me, entry->types->data, entry->types->len, false) :
                recombine_unknown(me, ty_union(me, entry->types->data, entry->types->len, nullptr, (TyList){0}));
            return new_flow_type(q, so_far, true);
        }
    }
    TyBuf types = {0};
    bool subtype_reduction = false;
    FlowType first = {0};
    for (u32 i = 0; i < antecedents->count; i++) {
        FlowType branch;
        if (i == 0) {
            // The way into the loop
            branch = first = type_at_flow_node(q, antecedents->items[i]);
        } else {
            loop_stack_push(me, flow, key, &types);
            u32 saved_generation = my flow_expr_generation;
            my flow_expr_generation = ++my flow_expr_generations;
            branch = type_at_flow_node(q, antecedents->items[i]);
            my flow_expr_generation = saved_generation;
            my flow_loop_count--;
            // A self-contained evaluation further down (a declaration) may have finished this loop already
            cached = intern_get(&my links, cache_key, 3);
            if (cached) {
                ty_buf_drop(&types);
                return (FlowType){ cached, false };
            }
        }
        push_if_unique(&types, branch.type);
        if (!query_subset_of(q, branch.type, q->initial)) subtype_reduction = true;
        if (branch.type == q->declared) break; // It can't get any wider
    }
    Ty const* result = union_at_label(q, &types, subtype_reduction);
    ty_buf_drop(&types);
    if (first.incomplete) return new_flow_type(q, result, true);
    *intern_slot(me, &my links, cache_key, 3) = result;
    return (FlowType){ result, false };
}

// A local `let x: T;` that nothing ever assigns (tsc's isNeverInitialized): a closure doesn't get to assume it is
bool ty_symbol_never_initialized(Symbol const* symbol) {
    if (!symbol || !symbol->is_local || symbol->is_const || symbol_assignments(symbol)->is_definitely_assigned || !(symbol->flags & SYMBOL_BLOCK_SCOPED_VARIABLE)) {
        return false;
    }
    Binding const* binding = symbol_find_node(symbol, SYMBOL_DECL_VARIABLE);
    if (!binding || binding->parent || binding->definitely_assigned || !binding->source) return false;
    if (binding->source->tag != LAZY_TYPE_INITIALIZED_BINDING) return false;
    InitializedBinding const* declaration = c_container_of(binding->source, InitializedBinding, lazy_type);
    return initialized_binding_value(declaration)->tag == EXPR_IMPLICIT_ANY;
}

// Whether narrowing of the reference survives the function boundary: a function expression sees the narrowed type
// of what can't have changed by the time it runs (tsc's checkIdentifier())
// tsc's convertAutoToAny
static Ty const* auto_to_any(Checker* me, Ty const* type) {
    if (type == ty_intrinsic(me, TY_AUTO)) return ty_intrinsic(me, TY_ANY);
    if (type == ty_auto_array(me)) return ty_array(me, ty_intrinsic(me, TY_ANY), false);
    return type;
}

static bool continues_outside(FlowQuery* q, FlowNode const* start, FunctionImpl const* function) {
    // (A destructuring pattern or a function's parameters as a pseudo reference: tsc's getNarrowedTypeOfSymbol asks
    // getFlowTypeOfReference without a flow container, so any function expression's start goes on outside)
    if (q->pattern || q->parameters_of) return true;
    Expr const* reference = skip_reference_wrappers(q->reference);
    // A qualified name of a `typeof a.b` type is no property access for tsc's getTypeAtFlowNode: its walk always goes
    // on outside (narrowed like the code around the function expression it's in)
    if (reference->tag == EXPR_PROPERTY && reference->property->qualified_name) return true;
    if (reference->tag != EXPR_IDENTIFIER) return false;
    if (reference->identifier.keyword == KEYWORD_IDENTIFIER_THIS) return function_impl_is_arrow(function);
    // An async or generator IIFE has a start of its own, but tsc's getControlFlowContainer skips it
    if (function->is_iife) return true;
    Symbol const* symbol = q->symbol;
    if (!symbol || symbol->flow_start == start) return false;
    // (Not a `const x = []`: what a closure sees of it is `any[]`, whatever is pushed after, tsc's checkIdentifier)
    if (symbol->is_const) return q->declared != q->me->auto_array;
    bool is_mutable_local = symbol->is_local && (symbol->flags & SYMBOL_BLOCK_SCOPED_VARIABLE);
    if (!symbol->is_parameter && !is_mutable_local) return false;
    // Past the last assignment
    char const* last_assignment_end = symbol_assignments(symbol)->last_assignment_end;
    return !last_assignment_end || last_assignment_end < span_start_ptr(reference->inner_span);
}

// A call statement: an assertion narrows, after a call that never returns there is nothing. False: no effect
static bool type_at_flow_call(FlowQuery* q, FlowNode const* flow, FlowType* out) {
    Checker* me = q->me;
    Expr const* expr = flow->node;
    CallEffects effects = call_effects(me, expr, true);
    if (effects.has_predicate && effects.predicate.asserts) {
        TyPredicate const* predicate = &effects.predicate;
        FlowType before = type_at_flow_node(q, flow->antecedent);
        Ty const* type = final_array_type(me, before.type);
        Ty const* narrowed = type;
        if (predicate->type) {
            narrowed = narrow_by_predicate(q, type, predicate, expr->call, true);
        } else if (!predicate->is_this && predicate->param_index >= 0 && predicate->param_index < vec_expr_size(&expr->call->args)) {
            narrowed = narrow_by_assertion(q, type, vec_expr_at(&expr->call->args, predicate->param_index));
        }
        *out = narrowed == type ? before : new_flow_type(q, narrowed, before.incomplete);
        return true;
    }
    if (effects.returns_never) {
        *out = (FlowType){ ty_intrinsic(me, TY_UNREACHABLE_NEVER), false };
        return true;
    }
    return false;
}

#define FLOW_MAX_DEPTH 2000

// The body the flow graph belongs to: its start node's function, else the reference's file (a namespace body's start
// node says nothing: its file then too)
static void report_flow_control_error(Checker* me, Expr const* reference, FlowNode const* flow) {
    for (u32 steps = 0; flow && !(flow->flags & FLOW_START) && steps < 10000000; steps++) {
        if (flow->flags & (FLOW_BRANCH_LABEL | FLOW_LOOP_LABEL)) {
            FlowList const* antecedents = flow->antecedents;
            flow = antecedents && antecedents->count ? antecedents->items[0] : nullptr;
        } else if (flow->flags & FLOW_UNREACHABLE) {
            flow = nullptr;
        } else {
            flow = flow->antecedent;
        }
    }
    FunctionImpl const* function = flow && (flow->flags & FLOW_START) ? flow->node : nullptr;
    VecStatement const* statements = nullptr;
    if (function && function->has_body) {
        statements = &function_impl_body(function)->statements;
    } else if (reference && span_source(expr_outer_span(reference))) {
        statements = &span_source(expr_outer_span(reference))->ast.statements;
    }
    if (!statements || vec_statement_is_empty(statements)) return;
    ty_error_at(me, block_first_token(vec_statement_front(statements)->span), 2563,
        cstr_from("The containing function or module body is too large for control flow analysis."));
}

static FlowType type_at_flow_node(FlowQuery* q, FlowNode const* flow) {
    Checker* me = q->me;
    if (q->depth == FLOW_MAX_DEPTH) {
        // tsc's getTypeAtFlowNode at 2000 recursions: TS2563 at the first token of the body (reportFlowControlError), and
        // no more flow analysis in it
        if (!my flow_analysis_disabled) {
            my flow_analysis_disabled = true;
            report_flow_control_error(me, q->reference, flow);
        }
        return (FlowType){ ty_intrinsic(me, TY_ERROR), false };
    }
    q->depth++;
    FlowNode const* shared = nullptr;
    FlowType type;
    while (true) {
        u32 flags = flow->flags;
        if (flags & FLOW_SHARED) {
            // Nodes with several successors are remembered for the rest of this query
            for (u32 i = q->shared_start; i < my flow_shared_count; i++) {
                if (my flow_shared[i].flow == flow) {
                    q->depth--;
                    return new_flow_type(q, my flow_shared[i].type, my flow_shared[i].incomplete);
                }
            }
            shared = flow;
        }
        if (flags & FLOW_ASSIGNMENT) {
            if (!type_at_flow_assignment(q, flow, &type)) {
                flow = flow->antecedent;
                continue;
            }
        } else if (flags & FLOW_CALL) {
            if (!type_at_flow_call(q, flow, &type)) {
                flow = flow->antecedent;
                continue;
            }
        } else if (flags & FLOW_CONDITION) {
            type = type_at_flow_condition(q, flow);
        } else if (flags & FLOW_SWITCH_CLAUSE) {
            type = type_at_switch_clause(q, flow);
        } else if (flags & FLOW_LABEL) {
            FlowList const* antecedents = label_antecedents(flow, q->overrides);
            if (!antecedents || !antecedents->count) {
                type = (FlowType){ ty_intrinsic(me, TY_NEVER), false };
            } else if (antecedents->count == 1) {
                flow = antecedents->items[0];
                continue;
            } else {
                type = flags & FLOW_BRANCH_LABEL ? type_at_branch_label(q, antecedents) :
                    type_at_loop_label(q, flow, antecedents);
            }
        } else if (flags & FLOW_REDUCE_LABEL) {
            FlowReduceLabel const* reduce = flow->node;
            my last_reachability_flow = nullptr; // (Unreliable once labels are overridden)
            FlowLabelOverride override = { q->overrides, reduce->target, reduce->antecedents };
            q->overrides = &override;
            type = type_at_flow_node(q, flow->antecedent);
            q->overrides = override.outer;
        } else if (flags & FLOW_START) {
            FunctionImpl const* function = flow->node;
            if ((flags & FLOW_CONTINUES_OUTSIDE) && continues_outside(q, flow, function)) {
                // (A function in unreachable code continues into the unreachable flow)
                if (!function->outer_flow) {
                    type = (FlowType){ auto_to_any(me, q->declared), false };
                    break;
                }
                flow = function->outer_flow;
                if (!function->is_iife) q->left_container = true;
                continue;
            }
            // A variable of another container than the one the reference is in is assumed to be initialized. A
            // parameter's initial type is what it is assumed to be anyway (without the undefined its default rules out)
            bool is_outer = (q->left_container || (q->symbol && q->symbol->flow_start != flow)) &&
                !(q->symbol && q->symbol->is_parameter) && !ty_symbol_never_initialized(q->symbol);
            type = (FlowType){ is_outer ? q->declared : q->initial, false };
        } else if (flags & FLOW_ARRAY_MUTATION) {
            if (!type_at_array_mutation(q, flow, &type)) {
                flow = flow->antecedent;
                continue;
            }
        } else {
            // Unreachable: errors come from the builder, the declared type keeps follow-on errors away (an auto type
            // as any: tsc's convertAutoToAny)
            type = (FlowType){ auto_to_any(me, q->declared), false };
        }
        break;
    }
    if (shared) {
        if (my flow_shared_count == my flow_shared_cap) {
            my flow_shared_cap = my flow_shared_cap ? my flow_shared_cap * 2 : 64;
            my flow_shared = realloc(my flow_shared, sizeof(*my flow_shared) * my flow_shared_cap);
            panic_if(!my flow_shared, "out of memory");
        }
        my flow_shared[my flow_shared_count++] = (FlowSharedEntry){ shared, type.type, type.incomplete };
    }
    q->depth--;
    return type;
}

Ty const* ty_flow_type(Checker* me, Expr const* reference, Ty const* declared, Ty const* initial, FlowNode const* flow) {
    bool substitutes = my flow_substitutes_constraints;
    my flow_substitutes_constraints = false;
    if (my flow_analysis_disabled) return ty_intrinsic(me, TY_ERROR);
    if (!flow) return declared;
    Expr const* inner = skip_reference_wrappers(reference);
    FlowQuery q = {
        .substitutes_constraints = substitutes,
        .me = me,
        .reference = reference,
        .symbol = inner->tag == EXPR_IDENTIFIER && !inner->identifier.keyword ? inner->identifier.target : nullptr,
        .identifier = inner->tag == EXPR_IDENTIFIER && !inner->identifier.keyword,
        .declared = declared,
        .initial = initial,
        .shared_start = my flow_shared_count,
    };
    FlowType result = type_at_flow_node(&q, flow);
    my flow_shared_count = q.shared_start;
    // (The result of a top-level query can be marked incomplete: a shared node that was first reached through a back
    // edge is remembered with what it had then. tsc takes the type as it is, so that is what the baselines show)
    Ty const* ret = result.type;
    // `x` of `x.push(v)` is the auto array, `any[]` without implicit any errors: anything can be pushed
    if (ret->kind == TY_EVOLVING_ARRAY) {
        ret = ty_is_evolving_array_operation_target(me, reference) ? ty_auto_array(me) : final_array_type(me, ret);
    }
    if (ret == ty_intrinsic(me, TY_UNREACHABLE_NEVER)) return declared;
    return ret;
}

// Whether a symbol of a parameter's pattern (nested ones too) is assigned somewhere (tsc's isSomeSymbolAssigned)
static bool some_pattern_symbol_assigned(Binding const* binding) {
    switch (binding->tag) {
        case BINDING_IDENTIFIER: return binding->symbol && symbol_assignments(binding->symbol)->is_assigned;
        case BINDING_ARRAY:
            for_each (i, vec_binding, &binding->array) {
                if (some_pattern_symbol_assigned(i)) return true;
            }
            return false;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &binding->object) {
                if (some_pattern_symbol_assigned(&i->target)) return true;
            }
            return false;
        default: return false;
    }
}

static Ty const* map_base_constraint(Checker* me, Ty const* ty, void const* arg) {
    (void)arg;
    // (An intersection with a generic part too: `CO<T> & { url?: string }` is `object & { url?: string }`, tsc's
    // getBaseConstraintOrType)
    Ty const* constraint = ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED) ||
        (ty->kind == TY_INTERSECTION && (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) ? ty_base_constraint(me, ty) : nullptr;
    return constraint ? constraint : ty;
}

Ty const* ty_destructured_narrowed_type(Checker* me, Binding const* binding, FlowNode const* flow) {
    Binding const* pattern = binding->parent;
    if (!flow || !pattern || binding->default_value || binding->rest) return nullptr;
    u32 elements = pattern->tag == BINDING_OBJECT ? (u32)vec_object_binding_item_size(&pattern->object) :
        (u32)vec_binding_size(&pattern->array);
    if (elements < 2) return nullptr;
    Binding const* root = pattern;
    while (root->parent) root = root->parent;
    bool parameter = root->source && root->source->tag == LAZY_TYPE_INITIALIZED_BINDING && binding->symbol &&
        binding->symbol->is_parameter;
    if (!parameter && !(binding->symbol && binding->symbol->is_const)) return nullptr;
    if (parameter && some_pattern_symbol_assigned(root)) return nullptr;
    Ty const* parent = map_type(me, ty_binding_type(me, pattern, (LowerCtx){ .generic = true }), map_base_constraint, nullptr);
    if (parent->kind != TY_UNION || (parent->flags & TYF_BOOLEAN)) return nullptr;
    static Expr const placeholder = { .tag = EXPR_IMPLICIT_ANY };
    FlowQuery q = {
        .me = me,
        .reference = &placeholder,
        .pattern = pattern,
        .declared = parent,
        .initial = parent,
        .shared_start = my flow_shared_count,
    };
    FlowType result = type_at_flow_node(&q, flow);
    my flow_shared_count = q.shared_start;
    Ty const* narrowed = result.type;
    if (narrowed == ty_intrinsic(me, TY_UNREACHABLE_NEVER)) return nullptr;
    if (narrowed->flags & TYF_NEVER) return ty_intrinsic(me, TY_NEVER);
    // (tsgo's getBindingElementTypeFromParentType checks that the narrowed parent iterates, at the pattern: a
    // generic's constraint that doesn't is TS2488 there next to the declared type's, `[number, number] | undefined`)
    if (pattern->tag == BINDING_ARRAY && my errs && my errs->phase == ERROR_PHASE_TYPECHECK) {
        ty_iterated_type(me, ITERATION_DESTRUCTURING, narrowed, pattern->content_span, my errs);
    }
    // (And that the element's property is in it, at the element: sequelize's `const { url, ...rest } = options` of a
    // generic `Raw<T> | string` is TS2339 on the constraint too)
    ObjectBindingItem const* item = pattern->tag == BINDING_OBJECT ? binding->parent_item : nullptr;
    if (item && !item->computed_key && !item->private_name && my errs && my errs->phase == ERROR_PHASE_TYPECHECK &&
        !(narrowed->flags & TYF_ANY)
    ) {
        // (Without the undefined an initializer rules out, or any nullable of an ambient parameter: tsc's
        // getBindingElementTypeFromParentType, like binding_element_parent_type())
        Expr const* initializer = pattern->parent ? pattern->default_value :
            root_initializer(pattern) ? initialized_binding_value(root_initializer(pattern)) : nullptr;
        Ty const* checked = narrowed;
        if (compiler_options()->strict_null_checks && pattern->ambient && parameter) {
            checked = ty_non_nullable(me, checked);
        } else if (compiler_options()->strict_null_checks && initializer &&
            !has_type_facts(me, ty_expr_type(me, initializer), TF_EQ_UNDEFINED)
        ) {
            checked = type_with_facts(me, checked, TF_NE_UNDEFINED);
        }
        if (!(checked->flags & TYF_NEVER)) ty_check_property_access(me, checked, ty_literal(me, &item->source), span_null,
            span_valid(item->source_span) ? item->source_span : binding->content_span, false, true, my errs);
    }
    return ty_binding_element_type_from(me, binding, narrowed);
}

Ty const* ty_parameter_narrowed_type(Checker* me, Binding const* binding, FlowNode const* flow) {
    Symbol const* symbol = binding->symbol;
    if (!flow || binding->parent || binding->has_explicit_type || !symbol || !symbol->is_parameter || !symbol->function) {
        return nullptr;
    }
    FunctionImpl const* function = symbol->function;
    u32 index = 0;
    if (vec_function_arg_size(&function->args) < 2 || !parameter_index(function, symbol, &index)) return nullptr;
    if (!ty_function_is_context_sensitive(function)) return nullptr;
    Signature const* sig = ty_function_assigned_contextual_signature(me, function);
    if (!sig || sig_res(me, sig)->param_count != 1 || !sig_res(me, sig)->params[0].rest) return nullptr;
    Ty const* rest = ty_apparent_type(me, sig_res(me, sig)->params[0].type);
    if (rest->kind != TY_UNION) return nullptr;
    for (u32 i = 0; i < rest->composite.types.len; i++) {
        if (!ty_is_tuple(rest->composite.types.data[i])) return nullptr;
    }
    for_each (i, vec_function_arg, &function->args) {
        if (some_pattern_symbol_assigned(&i->initializer.binding)) return nullptr;
    }
    static Expr const placeholder = { .tag = EXPR_IMPLICIT_ANY };
    FlowQuery q = {
        .me = me,
        .reference = &placeholder,
        .parameters_of = function,
        .declared = rest,
        .initial = rest,
        .shared_start = my flow_shared_count,
    };
    FlowType result = type_at_flow_node(&q, flow);
    my flow_shared_count = q.shared_start;
    Ty const* narrowed = result.type;
    // (Narrowed or not: a parameter of a generic rest type is its element of the apparent union, `kind` of
    // `<T extends ["a", number] | ["b", string]>(...args: T)` is "a" | "b", tsc's getNarrowedTypeOfSymbol)
    if (narrowed == ty_intrinsic(me, TY_UNREACHABLE_NEVER)) return nullptr;
    Value key = value_number(index);
    Ty const* element = ty_access(me, narrowed, ty_literal(me, &key));
    // (Past the end of the tuples it is undefined: tsc's getIndexedAccessType, getTupleElementTypeOutOfStartCount)
    return element ? element : ty_indexed(me, narrowed, ty_literal(me, &key));
}

// Inferred type predicates (tsc's getTypePredicateFromBody): `x => x !== undefined` is `(x: T | undefined) => x is T`

// The expression of the one `return` of a body, null if there are several, a bare one, or none
static bool find_single_return(Statement const* statement, Statement const** found);

static bool find_single_return_in(VecStatement const* statements, Statement const** found) {
    for_each (i, vec_statement, statements) {
        if (!find_single_return(i, found)) return false;
    }
    return true;
}

static bool find_single_return(Statement const* statement, Statement const** found) {
    switch (statement->tag) {
        case STATEMENT_BLOCK: return find_single_return_in(&statement->block.statements, found);
        case STATEMENT_IF_ELSE: return find_single_return_in(&statement->if_else.statements, found);
        case STATEMENT_FOR_LOOP:
            return find_single_return(&statement->for_loop->initialization, found) &&
                find_single_return(&statement->for_loop->body, found);
        case STATEMENT_FOR_EACH_LOOP: return find_single_return(&statement->for_each_loop->body, found);
        case STATEMENT_SWITCH:
            for_each (i, vec_switch_case, &statement->switch_->cases) {
                if (!find_single_return_in(&i->statements, found)) return false;
            }
            return true;
        case STATEMENT_TRY_CATCH:
            return find_single_return_in(&statement->try_catch->try_block.statements, found) &&
                find_single_return_in(&statement->try_catch->catch_block.statements, found) &&
                find_single_return_in(&statement->try_catch->finally_block.statements, found);
        case STATEMENT_LABELED: return find_single_return(statement->labeled.body, found);
        case STATEMENT_WITH: return find_single_return(&statement->with->body, found);
        case STATEMENT_RETURN_EXPR:
            if (*found || statement_is_bare_return(statement)) return false;
            *found = statement;
            return true;
        default: return true;
    }
}

// tsc's checkIfExpressionRefinesParameter: the type the parameter has where `expr` is true, if that says something
// and nothing of it is left where `expr` is false
static Ty const* refined_parameter_type(Checker* me, Expr const* expr, FlowNode* antecedent, Expr const* reference, Ty const* init) {
    FlowNode* when_true = arena_new(&my arena, FlowNode);
    *when_true = (FlowNode){ .flags = FLOW_TRUE_CONDITION, .antecedent = antecedent, .node = expr };
    Ty const* true_type = ty_flow_type(me, reference, init, init, when_true);
    if (true_type == init) return nullptr;
    FlowNode* when_false = arena_new(&my arena, FlowNode);
    *when_false = (FlowNode){ .flags = FLOW_FALSE_CONDITION, .antecedent = antecedent, .node = expr };
    // (Reduced: `{ k: false } & { k: true }` is never, tsc's getReducedType)
    Ty const* false_type = ty_reduced(me, ty_flow_type(me, reference, init, true_type, when_false));
    return false_type->flags & TYF_NEVER ? true_type : nullptr;
}

TyPredicate const* ty_infer_type_predicate(Checker* me, FunctionImpl const* function) {
    if (!function->has_body || function->is_type || function->is_async || function->is_generator ||
        function->is_constructor || function->accessor || function_impl_rare(function)->constructed_class
    ) {
        return nullptr;
    }
    Statement const* single = nullptr;
    if (!find_single_return_in(&function_impl_body(function)->statements, &single) || !single) return nullptr;
    if (ty_function_end_reachability(me, function) != FLOW_REACH_NO) return nullptr;
    Expr const* expr = &single->expr;
    FlowIsolation isolation = ty_flow_isolate(me);
    TyPredicate* ret = nullptr;
    // (Nothing in the body is an argument of the call inferring now, see function_return_type())
    struct OuterInference* outer = my outer_inference;
    my outer_inference = nullptr;
    Ty const* returned = ty_expr_type(me, expr);
    my outer_inference = outer;
    if (returned->flags & TYF_BOOLEAN) {
        FlowNode* antecedent = single->flow;
        if (!antecedent) {
            antecedent = arena_new(&my arena, FlowNode);
            *antecedent = (FlowNode){ .flags = FLOW_START };
        }
        u32 index = 0;
        for_each (i, vec_function_arg, &function->args) {
            Binding const* binding = &i->initializer.binding;
            // (The index counts a `this` parameter too: tsc's checkIfExpressionRefinesAnyParameter indexes
            // fn.Parameters(), so `isStr(this: void, x)` predicates about its second argument, which a call doesn't have)
            u32 at = index++;
            if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
            Symbol const* symbol = binding->tag == BINDING_IDENTIFIER ? binding->symbol : nullptr;
            if (!symbol || i->rest || symbol_assignments(symbol)->is_assigned) continue;
            Ty const* init = ty_type_of_symbol(me, symbol);
            if (!init || (init->flags & TYF_BOOLEAN)) continue;
            Expr* reference = arena_new(&my arena, Expr);
            *reference = (Expr){ .tag = EXPR_IDENTIFIER, .inner_span = binding->content_span };
            reference->identifier = (IdentifierExpr){ .target = symbol, .symbol = symbol };
            Ty const* refined = refined_parameter_type(me, expr, antecedent, reference, init);
            if (!refined) continue;
            ret = arena_new(&my arena, TyPredicate);
            *ret = (TyPredicate){
                .param_index = (i32)at,
                .param_name = checker_atom(me, span_sv(binding->content_span)),
                .type = refined,
            };
            break;
        }
    }
    ty_flow_restore(me, isolation);
    return ret;
}
