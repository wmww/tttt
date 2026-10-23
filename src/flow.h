#pragma once

#include "span.h"
#include "arena.h"

// The control flow graph (plan 05). The model is tsc's binder's, because narrowed types are in the baselines: every
// reference (identifier, `this`, property access) points at the flow node that was current when it was evaluated, and
// the checker walks *backwards* from there through the antecedents (`ty/flow.c`, not written yet). The graph is built
// by a FlowBuilder that rides along the resolve walk (`Scope.flow`) and is read-only afterwards. Nodes live in the
// binder's arena

typedef struct Expr Expr;
typedef struct CallExpr CallExpr;
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
typedef struct SourceFile SourceFile;

// tsc's FlowFlags
typedef enum FlowFlags {
    FLOW_UNREACHABLE = 1 << 0,
    FLOW_START = 1 << 1,           // node: FunctionImpl const* if the container is a function, else null
    FLOW_BRANCH_LABEL = 1 << 2,    // antecedents
    FLOW_LOOP_LABEL = 1 << 3,      // antecedents, the first one is the way in
    FLOW_ASSIGNMENT = 1 << 4,      // node: FlowAssign const*
    FLOW_TRUE_CONDITION = 1 << 5,  // node: Expr const*
    FLOW_FALSE_CONDITION = 1 << 6, // node: Expr const*
    FLOW_SWITCH_CLAUSE = 1 << 7,   // node: FlowSwitchClause const*
    FLOW_ARRAY_MUTATION = 1 << 8,  // node: Expr const*, `x.push(...)`/`x.unshift(...)` (EXPR_CALL) or `x[i] = v` (`=`)
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
    // `[[a = 1]] = e`, `{ b: { c: a = 1 } } = e`: an `a = 1` in a pattern that isn't itself the assignment's target is
    // a plain assignment of the default to tsc (getAssignedTypeOfBinaryExpression's isDestructuringAssignmentTarget
    // looks one level up only): `a` is assigned 1's type
    bool plain_default;
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
    // Inside what tsc's markNodeAssignments doesn't walk (an enum, a type node: a class's `extends` expression, an
    // instantiation expression `f<T>`, a type literal's computed key, a type query): assignments there don't count for
    // narrowing in closures (identifier_mark_assigned())
    u32 unmarked_assignments;
    bool callee_of_call; // The expression walked next is a call's callee (whose type arguments are the call's)
    Arena* arena;
    u32* next_id;
    u32 id_counter; // What next_id points at in the builder of a file
    u32 depth;
    // Inside a call expression, or the initializer of a declaration with a type annotation (not counting a function's
    // own), however deep: tsc's isContextuallyTyped (expressionToTypeNode.ts), see FunctionImpl.syntactically_contextual
    u32 contextual_depth;
    // The context root of what is being walked (FunctionImpl.context_root), 0 outside one: set per statement and
    // initializer, a function's body starts without one
    uintptr_t context_root;
    // The expression being walked is in a const context as far as syntax goes: the nearest ancestor that isn't an
    // object or array literal, a spread or a prefix `-`/`+` is `as const` (tsgo's pseudochecker IsInConstContext)
    bool const_context;
    ErrorCtx* errs;
    FunctionImpl const* function; // The container if it is a function, for "assigned in a closure"
    // What tsc's getControlFlowContainer is (a function that isn't an IIFE, a namespace body, a field's initializer, the
    // file; not a static block), only compared by identity
    void const* container;
    // The container is the constructor of this class, or an IIFE in one (tsc's getControlFlowContainer skips those)
    Symbol const* constructor_of;
    FlowNode* start; // Of the container (an inlined IIFE: of the flow around it)
    // The start of tsc's getControlFlowContainer: `start`, except in an async or generator IIFE, which has a start of
    // its own (it isn't inlined) but whose container is the one around it. What declarations record as
    // Symbol.flow_start and outer references compare with
    FlowNode* container_start;
    FlowNode* current;
    // Inside a statement that is unreachable: like tsc, nothing in there gets flow nodes, reports or counts as a
    // `return`. `current` doesn't change while it is set
    bool dead;
    // Inside a range of unreachable code (TS7027), however deep, except in function expressions: no other TS7027 (tsc's
    // withinUnreachableCode, the checker's walk)
    bool within_unreachable;
    FlowNode* break_target;
    FlowNode* continue_target;
    FlowNode* return_target;
    FlowNode* exception_target;
    FlowNode* pre_switch_case_flow;
    FlowActiveLabel* active_labels;
    bool has_explicit_return;
    bool has_bare_return; // `return;`
    bool has_flow_effects;
    SourceFile* file; // Null outside a file (SourceFile.flow_unresolved_names)
    // What the next expression walked is a condition of. Whoever walks an expression takes them (expr_lookup_names()),
    // so they never leak into operands that aren't conditions themselves
    FlowNode* true_target;
    FlowNode* false_target;
    bool chain_continues; // The next expression is the object of an optional chain that isn't its root
    Binding const* binding_root; // The declaration whose binding pattern is being walked
    // The variable declaration whose initializer (or for-in/of expression) is being walked: using it there is using it
    // before its declaration (TS2448), also in an IIFE
    struct InitializedBinding const* initializing;
    // The parameter (or element of a parameter's pattern) whose initializer or pattern is being walked, and the scope
    // of its function: it can't use itself (TS2372) or what that scope declares after it (TS2373)
    Binding const* parameter_initializing;
    Scope const* parameter_function_scope;
    // Evaluated with the class: a field's initializer or a `static {}` block's expression statement (tsc's
    // isInPropertyInitializerOrClassStaticBlock), where a property can be read before it is initialized (TS2729).
    // Functions (and the computed names of methods, which are in them for tsc) don't count
    uint8_t initializer_context; // InitializerContext
    // Inside an object literal, which is a binder container of its own for tsc: an assignment `f.x = v` in there
    // declares no expando (scope_add_nested_expando())
    u32 object_literal_depth;
    struct ClassItem const* initializer_field; // The field whose initializer it is
    bool in_type_query; // Inside `typeof x` written as a type: never "used before assigned"
    // Inside the computed name of a method, accessor or class property: tsc's getControlFlowContainer is that member,
    // so every variable read there is an outer one (assumed initialized unless never assigned)
    bool member_name;
    // ...of a method or accessor (not a property): tsc's getContainingFunction is that member, so a `yield` there
    // belongs to no generator as far as its type goes (any, no TS7057), though the generator still yields it
    bool method_name;
    // The identifier of `export = x`: in_type_query is set for it (it isn't used before its declaration, the emit moves
    // it), but tsc's checkExportAssignment still checks it as an expression: "used before being assigned" applies
    bool export_equals_name;
    // Where a name imported with `import type` may be used (tsc's isValidTypeOnlyAliasUseSite, besides type queries): the
    // identifier of `export default x`/`export = x`, computed names of abstract and `declare` members
    bool type_only_alias_ok;
    bool nullish_condition; // The next expression is a condition by being the left side of `??`: FLOW_NULLISH
    // The statement list of the Block the next statement is a direct child of (TS7027 covers runs of statements there)
    VecStatement const* statement_list;
    // The statement list the next statement is directly in, whatever kind of block (a file, a namespace body)
    VecStatement const* lookup_statements;
    FlowStatementFrame const* statements;
    // The next expression walked is assigned to (FlowAssignmentKind). Taken like the targets
    uint8_t assignment_target;
    bool compound_like_target; // ...by `x = x + 1` (see IdentifierExpr.compound_like_target)
    bool read_target; // ...and read as well (`x ??= e`, `x! = e`, `[...x] = e`: tsc's accessKind isn't Write)
    bool next_function_is_iife;
    bool ambient;
    // The `yield` expressions of the container (a generator), in source order. malloc'd, the function takes them
    struct Expr const** yields;
    u32 yield_count;
    u32 yield_cap;
    // The PropertyContext the last property access walked got (in arena): the next one in the same place shares it
    struct PropertyContext const* property_context;
    FlowDumpEntry* dump; // TTTT_DUMP_FLOW: everything that happened, in order
    u32 dump_count;
    u32 dump_cap;
} FlowBuilder;

