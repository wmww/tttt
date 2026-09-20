#include "expr.h"
#include "stc/types.h"
#include "type.h"
#include "error.h"
#include "scope.h"
#include "function_impl.h"
#include "generic.h"
#include "class_decl.h"
#include "interface_member.h"
#include "several_types.h"
#include "callable_type.h"
#include "ty/ty.h"

const Expr expr_implicit_any_no_span = { .tag = EXPR_IMPLICIT_ANY };
const int as_type_precedence = 90;
const int ternary_precedence = 20;
const int new_keyword_precedence = 170;

void identifier_expr_drop(IdentifierExpr* me) {
    ignore me;
}

void identifier_expr_lookup_names(IdentifierExpr* me, Scope* names, ErrorCtx* errs) {
    if (my keyword) {
        if (my keyword == KEYWORD_IDENTIFIER_THIS) {
            ThisRef this_ref = scope_get_this(names, false);
            my this_kind = this_ref.kind;
            my target = this_ref.symbol;
        }
        return;
    }
    my target = scope_get_value(names, my name.sv, &my symbol);
    if (my target) {
    } else if (scope_get_type(names, my name.sv, nullptr)) {
        error_ctx_error_d(
            errs, my name, 2693,
            "'", my name, "' only refers to a type, but is being used as a value here."
        );
    } else {
        error_ctx_error_d(errs, my name, 2304, "Cannot find name '", my name, "'.");
    }
}

Ty const* identifier_expr_evaluate_type(IdentifierExpr const* me) {
    return ty_identifier_reference_type(current_checker, me);
}

void identifier_expr_typecheck(IdentifierExpr const* me, ErrorCtx* errs) {
    ignore me;
    ignore errs;
}

// `f<A, B>` outside of a call (there the type arguments are the call's business, ty/call.c)
void generic_params_expr_typecheck(GenericParamsExpr* me, ErrorCtx* errs) {
    for_each (i, vec_spanned_type, &my params) {
        type_typecheck(&i->type, errs);
    }
    Checker* checker = current_checker;
    Ty const* callee = ty_expr_type(checker, &my expr);
    if (callee->flags & TYF_ANY) return;
    // TODO: every signature that takes this many type arguments, like ty_apply_type_arguments()
    Members const* members = ty_members(checker, callee);
    Signature const* sig = members->call_count ? members->call[0] : members->construct_count ? members->construct[0] : nullptr;
    if (sig) signature_resolve(checker, sig);
    if (!sig || !sig->type_params.len) {
        error_ctx_error_d(
            errs,
            span_between(
                vec_spanned_type_at(&my params, 0)->span,
                vec_spanned_type_back(&my params)->span
            ),
            2558,
            "Expected 0 type arguments, but got ", vec_spanned_type_size(&my params), "."
        );
        return;
    }
    for (u32 i = 0; i < sig->type_params.len && (isize_t)i < vec_spanned_type_size(&my params); i++) {
        Ty const* constraint = ty_param_constraint(checker, sig->type_params.data[i]);
        if (!constraint || ty_type_argument_satisfies_constraint(checker, sig->type_params, &my params, i)) continue;
        SpannedType const* arg = vec_spanned_type_at(&my params, i);
        error_ctx_error_d(errs, arg->span, 2344,
            "Type '", ty_widened_literal(checker, ty_of_node(&arg->type)), "' does not satisfy the constraint '", constraint, "'.");
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

        case EXPR_SPREAD:
            expr_drop(my spread);
            free(my spread);
            break;

        case EXPR_REGEX:
            break;

        case EXPR_TEMPLATE:
            if (my template_->tag) {
                expr_drop(my template_->tag);
                free(my template_->tag);
            }
            vec_expr_drop(&my template_->substitutions);
            cstr_drop(&my template_->plain_string);
            free(my template_);
            break;
    }
    lazy_type_drop(&my lazy_type);
}

