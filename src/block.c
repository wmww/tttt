#include "block.h"
#include "statement.h"
#include "name_stack.h"
#include "error.h"

void block_drop(Block* me) {
    vec_statement_drop(&my statements);
}

void cstr_append_block_debug(cstr* target, Block* me, int indent) {
    cstr_append_sv(target, c_sv("{\n"));
    c_foreach(i, vec_statement, my statements) {
        APPEND_INDENT(target, indent + 1);
        cstr_append_statement_debug(target, i.ref, indent + 1);
        cstr_append_sv(target, c_sv("\n"));
    }
    APPEND_INDENT(target, indent);
    cstr_append_sv(target, c_sv("}"));
}

void block_resolve_names_without_stack_frame(Block const* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_statement, my statements) {
        statement_add_names(i.ref, names, errs);
    }
    c_foreach(i, vec_statement, my statements) {
        statement_resolve_names(i.ref, names, errs);
    }
}

void block_resolve_names(Block const* me, NameStack* names, ErrorCtx* errs) {
    name_stack_push_block_frame(names);
    block_resolve_names_without_stack_frame(me, names, errs);
    name_stack_pop_frame(names);
}

void block_typecheck(Block* me, StatementCtx* ctx, ErrorCtx* errs) {
    c_foreach(i, vec_statement, my statements) {
        statement_typecheck(i.ref, ctx, errs);
    }
}

void block_resolve_names_toplevel(Block* me, ErrorCtx* errs) {
    ErrorPhase prev_phase = error_ctx_swap_phase(errs, ERROR_PHASE_RESOLVE_NAMES);
    NameStack* names = name_stack_new(errs);
    block_resolve_names(me, names, errs);
    name_stack_destroy(names);
    error_ctx_swap_phase(errs, prev_phase);
}

void block_typecheck_toplevel(Block* me, ErrorCtx* errs) {
    ErrorPhase prev_phase = error_ctx_swap_phase(errs, ERROR_PHASE_TYPECHECK);
    StatementCtx statement_ctx = statement_ctx_toplevel();
    block_typecheck(me, &statement_ctx, errs);
    statement_ctx_drop(&statement_ctx);
    error_ctx_swap_phase(errs, prev_phase);
}
