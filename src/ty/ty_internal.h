#pragma once

// Shared between the files of the new type core, mostly the seams with the legacy checker. Everything named legacy_*
// is scaffolding that goes away as plan 02's stages move type evaluation over.

#include "ty/ty.h"
#include "several_types.h"
#include "callable_type.h"
#include "interface_member.h"
#include "interface_decl.h"
#include "function_impl.h"
#include "declaration.h"
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
    KEY_TYPE_PARAM,     // Declaration -> Ty
    KEY_DECL_LINKS,     // Declaration -> DeclLinks
    KEY_GENERIC_PARAMS, // Generic -> TyList
    KEY_ALIAS_INSTANCE, // (alias Declaration, argument ids) -> Ty
    KEY_NODE,           // TYPE_INTERFACE/TYPE_CALLABLE node -> Ty
    KEY_NAMESPACE,      // Scope -> Ty
    KEY_DEFERRED_REFERENCE, // (TYPE_ARRAY/TYPE_TUPLE node, mapper pairs) -> Ty
    KEY_STABLE_WRAPPER, // LazyType -> arena copy of a TYPE_LAZY wrapper, see legacy_stable_view()
    KEY_NEW_WRAPPER,    // Ty id -> arena TYPE_NEW, see ty_legacy_wrapper()
    KEY_STRUCTURAL,     // Shape of a temporary legacy object type -> Ty, see ty_structural_from_legacy()
    KEY_VARIANCES,      // declared type id -> variance array
    KEY_MARKER,         // (declared type id, parameter index, which) -> marker instantiation
};

// Doesn't insert, unlike intern_slot()
void const* intern_get(InternTable const* table, u64 const* words, u32 count);

// The declarations merged into a Declaration's type side, as the legacy types they were registered with
typedef struct DeclEntries {
    Type const* data;
    u32 len;
} DeclEntries;

static inline DeclEntries legacy_declaration_entries(Declaration const* decl) {
    MergedType const* merged = &decl->merged_declared_type;
    if (vec_type_size(&merged->merge_list)) {
        return (DeclEntries){ .data = merged->merge_list.data, .len = (u32)vec_type_size(&merged->merge_list) };
    } else if (merged->lazy.tag == LAZY_TYPE_EVALUATED) {
        return (DeclEntries){ .data = &merged->lazy.memo.evaluated, .len = 1 };
    } else {
        return (DeclEntries){0};
    }
}

// Follows TYPE_REF only, which never evaluates anything
static inline Type const* legacy_skip_refs(Type const* type) {
    while (type->tag == TYPE_REF) type = type->ref;
    return type;
}

// Follows TYPE_REF and TYPE_LAZY. The legacy memo lives in the AST, hence the const cast (until stage 2)
static inline Type const* legacy_skip_wrappers(Type const* type) {
    for (;;) {
        if (type->tag == TYPE_REF) {
            type = type->ref;
        } else if (type->tag == TYPE_LAZY) {
            type = lazy_type_get((LazyType*)type->lazy);
        } else {
            return type;
        }
    }
}

static inline Type const* legacy_expr_type(Expr const* expr) {
    return lazy_type_get((LazyType*)&expr->lazy_type);
}

Type const* legacy_stable_view(Checker* me, Type const* type, u64* identity);
Ty const* ty_structural_from_legacy(Checker* me, Type const* node, LowerCtx ctx);
// alias_of: the deferred reference this is an instantiation of, if it goes by an alias name (null otherwise)
Ty const* ty_deferred_reference(
    Checker* me, Type const* node, LowerCtx ctx, TyMapper const* mapper, Declaration const* alias, TyList alias_args
);
Ty const* ty_anonymous_from_overloads(Checker* me, Type const* const* overloads, u32 count, LowerCtx ctx);

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
    INFER_PRIORITY_RETURN_TYPE = 2,
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
Ty const* ty_apparent_type(Checker* me, Ty const* ty);
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
