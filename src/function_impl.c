#include "function_impl.h"
#include "name_stack.h"
#include "callable_type.h"
#include "generic.h"

void FunctionArg_drop(FunctionArg* self) {
    cstr_drop(&self->name);
    Expr_drop(&self->value);
}

void FunctionImpl_drop(FunctionImpl* self) {
    Type_drop(&self->type);
    vec_FunctionArg_drop(&self->args);
    Block_drop(&self->body);
}

CallableType* FunctionImpl_callable_type(FunctionImpl* self) {
    if (self->type.tag == TYPE_CALLABLE) {
        return self->type.callable;
    } else if (self->type.tag == TYPE_GENERIC && self->type.generic->inner.tag == TYPE_CALLABLE) {
        return self->type.generic->inner.callable;
    } else {
        PANIC("FunctionImpl has invald type type");
    }
}

Generic* FunctionImpl_generic_or_null(FunctionImpl* self) {
    if (self->type.tag == TYPE_GENERIC) {
        return self->type.generic;
    } else {
        return nullptr;
    }
}

long FunctionImpl_arg_count(FunctionImpl* self) {
    int arg_count = vec_CallableTypeArg_size(&FunctionImpl_callable_type(self)->args);
    if (vec_FunctionArg_size(&self->args) != arg_count) {
        PANIC("function type has different number of args than function");
    }
    return arg_count;
}

void cstr_append_function_impl_debug(cstr* target, FunctionImpl* self, bool include_arrow, int indent) {
    Generic* generic = FunctionImpl_generic_or_null(self);
    CallableType* callable = FunctionImpl_callable_type(self);
    if (generic) {
        cstr_append_vec_generic_item_debug(target, &generic->items, indent);
    }
    cstr_append_sv(target, c_sv("("));
    int arg_count = FunctionImpl_arg_count(self);
    for (int i = 0; i < arg_count; i++) {
        FunctionArg* value_arg = vec_FunctionArg_at_mut(&self->args, i);
        CallableTypeArg* type_arg = vec_CallableTypeArg_at_mut(&callable->args, i);
        cstr_append_sv(target, c_sv(CLR_NAME));
        cstr_append_s(target, value_arg->name);
        cstr_append_sv(target, c_sv(CLR0));
        if (type_arg->type_explicit) {
            cstr_append_sv(target, c_sv(": "));
            cstr_append_type_debug(target, &type_arg->type, indent);
        }
        if (value_arg->value_explicit) {
            cstr_append_sv(target, c_sv(" = "));
            cstr_append_expr_debug(target, &value_arg->value, indent);
        }
        if (i < arg_count - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(")"));
    if (callable->ret_explicit) {
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_debug(target, &callable->ret, indent);
    }
    cstr_append_sv(target, include_arrow ? c_sv(" => ") : c_sv(" "));
    cstr_append_block_debug(target, &self->body, indent);
}

void FunctionImpl_resolve_names(FunctionImpl* self, NameStack* names, ErrorCtx* errs) {
    // First, resolve names in the callable type
    Type_resolve_names(&self->type, names, errs);

    // Push a stack frame for generics/arguments to live in
    // This is the function *implementation*, so we do not push a generic stack frame
    NameStack_push_frame(names, false);

    // Loop through generics and add them to the namespace
    Generic* generic = FunctionImpl_generic_or_null(self);
    if (generic) {
        c_foreach(i, vec_GenericItem, generic->items) {
            // This is the function *implementation*, so we treat generic paramaters as regular types
            if (!NameStack_add_type(names, cstr_clone(i.ref->name), &i.ref->extends, false, 0)) {
                // name is cstr_str(&result.ref->first)
                ErrorCtx_type_error(errs, "multiple function generics with same name");
            }
        }
    }

    // Loop through all arguments and add them to the namespace
    int arg_count = FunctionImpl_arg_count(self);
    CallableType* callable = FunctionImpl_callable_type(self);
    for (int i = 0; i < arg_count; i++) {
        if (!NameStack_add_variable(
            names,
            cstr_clone(vec_FunctionArg_at_mut(&self->args, i)->name),
            &vec_CallableTypeArg_at_mut(&callable->args, i)->type
        )) {
            // name is cstr_str(&result.ref->first)
            ErrorCtx_type_error(errs, "multiple function arguments with same name");
        }
    }

    // Loop through all arguments and resolve names in the default value expressions
    c_foreach(i, vec_FunctionArg, self->args) {
        Expr_resolve_names(&i.ref->value, names, errs);
    }

    // Resolve names in the function body, don't push a stack frame since the toplevel function body should be the same
    // namespace as the variables are in.
    Block_resolve_names_without_stack_frame(&self->body, names, errs);

    // Pop the stack frame we pushed above
    NameStack_pop_frame(names);
}

void FunctionImpl_typecheck(FunctionImpl* self, ErrorCtx* errs) {
    CallableType* callable = FunctionImpl_callable_type(self);
    int arg_count = FunctionImpl_arg_count(self);
    for (int i = 0; i < arg_count; i++) {
        FunctionArg* value_arg = vec_FunctionArg_at_mut(&self->args, i);
        CallableTypeArg* type_arg = vec_CallableTypeArg_at_mut(&callable->args, i);
        if (value_arg->value_explicit && type_arg->type_explicit) {
            Type expr_type = Expr_get_type_and_typecheck(&value_arg->value, errs);
            if (!Type_extends(&expr_type, &type_arg->type)) {
                ErrorCtx_type_error(errs, "invalid argument initialization type");
            }
            Type_drop(&expr_type);
        }
    }
    Block_typecheck(&self->body, errs);
    // TODO: check that return is compatible with return type
}
