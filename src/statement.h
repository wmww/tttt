#pragma once

#include "scope.h"
#include "expr.h"
#include "interface.h"

typedef struct Statement {
    enum {
        STATEMENT_INTERFACE,
        STATEMENT_SCOPE,
        STATEMENT_EXPR,
    } tag;
    union {
        NamedInterface interface;
        Scope scope;
        Expr expr;
    } data;
} Statement;

void Statement_drop(Statement* self);
cstr Statement_to_cstr(Statement* self, int indent);
Statement statement_simplify(Statement self);

#define i_key_class Statement
#define i_is_forward
#define i_no_clone
#include "stc/vec.h"
