#include "callable_type.h"
#include "generic.h"
#include "scope_stack.h"
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

void callable_type_resolve_names(CallableType* me, ScopeStack* names, ErrorCtx* errs) {
    if (my owned_function_impl) {
        function_impl_lookup_names(my owned_function_impl, false, names, errs);

        generic_drop(&my generic);
        my generic = generic_clone(my owned_function_impl->generic);
        c_foreach(i, vec_callable_arg, my args) {
            type_drop(&i.ref->type);
            i.ref->type = type_refcopy(binding_get_effective_type(&i.ref->source->initializer.binding));
            type_lookup_names(&i.ref->type, names, errs);
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

        type_typecheck(&i.ref->type, errs);

        if (i.ref->source->rest) {
            if (!type_extends_array(&i.ref->type)) {
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

static inline bool callable_type_has_rest(CallableType const* me) {
    return !vec_callable_arg_empty(&my args) && vec_callable_arg_back(&my args)->source->rest;
}

Type callable_type_get_arg_type(CallableType const* me, intptr_t i) {
    if (i >= vec_callable_arg_size(&my args) - 1 && callable_type_has_rest(me)) {
        intptr_t i_in_rest = i - vec_callable_arg_size(&my args) + 1;
        Type index_type = type_value(value_number(i_in_rest));
        // No need to drop index_type
        return index_type_evaluate_type(&vec_callable_arg_back(&my args)->type, &index_type);
    } else if (i >= vec_callable_arg_size(&my args) || i < 0) {
        return type_never;
    } else {
        CallableArg const* arg = vec_callable_arg_at(&my args, i);
        Type ret = type_refcopy(&arg->type);
        if (arg->optional) {
            ret = type_composite(TYPE_UNION, ret, type_undefined);
        }
        return ret;
    }
}

intptr_t callable_type_min_arg_count(CallableType const* me) {
    intptr_t ret = vec_callable_arg_size(&my args);
    if (callable_type_has_rest(me)) {
        Type const* arg_type = type_resolve(&vec_callable_arg_back(&my args)->type);
        if (arg_type->tag == TYPE_TUPLE && vec_tuple_element_size(&arg_type->tuple.elements) > 0) {
            // TODO: account for rest arguments within tuple
            return ret - 1 + vec_tuple_element_size(&arg_type->tuple.elements);
        } else {
            ret--;
        }
    }
    while (ret && vec_callable_arg_at(&my args, ret - 1)->optional) {
        ret--;
    }
    return ret;
}

intptr_t callable_type_max_arg_count(CallableType const* me) {
    if (callable_type_has_rest(me)) {
        if (vec_callable_arg_back(&my args)->type.tag == TYPE_TUPLE) {
            // TODO: account for rest arguments within tuple
            return vec_callable_arg_size(&my args) - 1 +
                vec_tuple_element_size(&vec_callable_arg_back(&my args)->type.tuple.elements);
        } else {
            return INTPTR_MAX;
        }
    } else {
        return vec_callable_arg_size(&my args);
    }
}
