#include "import_statement.h"
#include "error.h"

void import_item_drop(ImportItem* me) {
    ignore me;
}

void import_statement_drop(ImportStatement* me) {
    vec_import_item_drop(&my items);
    cstr_drop(&my path);
}

void import_statement_add_names(ImportStatement* me, Scope* scope) {
    if (!my source_file) {
        error_ctx_error(
            scope->errs,
            my import_keyword_span,
            1232,
            "An import declaration can only be used at the top level of a namespace or module."
        );
        return;
    }

    if (span_valid(my default_name)) {
        scope_add_import(scope, (NamedImport){
            .decl_name = my default_name,
            .source_name = span_null,
            .source_file = my source_file,
        });
    }

    c_foreach(i, vec_import_item, my items) {
        scope_add_import(scope, (NamedImport){
            .decl_name = i.ref->name,
            .source_name = i.ref->source,
            .source_file = my source_file,
        });
    }
}
