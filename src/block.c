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

Type block_evaluate_return_type(Block* me) {
    Type ret = type_composite_base(TYPE_UNION);
    c_foreach(i, vec_statement, my statements) {
        ret = type_composite(TYPE_UNION, ret, statement_evaluate_return_type(i.ref));
    }
    return ret;
}

void block_typecheck(Block* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    c_foreach(i, vec_statement, my statements) {
        statement_typecheck(i.ref, ctx, errs);
    }
}
