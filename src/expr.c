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
#include "identifier_type.h"

const Expr expr_implicit_any_no_span = { .tag = EXPR_IMPLICIT_ANY };
const int as_type_precedence = 90;
const int ternary_precedence = 20;
const int new_keyword_precedence = 170;

void expr_drop(Expr* me) {
    free(my currently_getting_type);
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
            break;

        case EXPR_LITERAL:
            value_drop(&my literal);
            break;

        case EXPR_IDENTIFIER:
            vec_spanned_type_drop(&my identifier.generic_params);
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

        case EXPR_TERNARY:
            expr_drop(&my ternary->condition);
            expr_drop(&my ternary->yes);
            expr_drop(&my ternary->no);
            free(my ternary);
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
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL:
        case EXPR_AS_TYPE:
        case EXPR_IDENTIFIER:
        case EXPR_OBJECT:
        case EXPR_ARRAY:
            break;

        default: {
            Type t = expr_get_type_and_typecheck(me, nullptr);
            cstr_append_sv(target, c_sv(CLR_IMPLICIT "<" CLR0));
            cstr_append_type_debug(target, &t, indent);
            cstr_append_sv(target, c_sv(CLR_IMPLICIT ">" CLR0));
            type_drop(&t);
        }
    }

    intptr_t paren_depth = 0;
    for (const char* c = span_start_ptr(&my outer_span); c < span_start_ptr(&my inner_span); c++) {
        if (*c == '(') paren_depth++;
    }

    for (int i = 0; i < paren_depth; i++) {
        cstr_append_sv(target, c_sv("("));
    }

    switch (my tag) {
        case EXPR_IMPLICIT_ANY: cstr_append_sv(target, c_sv("/* implicit any */")); break;

        case EXPR_LITERAL: cstr_append_value_debug(target, &my literal, indent); break;

        case EXPR_IDENTIFIER:
            cstr_append_sv(target, c_sv(CLR_NAME));
            cstr_append_span(target, &my identifier.name);
            cstr_append_sv(target, c_sv(CLR0));
            if (!vec_spanned_type_empty(&my identifier.generic_params)) {
                cstr_append_sv(target, c_sv("<"));
                c_foreach(i, vec_spanned_type, my identifier.generic_params) {
                    cstr_append_type_debug(target, &i.ref->type, indent);
                    if (i.ref != i.end - 1) {
                        cstr_append_sv(target, c_sv(", "));
                    }
                }
                cstr_append_sv(target, c_sv(">"));
            }
            if (!my identifier.var) {
                cstr_append_sv(target, c_sv(CLR_ERR " /* unresolved */" CLR0));
            } else if (!lazy_type_try_get(&my identifier.var->type)) {
                cstr_append_sv(target, c_sv(CLR_ERR " /* untypechecked */" CLR0));
            }
            break;

        case EXPR_BINARY_OP: {
            if (!paren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            }
            cstr_append_expr_debug(target, &my binary_op->left, indent);
            cstr_append_sv(target, c_sv(" "));
            cstr_append_sv(target, czview_sv(binary_op_get_zv(my binary_op->op)));
            cstr_append_sv(target, c_sv(" "));
            cstr_append_expr_debug(target, &my binary_op->right, indent);
            if (!paren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
        }   break;

        case EXPR_UNARY_OP: {
            if (!paren_depth) {
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
            if (!paren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
        }   break;

        case EXPR_PROPERTY: {
            if (!paren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            }

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

            if (!paren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
        }   break;

        case EXPR_AS_TYPE: {
            if (!paren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            }
            cstr_append_expr_debug(target, &my as_type->expr, indent);
            cstr_append_sv(target, c_sv(" as "));
            cstr_append_type_debug(target, &my as_type->type, indent);
            if (!paren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
        }   break;

        case EXPR_TERNARY: {
            if (!paren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT "(" CLR0));
            }
            cstr_append_expr_debug(target, &my ternary->condition, indent);
            cstr_append_sv(target, c_sv(" ? "));
            cstr_append_expr_debug(target, &my ternary->yes, indent);
            cstr_append_sv(target, c_sv(" : "));
            cstr_append_expr_debug(target, &my ternary->no, indent);
            if (!paren_depth) {
                cstr_append_sv(target, c_sv(CLR_IMPLICIT ")" CLR0));
            }
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
                cstr_append_s(target, i.ref->name_string);
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
    for (int i = 0; i < paren_depth; i++) {
        cstr_append_sv(target, c_sv(")"));
    }
}

void expr_resolve_names(Expr* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL:
            break;

        case EXPR_IDENTIFIER: {
            my identifier.var = name_stack_get_variable(names, span_sv(&my identifier.name));
            if (my identifier.var) {
            } else if (name_stack_get_type(names, span_sv(&my identifier.name))) {
                error_ctx_error(
                    errs, my identifier.name, 2693,
                    "'%.*s' only refers to a type, but is being used as a value here.",
                    c_SV(span_sv(&my identifier.name))
                );
            } else {
                error_ctx_error(
                    errs,
                    my identifier.name,
                    2304,
                    "Cannot find name '%.*s'.",
                    c_SV(span_sv(&my identifier.name))
                );
            }
            c_foreach(i, vec_spanned_type, my identifier.generic_params) {
                type_resolve_names(&i.ref->type, names, errs);
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

        case EXPR_TERNARY:
            expr_resolve_names(&my ternary->condition, names, errs);
            expr_resolve_names(&my ternary->yes, names, errs);
            expr_resolve_names(&my ternary->no, names, errs);
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
            name_stack_push_block_frame(names);
            class_decl_add_names(my class_decl, names, errs);
            name_stack_pop_frame(names);
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
    Expr const* expr,
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
                cstr message = cstr_lit("Operator '+' cannot be applied to types '");
                cstr_append_type_error_msg(&message, &left);
                cstr_append_sv(&message, c_sv("' and '"));
                cstr_append_type_error_msg(&message, &right);
                cstr_append_sv(&message, c_sv("'."));
                error_ctx_error_s(errs, expr->inner_span, 2365, message);
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
                    expr->binary_op->left.outer_span,
                    2362,
                    "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type."
                );
            }
            if (!type_extends(&right, &type_number)) {
                error_ctx_error(
                    errs,
                    expr->binary_op->right.outer_span,
                    2363,
                    "The right-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type."
                );
            }
            ret = type_number;
            break;

        case BINARY_OP_EQUAL:
            switch (expr->binary_op->left.tag) {
                case EXPR_IDENTIFIER:
                    if (expr->binary_op->left.identifier.var) {
                        check_assignable_or_error(
                            &right,
                            lazy_type_resolve(&expr->binary_op->left.identifier.var->type),
                            expr->binary_op->left.outer_span,
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
                        expr->binary_op->left.outer_span,
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
            if (!type_overlaps(&left, &right)) {
                if (errs) {
                    cstr message = cstr_lit("This comparison appears to be unintentional because the types '");
                    cstr_append_type_error_msg(&message, &left);
                    cstr_append_sv(&message, c_sv("' and '"));
                    cstr_append_type_error_msg(&message, &right);
                    cstr_append_sv(&message, c_sv("' have no overlap."));
                    error_ctx_error_s(errs, expr->outer_span, 2367, message);
                }
            }
            ret = type_boolean;
            break;

        case BINARY_OP_LESS:
        case BINARY_OP_GREATER:
        case BINARY_OP_LESS_EQUAL:
        case BINARY_OP_GREATER_EQUAL:
        case BINARY_OP_IN:
        case BINARY_OP_INSTANCEOF:
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

Type expr_get_type_and_typecheck(Expr const* me, ErrorCtx* errs) {
    Type ret;
    if (my currently_getting_type) {
        if (*my currently_getting_type) {
            return type_self_referential_error;
        }
        *my currently_getting_type = true;
    }

    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
            ret = type_any;
            break;

        case EXPR_LITERAL:
            ret = type_value(value_clone(my literal));
            break;

        case EXPR_IDENTIFIER:
            if (my identifier.var) {
                ret = type_ref(lazy_type_resolve(&my identifier.var->type));
                if (!vec_spanned_type_empty(&my identifier.generic_params)) {
                    c_foreach(i, vec_spanned_type, my identifier.generic_params) {
                        type_typecheck(&i.ref->type, errs);
                    }
                    if (generic_empty(my identifier.var->generic)) {
                        error_ctx_error(
                            errs,
                            span_between(
                                &vec_spanned_type_at(&my identifier.generic_params, 0)->span,
                                &(vec_spanned_type_end(&my identifier.generic_params).end - 1)->span
                            ),
                            2558,
                            "Expected 0 type arguments, but got %zd.",
                            vec_spanned_type_size(&my identifier.generic_params)
                        );
                    } else {
                        ret = generic_degenericize_type(
                            my identifier.var->generic,
                            ret,
                            &my identifier.generic_params
                        );
                    }
                }
            } else {
                ret = type_any;
            }
            break;

        case EXPR_BINARY_OP:
            ret = binary_op_get_type_and_typecheck(
                expr_get_type_and_typecheck(&my binary_op->left, errs),
                my binary_op->op,
                expr_get_type_and_typecheck(&my binary_op->right, errs),
                me,
                errs
            );
            break;

        case EXPR_UNARY_OP: {
            UnaryOpExpr* una = my unary_op;
            Type type = expr_get_type_and_typecheck(&una->expr, errs);
            ret = type_any;
            switch (una->op) {
                // TODO: add typechecking
                case UNARY_OP_LEFT_BANG: ret = type_boolean; break;
                case UNARY_OP_TYPEOF: ret = type_string; break;
                case UNARY_OP_TILDE: ret = type_number; break;
                case UNARY_OP_PLUS: ret = type_number; break;
                case UNARY_OP_MINUS: ret = type_number; break;
                case UNARY_OP_LEFT_PLUS_PLUS: ret = type_number; break;
                case UNARY_OP_LEFT_MINUS_MINUS: ret = type_number; break;
                case UNARY_OP_RIGHT_PLUS_PLUS: ret = type_number; break;
                case UNARY_OP_RIGHT_MINUS_MINUS: ret = type_number; break;
                case UNARY_OP_RIGHT_BANG:
                    type_remove_null_and_undefined(&type);
                    ret = type;
                    type = type_any;
                    break;
            }
            type_drop(&type);
        }   break;

        case EXPR_PROPERTY: {
            Type value = expr_get_type_and_typecheck(&my property->expr, errs);
            Type prop = expr_get_type_and_typecheck(&my property->prop, errs);
            ret = index_type_of(
                &value,
                &prop,
                my property->prop.outer_span,
                my property->form == PROPERTY_EXPR_BRACKET,
                errs
            );
            type_drop(&value);
            type_drop(&prop);
        }   break;

        case EXPR_AS_TYPE: {
            // TODO: check that type of expr overlaps with (not extends or is extended by) type. if it doesn't surface
            // TS2352: Conversion of type 'number' to type 'string' may be a mistake because neither type sufficiently overlaps with the other. If this was intentional, convert the expression to 'unknown' first.
            type_typecheck(&my as_type->type, errs);
            Type t = expr_get_type_and_typecheck(&my as_type->expr, errs);
            type_drop(&t);
            ret = type_clone(my as_type->type);
        }   break;

        case EXPR_TERNARY: {
            Type condition_type = expr_get_type_and_typecheck(&my ternary->condition, errs);
            type_drop(&condition_type);
            ret = expr_get_type_and_typecheck(&my ternary->yes, errs);
            ret = type_composite(TYPE_UNION, ret, expr_get_type_and_typecheck(&my ternary->no, errs));
        }   break;

        case EXPR_CALL: {
            Type callable = expr_get_type_and_typecheck(&my call->callable, errs);
            c_foreach(i, vec_expr, my call->args) {
                Type t = expr_get_type_and_typecheck(i.ref, errs);
                type_drop(&t);
            }
            ret = type_typecheck_call(&callable, my call, my outer_span, errs, 0);
            type_drop(&callable);
        }   break;

        case EXPR_FUNCTION:
            function_impl_typecheck(my function, errs);
            ret = type_clone(*function_impl_resolve_type(my function));
            break;

        case EXPR_OBJECT: {
            Interface interface = {0};
            c_foreach(i, vec_object_expr_item, my object) {
                Type t = expr_get_type_and_typecheck(&i.ref->value, errs);
                type_abstract_tuple_to_array(&t);
                type_abstract_literals(&t);
                bool underline_in_tests = i.ref->value.tag != EXPR_AS_TYPE;
                vec_interface_member_push(
                    &interface.members,
                    interface_member_make(
                        span_between(&i.ref->name_span, &i.ref->value.outer_span),
                        span_null,
                        underline_in_tests,
                        false,
                        false,
                        cstr_clone(i.ref->name_string),
                        i.ref->name_span,
                        false,
                        lazy_type_value(t)
                    )
                );
            }
            ret = type_interface(interface);
        }   break;

        case EXPR_ARRAY: {
            ret = type_tuple(vec_type_init(), false);
            c_foreach(i, vec_expr, my array) {
                Type t = expr_get_type_and_typecheck(i.ref, errs);
                vec_type_push(&ret.tuple.elements, t);
            }
        }   break;

        case EXPR_CLASS_DECL:
            class_decl_typecheck(my class_decl, errs);
            ret = type_clone(my class_decl->class_identifier_type);
            break;
    }

    if (my currently_getting_type) {
        if (!*my currently_getting_type) {
            type_drop(&ret);
            return type_self_referential_error;
        }
        *my currently_getting_type = false;
    }

    return ret;
}

// See https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Operator_precedence
int binary_op_get_precedence(BinaryOp op) {
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
        case BINARY_OP_IN:
        case BINARY_OP_INSTANCEOF:
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
        case BINARY_OP_IN: return c_zv("in");
        case BINARY_OP_INSTANCEOF: return c_zv("instanceof");
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
    NONFATAL("Invalid binary operator %d", op);
    return c_zv("<INVALID_OP>");
}

// See https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Operator_precedence
int unary_op_get_precedence(UnaryOp op) {
    switch (op) {
        case UNARY_OP_LEFT_BANG:
        case UNARY_OP_TYPEOF:
        case UNARY_OP_TILDE:
        case UNARY_OP_PLUS:
        case UNARY_OP_MINUS:
        case UNARY_OP_LEFT_PLUS_PLUS:
        case UNARY_OP_LEFT_MINUS_MINUS:
            return 140;

        case UNARY_OP_RIGHT_PLUS_PLUS:
        case UNARY_OP_RIGHT_MINUS_MINUS:
        case UNARY_OP_RIGHT_BANG:
            return 150;
    }
    UNREACHABLE;
}

czview unary_op_get_zv(UnaryOp op) {
    switch (op) {
        case UNARY_OP_LEFT_BANG: return c_zv("!");
        case UNARY_OP_TYPEOF: return c_zv("typeof ");
        case UNARY_OP_TILDE: return c_zv("~");
        case UNARY_OP_PLUS: return c_zv("+");
        case UNARY_OP_MINUS: return c_zv("-");
        case UNARY_OP_LEFT_PLUS_PLUS: return c_zv("++");
        case UNARY_OP_LEFT_MINUS_MINUS: return c_zv("--");
        case UNARY_OP_RIGHT_PLUS_PLUS: return c_zv("++");
        case UNARY_OP_RIGHT_MINUS_MINUS: return c_zv("--");
        case UNARY_OP_RIGHT_BANG: return c_zv("!");
    }
    NONFATAL("Invalid unary operator %d", op);
    return c_zv("<INVALID_OP>");
}

void object_expr_item_drop(ObjectExprItem* me) {
    cstr_drop(&my name_string);
    expr_drop(&my value);
}

ObjectExprItem* object_expr_get(ObjectExpr* me, const char* name) {
    c_foreach(i, vec_object_expr_item, *me) {
        if (strcmp(cstr_str(&i.ref->name_string), name) == 0) {
            return i.ref;
        }
    }
    return nullptr;
}
