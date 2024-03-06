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

void VarInitStatement_typecheck(VarInitStatement* self, ErrorCtx* errs) {
    if (self->data.value_explicit) {
        Type expr_type = Expr_get_type_and_typecheck(&self->data.value, errs);
        if (self->data.type_explicit && !Type_extends(&expr_type, &self->data.type)) {
            ErrorCtx_type_error(errs, "invalid variable initialization type");
        } else {
            Type_drop(&self->data.type);
            self->data.type = Type_clone(expr_type);
        }
        Type_drop(&expr_type);
    }
}

void FunctionStatementArg_drop(FunctionStatementArg* self) {
    cstr_drop(&self->name);
    Expr_drop(&self->value);
}

void FunctionStatement_drop(FunctionStatement* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
    vec_FunctionStatementArg_drop(&self->args);
    Block_drop(&self->body);
}

long FunctionStatement_arg_count(FunctionStatement* self) {
    if (self->type.tag != TYPE_CALLABLE) {
        PANIC("function type is not callable");
    }
    int arg_count = vec_CallableTypeArg_size(&self->type.callable->args);
    if (vec_FunctionStatementArg_size(&self->args) != arg_count) {
        PANIC("function type has different number of args than function");
    }
    return arg_count;
}

void cstr_append_function_statement_debug(cstr* target, FunctionStatement* self, int indent) {
    int arg_count = FunctionStatement_arg_count(self);
    cstr_append_sv(target, c_sv("function " CLR_NAME));
    cstr_append_s(target, self->name);
    cstr_append_sv(target, c_sv(CLR0));
    cstr_append_vec_callable_type_generic(target, &self->type.callable->generics, indent);
    cstr_append_sv(target, c_sv("("));
    for (int i = 0; i < arg_count; i++) {
        FunctionStatementArg* statement_arg = vec_FunctionStatementArg_at_mut(&self->args, i);
        CallableTypeArg* type_arg = vec_CallableTypeArg_at_mut(&self->type.callable->args, i);
        cstr_append_sv(target, c_sv(CLR_NAME));
        cstr_append_s(target, statement_arg->name);
        cstr_append_sv(target, c_sv(CLR0));
        if (type_arg->type_explicit) {
            cstr_append_sv(target, c_sv(": "));
            cstr_append_type_debug(target, &type_arg->type, indent);
        }
        if (statement_arg->value_explicit) {
            cstr_append_sv(target, c_sv(" = "));
            cstr_append_expr_debug(target, &statement_arg->value, indent);
        }
        if (i < arg_count - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(")"));
    if (self->type.callable->ret_explicit) {
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_debug(target, &self->type.callable->ret, indent);
    }
    cstr_append_sv(target, c_sv(" "));
    cstr_append_block_debug(target, &self->body, indent);
}

void FunctionStatement_resolve_names(FunctionStatement* self, NameStack* names, ErrorCtx* errs) {
    // First, resolve names in the callable type
    Type_resolve_names(&self->type, names, errs);

    // Push a stack frame for generics/arguments to live in
    NameStack_push_frame(names);

    // Loop through generics and add them to the namespace
    c_foreach(i, vec_CallableTypeGeneric, self->type.callable->generics) {
        hmap_str_CTypePtr_result result = hmap_str_CTypePtr_insert(
            &vec_NameStackFrame_back(&names->stack)->types,
            cstr_clone(i.ref->name),
            &i.ref->extends
        );
        if (!result.inserted) {
            // name is cstr_str(&result.ref->first)
            ErrorCtx_type_error(errs, "multiple function generics with same name");
        }
    }

    // Loop through all arguments and add them to the namespace
    int arg_count = FunctionStatement_arg_count(self);
    for (int i = 0; i < arg_count; i++) {
        FunctionStatementArg* statement_arg = vec_FunctionStatementArg_at_mut(&self->args, i);
        CallableTypeArg* type_arg = vec_CallableTypeArg_at_mut(&self->type.callable->args, i);
        hmap_str_CTypePtr_result result = hmap_str_CTypePtr_insert(
            &vec_NameStackFrame_back(&names->stack)->values,
            cstr_clone(statement_arg->name),
            &type_arg->type
        );
        if (!result.inserted) {
            // name is cstr_str(&result.ref->first)
            ErrorCtx_type_error(errs, "multiple function arguments with same name");
        }
    }

    // Loop through all arguments and resolve names in the default value expressions
    c_foreach(i, vec_FunctionStatementArg, self->args) {
        Expr_resolve_names(&i.ref->value, names, errs);
    }

    // Resolve names in the function body, don't push a stack frame since the toplevel function body should be the same
    // namespace as the variables are in.
    Block_resolve_names_without_stack_frame(&self->body, names, errs);

    // Pop the stack frame we pushed above
    NameStack_pop_frame(names);
}

void FunctionStatement_typecheck(FunctionStatement* self, ErrorCtx* errs) {
    int arg_count = FunctionStatement_arg_count(self);
    for (int i = 0; i < arg_count; i++) {
        FunctionStatementArg* statement_arg = vec_FunctionStatementArg_at_mut(&self->args, i);
        CallableTypeArg* type_arg = vec_CallableTypeArg_at_mut(&self->type.callable->args, i);
        if (statement_arg->value_explicit && type_arg->type_explicit) {
            Type expr_type = Expr_get_type_and_typecheck(&statement_arg->value, errs);
            if (!Type_extends(&expr_type, &type_arg->type)) {
                ErrorCtx_type_error(errs, "invalid argument initialization type");
            }
            Type_drop(&expr_type);
        }
    }
    Block_typecheck(&self->body, errs);
    // TODO: check that return is compatible with return type
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
            FunctionStatement_drop(&self->function);
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
            cstr_append_expr_debug(target, &self->loop->afterthought, indent);
            cstr_append_sv(target, c_sv(") "));
            cstr_append_statement_debug(target, &self->loop->body, indent);
            break;

        case STATEMENT_BREAK: cstr_append_sv(target, c_sv("break;")); break;
    }
}

