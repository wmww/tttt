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
#include "identifier_type.h"
#include "callable_type.h"
#include "indexed_type.h"
#include "ternary_type.h"
#include "name_stack.h"

void generic_compile_test_info(Generic const* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_generic_item, my items) {
        baseline_test_case_add_symbol(ctx, cstr_sv(&i.ref->name), i.ref->name_span, i.ref->name_span);
        type_compile_test_info(&i.ref->extends, ctx);
        if (i.ref->fallback_explicit) {
            type_compile_test_info(&i.ref->fallback, ctx);
        }
    }
}

void binding_compile_test_info(Binding const* me, bool name_is_value, Span scope, Span full, BaselineTestCase* ctx) {
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
                name_is_value,
                span_valid(&full) ? full : my content_span
            );
            baseline_test_case_add_type(ctx, my content_span, &my type);
            break;

        case BINDING_ARRAY:
            c_foreach(i, vec_binding, my array) {
                binding_compile_test_info(i.ref, false, scope, i.ref->content_span, ctx);
            }
            break;

        case BINDING_OBJECT:
            c_foreach(i, vec_object_binding_item, my object) {
                binding_compile_test_info(&i.ref->target, false, scope, i.ref->target.content_span, ctx);
            }
            break;
    }
    if (my type_explicit) {
        type_compile_test_info(&my type, ctx);
    }
}

void initialized_binding_compile_test_info(
    InitializedBinding const* me,
    bool name_is_value,
    Span scope,
    Span full,
    BaselineTestCase* ctx
) {
    binding_compile_test_info(&my binding, name_is_value, scope, full, ctx);
    if (my value.tag != EXPR_IMPLICIT_ANY) {
        expr_compile_test_info(&my value, my binding.tag != BINDING_ARRAY, ctx);
    }
}

void interface_member_compile_test_info(InterfaceMember const* me, Span scope, BaselineTestCase* ctx) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME:
            baseline_test_case_add_symbol_with_scope(
                ctx,
                span_sv(&scope),
                cstr_sv(&my key.name),
                my key.span,
                my key.name_is_value,
                my span
            );
            baseline_test_case_add_type(ctx, my key.span, &my type);
            break;

        case INTERFACE_KEY_INDEX:
            baseline_test_case_add_symbol(ctx, span_sv(&my key.span), my key.span, my key.span);
            baseline_test_case_add_type(ctx, my key.span, &my key.index);
            type_compile_test_info(&my key.index, ctx);
            break;

        case INTERFACE_KEY_EXPR:
            expr_compile_test_info(my key.expr.get, true, ctx);
            break;

        case INTERFACE_KEY_MAPPED:
            baseline_test_case_add_symbol(
                ctx,
                span_sv(&my key.mapped.type_name_span),
                my key.mapped.type_name_span,
                my key.mapped.type_name_span
            );
            type_compile_test_info(&my key.mapped.in_type, ctx);
            type_compile_test_info(&my key.mapped.as_type, ctx);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }
    type_compile_test_info(&my type, ctx);
}

void interface_compile_test_info(Interface const* me, Span scope, BaselineTestCase* ctx) {
    c_foreach(i, vec_interface_member, my members) {
        interface_member_compile_test_info(i.ref, scope, ctx);
    }
}

