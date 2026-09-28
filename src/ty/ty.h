#pragma once

// The type core. Semantic types are `Ty`: immutable once published, allocated in the arena of a `Checker`, and
// interned, so pointer equality is type identity. `TypeNode` (type.h) is the syntax of a type; `ty_from_node()` in
// lower.c is the only producer of `Ty` from it. Everything that has a type (expressions, declarations, functions...)
// evaluates to a `Ty`, memoised per checker through its LazyType (lazy_type.h).

#include "type.h"
#include "lazy_type.h"
#include "generic.h"

typedef struct Ty Ty;
typedef struct InitializedBinding InitializedBinding;
typedef struct FunctionImpl FunctionImpl;
typedef struct IdentifierExpr IdentifierExpr;
typedef struct ObjectExprItem ObjectExprItem;
typedef struct Expr Expr;
typedef struct CallExpr CallExpr;
typedef struct PropertyExpr PropertyExpr;
typedef struct Checker Checker;
typedef struct Prop Prop;
typedef struct Signature Signature;
typedef struct Members Members;
typedef struct TyMapper TyMapper;
typedef struct TyCondBranch TyCondBranch;
typedef struct GenericItem GenericItem;
typedef struct Program Program;
typedef struct ClassDecl ClassDecl;

// Arena

#include "arena.h"

// An interned string. Pointer equality is string equality (within one Checker).
typedef struct Atom {
    u32 len;
    char str[];
} Atom;
static inline csview atom_sv(Atom const* me) { return me ? csview_from_n(my str, my len) : c_sv(""); }

// Intern table: open addressing, keys are arena-allocated arrays of u64 words (a kind tag followed by ids/pointers/
// bits), values are pointers. Used for every kind of identity in the checker.
typedef struct InternEntry {
    u64 hash;
    u64 const* key; // key[0] is the number of words that follow
    void const* value;
} InternEntry;

typedef struct InternTable {
    InternEntry* entries;
    u32 cap; // power of two or 0
    u32 len;
} InternTable;

typedef struct TyList {
    Ty const* const* data;
    u32 len;
} TyList;

// A growable list of types for building unions and the like. Zero-initialize, drop when done
typedef struct TyBuf TyBuf;
struct TyBuf {
    Ty const** data;
    u32 len;
    u32 cap;
    Ty const* inline_data[16];
};

static inline void ty_buf_push(TyBuf* me, Ty const* ty) {
    if (!my data) {
        my data = my inline_data;
        my cap = c_arraylen(my inline_data);
    }
    if (my len == my cap) {
        my cap *= 2;
        if (my data == my inline_data) {
            my data = malloc(sizeof(Ty const*) * my cap);
            panic_if(!my data, "out of memory");
            memcpy(my data, my inline_data, sizeof(my inline_data));
        } else {
            my data = realloc(my data, sizeof(Ty const*) * my cap);
            panic_if(!my data, "out of memory");
        }
    }
    my data[my len++] = ty;
}

static inline void ty_buf_drop(TyBuf* me) {
    if (my data != my inline_data) free(my data);
}

typedef enum TyKind {
    TY_INTRINSIC,
    TY_LITERAL,
    TY_TYPE_PARAM,
    TY_DECLARED,    // interface, class instance, class static side, enum, enum object, namespace value, tuple target
    TY_REFERENCE,   // generic declared type (or tuple target) + type arguments. Interned by (target, args)
    TY_ANONYMOUS,   // type literal, function type, mapped type, or an instantiation of one
    TY_UNION,
    TY_INTERSECTION,
    TY_KEYOF,       // The rest are deferred types: they only exist while an operand is generic (or, for now, while
    TY_INDEXED,     // the new core lacks the machinery to resolve them)
    TY_CONDITIONAL,
    // tsc's substitution type: a type variable (or a type mentioning one) in the true branch of a conditional type whose
    // check type it is, with what the extends type implies about it (`T` in `T extends string ? Lowercase<T> : never`).
    // Relations read it as `base & constraint` on the source side and `base` on the target side. `NoInfer<T>` is one
    // with an unknown constraint (ty_no_infer()). Prints as its base
    TY_SUBSTITUTION,
    // String-like types (template.c). Not TYF_DEFERRED unless generic: a pattern like `a${string}` is as concrete as a
    // primitive
    TY_TEMPLATE_LITERAL,
    TY_STRING_MAPPING,
} TyKind;

// tsc-like TypeFlags, plus a few cached facts
typedef enum TyFlags {
    TYF_ANY             = 1 << 0,
    TYF_UNKNOWN         = 1 << 1,
    TYF_NEVER           = 1 << 2,
    TYF_VOID            = 1 << 3,
    TYF_UNDEFINED       = 1 << 4,
    TYF_NULL            = 1 << 5,
    TYF_STRING          = 1 << 6,
    TYF_NUMBER          = 1 << 7,
    TYF_BIGINT          = 1 << 8,
    TYF_ES_SYMBOL       = 1 << 9,
    TYF_NON_PRIMITIVE   = 1 << 10, // `object`
    TYF_STRING_LITERAL  = 1 << 11,
    TYF_NUMBER_LITERAL  = 1 << 12,
    TYF_BIGINT_LITERAL  = 1 << 13,
    TYF_BOOLEAN_LITERAL = 1 << 14,
    TYF_ENUM_LITERAL    = 1 << 15, // in addition to the string/number literal flag
    TYF_BOOLEAN         = 1 << 16, // the `true | false` union
    TYF_TYPE_PARAM      = 1 << 17,
    TYF_OBJECT          = 1 << 18, // declared, reference, anonymous
    TYF_UNION           = 1 << 19,
    TYF_INTERSECTION    = 1 << 20,
    TYF_DEFERRED        = 1 << 21, // keyof, indexed access, conditional
    TYF_ENUM            = 1 << 22, // an enum member without a constant value (tsc's computed enum type), TY_LITERAL
    TYF_UNIQUE_ES_SYMBOL = 1 << 23, // `unique symbol`, one per declaration: TY_LITERAL with `literal.unique`
    // Cached facts
    TYF_ERROR                        = 1 << 24, // the `any` that stands for "an error was already reported"
    TYF_COULD_CONTAIN_TYPE_VARIABLES = 1 << 25, // if unset, instantiate() is the identity
    // The fresh twin of a literal type (tsc's freshType): what a literal expression evaluates to, and the only kind of
    // literal that widens at a mutable location. On a union: some member is fresh (ty_regular() has work to do)
    TYF_FRESH_LITERAL                = 1 << 26,
    // A fresh object literal type (tsc's ObjectFlags.FreshLiteral): what an object literal expression evaluates to, and
    // the only kind of source that gets excess property checks. On unions and array/tuple references: one is inside
    // (ty_widened_objects() has work to do)
    TYF_FRESH_OBJECT                 = 1 << 27,
    TYF_TEMPLATE_LITERAL             = 1 << 28, // `a${B}` (TY_TEMPLATE_LITERAL)
    TYF_STRING_MAPPING               = 1 << 29, // Uppercase<T> & co. (TY_STRING_MAPPING)
    // Nothing is inferred from it (tsc's ObjectFlags.NonInferrableType): the silent never that stands for what an outer
    // inference hasn't found yet, and every union, intersection, reference or alias instantiation with one in it
    TYF_NON_INFERRABLE               = 1u << 30,
    // What a binding pattern implies, as the contextual type of its initializer (tsc's `pattern` on the clone
    // getTypeFromBindingPattern makes with includePatternInType): the return type of a generic call isn't inferred from
    // it alone. A type of its own, never interned
    TYF_BINDING_PATTERN              = 1u << 31,

    TYF_NULLABLE = TYF_UNDEFINED | TYF_NULL,
    TYF_ES_SYMBOL_LIKE = TYF_ES_SYMBOL | TYF_UNIQUE_ES_SYMBOL,
    TYF_LITERAL = TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_BIGINT_LITERAL | TYF_BOOLEAN_LITERAL,
    TYF_STRING_LIKE = TYF_STRING | TYF_STRING_LITERAL | TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING,
} TyFlags;

// The intrinsic string mapping aliases of the lib (`type Uppercase<S extends string> = intrinsic`)
typedef enum TyStringMapping {
    TY_MAPPING_UPPERCASE,
    TY_MAPPING_LOWERCASE,
    TY_MAPPING_CAPITALIZE,
    TY_MAPPING_UNCAPITALIZE,
} TyStringMapping;

typedef enum TyDeclaredKind {
    TY_DECLARED_INTERFACE,
    TY_DECLARED_CLASS_INSTANCE,
    TY_DECLARED_CLASS_STATIC,
    TY_DECLARED_ENUM_OBJECT,
    TY_DECLARED_NAMESPACE,
    TY_DECLARED_TUPLE_TARGET,
    TY_DECLARED_SYNTHETIC, // `Array`/`ReadonlyArray` stand-ins when the program has no lib
} TyDeclaredKind;

typedef enum TupleElementFlags {
    TUPLE_ELEMENT_OPTIONAL = 1 << 0,
    TUPLE_ELEMENT_REST     = 1 << 1,
} TupleElementFlags;

typedef struct TupleShape {
    u32 arity;
    bool readonly;
    unsigned char const* flags; // TupleElementFlags per element
    Atom const* const* names; // per element, null if unnamed. Null if no element is named
} TupleShape;

typedef enum LazyState {
    LAZY_STATE_NOT_STARTED = 0,
    LAZY_STATE_IN_FLIGHT,
    LAZY_STATE_DONE,
} LazyState;

// Where lowering happens: inherited by everything lowered lazily from inside an object type
typedef struct LowerCtx {
    bool generic; // some type parameter is in scope, so anonymous object types could mention one
    bool method; // the node being lowered is the type of a method (its signatures compare parameters bivariantly)
    bool in_alias; // directly inside a type alias that is being resolved: array/tuple nodes may need to be deferred
} LowerCtx;

// The mutable, lazily resolved part of an object type. Everything else about a Ty is immutable once published.
typedef struct TyObjectState {
    LazyState members_state;
    uint8_t base_state; // LazyState of equivalent_base
    Members const* members;
    Ty const* equivalent_base; // See ty_non_augmenting_base()
} TyObjectState;

typedef struct TyParamState {
    GenericItem const* item; // Null for type parameters that were only ever seen by reference (and infer/mapped ones)
    TypeNode const* constraint_node;
    LazyState constraint_state;
    Ty const* constraint; // null if unconstrained
    LazyState fallback_state;
    Ty const* fallback;
    bool is_infer; // declared by `infer X` in the extends type of a conditional type
    bool is_this; // The `this` type of a class or interface (Ty.declared.this_param), prints as `this`
    // A fresh copy made when a generic signature is instantiated (tsc's cloneTypeParameter): its constraint and default
    // are those of `clone_of` instantiated with `clone_mapper`
    Ty const* clone_of;
    TyMapper const* clone_mapper;
} TyParamState;