// `x as const` / `<const>x`: `const` isn't a type name (the new core checks the same, see is_const_assertion())
static bool as_type_is_const_assertion(AsTypeExpr const* me) {
    return my type.tag == TYPE_IDENTIFIER && span_equals(my type.identifier->name, "const") &&
        vec_span_is_empty(&my type.identifier->namespaces) && !vec_spanned_type_size(&my type.identifier->generic_params);
}

void expr_lookup_names(Expr* me, Scope* names, ErrorCtx* errs) {
    lazy_type_bind(&my lazy_type, LAZY_TYPE_EXPR);

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
            for_each (i, vec_spanned_type, &my generic_params->params) {
                type_lookup_names(&i->type, names, errs);
            }
            break;

        case EXPR_AS_TYPE:
            expr_lookup_names(&my as_type->expr, names, errs);
            if (!as_type_is_const_assertion(my as_type)) {
                type_lookup_names(&my as_type->type, names, errs);
            }
            break;

        case EXPR_TERNARY:
            expr_lookup_names(&my ternary->condition, names, errs);
            expr_lookup_names(&my ternary->yes, names, errs);
            expr_lookup_names(&my ternary->no, names, errs);
            break;

        case EXPR_CALL:
            expr_lookup_names(&my call->callable, names, errs);
            for_each (i, vec_expr, &my call->args) {
                expr_lookup_names(i, names, errs);
            }
            break;

        case EXPR_FUNCTION: {
            // A function expression's name is only visible inside it
            Scope* name_scope = names;
            if (span_valid(my function->name)) {
                name_scope = scope_new(names, SCOPE_BLOCK);
                function_impl_add_names(my function, name_scope, false);
            }
            function_impl_lookup_names(my function, !span_valid(my function->arrow_span), name_scope, errs);
        }   break;

        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &my object) {
                if (i->computed_key) {
                    expr_lookup_names(i->computed_key, names, errs);
                }
                expr_lookup_names(&i->value, names, errs);
            }
            break;

        case EXPR_ARRAY:
            for_each (i, vec_expr, &my array) {
                expr_lookup_names(i, names, errs);
            }
            break;

        case EXPR_CLASS_DECL: {
            Scope* class_name_scope = scope_new(names, SCOPE_BLOCK);
            class_decl_add_names(my class_decl, class_name_scope, false);
            class_decl_lookup_names(my class_decl, class_name_scope, errs);
        }   break;

        case EXPR_SPREAD:
            expr_lookup_names(my spread, names, errs);
            break;

        case EXPR_REGEX:
            break;

        case EXPR_TEMPLATE:
            if (my template_->tag) {
                expr_lookup_names(my template_->tag, names, errs);
            }
            for_each (i, vec_expr, &my template_->substitutions) {
                expr_lookup_names(i, names, errs);
            }
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
        default: panic(
            "binary_op_without_assignment() called with non-assignment op ",
            binary_op_get_zv(op).str,
            " (", (i32)op, ")"
        );
    }
}

static bool extends_strict_nulls(Ty const* source, TyIntrinsic target) {
    Checker* checker = current_checker;
    return ty_is_related(checker, source, ty_intrinsic(checker, target), TY_RELATION_ASSIGNABLE_STRICT_NULLS);
}

