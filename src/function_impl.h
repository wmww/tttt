#pragma once

#include "expr.h"
#include "type.h"
#include "block.h"
#include "value.h"

typedef struct CallableTypeArg CallableTypeArg;

typedef struct FunctionArg {
    cstr name;
    Span name_span;
    bool value_explicit;
    Expr value;
} FunctionArg;

void function_arg_drop(FunctionArg* self);

#define i_key_class_names FunctionArg, function_arg
#define i_is_forward
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct FunctionImpl {
    Type type;
    VecFunctionArg args;
    Span args_span;
    // needed because TS1200 needs to be inserted in the typecheck phase for some reason
    bool newline_before_arrow;
    Span arrow_span;
    Block body;
} FunctionImpl;

void function_impl_drop(FunctionImpl* self);
CallableType* function_impl_callable_type(FunctionImpl* self);
Generic* function_impl_generic_or_null(FunctionImpl* self);
int function_impl_arg_count(FunctionImpl* self);
void cstr_append_function_impl_debug(cstr* target, FunctionImpl* self, bool include_arrow, int indent);
void function_impl_resolve_names(FunctionImpl* self, NameStack* names, ErrorCtx* errs);
void function_impl_typecheck(FunctionImpl* self, ErrorCtx* errs);
