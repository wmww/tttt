#pragma once

#include "statement.h"
#include "declaration.h"

/*
 *    module_name           path
 *             \            /
 * import * as foo from './file'
 *
 * module_name         path
 *         \          /
 * import foo from './file'
 * is_import_default: true
 *
 * items[0].source/name     items[1].name
 *         |  items[1].source   |    path
 *         |      /      .------'     /
 * import {foo, bar as baz} from './file'
 */

typedef struct SourceFile SourceFile;

typedef struct ImportItem {
    bool type_only;
    // No source enum, as it's alway IMPORT_SOURCE_SPECIFIC
    Span source_name;
    Span decl_name; // may be same as source
} ImportItem;

void import_item_drop(ImportItem* me);

#define i_key_class_names ImportItem, import_item
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct ImportStatement {
    Span import_keyword_span; // Span of "import" or "export"
    bool type_only; // Has the "type" keyword for the main import
    bool is_export; // Is a re-export (uses "export" keyword)
    ImportSource source; // What to pull from the imported file (never IMPORT_SOURCE_SPECIFIC)
    Span module_name; // The name to give to the imported type/value
    VecImportItem items; // List of curly brace-enclosed imported items
    cstr path; // Path of the module to import from
    Span path_span; // Span of the path string
    SourceFile* source_file; // Populated by Program
} ImportStatement;

void import_statement_drop(ImportStatement* me);
void import_statement_add_names(ImportStatement* me, Scope* scope);
