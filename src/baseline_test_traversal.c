#include "baseline_test_traversal.h"
#include "baseline_test_case.h"
#include "type.h"
#include "generic.h"
#include "expr.h"
#include "as_type_expr.h"
#include "function_impl.h"
#include "statement.h"
#include "block.h"
#include "class_decl.h"
#include "function_impl.h"
#include "name_stack.h"

void type_compile_test_info(Type* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER: {
            baseline_test_case_add_symbol(
                ctx,
                cstr_str(&my identifier.name),
                my identifier.span,
                my identifier.type.get ? my identifier.type.get->name_span : span_null
            );
        }   break;


        case TYPE_GENERIC:
            type_compile_test_info(&my generic->inner, ctx);
            break;

        case TYPE_CALLABLE: break;
        case TYPE_INTERFACE: break;

        case TYPE_ARRAY:
            type_compile_test_info(my array, ctx);
            break;

        case TYPE_TERNARY: break;
    }
}

void expr_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    Type t = expr_get_type_and_typecheck(me, nullptr);
    baseline_test_case_add_type_with_peren(ctx, my span, &t, my peren_depth);

    switch (my tag) {
        case EXPR_LITERAL:
            break;

        case EXPR_IDENTIFIER: {
            if (my identifier.var.get) {
                baseline_test_case_add_symbol(
                    ctx,
                    cstr_str(&my identifier.name),
                    my span,
                    my identifier.var.get->name_span
                );
            }
        }   break;

        case EXPR_BINARY_OP:
            expr_compile_test_info(&my binary_op->left, ctx);
            expr_compile_test_info(&my binary_op->right, ctx);
            break;

        case EXPR_UNARY_OP:
            expr_compile_test_info(&my unary_op->expr, ctx);
            break;

        case EXPR_DOT_PROP:
            expr_compile_test_info(&my dot_prop->expr, ctx);
            baseline_test_case_add_type(ctx, my dot_prop->prop_span, &t);
            break;

        case EXPR_AS_TYPE:
            expr_compile_test_info(&my as_type->expr, ctx);
            type_compile_test_info(&my as_type->type, ctx);
            break;

        case EXPR_CALL:
            expr_compile_test_info(&my call->callable, ctx);
            c_foreach(i, vec_expr, my call->args) {
                expr_compile_test_info(i.ref, ctx);
            }
            break;

        case EXPR_FUNCTION: {
            if (!cstr_empty(&my function->name)) {
                baseline_test_case_add_symbol(ctx, cstr_str(&my function->name), my span, my span);
                baseline_test_case_add_type(ctx, my function->name_span, &my function->type);
            }
            CallableType* callable = function_impl_callable_type(my function);
            int arg_count = function_impl_arg_count(my function);
            for (int i = 0; i < arg_count; i++) {
                FunctionArg* value_arg = vec_function_arg_at_mut(&my function->args, i);
                CallableTypeArg* type_arg = vec_callable_type_arg_at_mut(&callable->args, i);
                baseline_test_case_add_symbol(
                    ctx,
                    cstr_str(&value_arg->name),
                    value_arg->name_span,
                    value_arg->name_span
                );
                baseline_test_case_add_type(ctx, value_arg->name_span, &type_arg->type);
            }
            block_compile_test_info(&my function->body, ctx);
        }   break;

        case EXPR_OBJECT:
            c_foreach(i, vec_object_expr_item, my object) {
                expr_compile_test_info(&i.ref->value, ctx);
            }
            break;

        case EXPR_ARRAY:
            c_foreach(i, vec_expr, my array) {
                expr_compile_test_info(i.ref, ctx);
            }
            break;

        case EXPR_CLASS_DECL:
            baseline_test_case_add_symbol(ctx, cstr_str(&my class_decl->name), my span, my span);
            break;
    }

    type_drop(&t);
}

void statement_compile_test_info(Statement* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case STATEMENT_NOOP: break;
        case STATEMENT_INTERFACE: break;
        case STATEMENT_BLOCK:
            block_compile_test_info(&my block, ctx);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_RETURN:
            if (my expr.tag != EXPR_LITERAL || my expr.literal.tag != VALUE_UNDEFINED) {
                expr_compile_test_info(&my expr, ctx);
            }
            break;

        case STATEMENT_VAR_INIT:
            c_foreach(i, vec_name_init_data, my var_init.vars) {
                baseline_test_case_add_symbol(ctx, cstr_str(&i.ref->name), i.ref->name_span, i.ref->name_span);
                baseline_test_case_add_type(ctx, i.ref->name_span, &i.ref->type);
                type_compile_test_info(&i.ref->type, ctx);
                if (i.ref->value_explicit) {
                    expr_compile_test_info(&i.ref->value, ctx);
                }
            }
            break;

        case STATEMENT_FUNCTION:
            baseline_test_case_add_symbol(
                ctx,
                cstr_str(&my function.name),
                my function.full_span,
                my function.full_span
            );
            baseline_test_case_add_type(ctx, my function.name_span, &my function.impl.type);
            type_compile_test_info(&my function.impl.type, ctx);
            CallableType* callable = function_impl_callable_type(&my function.impl);
            int arg_count = function_impl_arg_count(&my function.impl);
            for (int i = 0; i < arg_count; i++) {
                FunctionArg* value_arg = vec_function_arg_at_mut(&my function.impl.args, i);
                CallableTypeArg* type_arg = vec_callable_type_arg_at_mut(&callable->args, i);
                baseline_test_case_add_symbol(
                    ctx,
                    cstr_str(&value_arg->name),
                    value_arg->name_span,
                    value_arg->name_span
                );
                baseline_test_case_add_type(ctx, value_arg->name_span, &type_arg->type);
            }
            block_compile_test_info(&my function.impl.body, ctx);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    expr_compile_test_info(vec_expr_at_mut(&my if_else.conditions, i), ctx);
                }
                statement_compile_test_info(vec_statement_at_mut(&my if_else.statements, i), ctx);
            }
        }   break;

        case STATEMENT_LOOP:
            statement_compile_test_info(&my loop->initialization, ctx);
            expr_compile_test_info(&my loop->condition, ctx);
            if (my loop->afterthought.tag != EXPR_LITERAL || my loop->afterthought.literal.tag != VALUE_UNDEFINED) {
                expr_compile_test_info(&my loop->afterthought, ctx);
            }
            statement_compile_test_info(&my loop->body, ctx);
            break;

        case STATEMENT_BREAK: break;
    }
}

void block_compile_test_info(Block* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_statement, my statements) {
        statement_compile_test_info(i.ref, ctx);
    }
}