void type_compile_test_info(Type const* me, BaselineTestCase* ctx) {
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
            if (cstr_equals(&my identifier->name, "object")) {
                break;
            }
            baseline_test_case_add_symbol(
                ctx,
                cstr_sv(&my identifier->name),
                my identifier->span,
                my identifier->type.get ? my identifier->type.get->name_span : span_null
            );
            c_foreach(i, vec_type, my identifier->generic_params) {
                type_compile_test_info(i.ref, ctx);
            }
        }   break;

        case TYPE_CALLABLE:
            generic_compile_test_info(&my callable->generic, ctx);
            c_foreach(i, vec_callable_arg, my callable->args) {
                binding_compile_test_info(&i.ref->binding, false, span_null, i.ref->span, ctx);
            }
            type_compile_test_info(&my callable->ret, ctx);
            break;

        case TYPE_INTERFACE: interface_compile_test_info(&my interface, span_null, ctx); break;

        case TYPE_ARRAY:
            type_compile_test_info(my array.element, ctx);
            break;

        case TYPE_TUPLE:
            c_foreach(i, vec_type, my tuple.elements) {
                type_compile_test_info(i.ref, ctx);
            }
            break;

        case TYPE_INDEXED:
            type_compile_test_info(&my indexed->interface, ctx);
            type_compile_test_info(&my indexed->index, ctx);
            break;

        case TYPE_KEYOF:
            type_compile_test_info(&my keyof->interface, ctx);
            break;

        case TYPE_TERNARY:
            type_compile_test_info(&my ternary->maybe_sub, ctx);
            type_compile_test_info(&my ternary->maybe_super, ctx);
            type_compile_test_info(&my ternary->if_true, ctx);
            type_compile_test_info(&my ternary->if_false, ctx);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            c_foreach(i, vec_type, my composite) {
                type_compile_test_info(i.ref, ctx);
            }
            break;
    }
}

void function_impl_compile_test_info(FunctionImpl const* me, Span scope, BaselineTestCase* ctx) {
    if (!cstr_empty(&my name)) {
        baseline_test_case_add_symbol_with_scope(
            ctx,
            span_sv(&scope),
            cstr_sv(&my name),
            my name_span,
            false,
            my full_span
        );
        Type t = type_callable(c_new(CallableType, callable_type_clone(my type)));
        baseline_test_case_add_type(ctx, my name_span, &t);
        type_drop(&t);
    }
    generic_compile_test_info(&my type.generic, ctx);
    intptr_t arg_size = vec_callable_arg_size(&my type.args);
    for (intptr_t i = 0; i < arg_size; i++) {
        CallableArg const* arg = vec_callable_arg_at(&my type.args, i);
        Expr const* default_arg = function_impl_arg_default_at(me, i);
        binding_compile_test_info(&arg->binding, false, span_null, arg->span, ctx);
        if (default_arg->tag != EXPR_IMPLICIT_ANY) {
            expr_compile_test_info(default_arg, arg->binding.tag != BINDING_ARRAY, ctx);
        }
    }
    if (my type.ret_explicit) {
        type_compile_test_info(&my type.ret, ctx);
    }
    block_compile_test_info(&my body, ctx);
}

void variable_statement_compile_test_info(VariableStatement const* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        initialized_binding_compile_test_info(i.ref, false, span_null, span_null, ctx);
    }
}

