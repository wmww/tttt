#include "ty_internal.h"
#include "../flow.h"
#include "../function_impl.h"
#include "../binding.h"
#include "../statement.h"
#include "../scope.h"
#include "../compiler_options.h"

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
        u32 flags = flow->flags;
        if (flags & FLOW_SHARED && use_cache && !overrides) {
            u64 key[2] = { KEY_FLOW_REACHABLE, (u64)(uintptr_t)flow };
            uintptr_t cached = (uintptr_t)intern_get(&my links, key, 2);
            FlowReachability ret = cached ? (FlowReachability)(cached - 1) : reachability_walk(me, flow, overrides, false);
            if (!cached) *intern_slot(me, &my links, key, 2) = (void const*)((uintptr_t)ret + 1);
            return ret;
        }
        use_cache = true;
        if (flags & (FLOW_ASSIGNMENT | FLOW_CONDITION)) {
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
    return reachability_walk(me, flow, nullptr, true);
}

FlowReachability ty_function_end_reachability(Checker* me, FunctionImpl const* function) {
    return ty_flow_reachability(me, function->end_flow);
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
    Ty const* declared;
    Ty const* initial;
    u32 key; // 0: none (not computed, or the reference has none)
    bool key_set;
    u32 shared_start;
    u32 depth;
    bool left_container; // The walk continued outside of the function the reference is in
    FlowLabelOverride const* overrides;
} FlowQuery;

static FlowType type_at_flow_node(FlowQuery* q, FlowNode const* flow);
static Ty const* narrow_type(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_true, bool nullish);
static bool is_type_subset_of(Ty const* source, Ty const* target);

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

static bool is_empty_anonymous_object(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || ty->anonymous.function) return false;
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->call_count && !members->construct_count && !members->index_count &&
        !members->mapped;
}

static bool is_function_object(Checker* me, Ty const* ty) {
    Members const* members = ty_members(me, ty);
    if (members->call_count || members->construct_count) return true;
    Ty const* function = ty_global_function_type(me);
    return function && (ty == function || (
        ty_get_prop(me, ty, checker_atom(me, c_sv("bind"))) && ty_is_related(me, ty, function, TY_RELATION_SUBTYPE)
    ));
}

static u32 type_facts(Checker* me, Ty const* ty, u32 mask) {
    bool strict = compiler_options()->strict_null_checks;
    u32 flags = ty->flags;
    // Generic or not, like tsc's getTypeFacts: a mapping can be empty, a template like a non-empty literal
    if (flags & TYF_STRING_MAPPING) return primitive_facts(TF_TYPEOF_EQ_STRING, true, true);
    if (flags & TYF_TEMPLATE_LITERAL) return primitive_facts(TF_TYPEOF_EQ_STRING, true, false);
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
    TyList members = ty->composite.types;
    TyBuf mapped = {0};
    bool changed = false;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* t = map(me, members.data[i], arg);
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
    Ty const* types[2] = { ty, ty_empty_object_type(me) };
    return ty_intersection(me, types, 2, nullptr, (TyList){0});
}

typedef struct RemoveNullableArg {
    u32 target_facts;
    Ty const* intersect_with;
} RemoveNullableArg;

static Ty const* map_remove_nullable(Checker* me, Ty const* ty, void const* arg) {
    RemoveNullableArg const* remove = arg;
    if (!has_type_facts(me, ty, remove->target_facts)) return ty;
    Ty const* types[2] = { ty, remove->intersect_with };
    return ty_intersection(me, types, 2, nullptr, (TyList){0});
}

static Ty const* remove_nullable_by_intersection(Checker* me, Ty const* ty, u32 target_facts, u32 other_facts, u32 other_includes, Ty const* other) {
    u32 facts = type_facts(me, ty, TF_EQ_UNDEFINED | TF_EQ_NULL | TF_IS_UNDEFINED | TF_IS_NULL);
    if (!(facts & target_facts)) return ty;
    // HACK: tsc intersects with `{} | other` when the member could also be the other nullable and the type doesn't
    // include it already. Here: only when that can't be the case
    (void)other_facts;
    (void)other;
    if (!(facts & other_includes) && !compiler_options()->strict_null_checks) return ty;
    RemoveNullableArg arg = { target_facts, ty_empty_object_type(me) };
    return map_type(me, ty, map_remove_nullable, &arg);
}

// tsc's getAdjustedTypeWithFacts(): `unknown` is `{} | null | undefined` while it is filtered
static Ty const* adjusted_type_with_facts(Checker* me, Ty const* ty, u32 facts) {
    bool strict = compiler_options()->strict_null_checks;
    bool was_unknown = strict && (ty->flags & TYF_UNKNOWN);
    if (was_unknown) {
        Ty const* parts[3] = { ty_empty_object_type(me), ty_intrinsic(me, TY_NULL), ty_intrinsic(me, TY_UNDEFINED) };
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
Ty const* ty_truthy_part(Checker* me, Ty const* ty) { return adjusted_type_with_facts(me, ty, TF_TRUTHY); }
Ty const* ty_non_nullish_part(Checker* me, Ty const* ty) { return adjusted_type_with_facts(me, ty, TF_NE_UNDEFINED_OR_NULL); }
Ty const* ty_remove_definitely_falsy(Checker* me, Ty const* ty) { return type_with_facts(me, ty, TF_TRUTHY); }
bool ty_can_be_truthy(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_TRUTHY); }
bool ty_can_be_falsy(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_FALSY); }
bool ty_can_be_nullish(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_EQ_UNDEFINED_OR_NULL); }
bool ty_can_be_undefined(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_IS_UNDEFINED); }
bool ty_can_be_null(Checker* me, Ty const* ty) { return has_type_facts(me, ty, TF_IS_NULL); }

unsigned ty_undefined_or_null_facts(Checker* me, Ty const* ty) {
    return type_facts(me, ty, TF_IS_UNDEFINED_OR_NULL) & TF_IS_UNDEFINED_OR_NULL;
}

static Ty const* base_type_of_literal(Checker* me, Ty const* ty) { return ty_base_type_of_literal(me, ty); }

