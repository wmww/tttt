#pragma once

// Shared between the files of the type core

#include "ty/ty.h"
#include "several_types.h"
#include "callable_type.h"
#include "interface_member.h"
#include "interface_decl.h"
#include "function_impl.h"
#include "lazy_type.h"
#include "generic.h"
#include "expr.h"

enum {
    // First word of every key in Checker.types
    KEY_LITERAL = 1,
    KEY_UNION,
    KEY_INTERSECTION,
    KEY_REFERENCE,
    KEY_TUPLE_TARGET,
    KEY_KEYOF,
    KEY_INDEXED,
    KEY_CONDITIONAL,
    KEY_SUBSTITUTION,
    KEY_ANONYMOUS_INSTANCE,
    KEY_UNIQUE_SYMBOL,
    KEY_FRESH_LITERAL, // regular literal -> its fresh twin
    KEY_REGULAR_UNION, // union with fresh members -> ty_regular() of it
    KEY_WIDENED_OBJECTS, // type with fresh object literals -> ty_widened_objects() of it
    KEY_REGULAR_OBJECT_LITERAL, // fresh object literal -> ty_regular_object_literal() of it
    KEY_TEMPLATE_LITERAL,
    KEY_STRING_MAPPING,
    KEY_MAPPED_KEY_PARAM, // MappedInfo -> TyMapper from its key type parameter to a constrained stand-in
    KEY_INSTANTIATION_QUERY, // instantiation expression type id -> the `typeof f<T>` TypeNode that first lowered to it
    KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT, // The same, instantiated for a distributive constraint (Checker.for_constraint_root)
    KEY_CONDITIONAL_INSTANCE, // (root conditional type id, what its outer type parameters map to) -> Ty
    // First word of every key in Checker.links
    KEY_TYPE_PARAM,     // Symbol -> Ty
    KEY_SYMBOL_LINKS,   // Symbol -> SymbolLinks (node_links)
    KEY_GENERIC_PARAMS, // Generic -> TyList
    KEY_ALIAS_INSTANCE, // (alias Symbol, argument ids) -> Ty
    KEY_NODE,           // TYPE_INTERFACE/TYPE_CALLABLE node -> Ty
    KEY_NAMESPACE,      // Symbol -> Ty
    KEY_TYPE_ONLY_EXPORT, // (namespace Symbol, export Symbol) -> Prop, see ty_namespace_type_only_prop()
    KEY_MERGED_VALUE,   // Symbol -> Ty, see ty_merged_value_type()
    KEY_LITERAL_METHOD_OVERLOADS, // Last FunctionImpl of an object literal's methods of one name -> Ty, see check_object_literal()
    KEY_DEFERRED_OUTER, // TypeNode of a deferred reference -> TyConditionalOuter
    KEY_CALL_RESOLUTION, // CallExpr or tagged template Expr -> CallResolution (call.c): what its one resolution returned and reported
    KEY_DEFERRED_REFERENCE, // (TYPE_ARRAY/TYPE_TUPLE/TYPE_IDENTIFIER node, the pairs of the mapper it mentions) -> Ty
    KEY_STRUCTURAL,     // Shape of an object literal -> Ty, see ty_object_literal()
    KEY_KEYOF_PROPERTIES, // object type id -> ty_keyof() of it from its properties and index signatures (checker.c)
    KEY_SIMPLIFIED, // (indexed access type id, writing) -> ty_simplified() of it; itself while in flight (relate.c)
    KEY_NORMALIZED_INTERSECTION, // (intersection type id, writing) -> normalized_intersection() of it (relate.c)
    KEY_MAPPED_FILTERING, // declared mapped type's key parameter -> 1 remapping, 2 filtering, by relation (members.c)
    KEY_PROP_MAPPED_TYPE, // Prop with .mapped -> the mapped type it's a property of (members.c)
    KEY_PROP_WRITE_TYPE, // Prop with .write_type_known -> prop_write_type() of it, null for none (members.c)
    KEY_UNION_ACCESS, // (union type id, index type id, flags) -> access() of it at depth 0, a sentinel for none (access.c)
    KEY_BASE_CONSTRAINT, // type (node_links) -> base_constraint() of it, the type itself for none, sentinels while computed and for circular (relate.c)
    KEY_INFERRED_TRUE_BRANCH, // (conditional type id) -> inferred_true_branch() of it (relate.c, tsc's resolvedInferredTrueType)
    KEY_DISTRIBUTION_DEPENDENT, // (root conditional type id) -> 1 + ty_conditional_distribution_dependent() (conditional.c)
    KEY_DISTRIBUTIVE_CONSTRAINT, // (conditional type id) -> distributive_constraint() of it, the conditional itself for none (relate.c)
    KEY_IMPLIED_CONSTRAINT, // TernaryType (node_links) -> TyImplied of its (unary tuple-unwrapped) check and extends types (lower.c)
    KEY_RESTRICTIVE_PARAM, // type parameter id -> its unconstrained stand-in (conditional.c)
    KEY_TYPE_VARIABLE_REDUCTION, // (type variable id, primitive id) -> reduce_type_variable_intersection (checker.c)
    KEY_DEFINITELY_RELATED, // (source id, target id, which) -> 1 + ty_definitely_assignable/unassignable (conditional.c)
    KEY_CANONICAL_SIGNATURE, // Signature -> its canonical instantiation (relate.c canonical_signature())
    KEY_BASE_SIGNATURE, // Signature -> TyMapper of its type parameters to their base constraints (infer.c)
    KEY_VARIANCES,      // declared type id -> variance array
    KEY_MARKER,         // (declared type id, parameter index, which) -> marker instantiation
    KEY_ALIAS_VARIANCES, // alias Symbol -> variance array
    KEY_ALIAS_MARKER,   // type id -> non-null if it is an alias instantiation variances are measured with
    KEY_CHECK_MARKER,   // (type parameter name atom, which) -> `super-T`/`sub-T` of a variance annotation check
    KEY_CHECK_MARKER_REF, // type (node_links) -> non-null if it is a reference a variance annotation is checked with
    KEY_GENERIC_RELATION, // (relation key words) -> number of a relation between generic references (relate.c generic_relation_key())
    KEY_CONTEXT_PARAM,  // InitializedBinding (an unannotated parameter, node_links) -> Ty from the contextual signature
    KEY_CONTEXT_PARAM_UNWIDENED, // InitializedBinding -> its contextual type before a wider initializer replaced it
    KEY_CONTEXT_PARAM_PATTERN, // Binding (a parameter's pattern) -> non-null if its contextual type fell back to it
    KEY_CONTEXT_RETURN, // FunctionImpl -> contextual return type
    // FunctionImpl -> non-null: its contextual signature is its own, so its unit return type is the literal of its
    // context (see set_contextual_return())
    KEY_CONTEXT_RETURN_SELF,
    KEY_CONTEXT_IIFE_RETURN, // FunctionImpl -> the contextual type of the call that invokes it immediately
    KEY_CONTEXT_THIS,   // FunctionImpl -> the `this` type its contextual signature declares
    KEY_YIELD_CONTEXT,  // UnaryOpExpr of a `yield` -> the contextual type it was checked with (its generator's TNext)
    KEY_ACCESS_USES_CONTEXT, // Property access Expr -> itself, if its type has a type variable with a union constraint (narrowable_type())
    KEY_LITERAL_CONTEXT, // Object literal Expr -> the contextual type it is being checked with (for `this` in methods)
    KEY_PATTERN_COMPUTED, // Implied type of an object binding pattern with a computed name -> non-null
    KEY_BINDING_COMPUTED_KEY, // ObjectBindingItem -> the type of its computed name (ty_binding_computed_key_type()), or the in-flight marker
    KEY_BINDING_ACCESS_KEY, // ObjectBindingItem -> the computed key its element's type was read with (the one TS2538 is about)
    KEY_LITERAL_INFERENCE, // Object literal Expr -> the serial (cast, never 0) of the OuterInference its KEY_LITERAL_CONTEXT came from
    KEY_LITERAL_PARENT, // Object literal Expr -> the object literal it is the value of a property of
    KEY_LITERAL_ARG_OF, // Object literal Expr -> the CallExpr it is an argument of (set when the call is first resolved)
    KEY_DECL_GROUP,     // FunctionImpl of an interface member or merged function (node_links) -> TyDeclGroup
    KEY_DECL_GROUP_OF,  // Symbol, declaration (block) -> TyDeclGroup
    KEY_JSX_ATTRIBUTE,  // JsxAttribute -> the type its element's attributes checked it as (last check)
    KEY_FUNCTION,       // FunctionImpl -> Ty, see ty_function_type()
    KEY_ITERATION_TYPES, // (type id, which) -> TyIterationTypes, see iterate.c
    KEY_UNIQUE_SYMBOL_NAME, // Escaped property name atom (`__@sym@12`) -> the unique symbol type it stands for
    KEY_PRIVATE_NAME,   // (class symbol, `#x` atom) -> the class's own `#x` atom, see ty_private_name()
    KEY_PRIVATE_NAME_CLASS, // Private name atom -> its class symbol
    KEY_WIDENED_NULLABLE, // type -> ty_widened_nullable() of it
    KEY_DEFERRED_BODY,  // FunctionImpl -> non-null once its body is in Checker.deferred
    KEY_CHECKED_ONCE,   // AST node -> non-null once ty_first_check() saw it
    KEY_REFERENCED_MEMBER, // Start of a class member's declaration span -> non-null once read (noUnusedLocals)
    KEY_LITERAL_ANCHOR, // Object literal Expr -> ContextAnchor (expr.c)
    KEY_QUICK_CALLEE_CHECKED, // Assignment Expr -> non-null once flow checked its call's callee (flow.c, check_assigned_callee())
    KEY_FUNCTION_ANCHOR, // FunctionImpl -> ContextAnchor (expr.c): where its contextual type comes from, if from a call argument
    KEY_INTERSECTION_PROP_CONTEXT, // Intersection id, key type id -> what a property's contextual type is, when its type variable members distribute (expr.c's intersection_prop_context)
    KEY_UNDEFINED_PROPERTY, // Property name atom -> the Prop the first normalized-in optional `undefined` property of the name was made of
    KEY_SYNTHETIC_PROP_PARTS, // Prop (an intersection's property made of several, Prop.synthetic) -> SyntheticParts
    KEY_JS_IMPLICIT_ANY, // Expr (a JS assignment declaration's value) -> it was `[]`, made any[]; Symbol -> only null/undefined, made any
    KEY_RETURN_WIDENED_ANY, // FunctionImpl -> its return type before widening, when that widens null/undefined to any
    KEY_FUNCTION_RETURN, // FunctionImpl -> its inferred return type (FunctionReturn in function.c)
    KEY_CONTEXT_CHECKED, // FunctionImpl -> its contextual signature, decided by its first check (tsc's ContextChecked; expr.c's context_checked_none: it has none)
    KEY_IMPLEMENTATION_SIGNATURE, // Signature -> its clone with fresh type parameters (implementation_signature())
    KEY_RETURN_REPORTABLE, // FunctionImpl -> widening reportability bits decided with its return type (ty_function_decide_widening_reportable())
    KEY_FLOW_REACHABLE, // FlowNode (shared ones) -> FlowReachability + 1, see flow.c
    KEY_FLOW_REFERENCE, // (declared id, initial id, symbol | this, property names...) -> reference key id
    KEY_FLOW_EXPR,      // Expr -> Ty, see ty_flow_expr_type()
    KEY_FLOW_CALL,      // call Expr -> CallEffects as a statement's (node_links), see flow.c
    KEY_FLOW_CALL_IN_EXPR, // call Expr -> CallEffects as an expression's (node_links)
    KEY_FLOW_DISCRIMINANT, // (type id, property name) -> 1 + whether the property discriminates the union
    KEY_UNION_PAIR,     // (type id, type id) -> ty_union() of the two, without an alias
    KEY_INTERSECTION_OF, // (keep_empty_object, type ids...) -> intersection() of them, without an alias
    KEY_ASSIGNMENT_REDUCED, // (declared type id, assigned type id) -> assignment_reduced_type() (flow.c)
    KEY_REVERSE_MAPPED, // (source id, MappedInfo, operand id) -> reverse_mapped_type() at the top level (infer.c)
    KEY_UNION_KEY_NAME, // Union type id -> its key property name atom, or the union itself if it has none (flow.c)
    KEY_UNION_KEY_MEMBER, // (union type id, regular literal id) -> the member whose key property is it (unknown: several)
    KEY_FLOW_NARROWED,  // (type id, candidate id, assume_true | check_derived << 1) -> narrowed_type()
    KEY_FLOW_NOT_UNIT,  // (union id, unit type id) -> the union without the members a `!==` of the unit takes out
    KEY_FLOW_EXHAUSTIVE, // SwitchStatement -> 1 + whether it is exhaustive (2: being worked out)
    KEY_EVOLVING_ARRAY, // element type id -> the evolving array type of it (flow.c)
    KEY_AUTO_FLOW_ISSUE, // identifier Expr pointer -> the auto type its flow type was seen as (expr.c)
    KEY_FLOW_LOOP,      // (loop label FlowNode, reference key id) -> Ty at the top of the loop
    KEY_SYNTHETIC_ACCESS, // Binding (an element of an object pattern) -> Expr `init.name`, see destructured_flow_type()
    KEY_REDUCED_UNION, // union type id -> ty_reduced() of it
    KEY_SIGNATURE_INSTANCE, // (Signature, type argument ids) -> ty_signature_instantiation()
    KEY_SIGNATURE_TYPE, // Signature -> ty_signature_type()
    KEY_CONTEXT_FREE, // FunctionImpl -> its return-only type (tsc's links.contextFreeType, return_only_type())
    KEY_FUNCTION_FREE_PARAMS, // FunctionImpl (node_links) -> TyFreeParams of its annotations, see ty_function_free_params()
    KEY_FUNCTION_TYPE_MENTIONS, // function type -> FunctionMentions, see ty_function_type_mentions()
    KEY_CIRCULAR_BINDING, // Binding (or an `export default` SymbolDecl) -> non-null if its type references it (TS2502, TS7022, see ty_binding_type())
    KEY_ELEMENT_WIDENED_ANY, // binding element -> its type before widening, when that widens to any
    KEY_ARRAY_THIS_PROPS, // Array/ReadonlyArray target -> ThisProps, its members that mention `this` (members.c)
    KEY_TUPLE_NAME_ORDER, // (array type's target, prop count) -> u32 indices of a tuple's inherited props in name order (members.c)
    KEY_INDEX_NAME_ORDER, // element count -> u32 indices in the order of their index atoms (members.c)
    KEY_DECIMAL_ORDER, // element count -> u32 indices in the order of their decimal strings (members.c)
    KEY_YIELD_WIDENED_ANY, // generator FunctionImpl -> its yield type before widening, when that widens to any
    KEY_ITERATOR_REPORTED, // iterable Ty (+ async) -> non-null once its iterator's method problems were reported
    KEY_UNION_NODE,     // (union or generic reference TypeNode, ctx.generic) -> its lowered type, see lower()
    KEY_EMIT_HELPERS,   // helpers module Symbol -> EmitHelper mask already checked; SourceFile -> TS2354 said (helpers.c)
    KEY_NAMESPACE_IMPORT, // namespace import alias Symbol -> its module type (or the alias itself: none), see namespace_import_type()
    KEY_MODULE_CLONE,   // that module type -> the alias Symbol it was made for (TyAnonymous.module_clone)
    KEY_REST_SYMBOL,    // an object rest's type -> the rest binding's Symbol, its symbol to tsc (rest_literal())
    KEY_THISLESS_SIGNATURES, // raw Members of a class or interface -> ThislessSignatures (members.c)
    KEY_ACCESSIBLE_CHAIN, // (location Scope, Symbol, meaning and lookup flags) -> its accessible chain (symbol_chain.c)
    KEY_PSEUDO,         // ObjectExprItem, FunctionArg or FunctionImpl (its return type) -> its Pseudo (pseudo.c)
    KEY_EXTENDED_CONTAINERS, // (Symbol, enclosing SourceFile) -> the modules of its imports that export it (symbol_chain.c)
    KEY_MODULE_DISPLAY_NAME, // (module Symbol, file printed from, override mode, usage ModuleRef) -> its name in `import()` (csview)
    KEY_CLASS_MEMBER_SYMBOLS, // ClassDecl -> ClassMemberSymbols (check.c)
};

