#pragma once

#include "statement.h"
#include "declaration.h"

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
    bool type_only; // Has the "type" keyword for the main import
    Span source_name; // Only used for _SPECIFIC ports
    VecSpan alias_path; // Only used for PORT_IMPORT_ALIAS, the dotted name after the =
    VecDeclarationCPtr alias_path_decls; // What each item of alias_path resolved to, shorter if resolution failed
    bool alias_conflict; // An alias who's name was already taken, which is fine if the target turns out to have no value
    bool export_import; // `export import foo = ...`
    SourceFile* source_file; // File to import from, set by import_statement_set_source_file()
    bool error_reported; // Set and used by Scope
    ArcDeclaration declaration; // Set by Scope
    Declaration const* source_decl; // Set by Scope, used by the tests
    Declaration wrapped_declaration; // Used for gather statements, just need someone to own it
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

#define g_class_key PortItem, port_item
#define g_no_clone
#include <cleat/generic/vec.h>

typedef struct PortStatement {
    Span keyword_span; // Span of "import" or "export"
    PortItem main; // The main import item (not in {}s)
    VecPortItem items; // List of curly brace-enclosed imported items
    bool requires_target; // If the form implies there should be a target. target may still be invalid, but this causes
        // a compile error
    cstr target_path; // Path of the module to import from
    Span target_path_span; // Span of the path string
} PortStatement;

void port_statement_drop(PortStatement* me);
void port_statement_assign_source(PortStatement* me, SourceFile* source);
void port_statement_add_names(PortStatement* me, Scope* scope);
void port_statement_resolve_names(PortStatement* me, ScopeStack* names, ErrorCtx* errs);
