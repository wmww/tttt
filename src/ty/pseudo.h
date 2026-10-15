#pragma once

#include "prelude.h"

// tsgo's pseudochecker (internal/pseudochecker): the type of a declaration as its syntax says it, without the checker.
// A Pseudo is a skeleton of a type made of the declaration's own nodes: an annotation to reuse, a literal to copy, an
// object literal's members, a function expression's parameters... or "inferred" where the syntax doesn't say (the
// checker's type is printed there). Where a type is printed for a declaration (tsgo's serializeTypeForDeclaration and
// serializeReturnTypeForSignature: the types baseline's members, parameters and return types, declaration emit), the
// pseudo type is printed instead of the checker's type when it stands for the same type (ty_display.c's
// pseudo_equivalent()), and its Inferred parts are what isolatedDeclarations reports (TS9007-TS9038).
//
// Built once per node (the checker's node links), in the checker's arena. Ported from pseudochecker/lookup.go: the
// names of the builder functions in pseudo.c are its

typedef struct Expr Expr;
typedef struct TypeNode TypeNode;
typedef struct FunctionImpl FunctionImpl;
typedef struct FunctionArg FunctionArg;
typedef struct ObjectExprItem ObjectExprItem;
typedef struct InitializedBinding InitializedBinding;
typedef struct Checker Checker;
typedef struct ClassItem ClassItem;

typedef enum PseudoKind : uint8_t {
    // An annotation (or a type assertion's type): `node`. With `predicate`: the type predicate of `function`
    PSEUDO_DIRECT,
    // Too complex: the checker's type of `expr`, or of `function`'s return type (signature_return). `errors`: what
    // makes it so, for isolatedDeclarations (none: the expression itself, or its declaration)
    PSEUDO_INFERRED,
    // The declaration's type isn't syntactic at all (no annotation, nothing to infer from): `decl`
    PSEUDO_NO_RESULT,
    // A literal that is its literal type in a const context, else the widened one: `at`, `const_type`, `regular_type`
    PSEUDO_MAYBE_CONST,
    PSEUDO_UNION, // `types`
    PSEUDO_UNDEFINED,
    PSEUDO_NULL,
    PSEUDO_ANY,
    PSEUDO_STRING,
    PSEUDO_NUMBER,
    PSEUDO_BIGINT,
    PSEUDO_BOOLEAN,
    PSEUDO_FALSE,
    PSEUDO_TRUE,
    PSEUDO_SIGNATURE, // A function expression or arrow function: `function`, `params`, `ret`
    PSEUDO_TUPLE, // An `as const` array literal: `types`
    PSEUDO_OBJECT, // An object literal: `members`
    // Literals: `expr` is the literal (or `-1`, `-1n`), printed as written
    PSEUDO_STRING_LITERAL,
    PSEUDO_NUMERIC_LITERAL,
    PSEUDO_BIGINT_LITERAL,
} PseudoKind;

// A node tsgo's ReportInferenceFallback is given (the declaration emitter's isolatedDeclarations diagnostic for it is
// chosen by its kind, createGetIsolatedDeclarationErrors): an Inferred pseudo type's error nodes (what makes it so), its
// expression, a NoResult's declaration, a member or parameter whose pseudo type isn't equivalent
typedef enum PseudoErrorKind : uint8_t {
    PSEUDO_ERROR_OBJECT_MEMBER, // `item`: an object literal's member (a shorthand property, spread assignment, private name...)
    PSEUDO_ERROR_COMPUTED_NAME, // `item`'s computed name (TS9038)
    PSEUDO_ERROR_ARRAY, // `expr`: an array literal that isn't `as const` (TS9017)
    PSEUDO_ERROR_SPREAD_ELEMENT, // `expr`: a spread in an `as const` array literal (TS9018)
    PSEUDO_ERROR_CLASS, // `expr`: a class expression (TS9022)
    PSEUDO_ERROR_ACCESSOR, // `function`: an accessor without annotations (TS9009)
    PSEUDO_ERROR_PARAMETER, // `function`'s parameter `arg` (its initializer's type, TS9011 / TS9025)
    PSEUDO_ERROR_EXPRESSION, // `expr`: an expression whose type isn't syntactic (TS9013 and friends)
    PSEUDO_ERROR_FUNCTION, // `function`: its return type (TS9007, TS9008; an accessor's: TS9009)
    PSEUDO_ERROR_VARIABLE, // `binding`: a variable declaration (TS9010)
    PSEUDO_ERROR_FIELD, // `field`: a class's property declaration (TS9012)
} PseudoErrorKind;

