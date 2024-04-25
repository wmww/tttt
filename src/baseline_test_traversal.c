#include "baseline_test_traversal.h"
#include "baseline_test_case.h"
#include "type.h"
#include "interface_member.h"
#include "generic.h"
#include "expr.h"
#include "as_type_expr.h"
#include "function_impl.h"
#include "statement.h"
#include "block.h"
#include "class_decl.h"
#include "function_impl.h"
#include "callable_type.h"
#include "indexed_type.h"
#include "name_stack.h"

void generic_compile_test_info(Generic* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_generic_item, my items) {
        baseline_test_case_add_symbol(ctx, cstr_sv(&i.ref->name), i.ref->name_span, i.ref->name_span);
        type_compile_test_info(&i.ref->extends, ctx);
        if (i.ref->fallback_explicit) {
            type_compile_test_info(&i.ref->fallback, ctx);
        }
    }
}

void interface_compile_test_info(Interface* me, Span scope, BaselineTestCase* ctx) {
    c_foreach(i, vec_interface_member, my members) {
        baseline_test_case_add_symbol_with_scope(
            ctx,
            span_sv(&scope),
            cstr_sv(&i.ref->name),
            i.ref->name_span,
            i.ref->span
        );
        baseline_test_case_add_type(ctx, i.ref->name_span, &i.ref->type);
        type_compile_test_info(&i.ref->type, ctx);
    }
}

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
                cstr_sv(&my identifier.name),
                my identifier.span,
                my identifier.type.get ? my identifier.type.get->name_span : span_null
            );
            c_foreach(i, vec_type, my identifier.generic_params) {
                type_compile_test_info(i.ref, ctx);
            }
        }   break;

        case TYPE_CALLABLE:
            c_foreach(i, vec_callable_type_arg, my callable->args) {
                type_compile_test_info(&i.ref->type, ctx);
            }
            type_compile_test_info(&my callable->ret, ctx);
            break;

        case TYPE_INTERFACE: interface_compile_test_info(&my interface, span_null, ctx); break;

        case TYPE_ARRAY:
            type_compile_test_info(my array, ctx);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_type, my tuple) {
                type_compile_test_info(i.ref, ctx);
            }
            break;

        case TYPE_INDEXED:
            type_compile_test_info(&my indexed->interface, ctx);
            type_compile_test_info(&my indexed->index, ctx);
            break;

        case TYPE_TERNARY: break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_compile_test_info(i.ref, ctx);
            }
            break;
    }
}

void function_impl_compile_test_info(FunctionImpl* me, Span scope, BaselineTestCase* ctx) {
    if (!cstr_empty(&my name)) {
        baseline_test_case_add_symbol_with_scope(
            ctx,
            span_sv(&scope),
            cstr_sv(&my name),
            my full_span,
            my full_span
        );
        Type t = type_callable(c_new(CallableType, callable_type_clone(my type)));
        baseline_test_case_add_type(ctx, my name_span, &t);
        type_drop(&t);
    }
    generic_compile_test_info(&my type.generic, ctx);
    int arg_count = function_impl_arg_count(me);
    for (int i = 0; i < arg_count; i++) {
        FunctionArg* value_arg = vec_function_arg_at_mut(&my args, i);
        CallableTypeArg* type_arg = vec_callable_type_arg_at_mut(&my type.args, i);
        baseline_test_case_add_symbol(
            ctx,
            cstr_sv(&value_arg->name),
            value_arg->name_span,
            value_arg->name_span
        );
        baseline_test_case_add_type(ctx, value_arg->name_span, &type_arg->type);
        type_compile_test_info(&type_arg->type, ctx);
    }
    if (my type.ret_explicit) {
        type_compile_test_info(&my type.ret, ctx);
    }
    block_compile_test_info(&my body, ctx);
}

void name_init_compile_test_info(NameInitData* me, Span scope, Span full, BaselineTestCase* ctx) {
    baseline_test_case_add_symbol_with_scope(
        ctx,
        span_sv(&scope),
        cstr_sv(&my name),
        my name_span,
        span_valid(&full) ? full : my name_span
    );
    baseline_test_case_add_type(ctx, my name_span, &my type);
    if (my type_explicit) {
        type_compile_test_info(&my type, ctx);
    }
    if (my value_explicit) {
        expr_compile_test_info(&my value, true, ctx);
    }
}

