#include "statement.h"
#include "import_statement.h"
#include "error.h"
#include "name_stack.h"
#include "class_decl.h"
#include "function_impl.h"
#include "identifier_type.h"

void variable_statement_drop(VariableStatement* me) {
    vec_initialized_binding_drop(&my initializers);
}

void cstr_append_variable_statement_debug(cstr* target, VariableStatement* me, int indent) {
    switch (my keyword) {
        case VAR_INIT_VAR: cstr_append_sv(target, c_sv("var ")); break;
        case VAR_INIT_LET: cstr_append_sv(target, c_sv("let ")); break;
        case VAR_INIT_CONST: cstr_append_sv(target, c_sv("const ")); break;
    }
    c_foreach(i, vec_initialized_binding, my initializers) {
        cstr_append_initialized_binding_debug(target, i.ref, indent);
        if (i.ref != i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
}

void variable_statement_add_names(VariableStatement* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        if (my keyword == VAR_INIT_CONST && i.ref->value.tag == EXPR_IMPLICIT_ANY) {
            error_ctx_error(errs, i.ref->binding.content_span, 1155, "'const' declarations must be initialized.");
        }
        binding_add_names(
            &i.ref->binding,
            lazy_type_expr(&i.ref->value, my keyword != VAR_INIT_CONST),
            names,
            errs
        );
    }
}

void variable_statement_resolve_names(VariableStatement* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        type_resolve_names(&i.ref->binding.type, names, errs);
        expr_resolve_names(&i.ref->value, names, errs);
    }
}

void variable_statement_typecheck(VariableStatement* me, ErrorCtx* errs) {
    c_foreach(i, vec_initialized_binding, my initializers) {
        initialized_binding_typecheck(i.ref, errs);
    }
}

