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

// The InterfaceDecl an entry of legacy_declaration_entries() came from, or null if it's something else
static inline InterfaceDecl const* legacy_entry_interface_decl(Type const* entry) {
    entry = legacy_skip_refs(entry);
    if (entry->tag == TYPE_LAZY && entry->lazy->tag == LAZY_TYPE_INTERFACE_DECL) {
        return c_container_of(entry->lazy, InterfaceDecl, lazy_type);
    }
    return nullptr;
}

Ty const* ty_anonymous_from_overloads(Checker* me, Type const* const* overloads, u32 count, LowerCtx ctx);
