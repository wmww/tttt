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
    vec_callable_type_arg_drop(&my args);
    type_drop(&my ret);
}

CallableType callable_type_clone(CallableType me) {
    return (CallableType){
        .args = vec_callable_type_arg_clone(me.args),
        .ret_explicit = me.ret_explicit,
        .ret = type_clone(me.ret),
    };
}

void cstr_append_callable_type_debug(cstr* target, CallableType* me, int indent) {
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

CallableType* type_get_callable_type(Type const* type) {
    if (type->tag == TYPE_CALLABLE) {
        return type->callable;
    } else if (type->tag == TYPE_GENERIC && type->generic->inner.tag == TYPE_CALLABLE) {
        return type->generic->inner.callable;
    } else {
        return nullptr;
    }
}