struct Ty {
    u32 id;
    u32 kind; // TyKind
    u32 flags; // TyFlags
    // How to print the type if it was made by a type alias (tsc's aliasSymbol/aliasTypeArguments)
    Symbol const* alias;
    TyList alias_args;
    union {
        struct {
            csview name;
        } intrinsic;
        struct {
            Value value; // Strings are borrowed from `string`
            Atom const* string;
            // Enum literals (TYF_ENUM_LITERAL): the enum, and the (first) member with this value. A member whose
            // value isn't constant has TYF_ENUM instead of a literal flag, and no value
            Symbol const* enum_decl;
            Symbol const* enum_member;
            // Unique symbols (TYF_UNIQUE_ES_SYMBOL): the declaration. `string` is the escaped property name its
            // members are keyed by (tsc's `__@name@id`)
            UniqueSymbolDecl const* unique;
            Ty const* regular; // TYF_FRESH_LITERAL: the regular twin. Null for regular literals
        } literal;
        struct {
            Symbol const* symbol; // null for synthesized ones
            Atom const* name;
            TyParamState* state;
        } param;
        struct {
            TyDeclaredKind kind;
            Symbol const* symbol; // null for tuple targets and synthetics
            struct Symbol const* namespace_symbol; // TY_DECLARED_NAMESPACE only: the namespace, enum or module
            // TY_DECLARED_NAMESPACE of a CommonJS module as its `module.exports` (tsc's synthetic symbol of that name):
            // named `name` wherever it's printed
            bool commonjs_exports;
            // Set if this is the value of a declaration with several value declarations (a namespace merged with a
            // function, class or enum): the members are those of all of them, see ty_merged_value_type()
            Symbol const* merged_value;
            Atom const* name;
            // tsc's typeParameters: the outer ones (tsc's outerTypeParameters: those of the functions, classes... a
            // local class or interface is declared in, see OuterGenerics) and then its own (localTypeParameters),
            // which are what type arguments are written for. The static side of a class has only the outer ones
            TyList type_params;
            u32 outer_count;
            OuterGenerics const* outer; // Where the outer ones come from (display: `f<number>.C`)
            // Classes, and interfaces that mention `this` (tsc's isThislessInterface): the polymorphic `this` type, a
            // type parameter constrained to the type itself. `state` has the members with `this` = the type itself,
            // `raw` the members as declared (mentioning this_param), what references instantiate. Null otherwise
            Ty const* this_param;
            TyObjectState* raw;
            TupleShape const* tuple;
            TyObjectState* state;
        } declared;
        struct {
            Ty const* target; // TY_DECLARED
            TyList args; // Don't read directly, use ty_reference_args(): empty until resolved if `deferred` is set
            // What the target's `this` type is in the members (tsc's getTypeWithThisArgument): null means the reference
            // itself. Part of the identity: `C` with this-argument `T` (the apparent type of `T extends C`) is a
            // reference of its own, even to a target without type parameters
            Ty const* this_arg;
            // A deferred reference (tsc's deferred type reference): an array or tuple node inside the type alias that
            // is being resolved, `type Json = string | Json[]`. Identity is (node, mapper), arguments resolve on
            // first use
            struct TyDeferredRef* deferred;
            TyObjectState* state;
        } reference;
        struct {
            TypeNode const* node; // TYPE_INTERFACE or TYPE_CALLABLE. Null for instantiations and functions
            // The type of a function declaration/expression, method, accessor or constructor: one signature, read
            // straight off the FunctionImpl (the return type is inferred from its body if it isn't annotated)
            FunctionImpl const* function;
            Symbol const* merged_value; // Function overloads, see ty_merged_value_type()
            FunctionImpl const* const* overloads; // If set, the type of an overloaded method: its signatures
            u32 overload_count;
            Ty const* target; // Instantiations: the uninstantiated TY_ANONYMOUS
            TyMapper const* mapper;
            // An instantiation expression (`Bar<string>` as a value): target is any object type, and the mapper is
            // about the type parameters of its signatures, which lose them
            bool applies_type_args;
            bool realiased; // the same type as `target` + `mapper` by another alias name, see ty_realias()
            LowerCtx ctx;
            // Uninstantiated only: the type parameters declared outside the node that it mentions. Instantiation
            // only cares about these. If unknown (the node has parts that aren't syntax), assume every one in scope
            TyList free_params;
            bool free_params_unknown;
            bool object_literal; // Made by an object literal expression (ty_object_literal())
            bool jsx_attributes; // OBJECT_JSX_ATTRIBUTES(_FRESH): hyphenated names are never excess, and excess ones say so differently
            bool js_literal; // OBJECT_JS: an object literal of a JavaScript file (tsc's ObjectFlags.JSLiteral)
            TyObjectState* state;
        } anonymous;
        struct {
            TyList types; // sorted by id, no duplicates
            TyObjectState* state; // Intersections: their signatures and index infos (see ty_members())
        } composite;
        struct {
            Ty const* operand;
        } keyof;
        struct {
            Ty const* object;
            Ty const* index;
        } indexed;
        struct {
            Ty const* check;
            Ty const* extends;
            // Don't read the branches directly, use ty_conditional_branch(): a branch that mentions the type alias
            // being resolved (`type R<T> = T extends [infer H, ...infer Rest] ? R<Rest> : []`) is a node to lower on
            // first use, plus the mapper it has been instantiated with since
            TyCondBranch* branches; // [0] false, [1] true
            bool distributive; // the check type was written as a naked type parameter
            // Like tsc's ConditionalRoot + mapper: the conditional type as it was written, and how to get from there
            // to this one (null for the root itself). Instantiation always starts over from the root, because
            // distribution has to see what the *written* check type parameter ends up as (`Exclude<keyof T, K>`
            // inside `Omit<T, K>` only distributes once T is known)
            Ty const* root;
            TyMapper const* mapper;
        } conditional;
        struct {
            Ty const* base;
            Ty const* constraint;
        } substitution;
        struct {
            Atom const* const* texts; // types.len + 1 of them, around the types
            TyList types; // Each a type variable or a pattern placeholder (string, number, bigint, any, a pattern...)
        } template_literal;
        struct {
            TyStringMapping mapping;
            Symbol const* symbol; // The lib alias, for display. May be null
            Ty const* type; // A type variable, string, any, or a pattern
        } string_mapping;
    };
};

struct TyCondBranch {
    Ty const* type; // null until lowered if `node` or `source` is set
    TypeNode const* node;
    // Or a type to instantiate with `mapper` when the branch is picked: instantiating both branches of a conditional
    // up front would resolve what the untaken one mentions (`Awaited<V>` inside `Awaited<number>`) and never end
    Ty const* source;
    LowerCtx ctx;
    TyMapper const* mapper;
    LazyState state;
};

typedef struct TyDeferredRef {
    TypeNode const* node; // TYPE_ARRAY, TYPE_TUPLE or TYPE_IDENTIFIER (a generic interface or class)
    LowerCtx ctx;
    TyMapper const* mapper; // may be null
    LazyState state;
    bool circular; // Its arguments needed its arguments (TS4109/TS4110 when they're done, see ty_reference_args())
    TyList args;
} TyDeferredRef;

typedef enum PropFlags {
    PROP_OPTIONAL = 1 << 0,
    PROP_READONLY = 1 << 1,
    PROP_METHOD   = 1 << 2,
    PROP_STRIP_UNDEFINED = 1 << 3, // `-?` made it required: undefined is taken out of its type (tsc's StripOptional)
    // An accessor of an object literal (Prop.function): typed on first use by the getter's return type, else the setter's
    // parameter, like tsc's lazily resolved accessor symbols (a getter's body can mention `this`, the literal's type)
    PROP_GETTER = 1 << 4,
    PROP_SETTER = 1 << 5,
    // Class members: `private`/`protected` (dropped by mapped types and spreads like tsc's modifiers), static side,
    // declared as a get/set accessor (a getter-only one is readonly everywhere, even in the constructor)
    PROP_PRIVATE = 1 << 6,
    PROP_PROTECTED = 1 << 7,
    PROP_STATIC = 1 << 8,
    PROP_ACCESSOR = 1 << 9,
    PROP_FIELD = 1 << 10, // A class property declaration (tsc's isClassInstanceProperty when not static)
    // An accessor whose setter is private/protected (PROP_PRIVATE/PROP_PROTECTED are the getter's): writes are checked
    // against the setter
    PROP_SET_PRIVATE = 1 << 11,
    PROP_SET_PROTECTED = 1 << 12,
} PropFlags;

// A property of an object type. Deliberately the seed of plan 03's persistent symbol.
struct Prop {
    Atom const* name;
    u32 flags;
    Symbol const* parent; // the declared type it was declared in, may be null
    // Its declaration can't mention `this` (tsc's isThisless*): instantiating only `this` leaves it as it is
    bool thisless;
    // Exactly one source of the type (or it is known from the start): a node to lower, what the AST evaluates lazily
    // (an inferred class field, an accessor), a method, the overloads of a method, a symbol, a target to instantiate
    TypeNode const* node;
    LazyType const* lazy;
    FunctionImpl const* function; // A class method: ty_function_type()
    FunctionImpl const* const* overloads; // An overloaded method: all of its signatures
    Symbol const* symbol; // A member of a namespace: the type of the symbol as a value
    u32 overload_count;
    LowerCtx ctx;
    Prop const* target;
    TyMapper const* mapper;
    LazyState type_state;
    Ty const* type;
    // A divergent accessor's setter parameter type (tsc's write type), lowered with `ctx`; instantiations follow
    // `target`. See prop_write_type()
    TypeNode const* write_node;
    Ty const* write_type;
    // Where it was declared, for the symbols baseline (not copied to instantiations, follow `target`)
    Span name_span;
    Span const* decl_spans;
    u32 decl_span_count;
    bool name_is_value; // The name was written as a string or number literal
    bool literal_own; // A property of an object literal written in it (not spread): see ObjectLiteralProp.own
    // Declared more than once, not every time with a string literal name (`{ 1: a, "1": b }`): prints as a number
    bool numeric_redeclared;
    // Types baseline only: an object literal property `a: x as T` prints its type by reusing `T` (see ty_display.c)
    TypeNode const* asserted;
    Ty const* mapped_type; // A property of this mapped type (the resolved one): for TS2615, see prop_type()
    struct MappedInfo const* template_of; // The template of this mapped type, as a property its properties instantiate
    bool circular; // Its type needed its type (a mapped type property: TS2615 when done)
};

typedef enum SignatureFlags {
    SIGNATURE_CONSTRUCT = 1 << 0,
    SIGNATURE_ABSTRACT  = 1 << 1,
    SIGNATURE_METHOD    = 1 << 2, // declared with method syntax: parameters are bivariant even under strictFunctionTypes
} SignatureFlags;

typedef struct SigParam {
    Atom const* name;
    Ty const* type;
    bool optional;
    bool rest;
    struct FunctionArg const* decl; // Where it was declared, kept by instantiation. Null for synthesized ones
} SigParam;

// `x is T`, `this is T`, `asserts x`, `asserts x is T`
typedef struct TyPredicate {
    bool is_this;
    bool asserts;
    i32 param_index; // Among Signature.params, -1: no such parameter
    Atom const* param_name;
    Ty const* type; // Null: `asserts x`
} TyPredicate;

struct Signature {
    u32 flags;
    // Exactly one source: a function (a declaration/expression, or a function type node, see FunctionImpl.is_type)
    // or a target to instantiate
    FunctionImpl const* function;
    // Construct signatures of a class: the type parameters are the class's and the return type is its instance type,
    // whatever `node` or `target` say
    Ty const* class_instance;
    TyList class_type_params;
    Span name_span; // For the symbols baseline
    Span decl_span;
    bool drops_mapped_type_params; // Part of an instantiation expression, see Ty.anonymous.applies_type_args
    LowerCtx ctx;
    Signature const* target;
    TyMapper const* mapper;
    // Resolved lazily by signature_resolve()
    LazyState state;
    TyList type_params;
    SigParam const* params;
    u32 param_count;
    u32 min_args;
    Ty const* this_type; // `this: T`, not among params. Null if not declared
    // The return type is lazier than the rest, use signature_return_type(): it's often inferred from a function body
    // that is in the middle of being checked
    LazyState ret_state;
    Ty const* ret;
    TyPredicate const* predicate; // Use signature_predicate()
    bool predicate_done;
};

// A member with a computed name that an object literal's index signature was made of (`[a]: 1`)
typedef struct IndexComponent {
    Expr const* name; // What's in the brackets
    Ty const* type;
} IndexComponent;

typedef struct IndexInfo {
    Ty const* key;
    Ty const* value;
    Atom const* param_name;
    bool readonly;
    Span decl_span; // The index signature as declared, invalid for synthesized ones
    // tsc's IndexInfo.components: the printer writes them instead of the signature when their names are accessible
    IndexComponent const* components;
    u32 component_count;
} IndexInfo;

