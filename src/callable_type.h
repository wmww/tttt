#pragma once

#include "type.h"

typedef struct FunctionImpl FunctionImpl;

// A function type or signature written as a type (`(x: T) => U`, `new () => T`, `m(): void` in an interface): a
// FunctionImpl without a body, with FunctionImpl.is_type set
typedef struct CallableType {
    FunctionImpl* function; // Owned
} CallableType;

void callable_type_drop(CallableType* me);
void callable_type_resolve_names(CallableType* me, Scope* names, ErrorCtx* errs);
void callable_type_typecheck(CallableType const* me, ErrorCtx* errs);
