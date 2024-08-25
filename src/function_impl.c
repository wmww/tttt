#include "function_impl.h"
#include "name_stack.h"
#include "callable_type.h"
#include "generic.h"
#include "statement.h"
#include "error.h"

void function_arg_drop(FunctionArg* me) {
    initialized_binding_drop(&my initializer);
}

void function_impl_drop(FunctionImpl* me) {
    generic_drop(&my generic);
    vec_function_arg_drop(&my args);
    type_drop(&my ret);
    type_drop(&my resolved_type);
    named_variable_destroy(my named_var);
    block_drop(&my body);
}

void function_impl_add_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs) {
    __( errs );
    ASSERT(!my named_var);
    my named_var = make_named_variable(my name, my full_span, lazy_type_function(me), &my generic);
    name_stack_add_variable_s(names, cstr_from_span(&my name), my named_var);
}

void function_impl_resolve_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs) {
    // Handle generics (includes adding names)
    name_stack_push_generic_frame(names, &my generic);

    // Push a stack frame for arguments to live in
    name_stack_push_function_frame(names);

    // Loop through all arguments and add them to the namespace
    c_foreach(i, vec_function_arg, my args) {
        initialized_binding_add_names(&i.ref->initializer, true, names, errs);
        if (!i.ref->initializer.binding.has_explicit_type &&
            i.ref->initializer.value.tag == EXPR_IMPLICIT_ANY
        ) {
            if (errs && errs->conf.no_implicit_any) {
                binding_add_implicit_any_type_errors(&i.ref->initializer.binding, true, errs);
            }
        }
    }

    // Now that names for all arguments are added, resolve all names
    c_foreach(i, vec_function_arg, my args) {
        initialized_binding_resolve_names(&i.ref->initializer, names, errs);
    }

    // Resolve names in the function body, and return type. don't push a stack frame since the toplevel function body
    // should be the same namespace as the arguments
    block_resolve_names_without_stack_frame(&my body, names, errs);
    type_resolve_names(&my ret, names, errs);

    // Pop the stack frame we pushed above
    name_stack_pop_frame(names);

    // Pop the generics frame
    name_stack_pop_frame(names);
}

static Type function_impl_make_callable_sans_inferred_ret(FunctionImpl const* me) {
    VecCallableArg args = vec_callable_arg_init();
    c_foreach(i, vec_function_arg, my args) {
        vec_callable_arg_push(&args, (CallableArg){
            .optional = i.ref->explicit_optional || i.ref->initializer.value.tag != EXPR_IMPLICIT_ANY,
            .type = binding_get_lazy_type(&i.ref->initializer.binding),
            .source = i.ref, // Safe because FunctionImpls are always owned by the AST, and more args will not be added
        });
    }
    return type_callable(c_new(CallableType, {
        .generic = generic_clone(my generic),
        .args = args,
        .ret = type_clone(my ret),
        .is_constructor = my is_constructor,
        .is_abstract_constructor = my is_abstract_constructor
    }));
}

Type function_impl_into_type(FunctionImpl me) {
    // PERF: Maybe allocate the CallableType and FunctionImpl in the same memory block?
    FunctionImpl* impl = c_new(FunctionImpl, me);
    Type type = function_impl_make_callable_sans_inferred_ret(impl);
    type.callable->owned_function_impl = impl;
    return type;
}

Type const* function_impl_resolve_type(FunctionImpl* me) {
    switch (my resolve_state) {
        case RESOLVE_STATE_UNINITIALIZED:
            my resolve_state = RESOLVE_STATE_IN_FLIGHT;

            if (my ret_explicit || !my has_body) {
                my resolved_type = function_impl_make_callable_sans_inferred_ret(me);
                my resolve_state = RESOLVE_STATE_DONE;
            } else {
                StatementCtx ctx = statement_ctx_function(&type_any, true);
                block_typecheck(&my body, &ctx, nullptr);

                if (my resolve_state == RESOLVE_STATE_IN_FLIGHT) {
                    Type ret;
                    if (ctx.impl_return.tag == TYPE_NEVER) {
                        // This will need to be improved
                        ret = type_void;
                    } else {
                        ret = type_move(&ctx.impl_return);
                        type_abstract_literals(&ret);
                    }
                    my resolved_type = function_impl_make_callable_sans_inferred_ret(me);
                    type_drop(&my resolved_type.callable->ret);
                    my resolved_type.callable->ret = ret;
                    my resolve_state = RESOLVE_STATE_DONE;
                }

                statement_ctx_drop(&ctx);
            }
            break;

        case RESOLVE_STATE_IN_FLIGHT:
            my resolved_type = function_impl_make_callable_sans_inferred_ret(me);
            type_drop(&my resolved_type.callable->ret);
            my resolved_type.callable->ret = type_self_referential_error;
            my resolve_state = RESOLVE_STATE_DONE;
            break;

        case RESOLVE_STATE_DONE: break;
    }

    return &my resolved_type;
}

void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs) {
    c_foreach(i, vec_function_arg, my args) {
        initialized_binding_typecheck(&i.ref->initializer, errs);
    }

    function_impl_resolve_type(me);
    type_typecheck(&my resolved_type, errs);

    if (my newline_before_arrow) {
        error_ctx_error(errs, my arrow_span, 1200, "Line terminator not permitted before arrow.");
    }

    StatementCtx ctx = statement_ctx_function(my ret_explicit ? &my ret : &type_any, false);
    block_typecheck(&my body, &ctx, errs);
    statement_ctx_drop(&ctx);
}
