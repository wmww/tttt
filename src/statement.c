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

        case STATEMENT_VAR_INIT:
            VarInitStatement_drop(&self->data.var_init);
            break;

        case STATEMENT_FUNCTION:
            FunctionStatement_drop(&self->data.function);
            break;
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

        case STATEMENT_VAR_INIT:
            return VarInitStatement_to_cstr(&self->data.var_init, indent);

        case STATEMENT_FUNCTION:
            return FunctionStatement_to_cstr(&self->data.function, indent);
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

        case STATEMENT_VAR_INIT: {
            Type tmp = NameInitData_get_type_and_typecheck(&self->data.var_init.data, errs);
            Type_drop(&tmp);
        }   break;

        case STATEMENT_FUNCTION:
            // TODO: typecheck args? return type?
            Scope_typecheck(&self->data.function.body, errs);
            break;
    }
}
