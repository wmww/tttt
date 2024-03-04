#include "expr.h"
#include "type.h"
#include "error.h"

const Expr Expr_default = { .tag = EXPR_LITERAL, .data.literal = { .tag = VALUE_UNDEFINED } };

static inline int binary_op_get_precedence(BinaryOp op) {
    switch (op) {
        case BINARY_OP_SINGLE_EQUAL: return 10;
        case BINARY_OP_DOUBLE_EQUAL: return 20;
        case BINARY_OP_TRIPLE_EQUAL: return 20;
        case BINARY_OP_LESS: return 20;
        case BINARY_OP_GREATER: return 20;
        case BINARY_OP_LESS_EQUAL: return 20;
        case BINARY_OP_GREATER_EQUAL: return 20;
        case BINARY_OP_NOT_EQUAL: return 20;
        case BINARY_OP_DOUBLE_NOT_EQUAL: return 20;
        case BINARY_OP_PLUS: return 50;
        case BINARY_OP_MINUS: return 50;
        case BINARY_OP_STAR: return 60;
        case BINARY_OP_SLASH: return 60;
    }
    UNREACHABLE;
}

static inline csview binary_op_get_sv(BinaryOp op) {
    switch (op) {
        case BINARY_OP_PLUS: return c_sv("+");
        case BINARY_OP_MINUS: return c_sv("-");
        case BINARY_OP_STAR: return c_sv("*");
        case BINARY_OP_SLASH: return c_sv("/");
        case BINARY_OP_SINGLE_EQUAL: return c_sv("=");
        case BINARY_OP_DOUBLE_EQUAL: return c_sv("==");
        case BINARY_OP_TRIPLE_EQUAL: return c_sv("===");
        case BINARY_OP_LESS: return c_sv("<");
        case BINARY_OP_GREATER: return c_sv(">");
        case BINARY_OP_LESS_EQUAL: return c_sv("<=");
        case BINARY_OP_GREATER_EQUAL: return c_sv(">=");
        case BINARY_OP_NOT_EQUAL: return c_sv("!=");
        case BINARY_OP_DOUBLE_NOT_EQUAL: return c_sv("!==");
    }
    UNREACHABLE;
}

static inline csview unary_op_get_sv(UnaryOp op) {
    switch (op) {
        case UNARY_OP_BANG: return c_sv("!");
        case UNARY_OP_PLUS: return c_sv("+");
        case UNARY_OP_MINUS: return c_sv("-");
        case UNARY_OP_LEFT_PLUS_PLUS: return c_sv("++");
        case UNARY_OP_LEFT_MINUS_MINUS: return c_sv("--");
        case UNARY_OP_RIGHT_PLUS_PLUS: return c_sv("++");
        case UNARY_OP_RIGHT_MINUS_MINUS: return c_sv("--");
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

        case EXPR_DOT_PROP:
            Expr_drop(&self->data.dot_prop->expr);
            cstr_drop(&self->data.dot_prop->prop);
            free(self->data.dot_prop);
            break;

        case EXPR_CALL:
            Expr_drop(&self->data.call->callable);
            vec_Expr_drop(&self->data.call->args);
            free(self->data.call);
            break;

        case EXPR_OBJECT:
            vec_ObjectExprItem_drop(&self->data.object);
            break;
    }
}

