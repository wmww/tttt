#include "expr.h"
#include "stc/types.h"
#include "type.h"
#include "error.h"
#include "scope_stack.h"
#include "scope.h"
#include "function_impl.h"
#include "generic.h"
#include "class_decl.h"
#include "interface_member.h"
#include "several_types.h"
#include "callable_type.h"

const Expr expr_implicit_any_no_span = { .tag = EXPR_IMPLICIT_ANY };
const int as_type_precedence = 90;
const int ternary_precedence = 20;
const int new_keyword_precedence = 170;

void identifier_expr_drop(IdentifierExpr* me) {
    ignore me;
}

void identifier_expr_lookup_names(IdentifierExpr* me, ScopeStack* names, ErrorCtx* errs) {
    if (my keyword) {
        my decl_ref = scope_stack_get_keyword(names, my keyword, false);
        return;
    }
    my decl_ref = scope_stack_get_value(names, my name.sv);
    if (my decl_ref) {
    } else if (scope_stack_get_type(names, my name.sv)) {
        error_ctx_error(
            errs, my name, 2693,
            "'%.*s' only refers to a type, but is being used as a value here.",
            c_SPAN(my name)
        );
    } else {
        error_ctx_error(
            errs,
            my name,
            2304,
            "Cannot find name '%.*s'.",
            c_SPAN(my name)
        );
    }
}

Type identifier_expr_evaluate_type(IdentifierExpr const* me) {
    if (my decl_ref) {
        Type ret;
        if (my keyword && my decl_ref->value_tag == DECLARATION_CLASS) {
            ret = type_identifier(c_new(IdentifierType, {
                .name = my decl_ref->name,
                .decl_ref = my decl_ref,
            }));
        } else {
            ret = type_deref_clone(my decl_ref->value_type);
            ret.parsed = false;
        }
        return ret;
    } else {
        switch (my keyword) {
            case KEYWORD_IDENTIFIER_THIS:
                return type_undefined;
            default: break;
        }
        return type_any;
    }
}

void identifier_expr_typecheck(IdentifierExpr const* me, ErrorCtx* errs) {
    ignore me;
    ignore errs;
}

static Generic const* generic_params_get_generic(GenericParamsExpr* me) {
    Type const* expr_type = type_resolve(expr_get_type(&my expr));
    if (expr_type->tag == TYPE_CALLABLE) {
        return &expr_type->callable->generic;
    } else if (expr_type->tag == TYPE_INTERFACE) {
        c_foreach(i, vec_interface_member, expr_type->interface.members) {
            if (i.ref->key.tag == INTERFACE_KEY_CALLABLE) {
                ASSERT(i.ref->type.tag == TYPE_CALLABLE);
                return &i.ref->type.callable->generic;
            }
        }
    }
    return nullptr;
}

Type generic_params_expr_evaluate_type(GenericParamsExpr* me) {
    Type const* expr_type = expr_get_type(&my expr);
    Generic const* generic = generic_params_get_generic(me);
    if (!generic_empty(generic)) {
        return generic_degenericize_type(generic, type_clone(*expr_type), &my params);
    } else {
        return type_clone(*expr_type);
    }
}

void generic_params_expr_typecheck(GenericParamsExpr* me, ErrorCtx* errs) {
    c_foreach(i, vec_spanned_type, my params) {
        type_typecheck(&i.ref->type, errs);
    }
    Generic const* generic = generic_params_get_generic(me);
    for (intptr_t i = 0; i < vec_spanned_type_size(&my params); i++) {
        if (generic && i < vec_generic_item_size(&generic->items)) {
            SpannedType const* arg = vec_spanned_type_at(&my params, i);
            GenericItem const* item = vec_generic_item_at(&generic->items, i);
            if (item->extends_explicit && !type_extends(&arg->type, &item->extends)) {
                Type abstracted = type_clone(arg->type);
                type_abstract_literals(&abstracted);
                cstr msg = cstr_lit("Type '");
                cstr_append_type_error_msg(&msg, &abstracted);
                cstr_append_lit(&msg, "' does not satisfy the constraint '");
                cstr_append_type_error_msg(&msg, &item->extends);
                cstr_append_lit(&msg, "'.");
                error_ctx_error_s(errs, vec_spanned_type_at(&my params, i)->span, 2344, msg);
                type_drop(&abstracted);
            }
        }
    }
    if (type_resolve(expr_get_type(&my expr))->tag == TYPE_ANY) {
    } else if (generic_empty(generic)) {
        error_ctx_error(
            errs,
            span_between(
                vec_spanned_type_at(&my params, 0)->span,
                (vec_spanned_type_end(&my params).end - 1)->span
            ),
            2558,
            "Expected 0 type arguments, but got %zd.",
            vec_spanned_type_size(&my params)
        );
    }
}

