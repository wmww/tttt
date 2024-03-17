#include "statement.h"
#include "error.h"
#include "name_stack.h"

void name_init_data_drop(NameInitData* self) {
    cstr_drop(&self->name);
    type_drop(&self->type);
    expr_drop(&self->value);
}

void cstr_append_name_init_data_debug(cstr* target, NameInitData* self, int indent) {
    cstr_append_sv(target, c_sv(CLR_NAME));
    cstr_append_s(target, self->name);
    cstr_append_sv(target, c_sv(CLR0));
    if (self->type_explicit) {
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_debug(target, &self->type, indent);
    }
    if (self->value_explicit) {
        cstr_append_sv(target, c_sv(" = "));
        cstr_append_expr_debug(target, &self->value, indent);
    }
}

void var_init_statement_drop(VarInitStatement* self) {
    vec_name_init_data_drop(&self->vars);
}

void cstr_append_var_init_statement_debug(cstr* target, VarInitStatement* self, int indent) {
    switch (self->keyword) {
        case VAR_INIT_VAR: cstr_append_sv(target, c_sv("var ")); break;
        case VAR_INIT_LET: cstr_append_sv(target, c_sv("let ")); break;
        case VAR_INIT_CONST: cstr_append_sv(target, c_sv("const ")); break;
    }
    c_foreach(i, VecNameInitData, self->vars) {
        cstr_append_name_init_data_debug(target, i.ref, indent);
        if (i.ref != i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(";"));
}

void var_init_statement_typecheck(VarInitStatement* self, ErrorCtx* errs) {
    c_foreach(i, VecNameInitData, self->vars) {
        if (i.ref->value_explicit) {
            Type expr_type = expr_get_type_and_typecheck(&i.ref->value, errs);
            if (i.ref->type_explicit) {
                check_assignable_or_error(&expr_type, &i.ref->type, i.ref->name_span, errs);
            } else {
                type_drop(&i.ref->type);
                i.ref->type = type_clone(expr_type);
                if (self->keyword != VAR_INIT_CONST) {
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

void interface_statement_drop(InterfaceStatement* self) {
    cstr_drop(&self->name);
    type_drop(&self->type);
}

void cstr_append_interface_statement_debug(cstr* target, InterfaceStatement* self, int indent) {
    cstr_append_sv(target, c_sv("interface " CLR_NAME));
    cstr_append_s(target, self->name);
    cstr_append_sv(target, c_sv(CLR0 " "));
    cstr_append_type_debug(target, &self->type, indent);
}

void statement_drop(Statement* self) {
    switch (self->tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_INTERFACE:
            interface_statement_drop(&self->interface);
            break;

        case STATEMENT_BLOCK:
            block_drop(&self->block);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_RETURN:
            expr_drop(&self->expr);
            break;

        case STATEMENT_VAR_INIT:
            var_init_statement_drop(&self->var_init);
            break;

        case STATEMENT_FUNCTION:
            cstr_drop(&self->function.name);
            function_impl_drop(&self->function.impl);
            break;

        case STATEMENT_IF_ELSE:
            vec_expr_drop(&self->if_else.conditions);
            vec_statement_drop(&self->if_else.statements);
            break;

        case STATEMENT_LOOP:
            statement_drop(&self->loop->initialization);
            expr_drop(&self->loop->condition);
            expr_drop(&self->loop->afterthought);
            statement_drop(&self->loop->body);
            free(self->loop);
            break;

        case STATEMENT_BREAK: break; // nice
    }
}

void cstr_append_statement_debug(cstr* target, Statement* self, int indent) {
    switch (self->tag) {
        case STATEMENT_NOOP: cstr_append_sv(target, c_sv(";")); break;
        case STATEMENT_INTERFACE: cstr_append_interface_statement_debug(target, &self->interface, indent); break;
        case STATEMENT_BLOCK: cstr_append_block_debug(target, &self->block, indent); break;

        case STATEMENT_EXPR:
            cstr_append_expr_debug(target, &self->expr, indent);
            cstr_append_sv(target, c_sv(";"));
            break;

        case STATEMENT_RETURN:
            cstr_append_sv(target, c_sv("return "));
            cstr_append_expr_debug(target, &self->expr, indent);
            cstr_append_sv(target, c_sv(";"));
            break;

        case STATEMENT_VAR_INIT: cstr_append_var_init_statement_debug(target, &self->var_init, indent); break;

        case STATEMENT_FUNCTION:
            cstr_append_sv(target, c_sv("function " CLR_NAME));
            cstr_append_s(target, self->function.name);
            cstr_append_sv(target, c_sv(CLR0));
            cstr_append_function_impl_debug(target, &self->function.impl, false, indent);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&self->if_else.statements);
            int cond_size = vec_expr_size(&self->if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    cstr_append_sv(target, c_sv("if ("));
                    cstr_append_expr_debug(target, vec_expr_at(&self->if_else.conditions, i), indent);
                    cstr_append_sv(target, c_sv(") "));
                }
                cstr_append_statement_debug(target, vec_statement_at_mut(&self->if_else.statements, i), indent);
                if (i < statement_size - 1) {
                    cstr_append_sv(target, c_sv(" else "));
                }
            }
        }   break;

        case STATEMENT_LOOP:
            cstr_append_sv(target, c_sv("for ("));
            cstr_append_statement_debug(target, &self->loop->initialization, indent);
            if (cstr_str(target)[cstr_size(target) - 1] != ';') {
                cstr_append_sv(target, c_sv(";"));
            }
            cstr_append_sv(target, c_sv(" "));
            cstr_append_expr_debug(target, &self->loop->condition, indent);
            cstr_append_sv(target, c_sv("; "));
            cstr_append_expr_debug(target, &self->loop->afterthought, indent);
            cstr_append_sv(target, c_sv(") "));
            cstr_append_statement_debug(target, &self->loop->body, indent);
            break;

        case STATEMENT_BREAK: cstr_append_sv(target, c_sv("break;")); break;
    }
}

void statement_add_names(Statement* self, NameStack* names, ErrorCtx* errs) {
    switch (self->tag) {
        case STATEMENT_VAR_INIT:
            c_foreach(i, VecNameInitData, self->var_init.vars) {
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
                cstr_clone(self->function.name),
                &self->function.impl.type
            )) {
                // name is cstr_str(&result.ref->first)
                error_ctx_type_error(errs, "multiple values declared with same name");
            }
            break;

        case STATEMENT_INTERFACE:
            if (!name_stack_add_type(
                names,
                cstr_clone(self->interface.name),
                &self->interface.type,
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

void statement_resolve_names(Statement* self, NameStack* names, ErrorCtx* errs) {
    switch (self->tag) {
        case STATEMENT_BLOCK:
            block_resolve_names(&self->block, names, errs);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_RETURN:
            expr_resolve_names(&self->expr, names, errs);
            break;

        case STATEMENT_VAR_INIT:
            c_foreach(i, VecNameInitData, self->var_init.vars) {
                type_resolve_names(&i.ref->type, names, errs);
                expr_resolve_names(&i.ref->value, names, errs);
            }
            break;

        case STATEMENT_FUNCTION:
            function_impl_resolve_names(&self->function.impl, names, errs);
            break;

        case STATEMENT_INTERFACE:
            type_resolve_names(&self->interface.type, names, errs);
            break;

        case STATEMENT_IF_ELSE:
            c_foreach(i, VecExpr, self->if_else.conditions) {
                expr_resolve_names(i.ref, names, errs);
            }
            c_foreach(i, VecStatement, self->if_else.statements) {
                statement_resolve_names(i.ref, names, errs);
            }
            break;

        case STATEMENT_LOOP:
            name_stack_push_frame(names, false);
            statement_add_names(&self->loop->initialization, names, errs);
            statement_resolve_names(&self->loop->initialization, names, errs);
            expr_resolve_names(&self->loop->condition, names, errs);
            expr_resolve_names(&self->loop->afterthought, names, errs);
            statement_resolve_names(&self->loop->body, names, errs);
            name_stack_pop_frame(names);

        case STATEMENT_NOOP:
        case STATEMENT_BREAK:
            break;
    }
}

void statement_typecheck(Statement* self, StatementCtx* ctx, ErrorCtx* errs) {
    switch (self->tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_INTERFACE: break;

        case STATEMENT_BLOCK:
            block_typecheck(&self->block, ctx, errs);
            break;

        case STATEMENT_EXPR: {
            Type t = expr_get_type_and_typecheck(&self->expr, errs);
            type_drop(&t);
        }   break;

        case STATEMENT_RETURN: {
            if (ctx->return_allowed) {
                Type t = expr_get_type_and_typecheck(&self->expr, errs);
                check_assignable_or_error(&t, ctx->specified_return, self->expr.span, errs);
                vec_type_push(&ctx->returned_types, t);
            } else {
                error_ctx_error(
                    errs,
                    self->break_span,
                    1108,
                    "A 'return' statement can only be used within a function body."
                );
            }
        }   break;

        case STATEMENT_VAR_INIT:
            var_init_statement_typecheck(&self->var_init, errs);
            break;

        case STATEMENT_FUNCTION:
            function_impl_typecheck(&self->function.impl, errs);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&self->if_else.statements);
            int cond_size = vec_expr_size(&self->if_else.conditions);
            if (statement_size != cond_size && statement_size != cond_size + 1) {
                PANIC("invalid if statement has %d conditions and %d statements", cond_size, statement_size);
            }
            c_foreach(i, VecExpr, self->if_else.conditions) {
                Type t = expr_get_type_and_typecheck(i.ref, errs);
                // Conditions do not have to be booleans
                type_drop(&t);
            }
            c_foreach(i, VecStatement, self->if_else.statements) {
                statement_typecheck(i.ref, ctx, errs);
            }
            break;
        }

        case STATEMENT_LOOP: {
            statement_typecheck(&self->loop->initialization, ctx, errs);
            Type t = expr_get_type_and_typecheck(&self->loop->condition, errs);
            type_drop(&t);
            t = expr_get_type_and_typecheck(&self->loop->afterthought, errs);
            type_drop(&t);
            bool break_was_allowed = ctx->break_allowd;
            ctx->break_allowd = true;
            statement_typecheck(&self->loop->body, ctx, errs);
            ctx->break_allowd = break_was_allowed;
            break;
        }

        case STATEMENT_BREAK:
            if (!ctx->break_allowd) {
                error_ctx_error(
                    errs,
                    self->break_span,
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

void statement_ctx_drop(StatementCtx* self) {
    vec_type_drop(&self->returned_types);
}
