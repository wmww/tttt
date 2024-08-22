#pragma once

#include "expr.h"
#include "type.h"
#include "block.h"
#include "value.h"
#include "binding.h"
#include "generic.h"

typedef struct FunctionArg {
    Span span;
    InitializedBinding initializer;
    bool explicit_optional;
    Span rest_span;
    bool rest;
} FunctionArg;

void function_arg_drop(FunctionArg* me);

#define i_key_class_names FunctionArg, function_arg
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct FunctionImpl {
    Span name;
    Span full_span;
    Generic generic;
    VecFunctionArg args;
    Span args_span;
    Span ret_span;
    bool ret_explicit;
    Type ret;
    bool is_constructor;
    bool is_abstract_constructor;
    ResolveState resolve_state;
    Type resolved_type;
    NamedVariable* named_var;
    Span arrow_span;
    // needed because TS1200 needs to be inserted in the typecheck phase for some reason
    bool newline_before_arrow;
    bool has_block;
    Block body; // Initialized to empty block if has_block is false
} FunctionImpl;

void function_impl_drop(FunctionImpl* me);
void function_impl_add_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs);
void function_impl_resolve_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs);
Type function_impl_into_type(FunctionImpl me);
Type const* function_impl_resolve_type(FunctionImpl* me);
void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs);
