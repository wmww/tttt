#pragma once

#include "ast_alloc.h"
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

// A JSDoc `@param` tag that names no parameter: its type is only resolved and checked (tsc checks every tag)
typedef struct JsDocUnmatchedParam {
    Span name;
    TypeNode type; // Owned
    bool variadic; // `{...T}` (type is T)
} JsDocUnmatchedParam;

typedef struct FunctionArg {
    Span span;
    InitializedBinding initializer;
    // Declared by a JSDoc `@type` signature of the function: that parameter (where messages point, like tsc's). Owned,
    // null otherwise
    Span* jsdoc_span;
    // Made optional by its JSDoc `@param` (`{T=}`, `[name]`): the tag, where tsgo's reparsed `?` is. Owned, else null
    Span* jsdoc_question;
    // Of a class member's parameter: checked as calls by class_decl_typecheck(). Owned, null if there are none: read
    // through function_arg_decorators()
    VecExpr* decorators;
    // `?` (explicit_optional can come from JSDoc) and `...`: where they are in `span`, see function_arg_question_span()
    // and function_arg_rest_span()
    u32 question_offset; // 0: none
    u32 rest_length; // From the start of `span` to the end of the `...`, 0: not a rest parameter
    bool explicit_optional : 1;
    bool rest : 1;
    // A parameter of a signature in a type written as just a name that is a type (`(string) => void`), set when
    // resolving names: TS7051 instead of TS7006 (tsc's reportImplicitAny)
    bool name_is_type : 1;
    // `constructor(public x: T)`: also declares a property of the class
    bool is_parameter_property : 1;
    bool is_readonly_property : 1;
    bool is_override_property : 1; // `constructor(override x: T)`
    // A JavaScript parameter `@param [x=v]` or `{T=}` makes optional though it has an initializer: its declared type has
    // undefined (tsc's isJSDocOptionalParameter in isOptionalDeclaration)
    bool jsdoc_optional_initialized : 1;
    bool jsdoc_this : 1; // The `this` parameter a JSDoc `@this {T}` declares (not in the baselines' walk)
    uint8_t access; // AccessModifier (class_decl.h) of a parameter property
} FunctionArg;

void function_arg_drop(FunctionArg* me);
// `?`, invalid when the parameter has none
static inline Span function_arg_question_span(FunctionArg const* me) {
    if (!me->question_offset) return span_null;
    return span_in_sv(me->span, csview_from_n(me->span.buf + me->question_offset, 1));
}
// From the start of the parameter to the end of its `...`, invalid when it's not a rest parameter
static inline Span function_arg_rest_span(FunctionArg const* me) {
    if (!me->rest_length) return span_null;
    return span_in_sv(me->span, csview_from_n(me->span.buf, me->rest_length));
}
extern const VecExpr function_arg_no_decorators;
static inline VecExpr const* function_arg_decorators(FunctionArg const* me) {
    return me->decorators ? me->decorators : &function_arg_no_decorators;
}

#define g_class_key FunctionArg, function_arg
#define g_no_clone
#define g_allocator ast
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
    Span span; // The whole predicate, `asserts` included
} TypePredicateNode;

