#include "expr.h"
#include "type.h"
#include "error.h"
#include "name_stack.h"
#include "callable_type.h"

const Expr Expr_default = { .tag = EXPR_LITERAL, .literal = { .tag = VALUE_UNDEFINED } };

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
            Value_drop(&self->literal);
            break;

        case EXPR_IDENTIFIER:
            cstr_drop(&self->identifier.name);
            break;

        case EXPR_BINARY_OP:
            Expr_drop(&self->binary_op->left);
            Expr_drop(&self->binary_op->right);
            free(self->binary_op);
            break;

        case EXPR_UNARY_OP:
            Expr_drop(&self->unary_op->expr);
            free(self->unary_op);
            break;

        case EXPR_DOT_PROP:
            Expr_drop(&self->dot_prop->expr);
            cstr_drop(&self->dot_prop->prop);
            free(self->dot_prop);
            break;

        case EXPR_CALL:
            Expr_drop(&self->call->callable);
            vec_Expr_drop(&self->call->args);
            free(self->call);
            break;

        case EXPR_OBJECT:
            vec_ObjectExprItem_drop(&self->object);
            break;
    }
}

void cstr_append_expr_debug(cstr* target, Expr const* self, int indent) {
    switch (self->tag) {
        case EXPR_LITERAL: cstr_append_value_debug(target, &self->literal, indent); break;

        case EXPR_IDENTIFIER:
            cstr_append_sv(target, c_sv(CLR_NAME));
            cstr_append_s(target, self->identifier.name);
            cstr_append_sv(target, c_sv(CLR0));
            break;

        case EXPR_BINARY_OP: {
            if (self->binary_op->parenthesized) {
                cstr_append_sv(target, c_sv("("));
            } else {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            }
            cstr_append_expr_debug(target, &self->binary_op->left, indent);
            cstr_append_sv(target, c_sv(" "));
            cstr_append_sv(target, binary_op_get_sv(self->binary_op->op));
            cstr_append_sv(target, c_sv(" "));
            cstr_append_expr_debug(target, &self->binary_op->right, indent);
            if (self->binary_op->parenthesized) {
                cstr_append_sv(target, c_sv(")"));
            } else {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
        }   break;

        case EXPR_UNARY_OP: {
            if (self->unary_op->parenthesized) {
                cstr_append_sv(target, c_sv("("));
            } else {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            }
            UnaryOp op = self->unary_op->op;
            if (op < UNARY_OP_MIN_RIGHT_HAND_OP) {
                cstr_append_sv(target, unary_op_get_sv(op));
            }
            cstr_append_expr_debug(target, &self->unary_op->expr, indent);
            if (op >= UNARY_OP_MIN_RIGHT_HAND_OP) {
                cstr_append_sv(target, unary_op_get_sv(op));
            }
            if (self->unary_op->parenthesized) {
                cstr_append_sv(target, c_sv(")"));
            } else {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
        }   break;

        case EXPR_DOT_PROP: {
            cstr_append_expr_debug(target, &self->dot_prop->expr, indent);
            cstr_append_sv(target, c_sv("."));
            cstr_append_s(target, self->dot_prop->prop);
        }   break;

        case EXPR_CALL: {
            cstr_append_expr_debug(target, &self->call->callable, indent);
            if (vec_Expr_empty(&self->call->args)) {
                cstr_append_sv(target, c_sv("()"));
            } else {
                cstr_append_sv(target, c_sv("("));
                c_foreach(i, vec_Expr, self->call->args) {
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
            int size = vec_ObjectExprItem_size(&self->object);
            if (size > 1) {
                cstr_append_sv(target, c_sv("\n"));
                c_foreach(i, vec_ObjectExprItem, self->object) {
                    APPEND_INDENT(target, indent + 1);
                    cstr_append_s(target, i.ref->name);
                    cstr_append_sv(target, c_sv(": "));
                    cstr_append_expr_debug(target, &i.ref->value, indent);
                    cstr_append_sv(target, c_sv(",\n"));
                }
                APPEND_INDENT(target, indent);
            } else if (size == 1) {
                cstr_append_sv(target, c_sv(" "));
                cstr_append_s(target, vec_ObjectExprItem_at(&self->object, 0)->name);
                cstr_append_sv(target, c_sv(": "));
                cstr_append_expr_debug(target, &vec_ObjectExprItem_at(&self->object, 0)->value, indent);
                cstr_append_sv(target, c_sv(" "));
            }
            cstr_append_sv(target, c_sv("}"));
        }   break;
    }
}

void Expr_resolve_names(Expr* self, NameStack* names, ErrorCtx* errs) {
    switch (self->tag) {
        case EXPR_LITERAL:
            break;

        case EXPR_IDENTIFIER:
            c_foreach_rev(i, vec_NameStackFrame, names->stack) {
                hmap_str_CTypePtr_value const* result = hmap_str_CTypePtr_get(
                    &i.ref->values,
                    cstr_sv(&self->identifier.name)
                );
                if (result) {
                    self->identifier.type = result->second;
                    return;
                }
            }
            ErrorCtx_type_error(errs, "unkown identifier");
            break;

        case EXPR_BINARY_OP:
            Expr_resolve_names(&self->binary_op->left, names, errs);
            Expr_resolve_names(&self->binary_op->right, names, errs);
            break;

        case EXPR_UNARY_OP:
            Expr_resolve_names(&self->unary_op->expr, names, errs);
            break;

        case EXPR_DOT_PROP:
            Expr_resolve_names(&self->dot_prop->expr, names, errs);
            // TODO: resolve name of prop
            break;

        case EXPR_CALL:
            Expr_resolve_names(&self->call->callable, names, errs);
            c_foreach(i, vec_Expr, self->call->args) {
                Expr_resolve_names(i.ref, names, errs);
            }
            break;

        case EXPR_OBJECT:
            c_foreach(i, vec_ObjectExprItem, self->object) {
                Expr_resolve_names(&i.ref->value, names, errs);
            }
            break;
    }
}

Type Expr_get_type_and_typecheck(Expr* self, ErrorCtx* errs) {
    switch (self->tag) {
        case EXPR_LITERAL:
            return Type_value(Value_clone(self->literal));

        case EXPR_IDENTIFIER:
            if (self->identifier.type) {
                return Type_clone(*self->identifier.type);
            } else {
                return Type_any;
            }

        case EXPR_BINARY_OP: {
            BinaryOpExpr* bin = self->binary_op;
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
                    switch (bin->left.tag) {
                        case EXPR_IDENTIFIER:
                            if (bin->left.identifier.type && !Type_extends(bin->left.identifier.type, &type_b)) {
                                ErrorCtx_type_error(errs, "wrong type in assignment");
                            }
                            break;

                        case EXPR_DOT_PROP:
                            // TODO
                            break;

                        default:
                            ErrorCtx_type_error(errs, "invalid left-hand side of assignment");
                            break;
                    }
                    ret = type_b;
                    type_b = Type_any;
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
            UnaryOpExpr* una = self->unary_op;
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
            Type ret = Type_any;
            Type callable = Expr_get_type_and_typecheck(&self->call->callable, errs);
            long given_arg_count = vec_Expr_size(&self->call->args);
            long callable_arg_count = 0;
            if (callable.tag == TYPE_CALLABLE) {
                callable_arg_count = vec_CallableTypeArg_size(&callable.callable->args);
                // TODO: handle optional and variadic arguments
                if (callable_arg_count != given_arg_count) {
                    ErrorCtx_type_error(errs, "wrong number of arguments");
                }
                ret = Type_clone(callable.callable->ret);
            } else if (callable.tag != TYPE_ANY) {
                ErrorCtx_type_error(errs, "value is not callable");
            }
            for (long i = 0; i < given_arg_count; i++) {
                Type arg_type = Expr_get_type_and_typecheck(
                    vec_Expr_at_mut(&self->call->args, i),
                    errs
                );
                if (i < callable_arg_count) {
                    if (!Type_extends(&arg_type, &vec_CallableTypeArg_at(&callable.callable->args, i)->type)) {
                        ErrorCtx_type_error(errs, "bad argument type");
                    }
                }
                Type_drop(&arg_type);
            }
            Type_drop(&callable);
            return ret;
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