// The type parameters declared outside a root conditional type that it mentions (tsc's root.outerTypeParameters, less
// the ones it can't reference). Instantiation only looks at what these map to, and caches by that
typedef struct TyConditionalOuter {
    TyList params;
    TyList infers; // The `infer` type parameters its extends type declares: its own, which instantiation leaves alone
    bool unknown; // It has parts that aren't syntax (`this`, `typeof x`): every type parameter could matter
    // A Block lies between the node and its check type parameter's declaration (the parameter is declared before the
    // innermost Block it's written in): tsc's isTypeParameterPossiblyReferenced says the branches may mention it
    bool check_outside_block;
    Span node; // The conditional type node (compareTypes orders roots by its position, see order.c)
} TyConditionalOuter;
TyConditionalOuter const* ty_conditional_outer(Checker* me, Ty const* root); // lower.c, null if never lowered

// Doesn't insert, unlike intern_slot()
void const* intern_get(InternTable const* table, u64 const* words, u32 count);

// Checker.node_links: what `kind` (a KEY_ of Checker.links) says about `subject`, null if nothing. The value is held
// in the table, which moves: no slot to write later, unlike intern_slot(); set again to change it. Only for the kinds
// whose every use goes through these two (the commonest: calls, functions, shared flow nodes, object literals)
void const* ty_link_get(Checker const* me, u32 kind, void const* subject);
void ty_link_set(Checker* me, u32 kind, void const* subject, void const* value);
// Two type ids as one key word: keys made of mapper pairs are most of the interned key bytes
static inline u64 ty_id_pair(Ty const* a, Ty const* b) { return (u64)a->id << 32 | b->id; }