void binding_compile_test_info(Binding* me, Span scope, Span full, BaselineTestCase* ctx) {
    switch (my tag) {
        case BINDING_NOOP:
            baseline_test_case_add_type(ctx, my content_span, &type_undefined);
            break;

        case BINDING_IDENTIFIER:
            baseline_test_case_add_symbol_with_scope(
                ctx,
                span_sv(&scope),
                cstr_sv(&my identifier),
                my content_span,
                span_valid(&full) ? full : my content_span
            );
            baseline_test_case_add_type(ctx, my content_span, &my type);
            break;

        case BINDING_ARRAY:
            c_foreach(i, vec_binding, my array) {
                binding_compile_test_info(i.ref, scope, full, ctx);
            }
            break;

        case BINDING_OBJECT:
            c_foreach(i, vec_object_binding_item, my object) {
                binding_compile_test_info(&i.ref->target, scope, full, ctx);
            }
            break;
    }
    if (my type_explicit) {
        type_compile_test_info(&my type, ctx);
    }
    if (my value_explicit) {
        expr_compile_test_info(&my value, my tag != BINDING_ARRAY, ctx);
    }
}

void variable_statement_compile_test_info(VariableStatement* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_binding, my bindings) {
        binding_compile_test_info(i.ref, span_null, span_null, ctx);
    }
}

void class_decl_compile_test_info(ClassDecl* me, BaselineTestCase* ctx) {
    if (!cstr_empty(&my name)) {
        baseline_test_case_add_symbol(ctx, cstr_sv(&my name), my full_span, my full_span);
        Type t = type_identifier(cstr_clone(my name), my name_span);
        baseline_test_case_add_type(ctx, my name_span, &t);
        type_drop(&t);
    }
    generic_compile_test_info(&my generic, ctx);
    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        expr_compile_test_info(&my extends, true, ctx);
    }
    c_foreach(i, vec_type, my implements) {
        type_compile_test_info(i.ref, ctx);
    }
    c_foreach(i, vec_class_item, my items) {
        switch(i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR: function_impl_compile_test_info(&i.ref->constructor.func, my name_span, ctx); break;
            case CLASS_ITEM_MEMBER: function_impl_compile_test_info(&i.ref->member.func, my name_span, ctx); break;
            case CLASS_ITEM_FIELD: name_init_compile_test_info(&i.ref->field.data, my name_span, i.ref->span, ctx); break;
            case CLASS_ITEM_STATIC_BLOCK: block_compile_test_info(&i.ref->static_block, ctx); break;
        }
    }
}

void type_decl_compile_test_info(TypeDecl* me, BaselineTestCase* ctx) {
    baseline_test_case_add_symbol(
        ctx,
        cstr_sv(&my name),
        my name_span,
        my full_span
    );
    Type t = type_clone(my type);
    type_simplify(&t);
    if (!my is_interface) {
        baseline_test_case_add_type(
            ctx,
            my name_span,
            &t
        );
    }
    type_drop(&t);
    generic_compile_test_info(&my generic, ctx);
    if (my is_interface && my type.tag == TYPE_INTERFACE) {
        interface_compile_test_info(&my type.interface, my name_span, ctx);
    } else {
        type_compile_test_info(&my type, ctx);
    }
}

