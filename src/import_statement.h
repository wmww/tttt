#pragma once

#include "statement.h"
#include "declaration.h"

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
 * import {foo, bar as baz} from './file'
 */

typedef struct SourceFile SourceFile;

typedef struct ImportItem {
    Span decl_name; // May be same as source_name
    Span full_span; // Span of declaration
    bool type_only; // Has the "type" keyword for the main import
    bool is_export; // Is a re-export (uses "export" keyword)
    bool is_export_wildcard; // If this is an "export * from '...'" statement
    enum {
        IMPORT_SOURCE_NONE = 0, // Nothing is imported
        IMPORT_SOURCE_DEFAULT, // Import the default exported item
        IMPORT_SOURCE_WILDCARD, // Collect all exported items into a single namespace
        IMPORT_SOURCE_SPECIFIC, // Import an item with a specific name
    } source_tag;
    Span source_name; // Only used for IMPORT_SOURCE_SPECIFIC
    SourceFile* source_file; // File to import from, set by import_statement_set_source_file()
    bool error_reported; // Set and used by Scope
    ArcDeclaration declaration; // Set by Scope
} ImportItem;

void import_item_drop(ImportItem* me);

#define i_key_class_names ImportItem, import_item
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct ImportStatement {
    Span import_keyword_span; // Span of "import" or "export"
    ImportItem main; // The main import item (not in {}s)
    VecImportItem items; // List of curly brace-enclosed imported items
    cstr source_path; // Path of the module to import from
    Span source_path_span; // Span of the path string
} ImportStatement;

void import_statement_drop(ImportStatement* me);
void import_statement_add_names(ImportStatement* me, Scope* scope);
void import_statement_set_source_file(ImportStatement* me, SourceFile* file);
