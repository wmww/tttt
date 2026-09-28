#pragma once

#include "value.h"
#include "span.h"
#include "lazy_type.h"

typedef struct BinaryOpExpr BinaryOpExpr;
typedef struct UnaryOpExpr UnaryOpExpr;
typedef struct PropertyExpr PropertyExpr;
typedef struct PrivateNameExpr PrivateNameExpr;
typedef struct GenericParamsExpr GenericParamsExpr;
typedef struct AsTypeExpr AsTypeExpr;
typedef struct TernaryExpr TernaryExpr;
typedef struct CallExpr CallExpr;
typedef struct TemplateExpr TemplateExpr;
typedef struct ObjectExprItem ObjectExprItem;
typedef struct JsxElement JsxElement;
typedef struct JsxAttribute JsxAttribute;
typedef struct JsxChild JsxChild;
typedef struct vec_object_expr_item ObjectExpr;
typedef struct FunctionImpl FunctionImpl;
typedef struct ClassDecl ClassDecl;
typedef struct ErrorCtx ErrorCtx;
typedef struct Expr Expr;

g_declare_vec(ObjectExprItem, object_expr_item);
g_declare_vec(JsxAttribute, jsx_attribute);
g_declare_vec(JsxChild, jsx_child);
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
    // The callee of `require(x)` in a JavaScript file (module_ref is the module if x is a string literal): 1, or 2 when
    // the call is what a declaration's aliases import (InitializedBinding.require_ports, which report the module)
    uint8_t require_call;
    // (Bit fields: the struct is as big as an Expr may be, see expr.c)
    // The target of `x = <a shift, additive or multiplicative expression>` (tsc's isInCompoundLikeAssignment): it
    // has the base type of its declared type, so `let s = empty; s = s + "a"` is fine with `empty: ""`
    bool compound_like_target : 1;
    // Never "used before assigned": part of `typeof x` written as a type, or inside the binding pattern that declares it
    bool assume_initialized : 1;
    // A variable referenced from another flow container (function) than the one it's declared in (tsc's
    // isOuterVariable), as far as the resolve walk knows (a `var` whose declaration comes later isn't)
    bool outer_reference : 1;
    // The object of a property access or a callee (tsc's isConstraintPosition): a generic type with a union constraint
    // is narrowed as its constraint
    bool constraint_position : 1;
    bool is_arguments : 1; // The implicit `arguments` of the function around it (tsc's argumentsSymbol)
    // A name an instance field can't use because its constructor declares it (TS2301): no target (any), but `symbol`
    // is still what it finds
    bool invalid_initializer_reference : 1;
    // The namespace an extended interface's name starts with (`M` of `extends M.I`, TS2689): no target, but the
    // symbols baseline shows `symbol` (tsc's getSymbolAtLocation of a property access's object)
    bool namespace_as_value : 1;
    bool shorthand : 1; // The value of a shorthand property `{ a }`
    bool shorthand_unchecked : 1; // `{ a = 1 }` outside a pattern: tsc checks the default instead of the name
    // `super.x` / `super[x]` (the base instance), `super(...)` (the base constructor)
    bool super_member : 1;
    bool super_call : 1;
    bool class_base : 1; // The whole element of a class's `extends` clause (TS2863 rather than TS2693 for `extends number`)
    // `x` of `x.length`, `x.push(...)`, `x[i] = v`...: EvolvingArrayOp, see ty_is_evolving_array_operation_target()
    uint8_t evolving_op : 2;
    // The operand of `x!` (tsc's isAutomaticTypeInNonNull: an auto-typed variable there is initially undefined, and
    // has the non-nullable type of what control flow says)
    bool non_null_operand : 1;
    // The first name of the entity name a class's `extends` clause is (`M` of `extends M.I`), that entity name
    Expr const* extends_entity;
    union {
        // `this` in a function that isn't a method or arrow function (THIS_ANY): that function, whose `this` may be
        // contextually typed (ty_contextual_this_type())
        FunctionImpl const* this_function;
        struct Expr const* evolving_index; // EVOLVING_OP_ELEMENT_ASSIGNMENT: `i` of `x[i] = v` (never a keyword)
    };
} IdentifierExpr;

