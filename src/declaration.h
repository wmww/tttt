#pragma once

#include "lazy_type.h"

typedef enum DeclarationTag {
    DECLARATION_NONE = 0,
    DECLARATION_NAMESPACE_TYPE_ONLY,
    DECLARATION_NAMESPACE_WITH_VALUE,
    DECLARATION_FUNCTION,
    DECLARATION_VARIABLE,
    DECLARATION_INTERFACE,
    DECLARATION_TYPE,
    DECLARATION_CLASS,
    DECLARATION_ENUM,
} DeclarationTag;
#define DECLARATION_TAG_COUNT (DECLARATION_ENUM + 1)

// See https://www.typescriptlang.org/docs/handbook/declaration-merging.html
static inline bool declaration_tag_has_value(DeclarationTag tag) {
    static const bool ret[DECLARATION_TAG_COUNT] = {
        [DECLARATION_NAMESPACE_WITH_VALUE] = true,
        [DECLARATION_FUNCTION] = true,
        [DECLARATION_VARIABLE] = true,
        [DECLARATION_CLASS] = true,
        [DECLARATION_ENUM] = true,
    };
    return ret[tag];
}

static inline bool declaration_tag_has_type(DeclarationTag tag) {
    static const bool ret[DECLARATION_TAG_COUNT] = {
        [DECLARATION_INTERFACE] = true,
        [DECLARATION_TYPE] = true,
        [DECLARATION_CLASS] = true,
        [DECLARATION_ENUM] = true,
    };
    return ret[tag];
}

static inline bool declaration_tag_has_namespace(DeclarationTag tag) {
    static const bool ret[DECLARATION_TAG_COUNT] = {
        [DECLARATION_NAMESPACE_TYPE_ONLY] = true,
        [DECLARATION_NAMESPACE_WITH_VALUE] = true,
    };
    return ret[tag];
}

typedef struct Declaration {
    Span name;
    VecSpan declarations;
    DeclarationTag value_tag;
    Generic const* value_generic; // Null if generic empty
    Type value_type;
    DeclarationTag type_tag;
    Type declared_type;
    Generic const* type_generic;
    bool is_typeof;
    VecType declared_type_list;
    LazyType declared_type_lazy;
    DeclarationTag namespace_tag;
    // This is allowed to be null when declaration_tag_has_namespace() returns true. In this case every declaration
    // looked up should be an "any" declaration.
    // TODO: document better
    Scope const* namespace;
} Declaration;

typedef struct DeclProps {
    Span name;
    Span full;
    DeclarationTag tag;
    Generic const* generic;
    Type value_type;
    Type declared_type;
    bool is_typeof;
    Scope const* namespace;
} DeclProps;

void declaration_drop(Declaration* me);
Type declaration_evaluate_declared_type(Declaration const* me);

// If called multiple times attempts to merge declarations, does not check if merging is allowed, scope.c does that
void declaration_init(Declaration* me, DeclProps props);

#define i_key_class_names Declaration, declaration
#define i_opt c_no_atomic
#define i_is_forward
#include "wrap_stc/arc.h"

typedef enum {
    IMPORT_SOURCE_NONE = 0, // Nothing is imported
    IMPORT_SOURCE_DEFAULT, // Import the default exported item
    IMPORT_SOURCE_WILDCARD, // Collect all exported items into a single namespace
    IMPORT_SOURCE_SPECIFIC, // Import an item with a specific name
} ImportSource;

typedef struct NamedImport {
    Span decl_name;
    Span decl_span;
    ImportSource source; // Never IMPORT_SOURCE_NONE
    Span source_name; // Only used for IMPORT_SOURCE_SPECIFIC
    SourceFile* source_file;
    bool error_reported;
    ArcDeclaration* declaration;
} NamedImport;
