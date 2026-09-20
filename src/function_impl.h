#pragma once

#include "expr.h"
#include "type.h"
#include "block.h"
#include "value.h"
#include "binding.h"
#include "generic.h"
#include "lazy_type.h"
#include "flow.h"

typedef struct FunctionArg {
    Span span;
    InitializedBinding initializer;
    bool explicit_optional;
    Span rest_span;
    bool rest;
    VecExpr decorators; // TODO: only checked as expressions
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
    TypeNode ret;
    bool is_constructor;
    bool is_method; // Declared with method syntax (parameters compare bivariantly)
    bool is_async;
    bool is_generator;
    Symbol const* promise_decl; // The global Promise type (if any), looked up for async functions
    bool is_abstract_constructor;
    Symbol const* constructed_class; // The constructor of a class: what it returns
    bool is_type; // Written as a type (function_impl_into_type()): owned by a CallableType, no body to infer from
    LazyType lazy_type;
    Symbol* symbol;
    Span arrow_span;
    // needed because TS1200 needs to be inserted in the typecheck phase for some reason
    bool newline_before_arrow;
    bool has_body;
    Block body; // Initialized to empty block if has_body is false
    // Control flow (flow.h), set by the resolve walk
    FlowNode* outer_flow; // Function expressions, arrows, object literal methods: the flow of the container it's made in
    FlowNode* end_flow; // The end of the body if control can get there (before typed reachability), else null
    FlowNode* return_flow; // Constructors: every way out
    bool has_explicit_return; // A reachable `return`
    bool has_bare_return; // A reachable `return;`
    Span member_name; // Methods and accessors of classes and object literals (`name` is for what declares a symbol)
    uint8_t accessor; // 1: a getter, 2: a setter (of a class or an object literal)
    bool is_expression; // A function expression, arrow function or object literal method (FUNCTION_IS_EXPRESSION)
} FunctionImpl;

void function_impl_drop(FunctionImpl* me);
void function_impl_add_names(FunctionImpl* me, Scope* scope, bool exported);
typedef enum FunctionFlowKind {
    FUNCTION_IS_DECLARATION, // Function declarations, class members, signatures: a control flow graph of their own
    // Function expressions, arrow functions, object literal methods: narrowings of the flow they're made in can
    // survive into them (FunctionImpl.outer_flow), and an immediately invoked one is part of that flow
    FUNCTION_IS_EXPRESSION,
} FunctionFlowKind;
void function_impl_lookup_names_as(FunctionImpl* me, FunctionFlowKind kind, bool this_is_any, Scope* names, ErrorCtx* errs);
static inline void function_impl_lookup_names(FunctionImpl* me, bool this_is_any, Scope* names, ErrorCtx* errs) {
    function_impl_lookup_names_as(me, FUNCTION_IS_DECLARATION, this_is_any, names, errs);
}
TypeNode function_impl_into_type(FunctionImpl me);
Ty const* function_impl_evaluate_type(FunctionImpl* me);
// What return expressions are contextually typed by. Returns the enclosing function's, to be put back
Ty const* function_impl_enter_return_context(FunctionImpl const* me);
// The checks on a parameter list. `is_type`: a function type or signature, which can't have initializers
void function_impl_typecheck_signature(FunctionImpl const* me, bool is_type, ErrorCtx* errs);
void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs);
