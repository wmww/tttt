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

typedef enum DeclarationTag {
    DECLARATION_NONE = 0,
    DECLARATION_NAMESPACE,
    DECLARATION_FUNCTION,
    DECLARATION_VARIABLE,
    DECLARATION_INTERFACE,
    DECLARATION_TYPE,
    DECLARATION_CLASS,
    DECLARATION_ENUM,
} DeclarationTag;

// See https://www.typescriptlang.org/docs/handbook/declaration-merging.html
static inline bool declaration_tag_has_value(DeclarationTag tag) {
    static const bool ret[DECLARATION_ENUM + 1] = {
        [DECLARATION_NAMESPACE] = true,
        [DECLARATION_FUNCTION] = true,
        [DECLARATION_VARIABLE] = true,
        [DECLARATION_CLASS] = true,
        [DECLARATION_ENUM] = true,
    };
    return ret[tag];
}

static inline bool declaration_tag_has_type(DeclarationTag tag) {
    static const bool ret[DECLARATION_ENUM + 1] = {
        [DECLARATION_INTERFACE] = true,
        [DECLARATION_TYPE] = true,
        [DECLARATION_CLASS] = true,
        [DECLARATION_ENUM] = true,
    };
    return ret[tag];
}

typedef struct Declaration {
    Span name;
    VecSpan declarations;
    DeclarationTag value_tag;
    NamedVariable value;
    DeclarationTag type_tag;
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
