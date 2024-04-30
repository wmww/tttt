#include "callable_type.h"
#include "generic.h"
#include "binding.h"

void callable_type_drop(CallableType* me) {
    generic_drop(&my generic);
    vec_binding_drop(&my args);
    type_drop(&my ret);
}

CallableType callable_type_clone(CallableType me) {
    return (CallableType){
        .generic = generic_clone(me.generic),
        .args = vec_binding_clone(me.args),
        .min_args = me.min_args,
        .max_args = me.max_args,
        .variadic = me.variadic,
        .ret_explicit = me.ret_explicit,
        .ret = type_clone(me.ret),
        .is_constructor = me.is_constructor,
    };
}

void cstr_append_callable_type_debug(cstr* target, CallableType* me, int indent) {
    cstr_append_generic_debug(target, &my generic, indent);
    cstr_append_sv(target, c_sv("("));
    intptr_t args_size = vec_binding_size(&my args);
    for (intptr_t i = 0; i < args_size; i++) {
        Binding const* arg = vec_binding_at(&my args, i);
        cstr_append_binding_debug(target, arg, i >= my min_args, indent);
        if (i < args_size - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(")"));
    cstr_append_sv(target, c_sv(" => "));
    cstr_append_type_debug(target, &my ret, indent);
}

void cstr_append_callable_type_error_msg(cstr* target, CallableType* me) {
    if (my is_constructor) {
        cstr_append_sv(target, c_sv("typeof "));
        cstr_append_generic_error_msg(target, &my generic);
        cstr_append_type_error_msg(target, &my ret);
    } else {
        cstr_append_generic_error_msg(target, &my generic);
        cstr_append_sv(target, c_sv("("));
        intptr_t args_size = vec_binding_size(&my args);
        for (intptr_t i = 0; i < args_size; i++) {
            Binding const* arg = vec_binding_at(&my args, i);
            cstr_append_binding_error_msg(target, arg, i >= my min_args);
            if (i < args_size - 1) {
                cstr_append_sv(target, c_sv(", "));
            }
        }
        cstr_append_sv(target, c_sv(")"));
        cstr_append_sv(target, c_sv(" => "));
        cstr_append_type_error_msg(target, &my ret);
    }
}

void callable_type_resolve_names(CallableType* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_binding, my args) {
        binding_resolve_names(i.ref, names, errs);
    }
    type_resolve_names(&my ret, names, errs);
}

Type const* callable_type_get_wrapper(CallableType* me) {
    my wrapper_type_do_not_drop = type_callable(me);
    return &my wrapper_type_do_not_drop;
}
