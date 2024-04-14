#pragma once

#include "block.h"
#include "expr.h"
#include "type.h"
#include "function_impl.h"
#include "type_decl.h"

typedef struct ErrorCtx ErrorCtx;
typedef struct StatementCtx StatementCtx;
typedef struct ForLoopStatement ForLoopStatement;
typedef struct TryCatchStatement TryCatchStatement;
typedef struct ForEachLoopStatement ForEachLoopStatement;

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

void name_init_data_drop(NameInitData* me);
void cstr_append_name_init_data_debug(cstr* target, NameInitData* me, int indent);

#define i_key_class_names NameInitData, name_init_data
#define i_no_clone
#include "wrap_stc/vec.h"

// Variable initialization

typedef struct VariableStatement {
    enum {
        VAR_INIT_VAR,
        VAR_INIT_LET,
        VAR_INIT_CONST,
        VAR_INIT_CATCH, // Used only for the catch expression in try/catch blocks
    } keyword;
    VecNameInitData vars;
} VariableStatement;

void variable_statement_drop(VariableStatement* me);
void cstr_append_variable_statement_debug(cstr* target, VariableStatement* me, int indent);
void variable_statement_add_names(VariableStatement* me, NameStack* names, ErrorCtx* errs);
void variable_statement_resolve_names(VariableStatement* me, NameStack* names, ErrorCtx* errs);
void variable_statement_typecheck(VariableStatement* me, ErrorCtx* errs);

// If statements

typedef struct IfStatement {
    VecExpr conditions;
    // The length of statements should always be equal to conditions, or one greater (in the case of a trailing else)
    VecStatement statements;
} IfStatement;

// Statements

typedef enum DeclExported {
    DECL_EXPORTED_NO = 0,
    DECL_EXPORTED_YES,
    DECL_EXPORTED_YES_DEFAULT,
} DeclExported;

typedef struct Statement {
    enum {
        STATEMENT_NOOP,
        STATEMENT_BLOCK,
        STATEMENT_IF_ELSE,
        STATEMENT_FOR_LOOP,
        STATEMENT_FOR_EACH_LOOP,
        STATEMENT_TRY_CATCH,
        STATEMENT_BREAK,
        STATEMENT_EXPR,
        STATEMENT_EXPORT_DEFAULT_EXPR, // also uses expr union member
        STATEMENT_RETURN_EXPR, // also uses expr union member
        STATEMENT_DECL_VARIABLE,
        STATEMENT_DECL_FUNCTION,
        STATEMENT_DECL_CLASS,
        STATEMENT_DECL_TYPE,
    } tag;
    union {
        Block block;
        Expr expr; // also used for return and export default expressions
        IfStatement if_else;
        ForLoopStatement* for_loop;
        ForEachLoopStatement* for_each_loop;
        TryCatchStatement* try_catch;
        Span break_span;
        struct {
            DeclExported exported;
            union {
                VariableStatement variable;
                FunctionImpl function;
                ClassDecl* class;
                TypeDecl type;
            };
        } decl;
    };
} Statement;

#define statement_noop ((Statement){ .tag = STATEMENT_NOOP })

static inline DeclExported statement_get_exported(Statement* me) {
    switch (my tag) {
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_TYPE: return my decl.exported;
        case STATEMENT_EXPORT_DEFAULT_EXPR: return DECL_EXPORTED_YES_DEFAULT;
        default: return DECL_EXPORTED_NO;
    }
}

void statement_drop(Statement* me);
void cstr_append_statement_debug(cstr* target, Statement* me, int indent);
void statement_add_names(Statement* me, NameStack* names, ErrorCtx* errs);
void statement_resolve_names(Statement* me, NameStack* names, ErrorCtx* errs);
void statement_typecheck(Statement* me, StatementCtx* ctx, ErrorCtx* errs);

#define i_key_class_names Statement, statement
#define i_is_forward
#define i_no_clone
#include "wrap_stc/vec.h"

// Loop statements

typedef struct ForLoopStatement {
    Statement initialization; // initialization happens only once, but in the same stack frame as the body
    Expr condition;
    Expr afterthought;
    Statement body;
} ForLoopStatement;

typedef struct ForEachLoopStatement {
    enum {
        FOR_EACH_LOOP_IN,
        FOR_EACH_LOOP_OF,
    } keyword;
    struct {
        enum {
            FOR_EACH_LOOP_STATEMENT,
            FOR_EACH_LOOP_EXPR,
        } tag;
        union {
            VariableStatement statement;
            Expr expr;
        };
    } variable;
    Expr iterable;
    Statement body;
} ForEachLoopStatement;

// Try-catch-finally statements

typedef struct TryCatchStatement {
    Block try_block;
    VariableStatement catch_var;
    Block catch_block;
    Block finally_block;
} TryCatchStatement;

// Statement context

typedef struct StatementCtx {
    VecType returned_types;
    Type const* specified_return;
    bool return_allowed;
    bool break_allowd;
} StatementCtx;

StatementCtx statement_ctx_toplevel();
StatementCtx statement_ctx_function(Type const* specified_return);
void statement_ctx_drop(StatementCtx* me);
