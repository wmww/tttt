#pragma once

#include "expr.h"
#include "type.h"
#include "block.h"
#include "value.h"
#include "binding.h"
#include "generic.h"
#include "declaration.h"

typedef struct FunctionArg {
    Span span;
    InitializedBinding initializer;
    bool explicit_optional;
    Span rest_span;
    bool rest;
} FunctionArg;

void function_arg_drop(FunctionArg* me);

#define g_class_key FunctionArg, function_arg
#define g_no_clone
#include <cleat/generic/vec.h>

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
    bool is_async;
    bool is_generator;
    Declaration const* promise_decl; // The global Promise type (if any), looked up for async functions
    bool is_abstract_constructor;
    LazyType lazy_type;
    bool evaluating_ret;
    ArcDeclaration declaration;
    Declaration this_declaration;
    Span arrow_span;
    // needed because TS1200 needs to be inserted in the typecheck phase for some reason
    bool newline_before_arrow;
    bool has_body;
    Block body; // Initialized to empty block if has_body is false
} FunctionImpl;

void function_impl_drop(FunctionImpl* me);
void function_impl_add_names(FunctionImpl* me, Scope* scope, bool exported);
void function_impl_lookup_names(FunctionImpl* me, bool this_is_any, ScopeStack* names, ErrorCtx* errs);
Type function_impl_into_type(FunctionImpl me);
Type function_impl_evaluate_type(FunctionImpl* me);
void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs);