static bool is_unit_type(Ty const* ty) {
    if (ty->flags & TYF_ENUM) return false;
    return (ty->flags & (TYF_LITERAL | TYF_UNDEFINED | TYF_NULL)) && ty->kind != TY_UNION;
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

// The name an access goes by: `a.b`, `a["b"]`, `a[0]`, `a[c]` for a `const c` of a literal type
static Atom const* access_name(Checker* me, PropertyExpr const* access) {
    Expr const* key = &access->prop;
    if (access->private_class) return ty_literal_name(me, ty_access_key_type(me, access));
    if (key->tag == EXPR_LITERAL) {
        if (key->literal.tag != VALUE_STRING && key->literal.tag != VALUE_NUMBER) return nullptr;
    } else if (key->tag == EXPR_IDENTIFIER) {
        Symbol const* symbol = key->identifier.target;
        if (!symbol || !symbol->is_const || key->identifier.keyword) return nullptr;
    } else {
        return nullptr;
    }
    bool is_number = false;
    return ty_property_name_of_key(me, ty_flow_expr_type(me, key), &is_number);
}

// `x[k]` where k has no literal type but always names the same thing: a const, or a parameter or `let` never assigned
// (tsc's isConstantVariable || isParameterOrMutableLocalVariable && !isSymbolAssigned). Null otherwise
static Symbol const* access_key_symbol(PropertyExpr const* access) {
    Expr const* key = &access->prop;
    if (access->form != PROPERTY_EXPR_BRACKET || key->tag != EXPR_IDENTIFIER || key->identifier.keyword) return nullptr;
    Symbol const* symbol = key->identifier.target;
    return symbol && (symbol->is_const || !symbol->is_assigned) ? symbol : nullptr;
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

static bool symbol_matches_reference(Expr const* reference, Symbol const* symbol) {
    reference = skip_reference_wrappers(reference);
    return reference->tag == EXPR_IDENTIFIER && !reference->identifier.keyword && reference->identifier.target == symbol;
}

// `x.y.z` is about `x.y` and `x`
static bool contains_matching_reference(Checker* me, Expr const* source, Expr const* target) {
    source = skip_reference_wrappers(source);
    while (source->tag == EXPR_PROPERTY) {
        source = skip_reference_wrappers(&source->property->expr);
        if (is_matching_reference(me, source, target)) return true;
    }
    return false;
}

static bool contains_symbol_reference(Expr const* source, Symbol const* symbol) {
    source = skip_reference_wrappers(source);
    while (source->tag == EXPR_PROPERTY) {
        source = skip_reference_wrappers(&source->property->expr);
        if (symbol_matches_reference(source, symbol)) return true;
    }
    return false;
}

// `a?.b.c` contains `a`: if the chain isn't undefined, a isn't nullish
static bool optional_chain_contains_reference(Checker* me, Expr const* source, Expr const* target) {
    while (expr_is_optional_chain(source)) {
        source = source->tag == EXPR_PROPERTY ? &source->property->expr :
            source->tag == EXPR_CALL ? &source->call->callable : &source->unary_op->expr;
        while (source->tag == EXPR_GENERIC_PARAMS) source = &source->generic_params->expr;
        if (is_matching_reference(me, source, target)) return true;
    }
    return false;
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
    u32 count = reference_key_words(me, q->reference, words + 3, c_arraylen(words) - 3);
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
Ty const* ty_quick_expr_type(Checker* me, Expr const* expr, bool in_flow) {
    if (expr->tag != EXPR_CALL) return nullptr;
    CallExpr const* call = expr->call;
    if (call->uses_new || call->is_extends || call->callable.tag == EXPR_GENERIC_PARAMS || in_optional_chain(expr)) {
        return nullptr;
    }
    if (call->callable.tag == EXPR_IDENTIFIER) {
        csview name = call->callable.identifier.name.sv;
        if (csview_equals(name, "Symbol") || csview_equals(name, "require")) return nullptr;
    }
    Ty const* callee = in_flow ? ty_flow_expr_type(me, &call->callable) : ty_expr_type(me, &call->callable);
    if (!(callee->flags & TYF_OBJECT)) return nullptr;
    Members const* members = ty_members(me, callee);
    if (members->call_count != 1 || members->construct_count || members->index_count || members->prop_count) return nullptr;
    Signature const* sig = members->call[0];
    signature_resolve(me, sig);
    if (sig->type_params.len) return nullptr;
    return signature_return_type(me, sig);
}

Ty const* ty_flow_expr_type(Checker* me, Expr const* expr) {
    Ty const* quick = ty_quick_expr_type(me, expr, true);
    if (quick) return quick;
    LazyType* lazy = (LazyType*)&expr->lazy_type;
    if (lazy->tag == LAZY_TYPE_EXPR && lazy->slot) {
        LazyMemo* memo = lazy_type_state_peek(lazy);
        if (memo && memo->done) return memo->evaluated;
    }
    // With no loop in progress what comes out is complete, and remembered like in tsc's flowTypeCache (not in the
    // memo: nobody gave this evaluation a contextual type). Without it a chain of `x = x + x` doubles at every link
    // While a loop is in progress the cache is only good for the walk of one back edge (flow_expr_generation)
    bool complete = my flow_loop_count == my flow_loop_start;
    u64 key[3] = { KEY_FLOW_EXPR, (u64)(uintptr_t)expr, complete ? 0 : my flow_expr_generation };
    Ty const* cached = intern_get(&my links, key, 3);
    if (cached) return cached;
    my flow_uncached++;
    Ty const* ret = ty_compute_expr_without_context(me, expr);
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

// What is left of a declared union type after `assigned` was assigned to it
static Ty const* assignment_reduced_type(Checker* me, Ty const* declared, Ty const* assigned) {
    if (declared == assigned || (assigned->flags & TYF_NEVER)) return assigned;
    Ty const* reduced = filter_type(me, declared, assigned_is_assignable, assigned);
    // `flag = true` leaves a fresh `true`, which widens again: `let copy = flag` is boolean
    if (ty_is_fresh(assigned) && (assigned->flags & TYF_BOOLEAN_LITERAL)) reduced = map_type(me, reduced, map_fresh, nullptr);
    // (An erroneous assignment doesn't narrow)
    return ty_is_related(me, assigned, reduced, TY_RELATION_ASSIGNABLE) ? reduced : declared;
}

static Ty const* for_each_element_type(Checker* me, ForEachLoopStatement const* for_each) {
    (void)me;
    return lazy_type_get((LazyType*)&for_each->var_type);
}

// The type the value at the end of a destructuring path has, null if it can't be told
static Ty const* pattern_path_type(Checker* me, FlowAssign const* assign, FlowPatternPath const* path) {
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
            if (!ty_extends_array(me, object)) return nullptr;
            Value index = value_number(path->index);
            ret = ty_access(me, object, ty_literal(me, &index));
            if (!ret) ret = ty_access(me, object, ty_intrinsic(me, TY_NUMBER));
            break;
        }
        case FLOW_PATH_PROPERTY: {
            ObjectExprItem const* item = path->item;
            if (item->computed_key) return nullptr;
            ret = ty_access(me, object, ty_string_literal(me, cstr_sv(&item->name_string)));
            break;
        }
        case FLOW_PATH_REST:
            return nullptr; // TODO: the rest of a tuple or object
    }
    if (ret && path->default_value) {
        ret = union_of(me, type_with_facts(me, ret, TF_NE_UNDEFINED), ty_flow_expr_type(me, path->default_value));
    }
    return ret;
}

static Ty const* initial_or_assigned_type(FlowQuery* q, FlowAssign const* assign) {
    Checker* me = q->me;
    switch (assign->kind) {
        case FLOW_ASSIGN_DECLARATION: {
            Binding const* binding = assign->binding;
            if (!binding->parent && binding->source && binding->source->tag == LAZY_TYPE_INITIALIZED_BINDING) {
                InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
                if (initialized->value.tag == EXPR_IMPLICIT_ANY) return ty_binding_assigned_type(me, binding);
                if (binding->has_explicit_type && ty_expr_is_context_sensitive(&initialized->value)) {
                    // Typed by the annotation, like in the walk (see FLOW_ASSIGN_EXPR below)
                    Ty const* annotated = ty_from_node(me, &binding->explicit_type, (LowerCtx){ .generic = true });
                    return ty_check_expr(me, &initialized->value, annotated->flags & TYF_ANY ? nullptr : annotated);
                }
                return ty_flow_expr_type(me, &initialized->value);
            }
            return ty_binding_assigned_type(me, binding);
        }
        case FLOW_ASSIGN_EXPR: {
            // A function expression is typed by its target, like the walk over the statement does (tsc finds the
            // contextual type up the tree, so asking bottom-up gets the same). Anything else goes without context
            BinaryOpExpr const* assignment = assign->assignment->binary_op;
            if (assignment->op == BINARY_OP_EQUAL && ty_expr_is_context_sensitive(&assignment->right)) {
                return ty_expr_type(me, assign->assignment);
            }
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
        return assign->binding->tag == BINDING_IDENTIFIER && symbol_matches_reference(q->reference, assign->binding->symbol);
    }
    return assign->target && is_matching_reference(q->me, q->reference, assign->target);
}

static bool assign_contains(FlowQuery* q, FlowAssign const* assign) {
    if (assign->kind == FLOW_ASSIGN_DECLARATION) {
        return assign->binding->tag == BINDING_IDENTIFIER && contains_symbol_reference(q->reference, assign->binding->symbol);
    }
    return assign->target && contains_matching_reference(q->me, q->reference, assign->target);
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
            *out = (FlowType){ base_type_of_literal(me, before.type), before.incomplete };
            return true;
        }
        Ty const* declared = q->declared;
        if (declared == ty_intrinsic(me, TY_AUTO)) {
            // `let x;` is what was assigned last (tsc's autoType). HACK: `x = []` makes an evolving array in tsc, which
            // isn't there: any[], like `let x = []` (hacks.md)
            Expr const* right = assign->kind == FLOW_ASSIGN_EXPR ? &assign->assignment->binary_op->right : nullptr;
            if (right && right->tag == EXPR_ARRAY && !vec_expr_size(&right->array)) {
                *out = (FlowType){ ty_array(me, ty_intrinsic(me, TY_ANY), false), false };
                return true;
            }
            *out = (FlowType){ ty_widened_literal(me, initial_or_assigned_type(q, assign)), false };
            return true;
        }
        if (assign->kind == FLOW_ASSIGN_EXPR && assign->assignment->binary_op->op != BINARY_OP_EQUAL) {
            declared = base_type_of_literal(me, declared); // `x ||= e`
        } else if (assign->kind == FLOW_ASSIGN_EXPR) {
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
        if (ty_flow_reachability(me, flow) == FLOW_REACH_NO) {
            *out = (FlowType){ ty_intrinsic(me, TY_UNREACHABLE_NEVER), false };
            return true;
        }
        *out = (FlowType){ q->declared, false };
        return true;
    }
    return false;
}

// Calls: type predicates, assertions, functions that never return

static bool symbol_has_explicit_type(Symbol const* symbol) {
    if (symbol->flags & (SYMBOL_FUNCTION | SYMBOL_CLASS | SYMBOL_ENUM | SYMBOL_VALUE_MODULE)) return true;
    if (!(symbol->flags & SYMBOL_VARIABLE)) return false;
    Binding const* binding = symbol_find_node(symbol, SYMBOL_DECL_VARIABLE);
    return binding && !binding->parent && binding->has_explicit_type;
}

static bool prop_has_explicit_type(Prop const* prop) {
    while (prop->target) prop = prop->target;
    // HACK: the template of a mapped type (`Readonly<Services>`), where tsc asks the property it was mapped from
    if (!prop->name) return true;
    if (prop->symbol) return symbol_has_explicit_type(prop->symbol);
    if (prop->flags & (PROP_GETTER | PROP_SETTER)) return prop->function->ret_explicit;
    return prop->node || prop->function || prop->overload_count;
}

// tsc's getTypeOfDottedName(): the type of the callee of a call statement from declarations alone, without control
// flow analysis (which is in the middle of asking). Null: some name on the way has no explicit type, and the call is
// taken to have no effect (tsc reports TS2775/TS2776 for an assertion like that)
static Ty const* type_of_dotted_name(Checker* me, Expr const* expr) {
    expr = skip_reference_wrappers(expr);
    if (expr->tag == EXPR_IDENTIFIER) {
        u32 keyword = expr->identifier.keyword;
        if (keyword == KEYWORD_IDENTIFIER_THIS || keyword == KEYWORD_IDENTIFIER_SUPER) {
            return ty_identifier_reference_type(me, &expr->identifier);
        }
        Symbol const* symbol = expr->identifier.target;
        if (keyword || !symbol || !symbol_has_explicit_type(symbol)) return nullptr;
        return ty_identifier_reference_type(me, &expr->identifier);
    }
    if (expr->tag != EXPR_PROPERTY) return nullptr;
    if (ty_is_import_meta(expr)) return ty_import_meta_type(me);
    Ty const* object = type_of_dotted_name(me, &expr->property->expr);
    Atom const* name = object ? access_name(me, expr->property) : nullptr;
    if (!name) return nullptr;
    object = ty_apparent_type(me, object);
    if (!(object->flags & TYF_OBJECT)) return nullptr;
    Prop const* prop = ty_get_prop(me, object, name);
    return prop && prop_has_explicit_type(prop) ? prop_type(me, prop) : nullptr;
}

// tsc's getEffectsSignature(), remembered per call like there
static CallEffects call_effects(Checker* me, Expr const* call_expr, bool is_statement) {
    u64 key[3] = { KEY_FLOW_CALL, (u64)(uintptr_t)call_expr, is_statement };
    CallEffects const* cached = intern_get(&my links, key, 3);
    if (cached) return *cached;
    CallExpr const* call = call_expr->call;
    Expr const* callee_expr = &call->callable;
    if (callee_expr->tag == EXPR_GENERIC_PARAMS) callee_expr = &callee_expr->generic_params->expr;
    Ty const* callee = nullptr;
    if (is_statement) {
        callee = type_of_dotted_name(me, callee_expr);
    } else if (callee_expr->tag != EXPR_IDENTIFIER || callee_expr->identifier.keyword != KEYWORD_IDENTIFIER_SUPER) {
        callee = ty_non_nullable(me, ty_flow_expr_type(me, callee_expr));
    }
    my flow_uncached++;
    CallEffects effects = ty_call_effects(me, callee, call);
    my flow_uncached--;
    CallEffects* stored = arena_new(&my arena, CallEffects);
    *stored = effects;
    *intern_slot(me, &my links, key, 3) = stored;
    return effects;
}

static Ty const* non_nullable_constraint_or(Checker* me, Ty const* ty) {
    Ty const* constraint = ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED) ? ty_base_constraint(me, ty) : nullptr;
    return constraint ? constraint : ty;
}

// tsc's isTypeDerivedFrom(): what `instanceof` goes by, the classes something is made of rather than its shape
static bool is_type_derived_from(Checker* me, Ty const* source, Ty const* target) {
    if (source->kind == TY_UNION) {
        for (u32 i = 0; i < source->composite.types.len; i++) {
            if (!is_type_derived_from(me, source->composite.types.data[i], target)) return false;
        }
        return true;
    }
    if (target->kind == TY_UNION) {
        for (u32 i = 0; i < target->composite.types.len; i++) {
            if (is_type_derived_from(me, source, target->composite.types.data[i])) return true;
        }
        return false;
    }
    if (source->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < source->composite.types.len; i++) {
            if (is_type_derived_from(me, source->composite.types.data[i], target)) return true;
        }
        return false;
    }
    if (source->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
        Ty const* constraint = ty_base_constraint(me, source);
        if (!constraint || constraint == source) constraint = ty_intrinsic(me, TY_UNKNOWN_);
        return is_type_derived_from(me, constraint, target);
    }
    if (is_empty_anonymous_object(me, target)) return (source->flags & (TYF_OBJECT | TYF_NON_PRIMITIVE)) != 0;
    if (target == ty_global_object_type(me)) {
        return (source->flags & (TYF_OBJECT | TYF_NON_PRIMITIVE)) && !is_empty_anonymous_object(me, source);
    }
    if (target == ty_global_function_type(me)) return (source->flags & TYF_OBJECT) && is_function_object(me, source);
    if (!(source->flags & TYF_OBJECT) || !(target->flags & TYF_OBJECT)) return false;
    return ty_has_base_type(me, source, target);
}

