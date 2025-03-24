#pragma once

#include "block.h"
#include "expr.h"
#include "type.h"
#include "function_impl.h"
#include "interface_decl.h"
#include "binding.h"
#include "scope.h"

typedef struct ErrorCtx ErrorCtx;
typedef struct EnumDecl EnumDecl;
typedef struct SourceDirectiveStatement SourceDirectiveStatement;
typedef struct ForLoopStatement ForLoopStatement;
typedef struct TryCatchStatement TryCatchStatement;
typedef struct ForEachLoopStatement ForEachLoopStatement;
typedef struct SwitchStatement SwitchStatement;
typedef struct PortStatement PortStatement;

typedef struct DeclStatementProps {
    enum {
        DECL_EXPORTED_NO = 0,
        DECL_EXPORTED_YES,
        DECL_EXPORTED_YES_DEFAULT,
    } exported;
    bool declared;
} DeclStatementProps;

// Variable initialization

typedef struct VariableStatement {
    enum {
        VAR_INIT_VAR,
        VAR_INIT_LET,
        VAR_INIT_CONST,
    } keyword;
    VecInitializedBinding initializers;
} VariableStatement;

void variable_statement_drop(VariableStatement* me);
void variable_statement_add_names(VariableStatement* me, Scope* scope, bool exported);
void variable_statement_resolve_names(VariableStatement* me, ScopeStack* names, ErrorCtx* errs);
void variable_statement_typecheck(VariableStatement* me, bool declared, ErrorCtx* errs);

// Type declaration

typedef struct TypeDecl {
    Span name;
    Span full_span;
    Generic generic;
    Type type;
    ArcDeclaration declaration;
} TypeDecl;

void type_decl_drop(TypeDecl* me);
void type_decl_add_names(TypeDecl* me, Scope* scope, DeclStatementProps props);
void type_decl_typecheck(TypeDecl* me, ErrorCtx* errs);

// Scope declaration

typedef struct NamespaceDecl {
    Span name;
    Span full_span;
    bool is_global;
    Block contents;
    Scope inner_scope;
    Scope* global_scope; // Set only if global
    Type value_type;
    ArcDeclaration declaration;
} NamespaceDecl;

void namespace_decl_drop(NamespaceDecl* me);
void namespace_decl_add_names(NamespaceDecl* me, Scope* scope, DeclStatementProps props);
void namespace_decl_resolve_names(NamespaceDecl* me, ScopeStack* names, ErrorCtx* errs);
void namespace_decl_typecheck(NamespaceDecl* me, ErrorCtx* errs);

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
        STATEMENT_SOURCE_DIRECTIVE,
        STATEMENT_BLOCK,
        STATEMENT_IF_ELSE,
        STATEMENT_FOR_LOOP,
        STATEMENT_FOR_EACH_LOOP,
        STATEMENT_SWITCH,
        STATEMENT_TRY_CATCH,
        STATEMENT_BREAK,
        STATEMENT_CONTINUE,
        STATEMENT_EXPR,
        STATEMENT_EXPORT_DEFAULT_EXPR, // also uses expr union member
        STATEMENT_RETURN_EXPR, // also uses expr union member
        STATEMENT_THROW_EXPR, // also uses expr union member
        STATEMENT_IMPORT_EXPORT,
        STATEMENT_DECL_VARIABLE,
        STATEMENT_DECL_FUNCTION,
        STATEMENT_DECL_ENUM,
        STATEMENT_DECL_CLASS,
        STATEMENT_DECL_TYPE,
        STATEMENT_DECL_INTERFACE,
        STATEMENT_DECL_NAMESPACE,
    } tag;
    union {
        SourceDirectiveStatement* source_directive;
        Block block;
        Expr expr; // also used for return and export default expressions
        IfStatement if_else;
        ForLoopStatement* for_loop;
        ForEachLoopStatement* for_each_loop;
        SwitchStatement* switch_;
        TryCatchStatement* try_catch;
        Span keyword_span; // Used for continue and break
        PortStatement* import_export;
        struct {
            DeclStatementProps props;
            union {
                VariableStatement variable;
                struct {
                    FunctionImpl* impl;
                } function;
                EnumDecl* enum_;
                ClassDecl* class;
                TypeDecl type;
                InterfaceDecl* interface;
                NamespaceDecl namespace;
            };
        } decl;
    };
} Statement;

#define statement_noop ((Statement){ .tag = STATEMENT_NOOP })

static inline bool statement_tag_is_decl(typeof((Statement){}.tag) tag) {
    switch (tag) {
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_ENUM:
        case STATEMENT_DECL_CLASS:
        case STATEMENT_DECL_TYPE: return true;
        default: return false;
    }
}

static inline typeof(((DeclStatementProps){}).exported) statement_get_exported(Statement const* me) {
    if (statement_tag_is_decl(my tag)) {
        return my decl.props.exported;
    } else if (my tag == STATEMENT_EXPORT_DEFAULT_EXPR) {
        return DECL_EXPORTED_YES_DEFAULT;
    } else {
        return DECL_EXPORTED_NO;
    }
}

void statement_drop(Statement* me);
void statement_add_names(Statement* me, Scope* scope);
void statement_resolve_names(Statement* me, ScopeStack* names, ErrorCtx* errs);
Type statement_evaluate_return_type(Statement* me);
void statement_typecheck(Statement* me, BlockTypecheckCtx ctx, ErrorCtx* errs);

#define i_key_class_names Statement, statement
#define i_is_forward
#define i_no_clone
#include "wrap_stc/vec.h"

// Source directive statement (the XML thingys defined at the top level)

typedef struct SourceDirectiveStatement {
    enum {
        SOURCE_DIRECTIVE_REFERENCE_PATH,
        SOURCE_DIRECTIVE_REFERENCE_LIB,
        SOURCE_DIRECTIVE_REFERENCE_TYPES,
    } tag;
    cstr path;
    Span path_span;
    bool preserve;
} SourceDirectiveStatement;

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
    LazyType var_type;
    Expr iterable;
    Statement body;
} ForEachLoopStatement;

czview for_each_loop_statement_keyword_str(ForEachLoopStatement const* me);
Type for_each_loop_statement_evaluate_var_type(ForEachLoopStatement* me);

// Switch statement

typedef struct SwitchCase {
    bool is_pattern; // else default
    union {
        Expr pattern;
        Span default_span;
    };
    VecStatement statements;
} SwitchCase;

void switch_case_drop(SwitchCase* me);

#define i_key_class_names SwitchCase, switch_case
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct SwitchStatement {
    Expr needle;
    VecSwitchCase cases;
} SwitchStatement;

// Try-catch-finally statement

typedef struct TryCatchStatement {
    Block try_block;
    Binding catch_var;
    Block catch_block;
    Block finally_block;
} TryCatchStatement;
