#pragma once

#include "ast_alloc.h"
#include "type.h"
#include "lazy_type.h"
#include "import_attributes.h"

// Identifier

// What few type references have: qualified names, `import()` types, the error and JavaScript cases. Boxed out of
// IdentifierType (owned by it), which is mostly a bare `Foo` or `Foo<T>`
typedef struct IdentifierTypeRare {
    AstSpans namespaces;
    AstSymbols namespace_symbols; // What each of the namespaces resolved to (maybe an alias), shorter on failure
    // `import("./a").A.B<T>`: names are looked up in the module's exports instead of the scope. `name` is null for a
    // bare `import("./a")`. With import_typeof it is a value (`typeof import("./a").x`)
    ModuleRef* import_ref;
    ImportAttributes* import_attributes; // Owned, `import("./a", { with: { ... } })`
    struct TypeNode* import_argument; // Owned: `import(T)`, an argument that isn't a string (TS1141, no module)
    Span import_argument_span;
    Span import_span;
    // `typeof import("./a").A.foo`: from this index on (namespaces, then name) the names are properties of the type of
    // `target`, not exports. -1 if all are exports
    int import_prop_start;
    bool import_typeof;
    // `typeof import("./a").f<...>` has a type argument list, maybe empty: an instantiation expression (TS2635)
    bool import_type_args;
    char const* import_type_args_open; // Its `<`
    // A bare `import("./m")` of a CommonJS module whose `module.exports =` has the module's types merged in: tsc's
    // reference is to the `export=` alias itself, not a class: the target's declared type, no type arguments (TS2315)
    bool import_export_equals_alias;
    // `A.b` where A is only a type (tsc's checkAndReportErrorForUsingTypeAsNamespace): that type, TS2713 if it has a
    // property b, else TS2702, said when checking (it takes the type's members)
    Symbol const* type_as_namespace;
    // `A.b` where A is an `import { A }` of a property of what `export =` names, and nothing else is a namespace A: tsc's
    // alias is unknownSymbol (every meaning, silent) if the property is missing, else no namespace (TS2503). Said when
    // checking, which knows whether the property exists: the import (an alias of a SYMBOL_DECL_EXPORT_EQUALS_PROPERTY)
    Symbol const* export_equals_namespace;
    // `A.b.c` where b is a value but not a namespace (tsc's resolveEntityName of a qualified name): said when checking,
    // TS2749 for the whole name if it reads as a value (`A.b.c` as properties of A's value, tryGetQualifiedNameAsValue),
    // else TS2713 if b is a type too, else TS2694. `value_member_at`: b's index in namespaces, `value_member_of` the
    // namespace it is a member of, `first_value` what the first name means as a value. `value_missing`: a name
    // isn't a member of the namespace before it at all (value_member null, value_member_at its index), but
    // the first name means a value too (a namespace merged inside a namespace of the same name hides the outer one's
    // members from `A.b`, not from A's value): TS2749 if the name reads as a value, else TS2694
    Symbol const* value_member;
    Symbol const* value_member_of;
    Symbol const* first_value;
    u32 value_member_at;
    bool value_missing;
    // A generic type alias reference with type arguments (`Record<K, V>`) whose name is also an import alias that isn't
    // a type (`import { Record } from "effect"`, a namespace): tsc's getTypeFromTypeAliasReference looks the name up
    // as an alias too (resolveTypeReferenceName with SymbolFlags.Alias, for the alias it would print by), which marks
    // that import referenced. Marked when the reference is lowered and isn't another alias's body (lower.c)
    struct Symbol* args_alias;
    struct Scope const* args_alias_scope;
} IdentifierTypeRare;

typedef struct IdentifierType {
    Span name;
    // What the checker goes by: the symbol that has the meaning the name is used with (aliases resolved). Cached next
    // to `symbol` so that checking never chases alias chains. For `this`: see this_kind
    Symbol const* target;
    Symbol const* symbol; // The symbol the name found, which may be an alias (the symbols baseline shows that one)
    VecSpannedType generic_params;
    IdentifierTypeRare* rare; // Owned, null for a plain name (identifier_type_rare())
    KeywordIdentifier keyword : 8;
    ThisKind this_kind : 8;
    // Written in a JSDoc comment: tsc's JSDoc names (`String` is string, `Object` any...), see jsdoc_intrinsic
    bool jsdoc;
    // In a JSDoc tag tsgo's checkSourceElement never visits (`@throws`, an earlier comment's): only resolving it reports, no
    // type argument constraints (checkTypeReferenceNode)
    bool jsdoc_unchecked;
    // What a JSDoc name means instead of what it would find (tsc's getIntendedTypeFromJSDocTypeReference), set when
    // resolving: nothing is looked up then
    uint8_t jsdoc_intrinsic;
    // The whole body of a type alias written `intrinsic`: tsc's IntrinsicKeyword (the lib's Uppercase<S> & co.), not a
    // name. Anywhere else `intrinsic` is a name like any other (TS2304)
    bool intrinsic_keyword;
    // Inside a conditional type whose check type is a bare reference to the type parameter this names (the check type
    // included): it stands for the distributed type parameter (tsgo 7.1's getDistributedTypeParameter, plan 57)
    bool distributed;
    // The class a JavaScript `@implements` tag names (tsc's ExpressionWithTypeArguments under a JSDocImplementsTag): a
    // wrong type argument count asks for an `@extends` tag (TS8026, getTypeFromClassOrInterfaceReference)
    bool jsdoc_implements;
} IdentifierType;

