#include "expr.h"
#include "type.h"
#include "error.h"

const Expr Expr_default = { .tag = EXPR_LITERAL, .data.literal = { .tag = VALUE_UNDEFINED } };

static inline int binary_op_get_precedence(BinaryOp op) {
    switch (op) {
        case BINARY_OP_SINGLE_EQUALS: return 10;
        case BINARY_OP_DOUBLE_EQUALS: return 20;
        case BINARY_OP_TRIPLE_EQUALS: return 20;
        case BINARY_OP_PLUS: return 50;
        case BINARY_OP_MINUS: return 50;
        case BINARY_OP_STAR: return 60;
        case BINARY_OP_SLASH: return 60;
    }
    UNREACHABLE;
}

static inline const char* binary_op_get_str(BinaryOp op) {
    switch (op) {
        case BINARY_OP_PLUS: return "+";
        case BINARY_OP_MINUS: return "-";
        case BINARY_OP_STAR: return "*";
        case BINARY_OP_SLASH: return "/";
        case BINARY_OP_SINGLE_EQUALS: return "=";
        case BINARY_OP_DOUBLE_EQUALS: return "==";
        case BINARY_OP_TRIPLE_EQUALS: return "===";
    }
    UNREACHABLE;
}

static inline const char* unary_op_get_str(UnaryOp op) {
    switch (op) {
        case UNARY_OP_BANG: return "!";
        case UNARY_OP_PLUS: return "+";
        case UNARY_OP_MINUS: return "-";
        case UNARY_OP_LEFT_PLUS_PLUS: return "++";
        case UNARY_OP_LEFT_MINUS_MINUS: return "--";
        case UNARY_OP_RIGHT_PLUS_PLUS: return "++";
        case UNARY_OP_RIGHT_MINUS_MINUS: return "--";
    }
    UNREACHABLE;
}

void Expr_drop(Expr* self) {
    switch (self->tag) {
        case EXPR_LITERAL:
            Value_drop(&self->data.literal);
            break;

        case EXPR_IDENTIFIER:
            cstr_drop(&self->data.identifier);
            break;

        case EXPR_BINARY_OP:
            Expr_drop(&self->data.binary_op->left);
            Expr_drop(&self->data.binary_op->right);
            free(self->data.binary_op);
            break;

        case EXPR_UNARY_OP:
            Expr_drop(&self->data.unary_op->expr);
            free(self->data.unary_op);
            break;

        case EXPR_CALL:
            Expr_drop(&self->data.call->callable);
            vec_Expr_drop(&self->data.call->args);
            free(self->data.call);
            break;
    }
}

cstr Expr_to_cstr(Expr* self, int indent) {
    switch (self->tag) {
        case EXPR_LITERAL:
            return Value_to_cstr(&self->data.literal, indent);

        case EXPR_IDENTIFIER: {
            cstr ret = cstr_lit(CLR_NAME);
            cstr_append_s(&ret, self->data.identifier);
            cstr_append(&ret, CLR0);
            return ret;
        }

        case EXPR_BINARY_OP: {
            cstr ret = cstr_null;
            if (self->data.binary_op->parenthesized) {
                cstr_append(&ret, "(");
            } else {
                cstr_append(&ret, CLR_IMPLICIT "(" CLR0);
            }
            cstr a = Expr_to_cstr(&self->data.binary_op->left, indent);
            cstr b = Expr_to_cstr(&self->data.binary_op->right, indent);
            cstr_append_s(&ret, a);
            cstr_append(&ret, " ");
            cstr_append(&ret, binary_op_get_str(self->data.binary_op->op));
            cstr_append(&ret, " ");
            cstr_append_s(&ret, b);
            if (self->data.binary_op->parenthesized) {
                cstr_append(&ret, ")");
            } else {
                cstr_append(&ret, CLR_IMPLICIT ")" CLR0);
            }
            cstr_drop(&a);
            cstr_drop(&b);
            return ret;
        }

        case EXPR_UNARY_OP: {
            cstr ret = cstr_null;
            if (self->data.unary_op->parenthesized) {
                cstr_append(&ret, "(");
            } else {
                cstr_append(&ret, CLR_IMPLICIT "(" CLR0);
            }
            UnaryOp op = self->data.unary_op->op;
            cstr expr = Expr_to_cstr(&self->data.unary_op->expr, indent);
            if (op < UNARY_OP_MIN_RIGHT_HAND_OP) {
                cstr_append(&ret, unary_op_get_str(op));
            }
            cstr_append_s(&ret, expr);
            if (op >= UNARY_OP_MIN_RIGHT_HAND_OP) {
                cstr_append(&ret, unary_op_get_str(op));
            }
            if (self->data.unary_op->parenthesized) {
                cstr_append(&ret, ")");
            } else {
                cstr_append(&ret, CLR_IMPLICIT ")" CLR0);
            }
            cstr_drop(&expr);
            return ret;
        }

        case EXPR_CALL: {
            cstr ret = Expr_to_cstr(&self->data.call->callable, indent);
            if (vec_Expr_empty(&self->data.call->args)) {
                cstr_append(&ret, "()");
            } else {
                cstr_append(&ret, "(");
                c_foreach(i, vec_Expr, self->data.call->args) {
                    cstr arg_str = Expr_to_cstr(i.ref, indent + 1);
                    cstr_append_s(&ret, arg_str);
                    cstr_drop(&arg_str);
                    if (i.ref < i.end - 1) {
                        cstr_append(&ret, ", ");
                    }
                }
                cstr_append_sv(&ret, c_sv(")"));
            }
            return ret;
        }
    }
    UNREACHABLE;
}