static bool is_strict_subtype(Checker* me, Ty const* source, Ty const* target) {
    return ty_is_related(me, source, target, TY_RELATION_STRICT_SUBTYPE);
}

static bool is_subtype(Checker* me, Ty const* source, Ty const* target) {
    return ty_is_related(me, source, target, TY_RELATION_SUBTYPE);
}

// One member of the candidate of getNarrowedType(): the members of `type` that are it, or that it is
static Ty const* narrowed_by_candidate_member(Checker* me, Ty const* type, Ty const* c, bool check_derived) {
    TyList members = type->kind == TY_UNION ? type->composite.types : (TyList){ .data = &type, .len = 1 };
    // Only the member with the candidate's key, if the union has a key property (tsc's getNarrowedTypeWorker)
    Atom const* key = ty_union_key_property(me, type);
    Ty const* discriminant = key ? ty_prop_type_of(me, c, key) : nullptr;
    Ty const* matching = discriminant ? ty_union_member_for_key(me, type, discriminant) : nullptr;
    TyList direct = matching ? (TyList){ .data = &matching, .len = 1 } : members;
    TyBuf parts = {0};
    for (u32 i = 0; i < direct.len; i++) {
        Ty const* t = direct.data[i];
        if (check_derived ? is_type_derived_from(me, t, c) : is_strict_subtype(me, t, c)) ty_buf_push(&parts, t);
        else if (check_derived ? is_type_derived_from(me, c, t) : is_strict_subtype(me, c, t)) ty_buf_push(&parts, c);
        else if (!check_derived && is_subtype(me, t, c)) ty_buf_push(&parts, t);
        else if (!check_derived && is_subtype(me, c, t)) ty_buf_push(&parts, c);
    }
    if (!parts.len) {
        // A type parameter whose constraint the candidate fits is both
        for (u32 i = 0; i < members.len; i++) {
            Ty const* t = members.data[i];
            if (!(t->flags & (TYF_TYPE_PARAM | TYF_DEFERRED))) continue;
            Ty const* constraint = ty_base_constraint(me, t);
            if (!constraint || constraint == t) constraint = ty_intrinsic(me, TY_UNKNOWN_);
            if (check_derived ? is_type_derived_from(me, c, constraint) : is_subtype(me, c, constraint)) {
                Ty const* both[2] = { t, c };
                ty_buf_push(&parts, ty_intersection(me, both, 2, nullptr, (TyList){0}));
            }
        }
    }
    Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

static bool is_not_subset_of(Checker* me, Ty const* member, void const* arg) {
    (void)me;
    return !is_type_subset_of(member, arg);
}

static bool is_not_derived_from(Checker* me, Ty const* member, void const* arg) {
    return !is_type_derived_from(me, member, arg);
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
        bool was_unknown = (type->flags & TYF_UNKNOWN) != 0;
        if (was_unknown) {
            Ty const* parts[3] = { ty_empty_object_type(me), ty_intrinsic(me, TY_NULL), ty_intrinsic(me, TY_UNDEFINED) };
            type = ty_union(me, parts, 3, nullptr, (TyList){0});
        }
        Ty const* true_type = narrowed_type(me, type, candidate, true, false);
        Ty const* rest = filter_type(me, type, is_not_subset_of, true_type);
        return was_unknown && rest == type ? ty_intrinsic(me, TY_UNKNOWN_) : rest;
    }
    if ((type->flags & (TYF_ANY | TYF_UNKNOWN)) || type == candidate) return candidate;
    TyList candidates = candidate->kind == TY_UNION ? candidate->composite.types : (TyList){ .data = &candidate, .len = 1 };
    TyBuf parts = {0};
    for (u32 i = 0; i < candidates.len; i++) {
        ty_buf_push(&parts, narrowed_by_candidate_member(me, type, candidates.data[i], check_derived));
    }
    Ty const* narrowed = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
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
    uintptr_t cached = (uintptr_t)intern_get(&my links, key, 3);
    if (cached) return cached == 2;
    Ty const* first = nullptr;
    bool non_uniform = false;
    bool has_literal = false;
    bool generic = false;
    for (u32 i = 0; i < type->composite.types.len; i++) {
        Ty const* member = type->composite.types.data[i];
        if (member->flags & (TYF_NULLABLE | TYF_VOID)) continue;
        Ty const* prop = prop_type_by_name(me, member, name);
        if (!prop) continue;
        if (!first) first = prop;
        non_uniform |= prop != first;
        has_literal |= is_literal_type(prop);
        generic |= (prop->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) != 0;
    }
    bool ret = non_uniform && has_literal && !generic;
    *intern_slot(me, &my links, key, 3) = (void const*)(uintptr_t)(ret ? 2 : 1);
    return ret;
}