// The rare part, made on demand
static inline IdentifierTypeRare* identifier_type_rare(IdentifierType* me) {
    if (!my rare) my rare = ast_new(IdentifierTypeRare, { .import_prop_start = -1 });
    return my rare;
}
static inline AstSpans const* identifier_type_namespaces(IdentifierType const* me) {
    static const AstSpans empty = {0};
    return my rare ? &my rare->namespaces : &empty;
}
static inline isize identifier_type_namespace_count(IdentifierType const* me) {
    return my rare ? ast_spans_size(&my rare->namespaces) : 0;
}
static inline AstSymbols const* identifier_type_namespace_symbols(IdentifierType const* me) {
    static const AstSymbols empty = {0};
    return my rare ? &my rare->namespace_symbols : &empty;
}
static inline Span identifier_type_import_span(IdentifierType const* me) {
    return my rare ? my rare->import_span : span_null;
}
static inline bool identifier_type_import_typeof(IdentifierType const* me) {
    return my rare && my rare->import_typeof;
}
static inline TypeNode const* identifier_type_import_argument(IdentifierType const* me) {
    return my rare ? my rare->import_argument : nullptr;
}
static inline ModuleRef* identifier_type_import_ref(IdentifierType const* me) {
    return my rare ? my rare->import_ref : nullptr;
}

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
    JSDOC_INTRINSIC_INDEX_SIGNATURE, // `Object<K, V>`, `Object.<K, V>`: `Record<K, V>` of a valid key K, else any
} JsDocIntrinsic;

void identifier_type_drop(IdentifierType* me);
// While a JavaScript function's own JSDoc types (`@param`, `@returns`) are looked up: the function's scope, from which
// tsc resolves their names a second time (only marks references: unused.c). Returns the previous one to restore
Scope const* identifier_type_set_jsdoc_host(struct Scope const* scope);
void identifier_type_lookup_names(IdentifierType* me, Scope* names, ErrorCtx* errs);
// While a type parameter's default is looked up: the type parameters it may not reference, from `from` on (TS2744,
// tsc's checkTypeParametersNotReferenced). Set by generic_lookup_names()
typedef struct TypeParamDefaultScope {
    struct Generic const* generic;
    isize from;
} TypeParamDefaultScope;
extern _Thread_local TypeParamDefaultScope identifier_type_default_scope;
// resolved_error: the reference resolved to the error type (no constraint check, tsc's checkTypeReferenceOrImport)
// args_checked: identifier_type_typecheck_args() ran already (before the reference was resolved, tsc's order)
void identifier_type_typecheck(IdentifierType const* me, bool resolved_error, bool args_checked, ErrorCtx* errs);
void identifier_type_typecheck_args(IdentifierType const* me, ErrorCtx* errs);
// tsc's getTypeFromClassOrInterfaceReference arity check of a reference to `symbol` with arg_count type arguments
// (TS2314/TS2707, TS2315 for a type that isn't generic). Whether it reported
bool identifier_type_is_jsdoc_function(IdentifierType const* me);
bool several_types_is_generic(struct Symbol const* symbol);
bool several_types_arity_fits(struct Symbol const* symbol, u32 arg_count, struct SourceFile const* source);
// missing_extends: a JavaScript class's heritage clause without an `@extends` tag, which the message asks for (TS8026)
bool several_types_check_arity(struct Symbol const* symbol, u32 arg_count, Span span, bool missing_extends, ErrorCtx* errs);
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
    // `{ [K in C]: T } extends { [P in D]: infer X }`: T with C for K (InferredType.check_mapped is the check type's
    // mapped member)
    INFER_POSITION_MAPPED_TEMPLATE,
} InferPosition;

typedef struct InferredType {
    Span name;
    Span full_span;
    TypeNode extends;
    Symbol* owned_symbol;
    InferPosition position;
    IdentifierType const* reference; // INFER_POSITION_TYPE_ARG: the reference it is type argument `arg_index` of
    u32 arg_index;
    struct InterfaceMember const* check_mapped; // INFER_POSITION_MAPPED_TEMPLATE
} InferredType;

void inferred_type_drop(InferredType* me);

// Tuple

typedef struct TupleElement {
    Span span; // The element (tsc's node: from a `...` to the end of its type), invalid in JSDoc
    Span maybe_name;
    TypeNode type;
    bool rest;
    bool optional;
    bool rest_in_type; // `a: ...T` (TS5087): the `...` belongs to the type, a Required named member to tsc
} TupleElement;

static inline void tuple_element_drop(TupleElement* me) { type_drop(&my type); }

#define g_class_key TupleElement, tuple_element
#define g_no_clone
#define g_declared
#define g_allocator ast
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
    AstCStrs texts;
    VecTypeNode types;
    AstSpans type_spans; // Where each type was written
    Span span; // From the opening backtick to the closing one
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
    TypeFlow true_flow[2]; // What the true branch's nodes have as TypeNode.flow: [0] covariantly, [1] contravariantly
    // The `{` of the innermost Block it's written in, null if none: tsc's isTypeParameterPossiblyReferenced takes a
    // type parameter declared outside it to be possibly referenced (ty_conditional_distribution_dependent())
    const char* enclosing_block;
    Span span; // From the check type to the end (tsc's ConditionalTypeNode, where compareTypes orders roots by)
} TernaryType;

void ternary_type_drop(TernaryType* me);