// alias_of: the deferred reference this is an instantiation of, if it goes by an alias name (null otherwise)
Ty const* ty_deferred_reference(
    Checker* me, TypeNode const* node, LowerCtx ctx, TyMapper const* mapper, Symbol const* alias, TyList alias_args
);
Ty const* ty_anonymous_from_overloads(Checker* me, FunctionImpl const* const* overloads, u32 count, LowerCtx ctx);


// infer.c: type inference, shared by generic calls (call.c) and `infer X` in conditional types (conditional.c)

typedef struct InferParam {
    Ty const* param;
    Ty const** co; // candidates from covariant positions
    u32 co_len;
    Ty const** contra; // candidates from contravariant positions
    u32 contra_len;
    u32 priority; // of the candidates so far (lower is better), candidates of a worse priority are ignored
    bool has_priority;
    bool top_level; // every candidate so far came from the type parameter at the top level of a parameter type
    // The seam for the context-sensitive second pass (4b3): a result that is already decided. Inference leaves it alone
    Ty const* fixed;
    // Of a rest parameter's type parameter, how many arguments it gets plus one (0: not known, a spread among them):
    // tsc's InferenceInfo.impliedArity, which splits a source tuple against `[...T, ...U]`
    u32 implied_arity_plus_one;
} InferParam;