// Where a name is the object of an operation an evolving array allows (tsc's isEvolvingArrayOperationTarget): such a
// reference to `let x = []` is `any[]` rather than the array type control flow evolved so far. Set by the resolve walk
// on the reference root (`x` of `(x = [], x).push(1)`)
typedef enum EvolvingArrayOp {
    EVOLVING_OP_NONE,
    EVOLVING_OP_ACCESS, // `x.length`, `x.push(...)`, `x.unshift(...)`
    EVOLVING_OP_ELEMENT_ASSIGNMENT, // `x[i] = v`: only if i is number-like (evolving_index)
} EvolvingArrayOp;

void identifier_expr_drop(IdentifierExpr* me);
void identifier_expr_lookup_names(IdentifierExpr* me, Scope* names, ErrorCtx* errs);
Ty const* identifier_expr_evaluate_type(IdentifierExpr const* me);
void identifier_expr_typecheck(IdentifierExpr const* me, ErrorCtx* errs);
// A class, function, constant... being assigned to (the error is identifier_expr_typecheck()'s)
bool identifier_is_invalid_assignment_target(IdentifierExpr const* me);

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
        EXPR_PRIVATE_NAME, // `#x` on its own: only means something as the left operand of `in`
        EXPR_JSX_ELEMENT, // `<a x="1">...</a>`, `<a />`
        EXPR_JSX_FRAGMENT, // `<>...</>` (a JsxElement without a tag name or attributes)
        EXPR_JSX_ATTRIBUTES, // The attributes of a JsxElement, as one expression (tsc's JsxAttributes, an object literal)
    } tag;
    // An array or object literal that is assigned to, a destructuring pattern (tsc's isAssignmentTarget). Set by
    // expr_lookup_names()
    bool assignment_pattern;
    // An object literal that initializes a JavaScript `/** @enum {T} */` variable: not open-ended (set by the parser)
    bool jsdoc_enum_literal;
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
        PrivateNameExpr* private_name;
        JsxElement* jsx;
        VecJsxAttribute jsx_attributes;
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

