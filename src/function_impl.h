#pragma once

#include "expr.h"
#include "type.h"
#include "block.h"
#include "value.h"

typedef struct FunctionArg {
    cstr name;
    bool value_explicit;
    Expr value;
} FunctionArg;

void FunctionArg_drop(FunctionArg* self);

#define i_key_class FunctionArg
#define i_is_forward
#define i_no_clone
#include "stc/vec.h"

typedef struct FunctionImpl {
    Type type;
    vec_FunctionArg args;
    Block body;
} FunctionImpl;

void FunctionImpl_drop(FunctionImpl* self);
CallableType* FunctionImpl_callable_type(FunctionImpl* self);
Generic* FunctionImpl_generic_or_null(FunctionImpl* self);
long FunctionImpl_arg_count(FunctionImpl* self);
void cstr_append_function_impl_debug(cstr* target, FunctionImpl* self, bool include_arrow, int indent);
void FunctionImpl_resolve_names(FunctionImpl* self, NameStack* names, ErrorCtx* errs);
void FunctionImpl_typecheck(FunctionImpl* self, ErrorCtx* errs);
