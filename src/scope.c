#include "statement.h"

void Scope_drop(Scope* self) {
    vec_Statement_drop(&self->statements);
}

cstr Scope_to_cstr(Scope* self, int indent) {
    cstr ret = cstr_lit("{\n");
    c_foreach(i, vec_Statement, self->statements) {
        APPEND_INDENT(&ret, indent + 1);
        cstr statement = Statement_to_cstr(i.ref, indent + 1);
        cstr_append_s(&ret, statement);
        cstr_drop(&statement);
        cstr_append(&ret, "\n");
    }
    APPEND_INDENT(&ret, indent);
    cstr_append_sv(&ret, c_sv("}"));
    return ret;
}

Scope scope_simplify(Scope self) {
    c_foreach(i, vec_Statement, self.statements) {
        *i.ref = statement_simplify(*i.ref);
    }
    return self;
}
