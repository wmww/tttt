#include "statement.h"

void Statement_drop(Statement* self) {
    switch (self->tag) {
        case STATEMENT_INTERFACE:
            NamedInterface_drop(&self->data.interface);
            break;

        case STATEMENT_SCOPE:
            Scope_drop(&self->data.scope);
            break;

        case STATEMENT_EXPR:
            Expr_drop(&self->data.expr);
            break;
    }
}

cstr Statement_to_cstr(Statement* self, int indent) {
    switch (self->tag) {
        case STATEMENT_INTERFACE:
            return NamedInterface_to_cstr(&self->data.interface, indent);

        case STATEMENT_SCOPE:
            return Scope_to_cstr(&self->data.scope, indent);

        case STATEMENT_EXPR: {
            cstr ret = Expr_to_cstr(&self->data.expr);
            cstr_append(&ret, ";");
            return ret;
        }
    }
    UNREACHABLE;
}

Statement statement_simplify(Statement self) {
    switch (self.tag) {
        case STATEMENT_INTERFACE:
            self.data.interface.content = interface_simplify(self.data.interface.content);
            break;

        case STATEMENT_SCOPE:
            self.data.scope = scope_simplify(self.data.scope);
            break;

        case STATEMENT_EXPR:
            break;
    }
    return self;
}
