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
    KEY_ANONYMOUS_INSTANCE,
    KEY_UNIQUE_SYMBOL,
    KEY_FRESH_LITERAL, // regular literal -> its fresh twin
    KEY_REGULAR_UNION, // union with fresh members -> ty_regular() of it
    KEY_WIDENED_OBJECTS, // type with fresh object literals -> ty_widened_objects() of it
    KEY_TEMPLATE_LITERAL,
    KEY_STRING_MAPPING,
    KEY_CONDITIONAL_INFERS, // root conditional type id -> TyList* of the `infer` type parameters its extends type declares
    // First word of every key in Checker.links
    KEY_TYPE_PARAM,     // Symbol -> Ty
    KEY_SYMBOL_LINKS,   // Symbol -> SymbolLinks
    KEY_GENERIC_PARAMS, // Generic -> TyList
    KEY_ALIAS_INSTANCE, // (alias Symbol, argument ids) -> Ty
    KEY_NODE,           // TYPE_INTERFACE/TYPE_CALLABLE node -> Ty
    KEY_NAMESPACE,      // Symbol -> Ty
    KEY_MERGED_VALUE,   // Symbol -> Ty, see ty_merged_value_type()
    KEY_DEFERRED_REFERENCE, // (TYPE_ARRAY/TYPE_TUPLE node, mapper pairs) -> Ty
    KEY_STRUCTURAL,     // Shape of an object literal -> Ty, see ty_object_literal()
    KEY_VARIANCES,      // declared type id -> variance array
    KEY_MARKER,         // (declared type id, parameter index, which) -> marker instantiation
    KEY_CONTEXT_PARAM,  // InitializedBinding (an unannotated parameter) -> Ty from the contextual signature
    KEY_CONTEXT_RETURN, // FunctionImpl -> contextual return type
    KEY_CONTEXT_THIS,   // FunctionImpl -> the `this` type its contextual signature declares
    KEY_LITERAL_CONTEXT, // Object literal Expr -> the contextual type it is being checked with (for `this` in methods)
    KEY_LITERAL_PARENT, // Object literal Expr -> the object literal it is the value of a property of
    KEY_FUNCTION,       // FunctionImpl -> Ty, see ty_function_type()
    KEY_ITERATION_TYPES, // (type id, which) -> TyIterationTypes, see iterate.c
    KEY_UNIQUE_SYMBOL_NAME, // Escaped property name atom (`__@sym@12`) -> the unique symbol type it stands for
    KEY_FUNCTION_RETURN, // FunctionImpl -> its inferred return type (FunctionReturn in function.c)
    KEY_CONTEXT_DONE,   // FunctionImpl -> non-null once its contextual signature was assigned (it's assigned once)
    KEY_FLOW_REACHABLE, // FlowNode (shared ones) -> FlowReachability + 1, see flow.c
    KEY_FLOW_REFERENCE, // (declared id, initial id, symbol | this, property names...) -> reference key id
    KEY_FLOW_EXPR,      // Expr -> Ty, see ty_flow_expr_type()
    KEY_FLOW_CALL,      // (call Expr, is a statement) -> CallEffects, see flow.c
    KEY_FLOW_DISCRIMINANT, // (type id, property name) -> 1 + whether the property discriminates the union
    KEY_FLOW_EXHAUSTIVE, // SwitchStatement -> 1 + whether it is exhaustive (2: being worked out)
    KEY_FLOW_LOOP,      // (loop label FlowNode, reference key id) -> Ty at the top of the loop
    KEY_SYNTHETIC_ACCESS, // Binding (an element of an object pattern) -> Expr `init.name`, see destructured_flow_type()
    KEY_DISCRIMINANT_CONFLICT, // (intersection member ids) -> 1 + whether they have conflicting discriminants
    KEY_SIGNATURE_INSTANCE, // (Signature, type argument ids) -> ty_signature_instantiation()
    KEY_SIGNATURE_TYPE, // Signature -> ty_signature_type()
};

// Doesn't insert, unlike intern_slot()
void const* intern_get(InternTable const* table, u64 const* words, u32 count);

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
} InferParam;

typedef enum InferPriority {
    INFER_PRIORITY_NORMAL = 0,
    INFER_PRIORITY_NAKED_TYPE_VARIABLE = 1, // to a naked type parameter in a union with other things
    INFER_PRIORITY_HOMOMORPHIC_MAPPED_TYPE = 2, // T from a reverse mapped type (`{ [K in keyof T]: X<T[K]> }`)
    INFER_PRIORITY_MAPPED_TYPE_CONSTRAINT = 3, // K from the keys of the source (`{ [P in K]: X }`)
    INFER_PRIORITY_RETURN_TYPE = 4,
} InferPriority;