typedef struct MappedInfo {
    Ty const* type_param;
    Ty const* constraint;
    Ty const* name_type; // `as` clause, may be null
    // The template is lowered/instantiated on first use (tsc's getTemplateTypeFromMappedType): read it through
    // ty_mapped_template(). Lowering it while the mapped type's members are built can reach this mapped type again
    Ty const* template_;
    struct TypeNode const* template_node; // Declared: lowered with template_ctx
    LowerCtx template_ctx;
    struct MappedInfo const* template_from; // Instantiated: template_from's template through template_mapper
    struct TyMapper const* template_mapper;
    LazyState template_state;
    Ty const* modifiers_type; // `T` of a homomorphic `[K in keyof T]`: where optional/readonly come from. May be null
    bool keyof_constraint; // The constraint is written `keyof T` (not `K extends keyof T`): the keys are T's members
    signed char readonly; // -1 remove, +1 add
    signed char optional;
} MappedInfo;
Ty const* ty_mapped_template(Checker* me, MappedInfo const* mapped);

struct Members {
    Prop* const* props; // declaration order
    u32 prop_count;
    Prop* const* props_by_name; // sorted by name atom address, for lookup. Null for small tables
    Signature* const* call;
    u32 call_count;
    Signature* const* construct;
    u32 construct_count;
    IndexInfo const* index;
    u32 index_count;
    // Non-null for a mapped type whose keys aren't known. It has no other members, except `{ [K in keyof T]: X }` with T
    // generic: a property per property of T's constraint (see expand_generic_homomorphic())
    MappedInfo const* mapped;
    MappedInfo const* expanded_from; // The mapped type these are the members of, once its keys are known
};

// sources[i] -> targets[i]. Always flat in stage 1 (composition builds a new flat mapper).
struct TyMapper {
    u32 len;
    Ty const* const* sources; // type parameters
    Ty const* const* targets;
};

typedef enum TyVariance {
    VARIANCE_INVARIANT     = 0,
    VARIANCE_COVARIANT     = 1 << 0,
    VARIANCE_CONTRAVARIANT = 1 << 1,
    VARIANCE_BIVARIANT     = VARIANCE_COVARIANT | VARIANCE_CONTRAVARIANT,
    VARIANCE_INDEPENDENT   = 1 << 2,
    VARIANCE_UNRELIABLE    = 1 << 3, // Measured through something that isn't simply structural: fall back to structure
} TyVariance;

// What the checker knows about a symbol (nothing in a Symbol is written during checking)
typedef struct SymbolLinks {
    LazyState declared_state;
    Ty const* declared_type;
    Ty const* static_type; // See ty_static_type_of()
    bool circular; // A type alias whose resolution reached itself (TS2456 when it finishes, see ty_declared_type_of())
    uint8_t base_cycle; // BASE_CYCLE_* flags, see ty_interface_in_base_cycle()
    LazyState base_variable_state;
    Ty const* base_variable; // See ty_class_base_type_variable()
} SymbolLinks;

typedef enum TyRelation {
    TY_RELATION_ASSIGNABLE,
    TY_RELATION_COMPARABLE,
    // Assignable, except that null and undefined are never assignable to other types, whatever strictNullChecks says.
    // HACK: what the legacy operator checks mean by "extends number"
    TY_RELATION_ASSIGNABLE_STRICT_NULLS,
    // tsc's subtypeRelation (overload resolution's first round, common supertypes, narrowing's fallback): `any` is only
    // a subtype of `any`/`unknown`, and a type that lacks an optional property of the target isn't a subtype of it
    TY_RELATION_SUBTYPE,
    // What union (subtype) reduction uses (tsc's strictSubtypeRelation): the subtype relation, except that only fresh
    // object literals have an implicit index signature and a fresh `{}` is a supertype of nothing non-empty, so that
    // two different types are never mutual strict subtypes
    TY_RELATION_STRICT_SUBTYPE,
    // tsc's identityRelation: the same kind of type with identical parts (never reports)
    TY_RELATION_IDENTITY,
    TY_RELATION_COUNT,
} TyRelation;

// u64 -> u8 open addressing map, key (source id << 32 | target id)
typedef struct RelationCache {
    u64* keys;
    unsigned char* values;
    u32 cap;
    u32 len;
} RelationCache;

// One line of an explanation of why a relation doesn't hold, outermost first
typedef struct TyReportLine {
    unsigned ts_code;
    cstr message;
    Span span; // Where the error goes if this line heads it, invalid for "wherever the check was asked for"
    // Related information (tsc's associateRelatedInfo) for the diagnostic the explanation ends up in, at `span`.
    // ty_explain_unrelated() lists these after all the other lines
    bool related;
    // Heads the diagnostic in place of what the check would say (tsc: isRelatedTo reports it without
    // reportRelationError), at the check's span: the weak type errors
    bool replaces_head;
    // The whole diagnostic goes to `span`, while this line stays where it is in the chain (an excess JSX attribute)
    bool relocates;
    // While relating: a step down (tsc's reportIncompatibleError) that becomes a line once the path through the
    // consecutive ones is known, eg "The types of 'a.b' are incompatible between these types."
    bool incompatible;
} TyReportLine;
static inline void ty_report_line_drop(TyReportLine* me) { cstr_drop(&my message); }
static inline TyReportLine ty_report_line_clone(TyReportLine me) { me.message = cstr_clone(me.message); return me; }
#define g_class_key TyReportLine, ty_report_line
#include <cleat/generic/vec.h>

typedef enum TyIntrinsic {
    // Creation order copies tsc's, since union members print in id order
    TY_ANY,
    TY_AUTO, // The declared type of `let x;` under noImplicitAny: any, but typed by control flow (tsc's autoType)
    // What type parameters are while a conditional type checks whether it is definitely false (tsc's wildcardType):
    // any, and a conditional type, indexed access, union or intersection with it in it is it
    TY_WILDCARD,
    TY_ERROR,
    // What a declaration whose type depends on itself is (tsc's reportCircularityError gives plain any): kept apart
    // from `any` for TS7022 & co.
    TY_CIRCULAR,
    TY_UNKNOWN_,
    TY_UNDEFINED,
    TY_NULL,
    TY_STRING,
    TY_NUMBER,
    TY_BIGINT,
    TY_FALSE,
    TY_TRUE,
    TY_BOOLEAN,
    TY_ES_SYMBOL,
    TY_VOID,
    TY_NEVER,
    TY_SILENT_NEVER,
    TY_IMPLICIT_NEVER, // The element type of `[]` under strictNullChecks (tsc's implicitNeverType)
    TY_UNREACHABLE_NEVER, // Control flow: after a call that never returns. A reference there has its declared type
    TY_NON_PRIMITIVE,
    TY_INTRINSIC_COUNT,
} TyIntrinsic;

// Control flow queries in progress (flow.c)
typedef struct FlowLoopEntry {
    struct FlowNode const* flow; // A loop label whose back edges are being walked
    u32 key; // For which reference
    TyBuf* types; // What its antecedents gave so far
} FlowLoopEntry;

typedef struct FlowSharedEntry {
    struct FlowNode const* flow;
    Ty const* type;
    bool incomplete;
} FlowSharedEntry;

typedef struct CurrentNode {
    Span span;
    struct Expr const* expr; // If set, the node is this expression (the span isn't)
} CurrentNode;

struct Checker {
    Program* program;
    struct Binder const* binder;
    LazySlots lazy; // The memos of every LazyType in the AST, see lazy_type.h
    Arena arena;
    u32 next_type_id;
    Ty const* intrinsics[TY_INTRINSIC_COUNT];
    InternTable atoms;
    InternTable types;  // literals, unions, intersections, references, deferred, instantiated anonymous, tuple targets
    InternTable intersection_props; // (intersection id, property name) -> FoundProp, see find_prop()
    InternTable links;  // Symbol -> SymbolLinks, AST node -> Ty, Generic -> type parameter list...
    Ty const* array_target;
    Ty const* readonly_array_target;
    Ty const* global_types[13]; // GlobalType in relate.c: String, Number... looked up on first use
    bool global_types_resolved[13];
    u32 instantiation_depth;
    // Conditional types picked as the branch of a conditional type being instantiated now, which don't count towards
    // instantiation_depth (tsc resolves those in a loop, getConditionalType's tailCount), and the next ty_instantiate()
    // being one (ty_instantiate_tail())
    u32 instantiation_tail_count;
    u32 instantiation_epoch; // Bumped whenever instantiation_count starts over
    bool instantiating_tail;
    u32 flow_inline_level; // Aliased conditions being narrowed by (ty/flow.c), across nested flow queries like tsc's inlineLevel
    u32 reverse_mapped_depth; // HACK: bounds the eager reverse mapped types of infer.c (tsc's are lazy)
    u32 instantiation_count;
    struct JsxElement const* jsx_element; // The JSX element being checked (relation errors about its attributes)
    CurrentNode current_node; // See ty_enter_node()
    // The type aliases being resolved, innermost last (the aliases in tsc's resolutionTargets)
    Symbol const** aliases_in_flight;
    u32 aliases_in_flight_len;
    u32 aliases_in_flight_cap;
    u32 instantiation_limit_hits; // How many times ty_instantiate() gave up (TS2589): whether a computation was cut short
    u32 relation_complexity_hits; // How many relations ran out of their budget (TS2859, counted in the above too)
    // The typecheck walk inside the true branches of conditional types: `A` is `A & X` in `A extends X ? A['k'] : ...`
    // where indexed access types are checked (tsc's substitution types, see ty_check_indexed_access_type())
    RelationCache relations[TY_RELATION_COUNT];
    struct RelationState* relating; // The relation check in progress, if any (relate.c)
    Ty const* return_context; // The contextual type of `return` expressions in the function being looked at, or null
    // The generic call whose arguments are being checked for inference (call.c: OuterInference), or null
    struct OuterInference const* outer_inference;
    u32 inferential_depth; // Arguments are being checked to infer type arguments from (tsc's CheckMode.Inferential)
    // Control flow (flow.c). While flow_uncached is set expressions are evaluated without reading unfinished memos or
    // writing any: what they evaluate to may depend on the incomplete types of a loop, see ty_flow_expr_type()
    u32 flow_uncached;
    // The binding patterns whose implied types are being computed as contextual types (tsc's
    // contextualBindingPatterns): a reference inside one to its own elements is `any` (`const [a, b = a] = [1]`)
    struct Binding const* contextual_patterns[16];
    u32 contextual_pattern_count;
    // The pattern elements whose defaults ty_binding_default_type() is checking: a binding element's type has no memo of
    // its own, the default's LazyType is what detects `[x = x]` (a contextual check doesn't go through it)
    struct Binding const* defaults_in_flight[16];
    u32 defaults_in_flight_count;
    // The values whose types a JSDoc value reference (`@param {x} x`) is taking: the annotation has no memo of its own,
    // this is what detects the cycle (TS2502)
    struct Symbol const* jsdoc_values_in_flight[16];
    u32 jsdoc_values_in_flight_count;
    u32 flow_incomplete_seen; // Counts incomplete loop types handed out
    u32 flow_key_count; // Reference keys made
    u32 flow_expr_generation; // Which cache ty_flow_expr_type() uses while a loop is in progress: one per back edge walk
    u32 flow_expr_generations;
    FlowLoopEntry* flow_loops; // The loops in progress. A self-contained evaluation (a declaration) only sees its own,
    u32 flow_loop_start;       // from flow_loop_start on
    u32 flow_loop_count;
    u32 flow_loop_cap;
    FlowSharedEntry* flow_shared; // The shared nodes the queries in progress have been through
    u32 flow_shared_count;
    u32 flow_shared_cap;
    u32 variance_depth; // Variances are being measured, which is only reliable while the marker types are compared
    bool variance_unreliable; // structurally: set when they go through something that isn't (relate.c)
    struct IterationState const* iteration; // The lib's iteration interfaces, looked up on first use (iterate.c)
    // `Disposable | null | undefined` and `AsyncDisposable | Disposable | null | undefined`, what a `using` / `await
    // using` initializer must be (ty_check_using_initializer()), made on first use. Null: not yet
    Ty const* disposable_targets[2];
    bool disposable_looked_up[2];
    bool iterable_iterator_reported[2]; // TS2318 for a missing IterableIterator / AsyncIterableIterator (sync, async)
};