// Key properties (tsc's getKeyPropertyName and mapTypesByKeyProperty): a big union of objects told apart by a unit
// property maps the property's values to its members, which narrowing and relations look up instead of trying every
// member. Stored in the links table per union: KEY_UNION_KEY_NAME and KEY_UNION_KEY_MEMBER

Ty const* ty_prop_type_of(Checker* me, Ty const* ty, Atom const* name) {
    Ty const* holder = ty->flags & (TYF_OBJECT | TYF_INTERSECTION) ? ty : ty_apparent_type(me, ty);
    Prop const* prop = ty_get_prop(me, holder, name);
    return prop ? prop_type(me, prop) : nullptr;
}

// tsc's isUnitType (unlike is_unit_type(), with computed enum members)
static bool is_key_unit(Ty const* ty) {
    return ty->kind != TY_UNION && (ty->flags & (TYF_LITERAL | TYF_ENUM | TYF_UNIQUE_ES_SYMBOL | TYF_NULLABLE));
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
        if (!discriminant) continue;
        if (!is_literal_type(discriminant)) {
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
    // A member whose value an earlier member has too doesn't count, and that value maps to no member
    bool* duplicate = calloc(members.len, sizeof(bool));
    panic_if(!duplicate, "out of memory");
    for (u32 i = 1; i < len; i++) {
        if (entries[i].literal == entries[i - 1].literal) duplicate[entries[i].member] = true;
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
            if (is_key_unit(prop_type(me, props->props[j]))) name = props->props[j]->name;
        }
    }
    return name && map_by_key_property(me, ty, name) ? name : nullptr;
}

Atom const* ty_union_key_property(Checker* me, Ty const* ty) {
    if (ty->kind != TY_UNION || ty->composite.types.len < 10) return nullptr;
    u64 key[2] = { KEY_UNION_KEY_NAME, ty->id };
    void const* cached = intern_get(&my links, key, 2);
    if (cached) return cached == ty ? nullptr : cached;
    // (While it is worked out, the union has none)
    *intern_slot(me, &my links, key, 2) = ty;
    Atom const* name = key_property_uncached(me, ty);
    if (name) *intern_slot(me, &my links, key, 2) = name;
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
    return initialized->value.tag == EXPR_PROPERTY ? &initialized->value : nullptr;
}

bool ty_is_discriminant_property(Checker* me, Ty const* type, Atom const* name) {
    return is_discriminant_property(me, type, name);
}

// tsc's getDiscriminantPropertyAccess(): `expr` if it is `reference.name` for a discriminant property, or a const alias
// of that
// TODO: destructured discriminants (`const { kind } = x`)
static PropertyExpr const* discriminant_access(FlowQuery* q, Expr const* expr, Ty const* computed) {
    Checker* me = q->me;
    if (q->declared->kind != TY_UNION && computed->kind != TY_UNION) return nullptr;
    expr = skip_reference_wrappers(expr);
    Expr const* aliased = aliased_access(expr);
    if (aliased) expr = aliased;
    if (expr->tag != EXPR_PROPERTY || !is_matching_reference(me, q->reference, &expr->property->expr)) return nullptr;
    Atom const* name = access_name(me, expr->property);
    if (!name) return nullptr;
    Ty const* type = q->declared->kind == TY_UNION && is_type_subset_of(computed, q->declared) ? q->declared : computed;
    return is_discriminant_property(me, type, name) ? expr->property : nullptr;
}

