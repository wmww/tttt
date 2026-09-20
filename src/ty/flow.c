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
// So far: reachability, the skeleton of tsc's isReachableFlowNode(). What needs types (a call of a function that
// returns `never`, an assertion of `false`, a `switch` that is exhaustive) answers FLOW_REACH_UNKNOWN until narrowing
// exists (plan 05 stage 3), and callers only act on what is certain.

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

static FlowReachability reachability_walk(Checker* me, FlowNode const* flow, FlowLabelOverride const* overrides, bool use_cache) {
    bool unknown = false; // Something on the way may or may not stop control
    while (true) {
        u32 flags = flow->flags;
        if (flags & FLOW_SHARED && use_cache && !overrides) {
            u64 key[2] = { KEY_FLOW_REACHABLE, (u64)(uintptr_t)flow };
            uintptr_t cached = (uintptr_t)intern_get(&my links, key, 2);
            FlowReachability ret = cached ? (FlowReachability)(cached - 1) : reachability_walk(me, flow, overrides, false);
            if (!cached) *intern_slot(me, &my links, key, 2) = (void const*)((uintptr_t)ret + 1);
            return ret == FLOW_REACH_YES && unknown ? FLOW_REACH_UNKNOWN : ret;
        }
        use_cache = true;
        if (flags & (FLOW_ASSIGNMENT | FLOW_CONDITION)) {
            flow = flow->antecedent;
        } else if (flags & FLOW_CALL) {
            // TODO (stage 3): unreachable if the callee is declared to return `never`, or asserts a `false` argument
            unknown = true;
            flow = flow->antecedent;
        } else if (flags & FLOW_BRANCH_LABEL) {
            FlowList const* antecedents = label_antecedents(flow, overrides);
            FlowReachability ret = FLOW_REACH_NO;
            for (u32 i = 0; antecedents && i < antecedents->count && ret != FLOW_REACH_YES; i++) {
                FlowReachability branch = reachability_walk(me, antecedents->items[i], overrides, true);
                if (branch != FLOW_REACH_NO) ret = branch;
            }
            return ret == FLOW_REACH_YES && unknown ? FLOW_REACH_UNKNOWN : ret;
        } else if (flags & FLOW_LOOP_LABEL) {
            // A loop is reachable if the way to its top is
            FlowList const* antecedents = label_antecedents(flow, overrides);
            if (!antecedents || !antecedents->count) return FLOW_REACH_NO;
            flow = antecedents->items[0];
        } else if (flags & FLOW_SWITCH_CLAUSE) {
            // TODO (stage 3): "no clause matched" is unreachable if the switch is exhaustive
            FlowSwitchClause const* clause = flow->node;
            if (clause->clause_start == clause->clause_end) unknown = true;
            flow = flow->antecedent;
        } else if (flags & FLOW_REDUCE_LABEL) {
            FlowReduceLabel const* reduce = flow->node;
            FlowLabelOverride override = { overrides, reduce->target, reduce->antecedents };
            FlowReachability ret = reachability_walk(me, flow->antecedent, &override, true);
            return ret == FLOW_REACH_YES && unknown ? FLOW_REACH_UNKNOWN : ret;
        } else {
            if (flags & FLOW_UNREACHABLE) return FLOW_REACH_NO;
            return unknown ? FLOW_REACH_UNKNOWN : FLOW_REACH_YES;
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
        ty_get_prop(me, ty, checker_atom(me, c_sv("bind"))) && ty_is_related(me, ty, function, TY_RELATION_STRICT_SUBTYPE)
    ));
}