void expr_drop(Expr* me) {
    free(my currently_getting_type);
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
            break;

        case EXPR_LITERAL:
            value_drop(&my literal);
            break;

        case EXPR_IDENTIFIER:
            identifier_expr_drop(&my identifier);
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

        case EXPR_GENERIC_PARAMS:
            expr_drop(&my generic_params->expr);
            vec_spanned_type_drop(&my generic_params->params);
            free(my generic_params);
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
    lazy_type_drop(&my lazy_type);
}

void expr_lookup_names(Expr* me, ScopeStack* names, ErrorCtx* errs) {
    my lazy_type.tag = LAZY_TYPE_EXPR;

    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL: break;

        case EXPR_IDENTIFIER:
            identifier_expr_lookup_names(&my identifier, names, errs);
            break;

        case EXPR_BINARY_OP:
            expr_lookup_names(&my binary_op->left, names, errs);
            expr_lookup_names(&my binary_op->right, names, errs);
            break;

        case EXPR_UNARY_OP:
            expr_lookup_names(&my unary_op->expr, names, errs);
            break;

        case EXPR_PROPERTY:
            expr_lookup_names(&my property->expr, names, errs);
            expr_lookup_names(&my property->prop, names, errs);
            break;

        case EXPR_GENERIC_PARAMS:
            expr_lookup_names(&my generic_params->expr, names, errs);
            c_foreach(i, vec_spanned_type, my generic_params->params) {
                type_lookup_names(&i.ref->type, names, errs);
            }
            break;

        case EXPR_AS_TYPE:
            expr_lookup_names(&my as_type->expr, names, errs);
            type_lookup_names(&my as_type->type, names, errs);
            break;

        case EXPR_TERNARY:
            expr_lookup_names(&my ternary->condition, names, errs);
            expr_lookup_names(&my ternary->yes, names, errs);
            expr_lookup_names(&my ternary->no, names, errs);
            break;

        case EXPR_CALL:
            expr_lookup_names(&my call->callable, names, errs);
            c_foreach(i, vec_expr, my call->args) {
                expr_lookup_names(i.ref, names, errs);
            }
            break;

        case EXPR_FUNCTION: {
            Scope tmp_scope = scope_init(errs);
            function_impl_add_names(my function, &tmp_scope, false);
            scope_drop(&tmp_scope);
            function_impl_lookup_names(my function, !span_valid(my function->arrow_span), names, errs);
        }   break;

        case EXPR_OBJECT:
            c_foreach(i, vec_object_expr_item, my object) {
                expr_lookup_names(&i.ref->value, names, errs);
            }
            break;

        case EXPR_ARRAY:
            c_foreach(i, vec_expr, my array) {
                expr_lookup_names(i.ref, names, errs);
            }
            break;

        case EXPR_CLASS_DECL: {
            Scope class_name_scope = scope_init(errs);
            class_decl_add_names(my class_decl, &class_name_scope, false);
            scope_push_to_stack(&class_name_scope, names);
            scope_drop(&class_name_scope);
            class_decl_lookup_names(my class_decl, names, errs);
        }   break;
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

static Type binary_op_evaluate_type(
    Type const* left,
    BinaryOp op,
    Type const* right,
    Expr const* expr
) {
    switch (op) {
        case BINARY_OP_PLUS: {
            const bool left_extends_string = type_extends(left, &type_string);
            const bool right_extends_string = type_extends(right, &type_string);
            if (left_extends_string || right_extends_string) {
                if ((left_extends_string && type_resolve(left)->tag != TYPE_ANY) ||
                    (right_extends_string && type_resolve(right)->tag != TYPE_ANY)
                ) {
                    return type_string;
                } else {
                    return type_any;
                }
            } else if (type_extends(left, &type_number) && type_extends(right, &type_number)) {
                return type_number;
            } else {
                return type_any;
            }
        }

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
        case BINARY_OP_OR: return type_number;

        case BINARY_OP_EQUAL: return type_clone(*right);

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL:
        case BINARY_OP_LESS:
        case BINARY_OP_GREATER:
        case BINARY_OP_LESS_EQUAL:
        case BINARY_OP_GREATER_EQUAL:
        case BINARY_OP_IN:
        case BINARY_OP_INSTANCEOF: return type_boolean;

        case BINARY_OP_AND_AND:
        case BINARY_OP_OR_OR: return type_composite(TYPE_UNION, type_clone(*left), type_clone(*right));

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
            Type a = binary_op_evaluate_type(left, without_assignment, right, expr);
            Type b = binary_op_evaluate_type(left, BINARY_OP_EQUAL, &a, expr);
            type_drop(&a);
            return b;
        }

        case BINARY_OP_COMMA:
            // TODO: throw TS2695: Left side of comma operator is unused and has no side effects.
            return type_clone(*right);

        case BINARY_OP_EH_EH: {
            Type a = type_clone(*left);
            type_remove_null_and_undefined(&a);
            return type_composite(TYPE_UNION, a, type_clone(*right));
        }
    }
    UNREACHABLE;
}

