#pragma once

#include "type.h"

typedef struct CallableTypeArg {
    cstr name;
    bool type_explicit;
    Type type;
} CallableTypeArg;

void callable_type_arg_drop(CallableTypeArg* self);
CallableTypeArg callable_type_arg_clone(CallableTypeArg self);

#define i_key_class CallableTypeArg
#include "stc/vec.h"

typedef struct CallableType {
    VecCallableTypeArg args;
    bool ret_explicit;
    Type ret;
} CallableType;

void callable_type_drop(CallableType* self);
CallableType callable_type_clone(CallableType self);
void cstr_append_callable_type_debug(cstr* target, CallableType* self, int indent);
void cstr_append_callable_type_error_msg(cstr* target, CallableType* self);
