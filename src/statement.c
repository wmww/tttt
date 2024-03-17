#include "statement.h"
#include "error.h"
#include "name_stack.h"

void name_init_data_drop(NameInitData* me) {
    cstr_drop(&me->name);
    type_drop(&me->type);
    expr_drop(&me->value);
}

void cstr_append_name_init_data_debug(cstr* target, NameInitData* me, int indent) {
    cstr_append_sv(target, c_sv(CLR_NAME));
    cstr_append_s(target, me->name);
    cstr_append_sv(target, c_sv(CLR0));
    if (me->type_explicit) {
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_debug(target, &me->type, indent);
    }
    if (me->value_explicit) {
        cstr_append_sv(target, c_sv(" = "));
        cstr_append_expr_debug(target, &me->value, indent);
    }
}

void var_init_statement_drop(VarInitStatement* me) {
    vec_name_init_data_drop(&me->vars);
}

void cstr_append_var_init_statement_debug(cstr* target, VarInitStatement* me, int indent) {
    switch (me->keyword) {
        case VAR_INIT_VAR: cstr_append_sv(target, c_sv("var ")); break;
        case VAR_INIT_LET: cstr_append_sv(target, c_sv("let ")); break;
        case VAR_INIT_CONST: cstr_append_sv(target, c_sv("const ")); break;
    }
    c_foreach(i, vec_name_init_data, me->vars) {
        cstr_append_name_init_data_debug(target, i.ref, indent);
        if (i.ref != i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(";"));
}

void var_init_statement_typecheck(VarInitStatement* me, ErrorCtx* errs) {
    c_foreach(i, vec_name_init_data, me->vars) {
        if (i.ref->value_explicit) {
            Type expr_type = expr_get_type_and_typecheck(&i.ref->value, errs);
            if (i.ref->type_explicit) {
                check_assignable_or_error(&expr_type, &i.ref->type, i.ref->name_span, errs);
            } else {
                type_drop(&i.ref->type);
                i.ref->type = type_clone(expr_type);
                if (me->keyword != VAR_INIT_CONST) {
                    type_abstract_literals(&i.ref->type);
                }
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

void interface_statement_drop(InterfaceStatement* me) {
    cstr_drop(&me->name);
    type_drop(&me->type);
}

void cstr_append_interface_statement_debug(cstr* target, InterfaceStatement* me, int indent) {
    cstr_append_sv(target, c_sv("interface " CLR_NAME));
    cstr_append_s(target, me->name);
    cstr_append_sv(target, c_sv(CLR0 " "));
    cstr_append_type_debug(target, &me->type, indent);
}

void statement_drop(Statement* me) {
    switch (me->tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_INTERFACE:
            interface_statement_drop(&me->interface);
            break;

        case STATEMENT_BLOCK:
            block_drop(&me->block);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_RETURN:
            expr_drop(&me->expr);
            break;

        case STATEMENT_VAR_INIT:
            var_init_statement_drop(&me->var_init);
            break;

        case STATEMENT_FUNCTION:
            cstr_drop(&me->function.name);
            function_impl_drop(&me->function.impl);
            break;

        case STATEMENT_IF_ELSE:
            vec_expr_drop(&me->if_else.conditions);
            vec_statement_drop(&me->if_else.statements);
            break;

        case STATEMENT_LOOP:
            statement_drop(&me->loop->initialization);
            expr_drop(&me->loop->condition);
            expr_drop(&me->loop->afterthought);
            statement_drop(&me->loop->body);
            free(me->loop);
            break;

        case STATEMENT_BREAK: break; // nice
    }
}

void cstr_append_statement_debug(cstr* target, Statement* me, int indent) {
    switch (me->tag) {
        case STATEMENT_NOOP: cstr_append_sv(target, c_sv(";")); break;
        case STATEMENT_INTERFACE: cstr_append_interface_statement_debug(target, &me->interface, indent); break;
        case STATEMENT_BLOCK: cstr_append_block_debug(target, &me->block, indent); break;

        case STATEMENT_EXPR:
            cstr_append_expr_debug(target, &me->expr, indent);
            cstr_append_sv(target, c_sv(";"));
            break;

        case STATEMENT_RETURN:
            cstr_append_sv(target, c_sv("return "));
            cstr_append_expr_debug(target, &me->expr, indent);
            cstr_append_sv(target, c_sv(";"));
            break;

        case STATEMENT_VAR_INIT: cstr_append_var_init_statement_debug(target, &me->var_init, indent); break;

        case STATEMENT_FUNCTION:
            cstr_append_sv(target, c_sv("function " CLR_NAME));
            cstr_append_s(target, me->function.name);
            cstr_append_sv(target, c_sv(CLR0));
            cstr_append_function_impl_debug(target, &me->function.impl, false, indent);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&me->if_else.statements);
            int cond_size = vec_expr_size(&me->if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    cstr_append_sv(target, c_sv("if ("));
                    cstr_append_expr_debug(target, vec_expr_at(&me->if_else.conditions, i), indent);
                    cstr_append_sv(target, c_sv(") "));
                }
                cstr_append_statement_debug(target, vec_statement_at_mut(&me->if_else.statements, i), indent);
                if (i < statement_size - 1) {
                    cstr_append_sv(target, c_sv(" else "));
                }
            }
        }   break;

        case STATEMENT_LOOP:
            cstr_append_sv(target, c_sv("for ("));
            cstr_append_statement_debug(target, &me->loop->initialization, indent);
            if (cstr_str(target)[cstr_size(target) - 1] != ';') {
                cstr_append_sv(target, c_sv(";"));
            }
            cstr_append_sv(target, c_sv(" "));
            cstr_append_expr_debug(target, &me->loop->condition, indent);
            cstr_append_sv(target, c_sv("; "));
            cstr_append_expr_debug(target, &me->loop->afterthought, indent);
            cstr_append_sv(target, c_sv(") "));
            cstr_append_statement_debug(target, &me->loop->body, indent);
            break;

        case STATEMENT_BREAK: cstr_append_sv(target, c_sv("break;")); break;
    }
}

void statement_add_names(Statement* me, NameStack* names, ErrorCtx* errs) {
    switch (me->tag) {
        case STATEMENT_VAR_INIT:
            c_foreach(i, vec_name_init_data, me->var_init.vars) {
                if (!name_stack_add_variable(
                    names,
                    cstr_clone(i.ref->name),
                    &i.ref->type
                )) {
                    // name is cstr_str(&result.ref->first)
                    error_ctx_type_error(errs, "multiple values declared with same name");
                }
            }
            break;

        case STATEMENT_FUNCTION:
            if (!name_stack_add_variable(
                names,
                cstr_clone(me->function.name),
                &me->function.impl.type
            )) {
                // name is cstr_str(&result.ref->first)
                error_ctx_type_error(errs, "multiple values declared with same name");
            }
            break;

        case STATEMENT_INTERFACE:
            if (!name_stack_add_type(
                names,
                cstr_clone(me->interface.name),
                &me->interface.type,
                false, 0
            )) {
                // name is cstr_str(&result.ref->first)
                error_ctx_type_error(errs, "multiple values declared with same name");
            }
            break;

        case STATEMENT_NOOP:
        case STATEMENT_BLOCK:
        case STATEMENT_EXPR:
        case STATEMENT_RETURN:
        case STATEMENT_IF_ELSE:
        case STATEMENT_LOOP:
        case STATEMENT_BREAK:
            break;
    }
}

void statement_resolve_names(Statement* me, NameStack* names, ErrorCtx* errs) {
    switch (me->tag) {
        case STATEMENT_BLOCK:
            block_resolve_names(&me->block, names, errs);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_RETURN:
            expr_resolve_names(&me->expr, names, errs);
            break;

        case STATEMENT_VAR_INIT:
            c_foreach(i, vec_name_init_data, me->var_init.vars) {
                type_resolve_names(&i.ref->type, names, errs);
                expr_resolve_names(&i.ref->value, names, errs);
            }
            break;

        case STATEMENT_FUNCTION:
            function_impl_resolve_names(&me->function.impl, names, errs);
            break;

        case STATEMENT_INTERFACE:
            type_resolve_names(&me->interface.type, names, errs);
            break;

        case STATEMENT_IF_ELSE:
            c_foreach(i, vec_expr, me->if_else.conditions) {
                expr_resolve_names(i.ref, names, errs);
            }
            c_foreach(i, vec_statement, me->if_else.statements) {
                statement_resolve_names(i.ref, names, errs);
            }
            break;

        case STATEMENT_LOOP:
            name_stack_push_frame(names, false);
            statement_add_names(&me->loop->initialization, names, errs);
            statement_resolve_names(&me->loop->initialization, names, errs);
            expr_resolve_names(&me->loop->condition, names, errs);
            expr_resolve_names(&me->loop->afterthought, names, errs);
            statement_resolve_names(&me->loop->body, names, errs);
            name_stack_pop_frame(names);

        case STATEMENT_NOOP:
        case STATEMENT_BREAK:
            break;
    }
}

void statement_typecheck(Statement* me, StatementCtx* ctx, ErrorCtx* errs) {
    switch (me->tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_INTERFACE: break;

        case STATEMENT_BLOCK:
            block_typecheck(&me->block, ctx, errs);
            break;

        case STATEMENT_EXPR: {
            Type t = expr_get_type_and_typecheck(&me->expr, errs);
            type_drop(&t);
        }   break;

        case STATEMENT_RETURN: {
            if (ctx->return_allowed) {
                Type t = expr_get_type_and_typecheck(&me->expr, errs);
                check_assignable_or_error(&t, ctx->specified_return, me->expr.span, errs);
                vec_type_push(&ctx->returned_types, t);
            } else {
                error_ctx_error(
                    errs,
                    me->break_span,
                    1108,
                    "A 'return' statement can only be used within a function body."
                );
            }
        }   break;

        case STATEMENT_VAR_INIT:
            var_init_statement_typecheck(&me->var_init, errs);
            break;

        case STATEMENT_FUNCTION:
            function_impl_typecheck(&me->function.impl, errs);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&me->if_else.statements);
            int cond_size = vec_expr_size(&me->if_else.conditions);
            if (statement_size != cond_size && statement_size != cond_size + 1) {
                PANIC("invalid if statement has %d conditions and %d statements", cond_size, statement_size);
            }
            c_foreach(i, vec_expr, me->if_else.conditions) {
                Type t = expr_get_type_and_typecheck(i.ref, errs);
                // Conditions do not have to be booleans
                type_drop(&t);
            }
            c_foreach(i, vec_statement, me->if_else.statements) {
                statement_typecheck(i.ref, ctx, errs);
            }
            break;
        }

        case STATEMENT_LOOP: {
            statement_typecheck(&me->loop->initialization, ctx, errs);
            Type t = expr_get_type_and_typecheck(&me->loop->condition, errs);
            type_drop(&t);
            t = expr_get_type_and_typecheck(&me->loop->afterthought, errs);
            type_drop(&t);
            bool break_was_allowed = ctx->break_allowd;
            ctx->break_allowd = true;
            statement_typecheck(&me->loop->body, ctx, errs);
            ctx->break_allowd = break_was_allowed;
            break;
        }

        case STATEMENT_BREAK:
            if (!ctx->break_allowd) {
                error_ctx_error(
                    errs,
                    me->break_span,
                    1105,
                    "A 'break' statement can only be used within an enclosing iteration or switch statement."
                );
            }
            break;
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
    vec_type_drop(&me->returned_types);
}
