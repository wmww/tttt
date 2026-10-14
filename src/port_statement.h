#pragma once

#include "statement.h"
#include "lazy_type.h"
#include "scope.h"
#include "import_attributes.h"

// Covers both import and export statements

/*
 *     decl_name           source_path
 *             \            /
 * import * as foo from './file'
 *
 * decl_name         source_path
 *         \          /
 * import foo from './file'
 * is_import_default: true
 *
 * items[0].source/decl_name   items[1].decl_name
 *         |items[1].source_name|    source_path
 *         |      /      .------'     /
 * export {foo, bar as baz} from './file'
 */

typedef struct SourceFile SourceFile;

typedef struct PortItem {
    enum {
        PORT_NOOP = 0,          // import {...} from './abc' (noop is used for main item, which is empty)
        PORT_IMPORT_DEFAULT,    // import foo from './abc'
        PORT_IMPORT_GATHER,     // import * as foo from './abc'
        PORT_IMPORT_SPECIFIC,   // import {foo as bar} from './abc'
        PORT_IMPORT_ALIAS,      // import foo = a.b.c (`import foo = require('./abc')` is a PORT_IMPORT_GATHER)
        PORT_EXPORT_SPECIFIC,   // export {foo as bar}
        PORT_EXPORT_AS_NAMESPACE, // export as namespace foo (this doesn't seem to do anything??)
        PORT_REEXPORT_GATHER,   // export * as foo from './abc'
        PORT_REEXPORT_SPLAT,    // export * from './abc'
        PORT_REEXPORT_SPECIFIC, // export {foo as bar} from './abc'
        // export default ... is handled by STATEMENT_EXPORT_DEFAULT_EXPR
    } tag;
    Span decl_name; // May be same as source_name, empty only if is_export_wildcard is true
    Span full_span; // Span of declaration
    Span keyword_span; // The statement's `import` or `export` (set when the item is declared)
    Span source_name; // Only used for _SPECIFIC ports
    VecSpan alias_path; // Only used for PORT_IMPORT_ALIAS, the dotted name after the =
    VecSymbolCPtr alias_path_symbols; // What each item of alias_path found (maybe an alias), shorter if resolution failed
    Span modifiers; // PORT_EXPORT_AS_NAMESPACE: modifiers in front (TS1184, a binder error in tsc)
    Span clause; // PORT_IMPORT_DEFAULT: the import clause, `A, { b }` (tsc's error node for the default import)
    ModuleRef* module_ref; // The module to import from, null for forms that have none
    Symbol* symbol; // The alias symbol, set by scope_add_port(). Null for things that aren't aliases (`export *`)
    Symbol* source_symbol; // What source_name resolved to (which may be another alias), used by the tests
    bool type_only; // Has the "type" keyword for the main import
    bool type_modifier; // A specifier with its own `type` (`{ type a }`)
    bool export_import; // `export import foo = ...`
    bool declared; // `declare import foo = ...` (TS1029 or so, but ambient)
    bool not_top_level; // PORT_EXPORT_AS_NAMESPACE: not directly in the file (TS1316)
    bool is_require; // `import foo = require("./abc")` (a PORT_IMPORT_GATHER that isn't a namespace import)
    bool require_binding; // JavaScript's `const x = require("m")` or `const { x } = require("m")` (InitializedBinding.require_ports)
    bool jsdoc; // Of an `@import` tag (PortStatement.jsdoc)
    // Of a source phase import (PortStatement.source_phase): its module isn't resolved, the default binding is the
    // module's source (tsgo's getTargetOfImportClause), the others nothing (TS18112)
    bool source_phase;
    // PORT_IMPORT_ALIAS: an unresolved `import x = A.B` was used as a value, which checked `A` as a value (tsc's
    // markAliasSymbolAsReferenced: checkExpressionCached of the module reference)
    bool value_use_checked;
    // Of a statement in a namespace (TS1147), which resolves nothing: a use of the alias does (tsc's resolveAlias)
    bool in_namespace;
    // Of a statement in a function or block (TS1232/TS1233): declared there, but tsc's checker never resolves it
    // (checkGrammarModuleElementContext): what resolving it says is said by no one
    bool wrong_context;
    // An import attribute value isn't a string literal (TS2837/TS2858): the clause isn't checked, nothing its resolution
    // finds is reported (tsc's checkExternalImportOrExportDeclaration returning false)
    bool clause_unchecked;
    // Its name conflicted (TS2300): a symbol of its own outside the scope's table (tsc's declareSymbol), which
    // checkUnusedLocalsAndParameters never sees
    bool duplicate;
    // An import's name that is a string (TS1003): declared with a missing name, decl_name still spans the string
    bool decl_name_missing;
} PortItem;

