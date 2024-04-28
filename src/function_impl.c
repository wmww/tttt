#include "function_impl.h"
#include "name_stack.h"
#include "callable_type.h"
#include "generic.h"
#include "statement.h"
#include "error.h"

void function_impl_drop(FunctionImpl* me) {
    cstr_drop(&my name);
    callable_type_drop(&my type);
    vec_binding_drop(&my args);
    block_drop(&my body);
}

intptr_t function_impl_arg_count(FunctionImpl* me) {
    intptr_t arg_count = vec_callable_type_arg_size(&my type.args);
    if (vec_binding_size(&my args) != arg_count) {
        PANIC("function type has different number of args than function");
    }
    return arg_count;
}

void cstr_append_function_impl_debug(cstr* target, FunctionImpl* me, bool include_arrow, int indent) {
    cstr_append_generic_debug(target, &my type.generic, indent);
    cstr_append_sv(target, c_sv("("));
    c_foreach(i, vec_binding, my args) {
        cstr_append_binding_debug(target, i.ref, indent);
        if (i.ref < i.end - 1) {
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
    c_foreach(i, vec_binding, my args) {
        binding_add_names(i.ref, names, errs);
    }

    // Loop through all arguments and resolve names
    c_foreach(i, vec_binding, my args) {
        binding_resolve_names(i.ref, names, errs);
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
    c_foreach(i, vec_binding, my args) {
        binding_typecheck(i.ref, true, errs);
        // TODO: only run this check in strict mode
        if (!i.ref->type_explicit) {
            binding_add_implicit_any_type_errors(i.ref, true, errs);
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
