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
#include "ty/ty.h"

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
        error_ctx_error_d(
            errs, my name, 2693,
            "'", my name, "' only refers to a type, but is being used as a value here."
        );
    } else {
        error_ctx_error_d(errs, my name, 2304, "Cannot find name '", my name, "'.");
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
            ret = type_refcopy(&my decl_ref->value_type);
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
        for_each (i, vec_interface_member, &expr_type->interface.members) {
            if (i->key.tag == INTERFACE_KEY_CALLABLE) {
                check(i->type.tag == TYPE_CALLABLE);
                return &i->type.callable->generic;
            }
        }
    }
    return nullptr;
}

Type generic_params_expr_evaluate_type(GenericParamsExpr* me) {
    Type const* expr_type = expr_get_type(&my expr);
    Generic const* generic = generic_params_get_generic(me);
    if (!generic_empty(generic)) {
        // A type of the new core is instantiated as a whole (an identifier for one would stay as it is)
        Type const* new_type = type_resolve_to_new(expr_type);
        if (new_type) expr_type = new_type;
        return generic_degenericize_type(generic, type_clone(*expr_type), &my params);
    } else {
        return type_clone(*expr_type);
    }
}

void generic_params_expr_typecheck(GenericParamsExpr* me, ErrorCtx* errs) {
    for_each (i, vec_spanned_type, &my params) {
        type_typecheck(&i->type, errs);
    }
    Generic const* generic = generic_params_get_generic(me);
    for (intptr_t i = 0; i < vec_spanned_type_size(&my params); i++) {
        if (generic && i < vec_generic_item_size(&generic->items)) {
            SpannedType const* arg = vec_spanned_type_at(&my params, i);
            GenericItem const* item = vec_generic_item_at(&generic->items, i);
            if (item->extends_explicit && !type_satisfies_constraint(generic, &my params, i)) {
                Type abstracted = type_clone(arg->type);
                type_abstract_literals(&abstracted);
                error_ctx_error_d(errs, vec_spanned_type_at(&my params, i)->span, 2344,
                    "Type '", &abstracted, "' does not satisfy the constraint '", &item->extends, "'.");
                type_drop(&abstracted);
            }
        }
    }
    if (type_resolve(expr_get_type(&my expr))->tag == TYPE_ANY) {
    } else if (generic_empty(generic)) {
        error_ctx_error_d(
            errs,
            span_between(
                vec_spanned_type_at(&my params, 0)->span,
                vec_spanned_type_back(&my params)->span
            ),
            2558,
            "Expected 0 type arguments, but got ", vec_spanned_type_size(&my params), "."
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

        case EXPR_SPREAD:
            expr_drop(my spread);
            free(my spread);
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

void expr_lookup_names(Expr* me, ScopeStack* names, ErrorCtx* errs) {
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
            type_lookup_names(&my as_type->type, names, errs);
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
            Scope name_scope = scope_init(errs);
            function_impl_add_names(my function, &name_scope, false);
            scope_push_to_stack(&name_scope, names);
            function_impl_lookup_names(my function, !span_valid(my function->arrow_span), names, errs);
            scope_drop(&name_scope);
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
            Scope class_name_scope = scope_init(errs);
            class_decl_add_names(my class_decl, &class_name_scope, false);
            scope_push_to_stack(&class_name_scope, names);
            class_decl_lookup_names(my class_decl, names, errs);
            scope_drop(&class_name_scope);
        }   break;

        case EXPR_SPREAD:
            expr_lookup_names(my spread, names, errs);
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

static Type binary_op_evaluate_type(
    Type const* left,
    BinaryOp op,
    Type const* right,
    Expr const* expr
) {
    switch (op) {
        case BINARY_OP_PLUS: {
            const bool left_extends_string = type_extends_strict_nulls(left, &type_string);
            const bool right_extends_string = type_extends_strict_nulls(right, &type_string);
            if (left_extends_string || right_extends_string) {
                if ((left_extends_string && type_resolve(left)->tag != TYPE_ANY) ||
                    (right_extends_string && type_resolve(right)->tag != TYPE_ANY)
                ) {
                    return type_string;
                } else {
                    return type_any;
                }
            } else if (type_extends_strict_nulls(left, &type_number) && type_extends_strict_nulls(right, &type_number)) {
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
    impossible;
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
            if (type_extends_strict_nulls(left, &type_string) || type_extends_strict_nulls(right, &type_string)) {
            } else if (type_extends_strict_nulls(left, &type_number) && type_extends_strict_nulls(right, &type_number)) {
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
            if (!type_extends_strict_nulls(left, &type_number)) {
                error_ctx_error_d(
                    errs,
                    expr->binary_op->left.outer_span,
                    2362,
                    "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type."
                );
            }
            if (!type_extends_strict_nulls(right, &type_number)) {
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
            if (!type_overlaps(left, right)) {
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
            Type t = binary_op_evaluate_type(left, without_assignment, right, expr);
            binary_op_typecheck(left, BINARY_OP_EQUAL, &t, expr, errs);
            type_drop(&t);
        }

        case BINARY_OP_COMMA:
        case BINARY_OP_EH_EH:
            break;
    }
}

// The type `await` gives for a value of the given type
// TODO: only unwraps Promise<T> and PromiseLike<T> by name, not arbitrary thenables. Should be the lib's Awaited<T>
static Type type_awaited(Type const* type) {
    Type const* resolved = type_resolve_until_identifier(type);
    if (resolved->tag == TYPE_NEW) {
        return type_new(ty_awaited(current_checker, type_new_ty(current_checker, resolved)));
    }
    if (resolved->tag == TYPE_IDENTIFIER &&
        vec_span_is_empty(&resolved->identifier->namespaces) &&
        vec_spanned_type_size(&resolved->identifier->generic_params) == 1 &&
        (span_equals(resolved->identifier->name, "Promise") || span_equals(resolved->identifier->name, "PromiseLike"))
    ) {
        return type_awaited(&vec_spanned_type_front(&resolved->identifier->generic_params)->type);
    }
    return type_clone(*type);
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
        case UNARY_OP_DELETE: return type_boolean;
        case UNARY_OP_AWAIT: return type_awaited(expr_get_type(expr));

        // TODO: what a yield evaluates to is decided by the generator's TNext, which needs generators to be typed
        case UNARY_OP_YIELD:
        case UNARY_OP_YIELD_STAR:
            return type_any;

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
    impossible;
}

// The type an object literal item contributes to the object's type (not abstracted)
Type object_expr_item_evaluate_type(ObjectExprItem* me, ObjectExpr const* object) {
    switch (my tag) {
        case OBJECT_ITEM_SETTER:
            // If there's a matching getter it decides the type
            if (!my computed_key) {
                for_each (i, vec_object_expr_item, object) {
                    if (i->tag == OBJECT_ITEM_GETTER && !i->computed_key && cstr_eq(&i->name_string, &my name_string)) {
                        return object_expr_item_evaluate_type(i, object);
                    }
                }
            }
            [[fallthrough]];
        case OBJECT_ITEM_GETTER: {
            Type const* func = type_resolve(expr_get_type(&my value));
            if (func->tag != TYPE_CALLABLE) {
                return type_any;
            } else if (my tag == OBJECT_ITEM_GETTER) {
                return type_clone(func->callable->ret);
            } else if (vec_callable_arg_size(&func->callable->args)) {
                return type_clone(vec_callable_arg_front(&func->callable->args)->type);
            } else {
                return type_any;
            }
        }

        default:
            return type_clone(*expr_get_type(&my value));
    }
}

static InterfaceMember* interface_find_named_member(Interface* me, cstr const* name) {
    for_each (i, vec_interface_member, &my members) {
        if (i->key.tag == INTERFACE_KEY_NAME && cstr_eq(&i->key.name_string, name)) {
            return i;
        }
    }
    return nullptr;
}

static InterfaceMember* interface_find_index_member(Interface* me, Type const* key_type) {
    for_each (i, vec_interface_member, &my members) {
        if (i->key.tag == INTERFACE_KEY_INDEX && i->key.index.type.tag == key_type->tag) {
            return i;
        }
    }
    return nullptr;
}

static Type object_expr_evaluate_type(ObjectExpr const* me) {
    Interface interface = {0};
    for_each (i, vec_object_expr_item, me) {
        if (i->tag == OBJECT_ITEM_SPREAD) {
            Type const* type = type_resolve(expr_get_type(&i->value));
            if (type->tag == TYPE_INTERFACE) {
                for_each (i, vec_interface_member, &type->interface.members) {
                    vec_interface_member_push(&interface.members, interface_member_clone(*i));
                }
            }
            continue;
        }

        Type type = object_expr_item_evaluate_type(i, me);
        type_abstract_literals(&type);
        Span declaration = i->span;
        bool is_method = i->tag == OBJECT_ITEM_METHOD;

        cstr name = cstr_null;
        bool name_is_value = i->name_is_value;
        if (i->computed_key) {
            Type const* key_type = type_resolve(expr_get_type(i->computed_key));
            if (key_type->tag == TYPE_VALUE &&
                (key_type->value.tag == VALUE_STRING || key_type->value.tag == VALUE_NUMBER)
            ) {
                cstr_append_value_js(&name, &key_type->value);
                name_is_value = key_type->value.tag == VALUE_NUMBER;
            } else if (key_type->tag == TYPE_STRING || key_type->tag == TYPE_NUMBER || key_type->tag == TYPE_ANY) {
                // A key that's only known to be some string or number becomes an index signature
                Type const* index_key_type = key_type->tag == TYPE_NUMBER ? &type_number : &type_string;
                InterfaceMember* existing = interface_find_index_member(&interface, index_key_type);
                if (existing) {
                    existing->type = type_composite(TYPE_UNION, type_move(&existing->type), type);
                } else {
                    vec_interface_member_push(&interface.members, (InterfaceMember){
                        .declarations = vec_span_single(declaration),
                        .key = {
                            .span = i->name_span,
                            .tag = INTERFACE_KEY_INDEX,
                            .index = {
                                .name = sv_make_span(c_sv("x")),
                                .type = type_clone(*index_key_type),
                            },
                        },
                        .type = type,
                    });
                }
                continue;
            } else {
                // TODO: unique symbol keys. For now the member is left out of the type
                type_drop(&type);
                continue;
            }
        } else {
            name = cstr_clone(i->name_string);
        }

        InterfaceMember* existing = interface_find_named_member(&interface, &name);
        if (existing && (i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER)) {
            // The other half of an accessor pair. The getter decides the type
            vec_span_push(&existing->declarations, declaration);
            if (i->tag == OBJECT_ITEM_GETTER) {
                type_drop(&existing->type);
                existing->type = type;
            } else {
                type_drop(&type);
            }
            cstr_drop(&name);
            continue;
        }

        InterfaceMember member = interface_member_make(
            declaration,
            nullptr,
            false,
            false,
            name,
            i->name_span,
            name_is_value,
            type
        );
        member.key.name_is_member_function = is_method;
        vec_interface_member_push(&interface.members, member);
    }
    return type_interface(interface);
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

        case EXPR_AS_TYPE:
            if (my as_type->is_satisfies) {
                // TODO: the type should contextually type the expression
                return type_refcopy(expr_get_type(&my as_type->expr));
            }
            return type_refcopy(&my as_type->type);

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

        case EXPR_OBJECT:
            return object_expr_evaluate_type(&my object);

        case EXPR_ARRAY: {
            Type index_t = type_never;
            for_each (i, vec_expr, &my array) {
                Type type = type_clone(*expr_get_type(i));
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

        case EXPR_TEMPLATE: {
            if (!my template_->tag) {
                return type_string;
            }
            // TODO: a tagged template is a call (overloads, generics, argument checks). For now only the return type
            // of a plain function is used
            Type const* tag = type_resolve(expr_get_type(my template_->tag));
            if (tag->tag == TYPE_CALLABLE && generic_empty(&tag->callable->generic)) {
                return type_clone(tag->callable->ret);
            } else {
                return type_any;
            }
        }

        case EXPR_SPREAD: {
            // The type of each spread element. TODO: tuples and iterables, which are `any` for now
            Type const* operand = type_resolve(expr_get_type(my spread));
            if (operand->tag == TYPE_ARRAY) {
                return type_clone(*operand->array.element);
            } else {
                return type_any;
            }
        }
    }
    impossible;
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
            if (my as_type->is_satisfies) {
                Type const* source = expr_get_type(&my as_type->expr);
                if (!type_extends(source, &my as_type->type)) {
                    Type shown = type_clone(*source);
                    type_abstract_literals(&shown);
                    error_ctx_error_d(errs, my as_type->keyword_span, 1360,
                        "Type '", (Type const*)&shown, "' does not satisfy the expected type '", &my as_type->type, "'.");
                    type_drop(&shown);
                }
            }
            break;

        case EXPR_TERNARY:
            expr_typecheck(&my ternary->condition, errs);
            expr_typecheck(&my ternary->yes, errs);
            expr_typecheck(&my ternary->no, errs);
            break;

        case EXPR_CALL: {
            expr_typecheck(&my call->callable, errs);
            for_each (i, vec_expr, &my call->args) {
                expr_typecheck(i, errs);
            }
            type_typecheck_call(expr_get_type(&my call->callable), my call, my outer_span, errs);
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
