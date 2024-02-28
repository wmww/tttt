#pragma once

#include "prelude.h"
#include "value.h"

typedef struct BinaryOpExpr BinaryOpExpr;
typedef struct UnaryOpExpr UnaryOpExpr;
typedef struct ErrorCtx ErrorCtx;

typedef struct Expr {
    enum {
        EXPR_LITERAL,
        EXPR_IDENTIFIER,
        EXPR_BINARY_OP,
        EXPR_UNARY_OP,
    } tag;
    union {
        Value literal;
        cstr identifier;
        BinaryOpExpr* binary_op;
        UnaryOpExpr* unary_op;
    } data;
} Expr;

extern const Expr Expr_default;

static inline Expr Expr_literal_new(Value value) {
    return (Expr){ .tag = EXPR_LITERAL, .data.literal = value };
}

static inline Expr Expr_indentifier_new(cstr identifier) {
    return (Expr){ .tag = EXPR_IDENTIFIER, .data.identifier = identifier };
}

static inline Expr Expr_binary_op_new(BinaryOpExpr* op) {
    return (Expr){ .tag = EXPR_BINARY_OP, .data.binary_op = op };
}

static inline Expr Expr_unary_op_new(UnaryOpExpr* op) {
    return (Expr){ .tag = EXPR_UNARY_OP, .data.unary_op = op };
}

void Expr_drop(Expr* self);
cstr Expr_to_cstr(Expr* self, int indent);
Type Expr_get_type_and_typecheck(Expr* self, ErrorCtx* errs);

typedef enum BinaryOp {
    BINARY_OP_PLUS,
    BINARY_OP_MINUS,
    BINARY_OP_STAR,
    BINARY_OP_SLASH,
    BINARY_OP_SINGLE_EQUALS,
    BINARY_OP_DOUBLE_EQUALS,
    BINARY_OP_TRIPLE_EQUALS,
} BinaryOp;

// (BINARY_OP_STAR, BINARY_OP_PLUS) => 1
// (BINARY_OP_PLUS, BINARY_OP_STAR) => -1
// (BINARY_OP_PLUS, BINARY_OP_MINUS) => 0
signed char BinaryOp_cmp_precedence(BinaryOp a, BinaryOp b);

typedef struct BinaryOpExpr {
    BinaryOp op;
    Expr left;
    Expr right;
    bool parenthesized;
} BinaryOpExpr;

#define UNARY_OP_MIN_RIGHT_HAND_OP 100

typedef enum UnaryOp {
    UNARY_OP_BANG,
    UNARY_OP_PLUS,
    UNARY_OP_MINUS,
    UNARY_OP_LEFT_PLUS_PLUS,
    UNARY_OP_LEFT_MINUS_MINUS,
    UNARY_OP_RIGHT_PLUS_PLUS = UNARY_OP_MIN_RIGHT_HAND_OP,
    UNARY_OP_RIGHT_MINUS_MINUS,
} UnaryOp;

typedef struct UnaryOpExpr {
    UnaryOp op;
    Expr expr;
    bool parenthesized;
} UnaryOpExpr;
