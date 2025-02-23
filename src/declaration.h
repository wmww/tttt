#pragma once

#include "lazy_type.h"

typedef struct MergedType {
    VecType merge_list;
    LazyType lazy;
} MergedType;

void merged_type_add(MergedType* me, Type t);
MergedType merged_type_clone(MergedType me);
Type merged_type_evaluate_type(MergedType* me);
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
    // Be sure to update DECLARATION_TAG_COUNT when adding to the end
} DeclarationTag;
#define DECLARATION_TAG_COUNT (DECLARATION_ENUM_CASE + 1)

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

typedef struct Declaration {
    Span name;
    VecSpan declarations;
    VecSpan namespaces;
    bool exported;
    DeclarationTag value_tag;
    Generic const* value_generic; // Null if generic empty
    Type value_type;
    DeclarationTag type_tag;
    Type declared_type;
    Generic const* type_generic;
    bool is_typeof;
    MergedType merged_value_type;
    MergedType merged_declared_type;
    DeclarationTag namespace_tag;
    // This is allowed to be null when declaration_tag_has_namespace() returns true. In this case every declaration
    // looked up should be an "any" declaration.
    // TODO: document better
    Scope const* namespace;
    // Used when copying the same declarations between many programs that should remain isolated (such as loading the
    // standard library for the tests). Allows declarations to be "copy-on-write". A declaration shared between scopes
    // should be replaced by a deep copy before being modified.
    bool shared_between_scopes;
} Declaration;

void declaration_drop(Declaration* me);
// This function can be called on a declaration after copying it's memory from another declaration to make this one take
// ownership of its resources. This isn't a normal clone function because setting up declared_type correctly requires
// address stability, which would be broken by returning a Declaration by value.
void declaration_clone_in_place(Declaration* me);

typedef struct DeclProps {
    Span name;
    Span full;
    bool exported;
    DeclarationTag tag;
    Generic const* generic;
    Type value_type;
    Type declared_type;
    bool is_typeof;
    Scope const* namespace;
} DeclProps;

DeclProps decl_props_clone(DeclProps me);

// Returns true if the two declarations can be merged
bool declaration_test_mergable(Declaration const* me, DeclProps const* props, ErrorCtx* errs);

// If called multiple times attempts to merge declarations, does not check if merging is allowed, scope.c does that
void declaration_init(Declaration* me, DeclProps props);

#define i_key_class_names Declaration, declaration
#define i_opt c_no_atomic
#define i_is_forward
#include "wrap_stc/arc.h"

#define i_key_names DeclarationCPtr, declaration_cptr
#define i_is_forward
#include "wrap_stc/vec.h"
