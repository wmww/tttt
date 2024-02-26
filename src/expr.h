#pragma once

#include "prelude.h"
#include "value.h"

typedef struct BinaryOp BinaryOp;

typedef struct Expr {
    enum {
        EXPR_LITERAL,
        EXPR_BINARY_OP,
    } tag;
    union {
        Value literal;
        BinaryOp* binary_op;
    } data;
} Expr;

typedef struct BinaryOp {
    enum {
        BINARY_OP_PLUS,
        BINARY_OP_MINUS,
    } op;
    Expr a;
    Expr b;
} BinaryOp;

static inline Expr Expr_literal_new(Value value) { return (Expr){ .tag = EXPR_LITERAL, .data.literal = value }; }
static inline Expr Expr_binary_op_new(BinaryOp* op) { return (Expr){ .tag = EXPR_BINARY_OP, .data.binary_op = op }; }
static inline BinaryOp BinaryOp_op_new(Expr a, typeof((BinaryOp){0}.op) op, Expr b) { return (BinaryOp){ .a = a, .op = op, .b = b }; }

void Expr_drop(Expr* self);
cstr Expr_to_cstr(Expr* self, int indent);
