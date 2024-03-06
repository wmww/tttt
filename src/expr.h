#pragma once

#include "prelude.h"
#include "value.h"

typedef struct BinaryOpExpr BinaryOpExpr;
typedef struct UnaryOpExpr UnaryOpExpr;
typedef struct DotPropExpr DotPropExpr;
typedef struct CallExpr CallExpr;
typedef struct ObjectExprItem ObjectExprItem;
typedef struct vec_ObjectExprItem ObjectExpr;
typedef struct FunctionImpl FunctionImpl;
typedef struct ErrorCtx ErrorCtx;
typedef struct NameStack NameStack;

forward_vec(vec_ObjectExprItem, ObjectExprItem);

typedef struct Expr {
    enum {
        EXPR_LITERAL,
        EXPR_IDENTIFIER,
        EXPR_BINARY_OP,
        EXPR_UNARY_OP,
        EXPR_DOT_PROP,
        EXPR_CALL,
        EXPR_FUNCTION,
        EXPR_OBJECT,
    } tag;
    union {
        Value literal;
        struct {
            cstr name;
            Type const* type;
        } identifier;
        BinaryOpExpr* binary_op;
        UnaryOpExpr* unary_op;
        DotPropExpr* dot_prop;
        CallExpr* call;
        FunctionImpl* function;
        // TODO: perhaps optimize lookup somehow? (currently linear)
        ObjectExpr object;
    };
} Expr;

extern const Expr Expr_default;

static inline Expr Expr_literal_new(Value value) {
    return (Expr){ .tag = EXPR_LITERAL, .literal = value };
}

static inline Expr Expr_indentifier_new(cstr identifier) {
    return (Expr){ .tag = EXPR_IDENTIFIER, .identifier.name = identifier };
}

static inline Expr Expr_binary_op_new(BinaryOpExpr* op) {
    return (Expr){ .tag = EXPR_BINARY_OP, .binary_op = op };
}

static inline Expr Expr_unary_op_new(UnaryOpExpr* op) {
    return (Expr){ .tag = EXPR_UNARY_OP, .unary_op = op };
}

static inline Expr Expr_dot_prop_new(DotPropExpr* dot_prop) {
    return (Expr){ .tag = EXPR_DOT_PROP, .dot_prop = dot_prop };
}

static inline Expr Expr_call_new(CallExpr* call) {
    return (Expr){ .tag = EXPR_CALL, .call = call };
}

static inline Expr Expr_function_new(FunctionImpl* function) {
    return (Expr){ .tag = EXPR_FUNCTION, .function = function };
}

static inline Expr Expr_object_new(ObjectExpr object) {
    return (Expr){ .tag = EXPR_OBJECT, .object = object };
}

void Expr_drop(Expr* self);
void cstr_append_expr_debug(cstr* target, Expr const* self, int indent);
void Expr_resolve_names(Expr* self, NameStack* names, ErrorCtx* errs);
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
    BINARY_OP_SINGLE_EQUAL,
    BINARY_OP_DOUBLE_EQUAL,
    BINARY_OP_TRIPLE_EQUAL,
    BINARY_OP_LESS,
    BINARY_OP_GREATER,
    BINARY_OP_LESS_EQUAL,
    BINARY_OP_GREATER_EQUAL,
    BINARY_OP_NOT_EQUAL,
    BINARY_OP_DOUBLE_NOT_EQUAL,
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
