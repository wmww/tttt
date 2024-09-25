#pragma once

#include "generic.h"

typedef struct FunctionArg FunctionArg;
typedef struct FunctionImpl FunctionImpl;

typedef struct CallableArg {
    bool optional;
    Type type;
    FunctionArg const* source;
} CallableArg;

void callable_arg_drop(CallableArg* me);
CallableArg callable_arg_clone(CallableArg me);

#define i_key_class_names CallableArg, callable_arg
#include "wrap_stc/vec.h"

typedef struct CallableType {
    Generic generic;
    VecCallableArg args;
    Type ret;
    bool is_constructor;
    bool is_abstract_constructor;
    // If this CallableType was parsed directly from code into the AST (not cloned from another or created from a
    // FunctionImpl) than it must own it's own FunctionImpl. arg sources point into it. Clones will not own a
    // FunctionImpl and will point into this one (since this CallableType is owned by the AST it should outlive them)
    FunctionImpl* owned_function_impl;
} CallableType;

void callable_type_drop(CallableType* me);
CallableType callable_type_clone(CallableType me);
void callable_type_resolve_names(CallableType* me, NameStack* names, ErrorCtx* errs);
void callable_type_typecheck(CallableType const* me, ErrorCtx* errs);

// Returned type should be dropped before me is dropped or moved
Type callable_type_get_arg_type(CallableType const* me, intptr_t i);
intptr_t callable_type_min_arg_count(CallableType const* me);
// Returns INTPTR_MAX if unbounded
intptr_t callable_type_max_arg_count(CallableType const* me);
