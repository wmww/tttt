#pragma once

#include "prelude.h"

typedef struct Statement Statement;
typedef struct ErrorCtx ErrorCtx;
forward_vec(vec_Statement, Statement);

typedef struct Scope {
    vec_Statement statements;
} Scope;

void Scope_drop(Scope* self);
cstr Scope_to_cstr(Scope* self, int indent);
void Scope_typecheck(Scope* self, ErrorCtx* errs);
