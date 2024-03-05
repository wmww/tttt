#pragma once

#include "block.h"
#include "expr.h"
#include "interface.h"
#include "type.h"
#include "callable_type.h"

typedef struct ErrorCtx ErrorCtx;
typedef struct LoopStatement LoopStatement;

// NameInitData is useful for multiple things that can optionally both values and types

typedef struct NameInitData {
    cstr name;
    bool explicit_type;
    Type type;
    bool explicit_value;
    Expr value;
} NameInitData;

void NameInitData_drop(NameInitData* self);
void cstr_append_name_init_data_debug(cstr* target, NameInitData* self, int indent);
Type NameInitData_get_type_and_typecheck(NameInitData* self, ErrorCtx* errs);

// Variable initialization

typedef struct VarInitStatement {
    enum {
        VAR_INIT_VAR,
        VAR_INIT_LET,
        VAR_INIT_CONST,
    } keyword;
    NameInitData data;
} VarInitStatement;

void VarInitStatement_drop(VarInitStatement* self);
void cstr_append_var_init_statement_debug(cstr* target, VarInitStatement* self, int indent);

// Function declaration

typedef struct FunctionStatementArg {
    NameInitData data;
} FunctionStatementArg;

void FunctionStatementArg_drop(FunctionStatementArg* self);

#define i_key_class FunctionStatementArg
#define i_is_forward
#define i_no_clone
#include "stc/vec.h"

typedef struct FunctionStatement {
    cstr name;
    vec_CallableTypeGeneric generics;
    vec_FunctionStatementArg args;
    bool has_return_type;
    Type return_type;
    Block body;
} FunctionStatement;

void FunctionStatement_drop(FunctionStatement* self);
void cstr_append_function_statement_debug(cstr* target, FunctionStatement* self, int indent);
void FunctionStatement_typecheck(FunctionStatement* self, ErrorCtx* errs);

// If statements

typedef struct IfStatement {
    vec_Expr conditions;
    // The length of statements should always be equal to conditions, or one greater (in the case of a trailing else)
    vec_Statement statements;
} IfStatement;

// Statements

typedef struct Statement {
    enum {
        STATEMENT_NOOP,
        STATEMENT_INTERFACE,
        STATEMENT_BLOCK,
        STATEMENT_EXPR,
        STATEMENT_RETURN,
        STATEMENT_VAR_INIT,
        STATEMENT_FUNCTION,
        STATEMENT_IF_ELSE,
        STATEMENT_LOOP,
        STATEMENT_BREAK,
    } tag;
    union {
        NamedInterface interface;
        Block block;
        Expr expr;
        Expr return_expr;
        VarInitStatement var_init;
        FunctionStatement function;
        IfStatement if_else;
        LoopStatement* loop;
    };
} Statement;

#define Statement_noop ((Statement){ .tag = STATEMENT_NOOP })

void Statement_drop(Statement* self);
void cstr_append_statement_debug(cstr* target, Statement* self, int indent);
void Statement_typecheck(Statement* self, ErrorCtx* errs);

#define i_key_class Statement
#define i_is_forward
#define i_no_clone
#include "stc/vec.h"

// Loop statements

typedef struct LoopStatement {
    Statement initialization; // initialization happens only once, but in the same stack frame as the body
    Expr condition;
    Statement afterthought;
    Statement body;
} LoopStatement;
