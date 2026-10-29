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
// Names resolved while this is non-zero aren't uses (what tsc never resolves, but tttt does for whoever asks), and
// declarations walked meanwhile are never unused (tsc doesn't check them)
extern thread_local u32 unused_marking_suppressed;
void unused_mark_referenced(Scope const* scope, Symbol* found, u32 meaning);
// Whether the file's text mentions "@link" at all (decided once): without it no JSDoc has a link
bool unused_file_has_link_tag(struct SourceFile const* file);
// The `{@link X}` names of the JSDoc tsc attaches to the source element starting at node_start (a statement, a class,
// interface or enum member, a parameter: `trailing`) are uses, resolved from `names` (tsc's checkJSDocComments)
void unused_mark_jsdoc_links_before(struct Scope const* names, char const* node_start, struct SourceFile const* file, bool trailing);
// Everything registered in the file that nothing referenced
void unused_report(SourceFile const* file, ErrorCtx* errs, u32 order); // order: its index among the checked files