typedef Ty const* (*NarrowPropFn)(FlowQuery* q, Ty const* prop_type, void const* arg);

typedef struct DiscriminantFilter {
    Atom const* name;
    Ty const* narrowed;
} DiscriminantFilter;

static bool keep_by_discriminant(Checker* me, Ty const* member, void const* arg) {
    DiscriminantFilter const* filter = arg;
    Ty const* discriminant = prop_type_by_name(me, member, filter->name);
    if (!discriminant) discriminant = ty_intrinsic(me, TY_UNKNOWN_);
    if ((discriminant->flags & TYF_NEVER) || (filter->narrowed->flags & TYF_NEVER)) return false;
    return ty_is_comparable_either_way(me, filter->narrowed, discriminant);
}

// The members of `type` whose property could be what `narrow` leaves of the property's type
static Ty const* narrow_by_discriminant(FlowQuery* q, Ty const* type, PropertyExpr const* access, NarrowPropFn narrow, void const* arg) {
    Checker* me = q->me;
    Atom const* name = access_name(me, access);
    if (!name) return type;
    bool optional_chain = access->optional_chain;
    bool remove_nullable = compiler_options()->strict_null_checks && has_type_facts(me, type, TF_EQ_UNDEFINED_OR_NULL) &&
        (optional_chain || (access->expr.tag == EXPR_UNARY_OP && access->expr.unary_op->op == UNARY_OP_RIGHT_BANG));
    Ty const* prop = prop_type_by_name(me, remove_nullable ? type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL) : type, name);
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
    FlowQuery* q, Ty const* type, PropertyExpr const* access, BinaryOp op, Expr const* value, bool assume_true
) {
    Checker* me = q->me;
    if (op != BINARY_OP_TRIPLE_EQUAL && op != BINARY_OP_TRIPLE_NOT_EQUAL) return nullptr;
    Atom const* key = ty_union_key_property(me, type);
    if (!key || key != access_name(me, access)) return nullptr;
    Ty const* candidate = ty_union_member_for_key(me, type, ty_flow_expr_type(me, value));
    if (!candidate) return nullptr;
    if ((op == BINARY_OP_TRIPLE_EQUAL) == assume_true) return candidate;
    Ty const* candidate_key = ty_prop_type_of(me, candidate, key);
    return candidate_key && is_key_unit(candidate_key) ? filter_type(me, type, is_not_type, candidate) : type;
}

static Ty const* narrow_prop_by_facts(FlowQuery* q, Ty const* prop_type, void const* arg) {
    return type_with_facts(q->me, prop_type, *(u32 const*)arg);
}

// Conditions

static Ty const* narrow_by_truthiness(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_true) {
    Checker* me = q->me;
    if (is_matching_reference(me, q->reference, expr)) {
        return adjusted_type_with_facts(me, type, assume_true ? TF_TRUTHY : TF_FALSY);
    }
    if (compiler_options()->strict_null_checks && assume_true && optional_chain_contains_reference(me, expr, q->reference)) {
        type = adjusted_type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL);
    }
    PropertyExpr const* access = discriminant_access(q, expr, type);
    if (access) {
        u32 facts = assume_true ? TF_TRUTHY : TF_FALSY;
        return narrow_by_discriminant(q, type, access, narrow_prop_by_facts, &facts);
    }
    return type;
}

static Ty const* narrow_by_optionality(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_present) {
    Checker* me = q->me;
    if (is_matching_reference(me, q->reference, expr)) {
        return adjusted_type_with_facts(me, type, assume_present ? TF_NE_UNDEFINED_OR_NULL : TF_EQ_UNDEFINED_OR_NULL);
    }
    PropertyExpr const* access = discriminant_access(q, expr, type);
    if (access) {
        u32 facts = assume_present ? TF_NE_UNDEFINED_OR_NULL : TF_EQ_UNDEFINED_OR_NULL;
        return narrow_by_discriminant(q, type, access, narrow_prop_by_facts, &facts);
    }
    return type;
}

static bool is_comparable_to(Checker* me, Ty const* member, void const* arg) {
    Ty const* value = arg;
    return ty_is_comparable_either_way(me, member, value);
}

static bool is_coercible_kind(Ty const* ty) {
    return (ty->flags & (TYF_NUMBER | TYF_STRING | TYF_BOOLEAN_LITERAL | TYF_NUMBER_LITERAL | TYF_STRING_LITERAL)) != 0;
}

static bool is_comparable_or_coercible(Checker* me, Ty const* member, void const* arg) {
    Ty const* value = arg;
    if (ty_is_comparable_either_way(me, member, value)) return true;
    if (!is_coercible_kind(member)) return false;
    if (value->kind == TY_UNION) {
        for (u32 i = 0; i < value->composite.types.len; i++) {
            if (is_coercible_kind(value->composite.types.data[i])) return true;
        }
        return false;
    }
    return is_coercible_kind(value);
}

static bool is_not_unit_comparable_to(Checker* me, Ty const* member, void const* arg) {
    Ty const* value = arg;
    return !(is_unit_type(member) && ty_is_comparable_either_way(me, member, value));
}

typedef struct ExtractKindArg {
    Ty const* literals;
} ExtractKindArg;

static bool keep_flags(Checker* me, Ty const* ty, void const* arg) {
    (void)me;
    return (ty->flags & *(u32 const*)arg) != 0;
}

// `string` narrowed by `"a" | "b"` is `"a" | "b"`
static Ty const* map_primitive_to_literals(Checker* me, Ty const* ty, void const* arg) {
    Ty const* literals = arg;
    u32 kinds =
        ty->flags & TYF_STRING ? TYF_STRING | TYF_STRING_LITERAL :
        ty->flags & TYF_NUMBER ? TYF_NUMBER | TYF_NUMBER_LITERAL :
        ty->flags & TYF_BIGINT ? TYF_BIGINT | TYF_BIGINT_LITERAL : 0;
    if (!kinds || ty->kind != TY_INTRINSIC) return ty;
    Ty const* extracted = filter_type(me, literals, keep_flags, &kinds);
    return extracted->flags & TYF_NEVER ? ty : extracted;
}

static u32 union_flags(Ty const* ty) {
    if (ty->kind != TY_UNION) return ty->flags;
    u32 flags = 0;
    for (u32 i = 0; i < ty->composite.types.len; i++) flags |= ty->composite.types.data[i]->flags;
    return flags;
}

