#include "statement.h"
#include "import_statement.h"
#include "error.h"
#include "name_stack.h"
#include "class_decl.h"
#include "function_impl.h"
#include "several_types.h"

void variable_statement_drop(VariableStatement* me) {
    vec_initialized_binding_drop(&my initializers);
}

void variable_statement_add_names(VariableStatement* me, bool declared, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        if (my keyword == VAR_INIT_CONST &&
            i.ref->value.tag == EXPR_IMPLICIT_ANY &&
            !declared
        ) {
            error_ctx_error(errs, i.ref->binding.content_span, 1155, "'const' declarations must be initialized.");
        }
        initialized_binding_add_names(i.ref, my keyword != VAR_INIT_CONST, names, errs);
    }
}

void variable_statement_resolve_names(VariableStatement* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        initialized_binding_resolve_names(i.ref, names, errs);
    }
}

void variable_statement_typecheck(VariableStatement* me, ErrorCtx* errs) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        initialized_binding_typecheck(i.ref, errs);
    }
}

czview for_each_loop_statement_keyword_str(ForEachLoopStatement const* me) {
    switch (my keyword) {
        case FOR_EACH_LOOP_OF: return c_zv("of");
        case FOR_EACH_LOOP_IN: return c_zv("in");
    }
    UNREACHABLE;
}

Type const* for_each_loop_statement_resolve_var_type(ForEachLoopStatement* me) {
    switch (my types_resolve_state) {
        case RESOLVE_STATE_UNINITIALIZED: {
            my types_resolve_state = RESOLVE_STATE_IN_FLIGHT;

            Type owned_iterable_type = expr_get_type_and_typecheck(&my iterable, nullptr);
            Type const* iterable_type = type_resolve(&owned_iterable_type);
            if (iterable_type->tag == TYPE_TUPLE) {
                if (vec_tuple_element_empty(&iterable_type->tuple.elements)) {
                    iterable_type = &type_any;
                } else {
                    Type temp = type_clone(*iterable_type);
                    type_drop(&owned_iterable_type);
                    owned_iterable_type = temp;
                    type_abstract_tuple_to_array(&owned_iterable_type);
                    iterable_type = type_resolve(&owned_iterable_type);
                }
            }

            Type var_type = type_any;
            switch (my keyword) {
                case FOR_EACH_LOOP_IN:
                    switch (iterable_type->tag) {
                        case TYPE_ANY:
                        case TYPE_INTERFACE:
                        case TYPE_CALLABLE:
                            // TODO: there def needs to be more nuance in this check
                            // TODO: maybe do keyof and then index of the returned union?
                            var_type = type_string;
                            break;

                        case TYPE_ARRAY:
                        case TYPE_TUPLE:
                            var_type = type_any;
                            break;

                        default:
                            var_type = type_string;
                            break;
                    }
                    break;

                case FOR_EACH_LOOP_OF:
                    if (type_extends_array(iterable_type) || type_extends(iterable_type, &type_string)) {
                        var_type = index_type_of(iterable_type, &type_number, span_null, false, nullptr);
                    }
                    break;
            }

            if (my types_resolve_state == RESOLVE_STATE_IN_FLIGHT) {
                my iterable_type = owned_iterable_type;
                my var_type = var_type;
                my types_resolve_state = RESOLVE_STATE_DONE;
            } else {
                type_drop(&owned_iterable_type);
                type_drop(&var_type);
            }
        }   break;

        case RESOLVE_STATE_IN_FLIGHT:
            my iterable_type = type_self_referential_error;
            my var_type = type_self_referential_error;
            my types_resolve_state = RESOLVE_STATE_DONE;
            break;

        case RESOLVE_STATE_DONE: break;
    }

    return &my var_type;
}

