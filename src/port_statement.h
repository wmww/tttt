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
            break;
    }
    return false;
}

void port_item_drop(PortItem* me);

#define i_key_class_names PortItem, port_item
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct PortStatement {
    Span keyword_span; // Span of "import" or "export"
    PortItem main; // The main import item (not in {}s)
    VecPortItem items; // List of curly brace-enclosed imported items
    cstr source_path; // Path of the module to import from
    Span source_path_span; // Span of the path string
} PortStatement;

void port_statement_drop(PortStatement* me);
void port_statement_add_names(PortStatement* me, Scope* scope);
void port_statement_set_source_file(PortStatement* me, SourceFile* file);
