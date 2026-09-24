#pragma once

#include "span.h"
#include "arena.h"

// The control flow graph (plan 05). The model is tsc's binder's, because narrowed types are in the baselines: every
// reference (identifier, `this`, property access) points at the flow node that was current when it was evaluated, and
// the checker walks *backwards* from there through the antecedents (`ty/flow.c`, not written yet). The graph is built
// by a FlowBuilder that rides along the resolve walk (`Scope.flow`) and is read-only afterwards. Nodes live in the
// binder's arena

typedef struct Expr Expr;
typedef struct Binding Binding;
typedef struct Statement Statement;
typedef struct FunctionImpl FunctionImpl;
typedef struct ForEachLoopStatement ForEachLoopStatement;
typedef struct SwitchStatement SwitchStatement;
typedef struct ObjectExprItem ObjectExprItem;
typedef struct ErrorCtx ErrorCtx;
typedef struct Symbol Symbol;
typedef struct Scope Scope;
typedef struct vec_statement VecStatement;
typedef struct FlowNode FlowNode;

// tsc's FlowFlags (no array mutations: evolving array types are out of scope, see plan 05)
typedef enum FlowFlags {
    FLOW_UNREACHABLE = 1 << 0,
    FLOW_START = 1 << 1,           // node: FunctionImpl const* if the container is a function, else null
    FLOW_BRANCH_LABEL = 1 << 2,    // antecedents
    FLOW_LOOP_LABEL = 1 << 3,      // antecedents, the first one is the way in
    FLOW_ASSIGNMENT = 1 << 4,      // node: FlowAssign const*
    FLOW_TRUE_CONDITION = 1 << 5,  // node: Expr const*
    FLOW_FALSE_CONDITION = 1 << 6, // node: Expr const*
    FLOW_SWITCH_CLAUSE = 1 << 7,   // node: FlowSwitchClause const*
    FLOW_CALL = 1 << 9,            // node: Expr const* (EXPR_CALL), a possible assertion or `never` call
    FLOW_REDUCE_LABEL = 1 << 10,   // node: FlowReduceLabel const*
    FLOW_REFERENCED = 1 << 11,     // Something has it as its antecedent
    FLOW_SHARED = 1 << 12,         // More than one thing does: worth caching in a query
    // A condition on the left of `??` / `??=` or on the object of `?.`: "is not null or undefined" rather than
    // "is truthy" (tsc looks at the parent of the condition's expression)
    FLOW_NULLISH = 1 << 13,
    // A start node of a function expression, arrow function or object literal method: narrowings of what can't have
    // changed continue in the flow the function was made in (FunctionImpl.outer_flow)
    FLOW_CONTINUES_OUTSIDE = 1 << 14,

    FLOW_LABEL = FLOW_BRANCH_LABEL | FLOW_LOOP_LABEL,
    FLOW_CONDITION = FLOW_TRUE_CONDITION | FLOW_FALSE_CONDITION,
} FlowFlags;

typedef struct FlowList {
    u32 count;
    u32 cap;
    FlowNode* items[];
} FlowList;

typedef struct FlowNode {
    u32 flags;
    u32 id; // Per file, for dumps and deterministic debugging. Caches key on the pointer
    union {
        FlowNode* antecedent;
        FlowList* antecedents; // Labels. Null: none
    };
    void const* node; // By flags, see FlowFlags
} FlowNode;

// Where the assigned type of a target inside a destructuring assignment comes from. tsc finds it through parent
// pointers, the builder records the way down instead
typedef struct FlowPatternPath {
    struct FlowPatternPath const* parent; // Null: `pattern` is assigned the root value
    Expr const* pattern; // The array or object literal the target is in
    enum {
        FLOW_PATH_ELEMENT,  // [a, b]: index
        FLOW_PATH_PROPERTY, // { a: x }, { a }: item
        FLOW_PATH_REST,     // [...a] (index: how many elements come before), { ...a }
    } kind;
    u32 index;
    ObjectExprItem const* item;
    Expr const* default_value; // `[a = 1] = e`, `{ a = 1 } = e`: null if there is none
} FlowPatternPath;

