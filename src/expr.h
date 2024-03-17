#pragma once

#include "value.h"
#include "span.h"

typedef struct BinaryOpExpr BinaryOpExpr;
typedef struct UnaryOpExpr UnaryOpExpr;
typedef struct DotPropExpr DotPropExpr;
typedef struct AsTypeExpr AsTypeExpr;
typedef struct CallExpr CallExpr;
typedef struct ObjectExprItem ObjectExprItem;
typedef struct vec_object_expr_item ObjectExpr;
typedef struct FunctionImpl FunctionImpl;
typedef struct ErrorCtx ErrorCtx;
typedef struct NameStack NameStack;
typedef struct Expr Expr;

wrap_forward_vec(ObjectExprItem, object_expr_item);
wrap_forward_vec(Expr, expr);

typedef struct Expr {
    Span span;
    int peren_depth;
    enum {
        EXPR_LITERAL,
        EXPR_IDENTIFIER,
        EXPR_BINARY_OP,
        EXPR_UNARY_OP,
        EXPR_DOT_PROP,
        EXPR_AS_TYPE,
        EXPR_CALL,
        EXPR_FUNCTION,
        EXPR_OBJECT,
        EXPR_ARRAY,
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
        AsTypeExpr* as_type;
        CallExpr* call;
        FunctionImpl* function;
        // TODO: perhaps optimize lookup somehow? (currently linear)
        ObjectExpr object;
        VecExpr array;
    };
} Expr;

static inline Expr expr_default(Span span) {
    return (Expr){ .span = span, .tag = EXPR_LITERAL, .literal = value_undefined };
}

static inline Expr expr_literal(Span span, Value value) {
    return (Expr){ .span = span, .tag = EXPR_LITERAL, .literal = value };
}

static inline Expr expr_identifier(Span span, cstr identifier) {
    return (Expr){ .span = span, .tag = EXPR_IDENTIFIER, .identifier.name = identifier };
}

static inline Expr expr_binary_op(Span span, BinaryOpExpr* op) {
    return (Expr){ .span = span, .tag = EXPR_BINARY_OP, .binary_op = op };
}

static inline Expr expr_unary_op(Span span, UnaryOpExpr* op) {
    return (Expr){ .span = span, .tag = EXPR_UNARY_OP, .unary_op = op };
}

static inline Expr expr_dot_prop(Span span, DotPropExpr* dot_prop) {
    return (Expr){ .span = span, .tag = EXPR_DOT_PROP, .dot_prop = dot_prop };
}

static inline Expr expr_as_type(Span span, AsTypeExpr* as_type) {
    return (Expr){ .span = span, .tag = EXPR_AS_TYPE, .as_type = as_type };
}

static inline Expr expr_call(Span span, CallExpr* call) {
    return (Expr){ .span = span, .tag = EXPR_CALL, .call = call };
}

static inline Expr expr_function(Span span, FunctionImpl* function) {
    return (Expr){ .span = span, .tag = EXPR_FUNCTION, .function = function };
}

static inline Expr expr_object(Span span, ObjectExpr object) {
    return (Expr){ .span = span, .tag = EXPR_OBJECT, .object = object };
}

void expr_drop(Expr* self);
void cstr_append_expr_debug(cstr* target, Expr const* self, int indent);
void expr_resolve_names(Expr* self, NameStack* names, ErrorCtx* errs);
Type expr_get_type_and_typecheck(Expr* self, ErrorCtx* errs);

#define i_key_class_names Expr, expr
#define i_no_clone
#define i_is_forward
#include "wrap_stc/vec.h"

// Binary op

// Order based on https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Operator_precedence
typedef enum BinaryOp {
    BINARY_OP_STAR_STAR,

    BINARY_OP_STAR,
    BINARY_OP_SLASH,
    BINARY_OP_MOD,

    BINARY_OP_PLUS,
    BINARY_OP_MINUS,

    BINARY_OP_LESS_LESS,
    BINARY_OP_GREATER_GREATER,
    BINARY_OP_GREATER_GREATER_GREATER,

    BINARY_OP_LESS,
    BINARY_OP_LESS_EQUAL,
    BINARY_OP_GREATER,
    BINARY_OP_GREATER_EQUAL,

    BINARY_OP_DOUBLE_EQUAL,
    BINARY_OP_NOT_EQUAL,
    BINARY_OP_TRIPLE_EQUAL,
    BINARY_OP_TRIPLE_NOT_EQUAL,

    BINARY_OP_AND,

    BINARY_OP_XOR,

    BINARY_OP_OR,

    BINARY_OP_AND_AND,

    BINARY_OP_OR_OR,
    BINARY_OP_EH_EH,

    BINARY_OP_EQUAL,
    BINARY_OP_PLUS_EQUAL,
    BINARY_OP_MINUS_EQUAL,
    BINARY_OP_STAR_STAR_EQUAL,
    BINARY_OP_STAR_EQUAL,
    BINARY_OP_SLASH_EQUAL,
    BINARY_OP_MOD_EQUAL,
    BINARY_OP_LESS_LESS_EQUAL,
    BINARY_OP_GREATER_GREATER_EQUAL,
    BINARY_OP_GREATER_GREATER_GREATER_EQUAL,
    BINARY_OP_AND_EQUAL,
    BINARY_OP_XOR_EQUAL,
    BINARY_OP_OR_EQUAL,
    BINARY_OP_AND_AND_EQUAL,
    BINARY_OP_OR_OR_EQUAL,
    BINARY_OP_EH_EH_EQUAL,

    BINARY_OP_COMMA,
} BinaryOp;

// (BINARY_OP_STAR, BINARY_OP_PLUS) => 1
// (BINARY_OP_PLUS, BINARY_OP_STAR) => -1
// (BINARY_OP_PLUS, BINARY_OP_MINUS) => 0
signed char binary_op_cmp_precedence(BinaryOp a, BinaryOp b);
czview binary_op_get_zv(BinaryOp op);

typedef struct BinaryOpExpr {
    BinaryOp op;
    Expr left;
    Expr right;
} BinaryOpExpr;

// Unary op

#define UNARY_OP_MIN_RIGHT_HAND_OP 100

typedef enum UnaryOp {
    UNARY_OP_BANG,
    UNARY_OP_TILDE,
    UNARY_OP_PLUS,
    UNARY_OP_MINUS,
    UNARY_OP_LEFT_PLUS_PLUS,
    UNARY_OP_LEFT_MINUS_MINUS,
    UNARY_OP_RIGHT_PLUS_PLUS = UNARY_OP_MIN_RIGHT_HAND_OP,
    UNARY_OP_RIGHT_MINUS_MINUS,
} UnaryOp;

czview unary_op_get_zv(UnaryOp op);

typedef struct UnaryOpExpr {
    UnaryOp op;
    Expr expr;
} UnaryOpExpr;

// Dot property

typedef struct DotPropExpr {
    Expr expr;
    cstr prop;
} DotPropExpr;

// Call

typedef struct CallExpr {
    Expr callable;
    VecExpr args;
} CallExpr;

// Object

typedef struct ObjectExprItem {
    cstr name;
    Expr value;
} ObjectExprItem;

void object_expr_item_drop(ObjectExprItem* self);
ObjectExprItem* object_expr_get(ObjectExpr* self, const char* name);

#define i_key_class_names ObjectExprItem, object_expr_item
#define i_no_clone
#define i_is_forward
#include "wrap_stc/vec.h"
