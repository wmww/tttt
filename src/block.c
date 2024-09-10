#include "block.h"
#include "statement.h"
#include "name_stack.h"
#include "error.h"

void block_drop(Block* me) {
    vec_statement_drop(&my statements);
}

void block_add_names(Block const* me, Scope* scope) {
    c_foreach(i, vec_statement, my statements) {
        statement_add_names(i.ref, scope);
    }
}

void block_lookup_names(Block const* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_statement, my statements) {
        statement_resolve_names(i.ref, names, errs);
    }
}

void block_resolve_names(Block const* me, NameStack* names, ErrorCtx* errs) {
    Scope scope = scope_init(errs);
    block_add_names(me, &scope);
    scope_push_to_stack(&scope, names);
    block_lookup_names(me, names, errs);
    scope_drop(&scope);
}

void block_typecheck(Block* me, StatementCtx* ctx, ErrorCtx* errs) {
    c_foreach(i, vec_statement, my statements) {
        statement_typecheck(i.ref, ctx, errs);
    }
}
