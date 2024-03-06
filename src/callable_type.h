#pragma once

#include "type.h"

typedef struct CallableTypeArg {
    cstr name;
    bool type_explicit;
    Type type;
} CallableTypeArg;

void CallableTypeArg_drop(CallableTypeArg* self);
CallableTypeArg CallableTypeArg_clone(CallableTypeArg self);

#define i_key_class CallableTypeArg
#include "stc/vec.h"

typedef struct CallableType {
    vec_CallableTypeArg args;
    bool ret_explicit;
    Type ret;
} CallableType;

void CallableType_drop(CallableType* self);
CallableType CallableType_clone(CallableType self);
void cstr_append_callable_type_debug(cstr* target, CallableType* self, int indent);