void statement_drop(Statement* me) {
    switch (my tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_BLOCK:
            block_drop(&my block);
            break;

        case STATEMENT_IF_ELSE:
            vec_expr_drop(&my if_else.conditions);
            vec_statement_drop(&my if_else.statements);
            break;

        case STATEMENT_FOR_LOOP:
            statement_drop(&my for_loop->initialization);
            expr_drop(&my for_loop->condition);
            expr_drop(&my for_loop->afterthought);
            statement_drop(&my for_loop->body);
            free(my for_loop);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT: variable_statement_drop(&my for_each_loop->variable.statement); break;
                case FOR_EACH_LOOP_EXPR: expr_drop(&my for_each_loop->variable.expr); break;
            }
            type_drop(&my for_each_loop->iterable_type);
            type_drop(&my for_each_loop->var_type);
            expr_drop(&my for_each_loop->iterable);
            statement_drop(&my for_each_loop->body);
            free(my for_each_loop);
            break;

        case STATEMENT_SWITCH:
            expr_drop(&my switch_->needle);
            vec_switch_case_drop(&my switch_->cases);
            free(my switch_);
            break;

        case STATEMENT_TRY_CATCH:
            block_drop(&my try_catch->try_block);
            binding_drop(&my try_catch->catch_var);
            block_drop(&my try_catch->catch_block);
            block_drop(&my try_catch->finally_block);
            free(my try_catch);
            break;

        case STATEMENT_BREAK: break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            expr_drop(&my expr);
            break;

        case STATEMENT_IMPORT:
            import_statement_drop(my import);
            free(my import);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_drop(&my decl.variable);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_drop(&my decl.function);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_drop(my decl.class);
            free(my decl.class);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_drop(&my decl.type);
            break;

        case STATEMENT_DECL_NAMESPACE:
            block_drop(&my decl.namespace.contents);
            break;
    }
}

void statement_add_names(Statement* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case STATEMENT_IMPORT:
            import_statement_add_names(my import, names, errs);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_add_names(&my decl.variable, my decl.declared, names, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_add_names(&my decl.function, names, errs);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_add_names(my decl.class, names, errs);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_add_names(&my decl.type, names, errs);
            break;

        case STATEMENT_DECL_NAMESPACE:
            NONFATAL("namespace add names not implemented");
            break;

        case STATEMENT_NOOP:
        case STATEMENT_BLOCK:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_BREAK:
        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            break;
    }
}

