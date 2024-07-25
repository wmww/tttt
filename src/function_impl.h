#pragma once

#include "expr.h"
#include "type.h"
#include "block.h"
#include "value.h"
#include "callable_type.h"

typedef struct CallableTypeArg CallableTypeArg;

typedef struct FunctionImpl {
    Span full_span;
    Span name;
    CallableType callable;
    VecExpr arg_defaults; // May have less elements than there are args, these are assumed to be implicit any
    Span args_span;
    Span arrow_span;
    // needed because TS1200 needs to be inserted in the typecheck phase for some reason
    bool newline_before_arrow;
    bool has_block;
    bool ret_type_resolved;
    Block body; // Initialized to empty block if has_block is false
} FunctionImpl;

void function_impl_drop(FunctionImpl* me);
void cstr_append_function_impl_debug(cstr* target, FunctionImpl* me, bool include_arrow, int indent);
void function_impl_resolve_names(FunctionImpl* me, NameStack* names, ErrorCtx* errs);
Type const* function_impl_resolve_type(FunctionImpl* me);
void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs);

static inline Expr const* function_impl_arg_default_at(FunctionImpl const* me, intptr_t i) {
    if (i < vec_expr_size(&my arg_defaults)) {
        return vec_expr_at(&my arg_defaults, i);
    } else {
        return &expr_implicit_any_no_span;
    }
}