czview for_each_loop_statement_keyword_str(ForEachLoopStatement* me) {
    switch (my keyword) {
        case FOR_EACH_LOOP_OF: return c_zv("of");
        case FOR_EACH_LOOP_IN: return c_zv("in");
    }
    UNREACHABLE;
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

void cstr_append_statement_debug(cstr* target, Statement* me, int indent) {
    switch (statement_get_exported(me)) {
        case DECL_EXPORTED_NO: break;
        case DECL_EXPORTED_YES: cstr_append_sv(target, c_sv("export ")); break;
        case DECL_EXPORTED_YES_DEFAULT: cstr_append_sv(target, c_sv("export default ")); break;
    }

    if (statement_tag_is_decl(my tag) && my decl.declared) {
        cstr_append_sv(target, c_sv("declare "));
    }

    switch (my tag) {
        case STATEMENT_NOOP: cstr_append_sv(target, c_sv(";")); break;
        case STATEMENT_BLOCK: cstr_append_block_debug(target, &my block, indent); break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    cstr_append_sv(target, c_sv("if ("));
                    cstr_append_expr_debug(target, vec_expr_at(&my if_else.conditions, i), indent);
                    cstr_append_sv(target, c_sv(") "));
                }
                cstr_append_statement_debug(target, vec_statement_at_mut(&my if_else.statements, i), indent);
                if (i < statement_size - 1) {
                    cstr_append_sv(target, c_sv(" else "));
                }
            }
        }   break;

        case STATEMENT_FOR_LOOP:
            cstr_append_sv(target, c_sv("for ("));
            cstr_append_statement_debug(target, &my for_loop->initialization, indent);
            if (cstr_str(target)[cstr_size(target) - 1] != ';') {
                cstr_append_sv(target, c_sv(";"));
            }
            cstr_append_sv(target, c_sv(" "));
            cstr_append_expr_debug(target, &my for_loop->condition, indent);
            cstr_append_sv(target, c_sv("; "));
            cstr_append_expr_debug(target, &my for_loop->afterthought, indent);
            cstr_append_sv(target, c_sv(") "));
            cstr_append_statement_debug(target, &my for_loop->body, indent);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            cstr_append_sv(target, c_sv("for ("));
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    cstr_append_variable_statement_debug(target, &my for_each_loop->variable.statement, indent);
                    break;

                case FOR_EACH_LOOP_EXPR:
                    cstr_append_expr_debug(target, &my for_each_loop->variable.expr, indent);
                    break;
            }
            cstr_append_sv(target, c_sv(" "));
            cstr_append_sv(target, czview_sv(for_each_loop_statement_keyword_str(my for_each_loop)));
            cstr_append_sv(target, c_sv(" "));
            cstr_append_expr_debug(target, &my for_each_loop->iterable, indent);
            cstr_append_sv(target, c_sv(") "));
            cstr_append_statement_debug(target, &my for_each_loop->body, indent);
            break;

        case STATEMENT_SWITCH:
            cstr_append_sv(target, c_sv("switch ("));
            cstr_append_expr_debug(target, &my switch_->needle, indent);
            cstr_append_sv(target, c_sv(") {\n"));
            c_foreach(i, vec_switch_case, my switch_->cases) {
                APPEND_INDENT(target, indent + 1);
                if (i.ref->is_default) {
                    cstr_append_sv(target, c_sv("default:\n"));
                } else {
                    cstr_append_sv(target, c_sv("case "));
                    cstr_append_expr_debug(target, &i.ref->pattern, indent + 1);
                    cstr_append_sv(target, c_sv(":\n"));
                }
                c_foreach(j, vec_statement, i.ref->statements) {
                    APPEND_INDENT(target, indent + 2);
                    cstr_append_statement_debug(target, j.ref, indent + 2);
                    cstr_append_sv(target, c_sv("\n"));
                }
                cstr_append_sv(target, c_sv("\n"));
            }
            APPEND_INDENT(target, indent);
            cstr_append_sv(target, c_sv("}"));
            break;

        case STATEMENT_TRY_CATCH:
            cstr_append_sv(target, c_sv("try "));
            cstr_append_block_debug(target, &my try_catch->try_block, indent);
            if (my try_catch->catch_var.tag != BINDING_NOOP ||
                !vec_statement_empty(&my try_catch->catch_block.statements)
            ) {
                cstr_append_sv(target, c_sv(" catch "));
                if (my try_catch->catch_var.tag != BINDING_NOOP) {
                    cstr_append_sv(target, c_sv("("));
                    cstr_append_binding_debug(target, &my try_catch->catch_var, false, indent);
                    cstr_append_sv(target, c_sv(") "));
                }
                cstr_append_block_debug(target, &my try_catch->catch_block, indent);
            }
            if (!vec_statement_empty(&my try_catch->finally_block.statements)) {
                cstr_append_sv(target, c_sv(" finally "));
                cstr_append_block_debug(target, &my try_catch->finally_block, indent);
            }
            break;

        case STATEMENT_BREAK: cstr_append_sv(target, c_sv("break;")); break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
            cstr_append_expr_debug(target, &my expr, indent);
            cstr_append_sv(target, c_sv(";"));
            break;

        case STATEMENT_RETURN_EXPR:
            cstr_append_sv(target, c_sv("return "));
            cstr_append_expr_debug(target, &my expr, indent);
            cstr_append_sv(target, c_sv(";"));
            break;

        case STATEMENT_THROW_EXPR:
            cstr_append_sv(target, c_sv("throw "));
            cstr_append_expr_debug(target, &my expr, indent);
            cstr_append_sv(target, c_sv(";"));
            break;

        case STATEMENT_IMPORT:
            cstr_append_import_statement_debug(target, my import, indent);
            break;

        case STATEMENT_DECL_VARIABLE:
            cstr_append_variable_statement_debug(target, &my decl.variable, indent);
            cstr_append_sv(target, c_sv(";"));
            break;

        case STATEMENT_DECL_FUNCTION:
            cstr_append_sv(target, c_sv("function " CLR_NAME));
            cstr_append_span(target, &my decl.function.name);
            cstr_append_sv(target, c_sv(CLR0));
            cstr_append_function_impl_debug(target, &my decl.function, false, indent);
            break;

        case STATEMENT_DECL_CLASS: cstr_append_class_decl_debug(target, my decl.class, indent); break;

        case STATEMENT_DECL_TYPE: cstr_append_type_decl_debug(target, &my decl.type, indent); break;

        case STATEMENT_DECL_NAMESPACE:
            cstr_append_sv(target, c_sv("function " CLR_NAME));
            cstr_append_span(target, &my decl.namespace.name);
            cstr_append_sv(target, c_sv(CLR0 " "));
            cstr_append_block_debug(target, &my decl.namespace.contents, indent);
            break;
    }
}