void statement_resolve_names(Statement* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case STATEMENT_BLOCK:
            block_resolve_names(&my block, names, errs);
            break;

        case STATEMENT_IF_ELSE:
            c_foreach(i, vec_expr, my if_else.conditions) {
                expr_resolve_names(i.ref, names, errs);
            }
            c_foreach(i, vec_statement, my if_else.statements) {
                statement_resolve_names(i.ref, names, errs);
            }
            break;

        case STATEMENT_FOR_LOOP:
            name_stack_push_block_frame(names);
            statement_add_names(&my for_loop->initialization, names, errs);
            statement_resolve_names(&my for_loop->initialization, names, errs);
            expr_resolve_names(&my for_loop->condition, names, errs);
            expr_resolve_names(&my for_loop->afterthought, names, errs);
            statement_resolve_names(&my for_loop->body, names, errs);
            name_stack_pop_frame(names);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            name_stack_push_block_frame(names);
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    VecInitializedBinding* initializers = &my for_each_loop->variable.statement.initializers;
                    intptr_t var_size = vec_initialized_binding_size(initializers);
                    if (var_size > 1) {
                        error_ctx_error(
                            errs,
                            vec_initialized_binding_at(initializers, 1)->binding.content_span,
                            my for_each_loop->keyword == FOR_EACH_LOOP_OF ? 1188 : 1091,
                            "Only a single variable declaration is allowed in a 'for...%s' statement.",
                            for_each_loop_statement_keyword_str(my for_each_loop).str
                        );
                    } else if (var_size == 0) {
                        // Error added by parser
                    } else {
                        InitializedBinding* initializer = initializers->data;
                        if (initializer->value.tag != EXPR_IMPLICIT_ANY) {
                            error_ctx_error(
                                errs,
                                initializer->binding.content_span,
                                1189,
                                "The variable declaration of a 'for...%s' statement cannot have an initializer.",
                                for_each_loop_statement_keyword_str(my for_each_loop).str
                            );
                        } else if (initializer->binding.has_explicit_type) {
                            error_ctx_error(
                                errs,
                                initializer->binding.content_span,
                                2404,
                                "The left-hand side of a 'for...%s' statement cannot use a type annotation.",
                                for_each_loop_statement_keyword_str(my for_each_loop).str
                            );
                        }
                    }
                    c_foreach(i, vec_initialized_binding, *initializers) {
                        // If we set has_explicit_type = false here but don't clear the type, the type will still show
                        // up in test AST traversial (as required), but will not be typechecked (as required). This
                        // is dumb but it works.
                        i.ref->binding.has_explicit_type = false;
                        binding_add_names(&i.ref->binding, lazy_type_for_each_loop_var(my for_each_loop), names, errs);
                        binding_resolve_names(&i.ref->binding, names, errs);
                    }
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_resolve_names(&my for_each_loop->variable.expr, names, errs);
                    break;
            }
            expr_resolve_names(&my for_each_loop->iterable, names, errs);
            statement_add_names(&my for_each_loop->body, names, errs);
            statement_resolve_names(&my for_each_loop->body, names, errs);
            name_stack_pop_frame(names);
            break;

        case STATEMENT_SWITCH:
            expr_resolve_names(&my switch_->needle, names, errs);
            name_stack_push_block_frame(names);
            c_foreach(i, vec_switch_case, my switch_->cases) {
                if (i.ref->is_pattern) {
                    expr_resolve_names(&i.ref->pattern, names, errs);
                }
                c_foreach(j, vec_statement, i.ref->statements) {
                    statement_add_names(j.ref, names, errs);
                }
                c_foreach(j, vec_statement, i.ref->statements) {
                    statement_resolve_names(j.ref, names, errs);
                }
            }
            name_stack_pop_frame(names);
            break;

        case STATEMENT_TRY_CATCH:
            block_resolve_names(&my try_catch->try_block, names, errs);

            name_stack_push_block_frame(names);
            binding_add_names(&my try_catch->catch_var, lazy_type_ref(&type_any), names, errs);
            binding_resolve_names(&my try_catch->catch_var, names, errs);
            block_resolve_names(&my try_catch->catch_block, names, errs);
            name_stack_pop_frame(names);

            block_resolve_names(&my try_catch->finally_block, names, errs);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_THROW_EXPR:
            expr_resolve_names(&my expr, names, errs);
            break;

        case STATEMENT_RETURN_EXPR:
            if (!name_stack_has_function(names)) {
                error_ctx_error(
                    errs,
                    my expr.outer_span,
                    1108,
                    "A 'return' statement can only be used within a function body."
                );
            }
            expr_resolve_names(&my expr, names, errs);
            break;

        case STATEMENT_IMPORT: break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_resolve_names(&my decl.variable, names, errs);
            break;

        case STATEMENT_DECL_FUNCTION: {
            name_stack_push_block_frame(names);

            static NamedVariable this_var;
            this_var = (NamedVariable){ .type = lazy_type_value(type_any) };
            name_stack_add_variable_s(names, cstr_lit("this"), &this_var);

            function_impl_resolve_names(&my decl.function, names, errs);

            name_stack_pop_frame(names);
        }   break;

        case STATEMENT_DECL_CLASS:
            class_decl_resolve_names(my decl.class, names, errs);
            break;

        case STATEMENT_DECL_TYPE:
            name_stack_push_generic_frame(names, &my decl.type.generic);
            c_foreach(i, vec_spanned_type, my decl.type.interface_extends) {
                type_resolve_names(&i.ref->type, names, errs);
            }
            type_resolve_names(&my decl.type.type, names, errs);
            name_stack_pop_frame(names);
            break;

        case STATEMENT_DECL_NAMESPACE:
            block_resolve_names(&my decl.namespace.contents, names, errs);
            break;

        case STATEMENT_NOOP:
        case STATEMENT_BREAK:
            break;
    }
}

