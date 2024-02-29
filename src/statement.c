#include "statement.h"
#include "error.h"

void NameInitData_drop(NameInitData* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
    Expr_drop(&self->value);
}

cstr NameInitData_to_cstr(NameInitData* self, int indent) {
    cstr ret = cstr_lit(CLR_NAME);
    cstr_append_s(&ret, self->name);
    cstr_append(&ret, CLR0);
    if (self->has_type) {
        cstr_append(&ret, CLR_TYPE ": " CLR0);
        cstr type = Type_to_cstr(&self->type, indent);
        cstr_append_s(&ret, type);
        cstr_drop(&type);
    }
    if (self->has_value) {
        cstr_append(&ret, " = ");
        cstr value = Expr_to_cstr(&self->value, indent);
        cstr_append_s(&ret, value);
        cstr_drop(&value);
    }
    return ret;
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

cstr VarInitStatement_to_cstr(VarInitStatement* self, int indent) {
    cstr ret = cstr_null;
    switch (self->keyword) {
        case VAR_INIT_VAR: ret = cstr_lit("var "); break;
        case VAR_INIT_LET: ret = cstr_lit("let "); break;
        case VAR_INIT_CONST: ret = cstr_lit("const "); break;
    }
    cstr data_str = NameInitData_to_cstr(&self->data, indent);
    cstr_append_s(&ret, data_str);
    cstr_drop(&data_str);
    cstr_append(&ret, ";");
    return ret;
}

void FunctionStatementArg_drop(FunctionStatementArg* self) {
    NameInitData_drop(&self->data);
}

void FunctionStatement_drop(FunctionStatement* self) {
    cstr_drop(&self->name);
    vec_FunctionStatementArg_drop(&self->args);
    Type_drop(&self->return_type);
    Scope_drop(&self->body);
}

cstr FunctionStatement_to_cstr(FunctionStatement* self, int indent) {
    cstr ret = cstr_lit("function " CLR_NAME);
    cstr_append_s(&ret, self->name);
    cstr_append(&ret, CLR0"(");
    c_foreach(i, vec_FunctionStatementArg, self->args) {
        cstr data_str = NameInitData_to_cstr(&i.ref->data, indent);
        cstr_append_s(&ret, data_str);
        cstr_drop(&data_str);
        if (i.ref < i.end - 1) {
            cstr_append(&ret, ", ");
        }
    }
    cstr_append(&ret, ")");
    if (self->has_return_type) {
        cstr_append(&ret, CLR_TYPE ": " CLR0);
        cstr return_type_str = Type_to_cstr(&self->return_type, indent);
        cstr_append_s(&ret, return_type_str);
        cstr_drop(&return_type_str);
    }
    cstr_append(&ret, " ");
    cstr scope_str = Scope_to_cstr(&self->body, indent);
    cstr_append_s(&ret, scope_str);
    cstr_drop(&scope_str);
    return ret;
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
        case STATEMENT_INTERFACE:
            NamedInterface_drop(&self->data.interface);
            break;

        case STATEMENT_SCOPE:
            Scope_drop(&self->data.scope);
            break;

        case STATEMENT_EXPR:
            Expr_drop(&self->data.expr);
            break;

        case STATEMENT_RETURN:
            Expr_drop(&self->data.return_expr);
            break;

        case STATEMENT_VAR_INIT:
            VarInitStatement_drop(&self->data.var_init);
            break;

        case STATEMENT_FUNCTION:
            FunctionStatement_drop(&self->data.function);
            break;

        case STATEMENT_IF_ELSE:
            vec_Expr_drop(&self->data.if_else.conditions);
            vec_Statement_drop(&self->data.if_else.statements);
            break;

        case STATEMENT_LOOP:
            Scope_drop(&self->data.loop.scope);
            break;

        case STATEMENT_BREAK: break; // nice
    }
}