static inline bool port_item_is_export(PortItem const* me) {
    switch (my tag) {
        case PORT_EXPORT_SPECIFIC:
        case PORT_EXPORT_AS_NAMESPACE:
        case PORT_REEXPORT_SPECIFIC:
        case PORT_REEXPORT_SPLAT:
        case PORT_REEXPORT_GATHER:
            return true;

        case PORT_NOOP:
        case PORT_IMPORT_DEFAULT:
        case PORT_IMPORT_GATHER:
        case PORT_IMPORT_SPECIFIC:
        case PORT_IMPORT_ALIAS:
            break;
    }
    return false;
}

void port_item_drop(PortItem* me);
// A use of an alias of an import of a module in a namespace (TS1147, whose statement resolves nothing): what resolves
// it, TS2307 if its module isn't found (once, in the import's own file). Returns whether the alias is one
bool port_item_check_namespace_import_use(struct Symbol const* alias, struct Scope const* names);

// The name an import/export specifier stands for: `a`, or the contents of `"a"` (ES2022's arbitrary module namespace
// identifiers). Spans keep the quotes, which is how the baselines show them
static inline csview port_item_name(Span name) {
    if (name.size >= 2 && (*name.buf == '"' || *name.buf == '\'')) return csview_slice(span_sv(name), 1, name.size - 1);
    return span_sv(name);
}

#define g_class_key PortItem, port_item
#define g_no_clone
#include <cleat/generic/vec.h>

typedef struct PortStatement {
    Span keyword_span; // Span of "import" or "export"
    PortItem main; // The main import item (not in {}s)
    VecPortItem items; // List of curly brace-enclosed imported items
    // The phase modifier is `source` (a source phase import: nothing is resolved, the default binding is an
    // AbstractModuleSource), else `defer` when the rare part's phase_clause is valid
    bool source_phase;
    bool side_effect_only; // `import "./abc"`
    bool is_import_require; // `import a = require("m")`
    bool requires_target; // If the form implies there should be a target. target may still be invalid, but this causes
        // a compile error
    Span target_path_span; // Span of the path string (read the path with port_statement_target_path())
    ModuleRef* module_ref; // Made by the parser when there is a target path
    ImportAttributes* attributes; // Owned, null if there are none
    bool jsdoc; // A JavaScript file's `@import` tag (type-only, not part of the baselines' walk, no module indicator)
    // Directly in a namespace body (not an ambient module's), set by port_statement_add_names(): an import of a module
    // is TS1147, an export declaration TS1194 (unless the namespace is ambient and nothing is re-exported)
    bool in_namespace;
    bool in_ambient_namespace;
    // Directly in `declare module "m"` of a script (an ambient external module, not an augmentation): it can't name a
    // relative module (TS2439)
    bool in_ambient_module;
    bool wrong_context; // In a block or function (TS1232/TS1233): not declared, nothing resolved
    struct PortStatementRare* rare; // Owned, null when all of it is: read through port_statement_rare()
} PortStatement;

// What few import/export statements have (written through port_statement_rare_mut())
typedef struct PortStatementRare {
    // `import defer * as ns from "m"`, `import source s from "m"`: the import clause, from the phase modifier to the
    // bindings' end (tsc's ImportClause with a defer or source PhaseModifier, TS18058-TS18060, TS18111-TS18115)
    Span phase_clause;
    // The path of the module to import from while there is no module_ref (which takes it): read it with
    // port_statement_target_path()
    cstr target_path;
    // A module specifier tsc's types baseline shows as any: a missing expression (`import` then a reserved word), or
    // a name (`from Aaa`, `require(x)`: an identifier outside an expression context, errorType)
    Span missing_specifier;
    // Any other expression as the module specifier (TS1141 from the checker: `import { * } from` parses `* }`): owned,
    // walked by the baselines
    struct Expr* bad_specifier;
    // A module specifier that isn't a string literal: TS1141, the checker's (tsc's checkExternalImportOrExportDeclaration),
    // so not in a statement in the wrong context, which only says that (port_statement_add_names())
    Span non_string_specifier;
    VecExpr decorators; // `@dec import x = y`: TS1206, only resolved (silently) for the baselines
} PortStatementRare;

extern const PortStatementRare port_statement_no_rare;
static inline PortStatementRare const* port_statement_rare(PortStatement const* me) {
    return my rare ? my rare : &port_statement_no_rare;
}
// Allocates it if needed
PortStatementRare* port_statement_rare_mut(PortStatement* me);
// Frees the rare part if nothing in it is set any more
void port_statement_rare_trim(PortStatement* me);

// The module specifier (empty if there is none)
csview port_statement_target_path(PortStatement const* me);

void port_statement_drop(PortStatement* me);
void port_statement_add_names(PortStatement* me, Scope* scope);
void port_statement_lookup_names(PortStatement* me, Scope* scope, ErrorCtx* errs);
void port_statement_typecheck(PortStatement const* me, Span span, bool ambient, ErrorCtx* errs);
// TS2305 for an import of a property the value `export =` names doesn't have (also a JavaScript require destructuring's)
void port_item_check_export_equals_property(PortItem const* item, ErrorCtx* errs);
// TS18042 for JavaScript's `const t = require("m")` of something that is only a type
void port_item_check_js_require_type(PortItem const* item, ErrorCtx* errs);