static void binary_op_typecheck(
    Type const* left,
    BinaryOp op,
    Type const* right,
    Expr const* expr,
    ErrorCtx* errs
) {
    switch (op) {
        case BINARY_OP_PLUS:
            // PERF: we also do this checks when evaluating the type, shouldn't need to duplicate them
            if (type_extends(left, &type_string) || type_extends(right, &type_string)) {
            } else if (type_extends(left, &type_number) && type_extends(right, &type_number)) {
            } else {
                // TODO: it seems special handling of possibly null or undefined types is needed
                cstr message = cstr_lit("Operator '+' cannot be applied to types '");
                cstr_append_type_error_msg(&message, left);
                cstr_append_lit(&message, "' and '");
                cstr_append_type_error_msg(&message, right);
                cstr_append_lit(&message, "'.");
                error_ctx_error_s(errs, expr->inner_span, 2365, message);
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
            if (!type_extends(left, &type_number)) {
                error_ctx_error(
                    errs,
                    expr->binary_op->left.outer_span,
                    2362,
                    "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type."
                );
            }
            if (!type_extends(right, &type_number)) {
                error_ctx_error(
                    errs,
                    expr->binary_op->right.outer_span,
                    2363,
                    "The right-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type."
                );
            }
            break;

        case BINARY_OP_EQUAL:
            switch (expr->binary_op->left.tag) {
                case EXPR_IDENTIFIER:
                    if (expr->binary_op->left.identifier.decl_ref) {
                        type_error_if_not_assignable(
                            right,
                            &expr->binary_op->left.identifier.decl_ref->value_type,
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
            break;

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL:
            if (!type_overlaps(left, right)) {
                cstr message = cstr_lit("This comparison appears to be unintentional because the types '");
                cstr_append_type_error_msg(&message, left);
                cstr_append_lit(&message, "' and '");
                cstr_append_type_error_msg(&message, right);
                cstr_append_lit(&message, "' have no overlap.");
                error_ctx_error_s(errs, expr->outer_span, 2367, message);
            }
            break;

        case BINARY_OP_LESS:
        case BINARY_OP_GREATER:
        case BINARY_OP_LESS_EQUAL:
        case BINARY_OP_GREATER_EQUAL:
        case BINARY_OP_IN:
        case BINARY_OP_INSTANCEOF:
            // TODO: figure out rules and typecheck
            break;

        case BINARY_OP_AND_AND:
        case BINARY_OP_OR_OR:
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
            binary_op_typecheck(left, without_assignment, right, expr, errs);
            Type t = binary_op_evaluate_type(left, without_assignment, right, expr);
            binary_op_typecheck(left, BINARY_OP_EQUAL, &t, expr, errs);
            type_drop(&t);
        }

        case BINARY_OP_COMMA:
        case BINARY_OP_EH_EH:
            break;
    }
}

static Type unary_op_evaluate_type(
    UnaryOp op,
    Expr* expr
) {
    switch (op) {
        case UNARY_OP_LEFT_BANG:
            switch (type_is_trivially_truthy(expr_get_type(expr))) {
                case 1: return type_value(value_boolean(false));
                case 0: return type_value(value_boolean(true));
                default: return type_boolean;
            }
            break;

        case UNARY_OP_TYPEOF: return type_string;
        case UNARY_OP_VOID: return type_undefined;
        case UNARY_OP_TILDE: return type_number;

        case UNARY_OP_PLUS:
        case UNARY_OP_MINUS:
            if (!expr_is_perenthisized(expr) && expr->tag == EXPR_LITERAL && expr->literal.tag == VALUE_NUMBER) {
                return type_value(value_number(op == UNARY_OP_PLUS ? expr->literal.number : -expr->literal.number));
            } else {
                return type_number;
            }

        case UNARY_OP_LEFT_PLUS_PLUS:
        case UNARY_OP_LEFT_MINUS_MINUS:
        case UNARY_OP_RIGHT_PLUS_PLUS:
        case UNARY_OP_RIGHT_MINUS_MINUS: return type_number;

        case UNARY_OP_RIGHT_BANG: {
            Type ret = type_clone(*expr_get_type(expr));
            type_remove_null_and_undefined(&ret);
            return ret;
        }
    }
    UNREACHABLE;
}

Type expr_evaluate_type(Expr const* me) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY: return type_any;
        case EXPR_LITERAL: return type_value(value_clone(my literal));
        case EXPR_IDENTIFIER: return identifier_expr_evaluate_type(&my identifier);

        case EXPR_BINARY_OP:
            return binary_op_evaluate_type(
                expr_get_type(&my binary_op->left),
                my binary_op->op,
                expr_get_type(&my binary_op->right),
                me
            );

        case EXPR_UNARY_OP:
            return unary_op_evaluate_type(my unary_op->op, &my unary_op->expr);

        case EXPR_PROPERTY:
            return index_type_evaluate_type(
                expr_get_type(&my property->expr),
                expr_get_type(&my property->prop)
            );

        case EXPR_GENERIC_PARAMS: return generic_params_expr_evaluate_type(my generic_params);

        case EXPR_AS_TYPE: return type_refcopy(&my as_type->type);

        case EXPR_TERNARY:
            return type_composite(
                TYPE_UNION,
                type_refcopy(expr_get_type(&my ternary->yes)),
                type_refcopy(expr_get_type(&my ternary->no))
            );

        case EXPR_CALL:
            return type_call_evaluate_type(expr_get_type(&my call->callable), my call);

        case EXPR_FUNCTION:
            return type_lazy(&my function->lazy_type);

        case EXPR_OBJECT: {
            Interface interface = {0};
            c_foreach(i, vec_object_expr_item, my object) {
                if (i.ref->is_spread) {
                    Type const* type = type_resolve(expr_get_type(&i.ref->value));
                    if (type->tag == TYPE_INTERFACE) {
                        c_foreach(i, vec_interface_member, type->interface.members) {
                            vec_interface_member_push(&interface.members, interface_member_clone(*i.ref));
                        }
                    }
                } else {
                    Type type = type_clone(*expr_get_type(&i.ref->value));
                    type.parsed = false;
                    type_abstract_literals(&type);
                    vec_interface_member_push(
                        &interface.members,
                        interface_member_make(
                            span_between(i.ref->name_span, i.ref->value.outer_span),
                            nullptr,
                            false,
                            false,
                            cstr_clone(i.ref->name_string),
                            i.ref->name_span,
                            false,
                            type
                        )
                    );
                }
            }
            return type_interface(interface);
        }

        case EXPR_ARRAY: {
            Type index_t = type_never;
            c_foreach(i, vec_expr, my array) {
                Type type = type_clone(*expr_get_type(i.ref));
                type_abstract_literals(&type);
                if (type.tag != TYPE_VALUE || type.value.tag != VALUE_UNDEFINED) {
                    index_t = type_composite(TYPE_UNION, index_t, type);
                }
            }
            if (index_t.tag == TYPE_NEVER) {
                // TODO: this should only happen if strict_null_checks is false
                // Uh oh, are we going to need an ErrorConfig here? That might be a problem
                index_t = type_any;
            }
            return type_array(c_new(Type, index_t), false);
        }

        case EXPR_CLASS_DECL:
            return type_refcopy(&my class_decl->declaration.get->value_type);
    }
    UNREACHABLE;
}

void expr_typecheck(Expr const* me, ErrorCtx* errs) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY: break;
        case EXPR_LITERAL: break;

        case EXPR_IDENTIFIER: identifier_expr_typecheck(&my identifier, errs); break;

        case EXPR_BINARY_OP:
            expr_typecheck(&my binary_op->left, errs);
            expr_typecheck(&my binary_op->right, errs);
            binary_op_typecheck(
                expr_get_type(&my binary_op->left),
                my binary_op->op,
                expr_get_type(&my binary_op->right),
                me,
                errs
            );
            break;

        case EXPR_UNARY_OP:
            expr_typecheck(&my unary_op->expr, errs);
            switch (my unary_op->op) {
                case UNARY_OP_LEFT_BANG:
                case UNARY_OP_TYPEOF:
                case UNARY_OP_VOID:
                case UNARY_OP_TILDE:
                case UNARY_OP_PLUS:
                case UNARY_OP_MINUS:
                case UNARY_OP_LEFT_PLUS_PLUS:
                case UNARY_OP_LEFT_MINUS_MINUS:
                case UNARY_OP_RIGHT_PLUS_PLUS:
                case UNARY_OP_RIGHT_MINUS_MINUS:
                    // TODO: add typechecking
                    break;

                case UNARY_OP_RIGHT_BANG:
                    break;
            }
            break;

        case EXPR_PROPERTY: {
            expr_typecheck(&my property->expr, errs);
            expr_typecheck(&my property->prop, errs);
            Type to_drop = {0};
            Type const* expr_type = expr_get_type(&my property->expr);
            if (my property->optional_chain) {
                to_drop = type_clone(*expr_type);
                type_remove_null_and_undefined(&to_drop);
                expr_type = &to_drop;
            }
            bool error_if_not_found = my property->form == PROPERTY_EXPR_DOT;
            index_type_typecheck(
                expr_type,
                expr_get_type(&my property->prop),
                my property->expr.outer_span,
                my property->prop.outer_span,
                error_if_not_found,
                errs
            );
            type_drop(&to_drop);
        }   break;

        case EXPR_GENERIC_PARAMS: generic_params_expr_typecheck(my generic_params, errs); break;

        case EXPR_AS_TYPE:
            // TODO: check that type of expr overlaps with (not extends or is extended by) type. if it doesn't surface
            // TS2352: Conversion of type 'number' to type 'string' may be a mistake because neither type sufficiently overlaps with the other. If this was intentional, convert the expression to 'unknown' first.
            expr_typecheck(&my as_type->expr, errs);
            type_typecheck(&my as_type->type, errs);
            break;

        case EXPR_TERNARY:
            expr_typecheck(&my ternary->condition, errs);
            expr_typecheck(&my ternary->yes, errs);
            expr_typecheck(&my ternary->no, errs);
            break;

        case EXPR_CALL: {
            expr_typecheck(&my call->callable, errs);
            c_foreach(i, vec_expr, my call->args) {
                expr_typecheck(i.ref, errs);
            }
            type_typecheck_call(expr_get_type(&my call->callable), my call, my outer_span, errs);
        }   break;

        case EXPR_FUNCTION:
            function_impl_typecheck(my function, errs);
            break;

        case EXPR_OBJECT:
            c_foreach(i, vec_object_expr_item, my object) {
                expr_typecheck(&i.ref->value, errs);
            }
            break;

        case EXPR_ARRAY:
            c_foreach(i, vec_expr, my array) {
                expr_typecheck(i.ref, errs);
            }
            break;

        case EXPR_CLASS_DECL:
            class_decl_typecheck(my class_decl, errs);
            break;
    }
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
        case UNARY_OP_VOID:
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
        case UNARY_OP_VOID: return c_zv("void ");
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
