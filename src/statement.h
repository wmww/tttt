#pragma once

#include "scope.h"
#include "expr.h"
#include "interface.h"
#include "type.h"

typedef struct VarInit {
    enum {
        VAR_INIT_VAR,
        VAR_INIT_LET,
        VAR_INIT_CONST,
    } keyword;
    cstr name;
    bool has_type;
    Type type;
    bool has_value;
    Expr value;
} VarInit;

void VarInit_drop(VarInit* self);
cstr VarInit_to_cstr(VarInit* self, int indent);

typedef struct Statement {
    enum {
        STATEMENT_INTERFACE,
        STATEMENT_SCOPE,
        STATEMENT_EXPR,
        STATEMENT_VAR_INIT,
    } tag;
    union {
        NamedInterface interface;
        Scope scope;
        Expr expr;
        VarInit var_init;
    } data;
} Statement;

void Statement_drop(Statement* self);
cstr Statement_to_cstr(Statement* self, int indent);

#define i_key_class Statement
#define i_is_forward
#define i_no_clone
#include "stc/vec.h"
