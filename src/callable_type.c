#include "callable_type.h"
#include "generic.h"
#include "function_impl.h"
#include "error.h"
#include "several_types.h"

void callable_arg_drop(CallableArg* me) {
    type_drop(&my type);
}

CallableArg callable_arg_clone(CallableArg me) {
    me.type = type_clone(me.type);
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

void callable_type_resolve_names(CallableType* me, Scope* names, ErrorCtx* errs) {
    if (my owned_function_impl) {
        function_impl_lookup_names(my owned_function_impl, false, names, errs);

        generic_drop(&my generic);
        my generic = generic_clone(my owned_function_impl->generic);
        for_each (i, vec_callable_arg, &my args) {
            type_drop(&i->type);
            i->type = type_refcopy(binding_get_effective_type(&i->source->initializer.binding));
            type_lookup_names(&i->type, names, errs);
        }
        // If me owns a FunctionImpl, that means the type was parsed as a type (not generated from an implementaiton)
        // That means it doesn't have a function body
        // That means we don't have to deal with return type inference here, we can just take whatver type is there
        type_drop(&my ret);
        my ret = type_move(&my owned_function_impl->ret);
    }
}

void callable_type_typecheck(CallableType const* me, ErrorCtx* errs) {
    if (my owned_function_impl) function_impl_typecheck_signature(my owned_function_impl, true, errs);
    type_typecheck(&my ret, errs);
}
