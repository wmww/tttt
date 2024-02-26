#include "expr.h"
#include "type.h"
#include "error.h"

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

Type Expr_get_type_and_typecheck(Expr* self, ErrorCtx* errs) {
    switch (self->tag) {
        case EXPR_LITERAL:
            return Type_value_new(Value_clone(self->data.literal));

        case EXPR_BINARY_OP: {
            BinaryOp* bin = self->data.binary_op;
            Type type_a = Expr_get_type_and_typecheck(&bin->a, errs);
            Type type_b = Expr_get_type_and_typecheck(&bin->b, errs);
            Type ret = {0};
            switch (bin->op) {
                case BINARY_OP_PLUS:
                    if (Type_extends(&type_a, &Type_string) || Type_extends(&type_b, &Type_string)) {
                        ret = Type_string;
                    } else if (Type_extends(&type_a, &Type_number) && Type_extends(&type_b, &Type_number)) {
                        ret = Type_number;
                    } else {
                        ErrorCtx_type_error(errs, "invalid types to '+' operator");
                        ret = Type_number;
                    }
                    break;

                case BINARY_OP_MINUS:
                    if (Type_extends(&type_a, &Type_number) && Type_extends(&type_b, &Type_number)) {
                        ret = Type_number;
                    } else {
                        ErrorCtx_type_error(errs, "invalid types to '-' operator");
                        ret = Type_number;
                    }
                    break;
            }
            Type_drop(&type_a);
            Type_drop(&type_b);
            return ret;
        }
    }
    UNREACHABLE;
}