// What few functions have: JavaScript and JSDoc, constructors, generators, IIFEs, methods' links set when resolving.
// Read through function_impl_rare(), written through function_impl_rare_mut()
typedef struct FunctionImplRare {
    Span asterisk; // The `*` of a generator
    // A constructor's return annotation (TS1093) that is a missing type: where tsc's unknownSymbol is (symbols baseline;
    // the annotation itself is dropped)
    Span constructor_missing_type;
    Symbol const* promise_decl; // The global Promise type (if any), looked up for async functions
    // A function whose own `this` is used and is any: the function whose `this` is the one it shadows, when that's
    // also a function's (contextually typed, maybe: TS2738's related info on TS2683)
    struct FunctionImpl const* outer_this_function;
    // A constructor: the first `super(...)` in its body, not in a nested function. Read through
    // function_impl_first_super_call() (the walk of a deferred body sets it, as the fields below marked "walk")
    struct Expr const* first_super_call_;
    // A function expression or arrow written as an argument of `new X(...)`: the X when it's an identifier (TS2794:
    // tsc's isPromiseResolveArityError)
    struct Expr const* new_callee;
    // A constructor: its first `this` or `super.x` (not `super(...)`), not in a nested function or arrow function (walk)
    Span first_this_or_super_;
    Symbol const* constructed_class; // The constructor of a class: what it returns
    uint8_t constructor_access; // The constructor of a class: its AccessModifier (class_decl.h)
    // The type of its JSDoc `@type` tag (TS8030 for a declaration whose type has no call signature that takes as many
    // arguments as it needs: tsc's getContextualCallSignature in checkFunctionOrMethodDeclaration). The mismatch of a
    // written signature, which the parser moves into the function, is found by the parser
    Span jsdoc_signature_span;
    FlowNode* return_flow_; // Constructors: every way out (walk)
    struct Expr const** yields_; // Generators: every `yield` of the body, in source order (the AST's, owned; walk)
    u32 yield_count_;
    // JavaScript: the `@param` tags that name no parameter (tsc's checkUnmatchedJSDocParameters): TS8024 for a name,
    // TS8032 for a qualified name without its object (`left` valid). Found by the parser, reported when checking
    u32 jsdoc_param_issue_count;
    struct JsDocParamIssue* jsdoc_param_issues; // Owned
    JsDocUnmatchedParam* jsdoc_unmatched; // Owned
    u32 jsdoc_unmatched_count;
    // The last `@param` tag when it names no parameter (its index into jsdoc_unmatched plus one, else 0): what
    // `arguments` could be (tsc's checkUnmatchedJSDocParameters: TS8029 unless its type is an array)
    u32 jsdoc_arguments_candidate;
    // An overload signature (no body, not ambient, not a type's): what resolving its parameters' initializers reported,
    // kept for function_impl_check_compared_initializers() (tsc checks one only when the parameter's type is asked).
    // Owned, null if nothing
    struct ErrorCtx* initializer_errors;
    // An overload signature of a class method or constructor: the first declaration of its symbol with a body (tsc's
    // addImplementationSuccessElaboration looks there); of a JSDoc `@overload`, its host. Set by the resolve walk
    struct FunctionImpl const* overload_implementation;
    struct Symbol const* static_method_of; // A static method's class (display: `typeof C.m`), set by the resolve walk
    // A getter without a return type annotation whose setter's parameter has one: that annotation, which is the
    // getter's return type (tsc's getReturnTypeFromAnnotation). Set by the resolve walk
    // Read through function_impl_setter_type(), which checks setter_key
    TypeNode const* setter_type;
    struct Expr const* setter_key; // The computed name the pair shares (`[C.k]`), null if none
    // The `this` parameter of the other accessor of the pair (tsc's getAccessorThisParameter: a getter's only parameter,
    // a setter's first of two), which is this one's when it has none (getSignatureFromDeclaration) and types a setter's
    // unannotated one (getTypeForVariableLikeDeclaration). Set by the resolve walk
    struct FunctionArg const* paired_this;
    // An accessor of a class whose computed name isn't a literal: its class and member, whose pair is its symbol's
    // (ty_class_accessor_first(), by the key's type: the checker's). The resolve walk pairs the others (paired_this,
    // setter_type, InitializedBindingRare.getter); read through function_impl_paired_this(), function_impl_setter_type()
    struct ClassDecl const* late_accessor_class;
    struct ClassItem const* late_accessor_item;
    u32 iife_arg_count; // See FunctionImpl.is_iife
    struct CallExpr const* iife_call; // The call it is the callee of, when is_iife
    // What a `this` in it may be typed by (tsc's getContextualThisParameterType), set when resolving: the object literal
    // it is a method (or function-valued property) of, or the object of the property it is assigned to (`o.m = function`)
    struct Expr const* this_literal;
    struct Expr const* this_assigned_object;
    // A function expression or arrow function that initializes a variable with expandos (`const f = () => {}; f.x = 1`,
    // scope_add_expandos()): the variable, whose exports are members of this function's type (tsc's getSymbolOfExpando)
    Symbol const* expando_host;
    // A function expression or arrow function that initializes a variable of a file or namespace (`var f = () => f`):
    // the variable, by which its type prints `typeof f` within itself (tsgo's shouldWriteTypeOfFunctionSymbol)
    Symbol const* top_level_variable;
    // A parenthesized property value `a: (function () {})`: this_literal says whose property it is, but tsc's
    // getContainingObjectLiteral doesn't see through the parentheses (no contextual `this`)
    bool this_literal_parenthesized;
    // The name an anonymous function expression is called by where it is written (tsc's getAssignedName: the variable,
    // property or binding element it is directly the value of, the left of `=`), see mark_assigned_name(). Where its
    // errors go (tsgo's GetErrorRangeForNode)
    Span assigned_name;
    // Owned, JavaScript: a JSDoc `@type {F}` on the function that isn't a written signature (`{Processor<A, B>}`): its
    // signature types the parameters like a contextual one (tsc's getSignatureOfTypeTag, ty_function_apply_jsdoc_type())
    TypeNode* jsdoc_signature_type;
    // Owned, JavaScript: the JSDoc `@overload` signatures (function types), which are the function's signatures
    // instead of its own (tsc's getSignaturesOfSymbol)
    TypeNode* jsdoc_overloads;
    u32 jsdoc_overload_count;
} FunctionImplRare;