// checker.c
Checker* checker_new(Program* program);
void checker_drop(Checker* me);

// The checker that code without a context parameter works for: lazy_type_get() uses its slots (through
// current_lazy_slots). One per Program; set by program_typecheck() and stays set until that program is destroyed or
// another one is typechecked, because types are still evaluated after typechecking (the test traversal, for one).
// New-core functions take a `Checker* me` instead. See hacks.md
extern thread_local Checker* current_checker;
void checker_make_current(Checker* me); // null for none
// A class member, by (the start of) its declaration span, was read from outside of itself (tsc's isReferenced of a
// member symbol, see ty_check_member_modifiers()). Only tracked under noUnusedLocals/noUnusedParameters
bool ty_member_is_referenced(Checker* me, Span declaration);
void ty_mark_member_referenced(Checker* me, Span declaration);
// `const { name } = object` reads the member (check.c)
void ty_mark_destructured_member(Checker* me, Ty const* object, csview name);
Atom const* checker_atom(Checker* me, csview sv);
static inline Ty const* ty_intrinsic(Checker const* me, TyIntrinsic i) { return my intrinsics[i]; }

// Intern key building. Keys are small, build them on the stack: u64 key[1 + n] = { n, ... }
void const** intern_slot(Checker* me, InternTable* table, u64 const* words, u32 count);

Ty* ty_alloc(Checker* me, TyKind kind, u32 flags);
TyList ty_list_copy(Checker* me, Ty const* const* data, u32 len);

Ty const* ty_unresolved(Checker* me, csview name);
Ty const* ty_literal(Checker* me, Value const* value);
// The property name a string, number or unique symbol literal type is as a key (null for other types)
Atom const* ty_literal_name(Checker* me, Ty const* literal);
Ty const* ty_enum_literal(Checker* me, Value const* value, Symbol const* enum_decl, Symbol const* member);
Ty const* ty_enum_computed_member(Checker* me, Symbol const* enum_decl, Symbol const* member);
Ty const* ty_string_literal(Checker* me, csview sv);
// Fresh/regular literal types (tsc's getFreshTypeOfLiteralType / getRegularTypeOfLiteralType). ty_literal() and friends
// make regular literals; literal expressions and enum member values are fresh. Relations only ever see regular ones
Ty const* ty_fresh_literal(Checker* me, Ty const* ty); // Literals (not unique symbols), else ty itself
Ty const* ty_regular(Checker* me, Ty const* ty); // Fresh literals, and unions with fresh members member-wise
static inline bool ty_is_fresh(Ty const* ty) { return ty->kind == TY_LITERAL && (ty->flags & TYF_FRESH_LITERAL); }
static inline bool ty_is_fresh_object(Ty const* ty) { return ty->kind == TY_ANONYMOUS && (ty->flags & TYF_FRESH_OBJECT); }
// The freshness part of tsc's getWidenedType: fresh object literals (also inside unions, arrays and tuples, and in the
// properties of the regular twin) become regular. What declarations, inferred return types and inferences hold
Ty const* ty_widened_objects(Checker* me, Ty const* ty);
// tsc's getWidenedType for null and undefined without strictNullChecks: any, also inside the elements of arrays and
// tuples, the properties of object literals and the members of unions. HACK: tsc only widens the types of the `null`
// and `undefined` expressions, tttt can't tell them from written ones (hacks.md)
Ty const* ty_widened_nullable(Checker* me, Ty const* ty);
Ty const* ty_unique_symbol(Checker* me, UniqueSymbolDecl const* decl); // `symbol` if decl->key is null
Ty const* ty_widened_unique_symbol(Checker* me, Ty const* ty); // A unique symbol at a mutable location is `symbol`
// The type of a property name as a key: the unique symbol a `[sym]` member is named by (null if it isn't one), else
// the string literal
Ty const* ty_unique_symbol_of_name(Checker* me, Atom const* name);
Ty const* ty_prop_name_literal(Checker* me, Atom const* name);
Ty const* ty_prop_key_literal(Checker* me, Prop const* prop); // What keyof has for it: 1 for `{ 1: x }`
// `#x` of a class: a name of its own per class (tsc's getSymbolNameForPrivateIdentifier, `__#12@#x`). It's an atom with
// the text `#x` that isn't the interned one, so members are told apart by pointer like any name, and print as `#x`
Atom const* ty_private_name(Checker* me, Symbol const* class_symbol, csview text);
// The class a private name belongs to, null for any other name
Symbol const* ty_private_name_class(Checker* me, Atom const* name);
Ty const* ty_union(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args);
// The union of some of the members of the union `of`, in their order (a filter): no reductions to do
Ty const* ty_union_filtered(Checker* me, Ty const* of, Ty const* const* kept, u32 len);
Ty const* ty_intersection(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args);
// The same without dropping a `{}` next to a primitive: a written `string & {}` (tsc's NoSupertypeReduction)
Ty const* ty_intersection_keeping_empty_object(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args);
Ty const* ty_type_param(Checker* me, Symbol const* decl);
Ty const* ty_synthetic_type_param(Checker* me, csview name);
// A fresh type parameter like `param` whose constraint and default are instantiated with `mapper` (set later if null),
// named `name` (null: the same name)
Ty* ty_clone_type_param(Checker* me, Ty const* param, TyMapper const* mapper, Atom const* name);
// Whether a type parameter says anything about itself (a constraint or a default): only those need cloning
bool ty_param_is_bounded(Ty const* param);
bool ty_is_const_type_variable(Checker* me, Ty const* ty, u32 depth);
bool ty_some_mutable_array_like(Checker* me, Ty const* ty);
Ty const* ty_this_type_param(Checker* me, Ty const* declared); // Only make_declared() should call this
Ty const* ty_reference(Checker* me, Ty const* target, TyList args);
Ty const* ty_reference_with_this(Checker* me, Ty const* target, TyList args, Ty const* this_arg);
// tsc's getTypeWithThisArgument: the type with `this` in its members being `this_arg`. The type itself if it has no
// `this` type (or `this_arg` is the type itself)
Ty const* ty_with_this_arg(Checker* me, Ty const* ty, Ty const* this_arg);
Ty const* ty_array(Checker* me, Ty const* element, bool readonly);
Ty const* ty_tuple_target(Checker* me, TupleShape const* shape);
// The shape of a tuple type, the empty tuple (its own target) included. Null if it isn't one
TupleShape const* ty_tuple_shape(Ty const* ty);
Ty const* ty_keyof(Checker* me, Ty const* operand);
// template.c: template literal types and string mappings (tsc's getTemplateLiteralType, getStringMappingType)
Ty const* ty_template_literal(Checker* me, Atom const* const* texts, Ty const* const* types, u32 len);
Ty const* ty_string_mapping(Checker* me, TyStringMapping mapping, Symbol const* symbol, Ty const* type);
// A type variable at the top level (tsc's isGenericIndexType, roughly): what keeps keys, conditional types and
// template literals from resolving. `{ a: T }` isn't, nor is `string & { tag: T }`
bool ty_is_generic_index(Ty const* ty);
bool ty_is_pattern_literal(Ty const* ty); // A template or mapping of nothing but placeholders: `a${string}`
bool ty_is_valid_index_key(Ty const* ty); // tsc's isValidIndexKeyType: string, number, symbol, a pattern, `string & T`
bool ty_is_pattern_placeholder(Ty const* ty); // Something a pattern can be made of: string, number, a pattern...
// tsc's isTypeMatchedByTemplateLiteralType, isMemberOfStringMapping, templateLiteralTypesDefinitelyUnrelated
bool ty_template_matches(Checker* me, Ty const* source, Ty const* target);
bool ty_string_mapping_contains(Checker* me, Ty const* source, Ty const* target);
bool ty_templates_definitely_unrelated(Ty const* source, Ty const* target);
// tsc's inferTypesFromTemplateLiteralType: what each placeholder of `target` matched in `source`, false if it doesn't
// match. `out` has room for target's types
bool ty_infer_from_template(Checker* me, Ty const* source, Ty const* target, Ty const** out);
Ty const* ty_template_literal_for_constraint(Checker* me, Ty const* source, Ty const* constraint);
Ty const* ty_indexed(Checker* me, Ty const* object, Ty const* index);
Ty const* ty_indexed_instantiated(Checker* me, Ty const* object, Ty const* index);
// tsc's getSubstitutionType: `base` itself if the constraint says nothing about it
Ty const* ty_substitution(Checker* me, Ty const* base, Ty const* constraint);
Ty const* ty_no_infer(Checker* me, Ty const* ty); // tsc's getNoInferType
static inline bool ty_is_no_infer(Ty const* ty) {
    return ty->kind == TY_SUBSTITUTION && (ty->substitution.constraint->flags & TYF_UNKNOWN);
}
// tsc's getSubstitutionIntersection: what a substitution type is as a source
Ty const* ty_substitution_intersection(Checker* me, Ty const* ty);
// tsc's getActualTypeVariable: a substitution type's base, also inside an indexed access
Ty const* ty_actual_type_variable(Checker* me, Ty const* ty);
Ty const* ty_conditional(
    Checker* me, Ty const* check, Ty const* extends, TyCondBranch const* if_true, TyCondBranch const* if_false,
    Ty const* root, TyMapper const* mapper, // null for a root
    bool distributive,
    TyList infers // The `infer` type parameters its extends type declares (ty_conditional_own_infers of the root)
);
Ty const* ty_realias(Checker* me, Ty const* ty, Symbol const* alias, TyList alias_args);
TyList ty_conditional_own_infers(Checker* me, Ty const* root); // The `infer` type parameters its extends type declares
Ty const* ty_non_nullable(Checker* me, Ty const* ty);
// tsc's checkNonNullType: unknown and (under strictNullChecks) null or undefined are errors. Null if it reported
Ty const* ty_check_non_null_type(Checker* me, Ty const* type, Expr const* expr, ErrorCtx* errs);
// tsc's checkAssertionDeferred: `x as T` where x's type and T don't overlap (TS2352)
void ty_check_type_assertion(Checker* me, Ty const* expr_type, Ty const* target, Span span, ErrorCtx* errs);
// tsc's checkDeleteExpression: what `delete` deletes has to be an optional (and writable) property
void ty_check_delete_operand(Checker* me, Expr const* operand, ErrorCtx* errs);
// tsc's getSingleSignature: the one call (or construct) signature of an object type that has none of the other kind.
// `allow_members`: properties and index signatures may be there too
Signature const* ty_single_signature(Checker* me, Ty const* ty, bool construct, bool allow_members);
Ty const* ty_without_undefined(Checker* me, Ty const* ty); // Removes undefined only (tsc's getTypeWithFacts(NEUndefined))
Ty const* ty_cond_branch_type(Checker* me, TyCondBranch* branch);
static inline Ty const* ty_conditional_branch(Checker* me, Ty const* conditional, bool which) {
    return ty_cond_branch_type(me, &conditional->conditional.branches[which]);
}

