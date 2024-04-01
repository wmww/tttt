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
#include "callable_type.h"
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
            c_foreach(i, vec_generic_item, my generic->items) {
                baseline_test_case_add_symbol(ctx, cstr_str(&i.ref->name), i.ref->name_span, i.ref->name_span);
                type_compile_test_info(&i.ref->extends, ctx);
                if (i.ref->fallback_explicit) {
                    type_compile_test_info(&i.ref->fallback, ctx);
                }
            }
            type_compile_test_info(&my generic->inner, ctx);
            break;

        case TYPE_CALLABLE: break;
        case TYPE_INTERFACE: break;

        case TYPE_ARRAY:
            type_compile_test_info(my array, ctx);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_type, my tuple) {
                type_compile_test_info(i.ref, ctx);
            }
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_compile_test_info(i.ref, ctx);
            }
            break;

        case TYPE_TERNARY: break;
    }
}

void function_impl_compile_test_info(FunctionImpl* me, BaselineTestCase* ctx) {
    if (!cstr_empty(&my name)) {
        baseline_test_case_add_symbol(ctx, cstr_str(&my name), my full_span, my full_span);
        baseline_test_case_add_type(ctx, my name_span, &my type);
    }
    type_compile_test_info(&my type, ctx);
    CallableType* callable = function_impl_callable_type(me);
    int arg_count = function_impl_arg_count(me);
    for (int i = 0; i < arg_count; i++) {
        FunctionArg* value_arg = vec_function_arg_at_mut(&my args, i);
        CallableTypeArg* type_arg = vec_callable_type_arg_at_mut(&callable->args, i);
        baseline_test_case_add_symbol(
            ctx,
            cstr_str(&value_arg->name),
            value_arg->name_span,
            value_arg->name_span
        );
        baseline_test_case_add_type(ctx, value_arg->name_span, &type_arg->type);
        type_compile_test_info(&type_arg->type, ctx);
    }
    type_compile_test_info(&callable->ret, ctx);
    block_compile_test_info(&my body, ctx);
}

void name_init_compile_test_info(NameInitData* me, BaselineTestCase* ctx) {
    baseline_test_case_add_symbol(ctx, cstr_str(&my name), my name_span, my name_span);
    baseline_test_case_add_type(ctx, my name_span, &my type);
    if (my type_explicit) {
        type_compile_test_info(&my type, ctx);
    }
    if (my value_explicit) {
        expr_compile_test_info(&my value, ctx);
    }
}

void class_decl_compile_test_info(ClassDecl* me, BaselineTestCase* ctx) {
    if (!cstr_empty(&my name)) {
        baseline_test_case_add_symbol(ctx, cstr_str(&my name), my full_span, my full_span);
        Type t = type_identifier(cstr_clone(my name), my name_span);
        baseline_test_case_add_type(ctx, my name_span, &t);
        type_drop(&t);
    }
    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        expr_compile_test_info(&my extends, ctx);
    }
    c_foreach(i, vec_type, my implements) {
        type_compile_test_info(i.ref, ctx);
    }
    c_foreach(i, vec_class_item, my items) {
        switch(i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR: function_impl_compile_test_info(&i.ref->constructor.func, ctx); break;
            case CLASS_ITEM_MEMBER: function_impl_compile_test_info(&i.ref->member.func, ctx); break;
            case CLASS_ITEM_FIELD: name_init_compile_test_info(&i.ref->field.data, ctx); break;
            case CLASS_ITEM_STATIC_BLOCK: block_compile_test_info(&i.ref->static_block, ctx); break;
        }
    }
}

void expr_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    Type t = expr_get_type_and_typecheck(me, nullptr);
    if (my tag == EXPR_ARRAY) {
        if (vec_expr_empty(&my array)) {
            type_drop(&t);
            t = type_array(c_new(Type, type_undefined));
        } else {
            type_abstract_literals(&t);
            type_abstract_tuple_to_array(&t);
        }
    }
    baseline_test_case_add_type_with_peren(ctx, my span, &t, my peren_depth);

    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL:
            break;

        case EXPR_IDENTIFIER: {
            if (my identifier.var.get && (
                    !cstr_equals(&my identifier.name, "this") ||
                    span_valid(&my identifier.var.get->name_span)
                )
            ) {
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

        case EXPR_PROPERTY:
            expr_compile_test_info(&my property->expr, ctx);
            if (my property->form != PROPERTY_EXPR_DOT) {
                expr_compile_test_info(&my property->prop, ctx);
            }
            if (my property->form == PROPERTY_EXPR_DOT) {
                baseline_test_case_add_type(ctx, my property->prop.span, &t);
            }
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

        case EXPR_FUNCTION:
            function_impl_compile_test_info(my function, ctx);
            break;

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
            class_decl_compile_test_info(my class_decl, ctx);
            break;
    }

    type_drop(&t);
}

void statement_compile_test_info(Statement* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_BLOCK:
            block_compile_test_info(&my block, ctx);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
            if (my expr.tag != EXPR_LITERAL || my expr.literal.tag != VALUE_UNDEFINED) {
                expr_compile_test_info(&my expr, ctx);
            }
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

        case STATEMENT_FOR_LOOP:
            statement_compile_test_info(&my for_loop->initialization, ctx);
            expr_compile_test_info(&my for_loop->condition, ctx);
            if (my for_loop->afterthought.tag != EXPR_LITERAL ||
                my for_loop->afterthought.literal.tag != VALUE_UNDEFINED
            ) {
                expr_compile_test_info(&my for_loop->afterthought, ctx);
            }
            statement_compile_test_info(&my for_loop->body, ctx);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    c_foreach(i, vec_name_init_data, my for_each_loop->variable.statement.vars) {
                        name_init_compile_test_info(i.ref, ctx);
                    }
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_compile_test_info(&my for_each_loop->variable.expr, ctx);
                    break;
            }
            expr_compile_test_info(&my for_each_loop->iterable, ctx);
            statement_compile_test_info(&my for_each_loop->body, ctx);
            break;

        case STATEMENT_BREAK: break;

        case STATEMENT_DECL_VARIABLE:
            c_foreach(i, vec_name_init_data, my decl.variable.vars) {
                name_init_compile_test_info(i.ref, ctx);
            }
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_compile_test_info(&my decl.function, ctx);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_compile_test_info(my decl.class, ctx);
            break;

        case STATEMENT_DECL_INTERFACE: break;
    }
}

void block_compile_test_info(Block* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_statement, my statements) {
        statement_compile_test_info(i.ref, ctx);
    }
}