// A span in the file of the function (FunctionImpl.span_source): offset + 1 (0: invalid) and length
typedef struct FunctionSpan {
    u32 start;
    u32 length;
} FunctionSpan;

// FunctionImpl.context_root's tag (the low bits of the pointer)
typedef enum ContextRootKind {
    CONTEXT_ROOT_EXPR = 0, // An Expr the walk checks without a contextual type (an expression statement's)
    CONTEXT_ROOT_BINDING = 1, // An InitializedBinding whose initializer the walk checks (a variable, a parameter's default)
    CONTEXT_ROOT_RETURN = 2, // A FunctionImpl one of whose return expressions it is in (an arrow's expression body too)
    CONTEXT_ROOT_TAGS = 3,
} ContextRootKind;
static inline uintptr_t context_root_make(void const* node, ContextRootKind kind) { return (uintptr_t)node | kind; }
static inline ContextRootKind context_root_kind(uintptr_t root) { return (ContextRootKind)(root & CONTEXT_ROOT_TAGS); }
static inline void const* context_root_node(uintptr_t root) { return (void const*)(root & ~(uintptr_t)CONTEXT_ROOT_TAGS); }

typedef struct FunctionImpl {
    Span name;
    Span full_span;
    Generic generic;
    VecFunctionArg args;
    // The file of the FunctionSpans, set with the first of them: read them through function_impl_args_span() and co
    SourceFile const* span_source;
    FunctionSpan args_span_at;
    FunctionSpan ret_span_at;
    FunctionSpan arrow_span_at;
    FunctionSpan member_name_at; // Methods and accessors of classes and object literals (`name` is for what declares a symbol)
    FunctionSpan variable_name_at; // A function expression or arrow function that initializes a variable: its name (display)
    TypeNode ret;
    TypePredicateNode* predicate; // Usually null
    FunctionImplRare* rare; // Owned, null if none of its fields is set
    Symbol* symbol;
    // Initialized to empty block if has_body is false. Read through function_impl_body(): a deferred body (plan 78) is
    // elsewhere, `body_.deferred` says where
    Block body_;
    // Control flow (flow.h), set by the resolve walk
    FlowNode* outer_flow; // Function expressions, arrows, object literal methods: the flow of the container it's made in
    // The end of the body if control can get there (before typed reachability), else null. Read through
    // function_impl_end_flow(): the walk of a deferred body sets it, like has_explicit_return_ and co below
    FlowNode* end_flow_;
    uint8_t accessor; // 1: a getter, 2: a setter (of a class or an object literal)
    bool ret_explicit : 1;
    bool is_constructor : 1;
    bool is_method : 1; // Declared with method syntax (parameters compare bivariantly)
    bool is_async : 1;
    bool is_generator : 1;
    bool is_abstract_constructor : 1;
    bool is_abstract_member : 1; // An `abstract` method or accessor of a class
    bool is_type : 1; // Written as a type (function_impl_into_type()): owned by a CallableType, no body to infer from
    bool jsdoc_type : 1; // A JSDoc `function(...)` type: its parameters are unnamed (`argN`)
    bool jsdoc_unchecked : 1; // In a tag tsgo's checker doesn't visit (`@throws`): no TS7014
    // A JSDoc `@callback`'s signature: tsc's isTypeParameterPossiblyReferenced finds no container on the way up from it
    // (the template tags are its siblings), so it may reference every type parameter in scope
    bool jsdoc_callback : 1;
    bool jsdoc_signature_mismatch : 1; // See FunctionImplRare.jsdoc_signature_span
    // JavaScript: its JSDoc has `@param` or `@type` tags (tsc's hasJSDocParameterTags || getJSDocType), so it isn't an
    // untyped signature whose arguments are all optional
    bool jsdoc_typed : 1;
    // JavaScript: a `@type` tag of its host applies (its own, or its variable's: the contextual type tsgo's
    // isUntypedSignatureInJSFile asks for)
    bool jsdoc_type_tagged : 1;
    // JavaScript: its body refers to `arguments` (set by the resolve walk): its signature has a rest parameter
    bool uses_arguments : 1;
    // needed because TS1200 needs to be inserted in the typecheck phase for some reason
    bool newline_before_arrow : 1;
    bool has_body : 1;
    // The parser lost the body's `{` (`function f() => 4`, tsc's missing Block): no body, but not an overload waiting
    // for its implementation either (tsc checks `!body` there)
    bool body_missing : 1;
    bool private_ambient : 1; // A private method of an ambient class: no implicit any errors (tsc's isPrivateWithinAmbient)
    bool ambient : 1; // Declared in an ambient context (`declare`, a declaration file), where it can't have a body
    // A function declaration with the `declare` keyword and no `export`: an export context doesn't export it (tsc's
    // getEffectiveDeclarationFlags gives the Export flag only to an ambient declaration without the modifier)
    bool declared_unexported : 1;
    bool expression_body : 1; // An arrow function whose body is an expression (a synthesized `return`)
    // The nearest function expression around it that is context-sensitive (tsc's isContextSensitive), null if none: one
    // typed by a generic contextual signature takes that signature's type parameters, which the functions inside it
    // may mention (tsc's getOuterTypeParameters), see ty_function_closed()
    struct FunctionImpl const* contextual_ancestor;
    // The scope the function is declared in (set by the resolve walk): what encloses it, for the type parameters in
    // scope there (tsc's getOuterTypeParameters, ty_function_outer_type_params())
    struct Scope const* outer_scope;
    // A function expression: where its contextual type comes from, as the typecheck walk would check it (set by the
    // resolve walk, FlowBuilder.context_root; 0 if nothing gives it one): a tagged pointer, see ContextRootKind. The
    // checker checks that root first when something asks for the function's contextual signature before anything
    // checked the function (ty_settle_function_context(): tsc's getContextualSignature resolves the enclosing call on
    // demand), so the contextual signature is a function of the node, whatever was checked before
    uintptr_t context_root;
    // A declaration no type parameter list encloses (not an expression, whose context may have some, nor a method, nor
    // JavaScript): its type mentions no type parameters but its own (tsc's getOuterTypeParameters is empty), so
    // instantiating it is the identity. Set by the resolve walk
    bool no_outer_type_params : 1;
    // No type parameter list encloses it, nor a class (whose `this` its type could mention), and it isn't JavaScript:
    // unlike no_outer_type_params also for expressions and object literal methods. Their types mention no type
    // parameters but those of their own signatures (tsc's getOuterTypeParameters is empty: instantiating one is the
    // identity). Set by the resolve walk
    bool no_enclosing_type_params : 1;
    // A method or accessor of a class expression: narrowing continues into it like into a function expression (tsc's
    // isObjectLiteralOrClassExpressionMethodOrAccessor), it has an outer_flow
    bool in_class_expression : 1;
    bool jsdoc_params_checked : 1;
    bool is_expression : 1; // A function expression, arrow function or object literal method (FUNCTION_IS_EXPRESSION)
    // The callee of a call (tsc's getImmediatelyInvokedFunctionExpression), set by the resolve walk: its unannotated
    // parameters past the call's argument count are optional
    bool is_iife : 1;
    // Inside a call expression or an annotated declaration's initializer (FlowBuilder.contextual_depth): the types
    // baseline doesn't type its single returned expression syntactically
    bool syntactically_contextual : 1;
    // The parser skipped the body (plan 78): it is parsed and walked when a checker first needs it
    // (function_impl_body()). Set by the parser only, never cleared: `body_.deferred` is the body's state
    bool deferred : 1;
    // What the resolve walk of the body sets, apart from the bit-fields above (a memory location of their own: a deferred
    // body's materialization writes these while other checkers read those, plan 78). Read through their accessors
    // (function_impl_has_explicit_return() and co), which materialize a deferred body first; only the walk writes them
    bool has_explicit_return_; // A reachable `return`
    bool has_bare_return_; // A reachable `return;`
    // Its body or signature mentions `this` (an expression or a type), arrow functions and function types in it included
    // (tsc's NodeFlags.ContainsThis, set by the resolve walk): without it, it isn't context sensitive for its `this`
    bool contains_this_;
} FunctionImpl;