typedef struct PseudoError {
    PseudoErrorKind kind;
    union {
        ObjectExprItem const* item;
        Expr const* expr;
        FunctionImpl const* function;
        InitializedBinding const* binding;
        ClassItem const* field;
    };
    FunctionArg const* arg;
} PseudoError;

// Where pseudoTypeEquivalentToType reports (reportErrors): each node it hands ReportInferenceFallback
typedef struct PseudoReporter {
    void (*report)(void* ctx, PseudoError const* node);
    void* ctx;
} PseudoReporter;

// What a NoResult pseudo type is the declaration of
typedef enum PseudoDeclKind : uint8_t {
    PSEUDO_DECL_VARIABLE, // `binding`
    PSEUDO_DECL_PARAMETER, // `function`'s `arg`
    PSEUDO_DECL_OBJECT_MEMBER, // `item`
    PSEUDO_DECL_FUNCTION, // `function`'s return type (or an accessor's type)
    PSEUDO_DECL_FIELD, // `field`
} PseudoDeclKind;

typedef struct Pseudo Pseudo;

typedef struct PseudoParam {
    FunctionArg const* arg;
    bool rest, optional;
    Pseudo const* type;
} PseudoParam;

typedef enum PseudoMemberKind : uint8_t {
    PSEUDO_MEMBER_METHOD, // `function`, `params`, `type` (the return type)
    PSEUDO_MEMBER_PROPERTY, // `type`, `readonly`
    PSEUDO_MEMBER_SETTER, // `function`, `params` (the one parameter)
    PSEUDO_MEMBER_GETTER, // `function`, `type`
} PseudoMemberKind;

typedef struct PseudoMember {
    PseudoMemberKind kind;
    bool optional;
    bool readonly; // A getter without a setter, as a property
    ObjectExprItem const* item; // Its name
    FunctionImpl const* function;
    PseudoParam const* params;
    u32 param_count;
    Pseudo const* type;
} PseudoMember;

struct Pseudo {
    PseudoKind kind;
    bool signature_return; // PSEUDO_INFERRED: of `function`'s return type
    bool predicate; // PSEUDO_DIRECT
    // PSEUDO_MAYBE_CONST, PSEUDO_OBJECT: in a const context (its literals are literal types, its members readonly). By
    // syntax only, like tsgo's printer finds it: the checker's isConstContext when printing sees the instantiated
    // contextual type, never a const type parameter
    bool in_const;
    PseudoDeclKind decl_kind; // PSEUDO_NO_RESULT
    u32 count; // types, params, members, errors
    union {
        TypeNode const* node; // PSEUDO_DIRECT
        Expr const* expr; // PSEUDO_INFERRED (unless signature_return), literals, PSEUDO_OBJECT (the object literal)
        Expr const* at; // PSEUDO_MAYBE_CONST
        FunctionImpl const* function; // PSEUDO_SIGNATURE, PSEUDO_INFERRED with signature_return, PSEUDO_NO_RESULT
        InitializedBinding const* binding; // PSEUDO_NO_RESULT of a variable
        ObjectExprItem const* item; // PSEUDO_NO_RESULT of an object literal's member
        ClassItem const* field; // PSEUDO_NO_RESULT of a class's property declaration
    };
    FunctionArg const* arg; // PSEUDO_NO_RESULT of a parameter
    union {
        Pseudo const* const* types; // PSEUDO_UNION, PSEUDO_TUPLE
        PseudoParam const* params; // PSEUDO_SIGNATURE
        PseudoMember const* members; // PSEUDO_OBJECT
        PseudoError const* errors; // PSEUDO_INFERRED
        Pseudo const* const_type; // PSEUDO_MAYBE_CONST
    };
    union {
        Pseudo const* ret; // PSEUDO_SIGNATURE
        Pseudo const* regular_type; // PSEUDO_MAYBE_CONST
    };
};