// members.c
Members const* ty_members(Checker* me, Ty const* ty); // Never null. Empty for non-object types
Prop const* ty_get_prop(Checker* me, Ty const* ty, Atom const* name);
Prop const* ty_get_object_prop(Checker* me, Ty const* ty, Atom const* name); // relate.c, with Function/Object members
// The type whose members a value of `ty` has: `String` for a string, the constraint of a type parameter...
Ty const* ty_apparent_type(Checker* me, Ty const* ty);
Ty const* ty_global_function_type(Checker* me); // Null without a lib
void ty_init_global_types(Checker* me); // tsc's initializeTypeChecker's global types, in its order
Ty const* prop_type(Checker* me, Prop const* prop);
// What writes to the property are checked against when it differs from what reads get (a `set` accessor whose
// parameter type isn't the getter's), else null
Ty const* prop_write_type(Checker* me, Prop const* prop);
bool ty_props_identical(Checker* me, Prop const* source, Prop const* target); // tsc's isPropertyIdenticalTo
// The name of the property's first declaration (instantiations follow their target), invalid if it has none
Span ty_prop_decl_span(Prop const* prop);
bool ty_has_unresolved_base(Checker* me, Ty const* ty); // HACK: see has_unresolved_base()
// The base a class or interface without members of its own is equivalent to (tsc's
// getSingleBaseForNonAugmentingSubtype: relations see it instead). Null if there's none
Ty const* ty_non_augmenting_base(Checker* me, Ty const* ty);
// The type variable a mixin class extends (`class C extends Base` with `Base: T`, or `T & U`): its value is `typeof C & T`
// (tsc's getBaseTypeVariableOfClass). Null for any other class
Ty const* ty_class_base_type_variable(Checker* me, Symbol const* class_symbol);
void signature_resolve(Checker* me, Signature const* sig);
Ty const* signature_return_type(Checker* me, Signature const* sig);
bool ty_signature_return_in_flight(Checker* me, Signature const* sig); // Its return type is being inferred up the stack
TyPredicate const* signature_predicate(Checker* me, Signature const* sig);
bool signature_declared_never(Signature const* sig);
TyPredicate const* ty_infer_type_predicate(Checker* me, FunctionImpl const* function); // flow.c, null if none
Ty const* ty_tuple_as_array(Checker* me, Ty const* tuple);
// Whether lowering the node now would run into a type alias that is being resolved, through aliases it would
// resolve on the way (not looking into object types)
bool ty_node_reaches_alias_in_flight(Checker* me, TypeNode const* node);
// The type of parameter i (a rest parameter stands for every position from its own on), null if there's none
Ty const* ty_signature_param_type_at(Checker* me, Signature const* sig, u32 i);
// How many parameters a signature has, a rest tuple of fixed length counted as its elements (tsc's getParameterCount)
u32 ty_signature_param_count(Signature const* sig);
// Takes any number of arguments: a rest parameter that isn't a tuple of fixed length (tsc's hasEffectiveRestParameter)
bool ty_signature_has_rest(Signature const* sig);
// The signature a function expression gets its parameter types from, given its contextual type, null if none
Signature const* ty_function_contextual_signature(Checker* me, FunctionImpl const* function, Ty const* contextual);
Ty const* ty_quick_expr_type(Checker* me, Expr const* expr, bool in_flow); // tsc's getQuickTypeOfExpression, null if none // The array a tuple also is
Ty const* ty_optional_param_type(Checker* me, Ty const* type); // infer.c
Ty const* ty_signature_params_tuple(Checker* me, Signature const* sig, u32 from, TyMapper const* mapper); // infer.c
Signature const* ty_combine_intersection_signatures(Checker* me, Signature const* left, Signature const* right);
bool ty_signatures_identical(Checker* me, Signature const* source, Signature const* target, bool partial, bool ignore_return);
// tsc's isImplementationCompatibleWithOverload (TS2394): the returns relate either way (or the overload's is void) and,
// type parameters erased, the implementation is assignable to the overload with the returns ignored
bool ty_implementation_compatible_with_overload(Checker* me, Signature const* implementation, Signature const* overload);
// tsc's ExternalEmitHelpers: the tslib helpers the emitter lowers syntax into (helpers.c)
typedef enum EmitHelper {
    EMIT_HELPER_EXTENDS = 1u << 0,
    EMIT_HELPER_ASSIGN = 1u << 1,
    EMIT_HELPER_REST = 1u << 2,
    EMIT_HELPER_DECORATE = 1u << 3, // ES decorators: __esDecorate and __runInitializers
    EMIT_HELPER_METADATA = 1u << 4,
    EMIT_HELPER_PARAM = 1u << 5,
    EMIT_HELPER_AWAITER = 1u << 6,
    EMIT_HELPER_GENERATOR = 1u << 7,
    EMIT_HELPER_VALUES = 1u << 8,
    EMIT_HELPER_READ = 1u << 9,
    EMIT_HELPER_SPREAD_ARRAY = 1u << 10,
    EMIT_HELPER_AWAIT = 1u << 11,
    EMIT_HELPER_ASYNC_GENERATOR = 1u << 12,
    EMIT_HELPER_ASYNC_DELEGATOR = 1u << 13,
    EMIT_HELPER_ASYNC_VALUES = 1u << 14,
    EMIT_HELPER_EXPORT_STAR = 1u << 15,
    EMIT_HELPER_IMPORT_STAR = 1u << 16,
    EMIT_HELPER_IMPORT_DEFAULT = 1u << 17,
    EMIT_HELPER_MAKE_TEMPLATE_OBJECT = 1u << 18,
    EMIT_HELPER_CLASS_PRIVATE_FIELD_GET = 1u << 19,
    EMIT_HELPER_CLASS_PRIVATE_FIELD_SET = 1u << 20,
    EMIT_HELPER_CLASS_PRIVATE_FIELD_IN = 1u << 21,
    EMIT_HELPER_SET_FUNCTION_NAME = 1u << 22,
    EMIT_HELPER_PROP_KEY = 1u << 23,
    EMIT_HELPER_DISPOSABLE = 1u << 24, // __addDisposableResource and __disposeResources
    EMIT_HELPER_REQUESTED = 1u << 31, // Internal: the memo is set
} EmitHelper;
// TS2343/TS2807 for helpers the file's tslib doesn't have, TS2354 (once per file) when there is no tslib. Callers
// check the target/option conditions; nothing happens without importHelpers or in a script
void ty_check_emit_helpers(Checker* me, Span at, u32 helpers, ErrorCtx* errs);
// The file is emitted as CommonJS, AMD, UMD or a script (tsc's getEmitModuleFormatOfFile() < ModuleKind.System)
bool ty_emits_commonjs_like(struct SourceFile const* file);
// The call signature of a function declaration or method (its first construct signature for a constructor)
Signature const* ty_function_signature(Checker* me, FunctionImpl const* function);
// TS2394 on the first of the overloads (with their names) that the implementation isn't compatible with
void ty_check_overloads_compatible(
    Checker* me, FunctionImpl const* implementation, Span implementation_name, FunctionImpl const* const* overloads,
    Span const* names, u32 count, ErrorCtx* errs
);
Signature const* ty_union_signature(Checker* me, Signature const* base, Signature* const* composites, u32 count);
bool ty_type_params_identical(Checker* me, TyList source, TyList target);
bool ty_is_discriminant_property(Checker* me, Ty const* type, Atom const* name); // flow.c
// tsc's getKeyPropertyName: of a union of 10+ object types, the unit-typed property (`kind`) that tells most of them
// apart, else null. ty_union_member_for_key() is the member whose key property is `key` (tsc's
// getConstituentTypeForKeyType), null if none or several are
Atom const* ty_union_key_property(Checker* me, Ty const* ty);
Ty const* ty_union_member_for_key(Checker* me, Ty const* ty, Ty const* key);
// The type of the property `name` of `ty` (tsc's getTypeOfPropertyOfType, of an object, intersection or type variable)
Ty const* ty_prop_type_of(Checker* me, Ty const* ty, Atom const* name);
Signature* const* ty_union_signatures(Checker* me, TyList members, bool construct, u32* count_out);
Signature* const* ty_union_signature_lists(Checker* me, Signature* const* const* lists, u32 const* lens, u32 member_count, u32* count_out);
typedef struct CallEffects {
    bool has_predicate;
    bool returns_never;
    TyPredicate predicate; // With the type arguments of the call
} CallEffects;
CallEffects ty_call_effects(Checker* me, Ty const* callee, CallExpr const* call);
TyList ty_reference_args(Checker* me, Ty const* reference);
Ty const* ty_param_constraint(Checker* me, Ty const* param); // null if unconstrained
Ty const* ty_implied_infer_constraint(Checker* me, Ty const* param); // Of an `infer X` without `extends`, from its position
Ty const* ty_simplified(Checker* me, Ty const* ty, bool writing);
Ty const* ty_generic_mapped_prop_context(Checker* me, Ty const* object, Ty const* key); // tsc's getSimplifiedType, relate.c
Ty const* ty_base_constraint(Checker* me, Ty const* ty); // of a type parameter or deferred type, null if there's none
Ty const* ty_param_default(Checker* me, Ty const* param); // null if none

// instantiate.c
TyMapper const* ty_mapper_new(Checker* me, TyList sources, TyList targets);
Ty const* ty_instantiate(Checker* me, Ty const* ty, TyMapper const* mapper);
Ty const* ty_instantiate_tail(Checker* me, Ty const* ty, TyMapper const* mapper); // A conditional type's branch
// tsc's currentNode: the statement, type node or expression being checked (checkSourceElement, checkExpression), where
// diagnostics about what went wrong deep inside the checker go. Entering one starts the instantiation count over.
// Returns the previous one, to give back to ty_leave_node()
static inline CurrentNode ty_enter_node(Checker* me, Span span) {
    CurrentNode saved = me->current_node;
    me->current_node = (CurrentNode){ .span = span };
    me->instantiation_count = 0;
    me->instantiation_epoch++;
    return saved;
}
// An expression: its error span is only worked out if something is reported
static inline CurrentNode ty_enter_expr(Checker* me, struct Expr const* expr) {
    CurrentNode saved = ty_enter_node(me, (Span){0});
    me->current_node.expr = expr;
    return saved;
}
static inline void ty_leave_node(Checker* me, CurrentNode saved) { me->current_node = saved; }
Span ty_current_node_span(Checker* me);
// tsc's error(currentNode, ...): only while the program is being typechecked (not while a baseline prints types)
void ty_error_at_current_node(Checker* me, unsigned ts_code, cstr message);
void ty_error_at(Checker* me, Span span, unsigned ts_code, cstr message); // tsc's error(node, ...), likewise
#define ty_error_at_current_node_d(me, ts_code, ...) ty_error_at_current_node(me, ts_code, cstr_d(__VA_ARGS__))
// TS2589 at the current node, counted in instantiation_limit_hits: the instantiation limits ran out (the caller gives
// the error type) or a relation got too deep
void ty_instantiation_limit_reached(Checker* me);

