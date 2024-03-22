#include "function_impl.h"
#include "name_stack.h"
#include "callable_type.h"
#include "generic.h"
#include "statement.h"

void function_arg_drop(FunctionArg* me) {
    cstr_drop(&my name);
    expr_drop(&my value);
}

void function_impl_drop(FunctionImpl* me) {
    type_drop(&my type);
    vec_function_arg_drop(&my args);
    block_drop(&my body);
}

CallableType* function_impl_callable_type(FunctionImpl* me) {
    if (my type.tag == 0) {
        my type = type_callable(c_new(CallableType, {0}));
        return my type.callable;
    } else {
        CallableType* callable = type_get_callable_type(&my type);
        if (!callable) {
            PANIC("FunctionImpl has invald type type %d", my type.tag);
        }
        return callable;
    }
}

Generic* function_impl_generic_or_null(FunctionImpl* me) {
    if (my type.tag == TYPE_GENERIC) {
        return my type.generic;
    } else {
        return nullptr;
    }
}

intptr_t function_impl_arg_count(FunctionImpl* me) {
    intptr_t arg_count = vec_callable_type_arg_size(&function_impl_callable_type(me)->args);
    if (vec_function_arg_size(&my args) != arg_count) {
        PANIC("function type has different number of args than function");
    }
    return arg_count;
}

void cstr_append_function_impl_debug(cstr* target, FunctionImpl* me, bool include_arrow, int indent) {
    Generic* generic = function_impl_generic_or_null(me);
    CallableType* callable = function_impl_callable_type(me);
    if (generic) {
        cstr_append_vec_generic_item_debug(target, &generic->items, indent);
    }
    cstr_append_sv(target, c_sv("("));
    int arg_count = function_impl_arg_count(me);
    for (int i = 0; i < arg_count; i++) {
        FunctionArg* value_arg = vec_function_arg_at_mut(&my args, i);
        CallableTypeArg* type_arg = vec_callable_type_arg_at_mut(&callable->args, i);
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
    cstr_append_block_debug(target, &my body, indent);
}

void function_impl_resolve_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs) {
    // First, resolve names in the callable type
    type_resolve_names(&my type, names, errs);

    // Push a stack frame for generics/arguments to live in
    // This is the function *implementation*, so we do not push a generic stack frame
    name_stack_push_frame(names, STACK_FRAME_BLOCK);

    // Loop through generics and add them to the namespace
    Generic* generic = function_impl_generic_or_null(me);
    if (generic) {
        c_foreach(i, vec_generic_item, generic->items) {
            // This is the function *implementation*, so we treat generic paramaters as regular types
            if (!name_stack_add_type(names, cstr_clone(i.ref->name), &i.ref->extends)) {
                // name is cstr_str(&result.ref->first)
                error_ctx_type_error(errs, "multiple function generics with same name");
            }
        }
    }

    // Loop through all arguments and add them to the namespace
    int arg_count = function_impl_arg_count(me);
    CallableType* callable = function_impl_callable_type(me);
    for (int i = 0; i < arg_count; i++) {
        if (!name_stack_add_variable(
            names,
            cstr_clone(vec_function_arg_at_mut(&my args, i)->name),
            &vec_callable_type_arg_at_mut(&callable->args, i)->type
        )) {
            // name is cstr_str(&result.ref->first)
            error_ctx_type_error(errs, "multiple function arguments with same name");
        }
    }

    // Loop through all arguments and resolve names in the default value expressions
    c_foreach(i, vec_function_arg, my args) {
        expr_resolve_names(&i.ref->value, names, errs);
    }

    // Resolve names in the function body, don't push a stack frame since the toplevel function body should be the same
    // namespace as the variables are in.
    block_resolve_names_without_stack_frame(&my body, names, errs);

    // Pop the stack frame we pushed above
    name_stack_pop_frame(names);
}

void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs) {
    CallableType* callable = function_impl_callable_type(me);

    int arg_count = function_impl_arg_count(me);
    for (int i = 0; i < arg_count; i++) {
        FunctionArg* value_arg = vec_function_arg_at_mut(&my args, i);
        CallableTypeArg* type_arg = vec_callable_type_arg_at_mut(&callable->args, i);
        if (value_arg->value_explicit && type_arg->type_explicit) {
            Type expr_type = expr_get_type_and_typecheck(&value_arg->value, errs);
            check_assignable_or_error(
                &expr_type,
                &type_arg->type,
                span_between(&value_arg->name_span, &value_arg->value.span),
                errs
            );
            type_drop(&expr_type);
        }
        // TODO: only run this check in strict mode
        if (!type_arg->type_explicit) {
            error_ctx_error(
                errs,
                value_arg->name_span,
                7006,
                "Parameter '%s' implicitly has an 'any' type.",
                cstr_str(&value_arg->name)
            );
        }
    }

    if (my newline_before_arrow) {
        error_ctx_error(errs, my arrow_span, 1200, "Line terminator not permitted before arrow.");
    }

    StatementCtx ctx = statement_ctx_function(&callable->ret);

    block_typecheck(&my body, &ctx, errs);

    if (!callable->ret_explicit) {
        int type_count = vec_type_size(&ctx.returned_types);
        type_drop(&callable->ret);
        if (type_count == 0) {
            callable->ret = type_void;
        } else if (type_count == 1) {
            callable->ret = vec_type_pull(&ctx.returned_types);
            type_abstract_literals(&callable->ret);
        } else {
            error_ctx_error(errs, span_null, TS_CODE_NONE, "Multiple function return types not yet implemented.");
        }
    }

    statement_ctx_drop(&ctx);
}
