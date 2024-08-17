#include "callable_type.h"
#include "generic.h"
#include "binding.h"
#include "name_stack.h"
#include "function_impl.h"
#include "error.h"

void callable_arg_drop(CallableArg* me) {
    binding_drop(&my binding);
}

CallableArg callable_arg_clone(CallableArg me) {
    return (CallableArg){
        .binding = binding_clone(me.binding),
        .optional = me.optional,
        .explicit_optional = me.explicit_optional,
        .rest = me.rest,
    };
}

void callable_type_drop(CallableType* me) {
    generic_drop(&my generic);
    vec_callable_arg_drop(&my args);
    type_drop(&my ret);
}

CallableType callable_type_clone(CallableType me) {
    return (CallableType){
        .generic = generic_clone(me.generic),
        .args = vec_callable_arg_clone(me.args),
        .ret_explicit = me.ret_explicit,
        .ret = type_clone(me.ret),
        .is_constructor = me.is_constructor,
        .is_abstract_constructor = me.is_abstract_constructor,
    };
}

void callable_type_resolve_names(CallableType* me, FunctionImpl const* optional_func, NameStack* names, ErrorCtx* errs) {
    // Handle generics (includes adding names)
    name_stack_push_generic_frame(names, &my generic);

    // Push a stack frame for arguments to live in
    // This is the function *implementation*, so we do not push a generic stack frame
    name_stack_push_function_frame(names);

    // Loop through all arguments and add them to the namespace
    c_foreach(i, vec_callable_arg, my args) {
        intptr_t index = i.ref - my args.data;
        Expr const* default_arg = optional_func ?
            function_impl_arg_default_at(optional_func, index) :
            &expr_implicit_any_no_span;
        LazyType assigned = (default_arg->tag == EXPR_IMPLICIT_ANY && i.ref->binding.has_explicit_type) ?
            lazy_type_value(type_never) :
            lazy_type_expr(default_arg, true, true);
        binding_add_names(&i.ref->binding, assigned, names, errs);
        if (!i.ref->binding.has_explicit_type && (!default_arg || default_arg->tag == EXPR_IMPLICIT_ANY)) {
            if (errs && errs->conf.no_implicit_any) {
                binding_add_implicit_any_type_errors(&i.ref->binding, true, errs);
            }
        }
    }

    c_foreach(i, vec_callable_arg, my args) {
        binding_resolve_names(&i.ref->binding, names, errs);
    }

    // Loop through all argument defaults and resolve names
    if (optional_func) {
        c_foreach(i, vec_expr, optional_func->arg_defaults) {
            expr_resolve_names(i.ref, names, errs);
        }

        // Resolve names in the function body, don't push a stack frame since the toplevel function body should be the same
        // namespace as the arguments
        block_resolve_names_without_stack_frame(&optional_func->body, names, errs);
    }

    if (my ret_explicit) {
        type_resolve_names(&my ret, names, errs);
    }

    // Pop the stack frame we pushed above
    name_stack_pop_frame(names);

    // Pop the generics frame
    name_stack_pop_frame(names);
}

void callable_type_typecheck(CallableType const* me, FunctionImpl const* optional_func, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);

    bool found_optional = false;
    bool required_after_optional = false;
    bool rest_not_last = false;
    c_foreach(i, vec_callable_arg, my args) {
        intptr_t index = i.ref - my args.data;
        Expr const* default_arg = optional_func ?
            function_impl_arg_default_at(optional_func, index) :
            &expr_implicit_any_no_span;

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
        } else if (
            found_optional &&
            default_arg->tag == EXPR_IMPLICIT_ANY &&
            !required_after_optional
        ) {
            required_after_optional = true;
            error_ctx_error(
                errs,
                i.ref->binding.content_span,
                1016,
                "A required parameter cannot follow an optional parameter."
            );
        }

        binding_typecheck(&i.ref->binding, errs);

        if (i.ref->rest) {
            if (!type_extends_array(binding_get_type(&i.ref->binding))) {
                error_ctx_error(
                    errs,
                    i.ref->span,
                    2370,
                    "A rest parameter must be of an array type."
                );
            }
            if (default_arg && default_arg->tag != EXPR_IMPLICIT_ANY) {
                error_ctx_error(
                    errs,
                    i.ref->binding.content_span,
                    1048,
                    "A rest parameter cannot have an initializer."
                );
            }
        }

        Type t = expr_get_type_and_typecheck(default_arg, errs);
        type_drop(&t);
    }

    type_typecheck(&my ret, errs);
}

Type const* callable_type_get_wrapper(CallableType* me) {
    my wrapper_type_do_not_drop = type_callable(me);
    return &my wrapper_type_do_not_drop;
}
