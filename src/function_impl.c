#include "function_impl.h"
#include "name_stack.h"
#include "callable_type.h"
#include "generic.h"
#include "statement.h"
#include "error.h"

void function_impl_drop(FunctionImpl* me) {
    callable_type_drop(&my callable);
    named_variable_destroy(my named_var);
    vec_expr_drop(&my arg_defaults);
    block_drop(&my body);
}

void cstr_append_function_impl_debug(cstr* target, FunctionImpl* me, bool include_arrow, int indent) {
    cstr_append_generic_debug(target, &my callable.generic, indent);
    cstr_append_sv(target, c_sv("("));
    c_foreach(i, vec_callable_arg, my callable.args) {
        if (i.ref->rest) {
            cstr_append_sv(target, c_sv("..."));
        }
        cstr_append_binding_debug(target, &i.ref->binding, i.ref->explicit_optional, indent);
        intptr_t index = i.ref - my callable.args.data;
        Expr const* default_arg = function_impl_arg_default_at(me, index);
        if (default_arg->tag != EXPR_IMPLICIT_ANY) {
            cstr_append_sv(target, c_sv(" = "));
            cstr_append_expr_debug(target, default_arg, indent);
        }
        if (i.ref != i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(")"));
    if (my callable.ret_explicit) {
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_debug(target, &my callable.ret, indent);
    }
    if (my has_block) {
        cstr_append_sv(target, include_arrow ? c_sv(" => ") : c_sv(" "));
        cstr_append_block_debug(target, &my body, indent);
    } else {
        cstr_append_sv(target, c_sv(";"));
    }
}

void function_impl_add_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs) {
    ASSERT(!my named_var);
    my named_var = make_named_variable(my full_span, lazy_type_function(me), &my callable.generic);
    if (!name_stack_add_variable_s(names, cstr_from_span(&my name), my named_var)) {
        error_ctx_error(errs, my name, TS_CODE_NONE, "multiple values declared with same name");
    }
}

void function_impl_resolve_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs) {
    // Handle generics
    name_stack_push_generic_frame(names, &my callable.generic, errs);

    // First, resolve names in the callable type
    callable_type_resolve_names(&my callable, names, errs);

    // Push a stack frame for arguments to live in
    // This is the function *implementation*, so we do not push a generic stack frame
    name_stack_push_function_frame(names);

    // Loop through all arguments and add them to the namespace
    c_foreach(i, vec_callable_arg, my callable.args) {
        intptr_t index = i.ref - my callable.args.data;
        binding_add_names(
            &i.ref->binding,
            lazy_type_expr(function_impl_arg_default_at(me, index), true),
            names,
            errs
        );
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

Type const* function_impl_resolve_type(FunctionImpl* me) {
    if (my callable.ret_explicit) {
        my ret_resolve_state = RESOLVE_STATE_DONE;
    }

    switch (my ret_resolve_state) {
        case RESOLVE_STATE_UNINITIALIZED:
            type_drop(&my callable.ret);
            my ret_resolve_state = RESOLVE_STATE_IN_FLIGHT;

            StatementCtx ctx = statement_ctx_function(&type_any, true);
            block_typecheck(&my body, &ctx, nullptr);

            if (my ret_resolve_state == RESOLVE_STATE_IN_FLIGHT) {
                if (ctx.impl_return.tag == TYPE_NEVER) {
                    // This will need to be improved
                    my callable.ret = type_void;
                } else {
                    my callable.ret = type_move(&ctx.impl_return);
                    type_abstract_literals(&my callable.ret);
                }
                my ret_resolve_state = RESOLVE_STATE_DONE;
            }

            statement_ctx_drop(&ctx);

            break;

        case RESOLVE_STATE_IN_FLIGHT:
            type_drop(&my callable.ret);
            my callable.ret = type_self_referential_error;
            my ret_resolve_state = RESOLVE_STATE_DONE;
            break;

        case RESOLVE_STATE_DONE: break;
    }

    return callable_type_get_wrapper(&my callable);
}

void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs) {
    bool found_optional = false;
    bool required_after_optional = false;
    bool rest_not_last = false;

    c_foreach(i, vec_callable_arg, my callable.args) {
        intptr_t index = i.ref - my callable.args.data;
        Expr const* default_arg = function_impl_arg_default_at(me, index);

        if (i.ref->rest && i.ref != i.end - 1 && !rest_not_last) {
            rest_not_last = true;
            error_ctx_error(
                errs,
                i.ref->rest_span,
                1014,
                "A rest parameter must be last in a parameter list."
            );
        }

        if (i.ref->rest) {
        } else if (i.ref->explicit_optional) {
            found_optional = true;
        } else if (found_optional && default_arg->tag == EXPR_IMPLICIT_ANY && !required_after_optional) {
            required_after_optional = true;
            error_ctx_error(
                errs,
                i.ref->binding.content_span,
                1016,
                "A required parameter cannot follow an optional parameter."
            );
        }

        if (!i.ref->binding.type_explicit && default_arg->tag == EXPR_IMPLICIT_ANY) {
            // TODO: only run this check in strict mode
            binding_add_implicit_any_type_errors(&i.ref->binding, true, errs);
        } else {
            Type default_arg_type = expr_get_type_and_typecheck(default_arg, errs);
            binding_typecheck(&i.ref->binding, &default_arg_type, errs);
            type_drop(&default_arg_type);
        }

        if (i.ref->rest) {
            if (!type_extends_array(&i.ref->binding.type)) {
                error_ctx_error(
                    errs,
                    i.ref->span,
                    2370,
                    "A rest parameter must be of an array type."
                );
            }
            if (default_arg->tag != EXPR_IMPLICIT_ANY) {
                error_ctx_error(
                    errs,
                    i.ref->binding.content_span,
                    1048,
                    "A rest parameter cannot have an initializer."
                );
            }
        }
    }

    if (my newline_before_arrow) {
        error_ctx_error(errs, my arrow_span, 1200, "Line terminator not permitted before arrow.");
    }

    // PERF: if !my ret_type_resolved, only typecheck the block once
    function_impl_resolve_type(me);

    StatementCtx ctx = statement_ctx_function(my callable.ret_explicit ? &my callable.ret : &type_any, false);
    block_typecheck(&my body, &ctx, errs);
    statement_ctx_drop(&ctx);
}
