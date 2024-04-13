#include "statement.h"
#include "error.h"
#include "name_stack.h"
#include "class_decl.h"

void name_init_data_drop(NameInitData* me) {
    cstr_drop(&my name);
    type_drop(&my type);
    expr_drop(&my value);
}

void cstr_append_name_init_data_debug(cstr* target, NameInitData* me, int indent) {
    cstr_append_sv(target, c_sv(CLR_NAME));
    cstr_append_s(target, my name);
    cstr_append_sv(target, c_sv(CLR0));
    if (my type_explicit) {
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_debug(target, &my type, indent);
    }
    if (my value_explicit) {
        cstr_append_sv(target, c_sv(" = "));
        cstr_append_expr_debug(target, &my value, indent);
    }
}

void variable_statement_drop(VariableStatement* me) {
    vec_name_init_data_drop(&my vars);
}

void cstr_append_variable_statement_debug(cstr* target, VariableStatement* me, int indent) {
    switch (my keyword) {
        case VAR_INIT_VAR: cstr_append_sv(target, c_sv("var ")); break;
        case VAR_INIT_LET: cstr_append_sv(target, c_sv("let ")); break;
        case VAR_INIT_CONST: cstr_append_sv(target, c_sv("const ")); break;
        case VAR_INIT_CATCH: break;
    }
    c_foreach(i, vec_name_init_data, my vars) {
        cstr_append_name_init_data_debug(target, i.ref, indent);
        if (i.ref != i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
}

void variable_statement_add_names(VariableStatement* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_name_init_data, my vars) {
        if (!name_stack_add_variable(
            names,
            cstr_clone(i.ref->name),
            i.ref->name_span,
            &i.ref->type
        )) {
            // name is cstr_str(&result.ref->first)
            error_ctx_type_error(errs, "multiple values declared with same name");
        }
    }
}

void variable_statement_resolve_names(VariableStatement* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_name_init_data, my vars) {
        type_resolve_names(&i.ref->type, names, errs);
        expr_resolve_names(&i.ref->value, names, errs);
    }
}

void variable_statement_typecheck(VariableStatement* me, ErrorCtx* errs) {
    c_foreach(i, vec_name_init_data, my vars) {
        if (i.ref->value_explicit) {
            Type expr_type = expr_get_type_and_typecheck(&i.ref->value, errs);
            if (i.ref->type_explicit) {
                check_assignable_or_error(&expr_type, &i.ref->type, i.ref->name_span, errs);
            } else {
                type_drop(&i.ref->type);
                i.ref->type = type_clone(expr_type);
                if (my keyword != VAR_INIT_CONST || i.ref->type.tag == TYPE_TUPLE) {
                    type_abstract_literals(&i.ref->type);
                }
                type_abstract_tuple_to_array(&i.ref->type);
                if (i.ref->type.tag == TYPE_ARRAY &&
                    i.ref->type.array->tag == TYPE_VALUE &&
                    i.ref->type.array->value.tag == VALUE_UNDEFINED
                ) {
                    type_drop(i.ref->type.array);
                    *i.ref->type.array = type_any;
                }
            }
            type_drop(&expr_type);
        }
    }
}

void type_decl_statement_drop(TypeDeclStatement* me) {
    cstr_drop(&my name);
    type_drop(&my type);
}

void cstr_append_type_decl_statement_debug(cstr* target, TypeDeclStatement* me, int indent) {
    if (my is_interface) {
        cstr_append_sv(target, c_sv("interface " CLR_NAME));
        cstr_append_s(target, my name);
        cstr_append_sv(target, c_sv(CLR0 " "));
    } else {
        cstr_append_sv(target, c_sv("type " CLR_NAME));
        cstr_append_s(target, my name);
        cstr_append_sv(target, c_sv(CLR0 " = "));
    }
    cstr_append_type_debug(target, &my type, indent);
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

        case STATEMENT_TRY_CATCH:
            block_drop(&my try_catch->try_block);
            variable_statement_drop(&my try_catch->catch_var);
            block_drop(&my try_catch->catch_block);
            block_drop(&my try_catch->finally_block);
            free(my try_catch);
            break;

        case STATEMENT_BREAK: break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
            expr_drop(&my expr);
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
            type_decl_statement_drop(&my decl.type);
            break;
    }
}

