#pragma once

#include "type.h"
#include "lazy_type.h"
#include "import_attributes.h"

// Identifier

// PERF: maybe split this into a simple version that fits in a type by value and a complex version with namespaces/
// generic params/etc
typedef struct IdentifierType {
    VecSpan namespaces;
    VecSymbolCPtr namespace_symbols; // What each of the namespaces resolved to (maybe an alias), shorter on failure
    Span name;
    KeywordIdentifier keyword;
    // What the checker goes by: the symbol that has the meaning the name is used with (aliases resolved). Cached next
    // to `symbol` so that checking never chases alias chains. For `this`: see this_kind
    Symbol const* target;
    ThisKind this_kind;
    Symbol const* symbol; // The symbol the name found, which may be an alias (the symbols baseline shows that one)
    VecSpannedType generic_params;
    // `import("./a").A.B<T>`: names are looked up in the module's exports instead of the scope. `name` is null for a
    // bare `import("./a")`. With import_typeof it is a value (`typeof import("./a").x`)
    ModuleRef* import_ref;
    ImportAttributes* import_attributes; // Owned, `import("./a", { with: { ... } })`
    bool import_typeof;
    Span import_span;
    // `typeof import("./a").A.foo`: from this index on (namespaces, then name) the names are properties of the type of
    // `target`, not exports. -1 if all are exports
    int import_prop_start;
    // Written in a JSDoc comment: tsc's JSDoc names (`String` is string, `Object` any...), see jsdoc_intrinsic
    bool jsdoc;
    // What a JSDoc name means instead of what it would find (tsc's getIntendedTypeFromJSDocTypeReference), set when
    // resolving: nothing is looked up then
    uint8_t jsdoc_intrinsic;
} IdentifierType;

typedef enum JsDocIntrinsic {
    JSDOC_INTRINSIC_NONE = 0,
    JSDOC_INTRINSIC_STRING,
    JSDOC_INTRINSIC_NUMBER,
    JSDOC_INTRINSIC_BIGINT,
    JSDOC_INTRINSIC_BOOLEAN,
    JSDOC_INTRINSIC_VOID,
    JSDOC_INTRINSIC_UNDEFINED,
    JSDOC_INTRINSIC_NULL,
    JSDOC_INTRINSIC_ANY, // `Object` without noImplicitAny
    JSDOC_INTRINSIC_INDEX_SIGNATURE, // `Object<string, V>`, `Object.<number, V>`: `{ [x: string]: V }`
} JsDocIntrinsic;

void identifier_type_drop(IdentifierType* me);
void identifier_type_lookup_names(IdentifierType* me, Scope* names, ErrorCtx* errs);
void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs);
// The reference as written: from its first name to its closing `>` (just the name of a keyword or `import()`)
Span identifier_type_span(IdentifierType const* me);

static inline TypeNode type_identifier(IdentifierType* identifier) {
    return (TypeNode){
        .tag = TYPE_IDENTIFIER,
        .identifier = identifier,
    };
}

// Inferred identifier

// Where an `infer X` is written, which decides its implied constraint (tsc's getInferredTypeParameterConstraint)
typedef enum InferPosition {
    INFER_POSITION_OTHER = 0,
    INFER_POSITION_TYPE_ARG, // `Foo<infer X>`: the constraint of Foo's type parameter
    INFER_POSITION_REST, // `(...args: infer X)`, `[...infer X]`: unknown[]
    INFER_POSITION_TEMPLATE, // `${infer X}`: string
    INFER_POSITION_MAPPED_KEY, // `{ [K in infer X]: ... }`: keyof any
} InferPosition;

typedef struct InferredType {
    Span name;
    Span full_span;
    TypeNode extends;
    Symbol* owned_symbol;
    Symbol const* symbol_ref;
    InferPosition position;
    IdentifierType const* reference; // INFER_POSITION_TYPE_ARG: the reference it is type argument `arg_index` of
    u32 arg_index;
} InferredType;

void inferred_type_drop(InferredType* me);

// Tuple

typedef struct TupleElement {
    Span maybe_name;
    TypeNode type;
    bool rest;
    bool readonly;
    bool optional;
} TupleElement;

static inline void tuple_element_drop(TupleElement* me) { type_drop(&my type); }

#define g_class_key TupleElement, tuple_element
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>

// Index

typedef struct IndexedType {
    TypeNode interface;
    TypeNode index;
    Span index_span; // Where errors about the index go
    Span span; // The whole `T[K]`
} IndexedType;

void indexed_type_drop(IndexedType* me);
void indexed_type_lookup_names(IndexedType* me, Scope* names, ErrorCtx* errs);
void indexed_type_typecheck(IndexedType const* me, ErrorCtx* errs);

// Keyof

typedef struct KeyofType {
    TypeNode interface;
} KeyofType;

void keyof_type_drop(KeyofType* me);
void keyof_type_lookup_names(KeyofType* me, Scope* names, ErrorCtx* errs);
void keyof_type_typecheck(KeyofType const* me, ErrorCtx* errs);

// Template literal type

// `a${B}c`: the texts (cooked) around the substitutions, one more of them than of types
typedef struct TemplateType {
    VecCStr texts;
    VecTypeNode types;
    VecSpan type_spans; // Where each type was written
} TemplateType;

void template_type_drop(TemplateType* me);
void template_type_typecheck(TemplateType const* me, ErrorCtx* errs);

// Typeof

typedef struct TypeofType {
    Expr* expr;
    bool owns_expr;
} TypeofType;

void typeof_type_drop(TypeofType* me);
void typeof_type_lookup_names(TypeofType* me, Scope* names, ErrorCtx* errs);
void typeof_type_typecheck(TypeofType const* me, ErrorCtx* errs);

// Ternary

typedef struct TernaryType {
    TypeNode maybe_sub;
    TypeNode maybe_super;
    TypeNode if_true;
    TypeNode if_false;
} TernaryType;

void ternary_type_drop(TernaryType* me);
