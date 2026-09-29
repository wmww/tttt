#pragma once

#include "expr.h"
#include "type.h"
#include "block.h"
#include "value.h"
#include "binding.h"
#include "generic.h"
#include "lazy_type.h"
#include "flow.h"

typedef struct JsDocParamIssue {
    Span name;
    Span left; // The object a qualified name needs a tag of, invalid for a plain name
} JsDocParamIssue;

typedef struct FunctionArg {
    Span span;
    // Declared by a JSDoc `@type` signature of the function: that parameter (where messages point, like tsc's)
    Span jsdoc_span;
    InitializedBinding initializer;
    bool explicit_optional;
    Span question_span; // `?`, invalid when the parameter has none (explicit_optional can come from JSDoc)
    Span rest_span;
    bool rest;
    // A parameter of a signature in a type written as just a name that is a type (`(string) => void`), set when
    // resolving names: TS7051 instead of TS7006 (tsc's reportImplicitAny)
    bool name_is_type;
    bool jsdoc_variadic; // JavaScript `@param {...T} x` of a parameter that isn't a rest parameter (x is T)
    VecExpr decorators; // Of a class member's parameter: checked as calls by class_decl_typecheck()
    // `constructor(public x: T)`: also declares a property of the class
    bool is_parameter_property;
    bool is_readonly_property;
    bool is_override_property; // `constructor(override x: T)`
    uint8_t access; // AccessModifier (class_decl.h) of a parameter property
    bool jsdoc_this; // The `this` parameter a JSDoc `@this {T}` declares (not in the baselines' walk)
} FunctionArg;

void function_arg_drop(FunctionArg* me);

#define g_class_key FunctionArg, function_arg
#define g_no_clone
#include <cleat/generic/vec.h>

// `x is T`, `this is T`, `asserts x`, `asserts x is T` in the place of a return type (`ret` is boolean / void then)
typedef struct TypePredicateNode {
    Span name; // The parameter, or `this`
    bool is_this;
    bool asserts;
    bool has_type;
    TypeNode type;
    Span type_span;
    Symbol const* symbol; // The parameter, set by the resolve walk (null: `this`, a pattern, or no such parameter)
    i32 param_index; // Among `args`, -1 if there is none of that name
    bool jsdoc; // Written in a JSDoc `@returns`: not part of the baselines' walk
} TypePredicateNode;