void Statement_add_names(Statement* self, NameStack* names, ErrorCtx* errs) {
    switch (self->tag) {
        case STATEMENT_VAR_INIT: {
            hmap_str_CTypePtr_result result = hmap_str_CTypePtr_insert(
                &vec_NameStackFrame_back(&names->stack)->values,
                cstr_clone(self->var_init.data.name),
                &self->var_init.data.type
            );
            if (!result.inserted) {
                // name is cstr_str(&result.ref->first)
                ErrorCtx_type_error(errs, "multiple values declared with same name");
            }
        }   break;

        case STATEMENT_FUNCTION: {
            hmap_str_CTypePtr_result result = hmap_str_CTypePtr_insert(
                &vec_NameStackFrame_back(&names->stack)->values,
                cstr_clone(self->function.name),
                &self->function.type
            );
            if (!result.inserted) {
                // name is cstr_str(&result.ref->first)
                ErrorCtx_type_error(errs, "multiple values declared with same name");
            }
        }   break;

        case STATEMENT_INTERFACE: {
            hmap_str_CTypePtr_result result = hmap_str_CTypePtr_insert(
                &vec_NameStackFrame_back(&names->stack)->types,
                cstr_clone(self->interface.name),
                &self->interface.type
            );
            if (!result.inserted) {
                // name is cstr_str(&result.ref->first)
                ErrorCtx_type_error(errs, "multiple values declared with same name");
            }
        }   break;

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
            Type_resolve_names(&self->var_init.data.type, names, errs);
            Expr_resolve_names(&self->var_init.data.value, names, errs);
            break;

        case STATEMENT_FUNCTION:
            FunctionStatement_resolve_names(&self->function, names, errs);
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
            NameStack_push_frame(names);
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

void Statement_typecheck(Statement* self, ErrorCtx* errs) {
    switch (self->tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_INTERFACE: break;

        case STATEMENT_BLOCK:
            Block_typecheck(&self->block, errs);
            break;

        case STATEMENT_EXPR: {
            Type t = Expr_get_type_and_typecheck(&self->expr, errs);
            Type_drop(&t);
        }   break;

        case STATEMENT_RETURN: {
            // TODO: typecheck
            Type t = Expr_get_type_and_typecheck(&self->expr, errs);
            Type_drop(&t);
        }   break;

        case STATEMENT_VAR_INIT:
            VarInitStatement_typecheck(&self->var_init, errs);
            break;

        case STATEMENT_FUNCTION:
            FunctionStatement_typecheck(&self->function, errs);
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
            t = Expr_get_type_and_typecheck(&self->loop->afterthought, errs);
            Type_drop(&t);
            Statement_typecheck(&self->loop->body, errs);
            break;
        }

        case STATEMENT_BREAK:
            // TODO: can we check to make sure we're in a loop?
            break;
    }
}
