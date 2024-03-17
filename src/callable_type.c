#include "callable_type.h"

void callable_type_arg_drop(CallableTypeArg* self) {
    cstr_drop(&self->name);
    type_drop(&self->type);
}

CallableTypeArg callable_type_arg_clone(CallableTypeArg self) {
    return (CallableTypeArg){
        .name = cstr_clone(self.name),
        .type_explicit = self.type_explicit,
        .type = type_clone(self.type),
    };
}

void callable_type_drop(CallableType* self) {
    vec_callable_type_arg_drop(&self->args);
    type_drop(&self->ret);
}

CallableType callable_type_clone(CallableType self) {
    return (CallableType){
        .args = vec_callable_type_arg_clone(self.args),
        .ret_explicit = self.ret_explicit,
        .ret = type_clone(self.ret),
    };
}

void cstr_append_callable_type_debug(cstr* target, CallableType* self, int indent) {
    cstr_append_sv(target, c_sv("("));
    c_foreach(i, VecCallableTypeArg, self->args) {
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
    cstr_append_type_debug(target, &self->ret, indent);
}

void cstr_append_callable_type_error_msg(cstr* target, CallableType* self) {
    cstr_append_sv(target, c_sv("("));
    c_foreach(i, VecCallableTypeArg, self->args) {
        cstr_append_s(target, i.ref->name);
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_error_msg(target, &i.ref->type);
        if (i.ref < i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(")"));
    cstr_append_sv(target, c_sv(" => "));
    cstr_append_type_error_msg(target, &self->ret);
}
