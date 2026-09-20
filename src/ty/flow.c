#include "ty_internal.h"
#include "../flow.h"
#include "../function_impl.h"

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
