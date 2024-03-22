#pragma once

#include "type.h"

typedef struct CallableTypeArg {
    cstr name;
    bool type_explicit;
    Type type;
} CallableTypeArg;

void callable_type_arg_drop(CallableTypeArg* me);
CallableTypeArg callable_type_arg_clone(CallableTypeArg me);

#define i_key_class_names CallableTypeArg, callable_type_arg
#include "wrap_stc/vec.h"

typedef struct CallableType {
    VecCallableTypeArg args;
    bool ret_explicit;
    Type ret;
} CallableType;

void callable_type_drop(CallableType* me);
CallableType callable_type_clone(CallableType me);
void cstr_append_callable_type_debug(cstr* target, CallableType* me, int indent);
void cstr_append_callable_type_error_msg(cstr* target, CallableType* me);
CallableType* type_get_callable_type(Type const* type);
