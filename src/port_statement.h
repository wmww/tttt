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
    Span decl_name; // May be same as source_name, empty only if is_export_wildcard is true
    Span full_span; // Span of declaration
    bool type_only; // Has the "type" keyword for the main import
    bool is_export; // Is a re-export (uses "export" keyword)
    bool is_export_wildcard; // If this is an "export * from '...'" statement
    enum {
        IMPORT_SOURCE_NONE = 0, // Nothing is imported
        IMPORT_SOURCE_DEFAULT, // Import the default exported item
        IMPORT_SOURCE_WILDCARD, // Collect all exported items into a single namespace
        IMPORT_SOURCE_SPECIFIC, // Import an item with a specific name
        IMPORT_SOURCE_LOCAL, // Used when exporting local items by name
    } source_tag;
    Span source_name; // Only used for IMPORT_SOURCE_SPECIFIC
    SourceFile* source_file; // File to import from, set by import_statement_set_source_file()
    bool error_reported; // Set and used by Scope
    ArcDeclaration declaration; // Set by Scope
} PortItem;

void port_item_drop(PortItem* me);

#define i_key_class_names PortItem, port_item
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct PortStatement {
    Span import_keyword_span; // Span of "import" or "export"
    PortItem main; // The main import item (not in {}s)
    VecPortItem items; // List of curly brace-enclosed imported items
    cstr source_path; // Path of the module to import from
    Span source_path_span; // Span of the path string
} PortStatement;

void port_statement_drop(PortStatement* me);
void port_statement_add_names(PortStatement* me, Scope* scope);
void port_statement_set_source_file(PortStatement* me, SourceFile* file);
