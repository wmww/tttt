#pragma once

#include "statement.h"

typedef struct ImportItem {
    bool type_only;
    Span source;
    Span name; // may be same as source
} ImportItem;

#define i_key_names ImportItem, import_item
#include "wrap_stc/vec.h"

typedef struct ImportStatement {
    bool type_only;
    Span default_name;
    Span module_name;
    VecImportItem items;
    cstr path;
    Span path_span;
} ImportStatement;

void import_statement_drop(ImportStatement* me);
void cstr_append_import_statement_debug(cstr* target, ImportStatement* me, int indent);
void import_statement_add_names(ImportStatement* me, NameStack* names, ErrorCtx* errs);
