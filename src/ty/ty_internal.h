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
    KEY_FUNCTION,       // FunctionImpl -> Ty, see ty_function_type()
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
    // Walk state
    u32 priority;
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
} Inference;

void inference_init(Inference* inf, TyList params);
void inference_drop(Inference* inf);
void inference_collect_infer_params(Checker* me, Inference* inf, Ty const* target);
void infer_types(Checker* me, Inference* inf, Ty const* source, Ty const* target, u32 priority);
// What a call infers for parameter i: common supertype of the candidates, widened unless `keep_literals`. Null if
// there were no candidates
Ty const* inference_call_result(Checker* me, Inference* inf, u32 i, bool keep_literals);
// What a conditional type infers: union of the covariant candidates, else intersection of the contravariant ones
Ty const* inference_conditional_result(Checker* me, Inference* inf, u32 i);

// relate.c
Ty const* ty_global_regexp_type(Checker* me);
Ty const* ty_global_function_type(Checker* me); // Null without a lib
Ty const* ty_global_object_type(Checker* me); // Null without a lib
bool ty_has_base_type(Checker* me, Ty const* source, Ty const* target);
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
bool ty_is_tuple(Ty const* ty); // Not the empty tuple, which is its own target (TY_DECLARED)
bool ty_is_array(Checker* me, Ty const* ty);
