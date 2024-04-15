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

void function_arg_drop(FunctionArg* me);

#define i_key_class_names FunctionArg, function_arg
#define i_is_forward
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct FunctionImpl {
    Span full_span;
    cstr name;
    Span name_span;
    Type type;
    VecFunctionArg args;
    Span args_span;
    // needed because TS1200 needs to be inserted in the typecheck phase for some reason
    bool newline_before_arrow;
    Span arrow_span;
    bool has_block;
    Block body; // Initialized to empty block if has_block is false
} FunctionImpl;

void function_impl_drop(FunctionImpl* me);
CallableType* function_impl_callable_type(FunctionImpl* me);
Generic* function_impl_generic_or_null(FunctionImpl* me);
intptr_t function_impl_arg_count(FunctionImpl* me);
void cstr_append_function_impl_debug(cstr* target, FunctionImpl* me, bool include_arrow, int indent);
void function_impl_resolve_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs);
void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs);