void class_decl_compile_test_info(ClassDecl const* me, bool is_expr, BaselineTestCase* ctx) {
    if (!cstr_empty(&my name)) {
        baseline_test_case_add_symbol(ctx, cstr_sv(&my name), my name_span, my full_span);
        if (is_expr) {
            baseline_test_case_add_type(ctx, my name_span, &my constructor_type.wrapper_type_do_not_drop);
        } else {
            Type t = type_identifier(c_new(IdentifierType, {
                .name = cstr_clone(my name),
                .span = my name_span,
            }));
            c_foreach(i, vec_generic_item, my generic.items) {
                vec_type_push(&t.identifier->generic_params, type_identifier(c_new(IdentifierType, {
                    .name = cstr_clone(i.ref->name),
                    .span = i.ref->name_span,
                })));
            }
            baseline_test_case_add_type(ctx, my name_span, &t);
            type_drop(&t);
        }
    }
    generic_compile_test_info(&my generic, ctx);
    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        Type t = expr_get_type_and_typecheck(&my extends, nullptr);
        if (t.tag == TYPE_CALLABLE && t.callable->is_constructor) {
            baseline_test_case_add_type(ctx, my extends.span, &t.callable->ret);
            expr_compile_test_info_inner(&my extends, ctx);
        } else {
            expr_compile_test_info(&my extends, true, ctx);
        }
        type_drop(&t);
    }
    c_foreach(i, vec_type, my implements) {
        type_compile_test_info(i.ref, ctx);
    }
    c_foreach(i, vec_class_item, my items) {
        switch(i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                function_impl_compile_test_info(&i.ref->constructor.func, my name_span, ctx);
                break;

            case CLASS_ITEM_MEMBER:
                baseline_test_case_add_symbol_with_scope(
                    ctx,
                    span_sv(&my name_span),
                    cstr_sv(&i.ref->member.name),
                    i.ref->member.name_span,
                    i.ref->member.name_is_value,
                    i.ref->span
                );
                Type t = type_clone(*callable_type_get_wrapper(&i.ref->member.func.type));
                baseline_test_case_add_type(
                    ctx,
                    i.ref->member.name_span,
                    &t
                );
                type_drop(&t);
                function_impl_compile_test_info(&i.ref->member.func, my name_span, ctx);
                break;

            case CLASS_ITEM_FIELD:
                initialized_binding_compile_test_info(
                    &i.ref->field.initializer,
                    i.ref->field.name_is_value,
                    my name_span,
                    i.ref->span,
                    ctx
                );
                break;
            case CLASS_ITEM_INTERFACE_MEMBER:
                interface_member_compile_test_info(&i.ref->interface_memeber, my name_span, ctx);
                break;

            case CLASS_ITEM_STATIC_BLOCK:
                block_compile_test_info(&i.ref->static_block, ctx);
                break;
        }
    }
}

void type_decl_compile_test_info(TypeDecl const* me, BaselineTestCase* ctx) {
    baseline_test_case_add_symbol(
        ctx,
        cstr_sv(&my name),
        my name_span,
        my full_span
    );
    if (!my is_interface) {
        Type to_drop = {0};
        Type const* t = &my type;
        if (generic_empty(&my generic)) {
            if (my type.tag != TYPE_IDENTIFIER ||
                (my type.identifier->type.get && !my type.identifier->type.get->is_class)
            ) {
                t = type_resolve(&my type);
                switch (t->tag) {
                    case TYPE_ANY:
                    case TYPE_UNKNOWN:
                    case TYPE_NEVER:
                    case TYPE_VOID:
                    case TYPE_BOOLEAN:
                    case TYPE_NUMBER:
                    case TYPE_STRING:
                    case TYPE_VALUE:
                    case TYPE_IDENTIFIER: break;
                    default: t = &my type;
                }
            }
        } else {
            to_drop = type_identifier(c_new(IdentifierType, {
                .name = cstr_clone(my name),
                .span = my name_span,
            }));
            c_foreach(i, vec_generic_item, my generic.items) {
                vec_type_push(&to_drop.identifier->generic_params, type_identifier(c_new(IdentifierType, {
                    .name = cstr_clone(i.ref->name),
                    .span = i.ref->name_span,
                })));
            }
            t = &to_drop;
        }
        baseline_test_case_add_type(ctx, my name_span, t);
        type_drop(&to_drop);
    }
    generic_compile_test_info(&my generic, ctx);
    if (my is_interface && my type.tag == TYPE_INTERFACE) {
        c_foreach(i, vec_spanned_type, my interface_extends) {
            type_compile_test_info(&i.ref->type, ctx);
        }
        interface_compile_test_info(&my type.interface, my name_span, ctx);
    } else {
        type_compile_test_info(&my type, ctx);
    }
}

