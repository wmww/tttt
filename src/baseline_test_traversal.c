#include "baseline_test_traversal.h"
#include "baseline_test_case.h"
#include "type.h"
#include "generic.h"
#include "expr.h"
#include "as_type_expr.h"
#include "function_impl.h"
#include "statement.h"
#include "block.h"

void type_compile_test_info(Type* me, BaselineTestCase* ctx) {
    switch (me->tag) {
        case TYPE_ANY:
        case TYPE_UNKOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER:
            baseline_test_case_add_symbol(ctx, cstr_str(&me->identifier.name), &me->identifier.span);
            break;

        case TYPE_GENERIC:
            type_compile_test_info(&me->generic->inner, ctx);
            break;

        case TYPE_CALLABLE: break;
        case TYPE_INTERFACE: break;

        case TYPE_ARRAY:
            type_compile_test_info(me->array, ctx);
            break;

        case TYPE_TERNARY: break;
    }
}

void expr_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    Type t = expr_get_type_and_typecheck(me, nullptr);
    baseline_test_case_add_type(ctx, &me->span, me->peren_depth, &t);
    type_drop(&t);

    switch (me->tag) {
        case EXPR_LITERAL:
            break;

        case EXPR_IDENTIFIER:
            if (me->identifier.type) {
                baseline_test_case_add_symbol(ctx, cstr_str(&me->identifier.name), &me->span);
            }
            break;

        case EXPR_BINARY_OP:
            expr_compile_test_info(&me->binary_op->left, ctx);
            expr_compile_test_info(&me->binary_op->right, ctx);
            break;

        case EXPR_UNARY_OP:
            expr_compile_test_info(&me->unary_op->expr, ctx);
            break;

        case EXPR_DOT_PROP:
            expr_compile_test_info(&me->dot_prop->expr, ctx);
            break;

        case EXPR_AS_TYPE:
            expr_compile_test_info(&me->as_type->expr, ctx);
            type_compile_test_info(&me->as_type->type, ctx);
            break;

        case EXPR_CALL:
            expr_compile_test_info(&me->call->callable, ctx);
            c_foreach(i, vec_expr, me->call->args) {
                expr_compile_test_info(i.ref, ctx);
            }
            break;

        case EXPR_FUNCTION: {
            CallableType* callable = function_impl_callable_type(me->function);
            int arg_count = function_impl_arg_count(me->function);
            for (int i = 0; i < arg_count; i++) {
                FunctionArg* value_arg = vec_function_arg_at_mut(&me->function->args, i);
                CallableTypeArg* type_arg = vec_callable_type_arg_at_mut(&callable->args, i);
                baseline_test_case_add_symbol(ctx, cstr_str(&value_arg->name), &value_arg->name_span);
                baseline_test_case_add_type(ctx, &value_arg->name_span, 0, &type_arg->type);
            }
            block_compile_test_info(&me->function->body, ctx);
        }   break;

        case EXPR_OBJECT:
            c_foreach(i, vec_object_expr_item, me->object) {
                expr_compile_test_info(&i.ref->value, ctx);
            }
            break;

        case EXPR_ARRAY:
            c_foreach(i, vec_expr, me->array) {
                expr_compile_test_info(i.ref, ctx);
            }
            break;
    }
}

void statement_compile_test_info(Statement* me, BaselineTestCase* ctx) {
    switch (me->tag) {
        case STATEMENT_NOOP: break;
        case STATEMENT_INTERFACE: break;
        case STATEMENT_BLOCK:
            block_compile_test_info(&me->block, ctx);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_RETURN:
            if (me->expr.tag != EXPR_LITERAL || me->expr.literal.tag != VALUE_UNDEFINED) {
                expr_compile_test_info(&me->expr, ctx);
            }
            break;

        case STATEMENT_VAR_INIT:
            c_foreach(i, vec_name_init_data, me->var_init.vars) {
                baseline_test_case_add_symbol(ctx, cstr_str(&i.ref->name), &i.ref->name_span);
                baseline_test_case_add_type(ctx, &i.ref->name_span, 0, &i.ref->type);
                type_compile_test_info(&i.ref->type, ctx);
                if (i.ref->value_explicit) {
                    expr_compile_test_info(&i.ref->value, ctx);
                }
            }
            break;

        case STATEMENT_FUNCTION:
            block_compile_test_info(&me->function.impl.body, ctx);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&me->if_else.statements);
            int cond_size = vec_expr_size(&me->if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    expr_compile_test_info(vec_expr_at_mut(&me->if_else.conditions, i), ctx);
                }
                statement_compile_test_info(vec_statement_at_mut(&me->if_else.statements, i), ctx);
            }
        }   break;

        case STATEMENT_LOOP:
            statement_compile_test_info(&me->loop->initialization, ctx);
            expr_compile_test_info(&me->loop->condition, ctx);
            expr_compile_test_info(&me->loop->afterthought, ctx);
            statement_compile_test_info(&me->loop->body, ctx);
            break;

        case STATEMENT_BREAK: break;
    }
}

void block_compile_test_info(Block* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_statement, me->statements) {
        statement_compile_test_info(i.ref, ctx);
    }
}
