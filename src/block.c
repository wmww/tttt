#include "block.h"
#include "statement.h"
#include "name_stack.h"
#include "error.h"

void block_drop(Block* me) {
    vec_statement_drop(&my statements);
}

void block_add_names(Block const* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_statement, my statements) {
        statement_add_names(i.ref, names, errs);
    }
}

void block_lookup_names(Block const* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_statement, my statements) {
        statement_resolve_names(i.ref, names, errs);
    }
}

void block_resolve_names(Block const* me, NameStack* names, ErrorCtx* errs) {
    name_stack_push_block_frame(names);
    block_add_names(me, names, errs);
    block_lookup_names(me, names, errs);
    name_stack_pop_frame(names);
}

void block_typecheck(Block* me, StatementCtx* ctx, ErrorCtx* errs) {
    c_foreach(i, vec_statement, my statements) {
        statement_typecheck(i.ref, ctx, errs);
    }
}
