#pragma once

#include "value.h"
#include "span.h"
#include "lazy_type.h"

typedef struct BinaryOpExpr BinaryOpExpr;
typedef struct UnaryOpExpr UnaryOpExpr;
typedef struct PropertyExpr PropertyExpr;
typedef struct GenericParamsExpr GenericParamsExpr;
typedef struct AsTypeExpr AsTypeExpr;
typedef struct TernaryExpr TernaryExpr;
typedef struct CallExpr CallExpr;
typedef struct TemplateExpr TemplateExpr;
typedef struct ObjectExprItem ObjectExprItem;
typedef struct vec_object_expr_item ObjectExpr;
typedef struct FunctionImpl FunctionImpl;
typedef struct ClassDecl ClassDecl;
typedef struct ErrorCtx ErrorCtx;
typedef struct Expr Expr;

g_declare_vec(ObjectExprItem, object_expr_item);
g_declare_vec(Expr, expr);

typedef struct FlowNode FlowNode; // flow.h

typedef struct IdentifierExpr {
    Span name;
    // What the checker goes by: the symbol that has the meaning the name is used with (aliases resolved). Cached next
    // to `symbol` so that checking never chases alias chains. For `this`: see this_kind
    Symbol const* target;
    Symbol const* symbol; // The symbol the name found, which may be an alias (the symbols baseline shows that one)
    ModuleRef* module_ref; // `import("./a")`: the module, the identifier is the `import` keyword
    // The control flow node that was current when the reference is evaluated (`this` and `super` have one too), null
    // in unreachable code
    FlowNode* flow;
    uint8_t keyword; // KeywordIdentifier
    uint8_t this_kind; // ThisKind
    uint8_t assignment_target; // FlowAssignmentKind: the reference is being assigned to, so it has its declared type
    // The target of `x = <a shift, additive or multiplicative expression>` (tsc's isInCompoundLikeAssignment): it
    // has the base type of its declared type, so `let s = empty; s = s + "a"` is fine with `empty: ""`
    bool compound_like_target;
    // Never "used before assigned": part of `typeof x` written as a type, or inside the binding pattern that declares it
    bool assume_initialized;
    // A variable referenced from another flow container (function) than the one it's declared in (tsc's
    // isOuterVariable), as far as the resolve walk knows (a `var` whose declaration comes later isn't)
    bool outer_reference;
    // The object of a property access or a callee (tsc's isConstraintPosition): a generic type with a union constraint
    // is narrowed as its constraint
    bool constraint_position;
    // `super.x` / `super[x]`: only there does `super` have a type yet (TODO: `super(...)` against the base
    // constructor's signatures, until then its callee is any)
    bool super_member;
    // `this` in a function that isn't a method or arrow function (THIS_ANY): that function, whose `this` may be
    // contextually typed (ty_contextual_this_type())
    FunctionImpl const* this_function;
} IdentifierExpr;

void identifier_expr_drop(IdentifierExpr* me);
void identifier_expr_lookup_names(IdentifierExpr* me, Scope* names, ErrorCtx* errs);
Ty const* identifier_expr_evaluate_type(IdentifierExpr const* me);
void identifier_expr_typecheck(IdentifierExpr const* me, ErrorCtx* errs);

