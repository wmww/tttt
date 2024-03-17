#pragma once

#include "block.h"
#include "expr.h"
#include "interface.h"
#include "type.h"
#include "callable_type.h"
#include "function_impl.h"

typedef struct ErrorCtx ErrorCtx;
typedef struct StatementCtx StatementCtx;
typedef struct LoopStatement LoopStatement;

// NameInitData is useful for multiple things that can optionally both values and types

typedef struct NameInitData {
    cstr name;
    Span name_span;
    bool type_explicit;
    Type type;
    Span type_span;
    bool value_explicit;
    Expr value;
} NameInitData;

void name_init_data_drop(NameInitData* self);
void cstr_append_name_init_data_debug(cstr* target, NameInitData* self, int indent);

#define i_key_class_names NameInitData, name_init_data
#define i_no_clone
#include "wrap_stc/vec.h"

// Variable initialization

typedef struct VarInitStatement {
    enum {
        VAR_INIT_VAR,
        VAR_INIT_LET,
        VAR_INIT_CONST,
    } keyword;
    VecNameInitData vars;
} VarInitStatement;

void var_init_statement_drop(VarInitStatement* self);
void cstr_append_var_init_statement_debug(cstr* target, VarInitStatement* self, int indent);
void var_init_statement_typecheck(VarInitStatement* self, ErrorCtx* errs);

// Function statements

typedef struct FunctionStatement {
    cstr name;
    FunctionImpl impl;
} FunctionStatement;

// Interface statements

typedef struct InterfaceStatement {
    cstr name;
    Type type;
} InterfaceStatement;

void interface_statement_drop(InterfaceStatement* self);
void cstr_append_interface_statement_debug(cstr* target, InterfaceStatement* self, int indent);

// If statements

typedef struct IfStatement {
    VecExpr conditions;
    // The length of statements should always be equal to conditions, or one greater (in the case of a trailing else)
    VecStatement statements;
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
        Expr expr; // also used for return
        VarInitStatement var_init;
        FunctionStatement function;
        InterfaceStatement interface;
        IfStatement if_else;
        LoopStatement* loop;
        Span break_span;
    };
} Statement;

#define statement_noop ((Statement){ .tag = STATEMENT_NOOP })

void statement_drop(Statement* self);
void cstr_append_statement_debug(cstr* target, Statement* self, int indent);
void statement_add_names(Statement* self, NameStack* names, ErrorCtx* errs);
void statement_resolve_names(Statement* self, NameStack* names, ErrorCtx* errs);
void statement_typecheck(Statement* self, StatementCtx* ctx, ErrorCtx* errs);

#define i_key_class_names Statement, statement
#define i_is_forward
#define i_no_clone
#include "wrap_stc/vec.h"

// Loop statements

typedef struct LoopStatement {
    Statement initialization; // initialization happens only once, but in the same stack frame as the body
    Expr condition;
    Expr afterthought;
    Statement body;
} LoopStatement;

// Statement context

typedef struct StatementCtx {
    VecType returned_types;
    Type const* specified_return;
    bool return_allowed;
    bool break_allowd;
} StatementCtx;

StatementCtx statement_ctx_toplevel();
StatementCtx statement_ctx_function(Type const* specified_return);
void statement_ctx_drop(StatementCtx* self);