static inline Expr expr_private_name(Span span, PrivateNameExpr* private_name) {
    return (Expr){ .tag = EXPR_PRIVATE_NAME, .private_name = private_name, EXPR_DEFAULT(span) };
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

// What there is to say about a module name that an import-like construct uses (TS2307 & co.)
void module_ref_report(ModuleRef* ref, Scope* names, ErrorCtx* errs);

// CommonJS in JavaScript files: what the left operand of an `=` declares (tsc's getAssignmentDeclarationKind)
typedef enum CommonJsTarget {
    COMMONJS_TARGET_NONE = 0,
    COMMONJS_TARGET_EXPORTS_PROPERTY, // `exports.x`, `module.exports.x`, `exports["x"]`: an export named x
    COMMONJS_TARGET_MODULE_EXPORTS, // `module.exports`: the module's `export=`
} CommonJsTarget;
// `a.b` or `a["b"]` (not parenthesized, not optional): the name b (in the source, no quotes), else an invalid span
Span expr_static_property_name(Expr const* me);
// `F.prototype` (F a plain identifier): F, else null
// `F` of `F.prototype` (an entity name: `a.b.prototype` too), null for anything else
Expr* expr_prototype_object(Expr* me);
// The symbol an entity name `a` or `a.b.c` names through the exports of what's before each dot, once names are
// resolved. Null if some name isn't there
Symbol const* expr_entity_name_symbol(Expr const* me);
// `this`, or a variable initialized with it (`var self = this`, tsc's isThisInitializedDeclaration): that `this`.
// Null otherwise (names must be resolved)
IdentifierExpr const* expr_this_or_alias(Expr const* me);
// `name_ret` (may be null): the name of the property an EXPORTS_PROPERTY target exports (in the source, no quotes)
CommonJsTarget expr_commonjs_target(Expr const* left, Span* name_ret);
// What an assignment declares, in a CommonJS file (`file` of the assignment): tsc's getAssignmentDeclarationKind, which
// ignores assignments of `void 0`
CommonJsTarget binary_commonjs_declaration(struct BinaryOpExpr const* assignment, Span* name_ret);
// `module.exports` (tsc's isModuleExportsAccessExpression)
bool expr_is_module_exports(Expr const* me);
// `require("m")` with a single argument (tsc's isRequireCall without the string literal requirement)
bool expr_is_require_call(Expr const* me);
// tsc's isCommonJsRequire for what the callee of `require(x)` resolves to: its requireSymbol (no `require` in scope)
// or an ambient function or variable (@types/node's)
bool expr_symbol_is_commonjs_require(Symbol const* symbol);

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
// `[a, b]` or `{ a }` as the target of an assignment of `source` (for..of): each target gets what it destructures to
void expr_destructuring_typecheck(Expr const* target, Ty const* source, ErrorCtx* errs);

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

// `=`, `+=`, ... `??=`
static inline bool binary_op_is_assignment(BinaryOp op) {
    return op >= BINARY_OP_EQUAL && op <= BINARY_OP_EH_EH_EQUAL;
}

int binary_op_get_precedence(BinaryOp op);
/// What tsc's getEffectiveCheckNode checks and reports at: `satisfies` skipped (and parentheses: use its inner_span)
Expr const* expr_effective_check_node(Expr const* me);
// Where an error about an expression goes (tsc's getErrorSpanForNode): a function or class expression's name, a
// `satisfies` keyword, the first line of an arrow function...
Span expr_error_span(Expr const* me);
// tsc's checkTruthinessOfType, for a condition that has been typechecked
void expr_check_truthiness(Expr const* me, ErrorCtx* errs);
zsview binary_op_get_zv(BinaryOp op);

typedef struct BinaryOpExpr {
    BinaryOp op;
    Expr left;
    Expr right;
    bool is_operand; // An operand of another binary expression, without parentheses
    bool indirect_call; // `(0, o.f)()`: the comma is how to call f without `this` (tsc's isIndirectCall)
    // `<a /><b />` where one element is expected: a comma made up by the parser, which already reported TS2657
    bool jsx_siblings;
    // Owned, `/** @type {T} */ a.b = v` in JavaScript: the declared type of what the assignment declares (SymbolDecl.jsdoc_type)
    TypeNode* jsdoc_type;
    uint8_t jsdoc_modifiers; // JsDocModifiers: `/** @private */ this.x = v` (SymbolDecl.jsdoc_modifiers)
    // The declare step's scope_add_expandos() looked at it as a possible `f.x = v` expando; the resolve walk binds the
    // other ones (nested in expressions) with scope_add_nested_expando()
    bool expando_bound;
    // JavaScript: the `a.b || {}` of `a.b = a.b || {}` (a function, class...: tsc's getDefaultedExpandoInitializer), whose
    // left side tsc doesn't check (checkBinaryExpression). Its type is its right side's. Set by the resolve walk
    bool defaulted_expando;
} BinaryOpExpr;

static inline void expr_mark_binary_operand(Expr* me) {
    if (my tag == EXPR_BINARY_OP && !expr_is_perenthisized(me)) my binary_op->is_operand = true;
}

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

// Where an `await` (expression, `for await` or `await using`) is written, for its grammar (tsc's checkAwaitGrammar)
typedef enum AwaitPlace {
    AWAIT_ALLOWED, // In an async function, or somewhere the parser already reported
    AWAIT_IN_FUNCTION, // In a function that isn't async: TS1308/TS1103
    AWAIT_AT_TOP_LEVEL, // Outside of any function: needs a module and the right module and target options
    AWAIT_IN_STATIC_BLOCK, // In a class `static {}` block (not in a function inside it): TS18037 on an expression
} AwaitPlace;

typedef enum AwaitKind { AWAIT_KIND_EXPR, AWAIT_KIND_FOR, AWAIT_KIND_USING } AwaitKind;

// The grammar errors of an `await` (`at`: its keyword) by where it is, `function` being the one it is in
void await_check_grammar(AwaitPlace place, AwaitKind kind, Span at, FunctionImpl const* function, ErrorCtx* errs);

typedef struct UnaryOpExpr {
    UnaryOp op;
    uint8_t await_place; // `await`: an AwaitPlace
    // `yield x` outside a generator (tsc parses one where an operand follows on the line, isYieldExpression): TS1163,
    // the operand is neither checked nor reports unresolved names
    bool outside_generator;
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
    bool constraint_position; // Like IdentifierExpr.constraint_position
    // Part of the entity name of a `typeof a.b` type (tsc's QualifiedName): also sees a namespace's type-only exports
    bool qualified_name;
    uint8_t assignment_target; // FlowAssignmentKind
    bool compound_like_target; // Like IdentifierExpr.compound_like_target
    bool method_call; // The callee of a call or new (tsc's isMethodAccessForCall): the object widens
    // `M.x = v` declaring an export of a CommonJS module through an alias of it (`const M = module.exports`), set by the
    // declare step: a declaration, not an assignment to what x may alias (like `exports.x = v`)
    bool commonjs_export_alias;
    FlowNode* flow; // Like IdentifierExpr.flow
    // The innermost class it is in (member accessibility, see ClassDecl.outer_class for the others), and the class
    // whose constructor is the function it is directly in (where `this.x = ` may write a readonly x)
    ClassDecl const* enclosing_class;
    // `a.#x`: the innermost class around it that declares `#x`, whose `#x` it names (null if none does)
    ClassDecl const* private_class;
    Symbol const* in_constructor_of;
    // JavaScript: the member an assignment to this access declares (`this.x = v`, `ns.a = v`), set by the resolve walk
    Symbol const* declares;
    // What `this` is where it is, if not a class: a `this` parameter, or a function with a contextual `this` (a
    // protected member is accessible when that is a derived class, tsc's getEnclosingClassFromThisParameter)
    Symbol const* this_parameter;
    FunctionImpl const* this_function;
    FunctionImpl const* containing_function; // The innermost function it is in, if any
    // `this.x`/`C.x` evaluated with the class (see FlowBuilder.initializer_context): InitializerContext, and the field
    // whose initializer it is in
    uint8_t initializer_context;
    struct ClassItem const* initializer_field;
} PropertyExpr;

typedef struct PrivateNameExpr {
    ClassDecl const* enclosing_class;
    ClassDecl const* private_class; // See PropertyExpr.private_class
} PrivateNameExpr;

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
    // JavaScript's `/** @type {T} */ (x)`: parentheses to tsc, which references look through (isMatchingReference)
    bool is_jsdoc;
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
    bool incomplete; // Tagged: unterminated, or ends in a `${` without its `}` (tsc's callIsIncomplete, no minimum arity)
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
    // Owned, `{ a = 1 }` (a destructuring assignment target): the default. Only its names are looked up (TODO: check it
    // and show it in the baselines)
    Expr* shorthand_default;
    Span shorthand_equals; // Its `=`
    // Owned, a JavaScript property's JSDoc `@type` (`/** @type {T} */ a: v`): the property's declared type, which the
    // value is contextually typed by and checked against (tsc's getTypeForDeclarationFromJSDocComment in checkObjectLiteral)
    TypeNode* jsdoc_type;
} ObjectExprItem;

