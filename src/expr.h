#pragma once

#include "value.h"
#include "span.h"
#include "lazy_type.h"

typedef struct BinaryOpExpr BinaryOpExpr;
typedef struct UnaryOpExpr UnaryOpExpr;
typedef struct PropertyExpr PropertyExpr;
typedef struct AsTypeExpr AsTypeExpr;
typedef struct TernaryExpr TernaryExpr;
typedef struct CallExpr CallExpr;
typedef struct ObjectExprItem ObjectExprItem;
typedef struct vec_object_expr_item ObjectExpr;
typedef struct FunctionImpl FunctionImpl;
typedef struct ClassDecl ClassDecl;
typedef struct ErrorCtx ErrorCtx;
typedef struct NameStack NameStack;
typedef struct Expr Expr;
typedef struct NamedVariable NamedVariable;

wrap_forward_vec(ObjectExprItem, object_expr_item);
wrap_forward_vec(Expr, expr);

typedef struct Expr {
    Span inner_span; // Just the content (also does not include identifier generic params)
    Span outer_span; // Includes parentheses
    bool* currently_getting_type;
    enum {
        EXPR_IMPLICIT_ANY = 0,
        EXPR_LITERAL,
        EXPR_IDENTIFIER,
        EXPR_BINARY_OP,
        EXPR_UNARY_OP,
        EXPR_PROPERTY,
        EXPR_AS_TYPE,
        EXPR_TERNARY,
        EXPR_CALL,
        EXPR_FUNCTION,
        EXPR_OBJECT,
        EXPR_ARRAY,
        EXPR_CLASS_DECL,
    } tag;
    union {
        Value literal;
        struct {
            Span name;
            NamedVariable* var;
            VecSpannedType generic_params;
        } identifier;
        BinaryOpExpr* binary_op;
        UnaryOpExpr* unary_op;
        PropertyExpr* property;
        AsTypeExpr* as_type;
        TernaryExpr* ternary;
        CallExpr* call;
        FunctionImpl* function;
        ObjectExpr object;
        VecExpr array;
        ClassDecl* class_decl;
    };
    LazyType lazy_type;
} Expr;

#define EXPR_DEFAULT(span) \
    .inner_span = span, \
    .outer_span = span, \
    .lazy_type.tag = LAZY_TYPE_EXPR

static inline Expr expr_literal(Span span, Value value) {
    return (Expr){ .tag = EXPR_LITERAL, .literal = value, EXPR_DEFAULT(span) };
}

static inline Expr expr_identifier(Span name) {
    return (Expr){ .tag = EXPR_IDENTIFIER, .identifier.name = name, EXPR_DEFAULT(name) };
}

static inline Expr expr_binary_op(Span span, BinaryOpExpr* op) {
    return (Expr){ .tag = EXPR_BINARY_OP, .binary_op = op, EXPR_DEFAULT(span) };
}

static inline Expr expr_unary_op(Span span, UnaryOpExpr* op) {
    return (Expr){ .tag = EXPR_UNARY_OP, .unary_op = op, EXPR_DEFAULT(span) };
}

static inline Expr expr_property(Span span, PropertyExpr* property) {
    return (Expr){ .tag = EXPR_PROPERTY, .property = property, EXPR_DEFAULT(span) };
}

static inline Expr expr_as_type(Span span, AsTypeExpr* as_type) {
    return (Expr){ .tag = EXPR_AS_TYPE, .as_type = as_type, EXPR_DEFAULT(span) };
}

static inline Expr expr_ternary(Span span, TernaryExpr* ternary) {
    return (Expr){ .tag = EXPR_TERNARY, .ternary = ternary, EXPR_DEFAULT(span) };
}

static inline Expr expr_call(Span span, CallExpr* call) {
    return (Expr){ .tag = EXPR_CALL, .call = call, EXPR_DEFAULT(span) };
}

static inline Expr expr_function(Span span, FunctionImpl* function) {
    return (Expr){ .tag = EXPR_FUNCTION, .function = function, EXPR_DEFAULT(span) };
}

