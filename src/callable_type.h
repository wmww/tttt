#pragma once

#include "type.h"

typedef struct CallableTypeGeneric {
    cstr name;
    Type extends;
    bool fallback_explicit;
    Type fallback;
} CallableTypeGeneric;

void CallableTypeGeneric_drop(CallableTypeGeneric* self);
CallableTypeGeneric CallableTypeGeneric_clone(CallableTypeGeneric self);

#define i_key_class CallableTypeGeneric
#include "stc/vec.h"

void cstr_append_vec_callable_type_generic(cstr* target, vec_CallableTypeGeneric* generics, int indent);

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
    vec_CallableTypeGeneric generics;
    vec_CallableTypeArg args;
    bool ret_explicit;
    Type ret;
} CallableType;

void CallableType_drop(CallableType* self);
CallableType CallableType_clone(CallableType self);
void cstr_append_callable_type_debug(cstr* target, CallableType* self, int indent);