static u32 type_facts(Checker* me, Ty const* ty, u32 mask) {
    bool strict = compiler_options()->strict_null_checks;
    u32 flags = ty->flags;
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
        bool is_false = ty == ty_intrinsic(me, TY_FALSE);
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
    if (flags & TYF_ES_SYMBOL) {
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

typedef bool (*TyPredicate)(Checker* me, Ty const* ty, void const* arg);
typedef Ty const* (*TyMapFn)(Checker* me, Ty const* ty, void const* arg);

static Ty const* filter_type(Checker* me, Ty const* ty, TyPredicate keep, void const* arg) {
    if (ty->kind != TY_UNION) {
        return (ty->flags & TYF_NEVER) || keep(me, ty, arg) ? ty : ty_intrinsic(me, TY_NEVER);
    }
    TyList members = ty->composite.types;
    TyBuf kept = {0};
    for (u32 i = 0; i < members.len; i++) {
        if (keep(me, members.data[i], arg)) ty_buf_push(&kept, members.data[i]);
    }
    Ty const* ret = kept.len == members.len ? ty : ty_union(me, kept.data, kept.len, nullptr, (TyList){0});
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

// tsc's getBaseTypeOfLiteralType()
Ty const* ty_base_type_of_literal(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_BOOLEAN_LITERAL && ty->kind != TY_UNION) return ty_intrinsic(me, TY_BOOLEAN);
    return ty_widened_literal(me, ty);
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
                return source->identifier.keyword == target->identifier.keyword &&
                    source->identifier.keyword != KEYWORD_IDENTIFIER_IMPORT;
            }
            return source->identifier.target && source->identifier.target == target->identifier.target;
        case EXPR_PROPERTY: {
            Atom const* source_name = access_name(me, source->property);
            if (!source_name || source_name != access_name(me, target->property)) return false;
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
            else if (!expr->identifier.keyword && expr->identifier.target) words[0] = (u64)(uintptr_t)expr->identifier.target;
            else return 0;
            return 1;
        case EXPR_PROPERTY: {
            u32 count = reference_key_words(me, &expr->property->expr, words, cap);
            Atom const* name = access_name(me, expr->property);
            if (!count || !name || count == cap) return 0;
            words[count] = (u64)(uintptr_t)name;
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
Ty const* ty_flow_expr_type(Checker* me, Expr const* expr) {
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

// What is left of a declared union type after `assigned` was assigned to it
static Ty const* assignment_reduced_type(Checker* me, Ty const* declared, Ty const* assigned) {
    if (declared == assigned || (assigned->flags & TYF_NEVER)) return assigned;
    Ty const* reduced = filter_type(me, declared, assigned_is_assignable, assigned);
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
                if (initialized->value.tag != EXPR_IMPLICIT_ANY) return ty_flow_expr_type(me, &initialized->value);
            }
            return ty_binding_assigned_type(me, binding);
        }
        case FLOW_ASSIGN_EXPR:
            return ty_flow_expr_type(me, &assign->assignment->binary_op->right);
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
        if (assign_is_compound(assign)) {
            FlowType before = type_at_flow_node(q, flow->antecedent);
            *out = (FlowType){ base_type_of_literal(me, before.type), before.incomplete };
            return true;
        }
        Ty const* declared = q->declared;
        if (assign->kind == FLOW_ASSIGN_EXPR && assign->assignment->binary_op->op != BINARY_OP_EQUAL) {
            declared = base_type_of_literal(me, declared); // `x ||= e`
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
        *out = (FlowType){ q->declared, false };
        return true;
    }
    return false;
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
    // TODO (stage 3): discriminant property access
    return type;
}

static Ty const* narrow_by_optionality(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_present) {
    Checker* me = q->me;
    if (is_matching_reference(me, q->reference, expr)) {
        return adjusted_type_with_facts(me, type, assume_present ? TF_NE_UNDEFINED_OR_NULL : TF_EQ_UNDEFINED_OR_NULL);
    }
    // TODO (stage 3): discriminant property access
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
    if (ty_is_related(me, a->implied, ty, TY_RELATION_STRICT_SUBTYPE)) return a->implied;
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
        // TODO (stage 3): discriminant property access
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
            // TODO (stage 3): discriminant properties, `x.constructor === C`, comparisons with `true`/`false`
            return type;
        }
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
            // TODO (stage 3): instanceof, in
            return type;
    }
}

static Ty const* narrow_type(FlowQuery* q, Ty const* type, Expr const* expr, bool assume_true, bool nullish) {
    if (nullish) return narrow_by_optionality(q, type, expr, assume_true);
    switch (expr->tag) {
        case EXPR_IDENTIFIER:
            // TODO (stage 3): aliased conditions (`const ok = typeof x === "string"`)
            if (expr->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT) return type;
            return narrow_by_truthiness(q, type, expr, assume_true);
        case EXPR_PROPERTY:
            return narrow_by_truthiness(q, type, expr, assume_true);
        case EXPR_CALL:
            return type; // TODO (stage 3): type predicates
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
    for (u32 i = 0; i < antecedents->count; i++) {
        // TODO (stage 3): the bypass branch of an exhaustive switch
        FlowType branch = type_at_flow_node(q, antecedents->items[i]);
        // Every other branch can only add subtypes of the declared type
        if (branch.type == q->declared && q->declared == q->initial) {
            ty_buf_drop(&types);
            return (FlowType){ branch.type, false };
        }
        push_if_unique(&types, branch.type);
        if (!is_type_subset_of(branch.type, q->initial)) subtype_reduction = true;
        seen_incomplete |= branch.incomplete;
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
            // TODO (stage 3): assertion functions, calls that never return
            flow = flow->antecedent;
            continue;
        } else if (flags & FLOW_CONDITION) {
            type = type_at_flow_condition(q, flow);
        } else if (flags & FLOW_SWITCH_CLAUSE) {
            // TODO (stage 3): narrowing by switch
            flow = flow->antecedent;
            continue;
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
    return result.type;
}