void expr_compile_test_info_inner(Expr const* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY: break;

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
                    my identifier.span,
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

        case EXPR_PROPERTY: {
            bool has_compiled_expr_info = false;
            ErrorCtx null_errs = {0};
            Type expr_t = expr_get_type_and_typecheck(&my property->expr, &null_errs);
            Type prop_t = expr_get_type_and_typecheck(&my property->prop, &null_errs);

            if (expr_t.tag == TYPE_INTERFACE && prop_t.tag == TYPE_VALUE && prop_t.value.tag == VALUE_STRING) {
                c_foreach(i, vec_interface_member, expr_t.interface.members) {
                    if (i.ref->key.tag == INTERFACE_KEY_NAME && cstr_eq(&i.ref->key.name, &prop_t.value.string)) {
                        if (my property->form == PROPERTY_EXPR_DOT) {
                            baseline_test_case_add_symbol(
                                ctx,
                                cstr_sv(&i.ref->key.name),
                                my span,
                                i.ref->key.span
                            );
                        }
                        expr_compile_test_info(&my property->expr, true, ctx);
                        has_compiled_expr_info = true;
                        baseline_test_case_add_symbol(
                            ctx,
                            cstr_sv(&i.ref->key.name),
                            my property->prop.span,
                            i.ref->key.span
                        );
                        break;
                    }
                }
            }

            error_ctx_drop(&null_errs);
            type_drop(&prop_t);
            type_drop(&expr_t);

            if (!has_compiled_expr_info) {
                expr_compile_test_info(&my property->expr, true, ctx);
            }

            if (my property->form == PROPERTY_EXPR_BRACKET) {
                expr_compile_test_info(&my property->prop, true, ctx);
            } else {
                Type t = expr_get_type_and_typecheck(me, nullptr);
                baseline_test_case_add_type(ctx, my property->prop.span, &t);
                type_drop(&t);
            }

        }   break;

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
                type_abstract_tuple_to_array(&t);
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
            class_decl_compile_test_info(my class_decl, true, ctx);
            break;
    }
}

void expr_compile_test_info(Expr const* me, bool abstract_arrays, BaselineTestCase* ctx) {
    if (my tag != EXPR_LITERAL || my literal.tag != VALUE_NULL) {
        Type t = expr_get_type_and_typecheck(me, nullptr);
        if (my tag == EXPR_ARRAY) {
            type_abstract_literals(&t);
            if (abstract_arrays) {
                type_abstract_tuple_to_array(&t);
            }
        }
        baseline_test_case_add_type_with_peren(ctx, my span, &t, my peren_depth);
        type_drop(&t);
    }

    expr_compile_test_info_inner(me, ctx);
}

void statement_compile_test_info(Statement const* me, BaselineTestCase* ctx) {
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
                    expr_compile_test_info(vec_expr_at(&my if_else.conditions, i), true, ctx);
                }
                statement_compile_test_info(vec_statement_at(&my if_else.statements, i), ctx);
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

        case STATEMENT_SWITCH:
            expr_compile_test_info(&my switch_->needle, true, ctx);
            c_foreach(i, vec_switch_case, my switch_->cases) {
                expr_compile_test_info(&i.ref->pattern, true, ctx);
                c_foreach(j, vec_statement, i.ref->statements) {
                    statement_compile_test_info(j.ref, ctx);
                }
            }
            break;

        case STATEMENT_TRY_CATCH:
            block_compile_test_info(&my try_catch->try_block, ctx);
            if (my try_catch->catch_var.tag != BINDING_NOOP) {
                binding_compile_test_info(&my try_catch->catch_var, false, span_null, span_null, ctx);
                block_compile_test_info(&my try_catch->catch_block, ctx);
            }
            block_compile_test_info(&my try_catch->finally_block, ctx);
            break;

        case STATEMENT_BREAK: break;
        case STATEMENT_IMPORT: break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_compile_test_info(&my decl.variable, ctx);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_compile_test_info(&my decl.function, span_null, ctx);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_compile_test_info(my decl.class, false, ctx);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_compile_test_info(&my decl.type, ctx);
            break;
    }
}

void block_compile_test_info(Block const* me, BaselineTestCase* ctx) {
    c_foreach(i, vec_statement, my statements) {
        statement_compile_test_info(i.ref, ctx);
    }
}
