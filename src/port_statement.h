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
    bool type_only; // Has the "type" keyword for the main import
    bool type_modifier; // A specifier with its own `type` (`{ type a }`)
    Span source_name; // Only used for _SPECIFIC ports
    VecSpan alias_path; // Only used for PORT_IMPORT_ALIAS, the dotted name after the =
    VecSymbolCPtr alias_path_symbols; // What each item of alias_path found (maybe an alias), shorter if resolution failed
    bool export_import; // `export import foo = ...`
    bool declared; // `declare import foo = ...` (TS1029 or so, but ambient)
    bool not_top_level; // PORT_EXPORT_AS_NAMESPACE: not directly in the file (TS1316)
    Span modifiers; // PORT_EXPORT_AS_NAMESPACE: modifiers in front (TS1184, a binder error in tsc)
    bool is_require; // `import foo = require("./abc")` (a PORT_IMPORT_GATHER that isn't a namespace import)
    // JavaScript's `const a = require("m").a` (a PORT_IMPORT_SPECIFIC): a property of the module's type, so only a value
    // export, and nothing is reported without one (tsc's getTargetOfImportEqualsDeclaration, getCommonJSPropertyAccess)
    bool value_only;
    ModuleRef* module_ref; // The module to import from, null for forms that have none
    Symbol* symbol; // The alias symbol, set by scope_add_port(). Null for things that aren't aliases (`export *`)
    Symbol* source_symbol; // What source_name resolved to (which may be another alias), used by the tests
    bool jsdoc; // Of an `@import` tag (PortStatement.jsdoc)
    // PORT_IMPORT_ALIAS: an unresolved `import x = A.B` was used as a value, which checked `A` as a value (tsc's
    // markAliasSymbolAsReferenced: checkExpressionCached of the module reference)
    bool value_use_checked;
    // Of a statement in a namespace (TS1147), which resolves nothing: a use of the alias does (tsc's resolveAlias)
    bool in_namespace;
    // Of a statement in a function or block (TS1232/TS1233): declared there, but tsc's checker never resolves it
    // (checkGrammarModuleElementContext): what resolving it says is said by no one
    bool wrong_context;
    // Its name conflicted (TS2300): a symbol of its own outside the scope's table (tsc's declareSymbol), which
    // checkUnusedLocalsAndParameters never sees
    bool duplicate;
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
    if (name.sv.size >= 2 && (*name.sv.buf == '"' || *name.sv.buf == '\'')) return csview_slice(name.sv, 1, name.sv.size - 1);
    return name.sv;
}

#define g_class_key PortItem, port_item
#define g_no_clone
#include <cleat/generic/vec.h>

typedef struct PortStatement {
    Span keyword_span; // Span of "import" or "export"
    PortItem main; // The main import item (not in {}s)
    VecPortItem items; // List of curly brace-enclosed imported items
    bool side_effect_only; // `import "./abc"`
    bool is_import_require; // `import a = require("m")`
    bool requires_target; // If the form implies there should be a target. target may still be invalid, but this causes
        // a compile error
    cstr target_path; // Path of the module to import from
    Span target_path_span; // Span of the path string
    // A module specifier tsc's types baseline shows as any: a missing expression (`import` then a reserved word), or
    // a name (`from Aaa`, `require(x)`: an identifier outside an expression context, errorType)
    Span missing_specifier;
    // Any other expression as the module specifier (TS1141 from the checker: `import { * } from` parses `* }`): owned,
    // walked by the baselines
    struct Expr* bad_specifier;
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
    VecExpr decorators; // `@dec import x = y`: TS1206, only resolved (silently) for the baselines
} PortStatement;

void port_statement_drop(PortStatement* me);
void port_statement_add_names(PortStatement* me, Scope* scope);
void port_statement_lookup_names(PortStatement* me, Scope* scope, ErrorCtx* errs);
void port_statement_typecheck(PortStatement const* me, Span span, bool ambient, ErrorCtx* errs);
// TS2305 for an import of a property the value `export =` names doesn't have (also a JavaScript require destructuring's)
void port_item_check_export_equals_property(PortItem const* item, ErrorCtx* errs);