typedef struct FunctionImpl {
    Span name;
    Span full_span;
    Generic generic;
    VecFunctionArg args;
    Span args_span;
    Span ret_span;
    bool ret_explicit;
    TypeNode ret;
    TypePredicateNode* predicate; // Usually null
    bool is_constructor;
    bool is_method; // Declared with method syntax (parameters compare bivariantly)
    bool is_async;
    bool is_generator;
    Span asterisk; // The `*` of a generator
    Symbol const* promise_decl; // The global Promise type (if any), looked up for async functions
    bool is_abstract_constructor;
    struct Expr const* first_super_call; // A constructor: the first `super(...)` in its body, not in a nested function
    // A constructor: its first `this` or `super.x` (not `super(...)`), not in a nested function or arrow function
    Span first_this_or_super;
    Symbol const* constructed_class; // The constructor of a class: what it returns
    uint8_t constructor_access; // The constructor of a class: its AccessModifier (class_decl.h)
    bool is_type; // Written as a type (function_impl_into_type()): owned by a CallableType, no body to infer from
    bool jsdoc_type; // A JSDoc `function(...)` type: its parameters are unnamed (`argN`)
    // A JSDoc `@callback`'s signature: tsc's isTypeParameterPossiblyReferenced finds no container on the way up from it
    // (the template tags are its siblings), so it may reference every type parameter in scope
    bool jsdoc_callback;
    // A function expression typed by the JSDoc `@type` of the variable it initializes: the return type node of that
    // signature, which tsc reuses when it prints the function's type (getReturnTypeOfTypeTag). Display only
    struct TypeNode const* jsdoc_tag_ret;
    // JavaScript: its JSDoc has `@param` or `@type` tags (tsc's hasJSDocParameterTags || getJSDocType), so it isn't an
    // untyped signature whose arguments are all optional
    bool jsdoc_typed;
    // JavaScript: its JSDoc has `@class`/`@constructor`: a constructor function even without `this.x = v` (tsc's
    // isJSConstructor: getJSDocClassTag)
    bool jsdoc_class;
    // JavaScript: its body refers to `arguments` (set by the resolve walk): its signature has a rest parameter
    bool uses_arguments;
    LazyType lazy_type;
    Symbol* symbol;
    Span arrow_span;
    // needed because TS1200 needs to be inserted in the typecheck phase for some reason
    bool newline_before_arrow;
    bool has_body;
    bool private_ambient; // A private method of an ambient class: no implicit any errors (tsc's isPrivateWithinAmbient)
    bool ambient; // Declared in an ambient context (`declare`, a declaration file), where it can't have a body
    bool expression_body; // An arrow function whose body is an expression (a synthesized `return`)
    // A declaration no type parameter list encloses (not an expression, whose context may have some, nor a method, nor
    // JavaScript): its type mentions no type parameters but its own (tsc's getOuterTypeParameters is empty), so
    // instantiating it is the identity. Set by the resolve walk
    bool no_outer_type_params;
    Block body; // Initialized to empty block if has_body is false
    // Control flow (flow.h), set by the resolve walk
    FlowNode* outer_flow; // Function expressions, arrows, object literal methods: the flow of the container it's made in
    FlowNode* end_flow; // The end of the body if control can get there (before typed reachability), else null
    FlowNode* return_flow; // Constructors: every way out
    bool has_explicit_return; // A reachable `return`
    bool has_bare_return; // A reachable `return;`
    struct Expr const** yields; // Generators: every `yield` of the body, in source order (malloc'd, owned)
    u32 yield_count;
    // JavaScript: the `@param` tags that name no parameter (tsc's checkUnmatchedJSDocParameters): TS8024 for a name,
    // TS8032 for a qualified name without its object (`left` valid). Found by the parser, reported when checking
    struct JsDocParamIssue* jsdoc_param_issues; // Owned
    u32 jsdoc_param_issue_count;
    bool jsdoc_params_checked;
    Span member_name; // Methods and accessors of classes and object literals (`name` is for what declares a symbol)
    Span variable_name; // A function expression or arrow function that initializes a variable: its name (display)
    uint8_t accessor; // 1: a getter, 2: a setter (of a class or an object literal)
    // A getter without a return type annotation whose setter's parameter has one: that annotation, which is the
    // getter's return type (tsc's getReturnTypeFromAnnotation). Set by the resolve walk
    TypeNode const* setter_type;
    bool is_expression; // A function expression, arrow function or object literal method (FUNCTION_IS_EXPRESSION)
    // The callee of a call (tsc's getImmediatelyInvokedFunctionExpression), set by the resolve walk: its unannotated
    // parameters past the call's argument count are optional
    bool is_iife;
    u32 iife_arg_count;
    struct CallExpr const* iife_call; // The call it is the callee of, when is_iife
    // Inside a call expression or an annotated declaration's initializer (FlowBuilder.contextual_depth): the types
    // baseline doesn't type its single returned expression syntactically
    bool syntactically_contextual;
    // What a `this` in it may be typed by (tsc's getContextualThisParameterType), set when resolving: the object literal
    // it is a method (or function-valued property) of, or the object of the property it is assigned to (`o.m = function`)
    struct Expr const* this_literal;
    struct Expr const* this_assigned_object;
    // JavaScript: F of `F.prototype.m = function` or `F.prototype = { m() {} }`, whose type parameters the JSDoc names
    // of the method may be (tsc's getAssignmentDeclarationLocation) and whose instance `this` is in it (tsc's
    // getClassNameFromPrototypeMethod). Set when resolving
    struct Expr const* prototype_of;
    // A function expression or arrow function that initializes a variable with expandos (`const f = () => {}; f.x = 1`,
    // scope_add_expandos()): the variable, whose exports are members of this function's type (tsc's getSymbolOfExpando)
    Symbol const* expando_host;
    // JavaScript: the variable a function expression initializes (`var F = function () {}`), whose symbol is the class
    // a constructor function makes (a function declaration's is its own symbol). Set by the resolve walk
    Symbol* class_symbol;
    // Owned, JavaScript: a JSDoc `@type {F}` on the function that isn't a written signature (`{Processor<A, B>}`): its
    // signature types the parameters like a contextual one (tsc's getSignatureOfTypeTag, ty_function_apply_jsdoc_type())
    TypeNode* jsdoc_signature_type;
    // Owned, JavaScript: the JSDoc `@overload` signatures (function types), which are the function's signatures
    // instead of its own (tsc's getSignaturesOfSymbol)
    TypeNode* jsdoc_overloads;
    u32 jsdoc_overload_count;
} FunctionImpl;

