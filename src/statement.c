#include "statement.h"
#include "error.h"

void VarInit_drop(VarInit* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
    Expr_drop(&self->value);
}

cstr VarInit_to_cstr(VarInit* self, int indent) {
    cstr ret = cstr_null;
    switch (self->keyword) {
        case VAR_INIT_VAR: ret = cstr_lit("var "); break;
        case VAR_INIT_LET: ret = cstr_lit("let "); break;
        case VAR_INIT_CONST: ret = cstr_lit("const "); break;
    }
    cstr_append(&ret, CLR_NAME);
    cstr_append_s(&ret, self->name);
    cstr_append(&ret, CLR0);
    if (self->has_type) {
        cstr_append(&ret, ": ");
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
    cstr_append(&ret, ";");
    return ret;
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
            VarInit_drop(&self->data.var_init);
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
            return VarInit_to_cstr(&self->data.var_init, indent);
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

        case STATEMENT_VAR_INIT:
            if (self->data.var_init.has_value) {
                Type expr_type = Expr_get_type_and_typecheck(&self->data.var_init.value, errs);
                if (self->data.var_init.has_type) {
                    if (!Type_extends(&expr_type, &self->data.var_init.type)) {
                        ErrorCtx_type_error(errs, "invalid variable initialization type");
                    }
                }
                Type_drop(&expr_type);
            }
            break;
    }
}