static Ty const* replace_primitives_with_literals(Checker* me, Ty const* with_primitives, Ty const* with_literals) {
    if ((union_flags(with_primitives) & (TYF_STRING | TYF_NUMBER | TYF_BIGINT)) &&
        (union_flags(with_literals) & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_BIGINT_LITERAL))
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

static Ty const* narrow_by_equality(FlowQuery* q, Ty const* type, BinaryOp op, Expr const* value, bool assume_true) {
    Checker* me = q->me;
    if (type->flags & TYF_ANY) return type;
    if (op == BINARY_OP_NOT_EQUAL || op == BINARY_OP_TRIPLE_NOT_EQUAL) assume_true = !assume_true;
    Ty const* value_type = ty_flow_expr_type(me, value);
    // TODO: tsc narrows by the regular type of a literal here
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
            if ((value_type->kind == TY_INTRINSIC || value_type->kind == TY_LITERAL) || is_empty_anonymous_object(me, value_type)) {
                return value_type;
            }
            if (value_type->flags & TYF_OBJECT) return ty_intrinsic(me, TY_NON_PRIMITIVE);
        }
        Ty const* filtered = filter_type(me, type, double_equals ? is_comparable_or_coercible : is_comparable_to, value_type);
        return replace_primitives_with_literals(me, filtered, value_type);
    }
    if (is_unit_type(value_type)) return filter_type(me, type, is_not_unit_comparable_to, value_type);
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
        return union_of(me,
            narrow_by_type_facts(me, type, ty_intrinsic(me, TY_NON_PRIMITIVE), TF_TYPEOF_EQ_OBJECT),
            narrow_by_type_facts(me, type, ty_intrinsic(me, TY_NULL), TF_EQ_NULL));
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
    if (!is_matching_reference(me, q->reference, target)) {
        if (compiler_options()->strict_null_checks && optional_chain_contains_reference(me, target, q->reference) &&
            assume_true == !csview_equals(name, "undefined")
        ) {
            type = adjusted_type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL);
        }
        PropertyExpr const* access = discriminant_access(q, target, type);
        if (access) {
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
    if (apparent->flags & TYF_OBJECT) {
        Members const* members = ty_members(me, apparent);
        if (members->construct_count) {
            // TODO: tsc erases the type parameters of a generic signature to any
            TyBuf results = {0};
            for (u32 i = 0; i < members->construct_count; i++) ty_buf_push(&results, signature_return_type(me, members->construct[i]));
            Ty const* ret = ty_union(me, results.data, results.len, nullptr, (TyList){0});
            ty_buf_drop(&results);
            return ret;
        }
    }
    return ty_empty_object_type(me);
}

// TODO: `[Symbol.hasInstance]` methods with a type predicate
static Ty const* narrow_by_instanceof(FlowQuery* q, Ty const* type, BinaryOpExpr const* binary, bool assume_true) {
    Checker* me = q->me;
    Expr const* left = reference_candidate(&binary->left);
    if (!is_matching_reference(me, q->reference, left)) {
        if (assume_true && compiler_options()->strict_null_checks && optional_chain_contains_reference(me, left, q->reference)) {
            return adjusted_type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL);
        }
        return type;
    }
    Ty const* right_type = ty_flow_expr_type(me, &binary->right);
    Ty const* object = ty_global_object_type(me);
    Ty const* function = ty_global_function_type(me);
    if (!object || !function || !is_type_derived_from(me, right_type, object)) return type;
    if (!is_type_derived_from(me, right_type, function)) return type;
    Ty const* instance = map_type(me, right_type, constructor_instance_type, nullptr);
    if ((type->flags & TYF_ANY) && (instance == object || instance == function)) return type;
    if (!assume_true && !((instance->flags & TYF_OBJECT) && !is_empty_anonymous_object(me, instance))) return type;
    return narrowed_type(me, type, instance, assume_true, true);
}

typedef struct PresenceArg {
    Atom const* name;
    bool assume_true;
} PresenceArg;

static bool is_presence_possible(Checker* me, Ty const* member, void const* arg) {
    PresenceArg const* a = arg;
    Ty const* apparent = ty_apparent_type(me, member);
    if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return !a->assume_true;
    FoundProp prop = ty_find_prop(me, apparent, a->name);
    if (prop.found) return (prop.flags & PROP_OPTIONAL) || a->assume_true;
    bool has_index = prop_type_by_name(me, apparent, a->name) != nullptr;
    return has_index || !a->assume_true;
}

// `"a" in x`. TODO: tsc intersects with `Record<"a", unknown>` when no member of x is known to have the property
static Ty const* narrow_by_in(FlowQuery* q, Ty const* type, BinaryOpExpr const* binary, bool assume_true) {
    Checker* me = q->me;
    Expr const* target = reference_candidate(&binary->right);
    if (!is_matching_reference(me, q->reference, target)) return type;
    if (binary->left.tag == EXPR_PRIVATE_NAME) {
        // `#x in obj` says obj is an instance of the class that declares `#x` (its constructor for a static `#x`: tsc's
        // narrowTypeByPrivateIdentifierInInExpression)
        Ty const* owner = ty_private_name_owner(me, &binary->left, nullptr);
        return owner ? narrowed_type(me, type, owner, assume_true, true) : type;
    }
    bool is_number = false;
    Atom const* name = ty_property_name_of_key(me, ty_flow_expr_type(me, &binary->left), &is_number);
    if (!name || (type->flags & TYF_ANY)) return type;
    PresenceArg known = { name, true };
    Ty const* possible = filter_type(me, type, is_presence_possible, &known);
    bool is_known = false;
    TyList members = type->kind == TY_UNION ? type->composite.types : (TyList){ .data = &type, .len = 1 };
    for (u32 i = 0; i < members.len && !is_known; i++) {
        Ty const* apparent = ty_apparent_type(me, members.data[i]);
        is_known = (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) &&
            (ty_find_prop(me, apparent, name).found || prop_type_by_name(me, apparent, name));
    }
    (void)possible;
    if (!is_known) return type;
    PresenceArg arg = { name, assume_true };
    return filter_type(me, type, is_presence_possible, &arg);
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
            if (is_matching_reference(me, q->reference, left)) return narrow_by_equality(q, type, binary->op, right, assume_true);
            if (is_matching_reference(me, q->reference, right)) return narrow_by_equality(q, type, binary->op, left, assume_true);
            if (compiler_options()->strict_null_checks) {
                if (optional_chain_contains_reference(me, left, q->reference)) {
                    type = narrow_by_optional_chain_containment(q, type, binary->op, right, assume_true);
                } else if (optional_chain_contains_reference(me, right, q->reference)) {
                    type = narrow_by_optional_chain_containment(q, type, binary->op, left, assume_true);
                }
            }
            PropertyExpr const* left_access = discriminant_access(q, left, type);
            PropertyExpr const* right_access = left_access ? nullptr : discriminant_access(q, right, type);
            if (left_access || right_access) {
                EqualityPropArg arg = { binary->op, left_access ? right : left, assume_true };
                PropertyExpr const* access = left_access ? left_access : right_access;
                Ty const* by_key = narrow_by_key_equality(q, type, access, binary->op, arg.value, assume_true);
                if (by_key) return by_key;
                return narrow_by_discriminant(q, type, access, narrow_prop_by_equality, &arg);
            }
            // TODO: `x.constructor === C`
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
    if (is_matching_reference(me, q->reference, argument)) return narrowed_type(me, type, candidate, assume_true, false);
    if (compiler_options()->strict_null_checks && optional_chain_contains_reference(me, argument, q->reference) &&
        (assume_true ? !has_type_facts(me, candidate, TF_EQ_UNDEFINED) : every_member_is_nullable(candidate))
    ) {
        type = adjusted_type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL);
    }
    PropertyExpr const* access = discriminant_access(q, argument, type);
    if (access) {
        PredicatePropArg arg = { candidate, assume_true };
        return narrow_by_discriminant(q, type, access, narrow_prop_by_predicate, &arg);
    }
    return type;
}