// Flags like tsc's InferencePriority: a nested inference ORs its own in, and a higher number is a worse priority
// (a naked type variable inferred to while inferring from the return type is worse than the return type itself)
typedef enum InferPriority {
    INFER_PRIORITY_NORMAL = 0,
    INFER_PRIORITY_NAKED_TYPE_VARIABLE = 1 << 0, // to a naked type parameter in a union with other things
    INFER_PRIORITY_SUBSTITUTE_SOURCE = 1 << 2, // from a substitution type as `base & constraint`
    INFER_PRIORITY_HOMOMORPHIC_MAPPED_TYPE = 1 << 3, // T from a reverse mapped type (`{ [K in keyof T]: X<T[K]> }`)
    INFER_PRIORITY_PARTIAL_HOMOMORPHIC_MAPPED_TYPE = 1 << 4, // the same, from a source with a skipped function in it
    INFER_PRIORITY_MAPPED_TYPE_CONSTRAINT = 1 << 5, // K from the keys of the source (`{ [P in K]: X }`)
    INFER_PRIORITY_CONTRAVARIANT_CONDITIONAL = 1 << 6, // to the branches of a conditional type in a contravariant position
    INFER_PRIORITY_RETURN_TYPE = 1 << 7,
    INFER_PRIORITY_LITERAL_KEYOF = 1 << 8, // T from a string literal to `keyof T`
} InferPriority;

