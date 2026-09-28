#pragma once

#include "prelude.h"

// A read-only pre-order walk over every expression under an AST node, like tsc's forEachChild recursion restricted to
// expressions: statements, function bodies and parameters, classes, bindings' default values and initializers, JSX.
// Types (TypeNodes) are not entered. `visit` returns true to stop the walk, which then returns true
typedef struct Expr Expr;
typedef struct Statement Statement;
typedef bool (*AstExprVisit)(Expr const* expr, void* arg);

bool ast_walk_expr(Expr const* expr, AstExprVisit visit, void* arg);
bool ast_walk_statement(Statement const* statement, AstExprVisit visit, void* arg);
