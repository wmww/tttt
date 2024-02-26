#pragma once

#include "prelude.h"

typedef struct Expr {
    char _;
} Expr;

void Expr_drop(Expr* self);
cstr Expr_to_cstr(Expr* self);
