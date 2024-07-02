#include "callable_type.h"
#include "generic.h"
#include "binding.h"

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

void cstr_append_callable_type_debug(cstr* target, CallableType* me, int indent) {
    if (my is_abstract_constructor) {
        cstr_append_sv(target, c_sv("abstract "));
    }
    if (my is_constructor) {
        cstr_append_sv(target, c_sv("new "));
    }
    cstr_append_generic_debug(target, &my generic, indent);
    cstr_append_sv(target, c_sv("("));
    c_foreach(i, vec_callable_arg, my args) {
        if (i.ref->rest) {
            cstr_append_sv(target, c_sv("..."));
        }
        cstr_append_binding_debug(target, &i.ref->binding, i.ref->explicit_optional, indent);
        if (i.ref != i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(")"));
    cstr_append_sv(target, c_sv(" => "));
    cstr_append_type_debug(target, &my ret, indent);
}

void cstr_append_callable_type_error_msg(cstr* target, CallableType* me) {
    if (my is_constructor) {
        cstr_append_sv(target, c_sv("new "));
    }
    cstr_append_generic_error_msg(target, &my generic);
    cstr_append_sv(target, c_sv("("));
    c_foreach(i, vec_callable_arg, my args) {
        if (i.ref->rest) {
            cstr_append_sv(target, c_sv("..."));
        }
        cstr_append_binding_error_msg(target, &i.ref->binding, i.ref->optional);
        if (i.ref != i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(")"));
    cstr_append_sv(target, c_sv(" => "));
    cstr_append_type_error_msg(target, &my ret);
}

void callable_type_resolve_names(CallableType* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_callable_arg, my args) {
        binding_resolve_names(&i.ref->binding, names, errs);
    }
    type_resolve_names(&my ret, names, errs);
}

Type const* callable_type_get_wrapper(CallableType* me) {
    my wrapper_type_do_not_drop = type_callable(me);
    return &my wrapper_type_do_not_drop;
}