void object_expr_item_drop(ObjectExprItem* me);

// `{ #x: 1 }`: TS18016, and no member of the literal's type (tsc's binder gives it no name outside a class)
static inline bool object_item_is_private_name(ObjectExprItem const* me) {
    return !my computed_key && my tag != OBJECT_ITEM_SPREAD && my name_span.sv.size && my name_span.sv.buf[0] == '#';
}

#define g_class_key ObjectExprItem, object_expr_item
#define g_opt (c_no_atomic)
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>

// JSX (tsc's JsxElement, JsxSelfClosingElement, JsxFragment). Only in JSX files (SourceFile.is_jsx)

typedef struct JsxAttribute {
    Span span;
    // `x`, `data-x` (tsc's JSX identifiers take hyphens) or a namespaced `ns:x`. Empty for a spread
    Span name;
    enum {
        JSX_ATTRIBUTE_NO_VALUE, // `<a x />`, which is `x={true}`
        JSX_ATTRIBUTE_STRING, // `x="1"`: value is a string literal (no escapes in JSX strings)
        JSX_ATTRIBUTE_EXPR, // `x={e}`: value is e
        JSX_ATTRIBUTE_EMPTY_EXPR, // `x={}` (or only a comment in the braces)
        JSX_ATTRIBUTE_ELEMENT, // `x=<b />`: value is the element
        JSX_ATTRIBUTE_SPREAD, // `{...e}`: value is e
    } tag;
    Expr value;
} JsxAttribute;

