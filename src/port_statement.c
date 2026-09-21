#include "port_statement.h"
#include "error.h"
#include "program.h"
#include "source_file.h"
#include "scope.h"

void port_item_drop(PortItem* me) {
    vec_span_drop(&my alias_path);
    vec_symbol_cptr_drop(&my alias_path_symbols);
}

void port_statement_drop(PortStatement* me) {
    port_item_drop(&my main);
    vec_port_item_drop(&my items);
    cstr_drop(&my target_path);
}

void port_statement_add_names(PortStatement* me, Scope* scope) {
    if (my module_ref && !(scope->kind == SCOPE_MODULE || (scope->container && scope->container->is_module))) {
        error_ctx_error_d(
            scope->errs,
            my keyword_span,
            1232,
            "An import declaration can only be used at the top level of a namespace or module."
        );
        return;
    }

    scope_add_port(scope, &my main);
    for_each (i, vec_port_item, &my items) {
        scope_add_port(scope, i);
    }
}


void port_statement_lookup_names(PortStatement* me, Scope* scope, ErrorCtx* errs) {
    // Ambient modules of every file are known by now. tsc doesn't mind unresolved side effect imports (`import "./a"`)
    if (my module_ref && !my side_effect_only && !my module_ref->file &&
        !binder_module_of_ref(scope->binder, my module_ref)
    ) {
        module_ref_report_unresolved(my module_ref, errs);
    } else if (my module_ref && my module_ref->file) {
        module_ref_report_resolved(my module_ref, errs);
    }
}