static void binary_op_typecheck(
    Ty const* left,
    BinaryOp op,
    Ty const* right,
    Expr const* expr,
    ErrorCtx* errs
) {
    switch (op) {
        case BINARY_OP_PLUS:
            // PERF: we also do this checks when evaluating the type, shouldn't need to duplicate them
            if (extends_strict_nulls(left, TY_STRING) || extends_strict_nulls(right, TY_STRING)) {
            } else if (extends_strict_nulls(left, TY_NUMBER) && extends_strict_nulls(right, TY_NUMBER)) {
            } else {
                // TODO: it seems special handling of possibly null or undefined types is needed
                error_ctx_error_d(errs, expr->inner_span, 2365,
                    "Operator '+' cannot be applied to types '", left, "' and '", right, "'.");
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
            if (!extends_strict_nulls(left, TY_NUMBER)) {
                error_ctx_error_d(
                    errs,
                    expr->binary_op->left.outer_span,
                    2362,
                    "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type."
                );
            }
            if (!extends_strict_nulls(right, TY_NUMBER)) {
                error_ctx_error_d(
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
                    if (expr->binary_op->left.identifier.target) {
                        ty_check_assignable(
                            current_checker,
                            right,
                            ty_type_of_symbol(current_checker, expr->binary_op->left.identifier.target),
                            expr->binary_op->left.outer_span,
                            errs
                        );
                    }
                    break;

                case EXPR_PROPERTY:
                    // TODO
                    break;

                default:
                    error_ctx_error_d(
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
            if (!ty_is_comparable_either_way(current_checker, left, right)) {
                error_ctx_error_d(errs, expr->outer_span, 2367,
                    "This comparison appears to be unintentional because the types '", left,
                    "' and '", right, "' have no overlap.");
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
            // The expression's own type is what gets assigned
            binary_op_typecheck(left, BINARY_OP_EQUAL, expr_get_type((Expr*)expr), expr, errs);
        }   break;

        case BINARY_OP_COMMA:
        case BINARY_OP_EH_EH:
            break;
    }
}

Ty const* expr_evaluate_type(Expr const* me) {
    // Nobody checked this expression with a contextual type first, see ty/expr.c
    return ty_compute_expr_without_context(current_checker, me);
}

void expr_typecheck(Expr const* me, ErrorCtx* errs) {
    // The expression's own type first: evaluating it is what gives the expressions inside it their contextual types
    // (whoever knows this expression's contextual type has done the same before getting here)
    expr_get_type((Expr*)me);
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
                case UNARY_OP_DELETE:
                case UNARY_OP_AWAIT:
                case UNARY_OP_YIELD:
                case UNARY_OP_YIELD_STAR:
                    // TODO: add typechecking
                    break;

                case UNARY_OP_RIGHT_BANG:
                    break;
            }
            break;

        case EXPR_PROPERTY: {
            expr_typecheck(&my property->expr, errs);
            expr_typecheck(&my property->prop, errs);
            Ty const* expr_type = expr_get_type(&my property->expr);
            if (my property->optional_chain) expr_type = ty_non_nullable(current_checker, expr_type);
            bool error_if_not_found = my property->form == PROPERTY_EXPR_DOT;
            ty_check_property_access(
                current_checker,
                expr_type,
                expr_get_type(&my property->prop),
                my property->expr.outer_span,
                my property->prop.outer_span,
                error_if_not_found,
                errs
            );
        }   break;

        case EXPR_GENERIC_PARAMS: generic_params_expr_typecheck(my generic_params, errs); break;

        case EXPR_AS_TYPE:
            // TODO: check that type of expr overlaps with (not extends or is extended by) type. if it doesn't surface
            // TS2352: Conversion of type 'number' to type 'string' may be a mistake because neither type sufficiently overlaps with the other. If this was intentional, convert the expression to 'unknown' first.
            expr_typecheck(&my as_type->expr, errs);
            type_typecheck(&my as_type->type, errs);
            if (my as_type->is_satisfies) {
                Checker* checker = current_checker;
                Ty const* source = expr_get_type(&my as_type->expr);
                Ty const* target = ty_of_node(&my as_type->type);
                if (!ty_is_related(checker, source, target, TY_RELATION_ASSIGNABLE)) {
                    cstr shown = ty_report_source_cstr(checker, source, target);
                    error_ctx_error_d(errs, my as_type->keyword_span, 1360,
                        "Type '", shown, "' does not satisfy the expected type '", target, "'.");
                    cstr_drop(&shown);
                }
            }
            break;

        case EXPR_TERNARY:
            expr_typecheck(&my ternary->condition, errs);
            expr_typecheck(&my ternary->yes, errs);
            expr_typecheck(&my ternary->no, errs);
            break;

        case EXPR_CALL: {
            Expr const* callee = &my call->callable;
            if (callee->tag == EXPR_GENERIC_PARAMS) {
                // Explicit type arguments are the call's business (ty/call.c)
                for_each (i, vec_spanned_type, &callee->generic_params->params) {
                    type_typecheck(&i->type, errs);
                }
                callee = &callee->generic_params->expr;
            }
            expr_typecheck(callee, errs);
            // Resolving the call gives the arguments their contextual types, so it comes before checking them
            Checker* checker = current_checker;
            LazyMemo* memo = lazy_type_state_peek((LazyType*)&my lazy_type);
            Ty const* contextual = memo && memo->done ? memo->context : nullptr;
            ty_call_typecheck(checker, ty_expr_type(checker, callee), my call, contextual, my outer_span, errs);
            for_each (i, vec_expr, &my call->args) {
                expr_typecheck(i, errs);
            }
        }   break;

        case EXPR_FUNCTION:
            function_impl_typecheck(my function, errs);
            break;

        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &my object) {
                if (i->computed_key) {
                    expr_typecheck(i->computed_key, errs);
                }
                expr_typecheck(&i->value, errs);
            }
            break;

        case EXPR_ARRAY:
            for_each (i, vec_expr, &my array) {
                expr_typecheck(i, errs);
            }
            break;

        case EXPR_CLASS_DECL:
            class_decl_typecheck(my class_decl, errs);
            break;

        case EXPR_SPREAD:
            expr_typecheck(my spread, errs);
            break;

        case EXPR_REGEX:
            break;

        case EXPR_TEMPLATE:
            if (my template_->tag) {
                expr_typecheck(my template_->tag, errs);
            }
            for_each (i, vec_expr, &my template_->substitutions) {
                expr_typecheck(i, errs);
            }
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
    impossible;
}

zsview binary_op_get_zv(BinaryOp op) {
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
    log_error("Invalid binary operator ", (i32)op);
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
        case UNARY_OP_DELETE:
        case UNARY_OP_AWAIT:
            return 140;

        case UNARY_OP_YIELD:
        case UNARY_OP_YIELD_STAR:
            return 20; // Same as assignment

        case UNARY_OP_RIGHT_PLUS_PLUS:
        case UNARY_OP_RIGHT_MINUS_MINUS:
        case UNARY_OP_RIGHT_BANG:
            return 150;
    }
    impossible;
}

zsview unary_op_get_zv(UnaryOp op) {
    switch (op) {
        case UNARY_OP_LEFT_BANG: return c_zv("!");
        case UNARY_OP_TYPEOF: return c_zv("typeof ");
        case UNARY_OP_VOID: return c_zv("void ");
        case UNARY_OP_TILDE: return c_zv("~");
        case UNARY_OP_PLUS: return c_zv("+");
        case UNARY_OP_MINUS: return c_zv("-");
        case UNARY_OP_LEFT_PLUS_PLUS: return c_zv("++");
        case UNARY_OP_LEFT_MINUS_MINUS: return c_zv("--");
        case UNARY_OP_DELETE: return c_zv("delete ");
        case UNARY_OP_AWAIT: return c_zv("await ");
        case UNARY_OP_YIELD: return c_zv("yield ");
        case UNARY_OP_YIELD_STAR: return c_zv("yield* ");
        case UNARY_OP_RIGHT_PLUS_PLUS: return c_zv("++");
        case UNARY_OP_RIGHT_MINUS_MINUS: return c_zv("--");
        case UNARY_OP_RIGHT_BANG: return c_zv("!");
    }
    log_error("Invalid unary operator ", (i32)op);
    return c_zv("<INVALID_OP>");
}

void object_expr_item_drop(ObjectExprItem* me) {
    cstr_drop(&my name_string);
    if (my computed_key) {
        expr_drop(my computed_key);
        free(my computed_key);
    }
    expr_drop(&my value);
}