void jsx_attribute_drop(JsxAttribute* me);

#define g_class_key JsxAttribute, jsx_attribute
#define g_opt (c_no_atomic)
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>

typedef struct JsxChild {
    enum {
        JSX_CHILD_TEXT, // Text between tags, entities undecoded
        JSX_CHILD_EXPR, // `{e}` or `{...e}`: expr is e
        JSX_CHILD_EMPTY_EXPR, // `{}` or `{/* comment */}`
        JSX_CHILD_ELEMENT, // A nested element or fragment: expr is it
    } tag;
    Span span;
    // Text of only whitespace that contains a line break (tsc's JsxTextAllWhiteSpaces): not a child at all as far as
    // the checker is concerned
    bool whitespace_only;
    bool spread; // `{...e}`
    Expr expr;
} JsxChild;

void jsx_child_drop(JsxChild* me);

#define g_class_key JsxChild, jsx_child
#define g_opt (c_no_atomic)
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>

typedef struct JsxElement {
    // The tag name: an identifier (hyphens allowed: `my-elem`, or namespaced `a:b`, see namespaced), `this`, or a
    // property access `a.b.c`. EXPR_IMPLICIT_ANY for a fragment
    Expr tag_name;
    bool namespaced;
    bool tag_name_broken; // `<a.#b>`, `<a.>`: the name is missing after a dot (untyped)
    bool self_closing;
    bool has_type_args;
    VecSpannedType type_args; // `<C<string> />`
    Span type_args_trailing_comma; // `<C<string,> />`: TS1009, a grammar error
    Span type_args_empty; // `<C<> />`: TS1099, a grammar error
    Expr attributes; // EXPR_JSX_ATTRIBUTES (also for a fragment, with none)
    VecJsxChild children;
    Span opening_span; // `<a x="1">` / `<a />` / `<>`
    // The name in the closing tag, like tag_name (EXPR_IMPLICIT_ANY when self closing, a fragment, or missing)
    Expr closing_name;
    Span closing_span; // `</a>` / `</>`, invalid when self closing
    // What the factory's first name (`React` of `React.createElement`, tsc's getJsxNamespace) finds where the element
    // is, as a namespace (where `JSX` is looked up first, tsc's getJsxNamespaceAt) and as a value (the reference every
    // element makes, tsc's markJsxAliasReferenced). Set by the resolve walk, null if nothing
    Symbol const* factory_namespace;
    Symbol const* factory_value;
    // The module the file imports its factory from implicitly, `<jsxImportSource>/jsx-runtime` (react-jsx, or pragmas:
    // tsc's getJsxNamespaceContainerForImplicitImport), if it exists. Its `JSX` is the JSX namespace
    Symbol const* runtime_module;
} JsxElement;

void jsx_element_drop(JsxElement* me);
// The element an EXPR_JSX_ATTRIBUTES belongs to (tsc's node.parent): it is always a JsxElement's `attributes`
static inline JsxElement const* jsx_attributes_element(Expr const* attributes) {
    return c_container_of(attributes, JsxElement const, attributes);
}
// tsc's isJsxIntrinsicTagName: `<div>`, `<my-elem>`, `<a:b>` name an entry of JSX.IntrinsicElements, not a value
bool jsx_element_is_intrinsic(JsxElement const* me);