// A function body the parser skipped (plan 78, "Deferred bodies" in documentation.md): it is parsed, and its resolve walk
// run, when a checker first needs it (program_materialize_file(), for all of a file's at once). Made by the parser in the
// file's AST arena; the resolve walk (function_impl_lookup_names_as()) records where the walk of the body starts
typedef struct DeferredBody {
    Block block; // The body, once materialized
    u32 open; // Offset of the `{` in the file
    u32 end; // Offset just past the `}`
    // Set by the resolve walk: the function, the next deferred body of the file in walk order, the function's scope and
    // what its flow builder held where the body's walk starts (the rest of the builder is as flow_builder_init() makes it)
    struct FunctionImpl* function;
    struct DeferredBody* next;
    Scope* function_scope;
    FlowNode* start;
    FlowNode* container_start;
    FlowNode* current;
    FlowNode* return_target;
    Symbol const* constructor_of;
    struct PropertyContext const* property_context;
    u32 depth;
    u32 contextual_depth;
    bool dead : 1;
    bool has_flow_effects : 1;
    bool within_unreachable : 1;
    bool ambient : 1;
    bool unused_quiet : 1; // Walked under unused_marking_suppressed (what tsc never checks): so is its body
    bool silent : 1; // Walked without an error context (what tsc never checks): so is its body
    // In the file's current materialization (DeferredBodies.memory), set once its walk is done
    _Atomic(bool) ready;
    // Where its walk's unused registrations are in SourceFile.unused (DeferredBodies.out_of_order)
    u32 unused_from;
    u32 unused_to;
    // The names the signature's walk looked up through the function's scope, with the meanings it looked for (in the
    // binder's arena): had the body's declarations been there, one of them could have been found
    // (function_impl_lookup_deferred_body() checks)
    u32 signature_name_count;
    u32 signature_name_cap;
    struct DeferredSignatureName* signature_names;
} DeferredBody;