typedef enum FlowAssignKind {
    FLOW_ASSIGN_DECLARATION, // `let x = e`, an element of a destructuring declaration, a for-in/of variable: binding
    FLOW_ASSIGN_EXPR,        // `x = e`, `x += e`, `x ??= e`: assignment is the binary expression
    FLOW_ASSIGN_UPDATE,      // `x++`: assignment is the unary expression
    FLOW_ASSIGN_DELETE,      // `delete x.y`: assignment is the unary expression
    FLOW_ASSIGN_FOR_EACH,    // `for (x of xs)`
    FLOW_ASSIGN_PATTERN,     // `[x] = e`, `for ([x] of xs)`: path, and assignment or for_each as the root
} FlowAssignKind;

typedef struct FlowAssign {
    FlowAssignKind kind;
    Expr const* target; // The reference that is assigned, for reference matching. Null for declarations
    Binding const* binding; // Declarations: an identifier binding
    Expr const* assignment;
    ForEachLoopStatement const* for_each;
    FlowPatternPath const* path;
} FlowAssign;

typedef struct FlowSwitchClause {
    SwitchStatement const* switch_;
    u32 clause_start; // Clauses [start, end) lead here. An empty range: "no clause matched" of a switch without default
    u32 clause_end;
} FlowSwitchClause;

// try/finally: walking back through this node, `target` (the label before the finally block) temporarily has only
// these antecedents
typedef struct FlowReduceLabel {
    FlowNode* target;
    FlowList* antecedents;
} FlowReduceLabel;

typedef struct FlowActiveLabel {
    struct FlowActiveLabel* next;
    Span name;
    FlowNode* break_target;
    FlowNode* continue_target;
    bool referenced;
} FlowActiveLabel;

typedef struct FlowDumpEntry {
    FlowNode const* node;
    Span reference; // Valid: a reference that got `node`. Otherwise the node itself
} FlowDumpEntry;

// The statements around what is being walked, innermost first (the ones tsc's extendAssignmentPosition() looks at)
typedef struct FlowStatementFrame {
    struct FlowStatementFrame const* parent;
    Span span;
} FlowStatementFrame;

// The state of tsc's binder that is about flow, one per flow container (function, class field initializer, static
// block, namespace body, source file). Lives on the stack of whoever walks the container, reached through Scope.flow

typedef struct FlowBuilder {
    Arena* arena;
    u32* next_id;
    u32 id_counter; // What next_id points at in the builder of a file
    u32 depth;
    ErrorCtx* errs;
    FunctionImpl const* function; // The container if it is a function, for "assigned in a closure"
    // The container is the constructor of this class, or an IIFE in one (tsc's getControlFlowContainer skips those)
    Symbol const* constructor_of;
    FlowNode* start; // Of the container (an inlined IIFE: of the flow around it)
    FlowNode* current;
    // Inside a statement that is unreachable: like tsc, nothing in there gets flow nodes, reports or counts as a
    // `return`. `current` doesn't change while it is set
    bool dead;
    FlowNode* break_target;
    FlowNode* continue_target;
    FlowNode* return_target;
    FlowNode* exception_target;
    FlowNode* pre_switch_case_flow;
    FlowActiveLabel* active_labels;
    bool has_explicit_return;
    bool has_bare_return; // `return;`
    bool has_flow_effects;
    // What the next expression walked is a condition of. Whoever walks an expression takes them (expr_lookup_names()),
    // so they never leak into operands that aren't conditions themselves
    FlowNode* true_target;
    FlowNode* false_target;
    bool chain_continues; // The next expression is the object of an optional chain that isn't its root
    Binding const* binding_root; // The declaration whose binding pattern is being walked
    bool in_type_query; // Inside `typeof x` written as a type: never "used before assigned"
    bool nullish_condition; // The next expression is a condition by being the left side of `??`: FLOW_NULLISH
    // The statement list of the Block the next statement is a direct child of (TS7027 covers runs of statements there)
    VecStatement const* statement_list;
    FlowStatementFrame const* statements;
    // The next expression walked is assigned to (FlowAssignmentKind). Taken like the targets
    uint8_t assignment_target;
    bool compound_like_target; // ...by `x = x + 1` (see IdentifierExpr.compound_like_target)
    bool next_function_is_iife;
    bool ambient;
    // The `yield` expressions of the container (a generator), in source order. malloc'd, the function takes them
    struct Expr const** yields;
    u32 yield_count;
    u32 yield_cap;
    FlowDumpEntry* dump; // TTTT_DUMP_FLOW: everything that happened, in order
    u32 dump_count;
    u32 dump_cap;
} FlowBuilder;

