#pragma once

#include "scope.h"
#include "expr.h"
#include "interface.h"
#include "type.h"

typedef struct ErrorCtx ErrorCtx;

typedef struct NameInitData {
    cstr name;
    bool has_type;
    Type type;
    bool has_value;
    Expr value;
} NameInitData;

void NameInitData_drop(NameInitData* self);
cstr NameInitData_to_cstr(NameInitData* self, int indent);
Type NameInitData_get_type_and_typecheck(NameInitData* self, ErrorCtx* errs);

typedef struct VarInitStatement {
    enum {
        VAR_INIT_VAR,
        VAR_INIT_LET,
        VAR_INIT_CONST,
    } keyword;
    NameInitData data;
} VarInitStatement;

void VarInitStatement_drop(VarInitStatement* self);
cstr VarInitStatement_to_cstr(VarInitStatement* self, int indent);

typedef struct FunctionStatementArg {
    NameInitData data;
} FunctionStatementArg;

void FunctionStatementArg_drop(FunctionStatementArg* self);

#define i_key_class FunctionStatementArg
#define i_no_clone
#include "stc/vec.h"

typedef struct FunctionStatement {
    cstr name;
    vec_FunctionStatementArg args;
    bool has_return_type;
    Type return_type;
    Scope body;
} FunctionStatement;

void FunctionStatement_drop(FunctionStatement* self);
cstr FunctionStatement_to_cstr(FunctionStatement* self, int indent);
void FunctionStatement_typecheck(FunctionStatement* self, ErrorCtx* errs);

typedef struct Statement {
    enum {
        STATEMENT_INTERFACE,
        STATEMENT_SCOPE,
        STATEMENT_EXPR,
        STATEMENT_VAR_INIT,
        STATEMENT_FUNCTION,
    } tag;
    union {
        NamedInterface interface;
        Scope scope;
        Expr expr;
        VarInitStatement var_init;
        FunctionStatement function;
    } data;
} Statement;

void Statement_drop(Statement* self);
cstr Statement_to_cstr(Statement* self, int indent);
void Statement_typecheck(Statement* self, ErrorCtx* errs);

#define i_key_class Statement
#define i_is_forward
#define i_no_clone
#include "stc/vec.h"
