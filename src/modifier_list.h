#pragma once

#include "ast_alloc.h"
#include "span.h"
typedef struct ErrorCtx ErrorCtx;
typedef struct vec_error VecError;

typedef struct SpannedModifier {
    enum {
        MODIFIER_READONLY,
        MODIFIER_ABSTRACT,
        MODIFIER_STATIC,
        MODIFIER_ACCESSOR,
        MODIFIER_EXPORT,
        MODIFIER_EXPORT_DEFAULT,
        MODIFIER_ASYNC,
        MODIFIER_DECLARE,
        MODIFIER_OVERRIDE,
        MODIFIER_PRIVATE,
        MODIFIER_PROTECTED,
        MODIFIER_PUBLIC,
        MODIFIER_CONST,
        MODIFIER_IN, // Variance annotations: `in T`, `out T`
        MODIFIER_OUT,
        MODIFIER_ENUM_CAP,
    } tag;
    Span span;
} SpannedModifier;

#define g_key SpannedModifier, spanned_modifier
#define g_allocator ast
#include <cleat/generic/vec.h>

typedef struct ModifierList {
    VecSpannedModifier spanned;
    bool includes[MODIFIER_ENUM_CAP];
} ModifierList;

static void modifier_list_drop(ModifierList* me) {
    vec_spanned_modifier_drop(&my spanned);
}

static ModifierList modifier_list_clone(ModifierList me) {
    me.spanned = vec_spanned_modifier_clone(me.spanned);
    return me;
}


// The kind of node a modifier list is on (tsc's node.kind as checkGrammarModifiers() looks at it)
typedef enum ModifiedKind {
    MODIFIED_PROPERTY,
    MODIFIED_PROPERTY_SIGNATURE,
    MODIFIED_METHOD,
    MODIFIED_METHOD_SIGNATURE,
    MODIFIED_GET_ACCESSOR,
    MODIFIED_SET_ACCESSOR,
    MODIFIED_CONSTRUCTOR,
    MODIFIED_INDEX_SIGNATURE,
    MODIFIED_CLASS_STATIC_BLOCK,
    MODIFIED_PARAMETER,
    MODIFIED_VARIABLE_STATEMENT,
    MODIFIED_FUNCTION,
    MODIFIED_CLASS,
    MODIFIED_INTERFACE,
    MODIFIED_TYPE_ALIAS,
    MODIFIED_ENUM,
    MODIFIED_MODULE,
    MODIFIED_IMPORT,
    MODIFIED_IMPORT_EQUALS,
    MODIFIED_EXPORT_DECLARATION,
    MODIFIED_EXPORT_ASSIGNMENT,
    MODIFIED_NAMESPACE_EXPORT,
    MODIFIED_MISSING_DECLARATION,
    MODIFIED_TYPE_PARAMETER,
} ModifiedKind;

// What declares a type parameter (MODIFIED_TYPE_PARAMETER): which of `in`, `out` and `const` it can have
typedef enum TypeParamOwner {
    TYPE_PARAM_OF_FUNCTION, // Functions, methods, signatures, function and constructor types: `const`
    TYPE_PARAM_OF_CLASS, // `in`, `out` and `const`
    TYPE_PARAM_OF_TYPE, // Interfaces and type aliases: `in` and `out`
} TypeParamOwner;

// What contains the modified node (tsc's node.parent.kind)
typedef enum ModifiedParent {
    MODIFIED_IN_SOURCE_FILE,
    MODIFIED_IN_MODULE_BLOCK,
    MODIFIED_IN_CLASS,
    MODIFIED_IN_TYPE, // An interface or a type literal
    MODIFIED_IN_OTHER, // A block, a function's parameters...
} ModifiedParent;

typedef struct ModifiedNode {
    ModifiedKind kind;
    ModifiedParent parent;
    bool parent_ambient; // The parent is in an ambient context (a `declare` namespace, a declaration file)
    bool parent_is_namespace; // In a `namespace N {}` that isn't an ambient module (`default` isn't allowed)
    bool in_abstract_class; // The parent is a class declaration with `abstract`
    bool private_name;
    uint8_t using_kind; // 1 `using`, 2 `await using`
    uint8_t type_param_owner; // MODIFIED_TYPE_PARAMETER: TypeParamOwner
    Span name; // Where errors on the node itself go (tsc's getErrorSpanForNode)
} ModifiedNode;

// tsc's checkGrammarModifiers() (with findFirstIllegalModifier() and checkGrammarAsyncModifier()): the first
// problem with the modifiers of a node, as a grammar error. Returns whether it reported one
bool modifier_list_check_grammar(ModifierList const* me, ModifiedNode const* node, ErrorCtx* errs);