static inline Expr expr_object(Span span, ObjectExpr object) {
    return (Expr){ .tag = EXPR_OBJECT, .object = object, EXPR_DEFAULT(span) };
}

static inline Expr expr_class_decl(Span span, ClassDecl* class_decl) {
    return (Expr){ .tag = EXPR_CLASS_DECL, .class_decl = class_decl, EXPR_DEFAULT(span) };
}

static inline Expr expr_undefined(Span span) {
    return expr_literal(span, value_undefined);
}

static inline Expr expr_implicit_any(Span span) {
    return (Expr){
        .tag = EXPR_IMPLICIT_ANY,
        .inner_span = span,
        .outer_span = span,
        .lazy_type.tag = LAZY_TYPE_EVALUATED
    };
}

#undef EXPR_DEFAULT

static inline bool expr_is_perenthisized(Expr* me) {
    return span_start_ptr(my inner_span) != span_start_ptr(my outer_span);
}

extern const Expr expr_implicit_any_no_span;
extern const int as_type_precedence;
extern const int ternary_precedence;
extern const int new_keyword_precedence;

void expr_drop(Expr* me);
void expr_resolve_names(Expr* me, NameStack* names, ErrorCtx* errs);
// PERF: maybe make this return a lazy type?
Type expr_get_type_and_typecheck(Expr const* me, ErrorCtx* errs);

#define i_key_class_names Expr, expr
#define i_no_clone
#define i_is_forward
#include "wrap_stc/vec.h"

#define i_key_class_names Expr, expr
#define i_is_forward
#include "wrap_stc/arc.h"

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
    BINARY_OP_IN,
    BINARY_OP_INSTANCEOF,

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

int binary_op_get_precedence(BinaryOp op);
czview binary_op_get_zv(BinaryOp op);

typedef struct BinaryOpExpr {
    BinaryOp op;
    Expr left;
    Expr right;
} BinaryOpExpr;

// Unary op

#define UNARY_OP_MIN_RIGHT_HAND_OP 100

typedef enum UnaryOp {
    UNARY_OP_LEFT_BANG,
    UNARY_OP_TYPEOF,
    UNARY_OP_TILDE,
    UNARY_OP_PLUS,
    UNARY_OP_MINUS,
    UNARY_OP_LEFT_PLUS_PLUS,
    UNARY_OP_LEFT_MINUS_MINUS,
    UNARY_OP_RIGHT_PLUS_PLUS = UNARY_OP_MIN_RIGHT_HAND_OP,
    UNARY_OP_RIGHT_MINUS_MINUS,
    UNARY_OP_RIGHT_BANG,
} UnaryOp;

int unary_op_get_precedence(UnaryOp op);
czview unary_op_get_zv(UnaryOp op);

typedef struct UnaryOpExpr {
    UnaryOp op;
    Expr expr;
} UnaryOpExpr;

// Property

typedef struct PropertyExpr {
    Expr expr;
    Expr prop;
    enum {
        PROPERTY_EXPR_DOT,
        PROPERTY_EXPR_BRACKET,
    } form;
} PropertyExpr;

// As type

typedef struct AsTypeExpr {
    Expr expr;
    Type type;
    bool angle_bracket_syntax; // If the <type>value syntax was used
} AsTypeExpr;

// Ternary

typedef struct TernaryExpr {
    Expr condition;
    Expr yes;
    Expr no;
} TernaryExpr;

// Call

typedef struct CallExpr {
    bool uses_new;
    bool is_extends;
    Expr callable;
    // PERF: types of each argument should be cached
    VecExpr args;
} CallExpr;

// Object

typedef struct ObjectExprItem {
    cstr name_string;
    Span name_span;
    Expr value;
} ObjectExprItem;

void object_expr_item_drop(ObjectExprItem* me);

#define i_key_class_names ObjectExprItem, object_expr_item
#define i_no_clone
#define i_is_forward
#include "wrap_stc/vec.h"