typedef enum InitializerContext : uint8_t {
    INITIALIZER_NONE,
    INITIALIZER_INSTANCE_FIELD,
    INITIALIZER_STATIC_FIELD,
    INITIALIZER_STATIC_BLOCK,
    // The computed name of an object literal's method or accessor in a field's initializer: only reading that field
    // is too early there
    INITIALIZER_MEMBER_NAME,
    // A decorator of a field: inside the property declaration as far as tsc's isInPropertyInitializerOrClassStaticBlock
    // goes, but nothing there is deferred (isUsedInFunctionOrInstanceProperty doesn't stop at a decorator)
    INITIALIZER_FIELD_DECORATOR,
} InitializerContext;

typedef enum FlowAssignmentKind {
    FLOW_TARGET_NONE = 0,
    FLOW_TARGET_DEFINITE, // `x = e`, `x ??= e`, destructuring, for-in/of
    FLOW_TARGET_COMPOUND, // `x += e`, `x++`
} FlowAssignmentKind;

bool flow_dump_enabled(void);

extern FlowNode flow_unreachable;
extern FlowNode flow_reported_unreachable;

static inline bool flow_is_unreachable(FlowNode const* flow) { return flow->flags & FLOW_UNREACHABLE; }
/// Every way to `flow` from the start of its container goes through a `super(...)` call (tsc's isPostSuperFlowNode)
bool flow_is_post_super(FlowNode const* flow);

// A builder for a new container. `outer` may be null (a file)
void flow_builder_init(FlowBuilder* me, FlowBuilder const* outer, Scope* scope, FunctionImpl const* function, ErrorCtx* errs);
// Prints the container's graph if TTTT_DUMP_FLOW is set
void flow_builder_finish(FlowBuilder* me, csview name, Span span);
void flow_builder_reference_dump(FlowBuilder* me, Span name);
// A PropertyContext equal to `context` that lives as long as the arena: the last one made if it is the same
struct PropertyContext const* flow_builder_property_context(FlowBuilder* me, struct PropertyContext const* context);
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
// After a call or an `=` that isn't itself an assignment target: `x.push(v)`, `x.unshift(v)` and `x[i] = v` mark x as
// an evolving array operation target (IdentifierExpr.evolving_op), and get an array mutation node (tsc's
// createFlowMutation) if x may be an auto-typed variable
void flow_array_mutation(FlowBuilder* me, Expr* expr);
void flow_call_argument_targets(CallExpr* call);
// `x.length` (any property access): marks x as an evolving array operation target
void flow_length_access(Expr* access);
bool expr_is_dotted_name(Expr const* me);
bool expr_is_entity_name(Expr const* me); // isEntityNameExpression: `a.b.c`, no keywords
// tsc's checkGrammarForInvalidDynamicName: a computed key that is neither syntactically a literal nor an entity name
// (which is bound late by its literal type, or else is an implied index signature). Its type doesn't matter
bool expr_is_non_bindable_dynamic_name(Expr const* key);

FlowList* flow_list_concat(FlowBuilder* me, FlowList const* a, FlowList const* b, FlowList const* c);