// lower.c
Ty const* ty_from_node(Checker* me, TypeNode const* node, LowerCtx ctx);
// The type of a binding (a variable, a parameter, an element of a destructuring pattern): what is written (lowered
// with `ctx`), else what is assigned to it
Ty const* ty_binding_type(Checker* me, Binding const* binding, LowerCtx ctx);
Ty const* ty_empty_object_type(Checker* me); // `{}`
// Also `{}`, but only as the non-nullish part of `unknown` while narrowing it (tsc's unknownEmptyObjectType, for TS2638)
Ty const* ty_unknown_empty_object_type(Checker* me);
Ty const* ty_commonjs_module_object(Checker* me, SourceFile const* file); // `module` of a CommonJS file
// What a context-sensitive function is while context-sensitive arguments are skipped (tsc's anyFunctionType): no
// members, non-inferrable, related to every function type
Ty const* ty_any_function_type(Checker* me);
// tsc's getTypeFromBindingPattern. include_pattern: as the contextual type of an initializer (TYF_BINDING_PATTERN)
Ty const* ty_binding_pattern_type(Checker* me, Binding const* pattern, bool include_pattern);
Ty const* ty_pattern_clone(Checker* me, Ty const* ty); // A clone marked TYF_BINDING_PATTERN
bool ty_is_array(Checker* me, Ty const* ty); // A reference to Array<T> (not ReadonlyArray, not a tuple)
// A class as written inside itself: `C<T, U>` with its own type parameters as the arguments
Ty const* ty_class_instance_type(Checker* me, Symbol const* class_symbol);
Ty const* ty_binding_assigned_type(Checker* me, Binding const* binding);
// The type of the initializer of a parameter's pattern (or of an element of one) with what the pattern adds: the
// properties with defaults an object literal lacks, the elements a tuple lacks (tsc's checkDeclarationInitializer)
Ty const* ty_pad_initializer_type(Checker* me, Ty const* type, Binding const* pattern);
// The type of the default of an element of a pattern (`x = 1` in `{ x = 1 }`), checked with its contextual type
Ty const* ty_binding_default_type(Checker* me, Binding const* element);
Ty const* ty_declared_type_of(Checker* me, Symbol const* decl);
SymbolLinks* ty_symbol_links(Checker* me, Symbol const* symbol);
Ty const* ty_namespace_type(Checker* me, struct Symbol const* symbol);
Ty const* ty_static_type_of(Checker* me, Symbol const* symbol);
Ty const* ty_type_of_symbol(Checker* me, Symbol const* symbol);
Ty const* ty_merged_value_type(Checker* me, Symbol const* symbol);
// What `import("m")` promises: the module's value, with a synthetic `default` for an `export =` module
Ty const* ty_dynamic_import_module_type(Checker* me, Symbol* module);
// The class a JavaScript function is the constructor of (tsc's isJSConstructor: `this.x = v` or `F.prototype.x = v`
// declared members of it), else null
Symbol const* ty_js_constructor_class(FunctionImpl const* function);
// The JavaScript class whose `this` a function has: a constructor function's, or that of the constructor function
// whose prototype it is a method of (`F.prototype.m = function () {}`). Null if none
Symbol const* ty_js_this_class(FunctionImpl const* function);
Ty const* ty_js_this_type(Checker* me, FunctionImpl const* function);
// A JavaScript member that only `this.x = v` assignments declare, one of them in a constructor (tsc's
// isConstructorDeclaredProperty): its type is what the constructor assigns
bool ty_is_constructor_declared(Symbol const* symbol);
u32 ty_merged_value_entries(Checker* me, Symbol const* symbol, Ty const*** out); // malloc'd
Atom const* ty_qualified_name(Checker* me, Symbol const* decl); // `A.B.name`
// The symbol's name as printed: its name_span, but an anonymous class expression is called what it's assigned to
Span ty_symbol_display_name(Symbol const* symbol);
void ty_set_declared_type(Checker* me, Symbol const* decl, Ty const* type);
TyList ty_type_params_of(Checker* me, Generic const* generic);
TyList ty_outer_type_params(Checker* me, OuterGenerics const* outer);
// The type parameters of a class or interface that type arguments are written for (tsc's localTypeParameters)
static inline TyList ty_local_type_params(Ty const* declared) {
    TyList all = declared->declared.type_params;
    return (TyList){ .data = all.data + declared->declared.outer_count, .len = all.len - declared->declared.outer_count };
}
Ty const* ty_anonymous_from_node(Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args);

// enums.c
Ty const* ty_resolve_enum(Checker* me, Symbol const* decl); // Only ty_declared_type_of() should call this
bool ty_evaluate_string(Checker* me, Expr const* expr, cstr* out); // enums.c, tsc's evaluate() of a constant string
Ty const* ty_enum_of_member(Checker* me, Ty const* member); // The enum type an enum literal belongs to
bool ty_is_enum_union(Ty const* ty);

// relate.c
bool ty_is_related(Checker* me, Ty const* source, Ty const* target, TyRelation relation);
// Why source isn't related to target, outermost line first. The first line is the natural head of the error: TS2322
// "Type 'S' is not assignable to type 'T'." unless something more specific replaces it (TS2741 and friends)
void ty_explain_unrelated(Checker* me, Ty const* source, Ty const* target, TyRelation relation, VecTyReportLine* out);
// The member of a union target a failed relation is most likely about (tsc's getBestMatchingType), null if none
Ty const* ty_best_matching_type(Checker* me, Ty const* source, Ty const* target);
Ty const* ty_widened_literal(Checker* me, Ty const* ty);
// How the source of a failed relation shows in the error: widened unless the target has room for literal types
void ty_report_pair_cstr(Checker* me, Ty const* source, Ty const* target, cstr* source_out, cstr* target_out); // Both, as a message shows them
void relation_cache_drop(RelationCache* me);
// conditional.c
// Null while the conditional type has to stay deferred
Ty const* ty_conditional_resolve(
    Checker* me, Ty const* check_type, Ty const* extends, TyList infers, TyCondBranch const* if_true,
    TyCondBranch const* if_false
);
// Assignable whatever the type parameters in either side turn out to be (tsc's check on their restrictive instantiations)
bool ty_definitely_assignable(Checker* me, Ty const* source, Ty const* target);

// access.c
// The type of `object[index]` (and `object.name`, the index being the name as a literal). Null if there's no such
// property or the index isn't one that can be looked up
Ty const* ty_access(Checker* me, Ty const* object, Ty const* index);

// expr.c: the types of expressions. One entry point, with the contextual type (null for none) as a parameter:
// contextual typing of function expressions, object and array literals, and the context-sensitive pass of generic
// inference hang off it. The result is the expression's memo (its LazyType slot), so a later ty_expr_type() sees the
// type the expression got from the parent that knew its context.
Ty const* ty_check_expr(Checker* me, Expr const* expr, Ty const* contextual);
Ty const* ty_expr_type(Checker* me, Expr const* expr); // The memo, else checked without a contextual type
// The memo as a mutable location holds it (tsc's checkExpressionForMutableLocation with a contextual type): fresh
// literals widen unless the contextual type has room for them
Ty const* ty_mutable_location_type(Checker* me, Expr const* expr, Ty const* contextual);
// tsc's isLiteralOfContextualType (of a literal or a union with literals in it)
bool ty_is_literal_of_contextual_type(Checker* me, Ty const* ty, Ty const* contextual);
// A contextual type inside an argument of a generic call, instantiated with what the call's own contextual type says
// about its type parameters (tsc's instantiateContextualType with the returnMapper)
Ty const* ty_instantiate_contextual_type(Checker* me, Ty const* contextual);
// An array literal re-read as a tuple of its elements (tsc's checkArrayLiteral with forceTuple), from the elements'
// memos
Ty const* ty_array_literal_as_tuple(Checker* me, Expr const* expr, Ty const* contextual);
Ty const* ty_element_index_type(Checker* me, Expr const* index); // `index` of `x[index]`
Ty const* ty_access_key_type(Checker* me, PropertyExpr const* access); // What `x.y`/`x[y]` looks up (`#y`: the class's)
Ty const* ty_compute_expr_without_context(Checker* me, Expr const* expr); // Only for the LazyType of an Expr
// Every reference to a variable, parameter, function, class... gets its type here (plan 03: narrowing goes here)
Ty const* ty_identifier_reference_type(Checker* me, IdentifierExpr const* identifier);
// What a declaration without type annotation makes of its initializer's type (tsc's getWidenedType, plus
// getWidenedLiteralType if `literals`: mutable locations)
Ty const* ty_widened_for_declaration(Checker* me, Ty const* ty, bool literals);
// An inferred return type: literal types widen unless the contextual return type has room for them
Ty const* ty_widened_return_type(Checker* me, Ty const* ty, Ty const* contextual);
Ty const* ty_union_subtype_reduced(Checker* me, Ty const* const* types, u32 len);
// The type an unannotated parameter got from the contextual signature of its function expression, null if none
Ty const* ty_contextual_parameter_type(Checker* me, InitializedBinding const* parameter);
// A JavaScript function's JSDoc `@type {F}` (FunctionImpl.jsdoc_signature_type) as its contextual signature, once
void ty_function_apply_jsdoc_type(Checker* me, FunctionImpl const* function);
// The contextual type of a property or an element of something whose contextual type is `contextual` (null: none)
Ty const* ty_contextual_prop_type(Checker* me, Ty const* contextual, Ty const* key);
Ty const* ty_contextual_element_type(Checker* me, Ty const* contextual, u32 index);
Ty const* ty_contextual_return_type(Checker* me, FunctionImpl const* function); // null if none
// What its return expressions are contextually typed by: the contextual return type, or an IIFE's call's contextual type
Ty const* ty_return_expressions_context(Checker* me, FunctionImpl const* function);
// `import.meta` (the property access on the `import` keyword) and its type
bool ty_is_import_meta(struct Expr const* expr);
Ty const* ty_import_meta_type(Checker* me);
Ty const* ty_contextual_this_type(Checker* me, FunctionImpl const* function); // null if nothing says what `this` is
// The contextual signature a function expression's parameters were typed by, if any (after it was checked)
Signature const* ty_function_assigned_contextual_signature(Checker* me, FunctionImpl const* function);
// The type of an object literal accessor property: the getter's widened return type, else the setter's parameter
Ty const* ty_object_literal_accessor_type(Checker* me, FunctionImpl const* accessor, bool getter);
bool ty_expr_is_context_sensitive(Expr const* expr);
bool ty_function_is_context_sensitive(FunctionImpl const* function);
// The type of the property an item of an object literal made (what the `.types` baseline shows at its name)
Ty const* ty_object_literal_member_type(Checker* me, Expr const* object, ObjectExprItem const* item);
// `x as const`
typedef struct AsTypeExpr AsTypeExpr;
bool as_type_is_const(AsTypeExpr const* as_type);
Ty const* ty_check_expr_in_const(Checker* me, Expr const* expr, Ty const* contextual, bool is_const);
// tsc's getContextualSignature of a function expression
Signature const* ty_contextual_signature(Checker* me, Ty const* contextual, struct FunctionImpl const* function);
Ty const* ty_check_expr_skipping_sensitive(Checker* me, Expr const* expr, Ty const* contextual, bool is_const);
// JSX (jsx.c; the attributes are an object literal, see expr.c)
struct JsxElement;
struct JsxChild;
struct JsxAttribute;
typedef struct JsxIntrinsic {
    Ty const* elements; // JSX.IntrinsicElements, null if there's none
    Prop const* prop; // Its property the tag names
    IndexInfo const* index; // ... else the index signature that admits the tag
    Ty const* props; // What the attributes are checked against, null if the tag isn't there
} JsxIntrinsic;
Symbol const* ty_jsx_namespace(Checker* me, struct JsxElement const* element); // null if none
Ty const* ty_jsx_type(Checker* me, struct JsxElement const* element, csview name); // `JSX.<name>`, null if none
Ty const* ty_jsx_element_type(Checker* me, Expr const* expr); // Of an EXPR_JSX_ELEMENT or EXPR_JSX_FRAGMENT
Atom const* ty_jsx_children_prop_name(Checker* me, struct JsxElement const* element);
JsxIntrinsic ty_jsx_intrinsic(Checker* me, struct JsxElement const* element);
JsxIntrinsic ty_jsx_intrinsic_named(Checker* me, struct JsxElement const* element, csview name);
void ty_check_jsx_element(Checker* me, Expr const* expr, ErrorCtx* errs);
bool ty_jsx_skips_head(Checker* me, Ty const* source, Ty const* target);
bool ty_jsx_tag_arity_fits(Checker* me, struct JsxElement const* element, Ty const* tag_type, ErrorCtx* errs);
Ty const* ty_jsx_signature_props(Checker* me, struct JsxElement const* element, Signature const* sig, TyMapper const* mapper, bool class_component);
typedef struct JsxResolution {
    Signature const* sig; // What the element resolved to, null if the tag isn't typed or nothing fit
    TyMapper const* mapper;
    Ty const* ret; // What the call returns
    bool class_component; // The tag has construct signatures (tsc's JsxReferenceKind.Component)
    bool function_component; // ... only call signatures (JsxReferenceKind.Function). Neither: Mixed
} JsxResolution;
JsxResolution ty_resolve_jsx_element(Checker* me, struct JsxElement const* element, ErrorCtx* errs); // call.c
bool ty_jsx_child_is_semantic(struct JsxChild const* child);
// The type of the property an attribute made (what the `.types` baseline shows at its name)
Ty const* ty_jsx_attribute_type(Checker* me, Expr const* attributes, struct JsxAttribute const* attribute);

