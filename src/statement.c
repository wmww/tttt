#include "statement.h"
#include "error.h"
#include "name_stack.h"

void NameInitData_drop(NameInitData* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
    Expr_drop(&self->value);
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

void VarInitStatement_drop(VarInitStatement* self) {
    vec_NameInitData_drop(&self->vars);
}

void cstr_append_var_init_statement_debug(cstr* target, VarInitStatement* self, int indent) {
    switch (self->keyword) {
        case VAR_INIT_VAR: cstr_append_sv(target, c_sv("var ")); break;
        case VAR_INIT_LET: cstr_append_sv(target, c_sv("let ")); break;
        case VAR_INIT_CONST: cstr_append_sv(target, c_sv("const ")); break;
    }
    c_foreach(i, vec_NameInitData, self->vars) {
        cstr_append_name_init_data_debug(target, i.ref, indent);
        if (i.ref != i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(";"));
}

void VarInitStatement_typecheck(VarInitStatement* self, ErrorCtx* errs) {
    c_foreach(i, vec_NameInitData, self->vars) {
        if (i.ref->value_explicit) {
            Type expr_type = Expr_get_type_and_typecheck(&i.ref->value, errs);
            if (i.ref->type_explicit) {
                check_assignable_or_error(&expr_type, &i.ref->type, i.ref->name_span, errs);
            } else {
                Type_drop(&i.ref->type);
                i.ref->type = Type_clone(expr_type);
                if (self->keyword != VAR_INIT_CONST) {
                    Type_abstract_literals(&i.ref->type);
                }
                if (i.ref->type.tag == TYPE_ARRAY &&
                    i.ref->type.array->tag == TYPE_VALUE &&
                    i.ref->type.array->value.tag == VALUE_UNDEFINED
                ) {
                    Type_drop(i.ref->type.array);
                    *i.ref->type.array = Type_any;
                }
            }
            Type_drop(&expr_type);
        }
    }
}

void InterfaceStatement_drop(InterfaceStatement* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
}

void cstr_append_interface_statement_debug(cstr* target, InterfaceStatement* self, int indent) {
    cstr_append_sv(target, c_sv("interface " CLR_NAME));
    cstr_append_s(target, self->name);
    cstr_append_sv(target, c_sv(CLR0 " "));
    cstr_append_type_debug(target, &self->type, indent);
}

void Statement_drop(Statement* self) {
    switch (self->tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_INTERFACE:
            InterfaceStatement_drop(&self->interface);
            break;

        case STATEMENT_BLOCK:
            Block_drop(&self->block);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_RETURN:
            Expr_drop(&self->expr);
            break;

        case STATEMENT_VAR_INIT:
            VarInitStatement_drop(&self->var_init);
            break;

        case STATEMENT_FUNCTION:
            cstr_drop(&self->function.name);
            FunctionImpl_drop(&self->function.impl);
            break;

        case STATEMENT_IF_ELSE:
            vec_Expr_drop(&self->if_else.conditions);
            vec_Statement_drop(&self->if_else.statements);
            break;

        case STATEMENT_LOOP:
            Statement_drop(&self->loop->initialization);
            Expr_drop(&self->loop->condition);
            Expr_drop(&self->loop->afterthought);
            Statement_drop(&self->loop->body);
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
            int statement_size = vec_Statement_size(&self->if_else.statements);
            int cond_size = vec_Expr_size(&self->if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    cstr_append_sv(target, c_sv("if ("));
                    cstr_append_expr_debug(target, vec_Expr_at(&self->if_else.conditions, i), indent);
                    cstr_append_sv(target, c_sv(") "));
                }
                cstr_append_statement_debug(target, vec_Statement_at_mut(&self->if_else.statements, i), indent);
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

void Statement_add_names(Statement* self, NameStack* names, ErrorCtx* errs) {
    switch (self->tag) {
        case STATEMENT_VAR_INIT:
            c_foreach(i, vec_NameInitData, self->var_init.vars) {
                if (!NameStack_add_variable(
                    names,
                    cstr_clone(i.ref->name),
                    &i.ref->type
                )) {
                    // name is cstr_str(&result.ref->first)
                    ErrorCtx_type_error(errs, "multiple values declared with same name");
                }
            }
            break;

        case STATEMENT_FUNCTION:
            if (!NameStack_add_variable(
                names,
                cstr_clone(self->function.name),
                &self->function.impl.type
            )) {
                // name is cstr_str(&result.ref->first)
                ErrorCtx_type_error(errs, "multiple values declared with same name");
            }
            break;

        case STATEMENT_INTERFACE:
            if (!NameStack_add_type(
                names,
                cstr_clone(self->interface.name),
                &self->interface.type,
                false, 0
            )) {
                // name is cstr_str(&result.ref->first)
                ErrorCtx_type_error(errs, "multiple values declared with same name");
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

void Statement_resolve_names(Statement* self, NameStack* names, ErrorCtx* errs) {
    switch (self->tag) {
        case STATEMENT_BLOCK:
            Block_resolve_names(&self->block, names, errs);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_RETURN:
            Expr_resolve_names(&self->expr, names, errs);
            break;

        case STATEMENT_VAR_INIT:
            c_foreach(i, vec_NameInitData, self->var_init.vars) {
                Type_resolve_names(&i.ref->type, names, errs);
                Expr_resolve_names(&i.ref->value, names, errs);
            }
            break;

        case STATEMENT_FUNCTION:
            FunctionImpl_resolve_names(&self->function.impl, names, errs);
            break;

        case STATEMENT_INTERFACE:
            Type_resolve_names(&self->interface.type, names, errs);
            break;

        case STATEMENT_IF_ELSE:
            c_foreach(i, vec_Expr, self->if_else.conditions) {
                Expr_resolve_names(i.ref, names, errs);
            }
            c_foreach(i, vec_Statement, self->if_else.statements) {
                Statement_resolve_names(i.ref, names, errs);
            }
            break;

        case STATEMENT_LOOP:
            NameStack_push_frame(names, false);
            Statement_add_names(&self->loop->initialization, names, errs);
            Statement_resolve_names(&self->loop->initialization, names, errs);
            Expr_resolve_names(&self->loop->condition, names, errs);
            Expr_resolve_names(&self->loop->afterthought, names, errs);
            Statement_resolve_names(&self->loop->body, names, errs);
            NameStack_pop_frame(names);

        case STATEMENT_NOOP:
        case STATEMENT_BREAK:
            break;
    }
}

void Statement_typecheck(Statement* self, StatementCtx* ctx, ErrorCtx* errs) {
    switch (self->tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_INTERFACE: break;

        case STATEMENT_BLOCK:
            Block_typecheck(&self->block, ctx, errs);
            break;

        case STATEMENT_EXPR: {
            Type t = Expr_get_type_and_typecheck(&self->expr, errs);
            Type_drop(&t);
        }   break;

        case STATEMENT_RETURN: {
            if (ctx->return_allowed) {
                Type t = Expr_get_type_and_typecheck(&self->expr, errs);
                check_assignable_or_error(&t, ctx->specified_return, self->expr.span, errs);
                vec_Type_push(&ctx->returned_types, t);
            } else {
                ErrorCtx_error(
                    errs,
                    self->break_span,
                    1108,
                    "A 'return' statement can only be used within a function body."
                );
            }
        }   break;

        case STATEMENT_VAR_INIT:
            VarInitStatement_typecheck(&self->var_init, errs);
            break;

        case STATEMENT_FUNCTION:
            FunctionImpl_typecheck(&self->function.impl, errs);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_Statement_size(&self->if_else.statements);
            int cond_size = vec_Expr_size(&self->if_else.conditions);
            if (statement_size != cond_size && statement_size != cond_size + 1) {
                PANIC("invalid if statement has %d conditions and %d statements", cond_size, statement_size);
            }
            c_foreach(i, vec_Expr, self->if_else.conditions) {
                Type t = Expr_get_type_and_typecheck(i.ref, errs);
                // Conditions do not have to be booleans
                Type_drop(&t);
            }
            c_foreach(i, vec_Statement, self->if_else.statements) {
                Statement_typecheck(i.ref, ctx, errs);
            }
            break;
        }

        case STATEMENT_LOOP: {
            Statement_typecheck(&self->loop->initialization, ctx, errs);
            Type t = Expr_get_type_and_typecheck(&self->loop->condition, errs);
            Type_drop(&t);
            t = Expr_get_type_and_typecheck(&self->loop->afterthought, errs);
            Type_drop(&t);
            bool break_was_allowed = ctx->break_allowd;
            ctx->break_allowd = true;
            Statement_typecheck(&self->loop->body, ctx, errs);
            ctx->break_allowd = break_was_allowed;
            break;
        }

        case STATEMENT_BREAK:
            if (!ctx->break_allowd) {
                ErrorCtx_error(
                    errs,
                    self->break_span,
                    1105,
                    "A 'break' statement can only be used within an enclosing iteration or switch statement."
                );
            }
            break;
    }
}

StatementCtx StatementCtx_toplevel() {
    return (StatementCtx){0};
}

StatementCtx StatementCtx_function(Type const* specified_return) {
    return (StatementCtx){
        .specified_return = specified_return,
        .return_allowed = true,
    };
}

void StatementCtx_drop(StatementCtx* self) {
    vec_Type_drop(&self->returned_types);
}
