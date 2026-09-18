#pragma once

#include "prelude.h"

typedef struct TypeNode TypeNode;
typedef struct Expr Expr;
typedef struct Statement Statement;
typedef struct Block Block;
typedef struct BaselineTestCase BaselineTestCase;

void type_compile_test_info(TypeNode const* me, BaselineTestCase* ctx);
void expr_compile_test_info_inner(Expr* me, BaselineTestCase* ctx);
void expr_compile_test_info(Expr* me, BaselineTestCase* ctx);
void statement_compile_test_info(Statement* me, BaselineTestCase* ctx);
void block_compile_test_info(Block const* me, BaselineTestCase* ctx);