// `f<A, B>` as a value: the type arguments applied to the signatures that take that many
Ty const* ty_apply_type_arguments(Checker* me, Ty const* ty, TyList args);

typedef struct ObjectLiteralProp {
    Atom const* name;
    u32 flags; // PropFlags
    Ty const* type; // Null for an accessor (PROP_GETTER/PROP_SETTER): `accessor` has it
    FunctionImpl const* accessor;
    Span name_span;
    Span const* decl_spans;
    u32 decl_span_count;
    bool name_is_value;
    TypeNode const* asserted; // See Prop.asserted
    bool own; // Written in the literal itself, not spread into it: only those get excess property checks
    bool numeric_redeclared; // See Prop.numeric_redeclared
    Prop const* reuse; // The property is this one as it is (tsc: the same symbol), the other fields are ignored
} ObjectLiteralProp;
typedef enum ObjectLiteralKind {
    OBJECT_SYNTHETIC, // Made up by the checker
    OBJECT_LITERAL, // The type of an object literal expression (tsc's ObjectFlags.ObjectLiteral), widened
    OBJECT_LITERAL_FRESH, // ... as the expression evaluates to it (TYF_FRESH_OBJECT)
    OBJECT_JSX_ATTRIBUTES, // The attributes of a JSX element (tsc's ObjectFlags.JsxAttributes)
    OBJECT_JSX_ATTRIBUTES_FRESH,
    // Or'ed with the kind: an object literal of a JavaScript file, open-ended without noImplicitAny (tsc's
    // ObjectFlags.JSLiteral, isJSLiteralType)
    OBJECT_JS = 1 << 4,
} ObjectLiteralKind;
Ty const* ty_object_literal(
    Checker* me, ObjectLiteralProp const* props, u32 count, IndexInfo const* index, u32 index_count, ObjectLiteralKind kind
);

// function.c
Ty const* ty_function_type(Checker* me, FunctionImpl const* function);
// The return type of a function: its annotation, else inferred from the `return`s of its body
Ty const* ty_function_return_type(Checker* me, FunctionImpl const* function);
// The return type of a function expression that is an argument of the generic call inferring now: the calls it returns
// see what that call has inferred so far (tsc's getInferenceContext finds the call through the argument)
Ty const* ty_argument_return_type(Checker* me, FunctionImpl const* function);
bool ty_function_return_in_flight(Checker* me, FunctionImpl const* function);

// Control flow queries (ty/flow.c)
typedef struct FlowNode FlowNode;
typedef enum FlowReachability {
    FLOW_REACH_NO,
    FLOW_REACH_YES,
} FlowReachability;
FlowReachability ty_flow_reachability(Checker* me, FlowNode const* flow); // null: no
// Whether control can fall off the end of the function's body
FlowReachability ty_function_end_reachability(Checker* me, FunctionImpl const* function);
bool ty_extends_array(Checker* me, Ty const* ty); // Assignable to `readonly any[]`
// The type of a reference (identifier, `this`, property access) at a flow node: `declared` narrowed by the
// assignments and conditions on the way there. `initial`: what it is where its container starts. tsc's
// getFlowTypeOfReference()
Ty const* ty_flow_type(Checker* me, Expr const* reference, Ty const* declared, Ty const* initial, FlowNode const* flow);
// The type of an expression for the flow engine: never through an unfinished memo
Ty const* ty_flow_expr_type(Checker* me, Expr const* expr);
// TS2454: a variable that is read where it may not have been assigned yet
typedef enum IdentifierFlowIssue {
    IDENTIFIER_FLOW_OK,
    IDENTIFIER_FLOW_UNASSIGNED, // TS2454
    IDENTIFIER_FLOW_IMPLICIT_ANY, // A `let x;` whose type control flow can't tell here: TS7034 + TS7005
} IdentifierFlowIssue;
IdentifierFlowIssue ty_identifier_flow_issue(Checker* me, IdentifierExpr const* identifier);
// Type facts, for the operators
Ty const* ty_truthy_part(Checker* me, Ty const* ty); // What is left of x where `x` was truthy
Ty const* ty_non_nullish_part(Checker* me, Ty const* ty);
Ty const* ty_remove_definitely_falsy(Checker* me, Ty const* ty); // Without the members that are always falsy
Ty const* ty_base_type_of_literal(Checker* me, Ty const* ty); // `"a"` -> string, `true` -> boolean
bool ty_can_be_truthy(Checker* me, Ty const* ty);
bool ty_can_be_falsy(Checker* me, Ty const* ty);
bool ty_can_be_nullish(Checker* me, Ty const* ty);
bool ty_can_be_undefined(Checker* me, Ty const* ty); // tsc's hasTypeFacts(ty, TypeFacts.IsUndefined)
bool ty_can_be_null(Checker* me, Ty const* ty); // tsc's hasTypeFacts(ty, TypeFacts.IsNull)
unsigned ty_undefined_or_null_facts(Checker* me, Ty const* ty); // tsc's getTypeFacts(ty, TypeFacts.IsUndefinedOrNull)
// Declarations evaluate self-contained: not in uncached mode, and with no loop in progress (tsc's
// checkExpressionCached()). What they evaluate to is complete and can be remembered
typedef struct FlowIsolation {
    u32 uncached;
    u32 loop_start;
    u32 expr_generation;
} FlowIsolation;
static inline FlowIsolation ty_flow_isolate(Checker* me) {
    FlowIsolation saved = { my flow_uncached, my flow_loop_start, my flow_expr_generation };
    my flow_uncached = 0;
    my flow_loop_start = my flow_loop_count;
    return saved;
}
static inline void ty_flow_restore(Checker* me, FlowIsolation saved) {
    my flow_uncached = saved.uncached;
    my flow_loop_start = saved.loop_start;
    my flow_expr_generation = saved.expr_generation;
}

// iterate.c: the iteration protocol (tsc's IterationUse and getIterationTypesOfIterable)
Ty const* ty_for_in_key_type(Checker* me, Ty const* iterable); // `k` of `for (k in x)`
typedef enum IterationUse {
    ITERATION_ALLOWS_SYNC = 1 << 0,
    ITERATION_ALLOWS_ASYNC = 1 << 1,
    ITERATION_ALLOWS_STRING = 1 << 2,
    ITERATION_FOR_OF_FLAG = 1 << 3,
    ITERATION_YIELD_STAR_FLAG = 1 << 4,
    ITERATION_SPREAD_FLAG = 1 << 5,
    ITERATION_DESTRUCTURING_FLAG = 1 << 6,
    ITERATION_FOR_OF = ITERATION_ALLOWS_SYNC | ITERATION_ALLOWS_STRING | ITERATION_FOR_OF_FLAG,
    ITERATION_FOR_AWAIT_OF = ITERATION_ALLOWS_SYNC | ITERATION_ALLOWS_ASYNC | ITERATION_ALLOWS_STRING | ITERATION_FOR_OF_FLAG,
    ITERATION_YIELD_STAR = ITERATION_ALLOWS_SYNC | ITERATION_YIELD_STAR_FLAG,
    ITERATION_ASYNC_YIELD_STAR = ITERATION_ALLOWS_SYNC | ITERATION_ALLOWS_ASYNC | ITERATION_YIELD_STAR_FLAG,
    ITERATION_SPREAD = ITERATION_ALLOWS_SYNC | ITERATION_SPREAD_FLAG,
    ITERATION_DESTRUCTURING = ITERATION_ALLOWS_SYNC | ITERATION_DESTRUCTURING_FLAG,
    ITERATION_GENERATOR_RETURN = ITERATION_ALLOWS_SYNC,
    ITERATION_ASYNC_GENERATOR_RETURN = ITERATION_ALLOWS_ASYNC,
} IterationUse;
typedef struct TyIterationTypes {
    Ty const* yield_type;
    Ty const* return_type;
    Ty const* next_type;
} TyIterationTypes;
// What iterating a value of the type gives (every member of a union has to iterate), null if it doesn't
TyIterationTypes const* ty_iteration_types_of_iterable(Checker* me, Ty const* ty, u32 use);
TyIterationTypes const* ty_iteration_types_of_iterator(Checker* me, Ty const* ty, bool async); // null: not an iterator
// What for-of, spread and array destructuring get from a value: the yield type, below ES2015 the element type of an
// array (or string). Reports TS2488/TS2495/TS2461/TS2802... to `errs` if it isn't iterable (then it's any)
Ty const* ty_iterated_type(Checker* me, u32 use, Ty const* ty, Span span, ErrorCtx* errs);
TyIterationTypes const* ty_generator_return_iteration_types(Checker* me, Ty const* ret, bool async);
TyIterationTypes const* ty_generator_context(Checker* me, FunctionImpl const* generator);
void ty_check_generator_annotation(Checker* me, FunctionImpl const* generator, ErrorCtx* errs);
Ty const* ty_unwrapped_return_type(Checker* me, FunctionImpl const* function);
Ty const* ty_generator_type(Checker* me, bool async, Ty const* yield_type, Ty const* return_type, Ty const* next_type);

// check.c: what the statement/expression walk checks, with tsc's errors
void ty_check_assignable(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs); // TS2322 & co
void ty_check_type_predicate(Checker* me, Ty const* predicate, Ty const* param, Span span, ErrorCtx* errs); // TS2677
// The same for a source that is `expr`'s type: a literal or arrow function is reported at what inside it doesn't fit
// (tsc's elaborateError). Returns whether it was assignable
void ty_check_enum_member_value(Checker* me, Ty const* source, Span span, ErrorCtx* errs);
bool ty_check_assignable_expr(Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs);
// A `using` (TS2850) or `await using` (TS2851) initializer: disposable, null or undefined
void ty_check_using_initializer(Checker* me, Ty const* source, bool is_await, Span span, ErrorCtx* errs);
void ty_check_satisfies(Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs); // TS1360
// A class or interface that doesn't fit what it extends or implements: TS2416 at each of `members` (its own) that
// doesn't fit the base's, else `ts_code` with "<what> 'D' <relation> 'B'." at `span` (tsc's issueMemberSpecificError)
typedef struct HeritageMember {
    Atom const* name;
    Span span;
} HeritageMember;
Ty const* ty_class_base_instance(Checker* me, ClassDecl const* class_decl); // What it extends, null if nothing
bool ty_has_base_type(Checker* me, Ty const* source, Ty const* target);
// Whether the interface is on a cycle of `extends` (tsc reports TS2310 on each declaration of the interfaces in a
// cycle of base types, not on those that merely extend one)
bool ty_interface_in_base_cycle(Checker* me, Symbol const* symbol); // `source` is, extends or inherits from `target`
Atom const* ty_member_name(Checker* me, cstr const* name_string, Expr const* computed_key); // Null: not a known name
// Whether it fits (false: reported)
bool ty_check_heritage(
    Checker* me, Ty const* derived, Ty const* base, HeritageMember const* members, u32 count, unsigned ts_code,
    char const* what, char const* relation, Span span, ErrorCtx* errs
);
// TS2417: the static side of a class has to fit its base constructor's, whose signatures don't matter
bool ty_check_static_heritage(Checker* me, Ty const* static_type, Ty const* base_static, Span span, ErrorCtx* errs);
// tsc's checkIndexConstraints: the properties (TS2411) and index signatures (TS2413) of a class, interface or type
// literal fit the index signatures that apply to their names. `owner`: the class or interface (null: a type literal,
// all of whose members are its own), `bases`: an interface's base types, `name`: where what two bases bring together is
// reported
void ty_check_index_constraints(
    Checker* me, Ty const* type, Symbol const* owner, bool is_static, Ty const* const* bases, u32 base_count, Span name,
    ErrorCtx* errs
);
// tsc's checkComputedPropertyName: the key of `[key]` (`span`, with the brackets) is a string, number or symbol (TS2464)
// tsc's checkTypeForDuplicateIndexSignatures: of the index signatures among `members` (of all the declarations of an
// interface, of a class or type literal), at most one per key type (TS2374)
void ty_check_duplicate_index_signatures(Checker* me, struct InterfaceMember const* const* members, u32 count, ErrorCtx* errs);
// tsc's checkTypeParameterListsIdentical: the class and interface declarations of `symbol` declare the same type
// parameters (TS2428, on every declaration)
void ty_check_type_parameter_lists_identical(Checker* me, Symbol const* symbol, ErrorCtx* errs);
// Duplicate members (TS2300, TS2804): tsc's binder conflicts in a class's member tables (a method, accessor or
// property of the same name where the kinds don't merge) and its checkClassForDuplicateDeclarations
void ty_check_class_duplicate_members(Checker* me, ClassDecl* class_decl, ErrorCtx* errs);
// The same for the members of type literals and interfaces: `lists` are the member lists of the declarations (one for
// a type literal), whose binder table is shared; checkObjectTypeForDuplicateDeclarations runs per list
void ty_check_object_type_duplicate_members(Checker* me, VecInterfaceMember const* const* lists, u32 list_count, ErrorCtx* errs);
// TS2687: the property declarations of each instance member of the classes and interfaces that declare `symbol` (else of
// a class without one, else of a type literal) have the same modifiers and optionality
void ty_check_property_modifiers(Checker* me, Symbol const* symbol, struct ClassDecl const* class_decl,
    VecInterfaceMember const* type_literal, ErrorCtx* errs);
