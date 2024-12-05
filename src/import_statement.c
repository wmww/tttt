#include "import_statement.h"
#include "error.h"

void import_item_drop(ImportItem* me) {
    arc_declaration_drop(&my declaration);
}

void import_statement_drop(ImportStatement* me) {
    import_item_drop(&my main);
    vec_import_item_drop(&my items);
    cstr_drop(&my source_path);
}

void import_statement_add_names(ImportStatement* me, Scope* scope) {
    if (!my main.source_file) {
        error_ctx_error(
            scope->errs,
            my import_keyword_span,
            1232,
            "An import declaration can only be used at the top level of a namespace or module."
        );
        return;
    }

    if (my main.source_tag != IMPORT_SOURCE_NONE) {
        scope_add_import(scope, &my main);
    }

    c_foreach(i, vec_import_item, my items) {
        ASSERT(i.ref->source_tag == IMPORT_SOURCE_SPECIFIC);
        scope_add_import(scope, i.ref);
    }
}

void import_statement_set_source_file(ImportStatement* me, SourceFile* file) {
    ASSERT(!my main.source_file);
    my main.source_file = file;
    c_foreach(i, vec_import_item, my items) {
        ASSERT(!i.ref->source_file);
        i.ref->source_file = file;
    }
}
