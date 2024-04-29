#include "function_impl.h"
#include "name_stack.h"
#include "callable_type.h"
#include "generic.h"
#include "statement.h"
#include "error.h"

void function_impl_drop(FunctionImpl* me) {
    cstr_drop(&my name);
    callable_type_drop(&my type);
    vec_expr_drop(&my arg_defaults);
    block_drop(&my body);
}

void cstr_append_function_impl_debug(cstr* target, FunctionImpl* me, bool include_arrow, int indent) {
    cstr_append_generic_debug(target, &my type.generic, indent);
    cstr_append_sv(target, c_sv("("));
    intptr_t arg_size = vec_binding_size(&my type.args);
    for (intptr_t i = 0; i < arg_size; i++) {
        if (my type.variadic && i == arg_size - 1) {
            cstr_append_sv(target, c_sv("..."));
        }
        cstr_append_binding_debug(target, vec_binding_at(&my type.args, i), indent);
        Expr const* default_arg = function_impl_arg_default_at(me, i);
        if (default_arg->tag != EXPR_IMPLICIT_ANY) {
            cstr_append_sv(target, c_sv(" = "));
            cstr_append_expr_debug(target, default_arg, indent);
        }
        if (i < arg_size - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(")"));
    if (my type.ret_explicit) {
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_debug(target, &my type.ret, indent);
    }
    if (my has_block) {
        cstr_append_sv(target, include_arrow ? c_sv(" => ") : c_sv(" "));
        cstr_append_block_debug(target, &my body, indent);
    } else {
        cstr_append_sv(target, c_sv(";"));
    }
}

void function_impl_resolve_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs) {
    // Handle generics
    name_stack_push_generic_frame(names, &my type.generic, errs);

    // First, resolve names in the callable type
    callable_type_resolve_names(&my type, names, errs);

    // Push a stack frame for arguments to live in
    // This is the function *implementation*, so we do not push a generic stack frame
    name_stack_push_function_frame(names);

    // Loop through all arguments and add them to the namespace
    c_foreach(i, vec_binding, my type.args) {
        binding_add_names(i.ref, names, errs);
    }

    // Loop through all arguments and resolve names
    c_foreach(i, vec_binding, my type.args) {
        binding_resolve_names(i.ref, names, errs);
    }

    // Loop through all argument defaults and resolve names
    c_foreach(i, vec_expr, my arg_defaults) {
        expr_resolve_names(i.ref, names, errs);
    }

    // Resolve names in the function body, don't push a stack frame since the toplevel function body should be the same
    // namespace as the arguments
    block_resolve_names_without_stack_frame(&my body, names, errs);

    // Pop the stack frame we pushed above
    name_stack_pop_frame(names);

    // Pop the generics frame
    name_stack_pop_frame(names);
}

void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs) {
    intptr_t arg_size = vec_binding_size(&my type.args);
    for (intptr_t i = 0; i < arg_size; i++) {
        Binding* binding = vec_binding_at_mut(&my type.args, i);
        Expr const* default_arg = function_impl_arg_default_at(me, i);
        if (!binding->type_explicit && default_arg->tag == EXPR_IMPLICIT_ANY) {
            // TODO: only run this check in strict mode
            binding_add_implicit_any_type_errors(binding, true, errs);
        } else {
            binding_set_assigned_type(binding, expr_get_type_and_typecheck(default_arg, errs), true, errs);
        }
    }

    if (my newline_before_arrow) {
        error_ctx_error(errs, my arrow_span, 1200, "Line terminator not permitted before arrow.");
    }

    StatementCtx ctx = statement_ctx_function(&my type.ret);

    block_typecheck(&my body, &ctx, errs);

    if (!my type.ret_explicit) {
        int type_count = vec_type_size(&ctx.returned_types);
        type_drop(&my type.ret);
        if (type_count == 0) {
            my type.ret = type_void;
        } else if (type_count == 1) {
            my type.ret = vec_type_pull(&ctx.returned_types);
            type_abstract_literals(&my type.ret);
        } else {
            error_ctx_error(errs, span_null, TS_CODE_NONE, "Multiple function return types not yet implemented.");
        }
    }

    statement_ctx_drop(&ctx);
}
