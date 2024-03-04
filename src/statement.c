#include "statement.h"
#include "error.h"

void NameInitData_drop(NameInitData* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
    Expr_drop(&self->value);
}

void cstr_append_name_init_data_debug(cstr* target, NameInitData* self, int indent) {
    cstr_append_sv(target, c_sv(CLR_NAME));
    cstr_append_s(target, self->name);
    cstr_append_sv(target, c_sv(CLR0));
    if (self->has_type) {
        cstr_append_sv(target, c_sv(CLR_TYPE ": " CLR0));
        cstr_append_type_debug(target, &self->type, indent);
    }
    if (self->has_value) {
        cstr_append_sv(target, c_sv(" = "));
        cstr_append_expr_debug(target, &self->value, indent);
    }
}

Type NameInitData_get_type_and_typecheck(NameInitData* self, ErrorCtx* errs) {
    if (self->has_value) {
        Type expr_type = Expr_get_type_and_typecheck(&self->value, errs);
        if (self->has_type) {
            if (!Type_extends(&expr_type, &self->type)) {
                ErrorCtx_type_error(errs, "invalid variable initialization type");
            }
            Type_drop(&expr_type);
            return Type_clone(self->type);
        } else {
            return expr_type;
        }
    } else {
        return Type_any;
    }
}

void VarInitStatement_drop(VarInitStatement* self) {
    NameInitData_drop(&self->data);
}

void cstr_append_var_init_statement_debug(cstr* target, VarInitStatement* self, int indent) {
    switch (self->keyword) {
        case VAR_INIT_VAR: cstr_append_sv(target, c_sv("var ")); break;
        case VAR_INIT_LET: cstr_append_sv(target, c_sv("let ")); break;
        case VAR_INIT_CONST: cstr_append_sv(target, c_sv("const ")); break;
    }
    cstr_append_name_init_data_debug(target, &self->data, indent);
    cstr_append_sv(target, c_sv(";"));
}

void FunctionStatementArg_drop(FunctionStatementArg* self) {
    NameInitData_drop(&self->data);
}

void FunctionStatementGeneric_drop(FunctionStatementGeneric* self) {
    cstr_drop(&self->name);
    Type_drop(&self->extends);
    Type_drop(&self->fallback);
}

void FunctionStatement_drop(FunctionStatement* self) {
    cstr_drop(&self->name);
    vec_FunctionStatementGeneric_drop(&self->generics);
    vec_FunctionStatementArg_drop(&self->args);
    Type_drop(&self->return_type);
    Scope_drop(&self->body);
}

void cstr_append_function_statement_debug(cstr* target, FunctionStatement* self, int indent) {
    cstr_append_sv(target, c_sv("function " CLR_NAME));
    cstr_append_s(target, self->name);
    cstr_append_sv(target, c_sv(CLR0 "("));
    c_foreach(i, vec_FunctionStatementArg, self->args) {
        cstr_append_name_init_data_debug(target, &i.ref->data, indent);
        if (i.ref < i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(")"));
    if (self->has_return_type) {
        cstr_append_sv(target, c_sv(CLR_TYPE ": " CLR0));
        cstr_append_type_debug(target, &self->return_type, indent);
    }
    cstr_append_sv(target, c_sv(" "));
    cstr_append_scope_debug(target, &self->body, indent);
}

void FunctionStatement_typecheck(FunctionStatement* self, ErrorCtx* errs) {
    c_foreach(i, vec_FunctionStatementArg, self->args) {
        Type tmp = NameInitData_get_type_and_typecheck(&i.ref->data, errs);
        Type_drop(&tmp);
    }
    Scope_typecheck(&self->body, errs);
    // TODO: check that return is compatible with return type
}

void Statement_drop(Statement* self) {
    switch (self->tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_INTERFACE:
            NamedInterface_drop(&self->interface);
            break;

        case STATEMENT_SCOPE:
            Scope_drop(&self->scope);
            break;

        case STATEMENT_EXPR:
            Expr_drop(&self->expr);
            break;

        case STATEMENT_RETURN:
            Expr_drop(&self->return_expr);
            break;

        case STATEMENT_VAR_INIT:
            VarInitStatement_drop(&self->var_init);
            break;

        case STATEMENT_FUNCTION:
            FunctionStatement_drop(&self->function);
            break;

        case STATEMENT_IF_ELSE:
            vec_Expr_drop(&self->if_else.conditions);
            vec_Statement_drop(&self->if_else.statements);
            break;

        case STATEMENT_LOOP:
            Statement_drop(&self->loop->initialization);
            Expr_drop(&self->loop->condition);
            Statement_drop(&self->loop->afterthought);
            Statement_drop(&self->loop->body);
            free(self->loop);
            break;

        case STATEMENT_BREAK: break; // nice
    }
}

void cstr_append_statement_debug(cstr* target, Statement* self, int indent) {
    switch (self->tag) {
        case STATEMENT_NOOP: cstr_append_sv(target, c_sv(";")); break;
        case STATEMENT_INTERFACE: cstr_append_named_interface_debug(target, &self->interface, indent); break;
        case STATEMENT_SCOPE: cstr_append_scope_debug(target, &self->scope, indent); break;

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
        case STATEMENT_FUNCTION: cstr_append_function_statement_debug(target, &self->function, indent); break;

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
            cstr_append_statement_debug(target, &self->loop->afterthought, indent);
            if (cstr_str(target)[cstr_size(target) - 1] != ';') {
                cstr_append_sv(target, c_sv(";"));
            }
            cstr_append_sv(target, c_sv(") "));
            cstr_append_statement_debug(target, &self->loop->body, indent);
            break;

        case STATEMENT_BREAK: cstr_append_sv(target, c_sv("break;")); break;
    }
}

void Statement_typecheck(Statement* self, ErrorCtx* errs) {
    switch (self->tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_INTERFACE: break;

        case STATEMENT_SCOPE:
            Scope_typecheck(&self->scope, errs);
            break;

        case STATEMENT_EXPR: {
            Type t = Expr_get_type_and_typecheck(&self->expr, errs);
            Type_drop(&t);
        }   break;

        case STATEMENT_RETURN: {
            // TODO: typecheck
            Type t = Expr_get_type_and_typecheck(&self->return_expr, errs);
            Type_drop(&t);
        }   break;

        case STATEMENT_VAR_INIT: {
            Type tmp = NameInitData_get_type_and_typecheck(&self->var_init.data, errs);
            Type_drop(&tmp);
        }   break;

        case STATEMENT_FUNCTION:
            // TODO: typecheck args? return type?
            Scope_typecheck(&self->function.body, errs);
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
                Statement_typecheck(i.ref, errs);
            }
            break;
        }

        case STATEMENT_LOOP: {
            Statement_typecheck(&self->loop->initialization, errs);
            Type t = Expr_get_type_and_typecheck(&self->loop->condition, errs);
            Type_drop(&t);
            Statement_typecheck(&self->loop->afterthought, errs);
            Statement_typecheck(&self->loop->body, errs);
            break;
        }

        case STATEMENT_BREAK:
            // TODO: can we check to make sure we're in a loop?
            break;
    }
}
