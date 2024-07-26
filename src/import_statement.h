#pragma once

#include "statement.h"

typedef struct ImportItem {
    bool type_only;
    Span source;
    Span name; // may be same as source
    NamedVariable* named_var;
    NamedType* named_type;
} ImportItem;

void import_item_drop(ImportItem* me);

#define i_key_class_names ImportItem, import_item
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct ImportStatement {
    bool type_only;
    Span default_name;
    NamedVariable* default_named_var;
    NamedType* default_named_type;
    Span module_name;
    NamedVariable* module_named_var;
    NamedType* module_named_type;
    VecImportItem items;
    cstr path;
    Span path_span;
} ImportStatement;

void import_statement_drop(ImportStatement* me);
void cstr_append_import_statement_debug(cstr* target, ImportStatement* me, int indent);
void import_statement_add_names(ImportStatement* me, NameStack* names, ErrorCtx* errs);