void cstr_append_expr_debug(cstr* target, Expr const* self, int indent) {
    switch (self->tag) {
        case EXPR_LITERAL: cstr_append_value_debug(target, &self->data.literal, indent); break;

        case EXPR_IDENTIFIER:
            cstr_append_sv(target, c_sv(CLR_NAME));
            cstr_append_s(target, self->data.identifier);
            cstr_append_sv(target, c_sv(CLR0));
            break;

        case EXPR_BINARY_OP: {
            if (self->data.binary_op->parenthesized) {
                cstr_append_sv(target, c_sv("("));
            } else {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            }
            cstr_append_expr_debug(target, &self->data.binary_op->left, indent);
            cstr_append_sv(target, c_sv(" "));
            cstr_append_sv(target, binary_op_get_sv(self->data.binary_op->op));
            cstr_append_sv(target, c_sv(" "));
            cstr_append_expr_debug(target, &self->data.binary_op->right, indent);
            if (self->data.binary_op->parenthesized) {
                cstr_append_sv(target, c_sv(")"));
            } else {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
        }   break;

        case EXPR_UNARY_OP: {
            if (self->data.unary_op->parenthesized) {
                cstr_append_sv(target, c_sv("("));
            } else {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            }
            UnaryOp op = self->data.unary_op->op;
            if (op < UNARY_OP_MIN_RIGHT_HAND_OP) {
                cstr_append_sv(target, unary_op_get_sv(op));
            }
            cstr_append_expr_debug(target, &self->data.unary_op->expr, indent);
            if (op >= UNARY_OP_MIN_RIGHT_HAND_OP) {
                cstr_append_sv(target, unary_op_get_sv(op));
            }
            if (self->data.unary_op->parenthesized) {
                cstr_append_sv(target, c_sv(")"));
            } else {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
        }   break;

        case EXPR_DOT_PROP: {
            cstr_append_expr_debug(target, &self->data.dot_prop->expr, indent);
            cstr_append_sv(target, c_sv("."));
            cstr_append_s(target, self->data.dot_prop->prop);
        }   break;

        case EXPR_CALL: {
            cstr_append_expr_debug(target, &self->data.call->callable, indent);
            if (vec_Expr_empty(&self->data.call->args)) {
                cstr_append_sv(target, c_sv("()"));
            } else {
                cstr_append_sv(target, c_sv("("));
                c_foreach(i, vec_Expr, self->data.call->args) {
                    cstr_append_expr_debug(target, i.ref, indent + 1);
                    if (i.ref < i.end - 1) {
                        cstr_append_sv(target, c_sv(", "));
                    }
                }
                cstr_append_sv(target, c_sv(")"));
            }
        }   break;

        case EXPR_OBJECT: {
            cstr_append_sv(target, c_sv("{"));
            int size = vec_ObjectExprItem_size(&self->data.object);
            if (size > 1) {
                cstr_append_sv(target, c_sv("\n"));
                c_foreach(i, vec_ObjectExprItem, self->data.object) {
                    APPEND_INDENT(target, indent + 1);
                    cstr_append_s(target, i.ref->name);
                    cstr_append_sv(target, c_sv(": "));
                    cstr_append_expr_debug(target, &i.ref->value, indent);
                    cstr_append_sv(target, c_sv(",\n"));
                }
                APPEND_INDENT(target, indent);
            } else if (size == 1) {
                cstr_append_sv(target, c_sv(" "));
                cstr_append_s(target, vec_ObjectExprItem_at(&self->data.object, 0)->name);
                cstr_append_sv(target, c_sv(": "));
                cstr_append_expr_debug(target, &vec_ObjectExprItem_at(&self->data.object, 0)->value, indent);
                cstr_append_sv(target, c_sv(" "));
            }
            cstr_append_sv(target, c_sv("}"));
        }   break;
    }
}

Type Expr_get_type_and_typecheck(Expr* self, ErrorCtx* errs) {
    switch (self->tag) {
        case EXPR_LITERAL:
            return Type_value(Value_clone(self->data.literal));

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

                case BINARY_OP_SINGLE_EQUAL:
                    // TODO: typecheck
                    ret = type_a;
                    type_a = Type_any;
                    break;

                case BINARY_OP_DOUBLE_EQUAL:
                case BINARY_OP_TRIPLE_EQUAL:
                case BINARY_OP_LESS:
                case BINARY_OP_GREATER:
                case BINARY_OP_LESS_EQUAL:
                case BINARY_OP_GREATER_EQUAL:
                case BINARY_OP_NOT_EQUAL:
                case BINARY_OP_DOUBLE_NOT_EQUAL:
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

        case EXPR_DOT_PROP: {
            // TODO: add typechecking
            return Type_any;
        }

        case EXPR_CALL: {
            // TODO: add typechecking
            return Type_any;
        }

        case EXPR_OBJECT: {
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

void ObjectExprItem_drop(ObjectExprItem* self) {
    cstr_drop(&self->name);
    Expr_drop(&self->value);
}

ObjectExprItem* ObjectExpr_get(ObjectExpr* self, const char* name) {
    c_foreach(i, vec_ObjectExprItem, *self) {
        if (strcmp(cstr_str(&i.ref->name), name) == 0) {
            return i.ref;
        }
    }
    return nullptr;
}
