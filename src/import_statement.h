#pragma once

#include "statement.h"

/*
 *    module_name      path
 *             \       /
 * import * as foo from './file'
 *
 * items[0].source/name     items[1].name
 *         |  items[1].source   |    path
 *         |      /      ,------'     /
 * import {foo, bar as baz} from './file'
 */

typedef struct SourceFile SourceFile;

typedef struct ImportItem {
    bool type_only;
    Span source;
    Span name; // may be same as source
    Type imported_value_type;
    NameStack const* name_stack; // unowned
    NamedVariable* named_var;
    NamedType* named_type;
} ImportItem;

void import_item_drop(ImportItem* me);

#define i_key_class_names ImportItem, import_item
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct ImportStatement {
    Span import_keyword_span;
    bool type_only;
    Span default_name;
    NamedVariable* default_named_var;
    NamedType* default_named_type;
    Span module_name;
    Type imported_value_type;
    NameStack const* name_stack; // unowned
    NamedVariable* module_named_var;
    NamedType* module_named_type;
    VecImportItem items;
    cstr path;
    Span path_span;
    SourceFile const* source_file;
} ImportStatement;

void import_statement_drop(ImportStatement* me);
void import_statement_add_names(ImportStatement* me, NameStack* names, ErrorCtx* errs);
