#pragma once

#include "interface.h"
#include "value.h"
#include "span.h"

typedef struct IdentifierType IdentifierType;
typedef struct InferredType InferredType;
typedef struct TernaryType TernaryType;
typedef struct IndexedType IndexedType;
typedef struct KeyofType KeyofType;
typedef struct TypeofType TypeofType;
typedef struct CallableType CallableType;
typedef struct TupleElement TupleElement;
typedef struct TemplateType TemplateType;
typedef struct ErrorCtx ErrorCtx;
typedef struct Scope Scope;

// How a declaration statement is exported
typedef enum DeclExported : uint8_t {
    DECL_EXPORTED_NO = 0,
    DECL_EXPORTED_YES,
    DECL_EXPORTED_YES_DEFAULT,
} DeclExported;

// What the modifiers of a declaration statement say
typedef struct DeclStatementProps {
    DeclExported exported;
    bool declared;
    Span declare_keyword; // An explicit `declare` (declared is also true in declaration files and some ambient contexts)
    // Decorators on a declaration that can't have them (`@dec var x`, TS1206): owned, null if none. Only resolved
    // (silently) and typed for the baselines, like tsc's decorators of such a node
    struct vec_expr* invalid_decorators;
} DeclStatementProps;

typedef struct Generic Generic;
typedef struct OuterGenerics OuterGenerics; // generic.h
typedef struct CallExpr CallExpr;
typedef struct Expr Expr;
typedef struct LazyType LazyType;
typedef struct Ty Ty; // A semantic type, see ty/ty.h

g_declare_arc(Expr, expr);

// Symbols (scope.h) are not owned by the AST nodes that point at them: they live as long as the program
typedef struct Symbol Symbol;
typedef struct ModuleRef ModuleRef; // source_file.h
typedef Symbol const* SymbolCPtr;
g_declare_vec(SymbolCPtr, symbol_cptr);

// What `this` refers to somewhere
typedef enum ThisKind {
    THIS_NONE = 0,
    THIS_CLASS_INSTANCE, // symbol: the class
    THIS_CLASS_STATIC,   // symbol: the class
    THIS_ANY,            // A function that isn't a method or arrow function
    THIS_PARAMETER,      // symbol: the `this` parameter
    THIS_INTERFACE,      // symbol: the interface (only a `this` type means something there)
    THIS_GLOBAL,         // The top level of a script: `typeof globalThis`
    THIS_NAMESPACE,      // The body of a namespace: TS2331, any
    THIS_ENUM,           // An enum member's initializer: TS2332, any
    THIS_TYPE_LITERAL,   // A `this` type in a member of a type literal: TS2526 (its members are `this` containers)
} ThisKind;

typedef struct ThisRef {
    ThisKind kind;
    Symbol const* symbol;
    struct InterfaceDecl* interface; // THIS_INTERFACE: the declaration, which learns that it mentions `this`
    struct FunctionImpl const* function; // THIS_ANY: the function whose `this` it is (contextually typed, maybe)
    struct ClassDecl const* class_decl; // THIS_CLASS_INSTANCE/STATIC: the class
    bool type_boundary; // A function that isn't a method: a `this` *type* in it has nothing to be about (TS2526)
} ThisRef;
g_declare_vec(TupleElement, tuple_element);

// A declaration that can have a `unique symbol` type (tsc's isValidESSymbolDeclaration): a `const` variable, a `static
// readonly` class property or a `readonly` property signature. Set by the resolve walk on its `unique symbol`
// annotation and on a `Symbol()` initializer, which then both mean the same type
typedef struct UniqueSymbolDecl {
    void const* key; // What the type's identity is. Null: not such a declaration, the type is plain `symbol`
    Symbol const* owner; // The variable, or the class/interface the member belongs to (may be null for type literals)
    Span member; // The member's name, empty for a variable
} UniqueSymbolDecl;

// The true branches of conditional types a type node is in, innermost first, as tsc's getConditionalFlowTypeOfType
// walks up to them from the node (up to the enclosing statement), and the templates of mapped types without `as`.
// Set by the resolve walk (type_lookup_names()), so a node lowers to the same type whoever lowers it: see
// conditional_flow_type() (lower.c)
typedef struct TypeFlow {
    struct TernaryType const* ternary; // Null for a mapped type's template
    struct InterfaceMember const* mapped;
    bool covariant; // An even number of parameter positions lie between the true branch and the node
    struct TypeFlow const* outer;
} TypeFlow;