cstr Statement_to_cstr(Statement* self, int indent) {
    switch (self->tag) {
        case STATEMENT_INTERFACE:
            return NamedInterface_to_cstr(&self->data.interface, indent);

        case STATEMENT_SCOPE:
            return Scope_to_cstr(&self->data.scope, indent);

        case STATEMENT_EXPR: {
            cstr ret = Expr_to_cstr(&self->data.expr, indent);
            cstr_append(&ret, ";");
            return ret;
        }

        case STATEMENT_RETURN: {
            cstr ret = cstr_lit("return ");
            cstr expr = Expr_to_cstr(&self->data.return_expr, indent);
            cstr_append_s(&ret, expr);
            cstr_drop(&expr);
            cstr_append(&ret, ";");
            return ret;
        }

        case STATEMENT_VAR_INIT:
            return VarInitStatement_to_cstr(&self->data.var_init, indent);

        case STATEMENT_FUNCTION:
            return FunctionStatement_to_cstr(&self->data.function, indent);

        case STATEMENT_IF_ELSE: {
            cstr ret = cstr_null;
            int statement_size = vec_Statement_size(&self->data.if_else.statements);
            int cond_size = vec_Expr_size(&self->data.if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    cstr_append(&ret, "if (");
                    cstr expr = Expr_to_cstr(vec_Expr_at(&self->data.if_else.conditions, i), indent);
                    cstr_append_s(&ret, expr);
                    cstr_drop(&expr);
                    cstr_append(&ret, ") ");
                }
                cstr stmt = Statement_to_cstr(vec_Statement_at_mut(&self->data.if_else.statements, i), indent);
                cstr_append_s(&ret, stmt);
                cstr_drop(&stmt);
                if (i < statement_size - 1) {
                    cstr_append(&ret, " else ");
                }
            }
            return ret;
        }

        case STATEMENT_LOOP: {
            cstr ret = cstr_lit("while (true) ");
            cstr scope = Scope_to_cstr(&self->data.loop.scope, indent);
            cstr_append_s(&ret, scope);
            cstr_drop(&scope);
            return ret;
        }

        case STATEMENT_BREAK: return cstr_lit("break;");
    }
    UNREACHABLE;
}

void Statement_typecheck(Statement* self, ErrorCtx* errs) {
    switch (self->tag) {
        case STATEMENT_INTERFACE: break;

        case STATEMENT_SCOPE:
            Scope_typecheck(&self->data.scope, errs);
            break;

        case STATEMENT_EXPR: {
            Type t = Expr_get_type_and_typecheck(&self->data.expr, errs);
            Type_drop(&t);
        }   break;

        case STATEMENT_RETURN: {
            // TODO: typecheck
            Type t = Expr_get_type_and_typecheck(&self->data.return_expr, errs);
            Type_drop(&t);
        }   break;

        case STATEMENT_VAR_INIT: {
            Type tmp = NameInitData_get_type_and_typecheck(&self->data.var_init.data, errs);
            Type_drop(&tmp);
        }   break;

        case STATEMENT_FUNCTION:
            // TODO: typecheck args? return type?
            Scope_typecheck(&self->data.function.body, errs);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_Statement_size(&self->data.if_else.statements);
            int cond_size = vec_Expr_size(&self->data.if_else.conditions);
            if (statement_size != cond_size && statement_size != cond_size + 1) {
                PANIC("invalid if statement has %d conditions and %d statements", cond_size, statement_size);
            }
            c_foreach(i, vec_Expr, self->data.if_else.conditions) {
                Type t = Expr_get_type_and_typecheck(i.ref, errs);
                // Conditions do not have to be booleans
                Type_drop(&t);
            }
            c_foreach(i, vec_Statement, self->data.if_else.statements) {
                Statement_typecheck(i.ref, errs);
            }
            break;
        }

        case STATEMENT_LOOP: {
            Scope_typecheck(&self->data.loop.scope, errs);
            break;
        }

        case STATEMENT_BREAK:
            // TODO: can we check to make sure we're in a loop?
            break;
    }
}
