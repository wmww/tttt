#include "expr.h"
#include "stc/types.h"
#include "type.h"
#include "error.h"
#include "name_stack.h"
#include "callable_type.h"
#include "function_impl.h"
#include "as_type_expr.h"
#include "generic.h"
#include "class_decl.h"
#include "interface_member.h"

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

void expr_drop(Expr* me) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
            break;

        case EXPR_LITERAL:
            value_drop(&my literal);
            break;

        case EXPR_IDENTIFIER:
            cstr_drop(&my identifier.name);
            arc_named_variable_drop(&my identifier.var);
            break;

        case EXPR_BINARY_OP:
            expr_drop(&my binary_op->left);
            expr_drop(&my binary_op->right);
            free(my binary_op);
            break;

        case EXPR_UNARY_OP:
            expr_drop(&my unary_op->expr);
            free(my unary_op);
            break;

        case EXPR_PROPERTY:
            expr_drop(&my property->expr);
            expr_drop(&my property->prop);
            free(my property);
            break;

        case EXPR_AS_TYPE:
            expr_drop(&my as_type->expr);
            type_drop(&my as_type->type);
            free(my as_type);
            break;

        case EXPR_CALL:
            expr_drop(&my call->callable);
            vec_expr_drop(&my call->args);
            free(my call);
            break;

        case EXPR_FUNCTION:
            function_impl_drop(my function);
            free(my function);
            break;

        case EXPR_OBJECT:
            vec_object_expr_item_drop(&my object);
            break;

        case EXPR_ARRAY:
            vec_expr_drop(&my array);
            break;

        case EXPR_CLASS_DECL:
            class_decl_drop(my class_decl);
            free(my class_decl);
            break;
    }
}