typedef struct DeferredSignatureName {
    csview name;
    // The meanings (SymbolFlags) looked up from each place: DEFERRED_FROM_*. A lookup without a meaning is one for any
    u32 meaning[3];
    uint8_t looked_up; // Bit DEFERRED_FROM_*: looked up from there at all
    // Reported not found from there (bits), and with what meaning: its spelling suggestion may have been one the body
    // declares
    uint8_t not_found;
    u32 not_found_meaning[3];
} DeferredSignatureName;

// Where a signature's walk looked a name up from (what of the body it would have seen: function_local_hidden())
enum { DEFERRED_FROM_OTHER, DEFERRED_FROM_PARAMETERS, DEFERRED_FROM_RETURN_TYPE };

// program.c: materializes the file of a deferred function, returns its body
Block* function_impl_deferred_body(FunctionImpl const* me);
// The function's body: materialized first if the parser deferred it
static inline Block const* function_impl_body(FunctionImpl const* me) {
    return me->deferred ? function_impl_deferred_body(me) : &me->body_;
}
static inline Block* function_impl_body_mut(FunctionImpl* me) {
    return me->deferred ? function_impl_deferred_body(me) : &me->body_;
}
// The function once the resolve walk has been through its body (its body_ and the fields below are set): materialized
// first if deferred
static inline FunctionImpl const* function_impl_walked(FunctionImpl const* me) {
    if (me->deferred) function_impl_deferred_body(me);
    return me;
}
// The resolve walk of a deferred body (program_materialize_file(), after the parser): the end of
// function_impl_lookup_names_as() from where it stopped. False if the walk of the signature could have found a name the
// body declares (or suggested one, for a name it didn't find): the eager walk might have resolved differently
bool function_impl_lookup_deferred_body(DeferredBody* body, ErrorCtx* errs);