// The priorities whose candidates are combined (union of the covariant ones, intersection of the contravariant ones)
// rather than reduced to a common supertype or subtype: tsc's PriorityImpliesCombination
#define INFER_PRIORITY_IMPLIES_COMBINATION (INFER_PRIORITY_RETURN_TYPE | INFER_PRIORITY_MAPPED_TYPE_CONSTRAINT | INFER_PRIORITY_LITERAL_KEYOF)

#define INFER_MAX_DEPTH 24

// A (source id, target id) pair an inference walk visits, with the mode it is walked in (priority and variances:
// tsgo's InferenceKey). A zero pair is an empty slot
typedef struct VisitKey {
    u64 pair;
    u32 mode;
} VisitKey;

static inline bool visit_key_eq(VisitKey a, VisitKey b) {
    return a.pair == b.pair && a.mode == b.mode;
}

static inline u32 visit_key_hash(VisitKey k) {
    return (u32)(((k.pair ^ (u64)k.mode * 0xFF51AFD7ED558CCDull) * 0x9E3779B97F4A7C15ull) >> 32);
}

typedef struct Inference {
    InferParam* params;
    u32 len;
    u32 cap;
    bool within_relation; // Constraints are checked by the relation in progress (tsc's InferenceContext.compareTypes)
    bool any_default; // A call in a JavaScript file: what nothing is inferred for is any, not unknown (InferenceFlags.AnyDefault)
    // Type parameters of generic function arguments that the call's return type is generic in (tsc's
    // InferenceContext.inferredTypeParameters, see inference_propagate_generic()). Malloc'd
    Ty const** inferred_type_params;
    u32 inferred_type_param_count;
    // Walk state
    u32 priority;
    u32 hit_priority; // The best priority an inference was made at since it was last reset (tsc's inferencePriority)
    // How many times a better priority replaced a parameter's candidates: a non-fixing mapper cached for the candidate
    // count is stale then, though the count may be the same (see inference_count() in call.c)
    u32 replaced;
    // HACK: the types of array literal arguments, which count as literals like object literal types do (tsc marks
    // them ObjectFlags.ArrayLiteral; tttt's array types are interned without that). Malloc'd
    Ty const** array_literals;
    u32 array_literal_count;
    bool contravariant;
    // Inside the parameters of a method (tsc's bivariant): contravariant positions there give covariant candidates, and
    // that holds for the callbacks nested in them too
    bool bivariant;
    // A conditional type's `infer`: parameters are contravariant even without strictFunctionTypes (tsc's
    // InferencePriority.AlwaysStrict)
    bool always_strict;
    Ty const* original_target;
    u32 depth;
    Ty const* visiting_sources[INFER_MAX_DEPTH];
    Ty const* visiting_targets[INFER_MAX_DEPTH];
    // The (source id, target id) object pairs one infer_types() walked already, by mode (tsc's `visited`): a type
    // graph is a DAG with a lot of sharing, and walking every path to a pair is exponential. Open addressing
    VisitKey* visited;
    u32* visited_priority; // Of each pair in visited: the best priority its walk inferred at (tsc's invokeOnce status)
    u32 visited_cap;
    u32 visited_len;
    // The visited pairs whose walk was cut short by circularity: met again, they are circular too (tsc's invokeOnce
    // remembers each pair's inference priority)
    VisitKey* circular;
    u32 circular_cap;
    u32 circular_len;
    // tsc's invokeOnce: the object pairs being walked, and whether each side is expanding (the same recursion identity
    // twice, ever newer: an infinitely expanding generic). With both expanding, inference stops going deeper
    Ty const* object_sources[INFER_MAX_DEPTH];
    Ty const* object_targets[INFER_MAX_DEPTH];
    u32 object_depth;
    unsigned expanding;
    // A walk was cut short since the flag was last cleared: both sides expanding, or a pair met again while it is being
    // walked (tsc's InferencePriority.Circularity). A union target then doesn't know which sources went unmatched
    bool circularity;
    // The last mapper inference_mapper() made: it is asked for again and again with the same results while the
    // arguments are checked (~90% of webpack's mappers were repeats), and a mapper is immutable
    TyMapper const* last_mapper;
} Inference;

