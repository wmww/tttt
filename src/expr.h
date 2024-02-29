#pragma once

#include "prelude.h"
#include "value.h"

typedef struct BinaryOpExpr BinaryOpExpr;
typedef struct UnaryOpExpr UnaryOpExpr;
typedef struct DotPropExpr DotPropExpr;
typedef struct CallExpr CallExpr;
typedef struct ObjectExprItem ObjectExprItem;
typedef struct vec_ObjectExprItem ObjectExpr;
typedef struct ErrorCtx ErrorCtx;

forward_vec(vec_ObjectExprItem, ObjectExprItem);

typedef struct Expr {
    enum {
        EXPR_LITERAL,
        EXPR_IDENTIFIER,
        EXPR_BINARY_OP,
        EXPR_UNARY_OP,
        EXPR_DOT_PROP,
        EXPR_CALL,
        EXPR_OBJECT,
    } tag;
    union {
        Value literal;
        cstr identifier;
        BinaryOpExpr* binary_op;
        UnaryOpExpr* unary_op;
        DotPropExpr* dot_prop;
        CallExpr* call;
        // TODO: perhaps optimize lookup somehow? (currently linear)
        ObjectExpr object;
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

static inline Expr Expr_dot_prop_new(DotPropExpr* dot_prop) {
    return (Expr){ .tag = EXPR_DOT_PROP, .data.dot_prop = dot_prop };
}

static inline Expr Expr_call_new(CallExpr* call) {
    return (Expr){ .tag = EXPR_CALL, .data.call = call };
}

static inline Expr Expr_object_new(ObjectExpr object) {
    return (Expr){ .tag = EXPR_OBJECT, .data.object = object };
}

void Expr_drop(Expr* self);
cstr Expr_to_cstr(Expr const* self, int indent);
Type Expr_get_type_and_typecheck(Expr* self, ErrorCtx* errs);

#define i_key_class Expr
#define i_no_clone
#include "stc/vec.h"

// Binary op

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

// Unary op

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

// Dot property

typedef struct DotPropExpr {
    Expr expr;
    cstr prop;
} DotPropExpr;

// Call

typedef struct CallExpr {
    Expr callable;
    vec_Expr args;
} CallExpr;

// Object

typedef struct ObjectExprItem {
    cstr name;
    Expr value;
} ObjectExprItem;

void ObjectExprItem_drop(ObjectExprItem* self);
ObjectExprItem* ObjectExpr_get(ObjectExpr* self, const char* name);

#define i_key_class ObjectExprItem
#define i_no_clone
#define i_is_forward
#include "stc/vec.h"
