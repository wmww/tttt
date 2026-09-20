#pragma once

#include "generic.h"

typedef struct FunctionArg FunctionArg;
typedef struct FunctionImpl FunctionImpl;

typedef struct CallableArg {
    bool optional;
    TypeNode type;
    FunctionArg const* source;
} CallableArg;

void callable_arg_drop(CallableArg* me);
CallableArg callable_arg_clone(CallableArg me);

#define g_class_key CallableArg, callable_arg
#include <cleat/generic/vec.h>

typedef struct CallableType {
    Generic generic;
    VecCallableArg args;
    TypeNode ret;
    bool is_constructor;
    bool is_abstract_constructor;
    // If this CallableType was parsed directly from code into the AST (not cloned from another or created from a
    // FunctionImpl) than it must own it's own FunctionImpl. arg sources point into it. Clones will not own a
    // FunctionImpl and will point into this one (since this CallableType is owned by the AST it should outlive them)
    FunctionImpl* owned_function_impl;
} CallableType;

void callable_type_drop(CallableType* me);
CallableType callable_type_clone(CallableType me);
void callable_type_resolve_names(CallableType* me, Scope* names, ErrorCtx* errs);
void callable_type_typecheck(CallableType const* me, ErrorCtx* errs);