void inference_init(Inference* inf, TyList params);
// How many elements a tuple has before its first rest element (tsc's fixedLength)
u32 ty_tuple_fixed_length(TupleShape const* shape);
// tsc 6's compareTypes (stableTypeOrdering): the order of union members (order.c). Mostly decided by a sort key
// cached per type (ty_order_key()), equal keys by ty_compare_tie()
u64 ty_order_key(Ty const* t);
int ty_compare_tie(Ty const* a, Ty const* b, u64 key);
u32 ty_order_rank(Ty const* t);
static inline int ty_compare(Ty const* a, Ty const* b) {
    if (a == b) return 0;
    u64 ka = a->order_key, kb = b->order_key;
    if (!ka) ka = ty_order_key(a);
    if (!kb) kb = ty_order_key(b);
    if (ka != kb) return ka < kb ? -1 : 1;
    return ty_compare_tie(a, b, ka);
}
// The union of elements `from` to `arity - end_skip` of a tuple reference, rest elements by their element type, null
// if that's none (tsc's getElementTypeOfSliceOfTupleType)
// `no_reductions`: the union of the elements as a contextual type (tsc's noReductions), literals next to their primitive
Ty const* ty_tuple_slice_element_type(Checker* me, Ty const* tuple, u32 from, u32 end_skip, bool no_reductions);
// Elements `from` to `arity - end_skip` of a tuple as a mutable tuple (tsc's sliceTupleType)
bool ty_is_empty_anonymous_object(Checker* me, Ty const* ty); // tsc's isEmptyAnonymousObjectType
// ty_access() as tsc's getIndexedAccessTypeOrUndefined without an access node (constraints): a symbol key reads the
// string index signature when nothing else applies
Ty const* ty_access_without_node(Checker* me, Ty const* object, Ty const* index);
Ty const* ty_access_without_node_include_undefined(Checker* me, Ty const* object, Ty const* index, bool include_undefined);
bool ty_symbol_reads_string_index(Checker* me, Ty const* object, Ty const* index); // access.c, TS2538
Ty const* ty_access_element(Checker* me, Ty const* object, Ty const* index, bool reading); // access.c
Ty const* ty_slice_tuple(Checker* me, TupleShape const* shape, TyList args, u32 from, u32 end_skip);