// The syntax above the node being typed, as far as the pseudochecker looks up (it walks parents, tttt passes this down)
typedef struct PseudoPlace {
    // An ancestor is a call, `satisfies`, an annotated variable, parameter or property, a type assertion other than `as
    // const`, or JSX (tsgo's isContextuallyTyped): local inference is unreliable
    bool contextual;
    // The nearest ancestor that isn't an array or object literal, parentheses, a spread, a property assignment or a
    // prefix unary expression is `as const` (tsgo's IsInConstContext)
    bool in_const;
} PseudoPlace;

// GetTypeOfDeclaration of an object literal's member (a property assignment, shorthand property, method or accessor)
Pseudo const* pseudo_of_object_member(Checker* me, ObjectExprItem const* item);
// GetTypeOfDeclaration of a variable (`binding` declares one name, `declarations`: how many declarations its symbol has)
Pseudo const* pseudo_of_variable(Checker* me, InitializedBinding const* binding, bool is_const, PseudoPlace place);
// GetTypeOfDeclaration of a parameter
Pseudo const* pseudo_of_parameter(Checker* me, FunctionImpl const* function, FunctionArg const* arg);
// GetReturnTypeOfSignature: the annotation, else the single returned expression's
Pseudo const* pseudo_of_return(Checker* me, FunctionImpl const* function);
// GetTypeOfAccessor of a getter or setter (`setter` may be null, `getter` too)
Pseudo const* pseudo_of_accessor(Checker* me, FunctionImpl const* accessor);
// GetTypeOfAccessor of one of a pair (either may be null): the annotation of either, else the getter's return
Pseudo const* pseudo_of_accessors(Checker* me, FunctionImpl const* getter, FunctionImpl const* setter, bool of_setter);
// GetTypeOfDeclaration of a class's property declaration (typeFromProperty)
Pseudo const* pseudo_of_field(Checker* me, ClassItem const* field);
// The node a NoResult pseudo type reports (its declaration)
PseudoError pseudo_no_result_node(Pseudo const* pseudo);
// GetTypeOfExpression
Pseudo const* pseudo_of_expression(Checker* me, Expr const* expr, PseudoPlace place);

// CouldAlreadyReferToUndefinedType
bool pseudo_could_be_undefined(Pseudo const* pseudo);
// A union of the pseudo type and undefined
Pseudo const* pseudo_add_undefined(Checker* me, Pseudo const* pseudo);
// isStructuralPseudoType
bool pseudo_is_structural(Pseudo const* pseudo);

typedef struct Ty Ty;
// What pseudoTypeToNode reports where it meets an Inferred pseudo type: its error nodes, else its expression (its
// function, for a signature's return type), except that a name whose parent is a declaration reports that declaration:
// `property` the object literal member whose type it is, `function` the arrow function whose body it is (else null)
void pseudo_report_inferred_node(PseudoReporter const* reporter, Pseudo const* pseudo, ObjectExprItem const* property,
    FunctionImpl const* function);
// What equivalence (and pseudoTypeToNodeWithCheckerFallback) reports for an Inferred pseudo type: its error nodes, else
// its expression (its function)
void pseudo_report_inferred(PseudoReporter const* reporter, Pseudo const* pseudo);
// tsc's isEntityNameExpression: a name, or a dotted name of names
bool pseudo_is_entity_name(Expr const* expr);

// pseudoTypeToType (ty_display.c): the type a pseudo type stands for where it is direct, null for the structures
Ty const* ty_pseudo_type(Checker* me, Pseudo const* pseudo);
// pseudoTypeEquivalentToType (ty_display.c). `optional`: the declaration is optional (its type's undefined isn't in the
// pseudo type). `reporter`: null, or where its reportErrors go
bool ty_pseudo_equivalent(Checker* me, Pseudo const* pseudo, Ty const* ty, bool optional, PseudoReporter const* reporter);