void cstr_append_expr_debug(cstr* target, Expr const* me, int indent) {
    for (int i = 0; i < my peren_depth; i++) {
        cstr_append_sv(target, c_sv("("));
    }
    switch (my tag) {
        case EXPR_IMPLICIT_ANY: cstr_append_sv(target, c_sv("/* implicit any */")); break;

        case EXPR_LITERAL: cstr_append_value_debug(target, &my literal, indent); break;

        case EXPR_IDENTIFIER:
            cstr_append_sv(target, c_sv(CLR_NAME));
            cstr_append_s(target, my identifier.name);
            cstr_append_sv(target, c_sv(CLR0));
            break;

        case EXPR_BINARY_OP: {
            if (!my peren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            }
            cstr_append_expr_debug(target, &my binary_op->left, indent);
            cstr_append_sv(target, c_sv(" "));
            cstr_append_sv(target, czview_sv(binary_op_get_zv(my binary_op->op)));
            cstr_append_sv(target, c_sv(" "));
            cstr_append_expr_debug(target, &my binary_op->right, indent);
            if (!my peren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
        }   break;

        case EXPR_UNARY_OP: {
            if (!my peren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            }
            UnaryOp op = my unary_op->op;
            if (op < UNARY_OP_MIN_RIGHT_HAND_OP) {
                cstr_append_sv(target, czview_sv(unary_op_get_zv(op)));
            }
            cstr_append_expr_debug(target, &my unary_op->expr, indent);
            if (op >= UNARY_OP_MIN_RIGHT_HAND_OP) {
                cstr_append_sv(target, czview_sv(unary_op_get_zv(op)));
            }
            if (!my peren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
        }   break;

        case EXPR_PROPERTY: {
            cstr_append_expr_debug(target, &my property->expr, indent);
            switch (my property->form) {
                case PROPERTY_EXPR_DOT:
                    cstr_append_sv(target, c_sv("."));
                    ASSERT(my property->prop.tag == EXPR_LITERAL && my property->prop.literal.tag == VALUE_STRING);
                    cstr_append_s(target, my property->prop.literal.string);
                    break;

                case PROPERTY_EXPR_BRACKET:
                    cstr_append_sv(target, c_sv("["));
                    cstr_append_expr_debug(target, &my property->prop, indent);
                    cstr_append_sv(target, c_sv("]"));
            }
        }   break;

        case EXPR_AS_TYPE: {
            cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            cstr_append_expr_debug(target, &my as_type->expr, indent);
            cstr_append_sv(target, c_sv(" as "));
            cstr_append_type_debug(target, &my as_type->type, indent);
            cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
        }   break;

        case EXPR_CALL: {
            if (my call->uses_new) {
                cstr_append_sv(target, c_sv("new "));
            }
            cstr_append_expr_debug(target, &my call->callable, indent);
            if (vec_expr_empty(&my call->args)) {
                cstr_append_sv(target, c_sv("()"));
            } else {
                cstr_append_sv(target, c_sv("("));
                c_foreach(i, vec_expr, my call->args) {
                    cstr_append_expr_debug(target, i.ref, indent + 1);
                    if (i.ref < i.end - 1) {
                        cstr_append_sv(target, c_sv(", "));
                    }
                }
                cstr_append_sv(target, c_sv(")"));
            }
        }   break;

        case EXPR_FUNCTION:
            cstr_append_function_impl_debug(target, my function, true, indent);
            break;

        case EXPR_OBJECT: {
            cstr_append_sv(target, c_sv("{\n"));
            c_foreach(i, vec_object_expr_item, my object) {
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
            c_foreach(i, vec_expr, my array) {
                APPEND_INDENT(target, indent + 1);
                cstr_append_expr_debug(target, i.ref, indent + 1);
                cstr_append_sv(target, c_sv(",\n"));
            }
            APPEND_INDENT(target, indent);
            cstr_append_sv(target, c_sv("]"));
        }   break;

        case EXPR_CLASS_DECL:
            cstr_append_class_decl_debug(target, my class_decl, indent);
            break;
    }
    for (int i = 0; i < my peren_depth; i++) {
        cstr_append_sv(target, c_sv(")"));
    }
}

void expr_resolve_names(Expr* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL:
            break;

        case EXPR_IDENTIFIER: {
            ArcNamedVariable const* var = name_stack_get_variable(names, cstr_sv(&my identifier.name));
            if (var) {
                my identifier.var = arc_named_variable_clone(*var);
            } else if (name_stack_get_type(names, cstr_sv(&my identifier.name), nullptr)) {
                error_ctx_error(
                    errs, my span, 2693,
                    "'%s' only refers to a type, but is being used as a value here.",
                    cstr_str(&my identifier.name)
                );
            } else {
                error_ctx_error(errs, my span, 2304, "Cannot find name '%s'.", cstr_str(&my identifier.name));
            }
        }   break;

        case EXPR_BINARY_OP:
            expr_resolve_names(&my binary_op->left, names, errs);
            expr_resolve_names(&my binary_op->right, names, errs);
            break;

        case EXPR_UNARY_OP:
            expr_resolve_names(&my unary_op->expr, names, errs);
            break;

        case EXPR_PROPERTY:
            expr_resolve_names(&my property->expr, names, errs);
            expr_resolve_names(&my property->prop, names, errs);
            break;

        case EXPR_AS_TYPE:
            expr_resolve_names(&my as_type->expr, names, errs);
            type_resolve_names(&my as_type->type, names, errs);
            break;

        case EXPR_CALL:
            expr_resolve_names(&my call->callable, names, errs);
            c_foreach(i, vec_expr, my call->args) {
                expr_resolve_names(i.ref, names, errs);
            }
            break;

        case EXPR_FUNCTION:
            function_impl_resolve_names(my function, names, errs);
            break;

        case EXPR_OBJECT:
            c_foreach(i, vec_object_expr_item, my object) {
                expr_resolve_names(&i.ref->value, names, errs);
            }
            break;

        case EXPR_ARRAY:
            c_foreach(i, vec_expr, my array) {
                expr_resolve_names(i.ref, names, errs);
            }
            break;

        case EXPR_CLASS_DECL:
            class_decl_resolve_names(my class_decl, names, errs);
            break;
    }
}

static BinaryOp binary_op_without_assignment(BinaryOp op) {
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
            "binary_op_without_assignment() called with non-assignment op %s (%d)",
            binary_op_get_zv(op).str,
            op
        );
    }
}

