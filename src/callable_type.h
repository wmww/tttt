#pragma once

#include "type.h"
#include "generic.h"

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
    Generic generic;
    VecCallableTypeArg args;
    bool ret_explicit;
    Type ret;
    bool is_constructor;
    Type wrapper_type_do_not_drop; // used by callable_type_get_wrapper
} CallableType;

void callable_type_drop(CallableType* me);
CallableType callable_type_clone(CallableType me);
void cstr_append_callable_type_debug(cstr* target, CallableType* me, int indent);
void cstr_append_callable_type_error_msg(cstr* target, CallableType* me);
void callable_type_resolve_names(CallableType* me, NameStack* names, ErrorCtx* errs);

// HACK: returns a pointer to the generic version of this callable type. Returned type should never be dropped, and
// is invalidated by moving or dropping the given callable type.
Type const* callable_type_get_wrapper(CallableType* me);
