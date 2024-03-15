#include "expr.h"
#include "stc/types.h"
#include "type.h"
#include "error.h"
#include "name_stack.h"
#include "callable_type.h"
#include "function_impl.h"
#include "as_type_expr.h"
#include "generic.h"

// See https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Operator_precedence
static inline int binary_op_get_precedence(BinaryOp op) {
    switch (op) {
        case BINARY_OP_STAR_STAR:
            return 130;

        case BINARY_OP_STAR:
        case BINARY_OP_SLASH:
        case BINARY_OP_MOD:
            return 120;

        case BINARY_OP_PLUS:
        case BINARY_OP_MINUS:
            return 110;

        case BINARY_OP_LESS_LESS:
        case BINARY_OP_GREATER_GREATER:
        case BINARY_OP_GREATER_GREATER_GREATER:
            return 100;

        case BINARY_OP_LESS:
        case BINARY_OP_LESS_EQUAL:
        case BINARY_OP_GREATER:
        case BINARY_OP_GREATER_EQUAL:
            return 90;

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL:
            return 80;

        case BINARY_OP_AND:
            return 70;

        case BINARY_OP_XOR:
            return 60;

        case BINARY_OP_OR:
            return 50;

        case BINARY_OP_AND_AND:
            return 40;

        case BINARY_OP_OR_OR:
        case BINARY_OP_EH_EH:
            return 30;

        case BINARY_OP_EQUAL:
        case BINARY_OP_PLUS_EQUAL:
        case BINARY_OP_MINUS_EQUAL:
        case BINARY_OP_STAR_STAR_EQUAL:
        case BINARY_OP_STAR_EQUAL:
        case BINARY_OP_SLASH_EQUAL:
        case BINARY_OP_MOD_EQUAL:
        case BINARY_OP_LESS_LESS_EQUAL:
        case BINARY_OP_GREATER_GREATER_EQUAL:
        case BINARY_OP_GREATER_GREATER_GREATER_EQUAL:
        case BINARY_OP_AND_EQUAL:
        case BINARY_OP_XOR_EQUAL:
        case BINARY_OP_OR_EQUAL:
        case BINARY_OP_AND_AND_EQUAL:
        case BINARY_OP_OR_OR_EQUAL:
        case BINARY_OP_EH_EH_EQUAL:
            return 20;

        case BINARY_OP_COMMA:
            return 10;
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

        case EXPR_AS_TYPE:
            Expr_drop(&self->as_type->expr);
            Type_drop(&self->as_type->type);
            free(self->as_type);
            break;

        case EXPR_CALL:
            Expr_drop(&self->call->callable);
            vec_Expr_drop(&self->call->args);
            free(self->call);
            break;

        case EXPR_FUNCTION:
            FunctionImpl_drop(self->function);
            free(self->function);
            break;

        case EXPR_OBJECT:
            vec_ObjectExprItem_drop(&self->object);
            break;

        case EXPR_ARRAY:
            vec_Expr_drop(&self->array);
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
            cstr_append_sv(target, czview_sv(BinaryOp_get_zv(self->binary_op->op)));
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
                cstr_append_sv(target, czview_sv(UnaryOp_get_zv(op)));
            }
            cstr_append_expr_debug(target, &self->unary_op->expr, indent);
            if (op >= UNARY_OP_MIN_RIGHT_HAND_OP) {
                cstr_append_sv(target, czview_sv(UnaryOp_get_zv(op)));
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

        case EXPR_AS_TYPE: {
            cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            cstr_append_expr_debug(target, &self->as_type->expr, indent);
            cstr_append_sv(target, c_sv(" as "));
            cstr_append_type_debug(target, &self->as_type->type, indent);
            cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
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

        case EXPR_FUNCTION:
            cstr_append_function_impl_debug(target, self->function, true, indent);
            break;

        case EXPR_OBJECT: {
            cstr_append_sv(target, c_sv("{\n"));
            c_foreach(i, vec_ObjectExprItem, self->object) {
                APPEND_INDENT(target, indent + 1);
                cstr_append_s(target, i.ref->name);
                cstr_append_sv(target, c_sv(": "));
                cstr_append_expr_debug(target, &i.ref->value, indent + 1);
                cstr_append_sv(target, c_sv(",\n"));
            }
            APPEND_INDENT(target, indent);
            cstr_append_sv(target, c_sv("}"));
        }   break;

        case EXPR_ARRAY: {
            cstr_append_sv(target, c_sv("[\n"));
            c_foreach(i, vec_Expr, self->array) {
                APPEND_INDENT(target, indent + 1);
                cstr_append_expr_debug(target, i.ref, indent + 1);
                cstr_append_sv(target, c_sv(",\n"));
            }
            APPEND_INDENT(target, indent);
            cstr_append_sv(target, c_sv("]"));
        }   break;
    }
}

void Expr_resolve_names(Expr* self, NameStack* names, ErrorCtx* errs) {
    switch (self->tag) {
        case EXPR_LITERAL:
            break;

        case EXPR_IDENTIFIER: {
            Type const* var_type = NameStack_get_variable(names, cstr_sv(&self->identifier.name));
            if (var_type) {
                self->identifier.type = var_type;
            } else {
                ErrorCtx_error(errs, self->span, 2304, "Cannot find name '%s'.", cstr_str(&self->identifier.name));
            }
        }   break;

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

        case EXPR_AS_TYPE:
            Expr_resolve_names(&self->as_type->expr, names, errs);
            Type_resolve_names(&self->as_type->type, names, errs);
            break;

        case EXPR_CALL:
            Expr_resolve_names(&self->call->callable, names, errs);
            c_foreach(i, vec_Expr, self->call->args) {
                Expr_resolve_names(i.ref, names, errs);
            }
            break;

        case EXPR_FUNCTION:
            FunctionImpl_resolve_names(self->function, names, errs);
            break;

        case EXPR_OBJECT:
            c_foreach(i, vec_ObjectExprItem, self->object) {
                Expr_resolve_names(&i.ref->value, names, errs);
            }
            break;

        case EXPR_ARRAY:
            c_foreach(i, vec_Expr, self->array) {
                Expr_resolve_names(i.ref, names, errs);
            }
            break;
    }
}

static BinaryOp BinaryOp_without_assignment(BinaryOp op) {
    switch (op) {
        case BINARY_OP_PLUS_EQUAL: return BINARY_OP_PLUS; break;
        case BINARY_OP_STAR_STAR_EQUAL: return BINARY_OP_STAR_STAR; break;
        case BINARY_OP_STAR_EQUAL: return BINARY_OP_STAR; break;
        case BINARY_OP_SLASH_EQUAL: return BINARY_OP_SLASH; break;
        case BINARY_OP_MOD_EQUAL: return BINARY_OP_MOD; break;
        case BINARY_OP_MINUS_EQUAL: return BINARY_OP_MINUS; break;
        case BINARY_OP_LESS_LESS_EQUAL: return BINARY_OP_LESS_LESS; break;
        case BINARY_OP_GREATER_GREATER_EQUAL: return BINARY_OP_GREATER_GREATER; break;
        case BINARY_OP_GREATER_GREATER_GREATER_EQUAL: return BINARY_OP_GREATER_GREATER_GREATER; break;
        case BINARY_OP_AND_EQUAL: return BINARY_OP_AND; break;
        case BINARY_OP_XOR_EQUAL: return BINARY_OP_XOR; break;
        case BINARY_OP_OR_EQUAL: return BINARY_OP_OR; break;
        case BINARY_OP_AND_AND_EQUAL: return BINARY_OP_AND_AND; break;
        case BINARY_OP_OR_OR_EQUAL: return BINARY_OP_OR_OR; break;
        case BINARY_OP_EH_EH_EQUAL: return BINARY_OP_EH_EH; break;
        default: PANIC(
            "BinaryOp_without_assignment() called with non-assignment op %s (%d)",
            BinaryOp_get_zv(op).str,
            op
        );
    }
}

static Type BinaryOp_get_type_and_typecheck(
    Type left,
    BinaryOp op,
    Type right,
    Expr* expr,
    ErrorCtx* errs
) {
    Type ret = Type_any;
    switch (op) {
        case BINARY_OP_PLUS:
            if (Type_extends(&left, &Type_string) || Type_extends(&right, &Type_string)) {
                ret = Type_string;
            } else if (Type_extends(&left, &Type_number) && Type_extends(&right, &Type_number)) {
                ret = Type_number;
            } else {
                // TODO: it seems special handling of possibly null or undefined types is needed
                if (errs) {
                    cstr message = cstr_lit("Operator '+' cannot be applied to types '");
                    cstr_append_type_error_msg(&message, &left);
                    cstr_append_sv(&message, c_sv("' and '"));
                    cstr_append_type_error_msg(&message, &right);
                    cstr_append_sv(&message, c_sv("'."));
                    ErrorCtx_error_s(errs, expr->span, 2365, message);
                }
                ret = Type_any;
            }
            break;

        case BINARY_OP_STAR_STAR:
        case BINARY_OP_STAR:
        case BINARY_OP_SLASH:
        case BINARY_OP_MOD:
        case BINARY_OP_MINUS:
        case BINARY_OP_LESS_LESS:
        case BINARY_OP_GREATER_GREATER:
        case BINARY_OP_GREATER_GREATER_GREATER:
        case BINARY_OP_AND:
        case BINARY_OP_XOR:
        case BINARY_OP_OR:
            if (!Type_extends(&left, &Type_number)) {
                ErrorCtx_error(
                    errs,
                    expr->binary_op->left.span,
                    2362,
                    "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type."
                );
            }
            if (!Type_extends(&right, &Type_number)) {
                ErrorCtx_error(
                    errs,
                    expr->binary_op->right.span,
                    2363,
                    "The right-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type."
                );
            }
            ret = Type_number;
            break;

        case BINARY_OP_EQUAL:
            switch (expr->binary_op->left.tag) {
                case EXPR_IDENTIFIER:
                    if (expr->binary_op->left.identifier.type) {
                        check_assignable_or_error(
                            &right,
                            expr->binary_op->left.identifier.type,
                            expr->binary_op->left.span,
                            errs
                        );
                    }
                    break;

                case EXPR_DOT_PROP:
                    // TODO
                    break;

                default:
                    ErrorCtx_error(
                        errs,
                        expr->binary_op->left.span,
                        2364,
                        "The left-hand side of an assignment expression must be a variable or a property access."
                    );
                    break;
            }
            ret = right;
            right = Type_any;
            break;

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL:
            if (!Type_extends(&left, &right) && !Type_extends(&right, &left)) {
                if (errs) {
                    cstr message = cstr_lit("This comparison appears to be unintentional because the types '");
                    cstr_append_type_error_msg(&message, &left);
                    cstr_append_sv(&message, c_sv("' and '"));
                    cstr_append_type_error_msg(&message, &right);
                    cstr_append_sv(&message, c_sv("' have no overlap."));
                    ErrorCtx_error_s(errs, expr->span, 2367, message);
                }
            }
            ret = Type_boolean;
            break;

        case BINARY_OP_LESS:
        case BINARY_OP_GREATER:
        case BINARY_OP_LESS_EQUAL:
        case BINARY_OP_GREATER_EQUAL:
        case BINARY_OP_AND_AND:
        case BINARY_OP_OR_OR:
            // TODO: figure out rules and typecheck
            ret = Type_boolean;
            break;

        case BINARY_OP_PLUS_EQUAL:
        case BINARY_OP_STAR_STAR_EQUAL:
        case BINARY_OP_STAR_EQUAL:
        case BINARY_OP_SLASH_EQUAL:
        case BINARY_OP_MOD_EQUAL:
        case BINARY_OP_MINUS_EQUAL:
        case BINARY_OP_LESS_LESS_EQUAL:
        case BINARY_OP_GREATER_GREATER_EQUAL:
        case BINARY_OP_GREATER_GREATER_GREATER_EQUAL:
        case BINARY_OP_AND_EQUAL:
        case BINARY_OP_XOR_EQUAL:
        case BINARY_OP_OR_EQUAL:
        case BINARY_OP_AND_AND_EQUAL:
        case BINARY_OP_OR_OR_EQUAL:
        case BINARY_OP_EH_EH_EQUAL: {
            BinaryOp without_assignment = BinaryOp_without_assignment(op);
            ret = BinaryOp_get_type_and_typecheck(Type_clone(left), without_assignment, right, expr, errs);
            ret = BinaryOp_get_type_and_typecheck(left, BINARY_OP_EQUAL, ret, expr, errs);
            left = Type_any;
            right = Type_any;
        }   break;

        case BINARY_OP_COMMA:
            // TODO: throw TS2695: Left side of comma operator is unused and has no side effects.
            ret = right;
            right = Type_any;
            break;

        case BINARY_OP_EH_EH:
            // TODO
            ret = left;
            left = Type_any;
            break;
    }
    Type_drop(&left);
    Type_drop(&right);
    return ret;
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

        case EXPR_BINARY_OP:
            return BinaryOp_get_type_and_typecheck(
                Expr_get_type_and_typecheck(&self->binary_op->left, errs),
                self->binary_op->op,
                Expr_get_type_and_typecheck(&self->binary_op->right, errs),
                self,
                errs
            );

        case EXPR_UNARY_OP: {
            UnaryOpExpr* una = self->unary_op;
            Type type = Expr_get_type_and_typecheck(&una->expr, errs);
            Type ret = {0};
            switch (una->op) {
                // TODO: add typechecking
                case UNARY_OP_BANG: ret = Type_boolean; break;
                case UNARY_OP_TILDE: ret = Type_number; break;
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

        case EXPR_AS_TYPE: {
            // TODO: check that type of expr overlaps with (not extends or is extended by) type. if it doesn't surface
            // TS2352: Conversion of type 'number' to type 'string' may be a mistake because neither type sufficiently overlaps with the other. If this was intentional, convert the expression to 'unknown' first.
            return Type_clone(self->as_type->type);
        }

        case EXPR_CALL: {
            Type ret = Type_any;
            Type callable = Expr_get_type_and_typecheck(&self->call->callable, errs);
            if (callable.tag == TYPE_GENERIC) {
                Type inferred = Generic_callable_infer(callable.generic, &self->call->args, errs);
                Type_drop(&callable);
                callable = inferred;
            }
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
                ErrorCtx_type_error(errs, "expr is not callable");
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

        case EXPR_FUNCTION:
            FunctionImpl_typecheck(self->function, errs);
            return Type_clone(self->function->type);

        case EXPR_OBJECT: {
            // TODO: add typechecking
            return Type_any;
        }

        case EXPR_ARRAY: {
            Type array_type = Type_value(Value_undefined);
            c_foreach(i, vec_Expr, self->array) {
                Type t = Expr_get_type_and_typecheck(i.ref, errs);
                if (array_type.tag == TYPE_VALUE && array_type.value.tag == VALUE_UNDEFINED) {
                    Type_abstract_literals(&t);
                    Type_drop(&array_type);
                    array_type = t;
                } else if (Type_extends(&t, &array_type)) {
                    Type_drop(&t);
                } else {
                    Type_drop(&t);
                    Type_drop(&array_type);
                    array_type = Type_any;
                }
            }
            return Type_array(c_new(Type, array_type));
        }
    }
    UNREACHABLE;
}

signed char BinaryOp_cmp_precedence(BinaryOp a, BinaryOp b) {
    int ap = binary_op_get_precedence(a);
    int bp = binary_op_get_precedence(b);
    return ap == bp ? 0 : (ap > bp ? 1 : -1);
}

czview BinaryOp_get_zv(BinaryOp op) {
    switch (op) {
        case BINARY_OP_STAR_STAR: return c_zv("**");
        case BINARY_OP_STAR: return c_zv("*");
        case BINARY_OP_SLASH: return c_zv("/");
        case BINARY_OP_MOD: return c_zv("%");
        case BINARY_OP_PLUS: return c_zv("+");
        case BINARY_OP_MINUS: return c_zv("-");
        case BINARY_OP_LESS_LESS: return c_zv("<<");
        case BINARY_OP_GREATER_GREATER: return c_zv(">>");
        case BINARY_OP_GREATER_GREATER_GREATER: return c_zv(">>>");
        case BINARY_OP_LESS: return c_zv("<");
        case BINARY_OP_LESS_EQUAL: return c_zv("<=");
        case BINARY_OP_GREATER: return c_zv(">");
        case BINARY_OP_GREATER_EQUAL: return c_zv(">=");
        case BINARY_OP_DOUBLE_EQUAL: return c_zv("==");
        case BINARY_OP_NOT_EQUAL: return c_zv("!=");
        case BINARY_OP_TRIPLE_EQUAL: return c_zv("===");
        case BINARY_OP_TRIPLE_NOT_EQUAL: return c_zv("!==");
        case BINARY_OP_AND: return c_zv("&");
        case BINARY_OP_XOR: return c_zv("^");
        case BINARY_OP_OR: return c_zv("|");
        case BINARY_OP_AND_AND: return c_zv("&&");
        case BINARY_OP_OR_OR: return c_zv("||");
        case BINARY_OP_EH_EH: return c_zv("\?\?");
        case BINARY_OP_EQUAL: return c_zv("=");
        case BINARY_OP_PLUS_EQUAL: return c_zv("+=");
        case BINARY_OP_MINUS_EQUAL: return c_zv("-=");
        case BINARY_OP_STAR_STAR_EQUAL: return c_zv("**=");
        case BINARY_OP_STAR_EQUAL: return c_zv("*=");
        case BINARY_OP_SLASH_EQUAL: return c_zv("/=");
        case BINARY_OP_MOD_EQUAL: return c_zv("%=");
        case BINARY_OP_LESS_LESS_EQUAL: return c_zv("<<=");
        case BINARY_OP_GREATER_GREATER_EQUAL: return c_zv(">>=");
        case BINARY_OP_GREATER_GREATER_GREATER_EQUAL: return c_zv(">>>=");
        case BINARY_OP_AND_EQUAL: return c_zv("&=");
        case BINARY_OP_XOR_EQUAL: return c_zv("^=");
        case BINARY_OP_OR_EQUAL: return c_zv("|=");
        case BINARY_OP_AND_AND_EQUAL: return c_zv("&&=");
        case BINARY_OP_OR_OR_EQUAL: return c_zv("||=");
        case BINARY_OP_EH_EH_EQUAL: return c_zv("\?\?=");
        case BINARY_OP_COMMA: return c_zv(",");
    }
    NONFATAL("invalid binary operator %d", op);
    return c_zv("<INVALID_OP>");
}

czview UnaryOp_get_zv(UnaryOp op) {
    switch (op) {
        case UNARY_OP_BANG: return c_zv("!");
        case UNARY_OP_TILDE: return c_zv("~");
        case UNARY_OP_PLUS: return c_zv("+");
        case UNARY_OP_MINUS: return c_zv("-");
        case UNARY_OP_LEFT_PLUS_PLUS: return c_zv("++");
        case UNARY_OP_LEFT_MINUS_MINUS: return c_zv("--");
        case UNARY_OP_RIGHT_PLUS_PLUS: return c_zv("++");
        case UNARY_OP_RIGHT_MINUS_MINUS: return c_zv("--");
    }
    NONFATAL("invalid unary operator %d", op);
    return c_zv("<INVALID_OP>");
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
