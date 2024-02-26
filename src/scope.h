#pragma once

#include "prelude.h"

typedef struct Statement Statement;
forward_vec(vec_Statement, Statement);

typedef struct Scope {
    vec_Statement statements;
} Scope;

void Scope_drop(Scope* self);
cstr Scope_to_cstr(Scope* self, int indent);
Scope scope_simplify(Scope self);
