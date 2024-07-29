#pragma once

#include "type.h"
#include "generic.h"
#include "binding.h"

typedef struct CallableArg {
    Span span;
    Span rest_span;
    Binding binding;
    bool optional;
    bool explicit_optional;
    bool rest;
} CallableArg;

void callable_arg_drop(CallableArg* me);
CallableArg callable_arg_clone(CallableArg me);

#define i_key_class_names CallableArg, callable_arg
#include "wrap_stc/vec.h"

typedef struct CallableType {
    Generic generic;
    VecCallableArg args;
    Span ret_span;
    bool ret_explicit;
    Type ret;
    bool is_constructor;
    bool is_abstract_constructor;
    Type wrapper_type_do_not_drop; // used by callable_type_get_wrapper
} CallableType;

void callable_type_drop(CallableType* me);
CallableType callable_type_clone(CallableType me);
void cstr_append_callable_type_debug(cstr* target, CallableType* me, int indent);
void cstr_append_callable_type_error_msg(cstr* target, CallableType* me);
void callable_type_resolve_names(CallableType* me, FunctionImpl const* optional_func, NameStack* names, ErrorCtx* errs);
void callable_type_typecheck(CallableType const* me, FunctionImpl const* optional_func, ErrorCtx* errs);

// HACK: returns a pointer to the generic version of this callable type. Returned type should never be dropped, and
// is invalidated by moving or dropping the given callable type.
Type const* callable_type_get_wrapper(CallableType* me);
