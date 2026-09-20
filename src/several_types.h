#pragma once

#include "type.h"
#include "lazy_type.h"

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
    bool import_typeof;
    Span import_span;
    // `typeof import("./a").A.foo`: from this index on (namespaces, then name) the names are properties of the type of
    // `target`, not exports. -1 if all are exports
    int import_prop_start;
} IdentifierType;

void identifier_type_drop(IdentifierType* me);
IdentifierType identifier_type_clone(IdentifierType me);
void identifier_type_lookup_names(IdentifierType* me, Scope* names, ErrorCtx* errs);
void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs);

static inline TypeNode type_identifier(IdentifierType* identifier) {
    return (TypeNode){
        .tag = TYPE_IDENTIFIER,
        .identifier = identifier,
    };
}

// Inferred identifier

typedef struct InferredType {
    Span name;
    Span full_span;
    TypeNode extends;
    Symbol* owned_symbol;
    Symbol const* symbol_ref;
} InferredType;

void inferred_type_drop(InferredType* me);
InferredType inferred_type_clone(InferredType me);

// Tuple

typedef struct TupleElement {
    Span maybe_name;
    TypeNode type;
    bool rest;
    bool readonly;
    bool optional;
} TupleElement;

static inline void tuple_element_drop(TupleElement* me) { type_drop(&my type); }
static inline TupleElement tuple_element_clone(TupleElement me) { me.type = type_clone(me.type); return me; }

#define g_class_key TupleElement, tuple_element
#define g_declared
#include <cleat/generic/vec.h>

// Index

typedef struct IndexedType {
    TypeNode interface;
    TypeNode index;
    bool is_from_binding;
    bool array_only;
    // Only for is_from_binding:
    bool rest; // `[a, ...x]`: everything from index on. `{ a, ...x }`: the object (index is meaningless)
    Expr* default_value; // Not owned (the Binding's): `[x = 1]`
} IndexedType;

void indexed_type_drop(IndexedType* me);
IndexedType indexed_type_clone(IndexedType me);
void indexed_type_lookup_names(IndexedType* me, Scope* names, ErrorCtx* errs);
void indexed_type_typecheck(IndexedType const* me, ErrorCtx* errs);

// Keyof

typedef struct KeyofType {
    TypeNode interface;
} KeyofType;

void keyof_type_drop(KeyofType* me);
KeyofType keyof_type_clone(KeyofType me);
void keyof_type_lookup_names(KeyofType* me, Scope* names, ErrorCtx* errs);
void keyof_type_typecheck(KeyofType const* me, ErrorCtx* errs);

// Typeof

typedef struct TypeofType {
    Expr* expr;
    bool owns_expr;
} TypeofType;

void typeof_type_drop(TypeofType* me);
TypeofType typeof_type_clone(TypeofType me);
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
TernaryType ternary_type_clone(TernaryType me);
