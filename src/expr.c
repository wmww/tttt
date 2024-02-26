#include "expr.h"

void Expr_drop(Expr* self) {
    switch (self->tag) {
        case EXPR_LITERAL:
            Value_drop(&self->data.literal);
            break;

        case EXPR_BINARY_OP:
            Expr_drop(&self->data.binary_op->a);
            Expr_drop(&self->data.binary_op->b);
            free(self->data.binary_op);
            break;
    }
}

cstr Expr_to_cstr(Expr* self, int indent) {
    switch (self->tag) {
        case EXPR_LITERAL:
            return Value_to_cstr(&self->data.literal, indent);
            break;

        case EXPR_BINARY_OP: {
            cstr ret = cstr_null;
            cstr a = Expr_to_cstr(&self->data.binary_op->a, indent);
            cstr b = Expr_to_cstr(&self->data.binary_op->b, indent);
            cstr_append_s(&ret, a);
            switch (self->data.binary_op->op) {
                case BINARY_OP_PLUS: cstr_append(&ret, " + "); break;
                case BINARY_OP_MINUS: cstr_append(&ret, " - "); break;
            }
            cstr_append_s(&ret, b);
            cstr_drop(&a);
            cstr_drop(&b);
            return ret;
        }
    }
    UNREACHABLE;
}

Expr Expr_simplify(Expr self) {
    // TODO
    return self;
}
