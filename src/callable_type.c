#include "callable_type.h"
#include "generic.h"

void callable_type_arg_drop(CallableTypeArg* me) {
    cstr_drop(&my name);
    type_drop(&my type);
}

CallableTypeArg callable_type_arg_clone(CallableTypeArg me) {
    return (CallableTypeArg){
        .name = cstr_clone(me.name),
        .type_explicit = me.type_explicit,
        .type = type_clone(me.type),
    };
}

void callable_type_drop(CallableType* me) {
    generic_drop(&my generic);
    vec_callable_type_arg_drop(&my args);
    type_drop(&my ret);
}

CallableType callable_type_clone(CallableType me) {
    return (CallableType){
        .generic = generic_clone(me.generic),
        .args = vec_callable_type_arg_clone(me.args),
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
    c_foreach(i, vec_callable_type_arg, my args) {
        cstr_append_sv(target, c_sv(CLR_NAME));
        cstr_append_s(target, i.ref->name);
        cstr_append_sv(target, c_sv(CLR0));
        if (i.ref->type_explicit) {
            cstr_append_sv(target, c_sv(": "));
            cstr_append_type_debug(target, &i.ref->type, indent);
        }
        if (i.ref < i.end - 1) {
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
        c_foreach(i, vec_callable_type_arg, my args) {
            cstr_append_s(target, i.ref->name);
            cstr_append_sv(target, c_sv(": "));
            cstr_append_type_error_msg(target, &i.ref->type);
            if (i.ref < i.end - 1) {
                cstr_append_sv(target, c_sv(", "));
            }
        }
        cstr_append_sv(target, c_sv(")"));
        cstr_append_sv(target, c_sv(" => "));
        cstr_append_type_error_msg(target, &my ret);
    }
}

void callable_type_resolve_names(CallableType* me, NameStack* names, ErrorCtx* errs) {
    c_foreach(i, vec_callable_type_arg, my args) {
        type_resolve_names(&i.ref->type, names, errs);
    }
    type_resolve_names(&my ret, names, errs);
}

Type const* callable_type_get_wrapper(CallableType* me) {
    my wrapper_type_do_not_drop = type_callable(me);
    return &my wrapper_type_do_not_drop;
}