#define INFER_MAX_DEPTH 24

typedef struct Inference {
    InferParam* params;
    u32 len;
    u32 cap;
    bool collect_infer_params; // conditional types: every `infer X` met in the target becomes a parameter
    bool within_relation; // Constraints are checked by the relation in progress (tsc's InferenceContext.compareTypes)
    // Type parameters of generic function arguments that the call's return type is generic in (tsc's
    // InferenceContext.inferredTypeParameters, see inference_propagate_generic()). Malloc'd
    Ty const** inferred_type_params;
    u32 inferred_type_param_count;
    // Walk state
    u32 priority;
    u32 hit_priority; // The best priority an inference was made at since it was last reset (tsc's inferencePriority)
    // HACK: the types of array literal arguments, which count as literals like object literal types do (tsc marks
    // them ObjectFlags.ArrayLiteral; tttt's array types are interned without that). Malloc'd
    Ty const** array_literals;
    u32 array_literal_count;
    bool contravariant;
    Ty const* original_target;
    u32 depth;
    Ty const* visiting_sources[INFER_MAX_DEPTH];
    Ty const* visiting_targets[INFER_MAX_DEPTH];
    // (source id, target id) of the object pairs one infer_types() walked already (tsc's `visited`): a type graph
    // is a DAG with a lot of sharing, and walking every path to a pair is exponential. Open addressing, 0 is empty
    u64* visited;
    u32 visited_cap;
    u32 visited_len;
    // tsc's invokeOnce: the object pairs being walked, and whether each side is expanding (the same recursion identity
    // three times, ever newer: an infinitely expanding generic). With both expanding, inference stops going deeper
    Ty const* object_sources[INFER_MAX_DEPTH];
    Ty const* object_targets[INFER_MAX_DEPTH];
    u32 object_depth;
    unsigned expanding;
} Inference;

void inference_init(Inference* inf, TyList params);
// tsc's isDeeplyNestedType (relate.c): `ty` and at least two types on the stack with the same recursion identity
bool ty_is_deeply_nested(Ty const* ty, Ty const* const* stack, u32 depth);
void inference_drop(Inference* inf);
void inference_collect_infer_params(Checker* me, Inference* inf, Ty const* target);
void infer_types(Checker* me, Inference* inf, Ty const* source, Ty const* target, u32 priority);
// What a call infers for parameter i: common supertype of the candidates, widened unless `keep_literals`. Null if
// there were no candidates
Ty const* inference_call_result(Checker* me, Inference* inf, u32 i, bool keep_literals);
// What a conditional type infers: union of the covariant candidates, else intersection of the contravariant ones
Ty const* inference_conditional_result(Checker* me, Inference* inf, u32 i);
// What `inf` (over sig's type parameters) has inferred for them: call results, then defaults, then unknown; one that
// doesn't satisfy its constraint is the constraint. `fixing`: for the contextual type of a context-sensitive argument
TyMapper const* inference_mapper(Checker* me, Signature const* sig, Inference* inf, bool fixing);
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
Ty const* ty_global_function_type(Checker* me); // Null without a lib
Ty const* ty_global_object_type(Checker* me); // Null without a lib
Ty const* ty_empty_object_type(Checker* me); // `{}`
// The property name a string or number literal type stands for, null for other types
Atom const* ty_literal_name(Checker* me, Ty const* literal);
// The property name a key type stands for (a string or number literal), null if none
Atom const* ty_property_name_of_key(Checker* me, Ty const* key, bool* is_number);
typedef struct FoundProp {
    Ty const* type;
    u32 flags;
    bool found;
} FoundProp;
FoundProp ty_find_prop(Checker* me, Ty const* ty, Atom const* name);
Ty const* ty_rest_element_type(Checker* me, Ty const* rest);
// Type parameter `index` of a generic declared type was measured as contravariant
bool ty_type_param_is_contravariant(Checker* me, Ty const* target, u32 index);
bool ty_elides_default_type_args(Checker* me, Ty const* target); // iterate.c
bool ty_is_tuple(Ty const* ty); // Not the empty tuple, which is its own target (TY_DECLARED)
bool ty_is_array(Checker* me, Ty const* ty);
