#pragma once

#include "prelude.h"

typedef struct Statement Statement;
typedef struct ErrorCtx ErrorCtx;
forward_vec(vec_Statement, Statement);

typedef struct Scope {
    vec_Statement statements;
} Scope;

void Scope_drop(Scope* self);
void cstr_append_scope_debug(cstr* target, Scope* self, int indent);
void Scope_typecheck(Scope* self, ErrorCtx* errs);
