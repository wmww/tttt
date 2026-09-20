#include "block.h"
#include "statement.h"
#include "port_statement.h"
#include "scope.h"
#include "error.h"
#include "ty/ty.h"

void block_drop(Block* me) {
    vec_statement_drop(&my statements);
}

void block_add_names(Block const* me, Scope* scope) {
    bool hoist = scope_is_var_container(scope);
    for_each (i, vec_statement, &my statements) {
        statement_add_names(i, scope);
        // The `var`s nested in it. A variable statement of this block (labeled or not) was just declared
        Statement const* inner = i;
        while (inner->tag == STATEMENT_LABELED) inner = inner->labeled.body;
        if (hoist && inner->tag != STATEMENT_DECL_VARIABLE) statement_hoist_names(i, scope);
    }
}

bool block_has_export_declarations(Block const* me) {
    for_each (i, vec_statement, &my statements) {
        if (i->tag == STATEMENT_EXPORT_DEFAULT_EXPR) return true;
        if (i->tag == STATEMENT_IMPORT_EXPORT && span_equals(i->import_export->keyword_span, "export") &&
            !i->import_export->main.export_import
        ) {
            return true;
        }
    }
    return false;
}

void block_lookup_names(Block const* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_statement, &my statements) {
        statement_resolve_names(i, names, errs);
    }
}

void block_resolve_names(Block const* me, Scope* names, ErrorCtx* errs) {
    Scope* scope = scope_new(names, SCOPE_BLOCK);
    block_add_names(me, scope);
    block_lookup_names(me, scope, errs);
}

void block_collect_return_types(Block const* me, TyBuf* out) {
    for_each (i, vec_statement, &my statements) {
        statement_collect_return_types(i, out);
    }
}

void block_typecheck(Block* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    for_each (i, vec_statement, &my statements) {
        statement_typecheck(i, ctx, errs);
    }
}
