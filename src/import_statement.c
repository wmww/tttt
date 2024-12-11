#include "import_statement.h"
#include "error.h"
#include "program.h"

void import_item_drop(ImportItem* me) {
    arc_declaration_drop(&my declaration);
}

void import_statement_drop(ImportStatement* me) {
    import_item_drop(&my main);
    vec_import_item_drop(&my items);
    cstr_drop(&my source_path);
}

void import_statement_add_names(ImportStatement* me, Scope* scope) {
    if (span_valid(my source_path_span)) {
        if (!scope->export_scope) {
            error_ctx_error(
                scope->errs,
                my import_keyword_span,
                1232,
                "An import declaration can only be used at the top level of a namespace or module."
            );
            return;
        }

        my main.source_file = program_load_import(scope->errs->program, my source_path_span, cstr_sv(&my source_path));
    } else {
        my main.source_file = c_const_cast(SourceFile*, my import_keyword_span.source);
    }
    c_foreach(i, vec_import_item, my items) {
        i.ref->source_file = my main.source_file;
    }

    switch (my main.source_tag) {
        case IMPORT_SOURCE_DEFAULT:
        case IMPORT_SOURCE_WILDCARD:
            scope_add_import(scope, &my main);
            break;

        default: break;
    }

    c_foreach(i, vec_import_item, my items) {
        ASSERT(i.ref->source_tag == IMPORT_SOURCE_SPECIFIC);
        scope_add_import(scope, i.ref);
    }
}