void statement_add_names(Statement* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case STATEMENT_IMPORT:
            import_statement_add_names(my import, names, errs);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_add_names(&my decl.variable, names, errs);
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
                    variable_statement_add_names(&my for_each_loop->variable.statement, names, errs);
                    variable_statement_resolve_names(&my for_each_loop->variable.statement, names, errs);
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_resolve_names(&my for_each_loop->variable.expr, names, errs);
                    break;
            }
            expr_resolve_names(&my for_each_loop->iterable, names, errs);
            statement_resolve_names(&my for_each_loop->body, names, errs);
            name_stack_pop_frame(names);
            break;

        case STATEMENT_SWITCH:
            expr_resolve_names(&my switch_->needle, names, errs);
            name_stack_push_block_frame(names);
            c_foreach(i, vec_switch_case, my switch_->cases) {
                expr_resolve_names(&i.ref->pattern, names, errs);
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
            name_stack_push_generic_frame(names, &my decl.type.generic, errs);
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
            Type iterable_type = expr_get_type_and_typecheck(&my for_each_loop->iterable, errs);
            if (iterable_type.tag == TYPE_TUPLE && vec_type_empty(&iterable_type.tuple.elements)) {
                type_drop(&iterable_type);
                iterable_type = type_any;
            } else {
                type_abstract_tuple_to_array(&iterable_type);
            }
            Type var_type = type_any;

            switch (my for_each_loop->keyword) {
                case FOR_EACH_LOOP_IN:
                    switch (iterable_type.tag) {
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

                        default: {
                            cstr msg = cstr_lit("The right-hand side of a 'for...in' statement must be of type 'any', an object type or a type parameter, but here has type '");
                            cstr_append_type_error_msg(&msg, &iterable_type);
                            cstr_append_sv(&msg, c_sv("'."));
                            error_ctx_error_s(errs, my for_each_loop->iterable.outer_span, 2407, msg);
                            var_type = type_string;
                        }   break;
                    }
                    break;

                case FOR_EACH_LOOP_OF:
                    if (type_extends_array(&iterable_type) || type_extends(&iterable_type, &type_string)) {
                        var_type = index_type_of(&iterable_type, &type_number, span_null, nullptr);
                    } else {
                        cstr msg = cstr_lit("Type '");
                        cstr_append_type_error_msg(&msg, &iterable_type);
                        cstr_append_sv(&msg, c_sv("' is not an array type or a string type."));
                        error_ctx_error_s(errs, my for_each_loop->iterable.outer_span, 2495, msg);
                    }
                    break;
            }
            type_drop(&iterable_type);

            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT: {
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
                        } else if (initializer->binding.type_explicit) {
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
                        type_drop(&i.ref->binding.type);
                        if (i.ref == i.end - 1) {
                            i.ref->binding.type = var_type;
                            var_type = type_any;
                        } else {
                            i.ref->binding.type = type_clone(var_type);
                        }
                    }
                }   break;

                case FOR_EACH_LOOP_EXPR: {
                    Type existing_var_type = expr_get_type_and_typecheck(&my for_each_loop->variable.expr, errs);
                    check_assignable_or_error(&var_type, &existing_var_type, my for_each_loop->variable.expr.outer_span, errs);
                    type_drop(&existing_var_type);
                    break;
                }
            }
            type_drop(&var_type);

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
                if (i.ref->is_default) {
                    default_count++;
                    if (default_count == 2) {
                        error_ctx_error(
                            errs,
                            i.ref->pattern.outer_span,
                            1113,
                            "A 'default' clause cannot appear more than once in a 'switch' statement."
                        );
                    }
                } else {
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
            binding_typecheck(&my try_catch->catch_var, &type_any, errs);
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
    expr_drop(&my pattern);
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
