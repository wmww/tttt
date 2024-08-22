#include "callable_type.h"
#include "generic.h"
#include "name_stack.h"
#include "function_impl.h"
#include "error.h"

void callable_arg_drop(CallableArg* me) {
    lazy_type_drop(&my type);
}

CallableArg callable_arg_clone(CallableArg me) {
    me.type = lazy_type_clone(me.type);
    return me;
}

void callable_type_drop(CallableType* me) {
    generic_drop(&my generic);
    vec_callable_arg_drop(&my args);
    type_drop(&my ret);
    if (my owned_function_impl) {
        function_impl_drop(my owned_function_impl);
        free(my owned_function_impl);
    }
}

CallableType callable_type_clone(CallableType me) {
    me.generic = generic_clone(me.generic);
    me.args = vec_callable_arg_clone(me.args);
    me.ret = type_clone(me.ret);
    me.owned_function_impl = nullptr;
    return me;
}

void callable_type_resolve_names(CallableType* me, NameStack* names, ErrorCtx* errs) {
    if (my owned_function_impl) {
        function_impl_resolve_names(my owned_function_impl, names, errs);

        generic_drop(&my generic);
        my generic = generic_clone(my owned_function_impl->generic);
        c_foreach(i, vec_callable_arg, my args) {
            lazy_type_drop(&i.ref->type);
            i.ref->type = binding_get_lazy_type(&i.ref->source->initializer.binding);
            lazy_type_resolve_names(&i.ref->type, names, errs);
        }
        // If me owns a FunctionImpl, that means the type was parsed as a type (not generated from an implementaiton)
        // That means it doesn't have a function body
        // That means we don't have to deal with return type inference here, we can just take whatver type is there
        type_drop(&my ret);
        my ret = type_move(&my owned_function_impl->ret);
    }
}

void callable_type_typecheck(CallableType const* me, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);

    bool found_optional = false;
    bool required_after_optional = false;
    bool rest_not_last = false;
    c_foreach(i, vec_callable_arg, my args) {
        if (my owned_function_impl && i.ref->source->initializer.value.tag != EXPR_IMPLICIT_ANY) {
            error_ctx_error(
                errs,
                i.ref->source->initializer.value.outer_span,
                2371,
                "A parameter initializer is only allowed in a function or constructor implementation."
            );
        }

        if (i.ref->source->rest && i.ref != i.end - 1 && !rest_not_last) {
            rest_not_last = true;
            error_ctx_error(
                errs,
                i.ref->source->rest_span,
                1014,
                "A rest parameter must be last in a parameter list."
            );
        }

        if (i.ref->source->rest) {
        } else if (i.ref->source->explicit_optional) {
            found_optional = true;
        } else if (
            found_optional &&
            !i.ref->optional &&
            !required_after_optional
        ) {
            required_after_optional = true;
            error_ctx_error(
                errs,
                i.ref->source->initializer.binding.content_span,
                1016,
                "A required parameter cannot follow an optional parameter."
            );
        }

        lazy_type_typecheck(&i.ref->type, errs);

        if (i.ref->source->rest) {
            if (!type_extends_array(lazy_type_resolve(&i.ref->type))) {
                error_ctx_error(
                    errs,
                    i.ref->source->span,
                    2370,
                    "A rest parameter must be of an array type."
                );
            }
            if (i.ref->source->initializer.value.tag != EXPR_IMPLICIT_ANY) {
                error_ctx_error(
                    errs,
                    i.ref->source->initializer.binding.content_span,
                    1048,
                    "A rest parameter cannot have an initializer."
                );
            }
        }
    }

    type_typecheck(&my ret, errs);
}
