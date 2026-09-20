#pragma once

#include "lazy_type.h"

typedef struct MergedType {
    VecTypeNode merge_list;
    LazyType lazy;
    // Set if this is the declared type side of a Declaration (as opposed to its value side)
    struct Declaration const* declared_owner;
    // Set if this is the value side
    struct Declaration const* value_owner;
} MergedType;

void merged_type_add(MergedType* me, TypeNode t);
MergedType merged_type_clone(MergedType me);
Ty const* merged_type_evaluate_type(MergedType* me);
void merged_type_drop(MergedType* me);

typedef enum DeclarationTag {
    DECLARATION_NONE = 0,
    // The ordering of this enum matters! Declaration types lower in the list always take precedence over higher ones
    // when merging declarations
    DECLARATION_NAMESPACE_TYPE_ONLY,
    DECLARATION_NAMESPACE_WITH_VALUE,
    DECLARATION_FUNCTION_IMPL,
    DECLARATION_FUNCTION_SIGNATURE,
    DECLARATION_VARIABLE,
    DECLARATION_ARGUMENT,
    DECLARATION_INTERFACE,
    DECLARATION_TYPE,
    DECLARATION_GENERIC_TYPE,
    DECLARATION_CLASS,
    DECLARATION_ENUM,
    DECLARATION_ENUM_CASE,
    DECLARATION_ENUM_CASE_TYPE,
    // Be sure to update DECLARATION_TAG_COUNT when adding to the end
} DeclarationTag;
#define DECLARATION_TAG_COUNT (DECLARATION_ENUM_CASE_TYPE + 1)

// See https://www.typescriptlang.org/docs/handbook/declaration-merging.html
static inline bool declaration_tag_has_value(DeclarationTag tag) {
    static const bool ret[DECLARATION_TAG_COUNT] = {
        [DECLARATION_NAMESPACE_WITH_VALUE] = true,
        [DECLARATION_FUNCTION_SIGNATURE] = true,
        [DECLARATION_FUNCTION_IMPL] = true,
        [DECLARATION_VARIABLE] = true,
        [DECLARATION_ARGUMENT] = true,
        [DECLARATION_CLASS] = true,
        [DECLARATION_ENUM] = true,
        [DECLARATION_ENUM_CASE] = true,
    };
    return ret[tag];
}

static inline bool declaration_tag_has_type(DeclarationTag tag) {
    static const bool ret[DECLARATION_TAG_COUNT] = {
        [DECLARATION_INTERFACE] = true,
        [DECLARATION_TYPE] = true,
        [DECLARATION_GENERIC_TYPE] = true,
        [DECLARATION_CLASS] = true,
        [DECLARATION_ENUM] = true,
        [DECLARATION_ENUM_CASE] = true,
        [DECLARATION_ENUM_CASE_TYPE] = true,
    };
    return ret[tag];
}

static inline bool declaration_tag_has_namespace(DeclarationTag tag) {
    static const bool ret[DECLARATION_TAG_COUNT] = {
        [DECLARATION_NAMESPACE_TYPE_ONLY] = true,
        [DECLARATION_NAMESPACE_WITH_VALUE] = true,
        [DECLARATION_ENUM] = true,
    };
    return ret[tag];
}

// The AST node(s) a declaration was made from. A merged declaration has one per contributing class, interface or enum
typedef enum DeclNodeKind {
    DECL_NODE_NONE = 0,
    DECL_NODE_CLASS,          // ClassDecl, on both the class declaration and its `typeof` declaration
    DECL_NODE_INTERFACE,      // InterfaceDecl
    DECL_NODE_ENUM,           // EnumDecl, on both the enum declaration and its `typeof` declaration
    DECL_NODE_ENUM_CASE,      // EnumCase, on both the member declaration and its `typeof` declaration
} DeclNodeKind;

typedef struct DeclNode {
    DeclNodeKind kind;
    void const* ptr;
} DeclNode;

#define g_key DeclNode, decl_node
#include <cleat/generic/vec.h>

typedef struct Declaration {
    Span name;
    VecDeclNode nodes;
    VecSpan declarations;
    VecSpan namespaces;
    bool exported;
    DeclarationTag value_tag;
    Generic const* value_generic; // Null if generic empty
    TypeNode value_type;
    DeclarationTag type_tag;
    TypeNode declared_type;
    Generic const* type_generic;
    bool is_typeof;
    MergedType merged_value_type;
    MergedType merged_declared_type;
    DeclarationTag namespace_tag;
    // This is allowed to be null when declaration_tag_has_namespace() returns true. In this case every declaration
    // looked up should be an "any" declaration.
    // TODO: document better
    struct Symbol const* namespace; // Members are its exports
} Declaration;

void declaration_drop(Declaration* me);
// This function can be called on a declaration after copying it's memory from another declaration to make this one take
// ownership of its resources. This isn't a normal clone function because setting up declared_type correctly requires
// address stability, which would be broken by returning a Declaration by value. Only used when a program adds to a
// symbol of the shared lib program (scope.c: clone_parent_symbol)
void declaration_clone_in_place(Declaration* me);

typedef struct DeclProps {
    Span name;
    Span full;
    bool exported;
    DeclarationTag tag;
    Generic const* generic;
    TypeNode value_type;
    TypeNode declared_type;
    bool is_typeof;
    bool function_scoped; // `var` (parameters are DECLARATION_ARGUMENT)
    bool is_const_enum;
    bool is_infer; // `infer U`: several of the same name are one type parameter
    // Set by scope_init_declaration(): a further block of a namespace or a redeclared `var` adds nothing to the value
    bool value_known;
    // Set by scope_init_declaration(): the symbol being declared for namespaces and enums
    struct Symbol const* namespace;
    DeclNode node;
} DeclProps;

// If called multiple times attempts to merge declarations, does not check if merging is allowed, scope.c does that
void declaration_init(Declaration* me, DeclProps props);

#define g_key DeclarationCPtr, declaration_cptr
#define g_declared
#include <cleat/generic/vec.h>

#define g_key SymbolCPtr, symbol_cptr
#define g_declared
#include <cleat/generic/vec.h>
