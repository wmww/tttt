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
    bool type_explicit;
    Type type;
    bool value_explicit;
    Expr value;
} NameInitData;

void NameInitData_drop(NameInitData* self);
void cstr_append_name_init_data_debug(cstr* target, NameInitData* self, int indent);

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
void VarInitStatement_typecheck(VarInitStatement* self, ErrorCtx* errs);

// Function declaration

typedef struct FunctionStatementArg {
    cstr name;
    bool value_explicit;
    Expr value;
} FunctionStatementArg;

void FunctionStatementArg_drop(FunctionStatementArg* self);

#define i_key_class FunctionStatementArg
#define i_is_forward
#define i_no_clone
#include "stc/vec.h"

typedef struct FunctionStatement {
    cstr name;
    Type type;
    vec_FunctionStatementArg args;
    Block body;
} FunctionStatement;

void FunctionStatement_drop(FunctionStatement* self);
long FunctionStatement_arg_count(FunctionStatement* self);
void cstr_append_function_statement_debug(cstr* target, FunctionStatement* self, int indent);
void FunctionStatement_typecheck(FunctionStatement* self, ErrorCtx* errs);

// Interface statements

typedef struct InterfaceStatement {
    cstr name;
    Type type;
} InterfaceStatement;

void InterfaceStatement_drop(InterfaceStatement* self);
void cstr_append_interface_statement_debug(cstr* target, InterfaceStatement* self, int indent);

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
        STATEMENT_BLOCK,
        STATEMENT_EXPR,
        STATEMENT_RETURN, // also uses expr union member
        STATEMENT_VAR_INIT,
        STATEMENT_FUNCTION,
        STATEMENT_INTERFACE,
        STATEMENT_IF_ELSE,
        STATEMENT_LOOP,
        STATEMENT_BREAK,
    } tag;
    union {
        Block block;
        Expr expr;
        VarInitStatement var_init;
        FunctionStatement function;
        InterfaceStatement interface;
        IfStatement if_else;
        LoopStatement* loop;
    };
} Statement;

#define Statement_noop ((Statement){ .tag = STATEMENT_NOOP })

void Statement_drop(Statement* self);
void cstr_append_statement_debug(cstr* target, Statement* self, int indent);
void Statement_add_names(Statement* self, NameStack* names, ErrorCtx* errs);
void Statement_resolve_names(Statement* self, NameStack* names, ErrorCtx* errs);
void Statement_typecheck(Statement* self, ErrorCtx* errs);

#define i_key_class Statement
#define i_is_forward
#define i_no_clone
#include "stc/vec.h"

// Loop statements

typedef struct LoopStatement {
    Statement initialization; // initialization happens only once, but in the same stack frame as the body
    Expr condition;
    Expr afterthought;
    Statement body;
} LoopStatement;