void cstr_append_statement_debug(cstr* target, Statement* me, int indent) {
    switch (statement_get_exported(me)) {
        case DECL_EXPORTED_NO: break;
        case DECL_EXPORTED_YES: cstr_append_sv(target, c_sv("export ")); break;
        case DECL_EXPORTED_YES_DEFAULT: cstr_append_sv(target, c_sv("export default ")); break;
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

        case STATEMENT_TRY_CATCH:
            cstr_append_sv(target, c_sv("try "));
            cstr_append_block_debug(target, &my try_catch->try_block, indent);
            if (!vec_name_init_data_empty(&my try_catch->catch_var.vars) ||
                !vec_statement_empty(&my try_catch->catch_block.statements)
            ) {
                cstr_append_sv(target, c_sv(" catch "));
                if (!vec_name_init_data_empty(&my try_catch->catch_var.vars)) {
                    cstr_append_sv(target, c_sv("("));
                    cstr_append_variable_statement_debug(target, &my try_catch->catch_var, indent);
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

        case STATEMENT_DECL_VARIABLE:
            cstr_append_variable_statement_debug(target, &my decl.variable, indent);
            cstr_append_sv(target, c_sv(";"));
            break;

        case STATEMENT_DECL_FUNCTION:
            cstr_append_sv(target, c_sv("function " CLR_NAME));
            cstr_append_s(target, my decl.function.name);
            cstr_append_sv(target, c_sv(CLR0));
            cstr_append_function_impl_debug(target, &my decl.function, false, indent);
            break;

        case STATEMENT_DECL_CLASS: cstr_append_class_decl_debug(target, my decl.class, indent); break;

        case STATEMENT_DECL_TYPE: cstr_append_type_decl_statement_debug(target, &my decl.type, indent); break;
    }
}

void statement_add_names(Statement* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case STATEMENT_DECL_VARIABLE:
            variable_statement_add_names(&my decl.variable, names, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            if (!name_stack_add_variable(
                names,
                cstr_clone(my decl.function.name),
                my decl.function.full_span,
                &my decl.function.type
            )) {
                // name is cstr_str(&result.ref->first)
                error_ctx_type_error(errs, "multiple values declared with same name");
            }
            break;

        case STATEMENT_DECL_CLASS:
            if (!name_stack_add_variable(
                names,
                cstr_clone(my decl.class->name),
                my decl.class->name_span,
                &my decl.class->constructor_type
            )) {
                // name is cstr_str(&result.ref->first)
                error_ctx_type_error(errs, "multiple values declared with same name");
            }

            if (!name_stack_add_type(
                names,
                cstr_clone(my decl.class->name),
                my decl.class->name_span,
                &my decl.class->interface
            )) {
                // name is cstr_str(&result.ref->first)
                error_ctx_type_error(errs, "multiple values declared with same name");
            }
            break;

        case STATEMENT_DECL_TYPE:
            if (!name_stack_add_type(
                names,
                cstr_clone(my decl.type.name),
                span_null, // TODO
                &my decl.type.type)
            ) {
                // name is cstr_str(&result.ref->first)
                error_ctx_type_error(errs, "multiple values declared with same name");
            }
            break;

        case STATEMENT_NOOP:
        case STATEMENT_BLOCK:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_BREAK:
        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
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
            name_stack_push_frame(names, STACK_FRAME_BLOCK);
            statement_add_names(&my for_loop->initialization, names, errs);
            statement_resolve_names(&my for_loop->initialization, names, errs);
            expr_resolve_names(&my for_loop->condition, names, errs);
            expr_resolve_names(&my for_loop->afterthought, names, errs);
            statement_resolve_names(&my for_loop->body, names, errs);
            name_stack_pop_frame(names);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            name_stack_push_frame(names, STACK_FRAME_BLOCK);
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

        case STATEMENT_TRY_CATCH:
            block_resolve_names(&my try_catch->try_block, names, errs);

            name_stack_push_frame(names, STACK_FRAME_BLOCK);
            variable_statement_add_names(&my try_catch->catch_var, names, errs);
            variable_statement_resolve_names(&my try_catch->catch_var, names, errs);
            block_resolve_names(&my try_catch->catch_block, names, errs);
            name_stack_pop_frame(names);

            block_resolve_names(&my try_catch->finally_block, names, errs);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
            expr_resolve_names(&my expr, names, errs);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_resolve_names(&my decl.variable, names, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            name_stack_push_frame(names, STACK_FRAME_CLASS);
            name_stack_add_variable(names, cstr_lit("this"), span_null, &type_any);
            function_impl_resolve_names(&my decl.function, names, errs);
            name_stack_pop_frame(names);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_resolve_names(my decl.class, names, errs);
            break;

        case STATEMENT_DECL_TYPE:
            type_resolve_names(&my decl.type.type, names, errs);
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
            bool break_was_allowed = ctx->break_allowd;
            ctx->break_allowd = true;
            statement_typecheck(&my for_loop->body, ctx, errs);
            ctx->break_allowd = break_was_allowed;
            break;
        }

        case STATEMENT_FOR_EACH_LOOP: {
            Type iterable_type = expr_get_type_and_typecheck(&my for_each_loop->iterable, errs);
            type_abstract_tuple_to_array(&iterable_type);
            Type var_type = type_any;

            switch (my for_each_loop->keyword) {
                case FOR_EACH_LOOP_IN:
                    switch (iterable_type.tag) {
                        case TYPE_ANY:
                        case TYPE_INTERFACE:
                        case TYPE_CALLABLE:
                            // TODO: there def needs to be more nuance in this check
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
                            error_ctx_error_s(errs, my for_each_loop->iterable.span, 2407, msg);
                            var_type = type_string;
                        }   break;
                    }
                    break;

                case FOR_EACH_LOOP_OF:
                    if (type_extends_array(&iterable_type) || type_extends(&iterable_type, &type_string)) {
                        var_type = array_type_of(iterable_type);
                        iterable_type = type_any;
                    } else {
                        cstr msg = cstr_lit("Type '");
                        cstr_append_type_error_msg(&msg, &iterable_type);
                        cstr_append_sv(&msg, c_sv("' is not an array type or a string type."));
                        error_ctx_error_s(errs, my for_each_loop->iterable.span, 2495, msg);
                    }
                    break;
            }
            type_drop(&iterable_type);

            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    if (vec_name_init_data_size(&my for_each_loop->variable.statement.vars) > 1) {
                        error_ctx_error(
                            errs,
                            vec_name_init_data_at(&my for_each_loop->variable.statement.vars, 1)->name_span,
                            my for_each_loop->keyword == FOR_EACH_LOOP_OF ? 1188 : 1091,
                            "Only a single variable declaration is allowed in a 'for...%s' statement.",
                            for_each_loop_statement_keyword_str(my for_each_loop).str
                        );
                    } else {
                        ASSERT(vec_name_init_data_size(&my for_each_loop->variable.statement.vars) >= 1);
                    }
                    c_foreach(i, vec_name_init_data, my for_each_loop->variable.statement.vars) {
                        type_drop(&i.ref->type);
                        if (i.ref == i.end - 1) {
                            i.ref->type = var_type;
                            var_type = type_any;
                        } else {
                            i.ref->type = type_clone(var_type);
                        }
                    }
                    variable_statement_typecheck(&my for_each_loop->variable.statement, errs);
                    break;

                case FOR_EACH_LOOP_EXPR: {
                    Type existing_var_type = expr_get_type_and_typecheck(&my for_each_loop->variable.expr, errs);
                    check_assignable_or_error(&var_type, &existing_var_type, my for_each_loop->variable.expr.span, errs);
                    type_drop(&existing_var_type);
                    break;
                }
            }
            type_drop(&var_type);

            bool break_was_allowed = ctx->break_allowd;
            ctx->break_allowd = true;
            statement_typecheck(&my for_each_loop->body, ctx, errs);
            ctx->break_allowd = break_was_allowed;
        }   break;

        case STATEMENT_TRY_CATCH:
            block_typecheck(&my try_catch->try_block, ctx, errs);
            // No need to typecheck catch variable
            block_typecheck(&my try_catch->catch_block, ctx, errs);
            block_typecheck(&my try_catch->finally_block, ctx, errs);
            break;

        case STATEMENT_BREAK:
            if (!ctx->break_allowd) {
                error_ctx_error(
                    errs,
                    my break_span,
                    1105,
                    "A 'break' statement can only be used within an enclosing iteration or switch statement."
                );
            }
            break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR: {
            Type t = expr_get_type_and_typecheck(&my expr, errs);
            type_drop(&t);
        }   break;

        case STATEMENT_RETURN_EXPR: {
            if (ctx->return_allowed) {
                Type t = expr_get_type_and_typecheck(&my expr, errs);
                check_assignable_or_error(&t, ctx->specified_return, my expr.span, errs);
                vec_type_push(&ctx->returned_types, t);
            } else {
                error_ctx_error(
                    errs,
                    my break_span,
                    1108,
                    "A 'return' statement can only be used within a function body."
                );
            }
        }   break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_typecheck(&my decl.variable, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_typecheck(&my decl.function, errs);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_typecheck(my decl.class, errs);
            break;

        case STATEMENT_DECL_TYPE: break;
    }
}

StatementCtx statement_ctx_toplevel() {
    return (StatementCtx){0};
}

StatementCtx statement_ctx_function(Type const* specified_return) {
    return (StatementCtx){
        .specified_return = specified_return,
        .return_allowed = true,
    };
}

void statement_ctx_drop(StatementCtx* me) {
    vec_type_drop(&my returned_types);
}
