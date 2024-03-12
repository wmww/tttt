#pragma once

#include "prelude.h"

typedef struct Type Type;
typedef struct Expr Expr;
typedef struct Statement Statement;
typedef struct Block Block;
typedef struct BaselineTestCase BaselineTestCase;

void Type_compile_test_info(Type* self, BaselineTestCase* ctx);
void Expr_compile_test_info(Expr* self, BaselineTestCase* ctx);
void Statement_compile_test_info(Statement* self, BaselineTestCase* ctx);
void Block_compile_test_info(Block* self, BaselineTestCase* ctx);