typedef enum FlowAssignmentKind {
    FLOW_TARGET_NONE = 0,
    FLOW_TARGET_DEFINITE, // `x = e`, `x ??= e`, destructuring, for-in/of
    FLOW_TARGET_COMPOUND, // `x += e`, `x++`
} FlowAssignmentKind;

bool flow_dump_enabled(void);

extern FlowNode flow_unreachable;
extern FlowNode flow_reported_unreachable;

static inline bool flow_is_unreachable(FlowNode const* flow) { return flow->flags & FLOW_UNREACHABLE; }

// A builder for a new container. `outer` may be null (a file)
void flow_builder_init(FlowBuilder* me, FlowBuilder const* outer, Scope* scope, FunctionImpl const* function, ErrorCtx* errs);
// Prints the container's graph if TTTT_DUMP_FLOW is set
void flow_builder_finish(FlowBuilder* me, csview name, Span span);
void flow_builder_reference_dump(FlowBuilder* me, Span name);
// What references store: null in unreachable code, like tsc
static inline FlowNode* flow_builder_reference(FlowBuilder* me, Span name) {
    if (flow_dump_enabled()) flow_builder_reference_dump(me, name);
    return my dead || flow_is_unreachable(my current) ? nullptr : my current;
}
static inline void flow_set_current(FlowBuilder* me, FlowNode* flow) {
    if (!my dead) my current = flow;
}

FlowNode* flow_new_start(FlowBuilder* me, FunctionImpl const* function, bool continues_outside);
FlowNode* flow_new_branch_label(FlowBuilder* me);
FlowNode* flow_new_loop_label(FlowBuilder* me);
void flow_add_antecedent(FlowBuilder* me, FlowNode* label, FlowNode* antecedent);
// The label, its only antecedent, or unreachable
FlowNode* flow_finish_label(FlowNode* label);
FlowNode* flow_new_condition(FlowBuilder* me, u32 flag, FlowNode* antecedent, Expr const* expr);
FlowNode* flow_new_switch_clause(FlowBuilder* me, FlowNode* antecedent, SwitchStatement const* switch_, u32 start, u32 end);
FlowNode* flow_new_call(FlowBuilder* me, FlowNode* antecedent, Expr const* call);
FlowNode* flow_new_reduce_label(FlowBuilder* me, FlowNode* target, FlowList* antecedents, FlowNode* antecedent);
// Makes `current` an assignment node
void flow_assign(FlowBuilder* me, FlowAssign assign);

// Syntactic questions the builder asks (tsc's binder has the same ones)
bool expr_is_optional_chain(Expr const* me); // Part of `a?.b.c()`, from the `?.` on
bool expr_is_optional_chain_root(Expr const* me); // Has the `?.` itself
bool expr_is_narrowing(Expr const* me); // isNarrowingExpression
bool expr_is_narrowable_reference(Expr const* me);
bool expr_is_dotted_name(Expr const* me);
bool expr_is_entity_name(Expr const* me); // isEntityNameExpression: `a.b.c`, no keywords

FlowList* flow_list_concat(FlowBuilder* me, FlowList const* a, FlowList const* b, FlowList const* c);