static Type binary_op_get_type_and_typecheck(
    Type left,
    BinaryOp op,
    Type right,
    Expr* expr,
    ErrorCtx* errs
) {
    Type ret = type_any;
    switch (op) {
        case BINARY_OP_PLUS:
            if (type_extends(&left, &type_string) || type_extends(&right, &type_string)) {
                ret = type_string;
            } else if (type_extends(&left, &type_number) && type_extends(&right, &type_number)) {
                ret = type_number;
            } else {
                // TODO: it seems special handling of possibly null or undefined types is needed
                if (errs) {
                    cstr message = cstr_lit("Operator '+' cannot be applied to types '");
                    cstr_append_type_error_msg(&message, &left);
                    cstr_append_sv(&message, c_sv("' and '"));
                    cstr_append_type_error_msg(&message, &right);
                    cstr_append_sv(&message, c_sv("'."));
                    error_ctx_error_s(errs, expr->span, 2365, message);
                }
                ret = type_any;
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
            if (!type_extends(&left, &type_number)) {
                error_ctx_error(
                    errs,
                    expr->binary_op->left.span,
                    2362,
                    "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type."
                );
            }
            if (!type_extends(&right, &type_number)) {
                error_ctx_error(
                    errs,
                    expr->binary_op->right.span,
                    2363,
                    "The right-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type."
                );
            }
            ret = type_number;
            break;

        case BINARY_OP_EQUAL:
            switch (expr->binary_op->left.tag) {
                case EXPR_IDENTIFIER:
                    if (expr->binary_op->left.identifier.var.get) {
                        check_assignable_or_error(
                            &right,
                            expr->binary_op->left.identifier.var.get->type,
                            expr->binary_op->left.span,
                            errs
                        );
                    }
                    break;

                case EXPR_PROPERTY:
                    // TODO
                    break;

                default:
                    error_ctx_error(
                        errs,
                        expr->binary_op->left.span,
                        2364,
                        "The left-hand side of an assignment expression must be a variable or a property access."
                    );
                    break;
            }
            ret = right;
            right = type_any;
            break;

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL:
            if (!type_extends(&left, &right) && !type_extends(&right, &left)) {
                if (errs) {
                    cstr message = cstr_lit("This comparison appears to be unintentional because the types '");
                    cstr_append_type_error_msg(&message, &left);
                    cstr_append_sv(&message, c_sv("' and '"));
                    cstr_append_type_error_msg(&message, &right);
                    cstr_append_sv(&message, c_sv("' have no overlap."));
                    error_ctx_error_s(errs, expr->span, 2367, message);
                }
            }
            ret = type_boolean;
            break;

        case BINARY_OP_LESS:
        case BINARY_OP_GREATER:
        case BINARY_OP_LESS_EQUAL:
        case BINARY_OP_GREATER_EQUAL:
            // TODO: figure out rules and typecheck
            ret = type_boolean;
            break;

        case BINARY_OP_AND_AND:
        case BINARY_OP_OR_OR:
            // TODO: use union type once we support it
            if (type_extends(&left, &right) && type_extends(&right, &left)) {
                ret = left;
                left = type_any;
            } else {
                ret = type_any;
            }
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
            BinaryOp without_assignment = binary_op_without_assignment(op);
            ret = binary_op_get_type_and_typecheck(type_clone(left), without_assignment, right, expr, errs);
            ret = binary_op_get_type_and_typecheck(left, BINARY_OP_EQUAL, ret, expr, errs);
            left = type_any;
            right = type_any;
        }   break;

        case BINARY_OP_COMMA:
            // TODO: throw TS2695: Left side of comma operator is unused and has no side effects.
            ret = right;
            right = type_any;
            break;

        case BINARY_OP_EH_EH:
            // TODO
            ret = left;
            left = type_any;
            break;
    }
    type_drop(&left);
    type_drop(&right);
    return ret;
}

