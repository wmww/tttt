#pragma once

#include "prelude.h"
#include "span.h"

// noUnusedLocals / noUnusedParameters: tsc's checkUnusedIdentifiers. The resolve walk marks what names are used with
// (Symbol.referenced, unused_mark_referenced()) and registers the declarations that may be unused with their file
// (unused_register()); after checking, unused_report() says which of those nothing referenced. All of it only runs
// when one of the options is on

typedef struct Scope Scope;
typedef struct Symbol Symbol;
typedef struct SourceFile SourceFile;
typedef struct ErrorCtx ErrorCtx;

typedef enum UnusedKind : uint8_t {
    UNUSED_STATEMENT, // Statement: a declaration of a variable, function, class, interface, type alias, enum,
                      // namespace, or an import
    UNUSED_VARIABLES, // VariableStatement of a `for`, span: the declaration list
    UNUSED_FOR_IN_OF_VARIABLES, // Same of a `for-in/of`, where `_x` is fine
    UNUSED_FUNCTION, // FunctionImpl: its parameters and type parameters
    UNUSED_CLASS, // ClassDecl: private members, type parameters
    UNUSED_INTERFACE, // InterfaceDecl: type parameters
    UNUSED_TYPE_ALIAS, // TypeDecl: type parameters
    UNUSED_INFER, // InferredType
} UnusedKind;

typedef struct UnusedCandidate {
    UnusedKind kind;
    void const* node;
} UnusedCandidate;

// Either option is on
bool unused_enabled(void);
// Frees the lists unused_register() grew out of (each thread's growth retires the old one instead of freeing it in
// another thread's malloc arena): after checking, with no walk running
void unused_free_retired(void);
// A declaration (list) in the scope the walk is in that may turn out unused
void unused_register(Scope const* scope, UnusedKind kind, void const* node);
// `found` is what a name looked up from `scope` with `meaning` found, and the use reads it (tsc's resolveName with
// isUse): it is referenced unless the use is inside its own declaration (tsc's isSelfReferenceLocation: a recursive
// function, `class C { m() { C } }`, see Scope.self)
// Names resolved while this is non-zero aren't uses (what tsc never resolves, but tttt does for whoever asks)
extern thread_local u32 unused_marking_suppressed;
void unused_mark_referenced(Scope const* scope, Symbol* found, u32 meaning);
// The names of the file's JSDoc `{@link X}` tags are uses. Part of name resolution, before checking
void unused_mark_jsdoc_links(SourceFile const* file);
void unused_mark_leading_jsdoc_links(struct Scope const* names, char const* statement_start, char const* file_start);
// Everything registered in the file that nothing referenced
void unused_report(SourceFile const* file, ErrorCtx* errs);
