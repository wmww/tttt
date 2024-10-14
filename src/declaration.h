#pragma once

#include "lazy_type.h"

typedef struct NamedVariable {
    Span name;
    Type type;
    Generic const* generic;
} NamedVariable;

typedef struct NamedType {
    Span name;
    Type type;
    Generic const* generic;
    bool is_typeof_class;
    bool is_merged;
    VecType merged_types;   // All the interface types that will be merged together to make our type, not used if this
                            // is not a merged declaration and cleared once the merge happens
    LazyType lazy_type; // Only used if this is a merged declaration. In that case, `type` is set to be a lazy that
                        // points to this LazyType.
} NamedType;

Type named_type_evaluate_type(NamedType* me);

#define DECLARATION_HAS_VALUE 8
#define DECLARATION_HAS_TYPE 16

// See https://www.typescriptlang.org/docs/handbook/declaration-merging.html
typedef enum DeclarationTag {
    DECLARATION_UNINITIALIZED = 0,
    DECLARATION_NAMESPACE   = DECLARATION_HAS_VALUE | 1,
    DECLARATION_FUNCTION    = DECLARATION_HAS_VALUE | 2,
    DECLARATION_VALUE       = DECLARATION_HAS_VALUE | 3,
    DECLARATION_INTERFACE   = DECLARATION_HAS_TYPE | 1,
    DECLARATION_TYPE        = DECLARATION_HAS_TYPE | 2,
    DECLARATION_CLASS       = DECLARATION_HAS_VALUE | DECLARATION_HAS_TYPE | 1,
    DECLARATION_ENUM        = DECLARATION_HAS_VALUE | DECLARATION_HAS_TYPE | 2,
} DeclarationTag;

typedef struct Declaration {
    Span name;
    DeclarationTag tag;
    VecSpan declarations;
    bool has_value;
    NamedVariable value;
    bool has_type;
    NamedType type;
} Declaration;

typedef struct DeclProps {
    Span name;
    Span full;
    DeclarationTag tag;
    Generic const* generic;
    Type value_type;
    Type declared_type;
    bool is_typeof;
} DeclProps;

void declaration_drop(Declaration* me);

// If called multiple times attempts to merge declarations, does not check if merging is allowed, scope.c does that
void declaration_init(Declaration* me, DeclProps props);

#define i_key_class_names Declaration, declaration
#define i_is_forward
#include "wrap_stc/arc.h"

typedef struct NamedImport {
    Span decl_name;
    Span source_name;
    SourceFile* source_file;
    bool error_reported;
} NamedImport;