// Whether a call could say something about the reference: one of its arguments is it, or it is called on it
static bool has_matching_argument(FlowQuery* q, CallExpr const* call) {
    Checker* me = q->me;
    for_each (i, vec_expr, &call->args) {
        if (is_matching_reference(me, q->reference, i) || contains_matching_reference(me, q->reference, i) ||
            optional_chain_contains_reference(me, i, q->reference)
        ) {
            return true;
        }
    }
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    callee = skip_reference_wrappers(callee);
    if (callee->tag != EXPR_PROPERTY) return false;
    Expr const* object = &callee->property->expr;
    return is_matching_reference(me, q->reference, object) || contains_matching_reference(me, q->reference, object);
}

static Ty const* narrow_by_call(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_true) {
    CallExpr const* call = expr->call;
    if (call->uses_new || !has_matching_argument(q, call)) return type;
    // (An optional call that didn't happen says nothing)
    if (!assume_true && expr_is_optional_chain(expr)) return type;
    CallEffects effects = call_effects(q->me, expr, false);
    if (!effects.has_predicate || effects.predicate.asserts) return type;
    return narrow_by_predicate(q, type, &effects.predicate, call, assume_true);
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

// The declared type of a constant reference (see below), null if it isn't one
static Ty const* constant_reference_type(Checker* me, Expr const* reference, u32 depth) {
    if (depth > 8) return nullptr;
    reference = skip_reference_wrappers(reference);
    if (reference->tag == EXPR_IDENTIFIER) {
        if (reference->identifier.keyword == KEYWORD_IDENTIFIER_THIS) return ty_identifier_reference_type(me, &reference->identifier);
        Symbol const* symbol = reference->identifier.target;
        if (reference->identifier.keyword || !symbol || !(symbol->is_const || !symbol->is_assigned)) return nullptr;
        return ty_identifier_reference_type(me, &reference->identifier);
    }
    if (reference->tag != EXPR_PROPERTY) return nullptr;
    if (ty_is_import_meta(reference)) return ty_import_meta_type(me);
    Ty const* object = constant_reference_type(me, &reference->property->expr, depth + 1);
    Atom const* name = object ? access_name(me, reference->property) : nullptr;
    if (!name) return nullptr;
    // A property of a union is readonly if it is in some member
    object = ty_apparent_type(me, object);
    TyList parts = object->kind == TY_UNION ? object->composite.types : (TyList){ .data = &object, .len = 1 };
    bool readonly = false;
    for (u32 i = 0; i < parts.len && !readonly; i++) {
        FoundProp prop = ty_find_prop(me, ty_apparent_type(me, parts.data[i]), name);
        readonly = prop.found && (prop.flags & PROP_READONLY);
    }
    if (!readonly) return nullptr;
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
    if (q->me->flow_inline_level >= 5 || is_matching_reference(q->me, q->reference, condition)) return nullptr;
    Symbol const* symbol = condition->identifier.target;
    if (!symbol || !symbol->is_const || symbol->decl_count != 1 || symbol->decls[0].kind != SYMBOL_DECL_VARIABLE) return nullptr;
    Binding const* binding = symbol->decls[0].node;
    if (binding->parent || binding->has_explicit_type || !binding->source || binding->source->tag != LAZY_TYPE_INITIALIZED_BINDING) {
        return nullptr;
    }
    InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
    if (initialized->value.tag == EXPR_IMPLICIT_ANY || !is_constant_reference(q->me, q->reference)) return nullptr;
    return &initialized->value;
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
    if (member->kind == TY_INTERSECTION) {
        // `"a" & Tag`
        for (u32 i = 0; i < member->composite.types.len; i++) {
            if (is_unit_type(member->composite.types.data[i])) unit = member->composite.types.data[i];
        }
    }
    return !(is_unit_type(unit) && buf_contains(switch_types, unit));
}

static Ty const* narrow_by_switch_on_discriminant(FlowQuery* q, Ty const* type, FlowSwitchClause const* clause) {
    Checker* me = q->me;
    TyBuf switch_types = {0};
    switch_clause_types(me, clause->switch_, &switch_types);
    if (!switch_types.len) return type;
    bool has_default = clause->clause_start == clause->clause_end;
    for (u32 i = clause->clause_start; i < clause->clause_end; i++) has_default |= (switch_types.data[i]->flags & TYF_NEVER) != 0;
    // TODO: `unknown` narrowed by the cases of a switch without default
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
static Ty const* narrow_by_key_switch(FlowQuery* q, Ty const* type, PropertyExpr const* access, FlowSwitchClause const* clause) {
    Checker* me = q->me;
    if (clause->clause_start >= clause->clause_end) return nullptr;
    Atom const* key = ty_union_key_property(me, type);
    if (!key || key != access_name(me, access)) return nullptr;
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

// What is known when none of the cases outside of [start, end) matched
static u32 typeof_switch_not_equal_facts(SwitchStatement const* switch_, u32 start, u32 end) {
    u32 facts = 0;
    for_each (i, vec_switch_case, &switch_->cases) {
        if (i->is_pattern && ((u32)i_index < start || (u32)i_index >= end)) {
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
        ty_buf_push(&parts, narrow_by_type_name(me, type, cstr_sv(&switch_case_at(switch_, i)->pattern.literal.string)));
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
    Ty const* type = before.type;
    if (is_matching_reference(me, q->reference, needle)) {
        type = narrow_by_switch_on_discriminant(q, type, clause);
    } else if (is_typeof_expr(needle) && is_matching_reference(me, q->reference, &needle->unary_op->expr)) {
        type = narrow_by_switch_on_typeof(q, type, clause);
    } else if (needle->tag == EXPR_LITERAL && needle->literal.tag == VALUE_BOOLEAN && needle->literal.boolean) {
        type = narrow_by_switch_on_true(q, type, clause);
    } else {
        // `switch (x?.kind)`: in clauses whose cases are all something other than undefined, x isn't nullish (tsc's
        // narrowTypeBySwitchOptionalChainContainment)
        if (compiler_options()->strict_null_checks && clause->clause_start != clause->clause_end &&
            optional_chain_contains_reference(me, needle, q->reference)
        ) {
            TyBuf switch_types = {0};
            switch_clause_types(me, clause->switch_, &switch_types);
            bool every = true;
            for (u32 i = clause->clause_start; i < clause->clause_end; i++) {
                every &= !(switch_types.data[i]->flags & (TYF_UNDEFINED | TYF_NEVER));
            }
            ty_buf_drop(&switch_types);
            if (every) type = type_with_facts(me, type, TF_NE_UNDEFINED_OR_NULL);
        }
        PropertyExpr const* access = discriminant_access(q, needle, type);
        Ty const* by_key = access ? narrow_by_key_switch(q, type, access, clause) : nullptr;
        if (by_key) type = by_key;
        else if (access) type = narrow_by_discriminant(q, type, access, narrow_prop_by_switch, clause);
    }
    return (FlowType){ type, before.incomplete };
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
        Ty const* type = ty_regular(me, ty_flow_expr_type(me, needle));
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
    Ty const* narrowed = narrow_type(q, before.type, flow->node, assume_true, (flow->flags & FLOW_NULLISH) != 0);
    if (narrowed == before.type) return before;
    // A guard that leaves nothing of an incomplete type says control could get here once the type is complete: the
    // silent never doesn't report errors when operators are applied to it
    if (before.incomplete && (narrowed->flags & TYF_NEVER)) narrowed = ty_intrinsic(q->me, TY_SILENT_NEVER);
    return (FlowType){ narrowed, before.incomplete };
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

static void push_if_unique(TyBuf* buf, Ty const* ty) {
    for (u32 i = 0; i < buf->len; i++) {
        if (buf->data[i] == ty) return;
    }
    ty_buf_push(buf, ty);
}

static Ty const* union_at_label(FlowQuery* q, TyBuf const* types, bool subtype_reduction) {
    Checker* me = q->me;
    Ty const* result = subtype_reduction ? ty_union_subtype_reduced(me, types->data, types->len) :
        ty_union(me, types->data, types->len, nullptr, (TyList){0});
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
        if (!is_type_subset_of(branch.type, q->initial)) subtype_reduction = true;
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
            if (!is_type_subset_of(branch.type, q->initial)) subtype_reduction = true;
            seen_incomplete |= branch.incomplete;
        }
    }
    ret = (FlowType){ union_at_label(q, &types, subtype_reduction), seen_incomplete };
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
            my flow_incomplete_seen++;
            return (FlowType){ ty_union(me, entry->types->data, entry->types->len, nullptr, (TyList){0}), true };
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
        if (!is_type_subset_of(branch.type, q->initial)) subtype_reduction = true;
        if (branch.type == q->declared) break; // It can't get any wider
    }
    Ty const* result = union_at_label(q, &types, subtype_reduction);
    ty_buf_drop(&types);
    if (first.incomplete) return (FlowType){ result, true };
    *intern_slot(me, &my links, cache_key, 3) = result;
    return (FlowType){ result, false };
}

// Whether narrowing of the reference survives the function boundary: a function expression sees the narrowed type
// of what can't have changed by the time it runs (tsc's checkIdentifier())
static bool continues_outside(FlowQuery* q, FlowNode const* start, FunctionImpl const* function) {
    Expr const* reference = skip_reference_wrappers(q->reference);
    if (reference->tag != EXPR_IDENTIFIER) return false;
    if (reference->identifier.keyword == KEYWORD_IDENTIFIER_THIS) return span_valid(function->arrow_span);
    Symbol const* symbol = q->symbol;
    if (!symbol || symbol->flow_start == start) return false;
    if (symbol->is_const) return true;
    bool is_mutable_local = symbol->is_local && (symbol->flags & SYMBOL_BLOCK_SCOPED_VARIABLE);
    if (!symbol->is_parameter && !is_mutable_local) return false;
    // Past the last assignment
    return !symbol->last_assignment_end || symbol->last_assignment_end < span_start_ptr(reference->identifier.name);
}

// A call statement: an assertion narrows, after a call that never returns there is nothing. False: no effect
static bool type_at_flow_call(FlowQuery* q, FlowNode const* flow, FlowType* out) {
    Checker* me = q->me;
    Expr const* expr = flow->node;
    CallEffects effects = call_effects(me, expr, true);
    if (effects.has_predicate && effects.predicate.asserts) {
        TyPredicate const* predicate = &effects.predicate;
        FlowType before = type_at_flow_node(q, flow->antecedent);
        Ty const* narrowed = before.type;
        if (predicate->type) {
            narrowed = narrow_by_predicate(q, before.type, predicate, expr->call, true);
        } else if (!predicate->is_this && predicate->param_index >= 0 && predicate->param_index < vec_expr_size(&expr->call->args)) {
            narrowed = narrow_by_assertion(q, before.type, vec_expr_at(&expr->call->args, predicate->param_index));
        }
        *out = (FlowType){ narrowed, before.incomplete };
        return true;
    }
    if (effects.returns_never) {
        *out = (FlowType){ ty_intrinsic(me, TY_UNREACHABLE_NEVER), false };
        return true;
    }
    return false;
}

#define FLOW_MAX_DEPTH 2000

static FlowType type_at_flow_node(FlowQuery* q, FlowNode const* flow) {
    Checker* me = q->me;
    if (q->depth == FLOW_MAX_DEPTH) {
        // TODO: TS2563 "The containing function or module body is too large for control flow analysis"
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
                    return (FlowType){ my flow_shared[i].type, my flow_shared[i].incomplete };
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
            FlowLabelOverride override = { q->overrides, reduce->target, reduce->antecedents };
            q->overrides = &override;
            type = type_at_flow_node(q, flow->antecedent);
            q->overrides = override.outer;
        } else if (flags & FLOW_START) {
            FunctionImpl const* function = flow->node;
            if ((flags & FLOW_CONTINUES_OUTSIDE) && function->outer_flow && continues_outside(q, flow, function)) {
                flow = function->outer_flow;
                q->left_container = true;
                continue;
            }
            // A variable of another container than the one the reference is in is assumed to be initialized
            bool is_outer = q->left_container || (q->symbol && q->symbol->flow_start != flow);
            type = (FlowType){ is_outer ? q->declared : q->initial, false };
        } else {
            // Unreachable: errors come from the builder, the declared type keeps follow-on errors away
            type = (FlowType){ q->declared, false };
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
    if (!flow) return declared;
    Expr const* inner = skip_reference_wrappers(reference);
    FlowQuery q = {
        .me = me,
        .reference = reference,
        .symbol = inner->tag == EXPR_IDENTIFIER && !inner->identifier.keyword ? inner->identifier.target : nullptr,
        .declared = declared,
        .initial = initial,
        .shared_start = my flow_shared_count,
    };
    FlowType result = type_at_flow_node(&q, flow);
    my flow_shared_count = q.shared_start;
    // (The result of a top-level query can be marked incomplete: a shared node that was first reached through a back
    // edge is remembered with what it had then. tsc takes the type as it is, so that is what the baselines show)
    if (result.type == ty_intrinsic(me, TY_UNREACHABLE_NEVER)) return declared;
    return result.type;
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
    Ty const* false_type = ty_flow_type(me, reference, init, true_type, when_false);
    return false_type->flags & TYF_NEVER ? true_type : nullptr;
}

TyPredicate const* ty_infer_type_predicate(Checker* me, FunctionImpl const* function) {
    if (!function->has_body || function->is_type || function->is_async || function->is_generator ||
        function->is_constructor || function->accessor || function->constructed_class
    ) {
        return nullptr;
    }
    Statement const* single = nullptr;
    if (!find_single_return_in(&function->body.statements, &single) || !single) return nullptr;
    if (ty_function_end_reachability(me, function) != FLOW_REACH_NO) return nullptr;
    Expr const* expr = &single->expr;
    FlowIsolation isolation = ty_flow_isolate(me);
    TyPredicate* ret = nullptr;
    Ty const* returned = ty_expr_type(me, expr);
    if (returned->flags & TYF_BOOLEAN) {
        FlowNode* antecedent = single->flow;
        if (!antecedent) {
            antecedent = arena_new(&my arena, FlowNode);
            *antecedent = (FlowNode){ .flags = FLOW_START };
        }
        u32 index = 0;
        for_each (i, vec_function_arg, &function->args) {
            Binding const* binding = &i->initializer.binding;
            if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
            u32 at = index++;
            Symbol const* symbol = binding->tag == BINDING_IDENTIFIER ? binding->symbol : nullptr;
            if (!symbol || i->rest || symbol->is_assigned) continue;
            Ty const* init = ty_type_of_symbol(me, symbol);
            if (!init || (init->flags & TYF_BOOLEAN)) continue;
            Expr* reference = arena_new(&my arena, Expr);
            *reference = (Expr){ .tag = EXPR_IDENTIFIER, .inner_span = binding->content_span, .outer_span = binding->content_span };
            reference->identifier = (IdentifierExpr){ .name = binding->content_span, .target = symbol, .symbol = symbol };
            Ty const* refined = refined_parameter_type(me, expr, antecedent, reference, init);
            if (!refined) continue;
            ret = arena_new(&my arena, TyPredicate);
            *ret = (TyPredicate){
                .param_index = (i32)at,
                .param_name = checker_atom(me, binding->content_span.sv),
                .type = refined,
            };
            break;
        }
    }
    ty_flow_restore(me, isolation);
    return ret;
}
