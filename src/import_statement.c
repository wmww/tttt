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

    if (my source != IMPORT_SOURCE_NONE) {
        scope_add_import(scope, (NamedImport){
            .decl_name = my module_name,
            .source = my source,
            .source_file = my source_file,
        });
    }

    c_foreach(i, vec_import_item, my items) {
        scope_add_import(scope, (NamedImport){
            .decl_name = i.ref->decl_name,
            .source = IMPORT_SOURCE_SPECIFIC,
            .source_name = i.ref->source_name,
            .source_file = my source_file,
        });
    }
}