void statement_typecheck(Statement* me, StatementCtx* ctx, ErrorCtx* errs) {
    switch (my tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_BLOCK:
            block_typecheck(&my block, ctx, errs);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            if (statement_size != cond_size && statement_size != cond_size + 1) {
                PANIC("invalid if statement has %d conditions and %d statements", cond_size, statement_size);
            }
            c_foreach(i, vec_expr, my if_else.conditions) {
                Type t = expr_get_type_and_typecheck(i.ref, errs);
                // Conditions do not have to be booleans
                type_drop(&t);
            }
            c_foreach(i, vec_statement, my if_else.statements) {
                statement_typecheck(i.ref, ctx, errs);
            }
            break;
        }

        case STATEMENT_FOR_LOOP: {
            statement_typecheck(&my for_loop->initialization, ctx, errs);
            Type t = expr_get_type_and_typecheck(&my for_loop->condition, errs);
            type_drop(&t);
            t = expr_get_type_and_typecheck(&my for_loop->afterthought, errs);
            type_drop(&t);
            bool break_was_allowed = ctx->break_allowed;
            ctx->break_allowed = true;
            statement_typecheck(&my for_loop->body, ctx, errs);
            ctx->break_allowed = break_was_allowed;
            break;
        }

        case STATEMENT_FOR_EACH_LOOP: {
            for_each_loop_statement_resolve_var_type(my for_each_loop);
            Type const* iterable_type = type_resolve(&my for_each_loop->iterable_type);

            switch (my for_each_loop->keyword) {
                case FOR_EACH_LOOP_IN:
                    switch (my for_each_loop->variable.tag) {
                        case FOR_EACH_LOOP_STATEMENT:
                            if (!vec_initialized_binding_empty(&my for_each_loop->variable.statement.initializers) &&
                                my for_each_loop->variable.statement.initializers.data->binding.tag != BINDING_IDENTIFIER
                            ) {
                                ErrorPhase prev_phase = error_ctx_swap_phase(errs, ERROR_PHASE_TYPECHECK_2);
                                error_ctx_error(
                                    errs,
                                    my for_each_loop->variable.statement.initializers.data->binding.content_span,
                                    2491,
                                    "The left-hand side of a 'for...in' statement cannot be a destructuring pattern."
                                );
                                error_ctx_swap_phase(errs, prev_phase);
                            }
                            break;

                        case FOR_EACH_LOOP_EXPR:
                            if (my for_each_loop->variable.expr.tag == EXPR_ARRAY ||
                                my for_each_loop->variable.expr.tag == EXPR_OBJECT
                            ) {
                                ErrorPhase prev_phase = error_ctx_swap_phase(errs, ERROR_PHASE_TYPECHECK_2);
                                error_ctx_error(
                                    errs,
                                    my for_each_loop->variable.expr.outer_span,
                                    2491,
                                    "The left-hand side of a 'for...in' statement cannot be a destructuring pattern."
                                );
                                error_ctx_swap_phase(errs, prev_phase);
                            }
                            break;
                    }

                    switch (iterable_type->tag) {
                        case TYPE_ANY:
                        case TYPE_INTERFACE:
                        case TYPE_CALLABLE:
                        case TYPE_ARRAY:
                        case TYPE_TUPLE:
                            break;

                        default: {
                            if (errs) {
                                cstr msg = cstr_lit("The right-hand side of a 'for...in' statement must be of type 'any', an object type or a type parameter, but here has type '");
                                cstr_append_type_error_msg(&msg, iterable_type);
                                cstr_append_sv(&msg, c_sv("'."));
                                error_ctx_error_s(errs, my for_each_loop->iterable.outer_span, 2407, msg);
                            }
                        }   break;
                    }
                    break;

                case FOR_EACH_LOOP_OF:
                    if (!type_extends_array(iterable_type) && !type_extends(iterable_type, &type_string)) {
                        cstr msg = cstr_lit("Type '");
                        cstr_append_type_error_msg(&msg, iterable_type);
                        cstr_append_sv(&msg, c_sv("' is not an array type or a string type."));
                        error_ctx_error_s(errs, my for_each_loop->iterable.outer_span, 2495, msg);
                    }
                    break;
            }

            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT: {
                    c_foreach(i, vec_initialized_binding, my for_each_loop->variable.statement.initializers) {
                        binding_typecheck(&i.ref->binding, errs);
                    }
                }   break;

                case FOR_EACH_LOOP_EXPR: {
                    Type existing_var_type = expr_get_type_and_typecheck(&my for_each_loop->variable.expr, errs);
                    check_assignable_or_error(
                        &my for_each_loop->var_type,
                        &existing_var_type,
                        my for_each_loop->variable.expr.outer_span,
                        errs
                    );
                    type_drop(&existing_var_type);
                    break;
                }
            }

            bool break_was_allowed = ctx->break_allowed;
            ctx->break_allowed = true;
            statement_typecheck(&my for_each_loop->body, ctx, errs);
            ctx->break_allowed = break_was_allowed;
        }   break;

        case STATEMENT_SWITCH: {
            Type needle_type = expr_get_type_and_typecheck(&my switch_->needle, errs);
            intptr_t default_count = 0;
            bool break_was_allowed = ctx->break_allowed;
            ctx->break_allowed = true;
            c_foreach(i, vec_switch_case, my switch_->cases) {
                if (i.ref->is_pattern) {
                    Type pattern_type = expr_get_type_and_typecheck(&i.ref->pattern, errs);
                    if (!type_overlaps(&needle_type, &pattern_type)) {
                        cstr message = cstr_lit("Type '");
                        cstr_append_type_error_msg(&message, &pattern_type);
                        cstr_append_sv(&message, c_sv("' is not comparable to type '"));
                        cstr_append_type_error_msg(&message, &needle_type);
                        cstr_append_sv(&message, c_sv("'."));
                        error_ctx_error_s(errs, i.ref->pattern.outer_span, 2678, message);
                    }
                    type_drop(&pattern_type);
                } else {
                    default_count++;
                    if (default_count == 2) {
                        error_ctx_error(
                            errs,
                            i.ref->default_span,
                            1113,
                            "A 'default' clause cannot appear more than once in a 'switch' statement."
                        );
                    }
                }
                c_foreach(j, vec_statement, i.ref->statements) {
                    statement_typecheck(j.ref, ctx, errs);
                }
            }
            type_drop(&needle_type);
            ctx->break_allowed = break_was_allowed;
        }   break;

        case STATEMENT_TRY_CATCH:
            block_typecheck(&my try_catch->try_block, ctx, errs);
            binding_typecheck(&my try_catch->catch_var, errs);
            // No need to typecheck catch variable
            block_typecheck(&my try_catch->catch_block, ctx, errs);
            block_typecheck(&my try_catch->finally_block, ctx, errs);
            break;

        case STATEMENT_BREAK:
            if (!ctx->break_allowed) {
                error_ctx_error(
                    errs,
                    my break_span,
                    1105,
                    "A 'break' statement can only be used within an enclosing iteration or switch statement."
                );
            }
            break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_THROW_EXPR: {
            Type t = expr_get_type_and_typecheck(&my expr, errs);
            type_drop(&t);
        }   break;

        case STATEMENT_RETURN_EXPR: {
            Type t = expr_get_type_and_typecheck(&my expr, errs);

            if (ctx->specified_return) {
                check_assignable_or_error(&t, ctx->specified_return, my expr.outer_span, errs);
            }

            if (ctx->collect_returned_types) {
                ctx->impl_return = type_composite(TYPE_UNION, ctx->impl_return, t);
            } else {
                type_drop(&t);
            }
        }   break;

        case STATEMENT_IMPORT: break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_typecheck(&my decl.variable, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_typecheck(&my decl.function, errs);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_typecheck(my decl.class, errs);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_typecheck(&my decl.type, errs);
            break;

        case STATEMENT_DECL_NAMESPACE:
            block_typecheck(&my decl.namespace.contents, ctx, errs);
            break;
    }
}

void switch_case_drop(SwitchCase* me) {
    if (my is_pattern) {
        expr_drop(&my pattern);
    }
    vec_statement_drop(&my statements);
}

StatementCtx statement_ctx_toplevel() {
    return (StatementCtx){0};
}

StatementCtx statement_ctx_function(Type const* specified_return, bool collect_returned_types) {
    return (StatementCtx){
        .specified_return = specified_return,
        .collect_returned_types = collect_returned_types,
        .impl_return = type_never,
    };
}

void statement_ctx_drop(StatementCtx* me) {
    type_drop(&my impl_return);
}