void function_impl_drop(FunctionImpl* me);
// tsc's getErrorSpanForNode() of a function: its name, else an arrow function up to the end of its first line, else
// its first token
Span function_error_span(FunctionImpl const* me);
// tsc's isUnwrappedReturnTypeUndefinedVoidOrAny
bool function_impl_return_void_any_or_undefined(FunctionImpl const* me, struct Ty const* return_type);
// tsc's checkFunctionOrMethodDeclarationDiagnostics under noImplicitAny: a declaration without a body or return type
// (an overload, an ambient function, a method signature) returns an implicit any (TS7010 at `name`)
void function_impl_check_implicit_any_return(FunctionImpl const* me, Span name, Span at, ErrorCtx* errs);
// TS7006/TS7019/TS7031: parameters that nothing types (no annotation, default or contextual type)
void function_impl_check_implicit_any_parameters(FunctionImpl const* me, ErrorCtx* errs);
// hoisted: a function declaration of a nested block in non-strict code, which is declared in the enclosing function (or
// file) like a `var` (tsc's declareSymbolAndAddToSymbolTable outside of strict mode)
// declared: `declare function`
void function_impl_add_names(FunctionImpl* me, Scope* scope, DeclExported exported, bool hoisted, bool declared);
typedef enum FunctionFlowKind {
    FUNCTION_IS_DECLARATION, // Function declarations, class members, signatures: a control flow graph of their own
    // Function expressions, arrow functions, object literal methods: narrowings of the flow they're made in can
    // survive into them (FunctionImpl.outer_flow), and an immediately invoked one is part of that flow
    FUNCTION_IS_EXPRESSION,
} FunctionFlowKind;
void function_impl_lookup_names_as(FunctionImpl* me, FunctionFlowKind kind, bool this_is_any, Scope* names, ErrorCtx* errs);
static inline void function_impl_lookup_names(FunctionImpl* me, bool this_is_any, Scope* names, ErrorCtx* errs) {
    function_impl_lookup_names_as(me, FUNCTION_IS_DECLARATION, this_is_any, names, errs);
}
TypeNode function_impl_into_type(FunctionImpl me);
Ty const* function_impl_evaluate_type(FunctionImpl* me);
// What return expressions are contextually typed by. Returns the enclosing function's, to be put back
Ty const* function_impl_enter_return_context(FunctionImpl const* me);
// The checks on a parameter list. `is_type`: a function type or signature, which can't have initializers
// A setter's unannotated parameter has the type the getter of the same property returns
// and a getter without a return type annotation returns what the setter's parameter annotation says (setter_type)
void function_impl_pair_setter(FunctionImpl* setter, FunctionImpl* getter);
void function_impl_typecheck_signature(FunctionImpl const* me, bool is_type, ErrorCtx* errs);
void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs);