typedef struct Expr {
    Span inner_span; // Just the content (also does not include identifier generic params)
    Span outer_span; // Includes parentheses
    enum {
        EXPR_IMPLICIT_ANY = 0,
        EXPR_LITERAL,
        EXPR_IDENTIFIER,
        EXPR_BINARY_OP,
        EXPR_UNARY_OP,
        EXPR_PROPERTY,
        EXPR_GENERIC_PARAMS,
        EXPR_AS_TYPE,
        EXPR_TERNARY,
        EXPR_CALL,
        EXPR_FUNCTION,
        EXPR_OBJECT,
        EXPR_ARRAY,
        EXPR_CLASS_DECL,
        EXPR_SPREAD, // `...a`, only appears directly in array literals and call arguments
        EXPR_REGEX, // `/a+/g`, the span is all there is to it
        EXPR_TEMPLATE, // Template literal with substitutions and/or a tag. Plain `abc` is a string EXPR_LITERAL
    } tag;
    union {
        Value literal;
        IdentifierExpr identifier;
        BinaryOpExpr* binary_op;
        UnaryOpExpr* unary_op;
        PropertyExpr* property;
        GenericParamsExpr* generic_params;
        AsTypeExpr* as_type;
        TernaryExpr* ternary;
        CallExpr* call;
        FunctionImpl* function;
        ObjectExpr object;
        VecExpr array;
        ClassDecl* class_decl;
        Expr* spread;
        TemplateExpr* template_;
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

static inline Expr expr_identifier(IdentifierExpr identifier_expr) {
    return (Expr){ .tag = EXPR_IDENTIFIER, .identifier = identifier_expr, EXPR_DEFAULT(identifier_expr.name) };
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

static inline Expr expr_generic_params(Span span, GenericParamsExpr* generic_params) {
    return (Expr){ .tag = EXPR_GENERIC_PARAMS, .generic_params = generic_params, EXPR_DEFAULT(span) };
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

static inline Expr expr_spread(Span span, Expr* operand) {
    return (Expr){ .tag = EXPR_SPREAD, .spread = operand, EXPR_DEFAULT(span) };
}

static inline Expr expr_template(Span span, TemplateExpr* template_) {
    return (Expr){ .tag = EXPR_TEMPLATE, .template_ = template_, EXPR_DEFAULT(span) };
}

static inline Expr expr_regex(Span span) {
    return (Expr){ .tag = EXPR_REGEX, EXPR_DEFAULT(span) };
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

static inline bool expr_is_perenthisized(Expr const* me) {
    return span_start_ptr(my inner_span) != span_start_ptr(my outer_span);
}

// Should only be used in the typecheck phase
static inline Ty const* expr_get_type(Expr* me) {
    return lazy_type_get(&my lazy_type);
}

extern const Expr expr_implicit_any_no_span;
extern const int as_type_precedence;
extern const int ternary_precedence;
extern const int new_keyword_precedence;

void expr_drop(Expr* me);
void expr_lookup_names(Expr* me, Scope* names, ErrorCtx* errs);
// As the condition of a statement: control goes to one of the labels
void expr_lookup_names_condition(Expr* me, Scope* names, ErrorCtx* errs, FlowNode* true_target, FlowNode* false_target);
// As an expression statement, where a call can be an assertion
void expr_lookup_names_statement(Expr* me, Scope* names, ErrorCtx* errs);
// `for (me of xs)`: what the loop assigns to (already walked)
typedef struct ForEachLoopStatement ForEachLoopStatement;
void expr_flow_assign_for_each(Expr const* me, Scope* names, ForEachLoopStatement const* for_each);
Ty const* expr_evaluate_type(Expr const* me); // Should only be used by LazyType
void expr_typecheck(Expr const* me, ErrorCtx* errs);

#define g_class_key Expr, expr
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>

#define g_class_key Expr, expr
#define g_declared
#include <cleat/generic/arc.h>

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
/// What tsc's getEffectiveCheckNode checks and reports at: `satisfies` skipped (and parentheses: use its inner_span)
Expr const* expr_effective_check_node(Expr const* me);
zsview binary_op_get_zv(BinaryOp op);

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
    UNARY_OP_VOID,
    UNARY_OP_TILDE,
    UNARY_OP_PLUS,
    UNARY_OP_MINUS,
    UNARY_OP_LEFT_PLUS_PLUS,
    UNARY_OP_LEFT_MINUS_MINUS,
    UNARY_OP_DELETE,
    UNARY_OP_AWAIT,
    UNARY_OP_YIELD, // The operand is EXPR_IMPLICIT_ANY for a bare `yield`
    UNARY_OP_YIELD_STAR,
    UNARY_OP_RIGHT_PLUS_PLUS = UNARY_OP_MIN_RIGHT_HAND_OP,
    UNARY_OP_RIGHT_MINUS_MINUS,
    UNARY_OP_RIGHT_BANG,
} UnaryOp;

int unary_op_get_precedence(UnaryOp op);
zsview unary_op_get_zv(UnaryOp op);

typedef struct UnaryOpExpr {
    UnaryOp op;
    Expr expr;
    FunctionImpl const* generator; // `yield`: the generator it is in (set when resolving), null if there's none
} UnaryOpExpr;

// Property

typedef struct PropertyExpr {
    Expr expr;
    Expr prop;
    enum {
        PROPERTY_EXPR_DOT,
        PROPERTY_EXPR_BRACKET,
    } form;
    bool optional_chain;
    uint8_t assignment_target; // FlowAssignmentKind
    bool compound_like_target; // Like IdentifierExpr.compound_like_target
    FlowNode* flow; // Like IdentifierExpr.flow
    // The innermost class it is in (member accessibility, see ClassDecl.outer_class for the others), and the class
    // whose constructor is the function it is directly in (where `this.x = ` may write a readonly x)
    ClassDecl const* enclosing_class;
    Symbol const* in_constructor_of;
    // What `this` is where it is, if not a class: a `this` parameter, or a function with a contextual `this` (a
    // protected member is accessible when that is a derived class, tsc's getEnclosingClassFromThisParameter)
    Symbol const* this_parameter;
    FunctionImpl const* this_function;
} PropertyExpr;

// Generic params

typedef struct GenericParamsExpr {
    Expr expr;
    VecSpannedType params;
} GenericParamsExpr;

// As type

typedef struct AsTypeExpr {
    Expr expr;
    TypeNode type;
    bool angle_bracket_syntax; // If the <type>value syntax was used
    Span keyword_span; // `as` or `satisfies`
    bool is_satisfies; // `expr satisfies type`, which checks the expression against the type but doesn't change it
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
    bool optional_chain; // `f?.()`
    ClassDecl const* enclosing_class; // `new C()` of a private or protected constructor: see PropertyExpr
    Expr callable;
    // PERF: types of each argument should be cached
    VecExpr args;
    // The initializer of a declaration that can have a unique symbol type: `Symbol()` makes one (set when resolving)
    UniqueSymbolDecl unique_symbol;
} CallExpr;

// Template

typedef struct TemplateExpr {
    Expr* tag; // Owned, null if the template isn't tagged
    Span template_span; // Without the tag
    VecExpr substitutions;
    cstr plain_string; // The contents, if there are no substitutions (which only happens for tagged templates)
    VecCStr texts; // Untagged: the cooked texts around the substitutions
} TemplateExpr;

// Object

typedef struct ObjectExprItem {
    enum {
        OBJECT_ITEM_PROPERTY, // `a: 1`
        OBJECT_ITEM_SHORTHAND, // `a`, value is an identifier expr with the same span as the name
        OBJECT_ITEM_METHOD, // `a() {}`, value is a function expr
        OBJECT_ITEM_GETTER, // `get a() {}`, value is a function expr
        OBJECT_ITEM_SETTER, // `set a(v) {}`, value is a function expr
        OBJECT_ITEM_SPREAD, // `...a`, no name
    } tag;
    Span span;
    cstr name_string; // Empty for spreads and computed keys
    Span name_span; // Includes the brackets of a computed key
    bool name_is_value;
    Expr* computed_key; // Owned, null unless the key is `[expr]`
    Expr value;
    bool shorthand_default; // `{ a = 1 }` (a destructuring assignment target), the default isn't kept
} ObjectExprItem;

void object_expr_item_drop(ObjectExprItem* me);

#define g_class_key ObjectExprItem, object_expr_item
#define g_opt (c_no_atomic)
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>
