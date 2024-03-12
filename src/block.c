#include "block.h"
#include "statement.h"
#include "name_stack.h"

void Block_drop(Block* self) {
    vec_Statement_drop(&self->statements);
}

void cstr_append_block_debug(cstr* target, Block* self, int indent) {
    cstr_append_sv(target, c_sv("{\n"));
    c_foreach(i, vec_Statement, self->statements) {
        APPEND_INDENT(target, indent + 1);
        cstr_append_statement_debug(target, i.ref, indent + 1);
        cstr_append_sv(target, c_sv("\n"));
    }
    APPEND_INDENT(target, indent);
    cstr_append_sv(target, c_sv("}"));
}

void Block_resolve_names_without_stack_frame(Block* self, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_Statement, self->statements) {
        Statement_add_names(i.ref, names, errs);
    }
    c_foreach(i, vec_Statement, self->statements) {
        Statement_resolve_names(i.ref, names, errs);
    }
}

void Block_resolve_names(Block* self, NameStack* names, ErrorCtx* errs) {
    NameStack_push_frame(names, false);
    Block_resolve_names_without_stack_frame(self, names, errs);
    NameStack_pop_frame(names);
}

void Block_typecheck(Block* self, StatementCtx* ctx, ErrorCtx* errs) {
    c_foreach(i, vec_Statement, self->statements) {
        Statement_typecheck(i.ref, ctx, errs);
    }
}

void Block_resolve_names_toplevel(Block* self, ErrorCtx* errs) {
    NameStack names = NameStack_init();
    Block_resolve_names(self, &names, errs);
    NameStack_drop(&names);
}

void Block_typecheck_toplevel(Block* self, ErrorCtx* errs) {
    StatementCtx statement_ctx = StatementCtx_toplevel();
    Block_typecheck(self, &statement_ctx, errs);
    StatementCtx_drop(&statement_ctx);
}
