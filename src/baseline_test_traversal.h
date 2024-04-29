#pragma once

#include "prelude.h"

typedef struct Type Type;
typedef struct Expr Expr;
typedef struct Statement Statement;
typedef struct Block Block;
typedef struct BaselineTestCase BaselineTestCase;

void type_compile_test_info(Type* me, BaselineTestCase* ctx);
void expr_compile_test_info(Expr const* me, bool abstract_arrays, BaselineTestCase* ctx);
void statement_compile_test_info(Statement* me, BaselineTestCase* ctx);
void block_compile_test_info(Block* me, BaselineTestCase* ctx);