Span function_impl_span_get(FunctionImpl const* me, FunctionSpan span);
void function_impl_span_set(FunctionImpl* me, FunctionSpan* field, Span span);
#define FUNCTION_SPAN_ACCESSORS(name, field) \
    static inline Span function_impl_##name(FunctionImpl const* me) { return function_impl_span_get(me, me->field); } \
    static inline void function_impl_set_##name(FunctionImpl* me, Span span) { function_impl_span_set(me, &me->field, span); }
FUNCTION_SPAN_ACCESSORS(args_span, args_span_at)
FUNCTION_SPAN_ACCESSORS(ret_span, ret_span_at)
FUNCTION_SPAN_ACCESSORS(arrow_span, arrow_span_at)
FUNCTION_SPAN_ACCESSORS(member_name, member_name_at)
FUNCTION_SPAN_ACCESSORS(variable_name, variable_name_at)
#undef FUNCTION_SPAN_ACCESSORS
// An arrow function (it has a `=>`)
static inline bool function_impl_is_arrow(FunctionImpl const* me) { return me->arrow_span_at.start; }

extern const FunctionImplRare function_impl_no_rare;
static inline FunctionImplRare const* function_impl_rare(FunctionImpl const* me) {
    return me->rare ? me->rare : &function_impl_no_rare;
}
// Allocates them if needed
FunctionImplRare* function_impl_rare_mut(FunctionImpl* me);

// What the resolve walk of the body leaves on the function (the fields of the same names): a deferred body is walked
// first. The walk itself writes and reads the fields
static inline FlowNode* function_impl_end_flow(FunctionImpl const* me) { return function_impl_walked(me)->end_flow_; }
static inline bool function_impl_has_explicit_return(FunctionImpl const* me) {
    return function_impl_walked(me)->has_explicit_return_;
}
static inline bool function_impl_has_bare_return(FunctionImpl const* me) { return function_impl_walked(me)->has_bare_return_; }
static inline bool function_impl_contains_this(FunctionImpl const* me) { return function_impl_walked(me)->contains_this_; }
static inline FlowNode* function_impl_return_flow(FunctionImpl const* me) {
    return function_impl_rare(function_impl_walked(me))->return_flow_;
}
static inline struct Expr const* function_impl_first_super_call(FunctionImpl const* me) {
    return function_impl_rare(function_impl_walked(me))->first_super_call_;
}
static inline Span function_impl_first_this_or_super(FunctionImpl const* me) {
    return function_impl_rare(function_impl_walked(me))->first_this_or_super_;
}
static inline u32 function_impl_yield_count(FunctionImpl const* me) {
    return function_impl_rare(function_impl_walked(me))->yield_count_;
}
// The `yield`s of a generator's body, in source order (function_impl_yield_count() of them)
static inline struct Expr const* const* function_impl_yields(FunctionImpl const* me) {
    return (struct Expr const* const*)function_impl_rare(function_impl_walked(me))->yields_;
}

