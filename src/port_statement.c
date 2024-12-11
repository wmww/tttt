#include "port_statement.h"
#include "error.h"
#include "program.h"

void port_item_drop(PortItem* me) {
    arc_declaration_drop(&my declaration);
}

void port_statement_drop(PortStatement* me) {
    port_item_drop(&my main);
    vec_port_item_drop(&my items);
    cstr_drop(&my source_path);
}

void port_statement_add_names(PortStatement* me, Scope* scope) {
    if (span_valid(my source_path_span)) {
        if (!scope->export_scope) {
            error_ctx_error(
                scope->errs,
                my keyword_span,
                1232,
                "An import declaration can only be used at the top level of a namespace or module."
            );
            return;
        }
        my main.source_file = program_load_import(scope->errs->program, my source_path_span, cstr_sv(&my source_path));
    } else {
        my main.source_file = c_const_cast(SourceFile*, my keyword_span.source);
    }

    c_foreach(i, vec_port_item, my items) {
        i.ref->source_file = my main.source_file;
    }

    scope_add_port(scope, &my main);
    c_foreach(i, vec_port_item, my items) {
        scope_add_port(scope, i.ref);
    }
}