typedef struct TypeNode {
    enum : uint8_t {
        TYPE_ANY = 0,
        TYPE_UNKNOWN,
        TYPE_NEVER,
        TYPE_VOID,
        TYPE_BOOLEAN,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_SYMBOL,
        TYPE_BIGINT,
        TYPE_VALUE,
        TYPE_IDENTIFIER,
        TYPE_INFERRED,
        TYPE_CALLABLE,
        TYPE_INTERFACE,
        TYPE_ARRAY,
        TYPE_TUPLE,
        TYPE_INDEXED,
        TYPE_KEYOF,
        TYPE_TYPEOF,
        TYPE_TERNARY,
        TYPE_UNION,
        TYPE_INTERSECTION,
        TYPE_UNIQUE_SYMBOL,
        TYPE_TEMPLATE,
        // A JSDoc-only form written outside JSDoc (tsc's JSDocNullableType/JSDocNonNullableType: `?T`, `T?`, `!T`, `T!`),
        // which the checker reports (TS17019/TS17020); everything else goes by `jsdoc->type`, what it means in JSDoc
        TYPE_JSDOC,
        // A type predicate outside a return type position (`x: this is T`, `asserts x` anywhere: tsc's
        // parseNonArrayType parses them there). It means boolean (void for `asserts`), and the checker reports TS1228
        TYPE_PREDICATE,
    } tag;
    bool parsed : 1;
    bool parenthesized : 1; // Written in parentheses (the types baseline reprints annotations as written)
    bool placeholder : 1; // The empty operand before a leading `|` or `&` (`| A | B`), not written
    // JSDoc `T=`: the union `T | undefined` it means, which an `@param` takes for an optional parameter of type `T`
    bool jsdoc_optional : 1;
    // JSDoc `?T`/`T?`: the union `T | null` it means, one member of an enclosing union (tsgo's JSDocNullableType node)
    bool jsdoc_nullable : 1;
    // JSDoc `!T`: T itself, but no array or tuple to a `readonly` (tsgo's JSDocNonNullableType node, TS1354)
    bool jsdoc_non_null : 1;
    // `readonly T` of something other than an array or tuple (TS1354): T's type, reprinted with its `readonly`
    bool readonly_invalid : 1;
    // An annotation that a JSDoc tag gave (the root of its type): not part of the baselines' walk
    bool from_jsdoc : 1;
    bool synthesized : 1; // Not written: the `undefined` of JSDoc's `T=`, the `null` of `?T` (not reused when printed)
    // Where it starts in its file's text plus one (0: not parsed there, or not recorded: only with declarationMap), its
    // first token's (`readonly`, a parenthesized one's `(`): declaration maps map the nodes declaration emit reuses
    // (decl_map_type_start(), decl_map.h)
    u32 pos;
    TypeFlow const* flow; // Null outside true branches
    union {
        struct {
            bool self_referential_error;
            Span unkown_symbol_span; // Used by tests
        } any;
        struct {
            Value value;
            Span value_span; // Where it was written, if it was (for the types baseline)
        };
        IdentifierType* identifier;
        InferredType* inferred;
        CallableType* callable;
        Interface interface;
        struct {
            TypeNode* element;
            bool readonly;
            Span span; // The element to `]` (tsc's ArrayTypeNode, where TS4109 goes) when parsed as `T[]`, else null
        } array;
        struct {
            VecTupleElement elements;
            bool readonly;
            Span span; // `[` to `]` (tsc's TupleTypeNode, where TS4110 goes)
        } tuple;
        IndexedType* indexed;
        KeyofType* keyof;
        TypeofType* typeof_;
        TernaryType* ternary;
        VecTypeNode composite; // used for both TYPE_UNION and TYPE_INTERSECTION
        UniqueSymbolDecl unique_symbol;
        TemplateType* template_;
        struct JsdocType* jsdoc;
        struct TypePredicateNode* predicate;
    };
} TypeNode;

typedef struct JsdocType {
    TypeNode type; // What it means: `T | null` for `?T`/`T?` (the union's first member is T), T for `!T`/`T!`
    Span span; // The whole form, `?` or `!` included
    bool non_null; // `!`, else `?`
    bool postfix;
} JsdocType;

extern const TypeNode type_any;
extern const TypeNode type_self_referential_error;
extern const TypeNode type_unknown;
extern const TypeNode type_never;
extern const TypeNode type_void;
extern const TypeNode type_object;
extern const TypeNode type_boolean;
extern const TypeNode type_number;
extern const TypeNode type_string;
extern const TypeNode type_symbol;
extern const TypeNode type_bigint;
extern const TypeNode type_null;
extern const TypeNode type_undefined;

static inline TypeNode type_value(Value value) {
    return (TypeNode){
        .tag = TYPE_VALUE,
        .value = value,
    };
}

// type_identifier declared in several_types.h

static inline TypeNode type_inferred(InferredType* inferred) {
    return (TypeNode){
        .tag = TYPE_INFERRED,
        .inferred = inferred,
    };
}