Type expr_get_type_and_typecheck(Expr* me, ErrorCtx* errs) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
            return type_any;

        case EXPR_LITERAL:
            return type_value(value_clone(my literal));

        case EXPR_IDENTIFIER:
            if (my identifier.var.get) {
                return type_clone(*my identifier.var.get->type);
            } else {
                return type_any;
            }

        case EXPR_BINARY_OP:
            return binary_op_get_type_and_typecheck(
                expr_get_type_and_typecheck(&my binary_op->left, errs),
                my binary_op->op,
                expr_get_type_and_typecheck(&my binary_op->right, errs),
                me,
                errs
            );

        case EXPR_UNARY_OP: {
            UnaryOpExpr* una = my unary_op;
            Type type = expr_get_type_and_typecheck(&una->expr, errs);
            Type ret = {0};
            switch (una->op) {
                // TODO: add typechecking
                case UNARY_OP_BANG: ret = type_boolean; break;
                case UNARY_OP_TILDE: ret = type_number; break;
                case UNARY_OP_PLUS: ret = type_number; break;
                case UNARY_OP_MINUS: ret = type_number; break;
                case UNARY_OP_LEFT_PLUS_PLUS: ret = type_number; break;
                case UNARY_OP_LEFT_MINUS_MINUS: ret = type_number; break;
                case UNARY_OP_RIGHT_PLUS_PLUS: ret = type_number; break;
                case UNARY_OP_RIGHT_MINUS_MINUS: ret = type_number; break;
            }
            type_drop(&type);
            return ret;
        }

        case EXPR_PROPERTY: {
            Type value = expr_get_type_and_typecheck(&my property->expr, errs);
            Type prop = expr_get_type_and_typecheck(&my property->prop, errs);
            Type ret = type_any;
            if (value.tag == TYPE_ARRAY && type_extends(&prop, &type_number)) {
                ret = *value.array;
                *value.array = type_any;
            } else if (type_extends(&value, &type_string) && type_extends(&prop, &type_number)) {
                ret = type_string;
            } else {
                // TODO;
            }
            type_drop(&value);
            type_drop(&prop);
            return ret;
        }


        case EXPR_AS_TYPE: {
            // TODO: check that type of expr overlaps with (not extends or is extended by) type. if it doesn't surface
            // TS2352: Conversion of type 'number' to type 'string' may be a mistake because neither type sufficiently overlaps with the other. If this was intentional, convert the expression to 'unknown' first.
            return type_clone(my as_type->type);
        }

        case EXPR_CALL: {
            Type ret = type_any;
            Type callable = expr_get_type_and_typecheck(&my call->callable, errs);
            if (callable.tag == TYPE_GENERIC) {
                Type inferred = generic_callable_infer(callable.generic, &my call->args, errs);
                type_drop(&callable);
                callable = inferred;
            }
            long given_arg_count = vec_expr_size(&my call->args);
            long callable_arg_count = 0;
            if (callable.tag == TYPE_CALLABLE) {
                callable_arg_count = vec_callable_type_arg_size(&callable.callable->args);
                // TODO: handle optional and variadic arguments
                if (callable_arg_count != given_arg_count) {
                    error_ctx_type_error(errs, "wrong number of arguments");
                }
                ret = type_clone(callable.callable->ret);
            } else if (callable.tag != TYPE_ANY) {
                cstr type_str = cstr_null;
                cstr_append_type_error_msg(&type_str, &callable);
                // TODO: the two sentences of the error message are supposed to be on separate lines somehow
                error_ctx_error(
                    errs,
                    my call->callable.span,
                    2349,
                    "This expression is not callable. Type '%s' has no call signatures.",
                    cstr_str(&type_str)
                );
                cstr_drop(&type_str);
            }
            for (long i = 0; i < given_arg_count; i++) {
                Type arg_type = expr_get_type_and_typecheck(
                    vec_expr_at_mut(&my call->args, i),
                    errs
                );
                if (i < callable_arg_count) {
                    if (!type_extends(&arg_type, &vec_callable_type_arg_at(&callable.callable->args, i)->type)) {
                        error_ctx_type_error(errs, "bad argument type");
                    }
                }
                type_drop(&arg_type);
            }
            type_drop(&callable);
            return ret;
        }

        case EXPR_FUNCTION:
            function_impl_typecheck(my function, errs);
            return type_clone(my function->type);

        case EXPR_OBJECT: {
            Interface interface = {0};
            c_foreach(i, vec_object_expr_item, my object) {
                Type t = expr_get_type_and_typecheck(&i.ref->value, errs);
                vec_interface_member_push(
                    &interface.members,
                    interface_member_make(cstr_clone(i.ref->name), t)
                );
            }
            return type_interface(interface);
        }

        case EXPR_ARRAY: {
            Type array_type = type_value(value_undefined);
            c_foreach(i, vec_expr, my array) {
                Type t = expr_get_type_and_typecheck(i.ref, errs);
                if (array_type.tag == TYPE_VALUE && array_type.value.tag == VALUE_UNDEFINED) {
                    type_abstract_literals(&t);
                    type_drop(&array_type);
                    array_type = t;
                } else if (
                    (t.tag == TYPE_VALUE && t.value.tag == VALUE_UNDEFINED) ||
                    type_extends(&t, &array_type)
                ) {
                    type_drop(&t);
                } else {
                    type_drop(&t);
                    type_drop(&array_type);
                    array_type = type_any;
                }
            }
            return type_array(c_new(Type, array_type));
        }

        case EXPR_CLASS_DECL:
            class_decl_typecheck(my class_decl, errs);
            return type_clone(my class_decl->constructor_type);
    }
    UNREACHABLE;
}

signed char binary_op_cmp_precedence(BinaryOp a, BinaryOp b) {
    int ap = binary_op_get_precedence(a);
    int bp = binary_op_get_precedence(b);
    return ap == bp ? 0 : (ap > bp ? 1 : -1);
}

czview binary_op_get_zv(BinaryOp op) {
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

czview unary_op_get_zv(UnaryOp op) {
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

void object_expr_item_drop(ObjectExprItem* me) {
    cstr_drop(&my name);
    expr_drop(&my value);
}

ObjectExprItem* object_expr_get(ObjectExpr* me, const char* name) {
    c_foreach(i, vec_object_expr_item, *me) {
        if (strcmp(cstr_str(&i.ref->name), name) == 0) {
            return i.ref;
        }
    }
    return nullptr;
}