// A generic function nested in a literal argument whose contextual type wants a single non-generic signature (tsc's
// instantiateTypeWithSingleGenericCallSignature, which applies at any depth of an argument): the first inference pass
// skips it (SkipGenericFunctions: it is the non-inferrable any-function), the second instantiates it in the context of
// that signature, which fixes the type parameters the signature's parameters mention (tsc's fixing mapper)
// A call of a generic function returning a function nested there is skipped by the first pass too (silent never, which
// infers nothing: tsc's resolveCallExpression under SkipGenericFunctions), and checked by the second, where it sees what
// the outer call inferred: `createMachine({ context: { count: 0 }, entry: assign(({ context }) => ...) })`
typedef struct GenericFunctionMode {
    Signature const* sig; // Null: skip them. Else the call inferring, and its inferences
    Inference* inf;
    bool skipped; // Out: one was skipped
} GenericFunctionMode;
// An argument checked like that: only object and array literals pass the mode on to their members
Ty const* ty_check_literal_argument(
    Checker* me, Expr const* expr, Ty const* contextual, bool is_const, bool skip_sensitive, GenericFunctionMode* mode
);
Ty const* ty_generic_function_in_context(Checker* me, Ty const* type, Ty const* contextual, GenericFunctionMode* mode);
GenericFunctionMode ty_outer_generic_function_mode(Checker* me); // The call inferring now's (my outer_inference)
bool ty_calls_generic_function_returning_function(Checker* me, Expr const* expr);
Ty const* ty_call_resolved_return(Checker* me, CallExpr const* call); // call.c: a remembered resolution's return, or null
void ty_record_call_anchors(Checker* me, CallExpr const* call); // expr.c
// A call's resolved signature (or overload failure candidate) once it is resolved, with what argument `index` was
// checked against if that is known (else null). False while unresolved, or when the arguments' contextual types were
// the resolved ones already (a non-generic call of one signature)
bool ty_call_resolved_candidate(Checker* me, CallExpr const* call, u32 index, Signature const** sig, TyMapper const** mapper, Ty const** param_type); // call.c
bool ty_call_resolution_in_flight(Checker* me, CallExpr const* call); // call.c
Ty const* ty_mapped_template_fresh(Checker* me, MappedInfo const* mapped); // relate.c
// tsc's isDeeplyNestedType (relate.c): `ty` and at least two types on the stack with the same recursion identity
bool ty_is_deeply_nested(Checker* me, Ty const* ty, Ty const* const* stack, u32 depth, u32 max); // tsc's maxDepth: 3 for relations, 2 for inference
// What a type on those stacks is compared by: its recursion identity, an instantiated mapped type's modifiers type's
// (tsc's getMappedTargetWithSymbol), an intersection itself tagged with bit 0 (relate.c)
uintptr_t ty_recursion_entry(Checker* me, Ty const* ty);
void inference_drop(Inference* inf);
void infer_types(Checker* me, Inference* inf, Ty const* source, Ty const* target, u32 priority);
// What a call infers for parameter i: common supertype of the candidates, widened unless `keep_literals`. Null if
// there were no candidates
Ty const* inference_call_result(Checker* me, Inference* inf, u32 i, bool keep_literals);
Ty const* inference_call_result_fallback(Checker* me, Inference* inf, u32 i, bool keep_literals, Ty const** fallback);
// What a conditional type infers: union of the covariant candidates, else intersection of the contravariant ones
Ty const* inference_conditional_result(Checker* me, Inference* inf, u32 i);
// What `inf` (over sig's type parameters) has inferred for them: call results, then defaults, then unknown; one that
// doesn't satisfy its constraint is the constraint. `fixing`: for the contextual type of a context-sensitive argument
TyMapper const* inference_mapper(Checker* me, Signature const* sig, Inference* inf, bool fixing);
TyMapper const* inference_mapper_no_default(Checker* me, Signature const* sig, Inference* inf);
// `sig` with its type parameters replaced by `args` and dropped (tsc's getSignatureInstantiationWithoutFillingIn
// TypeArguments), memoised
Signature const* ty_signature_instantiation(Checker* me, Signature const* sig, TyList args);
// The object type whose only member is `sig` (tsc's getOrCreateTypeFromSignature), one per signature
Ty const* ty_signature_type(Checker* me, Signature const* sig);
// tsc's instantiateSignatureInContextOf: `sig`'s type parameters inferred from the parameter types of `contextual`
// (instantiated with `context_mapper`, the inference of an enclosing call, if any), without one also from its return
// type
Signature const* ty_instantiate_signature_in_context_of(
    Checker* me, Signature const* sig, Signature const* contextual, TyMapper const* context_mapper
);
// tsc's higher-order inference (the first branch of instantiateTypeWithSingleGenericCallSignature): a generic function
// `sig` passed where the non-generic `contextual` is wanted (in terms of the type parameters `inf` infers) is
// instantiated with its own type parameters (renamed `T_1` on a clash with ones already propagated), and what that
// says about the contextual signature's type parameters is adopted if they have no inferences yet. The instantiated
// signature if so (its type parameters are then in inf->inferred_type_params), else null
Signature const* inference_propagate_generic(Checker* me, Inference* inf, Signature const* sig, Signature const* contextual);
// `sig` (resolved) with other type parameters: tsc's cloneSignature + typeParameters, how a call returns a generic
// function (getSignatureInstantiation with inferredTypeParameters)
Signature const* ty_signature_with_type_params(Checker* me, Signature const* sig, TyList type_params);