static inline TypeNode type_callable(CallableType* callable) {
    return (TypeNode){
        .tag = TYPE_CALLABLE,
        .callable = callable,
    };
}

static inline TypeNode type_interface(Interface interface) {
    return (TypeNode){
        .tag = TYPE_INTERFACE,
        .interface = interface,
    };
}

static inline TypeNode type_array(TypeNode* element, bool readonly) {
    return (TypeNode){
        .tag = TYPE_ARRAY,
        .array = {
            .element = element,
            .readonly = readonly,
        }
    };
}

static inline TypeNode type_tuple(VecTupleElement elements, bool readonly) {
    return (TypeNode){
        .tag = TYPE_TUPLE,
        .tuple = {
            .elements = elements,
            .readonly = readonly,
        }
    };
}

static inline TypeNode type_indexed(IndexedType* indexed) {
    return (TypeNode){
        .tag = TYPE_INDEXED,
        .indexed = indexed,
    };
}

static inline TypeNode type_keyof(KeyofType* keyof) {
    return (TypeNode){
        .tag = TYPE_KEYOF,
        .keyof = keyof,
    };
}

static inline TypeNode type_typeof(TypeofType* typeof_) {
    return (TypeNode){
        .tag = TYPE_TYPEOF,
        .typeof_ = typeof_,
    };
}

static inline TypeNode type_ternary(TernaryType* ternary) {
    return (TypeNode){
        .tag = TYPE_TERNARY,
        .ternary = ternary,
    };
}

// type_composite() and type_composite_without_simplify() declared below

static inline bool type_is_self_referential(TypeNode const* me) {
    return my tag == TYPE_ANY && my any.self_referential_error;
}

TypeNode type_composite_without_simplify(typeof((TypeNode){}.tag) tag, TypeNode a, TypeNode b);
static inline TypeNode type_composite_base(typeof((TypeNode){}.tag) tag) {
    switch (tag) {
        case TYPE_UNION:        return (TypeNode){.tag = TYPE_NEVER};
        case TYPE_INTERSECTION: return (TypeNode){.tag = TYPE_ANY};
        default:                panic("Invalid composite tag");
    }
}

// PERF: should type_drop() be inlined?
void type_drop(TypeNode* me);
static inline TypeNode type_move(TypeNode* me) {
    TypeNode ret = *me;
    my tag = TYPE_ANY;
    return ret;
}

void type_lookup_names(TypeNode* me, Scope* names, ErrorCtx* errs);
// Set while looking up an import type's type arguments: tsc's checkImportType doesn't check them, getTypeFromTypeNode
// only resolves what isn't deferred, so the names in type literals and signatures in there say nothing
extern thread_local bool type_lookup_lazy_silent;
// Parameter positions flip the variance of the true branches around them (TypeNode.flow): call around the walk of a
// parameter's type, then restore with the result
TypeFlow const* type_flow_enter_parameter(void);
// Statements (a function body) end the walk up: none of the true branches around apply inside
TypeFlow const* type_flow_enter_statements(void);
TypeFlow const* type_flow_enter_mapped_template(struct InterfaceMember* mapped);
void type_flow_restore(TypeFlow const* saved);
// A type literal with index signatures whose members type_typecheck() checks against them (TS2411/TS2413)
bool type_literal_has_index_constraints(TypeNode const* me);
void type_typecheck(TypeNode const* me, ErrorCtx* errs);
#define g_class_key TypeNode, type
#define g_no_clone
#define g_declared
#include <cleat/generic/vec.h>

#define g_key SymbolCPtr, symbol_cptr
#define g_declared
#include <cleat/generic/vec.h>

typedef struct SpannedType {
    Span span;
    TypeNode type;
} SpannedType;
static inline SpannedType spanned_type(Span span, TypeNode type) { return (SpannedType){span, type}; }
static inline void spanned_type_drop(SpannedType* me) { type_drop(&my type); }

#define g_class_key SpannedType, spanned_type
#define g_no_clone
#include <cleat/generic/vec.h>

static inline TypeNode const* generic_param_at(VecSpannedType const* params, intptr_t i) {
    return i < vec_spanned_type_size(params) ? &vec_spanned_type_at(params, i)->type : &type_any;
}

typedef enum KeywordIdentifier {
    KEYWORD_IDENTIFIER_NONE = 0,
    KEYWORD_IDENTIFIER_THIS,
    KEYWORD_IDENTIFIER_SUPER,
    KEYWORD_IDENTIFIER_IMPORT, // `import("./a")` and `import.meta`
    KEYWORD_IDENTIFIER_NEW_TARGET, // `new.target`, the name spans all of it. target: the class or function it is
} KeywordIdentifier;