// `in_type`: of a class, interface or type literal member
// The property a key type names (a string, number or unique symbol literal), null if none; *is_number for numbers
Atom const* ty_property_name_of_key(Checker* me, Ty const* key, bool* is_number);
void ty_check_computed_name(Checker* me, Expr const* key, Span span, bool in_type, ErrorCtx* errs);
void ty_report_unfit_argument(Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs); // TS2345
void ty_report_unfit_this(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs); // TS2684
void ty_check_spread_type(Checker* me, Ty const* type, Span span, ErrorCtx* errs); // `...x` in a literal: TS2698
typedef struct TyWrittenProp {
    Atom const* name;
    Span span; // The whole property or attribute
} TyWrittenProp;
void ty_check_spread_overrides(Checker* me, Ty const* type, TyWrittenProp const* written, u32 count, Span spread_span, ErrorCtx* errs);
typedef enum JsxComponentCheck {
    JSX_COMPONENT_RETURN, // A function component's return type against JSX.Element | null
    JSX_COMPONENT_INSTANCE, // A class component's instance type against JSX.ElementClass
    JSX_COMPONENT_ELEMENT, // Either, against both
    JSX_COMPONENT_TYPE, // The tag's type against JSX.ElementType
} JsxComponentCheck;
bool ty_is_generic_object(Checker* me, Ty const* ty); // tsc's isGenericObjectType, roughly (check.c)
bool ty_check_jsx_component(Checker* me, Ty const* source, Ty const* target, Span tag_name, JsxComponentCheck kind, ErrorCtx* errs);
bool ty_is_comparable_either_way(Checker* me, Ty const* a, Ty const* b);
Ty const* ty_of_node(TypeNode const* node); // A written type, by the current checker
// tsc's createUnionOrIntersectionProperty: a union has no property that is private or protected in some member unless
// every member has it from the same declaration
bool ty_union_non_public_conflict(Checker* me, Ty const* object, Atom const* name);
// The flags of an index type as a key: those of its members for an intersection (`string & Tag` is a string key, tsc's
// isTypeAssignableToKind)
static inline u32 ty_key_flags(Ty const* index) {
    if (index->kind != TY_INTERSECTION) return index->flags;
    u32 flags = 0;
    for (u32 i = 0; i < index->composite.types.len; i++) flags |= index->composite.types.data[i]->flags;
    return flags;
}
IndexInfo const* ty_applicable_index_info(Checker* me, Ty const* object, Ty const* index); // Of an apparent type
// A number, symbol or pattern index signature applies to the key (tsc's isApplicableIndexType, not for `string` keys)
bool ty_specific_index_applies(Checker* me, IndexInfo const* info, Ty const* index);
// Member modifiers at a property access (tsc's checkPropertyAccessibility and the readonly checks of
// checkPropertyAccessExpressionOrQualifiedName): TS2341, TS2445, TS2446, TS2340, TS2855, TS2540, TS2542
void ty_check_member_modifiers(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs);
// `#x` in `#x in obj`: the instance type of the class that declares it (its constructor for a static `#x`), null if
// none does. `name` (may be null): the class's `#x`
Ty const* ty_private_name_owner(Checker* me, Expr const* private_name, Atom const** name);
void ty_check_in_expression(Checker* me, Expr const* left, Expr const* right, ErrorCtx* errs); // `left in right`
// `a.#x`: what tsc says about private names rather than TS2339 (TS18013, TS18014, TS18016). True if it reported
bool ty_check_private_name_access(Checker* me, PropertyExpr const* access, Ty const* object, ErrorCtx* errs);
// It assigns to a readonly property (TS2540), which makes its type the error type
bool ty_is_readonly_assignment(Checker* me, PropertyExpr const* access, Ty const* object);
// `new C()` of a class whose constructor is private or protected, from where it isn't accessible: TS2673, TS2674
bool ty_constructor_accessible(Checker* me, CallExpr const* call, Signature const* sig, Span span, ErrorCtx* errs);
// `object[index]` / `object.name`: TS2538, TS2493, TS2339...
void ty_check_property_access(
    Checker* me, Ty const* object, Ty const* index, Span object_span, Span index_span, bool object_is_named,
    bool error_if_not_found, ErrorCtx* errs
);
// `o[k]` under noImplicitAny that finds nothing: TS7053, TS7015, TS7052... (`span`: the whole access)
void ty_check_element_implicit_any(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs);
// A class's instance type as its constructor returns it (generic: applied to its own type parameters)
Ty const* ty_instance_type_of_class(Checker* me, struct Symbol const* class_symbol);
// What a compound assignment (`o.x += 1`) to `target` is checked against: a divergent setter's type for a property
// (tsc's checkAssignmentOperatorWorker), else the type it reads
Ty const* ty_compound_assignee_type(Checker* me, struct Expr const* target, Ty const* read_type);
// Around the typecheck walk of a conditional type's true branch
// `O[K]` written as a type: the property, element or index signature it reads is there (tsc's checkIndexedAccessType)
void ty_check_indexed_access_type(Checker* me, Ty const* object, Ty const* index, Span index_span, Span span, ErrorCtx* errs);
// TS2536 for `o[k]` with k generic (tsc's checkIndexedAccessIndexType; the expression's type is the error type)
void ty_check_generic_element_access(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs);
Ty const* ty_invalid_generic_element_access(Checker* me, PropertyExpr const* access, Ty const* object);
bool ty_indexed_access_index_ok(Checker* me, Ty const* indexed);
bool ty_is_numeric_string_type(Checker* me, Ty const* ty); // `${number}` (tsc's numericStringType)
void ty_check_infer_constraints(Checker* me, struct InferredType const* inferred, ErrorCtx* errs);
bool ty_type_argument_satisfies_constraint(Checker* me, TyList params, VecSpannedType const* args, u32 index);
void ty_check_type_reference_constraints(Checker* me, Symbol const* symbol, VecSpannedType const* args, ErrorCtx* errs);

// call.c
// `contextual`: the contextual type of the call expression (return type inference), may be null
Ty const* ty_call_return_type(Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual);
void ty_call_typecheck(Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual, Span call_span, ErrorCtx* errs);
// A tagged template `` tag`a${x}` ``, the EXPR_TEMPLATE: a call of the tag with a TemplateStringsArray and the substitutions
Ty const* ty_tagged_template_return_type(Checker* me, Ty const* callee, Expr const* template, Ty const* contextual);
void ty_tagged_template_typecheck(Checker* me, Ty const* callee, Expr const* template, Ty const* contextual, ErrorCtx* errs);
Ty const* ty_awaited(Checker* me, Ty const* ty);
Ty const* ty_promise_like_of(Checker* me, Ty const* ty);
Ty const* ty_awaited_no_alias(Checker* me, Ty const* ty);
Ty const* ty_async_contextual_awaited(Checker* me, Ty const* contextual); // function.c
Ty const* ty_awaited_returned(Checker* me, Ty const* ty);

// ty_display.c
// The old display's `underline` mode underlines a type's whole rendering, so the `.types` traversal can do that from
// the string length when it switches over (stage 4); no display mode needed here.
typedef struct TyDisplayVisit {
    Ty const* ty;
    struct TyDisplayVisit const* outer;
} TyDisplayVisit;

// tsc's GenerateNamesForShadowedTypeParams (the types baseline): a type parameter whose name another one printed in
// the same scope already has, or that names another type parameter in scope where the type is printed, prints as
// `T_1`, `T_2`... (typeParameterToName)
typedef struct TyParamName {
    Ty const* param;
    Atom const* name;
} TyParamName;
typedef struct TyParamNames {
    TyList in_scope; // The declared type parameters in scope where the type is printed, innermost last
    // The names given so far (tsc's typeParameterNames and typeParameterNamesByText): a signature is a scope, it drops
    // the names given inside it on its way out
    TyParamName* given;
    u32 len;
    u32 cap;
} TyParamNames;

typedef struct TyDisplay {
    cstr* target;
    u32 depth;
    TyDisplayVisit const* visiting; // The anonymous types being printed further up the stack
    // Literal types inside object types that came from expressions print as their primitive: tsc widens the
    // properties of an object literal when it types it, the legacy checker only when it reports. HACK
    bool widen_literals;
    // Where the type is printed from (tsc's enclosingDeclaration): names are qualified as far as needed to be reachable
    // from there (`T` inside namespace N, `N.T` outside, an import's name, `import("a").T` when nothing names it).
    // Null: the bare name (tsc's lookupSymbolChainWorker without an enclosing declaration: what error messages show),
    // or fully qualified with `fully_qualified` (tsc's UseFullyQualifiedType, when a source and target print the same)
    struct Scope const* enclosing;
    bool fully_qualified;
    // The types baseline's `^^^` line: every character printed gets a `^`, or a space where tsc's declaration
    // emitter would have reused a node from the source (`reused`, see display_signature()). Null: not wanted
    cstr* underline;
    bool reused;
    // The type parameters of an instantiated generic signature being printed: tsc gives it fresh ones, so nothing that
    // mentions them is reused inside of it
    TyList fresh_params;
    bool unique_symbols; // Print unique symbol types as `unique symbol` rather than `typeof sym` (types baseline)
    TyList infer_params; // Inside the extends type of a conditional type: its own `infer` type parameters
    // Reprinting an annotation of this instantiated signature (tsc's context.mapper): its type parameters that the
    // instantiation maps aren't reused, they print what they're mapped to
    struct Signature const* mapped_by;
    // A function expression that tsc types syntactically where it is printed (the returned function of
    // typeFromSingleReturnExpression): its parameter declarations are reused, `: ` and all
    struct FunctionImpl const* syntactic_function;
    TyParamNames* param_names; // Null: type parameters print as they're called
} TyDisplay;
void ty_display(Checker* me, TyDisplay d, Ty const* ty);
void ty_display_signature(Checker* me, TyDisplay d, Signature const* sig);
void ty_display_predicate(Checker* me, TyDisplay d, TyPredicate const* predicate);
cstr ty_to_cstr(Checker* me, Ty const* ty);
cstr ty_prop_name_cstr(Checker* me, Atom const* name); // As messages show it: `x`, `[Symbol.iterator]`
cstr ty_prop_written_name(Checker* me, Prop const* prop); // As declared if not an identifier (`"a"`, `2.0`, `["a"]`)