Type Expr_get_type_and_typecheck(Expr* self, ErrorCtx* errs) {
    switch (self->tag) {
        case EXPR_LITERAL:
            return Type_value_new(Value_clone(self->data.literal));

        case EXPR_IDENTIFIER:
            // TODO
            return Type_any;

        case EXPR_BINARY_OP: {
            BinaryOpExpr* bin = self->data.binary_op;
            Type type_a = Expr_get_type_and_typecheck(&bin->left, errs);
            Type type_b = Expr_get_type_and_typecheck(&bin->right, errs);
            Type ret = {0};
            switch (bin->op) {
                case BINARY_OP_PLUS:
                    if (Type_extends(&type_a, &Type_string) || Type_extends(&type_b, &Type_string)) {
                        ret = Type_string;
                    } else if (Type_extends(&type_a, &Type_number) && Type_extends(&type_b, &Type_number)) {
                        ret = Type_number;
                    } else {
                        ErrorCtx_type_error(errs, "invalid types to operator");
                        ret = Type_number;
                    }
                    break;

                case BINARY_OP_MINUS:
                case BINARY_OP_STAR:
                case BINARY_OP_SLASH:
                    if (Type_extends(&type_a, &Type_number) && Type_extends(&type_b, &Type_number)) {
                        ret = Type_number;
                    } else {
                        ErrorCtx_type_error(errs, "invalid types to operator");
                        ret = Type_number;
                    }
                    break;

                case BINARY_OP_SINGLE_EQUALS:
                    // TODO: typecheck
                    ret = type_a;
                    type_a = Type_any;
                    break;

                case BINARY_OP_DOUBLE_EQUALS:
                case BINARY_OP_TRIPLE_EQUALS:
                    // TODO: typecheck
                    ret = Type_boolean;
            }
            Type_drop(&type_a);
            Type_drop(&type_b);
            return ret;
        }

        case EXPR_UNARY_OP: {
            UnaryOpExpr* una = self->data.unary_op;
            Type type = Expr_get_type_and_typecheck(&una->expr, errs);
            Type ret = {0};
            switch (una->op) {
                // TODO: add typechecking
                case UNARY_OP_BANG: ret = Type_boolean; break;
                case UNARY_OP_PLUS: ret = Type_number; break;
                case UNARY_OP_MINUS: ret = Type_number; break;
                case UNARY_OP_LEFT_PLUS_PLUS: ret = Type_number; break;
                case UNARY_OP_LEFT_MINUS_MINUS: ret = Type_number; break;
                case UNARY_OP_RIGHT_PLUS_PLUS: ret = Type_number; break;
                case UNARY_OP_RIGHT_MINUS_MINUS: ret = Type_number; break;
            }
            Type_drop(&type);
            return ret;
        }

        case EXPR_CALL: {
            // TODO: add typechecking
            return Type_any;
        }
    }
    UNREACHABLE;
}

signed char BinaryOp_cmp_precedence(BinaryOp a, BinaryOp b) {
    int ap = binary_op_get_precedence(a);
    int bp = binary_op_get_precedence(b);
    return ap == bp ? 0 : (ap > bp ? 1 : -1);
}