// relate.c
// Assignability asked from inside the relation in progress, on its state (its assumptions hold, so a recursive
// question ends); a plain ty_is_related() outside of one
bool ty_is_related_within(Checker* me, Ty const* source, Ty const* target);
Ty const* ty_global_regexp_type(Checker* me);
Ty const* ty_global_object_type(Checker* me); // Null without a lib
bool ty_is_numeric_name(Atom const* name); // relate.c, tsc's isNumericLiteralName: the name is what its number prints as
// Whether these properties of the parts of an intersection or union are more than one: not all the same, nor
// instantiations of the same one with the same type (tsc's createUnionOrIntersectionProperty's singleProp)
bool ty_props_distinct(Checker* me, Prop const* const* props, u32 len);
typedef struct FoundProp {
    Ty const* type;
    u32 flags;
    bool found;
    bool synthetic; // An intersection's that several members have (tsc's synthetic symbol)
    Prop const* prop; // The property found (of an intersection: of its first member that has one)
} FoundProp;
FoundProp ty_find_prop(Checker* me, Ty const* ty, Atom const* name);
bool ty_prop_reference_private(Checker* me, Prop const* prop); // members.c
FoundProp ty_find_own_prop(Checker* me, Ty const* ty, Atom const* name);
// tsc's isGenericReducibleType (checker.c), part of its isGenericObjectType
bool ty_is_generic_reducible(Checker* me, Ty const* ty);
// The annotation a property was declared with, which the types baseline reuses for a copy of it where it still says
// the same (ObjectLiteralProp.asserted). Null if none (lower.c)
TypeNode const* ty_prop_annotation(Prop const* prop);
Ty const* ty_string_index_only_key(Checker* me, Ty const* object, Ty const* index); // access.c
Ty const* ty_constructor_declared_flow_type(Checker* me, Symbol const* member, Expr const* reference, struct FlowNode const* flow); // lower.c
// A CommonJS export with several declarations, every one `exports.x = <alias>` (tsc's isDuplicatedCommonJSExport of an
// Alias symbol, which getTypeOfAlias types autoType). lower.c
bool ty_is_duplicated_commonjs_alias_export(Symbol const* symbol);
Ty const* ty_named_union_of_reduced(Checker* me, Ty const* const* inputs, u32 len, Ty const* const* members, u32 count);
bool ty_is_spreadable_prop(Checker* me, Prop const* prop); // expr.c, tsc's isSpreadableProperty
// A definitely non-nullable source against `T | undefined` (or `| null`, or both) is related to T, which errors name
// too (tsc's isRelatedTo). Else `target`
Ty const* ty_strip_nullable_target(Checker* me, Ty const* source, Ty const* target);
// The target an assignability error names, as tsc's isRelatedTo normalizes it (getNormalizedType: simplified indexed
// accesses and conditionals, substitution bases, generic tuples normalized, then the nullable part stripped) unless it
// has an alias (reportErrorResults)
Ty const* ty_report_source(Checker* me, Ty const* source);
// tsc's getNormalizedTupleType: a tuple with a generic spread's simplifiable elements simplified
Ty const* ty_normalized_generic_tuple(Checker* me, Ty const* ty, bool writing);
Ty const* ty_access_write_type(Checker* me, Ty const* object, Ty const* index); // access.c
// Where an interface's method or call signature was declared: tsc's signature.declaration symbol and parent, which
// overload resolution orders by (reorder_candidates)
typedef struct TyDeclGroup {
    Symbol const* symbol; // The interface
    void const* declaration; // Which of its declarations
} TyDeclGroup;
TyDeclGroup const* ty_decl_group(Checker* me, FunctionImpl const* function);
Ty const* ty_rest_element_type(Checker* me, Ty const* rest);
// Type parameter `index` of a generic declared type was measured as contravariant
bool ty_type_param_is_contravariant(Checker* me, Ty const* target, u32 index);
bool ty_alias_param_is_contravariant(Checker* me, Symbol const* alias, u32 index);
Ty const* ty_alias_instance(Checker* me, Symbol const* decl, TyList args);
Signature const* ty_erased_signature(Checker* me, Signature const* sig); // relate.c, tsc's getErasedSignature
TyList ty_alias_type_params(Checker* me, Symbol const* decl);
bool ty_conditional_distribution_dependent(Checker* me, Ty const* conditional);
bool ty_elides_default_type_args(Checker* me, Ty const* target); // iterate.c
bool ty_is_tuple(Ty const* ty); // Not the empty tuple, which is its own target (TY_DECLARED)
bool ty_is_generic_tuple(Checker* me, Ty const* ty); // checker.c, tsc's isGenericTupleType
bool ty_tuple_node_has_variadic(TypeNode const* node); // lower.c: a rest element not written as an array
// inner, then outer: what inner maps to gets instantiated by outer, and outer's own mappings are carried along
TyMapper const* ty_compose_mappers(Checker* me, TyMapper const* inner, TyMapper const* outer); // instantiate.c
TyMapper const* ty_mapper_with(Checker* me, TyMapper const* mapper, Ty const* source, Ty const* target); // instantiate.c
// The mapper without the mappings of params (which the instantiated type binds itself)
TyMapper const* ty_mapper_without(Checker* me, TyMapper const* mapper, TyList params); // instantiate.c
MappedInfo const* ty_mapped_info_instantiate(Checker* me, MappedInfo const* source, TyMapper const* mapper); // members.c
// tsc's isTypeDerivedFrom(): what `instanceof` goes by, the classes something is made of rather than its shape
bool ty_is_type_derived_from(Checker* me, Ty const* source, Ty const* target); // flow.c
// tsc's isMutableArrayOrTuple: an Array<T> or a tuple that isn't readonly
bool ty_is_mutable_array_or_tuple(Checker* me, Ty const* ty);
// tsgo's invocationErrorRecovery: TS7038 on a call or `new` of a namespace import whose `export =` value would do
typedef struct Error Error;
void ty_add_originating_import_invocation(Checker* me, Error* error, Expr const* callee, bool construct); // check.c
