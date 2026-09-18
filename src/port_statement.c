#include "port_statement.h"
#include "error.h"
#include "program.h"
#include "source_file.h"

void port_item_drop(PortItem* me) {
    arc_declaration_drop(&my declaration);
    declaration_drop(&my wrapped_declaration);
}

void port_statement_drop(PortStatement* me) {
    port_item_drop(&my main);
    vec_port_item_drop(&my items);
    cstr_drop(&my target_path);
}

void port_statement_assign_source(PortStatement* me, SourceFile* source) {
    my main.source_file = source;
    for_each (i, vec_port_item, &my items) {
        i->source_file = my main.source_file;
    }
}

void port_statement_add_names(PortStatement* me, Scope* scope) {
    if (span_valid(my target_path_span)) {
        if (!scope->export_scope) {
            error_ctx_error_d(
                scope->errs,
                my keyword_span,
                1232,
                "An import declaration can only be used at the top level of a namespace or module."
            );
            return;
        }
        if (!my main.source_file) {
            error_ctx_error_d(
                scope->errs, my target_path_span, 2307,
                "Cannot find module '", my target_path, "' or its corresponding type declarations."
            );
        }
    }

    scope_add_port(scope, &my main);
    for_each (i, vec_port_item, &my items) {
        scope_add_port(scope, i);
    }
}