void expr_compile_test_info(Expr* me, bool abstract_arrays, BaselineTestCase* ctx) {
    Type t = expr_get_type_and_typecheck(me, nullptr);
    if (my tag == EXPR_ARRAY) {
        if (vec_expr_empty(&my array)) {
            type_drop(&t);
            t = type_array(c_new(Type, type_undefined));
        } else {
            type_abstract_literals(&t);
            if (abstract_arrays) {
                type_abstract_tuple_to_array(&t);
            }
        }
    }
    if (my tag != EXPR_LITERAL || my literal.tag != VALUE_NULL) {
        baseline_test_case_add_type_with_peren(ctx, my span, &t, my peren_depth);
    }

    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL:
            if (my literal.tag == VALUE_UNDEFINED && csview_equals(span_sv(&my span), "undefined")) {
                baseline_test_case_add_symbol(ctx, c_sv("undefined"), my span, span_null);
            }
            break;

        case EXPR_IDENTIFIER: {
            if (my identifier.var.get && (
                    !cstr_equals(&my identifier.name, "this") ||
                    span_valid(&my identifier.var.get->name_span)
                )
            ) {
                baseline_test_case_add_symbol(
                    ctx,
                    cstr_sv(&my identifier.name),
                    my span,
                    my identifier.var.get->name_span
                );
            }
        }   break;

        case EXPR_BINARY_OP:
            expr_compile_test_info(&my binary_op->left, true, ctx);
            expr_compile_test_info(&my binary_op->right, true, ctx);
            break;

        case EXPR_UNARY_OP:
            expr_compile_test_info(&my unary_op->expr, true, ctx);
            break;

        case EXPR_PROPERTY:
            expr_compile_test_info(&my property->expr, true, ctx);
            if (my property->form != PROPERTY_EXPR_DOT) {
                expr_compile_test_info(&my property->prop, true, ctx);
            }
            if (my property->form == PROPERTY_EXPR_DOT) {
                baseline_test_case_add_type(ctx, my property->prop.span, &t);
            }
            break;

        case EXPR_AS_TYPE:
            if (my as_type->angle_bracket_syntax) {
                type_compile_test_info(&my as_type->type, ctx);
            }
            expr_compile_test_info(&my as_type->expr, true, ctx);
            if (!my as_type->angle_bracket_syntax) {
                type_compile_test_info(&my as_type->type, ctx);
            }
            break;

        case EXPR_TERNARY:
            expr_compile_test_info(&my ternary->condition, true, ctx);
            expr_compile_test_info(&my ternary->yes, true, ctx);
            expr_compile_test_info(&my ternary->no, true, ctx);
            break;

        case EXPR_CALL:
            expr_compile_test_info(&my call->callable, true, ctx);
            c_foreach(i, vec_expr, my call->args) {
                expr_compile_test_info(i.ref, true, ctx);
            }
            break;

        case EXPR_FUNCTION:
            function_impl_compile_test_info(my function, span_null, ctx);
            break;

        case EXPR_OBJECT:
            c_foreach(i, vec_object_expr_item, my object) {
                baseline_test_case_add_symbol(
                    ctx,
                    cstr_sv(&i.ref->name),
                    i.ref->name_span,
                    i.ref->name_span
                );
                Type t = expr_get_type_and_typecheck(&i.ref->value, nullptr);
                type_abstract_literals(&t);
                baseline_test_case_add_type(
                    ctx,
                    i.ref->name_span,
                    &t
                );
                type_drop(&t);
                expr_compile_test_info(&i.ref->value, true, ctx);
            }
            break;

        case EXPR_ARRAY:
            c_foreach(i, vec_expr, my array) {
                expr_compile_test_info(i.ref, true, ctx);
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
        case STATEMENT_THROW_EXPR:
            if (my expr.tag != EXPR_LITERAL || my expr.literal.tag != VALUE_UNDEFINED) {
                expr_compile_test_info(&my expr, true, ctx);
            }
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    expr_compile_test_info(vec_expr_at_mut(&my if_else.conditions, i), true, ctx);
                }
                statement_compile_test_info(vec_statement_at_mut(&my if_else.statements, i), ctx);
            }
        }   break;

        case STATEMENT_FOR_LOOP:
            statement_compile_test_info(&my for_loop->initialization, ctx);
            expr_compile_test_info(&my for_loop->condition, true, ctx);
            if (my for_loop->afterthought.tag != EXPR_LITERAL ||
                my for_loop->afterthought.literal.tag != VALUE_UNDEFINED
            ) {
                expr_compile_test_info(&my for_loop->afterthought, true, ctx);
            }
            statement_compile_test_info(&my for_loop->body, ctx);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    variable_statement_compile_test_info(&my for_each_loop->variable.statement, ctx);
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_compile_test_info(&my for_each_loop->variable.expr, true, ctx);
                    break;
            }
            expr_compile_test_info(&my for_each_loop->iterable, true, ctx);
            statement_compile_test_info(&my for_each_loop->body, ctx);
            break;

        case STATEMENT_TRY_CATCH:
            block_compile_test_info(&my try_catch->try_block, ctx);
            if (my try_catch->catch_var.tag != BINDING_NOOP) {
                binding_compile_test_info(&my try_catch->catch_var, span_null, span_null, ctx);
                block_compile_test_info(&my try_catch->catch_block, ctx);
            }
            block_compile_test_info(&my try_catch->finally_block, ctx);
            break;

        case STATEMENT_BREAK: break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_compile_test_info(&my decl.variable, ctx);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_compile_test_info(&my decl.function, span_null, ctx);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_compile_test_info(my decl.class, ctx);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_compile_test_info(&my decl.type, ctx);
            break;
    }
}

void block_compile_test_info(Block* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_statement, my statements) {
        statement_compile_test_info(i.ref, ctx);
    }
}
