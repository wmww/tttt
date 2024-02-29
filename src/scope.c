#include "scope.h"
#include "statement.h"

void Scope_drop(Scope* self) {
    vec_Statement_drop(&self->statements);
}

void cstr_append_scope_debug(cstr* target, Scope* self, int indent) {
    cstr_append_sv(target, c_sv("{\n"));
    c_foreach(i, vec_Statement, self->statements) {
        APPEND_INDENT(target, indent + 1);
        cstr_append_statement_debug(target, i.ref, indent + 1);
        cstr_append_sv(target, c_sv("\n"));
    }
    APPEND_INDENT(target, indent);
    cstr_append_sv(target, c_sv("}"));
}

void Scope_typecheck(Scope* self, ErrorCtx* errs) {
    c_foreach(i, vec_Statement, self->statements) {
        Statement_typecheck(i.ref, errs);
    }
}
