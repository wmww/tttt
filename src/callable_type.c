#include "callable_type.h"

void CallableTypeGeneric_drop(CallableTypeGeneric* self) {
    cstr_drop(&self->name);
    Type_drop(&self->extends);
    Type_drop(&self->fallback);
}

CallableTypeGeneric CallableTypeGeneric_clone(CallableTypeGeneric self) {
    return (CallableTypeGeneric){
        .name = cstr_clone(self.name),
        .extends = Type_clone(self.extends),
        .fallback_explicit = self.fallback_explicit,
        .fallback = Type_clone(self.fallback),
    };
}

void cstr_append_vec_callable_type_generic(cstr* target, vec_CallableTypeGeneric* generics, int indent) {
    if (vec_CallableTypeGeneric_empty(generics)) {
        return;
    }
    cstr_append_sv(target, c_sv("<"));
    c_foreach(i, vec_CallableTypeGeneric, *generics) {
        cstr_append_sv(target, c_sv(CLR_TYPE));
        cstr_append_s(target, i.ref->name);
        cstr_append_sv(target, c_sv(CLR0));
        if (i.ref->extends.tag != TYPE_ANY) {
            cstr_append_sv(target, c_sv(" extends "));
            cstr_append_type_debug(target, &i.ref->extends, indent);
        }
        if (i.ref->fallback_explicit) {
            cstr_append_sv(target, c_sv(" = "));
            cstr_append_type_debug(target, &i.ref->fallback, indent);
        }
        if (i.ref < i.end - 1) {
            cstr_append_sv(target, c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv(">"));
}

void CallableTypeArg_drop(CallableTypeArg* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
}

CallableTypeArg CallableTypeArg_clone(CallableTypeArg self) {
    return (CallableTypeArg){
        .name = cstr_clone(self.name),
        .type_explicit = self.type_explicit,
        .type = Type_clone(self.type),
    };
}

void CallableType_drop(CallableType* self) {
    vec_CallableTypeGeneric_drop(&self->generics);
    vec_CallableTypeArg_drop(&self->args);
    Type_drop(&self->ret);
}

CallableType CallableType_clone(CallableType self) {
    return (CallableType){
        .generics = vec_CallableTypeGeneric_clone(self.generics),
        .args = vec_CallableTypeArg_clone(self.args),
        .ret_explicit = self.ret_explicit,
        .ret = Type_clone(self.ret),
    };
}

void cstr_append_callable_type_debug(cstr* target, CallableType* self, int indent) {
    cstr_append_vec_callable_type_generic(target, &self->generics, indent);
    cstr_append_sv(target, c_sv("("));
    c_foreach(i, vec_CallableTypeArg, self->args) {
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
    if (self->ret_explicit) {
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_debug(target, &self->ret, indent);
    }
}