void function_impl_drop(FunctionImpl* me);
// tsc's getErrorSpanForNode() of a function: its name, else an arrow function up to the end of its first line, else
// its first token
Span function_error_span(FunctionImpl const* me);
// tsc's isUnwrappedReturnTypeUndefinedVoidOrAny
bool function_impl_return_void_any_or_undefined(FunctionImpl const* me, struct Ty const* return_type);
// tsc's nodeCanBeDecorated for parameter `index`: legacy decorators of a constructor, method or setter with a body in
// a class declaration (`in_class_declaration`), not its `this` parameter. checkDecorators looks at nothing else
bool function_arg_decoratable(FunctionImpl const* me, isize index, bool in_class_declaration);
// tsc's checkFunctionOrMethodDeclarationDiagnostics under noImplicitAny: a declaration without a body or return type
// (an overload, an ambient function, a method signature) returns an implicit any (TS7010 at `name`)
void function_impl_check_implicit_any_return(FunctionImpl const* me, Span name, Span at, ErrorCtx* errs);
// TS7006/TS7019/TS7031: parameters that nothing types (no annotation, default or contextual type)
void function_impl_check_implicit_any_parameters(FunctionImpl const* me, ErrorCtx* errs);
// hoisted: a function declaration of a nested block in non-strict code, which is declared in the enclosing function (or
// file) like a `var` (tsc's declareSymbolAndAddToSymbolTable outside of strict mode)
// declared: `declare function`
void function_impl_add_names(FunctionImpl* me, Scope* scope, DeclStatementProps props, bool hoisted);
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
// What return expressions are contextually typed by. Returns the enclosing function's, to be put back
Ty const* function_impl_enter_return_context(FunctionImpl const* me);
// The checks on a parameter list. `is_type`: a function type or signature, which can't have initializers
// A setter's unannotated parameter has the type the getter of the same property returns
// and a getter without a return type annotation returns what the setter's parameter annotation says (setter_type)
void function_impl_pair_setter(FunctionImpl* setter, FunctionImpl* getter, struct Expr const* key);
// setter_type, unless the computed name the pair shares isn't late bindable after all (`[C.n]` with n a number: two
// symbols to tsc's isLateBindableName, which goes by the name's type)
TypeNode const* function_impl_setter_type(FunctionImpl const* me);
// paired_this, or a late-bound accessor's (see FunctionImplRare.late_accessor_class)
struct FunctionArg const* function_impl_paired_this(FunctionImpl const* me);
// The getter a late-bound setter is paired with, null if none
FunctionImpl const* function_impl_late_getter(FunctionImpl const* setter);
void function_impl_typecheck_signature(FunctionImpl const* me, bool is_type, ErrorCtx* errs);
void function_impl_check_parameter_decorators(FunctionImpl const* me, ErrorCtx* errs);
// TS2842 for the unused renamings in a bodiless signature's patterns (part of function_impl_typecheck_signature())
void function_impl_check_unused_renamings(FunctionImpl const* me, ErrorCtx* errs);
void function_impl_check_jsdoc_overloads(FunctionImpl const* me, Span name, ErrorCtx* errs);
// An overload compared with its implementation (tsc's isImplementationCompatibleWithOverload asks its parameters'
// types): its unannotated parameters' initializers checked as typing them checks them (FunctionImplRare.initializer_errors)
void function_impl_check_compared_initializers(FunctionImpl const* me, ErrorCtx* errs);
void function_impl_typecheck(FunctionImpl* me, ErrorCtx* errs);
// The part of function_impl_typecheck() about the body, deferred for a function expression (Checker.defer_checks)
void function_impl_typecheck_body(FunctionImpl* me, ErrorCtx* errs);
// The parameters a call has to pass for a JSDoc `@type` signature to fit (tsc's isAritySmaller of the target)
u32 function_impl_jsdoc_required_params(FunctionImpl const* me);
