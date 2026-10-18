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
typedef struct BinaryOpExpr BinaryOpExpr;
typedef struct Checker Checker;
typedef struct Prop Prop;
struct ObjectLiteralProp;
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

// Intern table: open addressing, keys are arrays of u64 words (a kind tag followed by ids/pointers/bits), values are
// pointers. Used for every kind of identity in the checker.
typedef struct InternEntry {
    // The key's hash (never 0: 0 is an empty slot), its top 16 bits the key's word count (INTERN_LONG_KEY: in the block)
    u64 hash;
    // Arena-allocated: the value, then the key's words (after the word count when it's INTERN_LONG_KEY or more). The
    // atoms table has the Atom here instead
    void const** block;
} InternEntry;

typedef struct InternTable {
    InternEntry* entries;
    u32 cap; // power of two or 0
    u32 len;
} InternTable;

// The commonest two-word keys of Checker.links, (kind, AST node or other address) -> value, with the key in the entry
// itself: no arena block, no hash to keep (vscode: 3.5M of them). See ty_link_get()
typedef struct LinkEntry {
    u64 key; // The kind in the top byte, the subject's address below. 0: empty
    void const* value;
} LinkEntry;

typedef struct LinkTable {
    LinkEntry* entries;
    u32 cap; // 0 or 1024 * 1.5^n (about)
    u32 len;
} LinkTable;

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
    // tsc's EvolvingArrayType: what `let x = []` is while control flow sees elements being pushed. Only exists inside
    // a flow query (ty/flow.c): ty_flow_type() answers its final array type
    TY_EVOLVING_ARRAY,
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
    // (ty_widened_objects() has work to do). Also on the regular twin before widening (OBJECT_LITERAL_REGULAR, which
    // ty_is_fresh_object() tells apart): the flag is tsc's ContainsObjectOrArrayLiteral as far as widening goes
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
    // Per element, the declaration its label comes from (a TupleElement or a FunctionArg), null if none or unknown. Null
    // if none is known. Part of the target's identity: two tuples labelled by different declarations are different
    // types (tsc's getTupleTargetType keys on namedMemberDeclarations)
    void const* const* labels;
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
    uint8_t reduced_state; // Intersections: whether ty_reduced() is never (REDUCED_*, checker.c)
    Members const* members;
    Members const* unexpanded; // resolve_members_inner()'s result before expand_mapped(), see ty_mapped_info_unexpanded()
    Ty const* equivalent_base; // See ty_non_augmenting_base()
} TyObjectState;

typedef struct TyParamState {
    GenericItem const* item; // Null for type parameters that were only ever seen by reference (and infer/mapped ones)
    TypeNode const* constraint_node;
    LazyState constraint_state;
    Ty const* constraint; // null if unconstrained
    LazyState fallback_state;
    bool fallback_circular; // Its default reaches itself (TS2716): then it has none
    Ty const* fallback;
    bool is_infer; // declared by `infer X` in the extends type of a conditional type
    bool is_this; // The `this` type of a class or interface (Ty.declared.this_param), prints as `this`
    bool is_check_marker; // `sub-T`/`super-T` of a variance annotation check (ty_variance_check_instance())
    // A fresh copy made when a generic signature is instantiated (tsc's cloneTypeParameter): its constraint and default
    // are those of `clone_of` instantiated with `clone_mapper`
    Ty const* clone_of;
    TyMapper const* clone_mapper;
    // Whether the declared constraint reaches the parameter itself (tsc's getResolvedBaseConstraint returning
    // circularConstraintType, TS2313): then it has none. 0 not known yet, 1 no, 2 yes
    unsigned char circular;
    // A distributed type parameter (tsgo 7.1's isDistributed, plan 57): the one it is the twin of, which is also its
    // constraint. Mappers, inference and the printer see through it (ty_non_distributed())
    Ty const* distributed_of;
    Ty const* distributed; // The original's twin, made on demand (ty_distributed_type_param())
} TyParamState;


struct Ty {
    u32 id;
    u32 kind; // TyKind
    u32 flags; // TyFlags
    u32 xflags; // TYX_*: flags that propagate like tsc's ObjectFlags.PropagatingFlags
    // ty_compare()'s cached prefix (order.c's order_key()), 0 until first compared
    u64 order_key;
    // How to print the type if it was made by a type alias (tsc's aliasSymbol/aliasTypeArguments)
    Symbol const* alias;
    TyList alias_args;
    union {
        struct {
            csview name;
            // An unresolved reference (ty_unresolved()): its type arguments, printed after the name. Never instantiated,
            // like tsc's error types for unresolved symbols
            TyList unresolved_args;
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
            uint8_t kind; // TyDeclaredKind
            // TY_DECLARED_NAMESPACE of a JavaScript `var ns = {}` with expandos (is_js_container()): open-ended like
            // the object literal it is (tsc's getJSContainerObjectType makes it ObjectFlags.JSLiteral)
            bool js_container;
            u32 outer_count; // See type_params
            Symbol const* symbol; // null for tuple targets and synthetics
            struct Symbol const* namespace_symbol; // TY_DECLARED_NAMESPACE only: the namespace, enum or module
            // Set if this is the value of a declaration with several value declarations (a namespace merged with a
            // function, class or enum): the members are those of all of them, see ty_merged_value_type()
            Symbol const* merged_value;
            Atom const* name;
            // tsc's typeParameters: the outer ones (tsc's outerTypeParameters: those of the functions, classes... a
            // local class or interface is declared in, see OuterGenerics) and then its own (localTypeParameters),
            // which are what type arguments are written for. The static side of a class has only the outer ones
            TyList type_params;
            OuterGenerics const* outer; // Where the outer ones come from (display: `f.C`, tsgo drops their arguments)
            // Classes, and interfaces that mention `this` (tsc's isThislessInterface): the polymorphic `this` type, a
            // type parameter constrained to the type itself. Null otherwise
            Ty const* this_param;
            // Classes and interfaces: the members as declared (mentioning this_param), what references instantiate
            // (`state` has the members with `this` = the type itself). See Members.bases
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
            // First made by lowering an array, tuple or generic type reference node (tsc's ObjectFlags.FromTypeNode, which
            // the instantiation cache keeps from whoever made it first): its own recursion identity
            bool from_type_node;
        } reference;
        struct {
            TypeNode const* node; // TYPE_INTERFACE or TYPE_CALLABLE. Null for instantiations and functions
            // The type of a function declaration/expression, method, accessor or constructor: one signature, read
            // straight off the FunctionImpl (the return type is inferred from its body if it isn't annotated)
            FunctionImpl const* function;
            union {
                Symbol const* merged_value; // Function overloads, see ty_merged_value_type(). Unless `is_reverse`
                // is_reverse: a reverse mapped type (tsc's ReverseMappedType, ty_reverse_mapped_object()): members
                // resolved on first use, each property's type inferred when asked for. Read through ty_reverse_of()
                struct ReverseMapped const* reverse;
            };
            union {
                FunctionImpl const* const* overloads; // If overload_count, the type of an overloaded method: its signatures
                // object_literal: the literal expression it is the type of (or was widened from), if known: where error
                // messages print it from (tsc's symbol.valueDeclaration, see ty_report_enclosing())
                struct Expr const* literal_expr;
                // rest_of_binding (never an object_literal nor overloaded): the binding whose rest it is (tsc's
                // getRestType gives it that symbol), which orders it in unions
                struct Symbol const* rest_binding;
            };
            Ty const* target; // Instantiations: the uninstantiated TY_ANONYMOUS
            TyMapper const* mapper;
            // Uninstantiated only: the type parameters declared outside the node that it mentions. Instantiation
            // only cares about these. If unknown (the node has parts that aren't syntax), assume every one in scope
            TyList free_params;
            TyObjectState* state;
            uint16_t overload_count;
            // object_literal: which alternative of a spread of a union it is (ty_object_literal_alternative()), 0 if not
            // one: part of its identity, and of its regular and widened versions'
            uint16_t spread_alternative;
            LowerCtx ctx;
            // An instantiation expression (`Bar<string>` as a value): target is any object type, and the mapper is
            // about the type parameters of its signatures, which lose them
            bool applies_type_args : 1;
            bool realiased : 1; // the same type as `target` + `mapper` by another alias name, see ty_realias()
            // An alias body's homomorphic mapped instance (ty_alias_body_mapped_instance()): any instantiation makes it the
            // ordinary instance again, as tsc's couldContainTypeVariables lets any mapper through to it
            bool alias_body_twin : 1;
            bool free_params_unknown : 1;
            // Uninstantiated only: the node mentions no type parameter, its free_params are the ones around the Block
            // it is in (tsc's isTypeParameterPossiblyReferenced): instantiated for identity's sake, its members the same
            bool block_params_only : 1;
            bool object_literal : 1; // Made by an object literal expression (ty_object_literal())
            // OBJECT_LITERAL_REGULAR: TYF_FRESH_OBJECT means it still widens, but it isn't fresh (ty_is_fresh_object())
            bool regular : 1;
            bool jsx_attributes : 1; // OBJECT_JSX_ATTRIBUTES(_FRESH): hyphenated names are never excess, and excess ones say so differently
            bool js_literal : 1; // OBJECT_JS: an object literal of a JavaScript file (tsc's ObjectFlags.JSLiteral)
            bool is_reverse : 1; // `reverse` is set, not `merged_value`
            bool no_inferable_index : 1; // OBJECT_NO_INFERABLE_INDEX
            bool contains_spread : 1; // OBJECT_CONTAINS_SPREAD
            // The type of a namespace import of an `export =` module, made anew (tsc's cloneTypeAsModuleType: an anonymous
            // type whose symbol is a clone of the assigned one, `typeof x`): KEY_MODULE_CLONE says for which alias
            bool module_clone : 1;
            // `import("m")`'s synthetic default object: tsc's anonymousSymbol (a TypeLiteral symbol without members, see
            // getTypeWithSyntheticDefaultImportType), which makes isEmptyAnonymousObjectType say it's empty
            bool empty_symbol : 1;
            bool mapped : 1; // A mapped type node (`{ [K in C]: X }`) or an instantiation of one (tsc's ObjectFlags.Mapped)
            bool rest_of_binding : 1; // A destructuring's object rest: `rest_binding` is set
        } anonymous;
        struct {
            TyList types; // sorted by id, no duplicates
            TyObjectState* state; // Intersections: their signatures and index infos (see ty_members())
            // Unions: what it was written/built as, for display (tsc's UnionType.origin): a union of named unions and
            // other members, an intersection it was distributed from, or a `keyof` (TY_KEYOF). Not interned. Null if none
            Ty const* origin;
        } composite;
        struct {
            Ty const* operand;
        } keyof;
        struct {
            Ty const* object;
            Ty const* index;
            // An element access read under noUncheckedIndexedAccess (tsc's AccessFlags.IncludeUndefined, which it keeps
            // on the IndexedAccessType): what an index signature gives includes undefined once resolved, and so does
            // its constraint. A type of its own, printed the same
            bool include_undefined;
        } indexed;
        struct {
            Ty const* check;
            Ty const* extends;
            // Don't read the branches directly, use ty_conditional_branch(): a branch that mentions the type alias
            // being resolved (`type R<T> = T extends [infer H, ...infer Rest] ? R<Rest> : []`) is a node to lower on
            // first use, plus the mapper it has been instantiated with since
            TyCondBranch* branches; // [0] false, [1] true
            bool distributive; // the check type was written as a naked type parameter
            // tsc's checkTuples: the check and extends types were written as tuples of one arity without optional or rest
            // elements (`[T] extends [U]`), whose generic elements defer it like a generic check type
            bool check_tuples;
            // Like tsc's ConditionalRoot + mapper: the conditional type as it was written, and how to get from there
            // to this one (null for the root itself). Instantiation always starts over from the root, because
            // distribution has to see what the *written* check type parameter ends up as (`Exclude<keyof T, K>`
            // inside `Omit<T, K>` only distributes once T is known)
            Ty const* root;
            TyMapper const* mapper;
            // Of a root lowered from a node (lower.c; null otherwise and on instances): the type parameters declared
            // outside it that instantiations are keyed by, and the root as the type alias declaring it names it (tsc's
            // root.aliasSymbol and aliasTypeArguments)
            struct TyConditionalOuter const* outer;
            Ty const* root_aliased;
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
            Ty const* element; // What was pushed so far (never: nothing), not subtype reduced
            Ty const* final_array; // Cached by flow.c's final_array_type(), null until then
        } evolving_array;
        struct {
            TyStringMapping mapping;
            Symbol const* symbol; // The lib alias, for display. May be null
            Ty const* type; // A type variable, string, any, or a pattern
        } string_mapping;
    };
};

// tsgo's getNonDistributedTypeParameter
static inline Ty const* ty_non_distributed(Ty const* ty) {
    return ty->kind == TY_TYPE_PARAM && ty->param.state->distributed_of ? ty->param.state->distributed_of : ty;
}

// (Hundreds of thousands per checker; the union is as big as `declared`)
static_assert(sizeof(Ty) == 144);

// How many bytes of a Ty of the kind are there: ty_alloc() allocates only the header and the kind's variant, so a Ty
// is never copied whole (ty_copy())
size_t ty_size_of_kind(TyKind kind);

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
    PROP_OPTIONAL = 1 << 0, // Its type has undefined (tsc's addOptionality): see prop_type()
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
    // Optional because a destructuring pattern's element has a default (`{ x = 1 } = a`): tsc only sets the flag, the
    // type doesn't get undefined added like a declared `x?: T`
    PROP_PATTERN_OPTIONAL = 1 << 13,
    PROP_PROTOTYPE = 1 << 14, // A class's `prototype` (tsc's SymbolFlags.Prototype): relations skip it as a target
    PROP_CLASS_GETTER = 1 << 15, // A class accessor with a getter (tsc's SymbolFlags.GetAccessor, PROP_GETTER for literals)
    // tsc's symbol has declarations but no valueDeclaration (no "declared here"): a copy a spread or rest made as a fresh
    // symbol (getSpreadSymbol for a property whose readonly-ness changes, getAnonymousPartialType), a namespace's export
    // through an alias (`export { f }`)
    PROP_NO_VALUE_DECLARATION = 1 << 16,
} PropFlags;

// How a property's name prints (tsc's getPropertyNameNodeForSymbol), from how its declarations wrote it. Without
// PROP_NAME_KNOWN it's read off PropDecl.name_span (see ty_display.c's display_member_name)
typedef enum PropNameForm {
    PROP_NAME_KNOWN = 1 << 0,
    PROP_NAME_STRING = 1 << 1, // Every declaration names it with a string: a string literal, a string-typed computed name
    PROP_NAME_SINGLE_QUOTED = 1 << 2, // Every declaration names it with a 'single-quoted' string literal
    PROP_NAME_TYPED = 1 << 3, // tsc's nameType: a computed name's literal type (not a literal written in a class or
                              // interface, which the binder names), a mapped type's key: `-1` prints as `[-1]`
    // A new symbol named by its text, without declaration or nameType (an import attribute): symbolToString prints the
    // text as it is (`Property 'resolution-mode'`), unlike a type, which quotes it
    PROP_NAME_RAW = 1 << 4,
} PropNameForm;
// The form of one declaration's name: `written` as written, or the type of its computed name (`literal_key`: an
// object literal's, whose literal type is always its nameType)
uint8_t ty_prop_name_form(csview written, Expr const* computed, Ty const* key_type, bool literal_key);
uint8_t ty_prop_name_form_merge(uint8_t a, uint8_t b);
// A property's form, known or read off its declaration (what a copy of it keeps)
uint8_t ty_prop_name_form_of(Prop const* prop);

// A property of an object type. Deliberately the seed of plan 03's persistent symbol.
// What a declared property knows about its declaration. Instantiations (which follow `target`) and synthetic
// properties share ty_prop_decl_none, all zero and read-only: a property is 56 bytes, not 250
// What few props have: overloads, computed literal names, accessors with a write type, mapped and reverse mapped types'
// (PropDecl.rare)
typedef struct PropDeclRare {
    // An overloaded method: all of its signatures (a source of the type, see PropDecl)
    FunctionImpl const* const* overloads;
    u32 overload_count;
    Ty const* name_type; // An object literal's computed literal name's type, as it was (fresh): what keyof gives
    // A divergent accessor's setter parameter type (tsc's write type), lowered with `ctx`; instantiations follow
    // `target`. See prop_write_type()
    TypeNode const* write_node;
    // An object literal's accessor pair: the setter (`function` is the getter), whose parameter's annotation is the
    // write type. See prop_write_type()
    FunctionImpl const* setter;
    Span setter_param; // A type member accessor pair's setter parameter name (to print a divergent pair)
    struct MappedInfo const* template_of; // The template of this mapped type, as a property its properties instantiate
    // Types baseline only: a mapped type's property is declared where the property it maps is (tsc's mapped symbol's
    // declarations), whose annotation is reused when it says the same
    struct Prop const* mapped_from;
    // Symbols baseline only: the mapped type's modifiers type when it is an intersection, whose members' properties of
    // this name declare it too (tsc's intersection property symbol; a union's are added when the property is made)
    Ty const* mapped_intersection;
    // A property of a reverse mapped type: its type is inferred from `reverse_source` (the source property's type) on
    // first use (tsc's getTypeOfReverseMappedSymbol). `reverse_declared` is the source property's declared type
    struct ReverseMapped const* reverse_of;
    Ty const* reverse_source;
    Ty const* reverse_declared;
    // Types baseline only: a property spread from a module's variable (`{ ...ns }`) is declared by that variable (tsc's
    // getSpreadSymbol keeps the declarations), whose annotation or literal initializer is reused
    Symbol const* spread_variable;
    Symbol const* spread_namespace; // The namespace or module whose export spread_variable is (symbols baseline)
    // The spread made a fresh symbol (tsc's getSpreadSymbol of a readonly or set-only one, a partial spread's): not
    // spread_variable's, no parent, a bare name in the symbols baseline
    bool spread_fresh;
    // A type member getter with a body (TS1183) and no return annotation: its type is the body's return type
    FunctionImpl const* getter_body;
    // A class accessor's get and set declarations: declaration emit prints the property as them in an object type (tsc's
    // addPropertyToElementList)
    FunctionImpl const* class_getter;
    FunctionImpl const* class_setter;
    bool class_auto_accessor; // `accessor x`: printed as a getter and a setter the same way
} PropDeclRare;

typedef struct PropDecl {
    // Exactly one source of the type (or it is known from the start): a node to lower, what the AST evaluates lazily
    // (an inferred class field, an accessor), a method, the overloads of a method (in `rare`), a symbol, a target to
    // instantiate
    TypeNode const* node;
    LazyType const* lazy;
    FunctionImpl const* function; // A class method: ty_function_type()
    Symbol const* symbol; // A member of a namespace: the type of the symbol as a value
    LowerCtx ctx;
    uint8_t name_form; // PropNameForm: how the name prints
    bool parameter_property; // `constructor(public x: T)`: declared by the parameter, decl_spans[0]
    // Where it was declared, for the symbols baseline (not copied to instantiations, follow `target`)
    Span name_span;
    Span const* decl_spans;
    u32 decl_span_count;
    bool name_is_value; // The name was written as a string or number literal
    bool literal_own; // A property of an object literal written in it (not spread): see ObjectLiteralProp.own
    // A type member's: the kinds of declaration its symbol has so far (1 property, 2 method, 4 get, 8 set; 0 unknown),
    // what a later member of its name joins or conflicts with (builder_add_interface_member())
    uint8_t member_kinds;
    // Types baseline only: an annotation that still says the property's type, reused (see ty_display.c): of a property
    // it was spread or mapped from, an object literal property's JSDoc `@type`
    TypeNode const* asserted;
    // Types baseline only: the object literal member that declared it (tsc's valueDeclaration), whose pseudo type
    // (pseudo.h) prints it where it says the same. Copies of the property (widened, spread) keep it
    struct ObjectExprItem const* item;
    // Arena-allocated, null if all of it is: read through prop_decl_rare(), made by prop_decl_rare_mut() (members.c).
    // Never shared: a copied PropDecl gets a copy (prop_copy_into())
    PropDeclRare* rare;
} PropDecl;

static_assert(sizeof(PropDecl) == 96);

static inline PropDeclRare const* prop_decl_rare(PropDecl const* decl) {
    static const PropDeclRare none = {0};
    return decl->rare ? decl->rare : &none;
}

extern PropDecl const ty_prop_decl_none;
// What a tuple element property shares: only name_is_value, read-only like ty_prop_decl_none
extern PropDecl const ty_prop_decl_value_name;

struct Prop {
    Atom const* name;
    u32 flags;
    uint8_t type_state; // LazyState
    // Its declaration can't mention `this` (tsc's isThisless*): instantiating only `this` leaves it as it is
    bool thisless : 1;
    bool circular : 1; // Its type needed its type (a mapped type property: TS2615 when done)
    // A property of a mapped type: the (resolved) mapped type is in the checker's links (KEY_PROP_MAPPED_TYPE), for
    // TS2615, see prop_type()
    bool mapped : 1;
    // prop_write_type() is known: in the checker's links (KEY_PROP_WRITE_TYPE), null there for none
    bool write_type_known : 1;
    // An intersection's property several of its members have (tsc's synthetic property): what they are is in the
    // checker's links, see ty_synthetic_prop_parts()
    bool synthetic : 1;
    // A mapped type's template: the properties that target it are the mapped type's (tsc's getTypeOfMappedSymbol)
    bool is_template : 1;
    Symbol const* parent; // the declared type it was declared in, may be null
    Prop const* target;
    TyMapper const* mapper;
    Ty const* type;
    PropDecl* decl; // Never null (ty_prop_decl_none)
};
// (Millions of instantiated ones: zod's are 86 MB at 80 bytes)
static_assert(sizeof(Prop) == 56);

typedef enum SignatureFlags {
    SIGNATURE_CONSTRUCT = 1 << 0,
    SIGNATURE_ABSTRACT  = 1 << 1,
    SIGNATURE_METHOD    = 1 << 2, // declared with method syntax: parameters are bivariant even under strictFunctionTypes
    SIGNATURE_NON_INFERRABLE = 1 << 3, // tsc's SignatureFlags.IsNonInferrable: a return-only signature (expr.c)
    // A union signature one of whose members (tsc's compositeSignatures) is abstract: `new` of it is TS2511 (tsc's
    // someSignature in resolveNewExpression)
    SIGNATURE_SOME_ABSTRACT = 1 << 4,
} SignatureFlags;

typedef struct SigParam {
    Atom const* name;
    Ty const* type;
    bool optional;
    bool rest;
    // `type` already is what the position takes, undefined included or reduced away: a combined union signature's
    // `(number | undefined) & string` is never (tsc's combineUnionOrIntersectionParameters sets the resolved type, no
    // optionality added). Else an optional parameter's position takes `type | undefined` (ty_sig_param_type())
    bool optionality_included;
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
    // Resolved lazily by signature_resolve()
    uint8_t state; // LazyState
    // The return type is lazier than the rest, use signature_return_type(): it's often inferred from a function body
    // that is in the middle of being checked
    uint8_t ret_state; // LazyState
    bool predicate_done : 1;
    bool predicate_in_flight : 1; // signature_predicate() is lowering its type node
    bool drops_mapped_type_params; // Part of an instantiation expression, see Ty.anonymous.applies_type_args
    // Its mapper gives its own type parameters their arguments (ty_signature_instantiation()). Otherwise an outer
    // mapper that has them (a recursive type met again inside its own instantiation) doesn't reach inside: they're
    // its own, fresh ones (see clone_type_params())
    bool instantiates_own_params;
    // Its own type parameters are cloned whatever the mapper (tsc's instantiateSignature always does; clone_type_params()
    // skips it where nothing can tell, but a function expression typed by it shows its clones in the types baseline)
    bool fresh_type_params;
    LowerCtx ctx;
    u32 param_count;
    u32 min_args;
    // Exactly one source: a function (a declaration/expression, or a function type node, see FunctionImpl.is_type)
    // or a target to instantiate
    FunctionImpl const* function;
    // Construct signatures of a class: the type parameters are the class's and the return type is its instance type,
    // whatever `node` or `target` say
    Ty const* class_instance;
    TyList class_type_params;
    Span name_span; // For the symbols baseline
    Span decl_span;
    // A combination of signatures (tsc's combineSignaturesOfUnionMembers): the one whose declaration it has, whose
    // parameters' declarations its own (synthesized) parameters don't have
    struct Signature const* declared_by;
    Signature const* target;
    TyMapper const* mapper;
    TyList type_params;
    SigParam const* params;
    Ty const* this_type; // `this: T`, not among params. Null if not declared
    Ty const* ret;
    TyPredicate const* predicate; // Use signature_predicate()
};
static_assert(sizeof(Signature) == 160);

// A member with a computed name that an object literal's index signature was made of (`[a]: 1`)
typedef struct IndexComponent {
    Expr const* name; // What's in the brackets
    Ty const* type;
    struct ObjectExprItem const* item; // The member (declaration maps), if known
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
// `keyof` a mapped type with an `as` clause over `keyof X` (tsc's getIndexTypeForMappedType), null for another constraint
Ty const* ty_mapped_type_name_keys(Checker* me, MappedInfo const* mapped);

// The T for which `{ [K in keyof operand]: template }` (`mapped`) would be `source` (tsc's ReverseMappedType)
typedef struct ReverseMapped {
    Ty const* source;
    MappedInfo const* mapped;
    Ty const* operand;
} ReverseMapped;
Ty const* ty_reverse_mapped_object(Checker* me, ReverseMapped const* reverse);
static inline ReverseMapped const* ty_reverse_of(Ty const* ty) {
    return ty->kind == TY_ANONYMOUS && ty->anonymous.is_reverse ? ty->anonymous.reverse : nullptr;
}
Ty const* ty_reverse_mapped_member_type(Checker* me, ReverseMapped const* reverse, Ty const* source);
// (infer.c) What a property of type `source` says about `operand[K]` (tsc's inferReverseMappedType)
Ty const* ty_reverse_mapped_prop_type(Checker* me, Ty const* source, MappedInfo const* mapped, Ty const* operand);
// (infer.c) The properties of a reverse mapped type's source, malloc'd, types as declared
u32 ty_reverse_mapped_source_props(Checker* me, Ty const* source, struct ObjectLiteralProp** out);
Ty const* ty_mapped_template_instantiated(Checker* me, MappedInfo const* mapped, TyMapper const* mapper);

struct Members {
    // (The counts are packed in pairs: there are tens of thousands of these)
    Prop* const* props; // declaration order
    u32 prop_count;
    u32 call_count;
    Prop* const* props_by_name; // sorted by name atom address, for lookup. Null for small tables
    Signature* const* call;
    Signature* const* construct;
    u32 construct_count;
    u32 index_count;
    IndexInfo const* index;
    // Non-null for a mapped type whose keys aren't known. It has no other members, except `{ [K in keyof T]: X }` with T
    // generic: a property per property of T's constraint (see expand_generic_homomorphic())
    MappedInfo const* mapped;
    // Arena-allocated, null if all of it is: read through members_rare() (there are a million Members on vscode)
    struct MembersRare const* rare;
};

// What few Members have: mapped types' expansions, generic bases
typedef struct MembersRare {
    MappedInfo const* expanded_from; // The mapped type these are the members of, once its keys are known
    // A generic mapped type whose members are those of the lower bound of its keys (expand_mapped()), maybe none: they
    // are all it has as far as property access goes
    bool generic_keys_known;
    // HACK: a generic mapped type whose expansion left out index signatures tsc's would have (the non-homomorphic parts'
    // of an intersection, expand_generic_intersection_homomorphic()): what it seems to lack isn't reported missing
    bool index_signatures_incomplete;
    // A mapped type (expanded_from) one of whose properties' types needed itself (TS2615, tsc's containsError): it
    // prints in mapped syntax
    bool contains_error;
    // Only in the raw members of a class or interface (declared_raw_members()) with a base whose members depend on the
    // type arguments (`interface I<T extends object> extends T`, or extending such a type): the bases, in order, with
    // the `this` type as this-argument. The members above are then only the type's own, and whoever instantiates or
    // uses them adds each base's (instantiated) members (tsc's resolveObjectTypeMembers)
    Ty const* const* bases;
    u32 base_count;
    // Only in the raw members of a generic class or interface: the bases (with the `this` type as this-argument) whose
    // instantiation resolves something about its type arguments (a conditional type, an indexed access, a `keyof`),
    // which tsc instantiates whenever a reference's members are resolved (see probe_eager_bases())
    Ty const* const* eager_bases;
    u32 eager_base_count;
    // Only in the raw members of a class or interface: a base's type arguments mention the type itself (zod's
    // `interface ZodJSONSchema extends _ZodJSONSchema`, `type _ZodJSONSchema = ZodUnion<[..., ZodArray<ZodJSONSchema>]>`):
    // what an eager base's instantiation can get back to (see mentions_members_in_flight())
    bool recursive_bases;
    // Members of more than 8 properties with distinct names that were instantiated (members_by_name_order()): the
    // index in props of each of props_by_name, which an instantiation's props (the same names in the same order) take
    u32 const* by_name_order;
    bool by_name_order_none; // (Computed, and a name is there twice)
} MembersRare;

static inline MembersRare const* members_rare(Members const* me) {
    static const MembersRare none = {0};
    return my rare ? my rare : &none;
}

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
    // Measured through something nonlinear (a `-?` mapped type): only identical arguments are related without a
    // structural comparison
    VARIANCE_UNMEASURABLE  = 1 << 4,
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
    // A missing property of the checked pair itself: heads the diagnostic in place of a head message, but stays below
    // a containing message (tsgo's reportRelationError, chainArgsMatch)
    bool missing_head;
    // The whole diagnostic goes to `span`, while this line stays where it is in the chain (an excess JSX attribute)
    bool relocates;
    // While relating: a marker that is never shown (tsgo's elidedInCompatibilityPyramid), a signature's return types
    bool elided;
} TyReportLine;
static inline void ty_report_line_drop(TyReportLine* me) { cstr_drop(&my message); }
static inline TyReportLine ty_report_line_clone(TyReportLine me) { me.message = cstr_clone(me.message); return me; }
#define g_class_key TyReportLine, ty_report_line
#include <cleat/generic/vec.h>

enum {
    TYX_CONTAINS_WIDENING = 1, // tsc's ObjectFlags.ContainsWideningType
    // `T & P` where T's constraint is primitives and P a primitive that constrains it further (tsc's
    // ObjectFlags.IsConstrainedTypeVariable): unions of them that cover T's constraint are T. Only read on the
    // intersection itself (it propagates like the others, meaninglessly)
    TYX_CONSTRAINED_TYPE_VARIABLE = 2,
    // A union with both undefined and null among its members, which is_unknown_like_union() (relate.c) then looks at
    // more closely. Only read on the union itself
    TYX_UNDEFINED_AND_NULL = 4,
    // A union with an intersection among its members that may reduce to never (tsc's ObjectFlags.ContainsIntersections):
    // ty_reduced() looks at it. Only read on the union itself
    TYX_CONTAINS_INTERSECTIONS = 8,
    // Something in it widens (ty_widened_nullable()): TYX_CONTAINS_WIDENING, and also an object literal that has such
    // a property only through a spread, whose ContainsWideningType tsc doesn't propagate (nothing in it is reported,
    // TS7018, but getWidenedType still widens every object literal)
    TYX_WIDENS = 16,
};

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
    // tsc's missingType: the undefined an optional property's or tuple element's type has under
    // exactOptionalPropertyTypes (and only then: ty_undefined_or_missing()), which getNonMissingTypeOfSymbol takes out
    // (ty_remove_missing())
    TY_MISSING,
    // tsc's optionalType: the undefined of an optional chain that ended (`a?.b` in `a?.b.c`), which only the rest of the
    // chain sees: it takes that out again and adds a plain undefined at the chain's end (ty_optional_expression_type())
    TY_OPTIONAL,
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
    // Without strictNullChecks, what the `null`/`undefined` expressions, `void e` and `[]`'s element are (tsc's
    // nullWideningType/undefinedWideningType): only these widen to any. The regular ones under strictNullChecks
    TY_UNDEFINED_WIDENING,
    TY_NULL_WIDENING,
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
    Span error_span; // If set, where an error at the node goes (tsc's getErrorSpanForNode: a declaration's name)
} CurrentNode;

// What Checker.deferred holds (tsc's checkDeferredNode cases tttt has)
typedef enum DeferredCheckKind {
    DEFERRED_FUNCTION_BODY, // A FunctionImpl: function_impl_typecheck_body()
    DEFERRED_CLASS_MEMBERS, // A class expression's ClassDecl: class_decl_typecheck_members()
    DEFERRED_JSX_ELEMENT, // An EXPR_JSX_ELEMENT Expr: its tag, attributes and children
    DEFERRED_EXPR, // An Expr checked by expr_typecheck() (a `void` operand)
} DeferredCheckKind;

typedef struct DeferredCheck {
    DeferredCheckKind kind;
    void const* node;
} DeferredCheck;

struct Checker {
    Program* program;
    struct Binder const* binder;
    // The initializer of the enum member being checked: an overshift there is an error, elsewhere a suggestion (TS6807)
    struct Expr const* enum_member_initializer;
    // The `o.x` property access whose missing name is being reported: its suggestions are only what it may access (tsc's
    // getSuggestedSymbolForNonexistentProperty with isValidPropertyAccessForCompletions)
    struct PropertyExpr const* suggestion_access;
    // Where its diagnostics go (ty_error_at()): its own while it checks its files (program_typecheck() merges them into
    // the program's), the program's otherwise. Only reported in the typecheck phase
    struct ErrorCtx* errs;
    // What its declaration walks painted (declaration_diagnostics_check()), kept: tsc's isVisible stays set on them, so
    // the suite's walk after emit names through a painted namespace (baseline_test_case.c)
    struct Symbol const** painted_kept;
    u32 painted_kept_count;
    u32 painted_kept_cap;
    double check_ms; // How long program_typecheck() spent checking its files (tttt --time)
    u32 files_taken; // Of them, the files it took from other checkers' shares (check_share())
    // Its share's checkers program_typecheck() dropped before it (Program.checker_memory) and the most any of them
    // held (checker_memory()), for tttt --time
    u32 recycles;
    u64 peak_memory;
    u32 file_order; // The index (among the checked files) of the file it checks, for Error.once_order
    // What the walk of the file being checked meets and tsc checks after the file's statements, in that order (tsc's
    // checkNodeDeferred and checkDeferredNodes): function expression bodies (arrows, object literal methods and
    // accessors), class expression members, JSX elements and `void` operands. What they ask is then asked after
    // everything around them settled, which decides cycles and the order types are made in (union order)
    bool defer_checks;
    struct SourceFile const* checking_file; // The file whose statements are being checked (defer_checks)
    // The materializations of deferred function bodies this incarnation reads (plan 78 stage 2: program.c's
    // deferred_bodies_acquire()), released when it is dropped. A bit per SourceFile.id of theirs, and the list
    u64* held_files; // malloc'd
    // Which of them it read while checking another file (cross demand): kept for its successor at a recycle (malloc'd,
    // as many words)
    u64* cross_files;
    u32 held_files_words;
    struct DeferredBodies** held; // malloc'd
    u32 held_count;
    u32 held_cap;
    DeferredCheck* deferred;
    u32 deferred_count;
    u32 deferred_cap;
    LazySlots lazy; // The memos of every LazyType in the AST, see lazy_type.h
    Arena arena;
    u32 next_type_id;
    Ty const* intrinsics[TY_INTRINSIC_COUNT];
    Ty const* typeof_type; // tsc's typeofType: made with the intrinsics, so its literals get tsc's low ids
    // Names flow asks about at every narrowing (checker_atom() hashes the name each time)
    struct Atom const* atom_constructor;
    struct Atom const* atom_prototype;
    struct Atom const* atom_bind;
    struct Atom const** index_atoms; // ty_index_atom(): "0", "1"... by index
    u32 index_atoms_len;
    u32* index_atoms_by_name; // 0..index_atoms_by_name_len-1 in the order of their atoms' addresses (members.c)
    u32 index_atoms_by_name_len;
    InternTable atoms;
    InternTable types;  // literals, unions, intersections, references, deferred, instantiated anonymous, tuple targets
    InternTable intersection_props; // (intersection id, property name) -> FoundProp, see find_prop()
    InternTable links;  // Symbol -> SymbolLinks, AST node -> Ty, Generic -> type parameter list...
    LinkTable node_links; // Same for some kinds of two-word keys: see ty_link_get()
    // The errors of calls' resolutions (call.c), kept until the checker is dropped
    struct ErrorCtx** call_errors;
    u32 call_errors_len, call_errors_cap;
    Ty const* array_target;
    Ty const* auto_array; // ty_auto_array(), made with array_target
    // The element type parameters every tuple target shares (a target of arity n has the first n): replaced by a
    // longer copy when a longer tuple needs more, so a target's list stays valid
    TyList tuple_params;
    Ty const* readonly_array_target;
    struct Signature** unknown_signature; // tsc's unknownSignature (no parameters, returning errorType) as a list of one
    Ty const* global_types[14]; // GlobalType in relate.c: String, Number... looked up on first use
    bool global_types_resolved[14];
    u32 instantiation_depth;
    Ty const* instantiation_stack[100]; // What each depth of instantiation_depth instantiates (tsc's instantiationStack)
    // Conditional types picked as the branch of a conditional type being instantiated now, which don't count towards
    // instantiation_depth (tsc resolves those in a loop, getConditionalType's tailCount), and the next ty_instantiate()
    // being one (ty_instantiate_tail())
    u32 instantiation_tail_count;
    u32 instantiation_epoch; // Bumped whenever instantiation_count starts over
    bool instantiating_tail;
    // The lists mappers recently kept, by their types (mapper_list()): most mappers map a declaration's type parameters,
    // and inference makes the same results over and over
    TyList mapper_lists[1024];
    // The root conditional type a distributive constraint is being instantiated from (tsc's forConstraint): a definitely
    // false check of it gives both branches
    Ty const* for_constraint_root;
    u32 flow_inline_level; // Aliased conditions being narrowed by (ty/flow.c), across nested flow queries like tsc's inlineLevel
    // The last reachability query and its answer (tsc's lastFlowNode): the walk from a statement stops at the previous
    // statement's node, which keeps checking a long straight-line block linear
    struct FlowNode const* last_reachability_flow;
    unsigned char last_reachability;
    u32 reverse_mapped_depth; // HACK: bounds the eager reverse mapped types of infer.c (tsc's are lazy)
    u32 instantiation_count;
    // The next ty_flow_type() is of a reference whose declared type had its constraints substituted (tsc's
    // getNarrowableTypeForReference): what is assigned to it is substituted too. Read and cleared by ty_flow_type()
    bool flow_substitutes_constraints;
    // The mappers being instantiated with, outermost first, each with what types instantiated with it inside the
    // outermost such call gave (tsc's activeTypeMappers and activeTypeMappersCaches): see ty_instantiate()
    struct ActiveMapper* active_mappers;
    u32 active_mapper_count, active_mapper_cap;
    struct JsxElement const* jsx_element; // The JSX element being checked (relation errors about its attributes)
    CurrentNode current_node; // See ty_enter_node()
    // What is being resolved, innermost last (tsc's resolutionTargets): see ty_resolving_push()
    struct Resolving* resolving;
    u32 resolving_len;
    u32 resolving_cap;
    u32 resolving_start; // Cycles aren't looked for below this (tsc's resolutionStart)
    u32 base_constructors_in_flight; // RESOLVING_BASE_CONSTRUCTOR entries on the stack (relate.c's tuple index relation)
    // The classes, interfaces and references whose members are being resolved, innermost last, with the resolving stack's
    // length and the instantiation depth when each started (see probe_eager_bases())
    struct MembersInFlight* members_in_flight;
    u32 members_in_flight_len, members_in_flight_cap;
    u32 eager_probe_depth; // probe_eager_bases() calls in flight
    u32 eager_probe_floor; // members_in_flight_len when the outermost of them started
    bool eager_probe_reported; // The outermost of them reported its runaway
    // The classes and interfaces whose bases are being lowered, innermost first, with their own members so far (see
    // resolve_declared_members())
    struct BasesFrame* bases_frames;
    // The call whose arguments are checked against a candidate to report why it doesn't fit: its arguments' contextual
    // types are that candidate's parameters meanwhile (tsc's getSignatureApplicabilityError pushes them)
    struct { void const* call; struct Signature const* sig; struct TyMapper const* mapper; } reporting_candidate;
    bool resolving_nested_call; // The next resolve_call() is a call re-entered while in flight (no barrier)
    u32 resolving_call_start; // resolving_start outside the call being resolved: what its return type is resolved with
    bool creating_from_type_node; // ty_reference_with_this() marks what it makes from_type_node (see lower.c)
    bool circular_bindings; // Whether a KEY_CIRCULAR_BINDING was recorded (it's rare: most checkers skip the lookup)
    u32 instantiation_limit_hits; // How many times ty_instantiate() gave up (TS2589): whether a computation was cut short
    // While explaining a relation to a synthetic type that stands for a named one (the base static side without
    // signatures, tsc's getTypeWithoutSignatures keeps the symbol): messages print `display_as` for `display_of`
    Ty const* display_of;
    Ty const* display_as;
    u32 relation_complexity_hits; // How many relations ran out of their budget (TS2859, counted in the above too)
    u32 relation_depth_hits; // How many relations ran out of stack (TS2321, counted in the above too)
    // The typecheck walk inside the true branches of conditional types: `A` is `A & X` in `A extends X ? A['k'] : ...`
    // where indexed access types are checked (tsc's substitution types, see ty_check_indexed_access_type())
    RelationCache relations[TY_RELATION_COUNT];
    struct RelationState* relating; // The relation check in progress, if any (relate.c)
    // The function whose return type is being inferred from its return statements (function.c), and whether a return
    // of a bare call to it was left out (tsc's checkAndAggregateReturnExpressionTypes, hasReturnOfTypeNever)
    FunctionImpl const* return_inference_of;
    struct EnumCase const* enum_evaluating; // The enum member whose initializer is being evaluated (ty_resolve_enum())
    // The typecheck walk (expr.c's check_const_enum_access): the expression where a const enum object may be, and
    // whether it is inside a type
    struct Expr const* const_enum_object_ok;
    u32 type_only_site_depth;
    bool return_inference_skipped;
    u32 conditional_constraint_depth; // Conditional types whose constraint base_constraint() computes (tsc's conditionalConstraintDepth)
    // Typechecking a range of unreachable code (TS7027): nothing in it says so again, except in function expressions,
    // which tsc checks later (tsc's withinUnreachableCode)
    bool within_unreachable;
    Ty const* return_context; // The contextual type of `return` expressions in the function being looked at, or null
    // The generic call whose arguments are being checked for inference (call.c: OuterInference), or null
    struct OuterInference* outer_inference;
    // Every generic call inferring now, innermost first (OuterInference.enclosing), and the serial the last one got:
    // an object literal remembers which one its contextual type came from (KEY_LITERAL_INFERENCE)
    struct OuterInference* active_inferences;
    u64 inference_serial;
    u32 inferential_depth; // Arguments are being checked to infer type arguments from (tsc's CheckMode.Inferential)
    // The return type of a function argument is being computed while its call infers: a generic function among its
    // return expressions is instantiated in context (tsc's instantiateTypeWithSingleGenericCallSignature, see expr.c)
    u32 argument_return_depth;
    struct CallExpr const* resolving_call; // The innermost call whose overloads are being resolved, if any
    // Control flow (flow.c). While flow_uncached is set expressions are evaluated without reading unfinished memos or
    // writing any: what they evaluate to may depend on the incomplete types of a loop, see ty_flow_expr_type()
    u32 flow_uncached;
    // An object literal getter whose type a call's resolution asks for first: its `this` is any (ty_contextual_this_type(),
    // see ty_object_literal_accessor_type())
    struct FunctionImpl const* lazy_getter;
    bool lazy_getter_nested; // Its literal is inside the argument: no contextual type at all, `this` is the literal's type
    // The binding patterns whose implied types are being computed as contextual types (tsc's
    // contextualBindingPatterns): a reference inside one to its own elements is `any` (`const [a, b = a] = [1]`)
    struct Binding const* contextual_patterns[16];
    u32 contextual_pattern_count;
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
    // A query ran out of flow depth (TS2563): no more control flow analysis in the function or module body being
    // checked (tsc's flowAnalysisDisabled, saved and restored around function and module blocks; per file here)
    bool flow_analysis_disabled;
    u32 flow_shared_cap;
    u32 generic_relation_keys; // How many generic_relation_key()s were made (relate.c)
    u32 variance_depth; // Variances are being measured, which is only reliable while the marker types are compared
    // The declared types and aliases whose variances are being measured, outermost first (tsgo's varianceStack): a
    // circularity restarts from its smallest symbol (relate.c, measured_variances())
    struct { struct Ty const* declared; struct Symbol const* alias; void* variances; } variance_stack[64];
    u32 variance_stack_len;
    unsigned char variance_reports; // VARIANCE_UNRELIABLE/UNMEASURABLE of what the measurement went through (relate.c)
    struct IterationState const* iteration; // The lib's iteration interfaces, looked up on first use (iterate.c)
    // `Disposable | null | undefined` and `AsyncDisposable | Disposable | null | undefined`, what a `using` / `await
    // using` initializer must be (ty_check_using_initializer()), made on first use. Null: not yet
    Ty const* disposable_targets[2];
    bool disposable_looked_up[2];
    bool awaited_reported; // TS2318 for a missing Awaited (tsc's getGlobalAwaitedSymbol(/*reportErrors*/ true))
    bool iterable_iterator_reported[2]; // TS2318 for a missing IterableIterator / AsyncIterableIterator (sync, async)
    bool iterable_reported; // TS2318 for a missing Iterable (an empty or rest-only array pattern's type)
};

// checker.c
Checker* checker_new(Program* program);
void checker_drop(Checker* me);
// What the checker holds, in bytes: its arena and its tables (the intern and link tables, the relation caches, the
// type directory). Not the LazyMemo pages, whose number depends on how the parser threads numbered the slots: this is
// what program_typecheck() recycles a checker by, which must depend only on what the checker did
u64 checker_memory(Checker const* me);
// Checks something after the statements of the file being checked (Checker.defer_checks)
void checker_defer(Checker* me, DeferredCheckKind kind, void const* node);
// A function expression's body is checked after the statements of the file being checked, from the first time the
// function is checked on (tsc's checkNodeDeferred in checkFunctionExpressionOrObjectLiteralMethod): where the walk
// meets it, or earlier where its type is checked (the return type of the function around it). Once
void checker_defer_function_body(Checker* me, struct FunctionImpl const* function);

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
// True the first time this checker asks about the node: for checks that must run once per node (a class reached by
// the statement walk and by the expression walk)
bool ty_first_check(Checker* me, void const* node);
// `const { name } = object` reads the member (check.c)
// A property a destructuring pattern reads (`key` a literal type, else nothing is checked): marked referenced, and
// TS2341 at span when it is private outside its class. `from_this`: an assignment from `this`, which doesn't read the
// method it is in
typedef struct AccessSite AccessSite; // expr.h
void ty_check_destructured_member(
    Checker* me, Ty const* object, Ty const* key, Span span, bool from_this, AccessSite const* site, ErrorCtx* errs
);
Atom const* checker_atom(Checker* me, csview sv);
Atom const* ty_index_atom(Checker* me, u32 index); // "0", "1"... (an index's property name)
static inline Ty const* ty_intrinsic(Checker const* me, TyIntrinsic i) { return my intrinsics[i]; }

// Intern key building. Keys are small, build them on the stack: u64 key[1 + n] = { n, ... }
void const** intern_slot(Checker* me, InternTable* table, u64 const* words, u32 count);

Ty* ty_alloc(Checker* me, TyKind kind, u32 flags);
TyList ty_list_copy(Checker* me, Ty const* const* data, u32 len);

Ty const* ty_unresolved(Checker* me, csview name, TyList args);
// A type node printed like tsc's printer reprints it (as the types baseline reuses annotations)
cstr ty_type_node_to_cstr(Checker* me, TypeNode const* node);
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
static inline bool ty_is_fresh_object(Ty const* ty) {
    return ty->kind == TY_ANONYMOUS && (ty->flags & TYF_FRESH_OBJECT) && !ty->anonymous.regular;
}
// tsc's isObjectLiteralType on a type that isn't widened yet: a fresh or regular object literal (not what widening
// makes: tsc's getWidenedTypeOfObjectLiteral drops ObjectFlags.ObjectLiteral)
static inline bool ty_is_unwidened_object_literal(Ty const* ty) {
    return ty->kind == TY_ANONYMOUS && (ty->flags & TYF_FRESH_OBJECT) && ty->anonymous.object_literal;
}
// ... with nothing spread into it (tsc's isObjectLiteralType && !ObjectFlags.ContainsSpread)
static inline bool ty_is_object_literal_without_spread(Checker* me, Ty const* ty) {
    (void)me;
    return ty_is_unwidened_object_literal(ty) && !ty->anonymous.contains_spread;
}
// tsc's getRegularTypeOfObjectLiteral: a fresh object literal (and those in its properties) no longer fresh, but still an
// object literal that widens (ty_is_unwidened_object_literal()). Anything else is itself
Ty const* ty_regular_object_literal(Checker* me, Ty const* ty);
bool ty_is_empty_object_type(Checker* me, Ty const* ty); // tsc's isEmptyObjectType
// The freshness part of tsc's getWidenedType: fresh object literals (also inside unions, arrays and tuples, and in the
// properties of the regular twin) become regular. What declarations, inferred return types and inferences hold
Ty const* ty_widened_objects(Checker* me, Ty const* ty);
// tsc's getWidenedType for null and undefined without strictNullChecks: any, also inside the elements of arrays and
// tuples, the properties of object literals and the members of unions. Only the widening ones (TY_NULL_WIDENING,
// TY_UNDEFINED_WIDENING: what TYX_CONTAINS_WIDENING marks) widen, a written `undefined` type stays
Ty const* ty_widened_nullable(Checker* me, Ty const* ty);
Ty const* ty_unique_symbol(Checker* me, UniqueSymbolDecl const* decl); // `symbol` if decl->key is null
Ty const* ty_widened_unique_symbol(Checker* me, Ty const* ty); // A unique symbol at a mutable location is `symbol`
// The type of a property name as a key: the unique symbol a `[sym]` member is named by (null if it isn't one), else
// the string literal
Ty const* ty_unique_symbol_of_name(Checker* me, Atom const* name);
Ty const* ty_prop_name_literal(Checker* me, Atom const* name);
Ty const* ty_prop_key_literal(Checker* me, Prop const* prop); // What keyof has for it: 1 for `{ 1: x }`
// Whether keyof and mapped types see it: not a #private name, not a private or protected member (tsc's
// getLiteralTypeFromProperty)
bool ty_prop_is_key(Checker* me, Prop const* prop);
// `#x` of a class: a name of its own per class (tsc's getSymbolNameForPrivateIdentifier, `__#12@#x`). It's an atom with
// the text `#x` that isn't the interned one, so members are told apart by pointer like any name, and print as `#x`
Atom const* ty_private_name(Checker* me, Symbol const* class_symbol, csview text);
// The class a private name belongs to, null for any other name
Symbol const* ty_private_name_class(Checker* me, Atom const* name);
Ty const* ty_union(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args);
// The union of some of the members of the union `of`, in their order (a filter): no reductions to do
Ty const* ty_union_unreduced(Checker* me, Ty const* const* types, u32 len);
Ty const* ty_union_filtered(Checker* me, Ty const* of, Ty const* const* kept, u32 len);
Ty const* ty_intersection(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args);
// tsc's getReducedType (checker.c): the common answers inline
enum { REDUCED_NOT_STARTED, REDUCED_IN_FLIGHT, REDUCED_ITSELF, REDUCED_NEVER };
Ty const* ty_reduced_slow(Checker* me, Ty const* ty);
static inline Ty const* ty_reduced(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) {
        if (!ty->composite.state || ty->composite.state->reduced_state == REDUCED_ITSELF) return ty;
    } else if (ty->kind != TY_UNION || !(ty->xflags & TYX_CONTAINS_INTERSECTIONS)) {
        return ty;
    }
    return ty_reduced_slow(me, ty);
}
Ty const* ty_class_never_base(Checker* me, struct ClassDecl const* class_decl);
bool ty_never_intersection_elaboration(Checker* me, Ty const* ty, unsigned* ts_code, cstr* message);
// The same without dropping a `{}` next to a primitive: a written `string & {}` (tsc's NoSupertypeReduction)
Ty const* ty_intersection_keeping_empty_object(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args);
// tsc's getIntersectionType with NoConstraintReduction: `T & P` stays when T's constraint is a subtype of P
Ty const* ty_intersection_no_constraint_reduction(Checker* me, Ty const* const* types, u32 len);
Ty const* ty_intersection_unaliased(Checker* me, Ty const* ty);
Ty const* ty_type_param(Checker* me, Symbol const* decl);
Ty const* ty_distributed_type_param(Checker* me, Ty const* param); // plan 57
Ty const* ty_synthetic_type_param(Checker* me, csview name);
// Variance annotations (relate.c)
typedef enum VarianceModifiers {
    VARIANCE_MODIFIER_IN = 1 << 0,
    VARIANCE_MODIFIER_OUT = 1 << 1,
} VarianceModifiers;
/// tsc's getTypeParameterModifiers: the `in`/`out` of the type parameter at `index` (among its own, not the outer ones)
/// of the class, interface or type alias, on all its declarations (merged interfaces unite them)
unsigned ty_variance_modifiers(Symbol const* owner, u32 index);
/// tsc's createMarkerType for checkTypeParameterDeferred: the class, interface or type alias with its own type parameter
/// at `index` replaced by `sub-T` (`sub`) or `super-T`, named after it (`sub-T` extends `super-T`). Relations compare
/// these structurally, never by variances. Null if there's no such parameter
Ty const* ty_variance_check_instance(Checker* me, Symbol const* owner, u32 index, bool sub);
/// tsc's checkTypeParameterDeferred for the type parameters of a class, interface or type alias: variance annotations
/// on an alias that isn't an object type (TS2637), and that disagree with how the type uses the parameter (TS2636)
void ty_check_variance_annotations(Checker* me, Symbol const* owner, Generic const* generic, bool is_alias, ErrorCtx* errs);
// A fresh type parameter like `param` whose constraint and default are instantiated with `mapper` (set later if null),
// named `name` (null: the same name)
Ty* ty_clone_type_param(Checker* me, Ty const* param, TyMapper const* mapper, Atom const* name);
// Whether a type parameter says anything about itself (a constraint or a default): only those need cloning
bool ty_param_is_bounded(Ty const* param);
// tsc's compareSymbols minus symbol ids: by first declaration (file order, position), then name; 0 a tie
int ty_compare_symbols(Symbol const* a, Symbol const* b);
int ty_compare_declarations(Span a, csview a_name, Span b, csview b_name);
bool ty_is_const_type_variable(Checker* me, Ty const* ty, u32 depth);
// tsc's isValidConstAssertionArgument, as far as a const context changes what these check to (expr.c)
bool ty_is_const_context_kind(struct Expr const* expr);
bool ty_some_mutable_array_like(Checker* me, Ty const* ty);
Ty const* ty_this_type_param(Checker* me, Ty const* declared); // Only make_declared() should call this
Ty const* ty_reference(Checker* me, Ty const* target, TyList args);
Ty const* ty_reference_with_this(Checker* me, Ty const* target, TyList args, Ty const* this_arg);
// tsc's getTypeWithThisArgument: the type with `this` in its members being `this_arg`. The type itself if it has no
// `this` type (or `this_arg` is the type itself)
Ty const* ty_with_this_arg(Checker* me, Ty const* ty, Ty const* this_arg);
Ty const* ty_array(Checker* me, Ty const* element, bool readonly);
// tsc's autoArrayType: `any[]` of TY_AUTO (prints as `any[]`), the declared type of `let x = []`, which control flow
// evolves (flow.c's TY_EVOLVING_ARRAY)
Ty const* ty_auto_array(Checker* me);
Ty const* ty_readonly_array_target(Checker* me); // checker.c
// tsc's isEvolvingArrayOperationTarget: `x` of `x.length`, `x.push(v)`, `x.unshift(v)`, `x[i] = v` with a number-like
// i (IdentifierExpr.evolving_op). There `let x = []` is the auto array, not what it evolved to (flow.c)
bool ty_is_evolving_array_operation_target(Checker* me, struct Expr const* reference);
// A `let x;` that nothing ever definitely assigns (tsc's isNeverInitialized)
bool ty_symbol_never_initialized(Symbol const* symbol);
Ty const* ty_tuple_rest_slice(Checker* me, Ty const* source, u32 start);
struct ImportAttributes;
void ty_check_import_attributes(Checker* me, struct ImportAttributes const* attributes, ErrorCtx* errs);
// `import(x)`: x is a string (TS7036)
void ty_check_import_call_specifier(Checker* me, Expr const* specifier, ErrorCtx* errs);
// What `import("m", options)` imports (`options`' type may say which pattern ambient module), call.c
Symbol* ty_dynamic_import_module(Checker* me, CallExpr const* call, ModuleRef const* ref);
void ty_check_module_attributes(Checker* me, TypeNode const* attributes, Span span, ErrorCtx* errs);
void ty_check_import_call_options(Checker* me, Expr const* options, ErrorCtx* errs);
// tsgo's getGlobalAbstractModuleSourceType: the type of a source phase import (`{}` and TS2318 without the lib)
Ty const* ty_abstract_module_source_type(Checker* me);
Ty const* ty_tuple_target(Checker* me, TupleShape const* shape);
// The shape of a tuple type, the empty tuple (its own target) included. Null if it isn't one
TupleShape const* ty_tuple_shape(Ty const* ty);
// tsc's getTupleElementLabel of an unlabeled element of a rest parameter's tuple (`args_0`), ty_display.c
Atom const* ty_rest_element_label(Checker* me, SigParam const* rest, u32 index, unsigned char flags);
Ty const* ty_keyof(Checker* me, Ty const* operand);
// A mapped type's declaration (key, constraint, modifiers type) without expanding its members
struct MappedInfo const* ty_mapped_info_unexpanded(Checker* me, Ty const* ty);
struct MappedInfo const* ty_generic_mapped(Checker* me, Ty const* ty);
Ty const* ty_keyof_without_index_signatures(Checker* me, Ty const* operand);
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
// The same where a relation is in progress: its comparison of the placeholders (tsc's compareTypes)
typedef struct TyCompare {
    bool (*fn)(Checker* me, void* ctx, Ty const* source, Ty const* target);
    void* ctx;
} TyCompare;
bool ty_template_matches_with(Checker* me, Ty const* source, Ty const* target, TyCompare const* compare);
bool ty_string_mapping_contains(Checker* me, Ty const* source, Ty const* target);
bool ty_templates_definitely_unrelated(Ty const* source, Ty const* target);
// tsc's inferTypesFromTemplateLiteralType: what each placeholder of `target` matched in `source`, false if it doesn't
// match. `out` has room for target's types
bool ty_infer_from_template(Checker* me, Ty const* source, Ty const* target, Ty const** out);
Ty const* ty_template_literal_for_constraint(Checker* me, Ty const* source, Ty const* constraint);
Ty const* ty_indexed(Checker* me, Ty const* object, Ty const* index);
Ty const* ty_indexed_or_null(Checker* me, Ty const* object, Ty const* index, bool include_undefined); // Null if nothing's there
Ty const* ty_indexed_written(Checker* me, Ty const* object, Ty const* index); // An IndexedAccessTypeNode's: a missing property is the error type
Ty const* ty_indexed_instantiated(Checker* me, Ty const* object, Ty const* index, bool include_undefined);
// ty_indexed() of a read under noUncheckedIndexedAccess (see TY_INDEXED's include_undefined)
Ty const* ty_indexed_include_undefined(Checker* me, Ty const* object, Ty const* index, bool include_undefined);
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
    bool check_tuples, // (Of a root: instances take the root's)
    TyList infers // The `infer` type parameters its extends type declares (ty_conditional_own_infers of the root)
);
Ty const* ty_realias(Checker* me, Ty const* ty, Symbol const* alias, TyList alias_args);
// An alias body's homomorphic mapped instance, its own type by the same name (instantiate.c)
Ty const* ty_alias_body_mapped_instance(Checker* me, Ty const* ty, Symbol const* alias, TyList alias_args);
TyList ty_conditional_own_infers(Checker* me, Ty const* root); // The `infer` type parameters its extends type declares
Ty const* ty_non_nullable(Checker* me, Ty const* ty);
bool ty_only_nullable(Ty const* ty);
// tsc's getTypeFacts(type, IsUndefinedOrNull) as bits: 1 possibly undefined (void too), 2 possibly null. A type
// parameter goes by its constraint
u32 ty_nullable_facts(Checker* me, Ty const* ty);
// tsc's checkNonNullTypeWithReporter for a callee: TS2721-2723 if it may be null or undefined (strictNullChecks), and
// what is called then (the error type if nothing is left)
Ty const* ty_check_non_null_callee(Checker* me, Ty const* callee, Span span, ErrorCtx* errs);
// tsc's checkNonNullType: unknown and (under strictNullChecks) null or undefined are errors. Null if it reported
Ty const* ty_check_non_null_type(Checker* me, Ty const* type, Expr const* expr, ErrorCtx* errs);
// tsc's checkNonNullType at a node that isn't an entity name (a pattern): TS2571, TS2531-2533, and with `check_void`
// checkNonNullNonVoidType's void, possibly undefined. The code reported, 0 if none
u32 ty_check_non_null_non_void_at(Checker* me, Ty const* type, Span span, bool check_void, ErrorCtx* errs);
// tsc's checkAssertionDeferred: `x as T` where x's type and T don't overlap (TS2352)
void ty_check_type_assertion(Checker* me, Ty const* expr_type, Ty const* target, Span span, ErrorCtx* errs);
// tsc's checkDeleteExpression: what `delete` deletes has to be an optional (and writable) property
void ty_check_delete_operand(Checker* me, Expr const* operand, ErrorCtx* errs);
// tsc's getSingleSignature: the one call (or construct) signature of an object type that has none of the other kind.
// `allow_members`: properties and index signatures may be there too
Signature const* ty_single_signature(Checker* me, Ty const* ty, bool construct, bool allow_members);
Signature const* ty_single_signature_non_nullable(Checker* me, Ty const* ty, bool construct, bool allow_members);
Ty const* ty_without_undefined(Checker* me, Ty const* ty); // Removes undefined only (tsc's getTypeWithFacts(NEUndefined))
Ty const* ty_undefined_or_missing(Checker* me); // tsc's undefinedOrMissingType
Ty const* ty_remove_missing(Checker* me, Ty const* ty, bool optional); // tsc's removeMissingType
// tsc's containsMissingType. Relies on TY_MISSING having the lowest id of the undefined-likes: it sorts first in a union
bool ty_contains_missing(Checker* me, Ty const* ty);
bool ty_mentions_any(Checker* me, Ty const* ty, Ty const* const* params, u32 count);
// The type parameters a function expression's type (ty_function_type()) mentions, once its signature is resolved and
// its return type inferred, or an object literal's type once its property types are known; false (unknown) before that
// or when unsure
bool ty_function_type_mentions(Checker* me, Ty const* function_type, TyList* out);
bool ty_signature_has_this(Checker* me, Signature const* sig);
bool ty_exact_optional_mismatch(Checker* me, Ty const* source, Ty const* target);
bool ty_has_exact_optional_unassignable(Checker* me, Ty const* source, Ty const* target);
Ty const* ty_optional_prop_type(Checker* me, Ty const* ty); // tsc's getOptionalType(type, isProperty=true)
Ty const* ty_remove_optional_marker(Checker* me, Ty const* ty); // tsc's removeOptionalTypeMarker: without TY_OPTIONAL
// tsc's getOptionalExpressionType: `object`, the type of an expression, as the access, call or `!` it is the object
// (callee, operand) of sees it: after a `?.` (`is_root`) without null and undefined, else without TY_OPTIONAL (which
// only the rest of an optional chain sees, so no need to ask whether the expression is one). `optional` (may be null):
// whether that took something out, so that what the access or call evaluates to may be undefined
Ty const* ty_optional_expression_type(Checker* me, Ty const* object, bool is_root, bool* optional);
Ty const* ty_cond_branch_type(Checker* me, TyCondBranch* branch);
static inline Ty const* ty_conditional_branch(Checker* me, Ty const* conditional, bool which) {
    return ty_cond_branch_type(me, &conditional->conditional.branches[which]);
}

// members.c
static inline TyObjectState* ty_object_state(Ty const* ty) {
    switch (ty->kind) {
        case TY_DECLARED: return ty->declared.state;
        case TY_REFERENCE: return ty->reference.state;
        case TY_ANONYMOUS: return ty->anonymous.state;
        case TY_INTERSECTION: return ty->composite.state;
        default: return nullptr;
    }
}
// ty_members() of members not resolved yet (and of non-object types)
Members const* ty_members_resolving(Checker* me, Ty const* ty);
// Never null. Empty for non-object types. (Resolved ones inline: drizzle asks 17M times)
static inline Members const* ty_members(Checker* me, Ty const* ty) {
    TyObjectState const* state = ty_object_state(ty);
    if (state && state->members_state == LAZY_STATE_DONE) return state->members;
    return ty_members_resolving(me, ty);
}
bool ty_members_done(Ty const* ty); // Its members are resolved (not in flight: what ty_members() gives is final)
// A generic class or interface (or one with a `this` type) declared more than once (`interface Array<T>` in every lib):
// its type parameters have several declarations, so tsc's isTypeParameterPossiblyReferenced says every node in it may
// reference them, they are free in every anonymous type made there (TypeFlow.owner, lower.c)
bool ty_declared_is_merged(Ty const* declared);
Prop const* ty_get_prop(Checker* me, Ty const* ty, Atom const* name);
// The member of a class's type that a field or method of its declaration declares (Prop.decl's spans: all of its
// symbol's declarations), null for one of its own
struct ClassItem;
Prop const* ty_class_item_prop(Checker* me, Symbol const* class_symbol, struct ClassItem const* item);
// The members' properties an intersection's synthetic property (of a class whose base is an intersection: a mixin)
// is made of, through instantiations. 0 if it isn't one
u32 ty_synthetic_prop_parts(Checker* me, Prop const* prop, Prop const* const** out);
bool ty_is_valid_base_type(Checker* me, Ty const* ty); // members.c, tsc's isValidBaseType
bool ty_class_base_constructors_differ(Checker* me, struct ClassDecl const* class_decl, Ty const* base); // members.c, TS2510
bool ty_is_thenable(Checker* me, Ty const* ty); // call.c, tsc's isThenableType
bool ty_awaits_thenable(Checker* me, Ty const* ty); // call.c: awaiting it meets a thenable that's no promise (TS1320...)
Prop const* ty_get_object_prop(Checker* me, Ty const* ty, Atom const* name); // relate.c, with Function/Object members
/// tsc's getPropertyOfType of any type (an intersection's from its first part that has it), null if none. relate.c
Prop const* ty_property_of_type(Checker* me, Ty const* ty, Atom const* name);
// A namespace's (or module's) type-only export, which isn't among the members of its value but is what the entity name
// `typeof ns.A` names (tsc's getPropertyOfType with includeTypeOnlyMembers). Null if that's not what `name` is
Prop const* ty_namespace_type_only_prop(Checker* me, Ty const* object, Atom const* name); // members.c
// The type whose members a value of `ty` has: `String` for a string, the constraint of a type parameter...
Ty const* ty_apparent_type(Checker* me, Ty const* ty);
// What a generic key type is known to include at least (tsc's getLowerBoundOfKeyType, members.c)
Ty const* ty_lower_bound_of_key_type(Checker* me, Ty const* ty);
Ty const* ty_global_function_type(Checker* me); // Null without a lib
Symbol const* ty_global_value_symbol(Checker* me, csview name); // The global value of that name, null if none (relate.c)
void ty_init_global_types(Checker* me); // tsc's initializeTypeChecker's global types, in its order
Ty const* prop_type(Checker* me, Prop const* prop);
// What writes to the property are checked against when it differs from what reads get (a `set` accessor whose
// parameter type isn't the getter's), else null
Ty const* prop_write_type(Checker* me, Prop const* prop);
bool ty_props_identical(Checker* me, Prop const* source, Prop const* target); // tsc's isPropertyIdenticalTo
// The name of the property's first declaration (instantiations follow their target), invalid if it has none
Span ty_prop_decl_span(Prop const* prop);
bool ty_class_base_constructor_unfit(Checker* me, ClassDecl const* class_decl);
// The base a class or interface without members of its own is equivalent to (tsc's
// getSingleBaseForNonAugmentingSubtype: relations see it instead). Null if there's none
Ty const* ty_non_augmenting_base(Checker* me, Ty const* ty);
// The type variable a mixin class extends (`class C extends Base` with `Base: T`, or `T & U`): its value is `typeof C & T`
// (tsc's getBaseTypeVariableOfClass). Null for any other class
Ty const* ty_class_base_type_variable(Checker* me, Symbol const* class_symbol);
void signature_resolve(Checker* me, Signature const* sig);
Ty const* signature_return_type(Checker* me, Signature const* sig);
bool ty_signature_return_in_flight(Checker* me, Signature const* sig); // Its return type is being inferred up the stack
// The same, but an inferred return type in flight only counts above the barrier of a call being resolved: below it, asking
// infers it again (tsc's isResolvingReturnTypeOfSignature, findResolutionCycleStartIndex)
bool ty_signature_return_resolving(Checker* me, Signature const* sig);
TyPredicate const* signature_predicate(Checker* me, Signature const* sig);
bool signature_declared_never(Signature const* sig);
TyPredicate const* ty_infer_type_predicate(Checker* me, FunctionImpl const* function); // flow.c, null if none
Ty const* ty_tuple_as_array(Checker* me, Ty const* tuple);
// The type of parameter i (a rest parameter stands for every position from its own on), null if there's none
Ty const* ty_signature_param_type_at(Checker* me, Signature const* sig, u32 i);
Ty const* ty_rest_element_type(Checker* me, Ty const* rest); // The element type of a rest parameter's type
// How many parameters a signature has, a rest tuple of fixed length counted as its elements (tsc's getParameterCount)
u32 ty_signature_param_count(Signature const* sig);
// Takes any number of arguments: a rest parameter that isn't a tuple of fixed length (tsc's hasEffectiveRestParameter)
bool ty_signature_has_rest(Signature const* sig);
// The signature a function expression gets its parameter types from, given its contextual type, null if none
Signature const* ty_function_contextual_signature(Checker* me, FunctionImpl const* function, Ty const* contextual);
Ty const* ty_quick_expr_type(Checker* me, Expr const* expr, bool in_flow); // tsc's getQuickTypeOfExpression, null if none // The array a tuple also is
Ty const* ty_optional_param_type(Checker* me, Ty const* type); // infer.c
// What the position of `param` takes: an optional one's type with undefined (tsc's getTypeOfParameter) // infer.c
Ty const* ty_sig_param_type(Checker* me, SigParam const* param, Ty const* type);
Ty const* ty_signature_params_tuple(Checker* me, Signature const* sig, u32 from, TyMapper const* mapper); // infer.c
u32 ty_min_argument_count(Checker* me, Signature const* sig); // relate.c: tsc's getMinArgumentCount, less trailing void
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
Signature const* ty_implementation_signature(Checker* me, FunctionImpl const* function);
bool ty_has_call_signature_for_arity(Checker* me, Ty const* type, u32 required);
void ty_check_overloads_compatible(
    Checker* me, FunctionImpl const* implementation, Span implementation_name, FunctionImpl const* const* overloads,
    Span const* names, u32 count, ErrorCtx* errs
);
// A contextual signature of a union: `base` returning what any of `composites` returns, with `base`'s `this`
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
bool ty_has_prop(Checker* me, Ty const* ty, Atom const* name);
Signature* const* ty_union_signatures(Checker* me, TyList members, bool construct, u32* count_out);
Signature* const* ty_union_signature_lists(Checker* me, Signature* const* const* lists, u32 const* lens, u32 member_count, u32* count_out);
typedef struct CallEffects {
    bool has_predicate;
    bool returns_never;
    TyPredicate predicate; // With the type arguments of the call
} CallEffects;
CallEffects ty_call_effects(Checker* me, Ty const* callee, CallExpr const* call);
TyList ty_reference_args(Checker* me, Ty const* reference);
Ty const* ty_param_constraint(Checker* me, Ty const* param); // null if unconstrained (or circularly constrained)
bool ty_param_constraint_is_circular(Checker* me, Ty const* param); // TS2313
Ty const* ty_declared_param_constraint(Checker* me, Ty const* param); // tsc's getConstraintFromTypeParameter, not gated
bool ty_has_non_circular_base_constraint(Checker* me, Ty const* ty); // relate.c
bool ty_base_constraint_in_flight(Checker* me, Ty const* ty);
// Of an `infer X` without `extends`, from its position (`omit_type_args`: not from a type argument's, tsc's
// omitTypeReferences)
Ty const* ty_implied_infer_constraint(Checker* me, Ty const* param, bool omit_type_args);
Ty const* ty_simplified(Checker* me, Ty const* ty, bool writing);
Ty const* ty_distribute_index_over_object(Checker* me, Ty const* object, Ty const* index); // relate.c
Ty const* ty_generic_mapped_prop_context(Checker* me, Ty const* object, Ty const* key); // tsc's getSimplifiedType, relate.c
Ty const* ty_base_constraint(Checker* me, Ty const* ty); // of a type parameter or deferred type, null if there's none
Ty const* ty_conditional_constraint(Checker* me, Ty const* conditional); // tsc's getConstraintOfConditionalType, relate.c
Ty const* ty_param_default(Checker* me, Ty const* param); // null if none
bool ty_param_has_default(Ty const* param); // Whether it declares a default (without lowering it)

// instantiate.c
TyMapper const* ty_mapper_new(Checker* me, TyList sources, TyList targets);
// (What ty_instantiate() doesn't decide inline: half of its calls have nothing to instantiate, drizzle 9M calls)
Ty const* ty_instantiate_mapped(Checker* me, Ty const* ty, TyMapper const* mapper, bool tail);
Ty const* ty_alias_body_twin_instance(Checker* me, Ty const* twin); // The ordinary instance it stands next to
static inline Ty const* ty_instantiate(Checker* me, Ty const* ty, TyMapper const* mapper) {
    bool tail = my instantiating_tail;
    my instantiating_tail = false;
    if (!mapper || !mapper->len) return ty;
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        if (ty->kind == TY_ANONYMOUS && ty->anonymous.alias_body_twin) return ty_alias_body_twin_instance(me, ty);
        return ty;
    }
    return ty_instantiate_mapped(me, ty, mapper, tail);
}
Ty const* ty_instantiate_tail(Checker* me, Ty const* ty, TyMapper const* mapper); // A conditional type's branch
void ty_active_mappers_drop(Checker* me);
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
// The same with the stack tsc would have when it runs out (see probe_eager_bases())
void ty_instantiation_limit_reached_on(Checker* me, Ty const* const* stack, u32 depth);

// lower.c
Ty const* ty_from_node(Checker* me, TypeNode const* node, LowerCtx ctx);
// The type of a binding (a variable, a parameter, an element of a destructuring pattern): what is written (lowered
// with `ctx`), else what is assigned to it
Ty const* ty_binding_type(Checker* me, Binding const* binding, LowerCtx ctx);
typedef struct ObjectBindingItem ObjectBindingItem;
// The type of an object pattern item's computed name, `{}` while it's being computed (tsc's checkComputedPropertyName with
// its circularConstraintType marker: `let { [a]: a } = o` reads o with a `{}` key)
Ty const* ty_binding_computed_key_type(Checker* me, ObjectBindingItem const* item);
// The computed key the item's element type was read with (null before): what tsc's getTypeForBindingElement reports about
Ty const* ty_binding_access_key(Checker* me, ObjectBindingItem const* item);
Ty const* ty_empty_object_type(Checker* me); // `{}`
// Also `{}`, but only as the non-nullish part of `unknown` while narrowing it (tsc's unknownEmptyObjectType, for TS2638)
Ty const* ty_unknown_empty_object_type(Checker* me);
Ty const* ty_empty_type_literal(Checker* me); // tsc's emptyTypeLiteralType
Ty const* ty_commonjs_module_object(Checker* me, SourceFile const* file); // `module` of a CommonJS file
// `module.exports` of a CommonJS file (what `this` is at its top level)
Ty const* ty_commonjs_exports_type(Checker* me, SourceFile const* file);
// What a context-sensitive function is while context-sensitive arguments are skipped (tsc's anyFunctionType): no
// members, non-inferrable, related to every function type
Ty const* ty_any_function_type(Checker* me);
bool ty_mapped_is_remapping(Checker* me, MappedInfo const* mapped); // tsc's getMappedTypeNameTypeKind == Remapping
// tsc's getTypeFromBindingPattern. include_pattern: as the contextual type of an initializer (TYF_BINDING_PATTERN)
Ty const* ty_binding_pattern_type(Checker* me, Binding const* pattern, bool include_pattern);
bool ty_sig_param_adds_undefined(SigParam const* param);
void ty_report_pattern_iterable(Checker* me, Binding const* pattern);
Ty const* ty_pattern_clone(Checker* me, Ty const* ty); // A clone marked TYF_BINDING_PATTERN
Ty const* ty_pattern_unclone(Checker* me, Ty const* ty); // A clone of such a clone that is no pattern's
// tsc's originatingImport: what `import * as ns` of an `export =` module imports when ns's type is a module type made
// from it (a synthetic default under esModuleInterop), else null
Ty const* ty_namespace_import_original(Checker* me, Symbol const* alias);
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
bool ty_alias_is_circular(Checker* me, Symbol const* symbol);
SymbolLinks* ty_symbol_links(Checker* me, Symbol const* symbol);
Ty const* ty_namespace_type(Checker* me, struct Symbol const* symbol);
Ty const* ty_static_type_of(Checker* me, Symbol const* symbol);
Ty const* ty_type_of_symbol(Checker* me, Symbol const* symbol);
struct ClassDecl;
struct ClassItem;
Ty const* ty_class_field_symbol_type(Checker* me, struct ClassDecl const* class_decl, struct ClassItem const* field);
// `import { x }` of a property of what `export =` names (SYMBOL_DECL_EXPORT_EQUALS_PROPERTY) that it doesn't have
bool ty_export_equals_property_missing(Checker* me, Symbol const* property);
// What an import tsc combines means as a type (SymbolAlias.combined_type): the combined interface when the value has the
// property, else null (the alias's own target)
Symbol const* ty_alias_combined_type(Checker* me, Symbol const* alias);
Ty const* ty_merged_value_type(Checker* me, Symbol const* symbol);
// What `import("m")` promises: the module's value, with a synthetic `default` for an `export =` module
Ty const* ty_dynamic_import_module_type(Checker* me, Symbol* module, struct SourceFile const* importer);
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
typedef struct EnumCase EnumCase;
// TS2565/TS2651 for the enum members an initializer of `member` refers to (tsc's evaluateEnumMember)
void ty_check_enum_initializer_references(Checker* me, EnumCase const* member, Expr const* expr, ErrorCtx* errs);
bool ty_evaluate_string(Checker* me, Expr const* expr, cstr* out); // enums.c, tsc's evaluate() of a constant string
bool ty_evaluate_number(Checker* me, Expr const* expr, double* out); // enums.c, tsc's evaluate() of a constant number
// An enum member's initializer evaluated the way its enum's resolution does, without making the enum's literal types
// (tsc's computeEnumMemberValue: the types come later, getDeclaredTypeOfEnum). 0: not constant, 1: a number, 2: a string
int ty_enum_member_constant(Checker* me, struct EnumCase const* member, double* number);
// Not a string, or written as one (tsc's isSyntacticallyString, TS18055 under isolatedModules), enums.c
bool ty_enum_initializer_is_syntactic_string(Checker* me, Expr const* expr);
Ty const* ty_enum_of_member(Checker* me, Ty const* member); // The enum type an enum literal belongs to
bool ty_is_enum_union(Ty const* ty);

// check.c: tsc's getSuggestedSymbolForNonexistentProperty, the property of `object` spelled most like `name` (null if none)
Prop const* ty_suggested_property(Checker* me, Ty const* object, csview name);
// relate.c
bool ty_is_related(Checker* me, Ty const* source, Ty const* target, TyRelation relation);
// Why source isn't related to target, outermost line first. The first line is the natural head of the error: TS2322
// "Type 'S' is not assignable to type 'T'." unless something more specific replaces it (TS2741 and friends)
// The explaining (reporting) pass of a relation, which can find it holds after all: tsc's checkTypeRelatedTo then
// reports nothing (an invariant alias argument falls back to the structure). Returns whether it related
bool ty_explain_unrelated(Checker* me, Ty const* source, Ty const* target, TyRelation relation, VecTyReportLine* out);
// The same under a head of the caller's (tsc's headMessage), whose code some elaborations depend on
bool ty_explain_unrelated_headed(
    Checker* me, Ty const* source, Ty const* target, TyRelation relation, unsigned head_code, VecTyReportLine* out
);
// The member of a union target a failed relation is most likely about (tsc's getBestMatchingType), null if none
Ty const* ty_best_matching_type(Checker* me, Ty const* source, Ty const* target);
Ty const* ty_widened_literal(Checker* me, Ty const* ty);
// How the source of a failed relation shows in the error: widened unless the target has room for literal types
void ty_report_pair_cstr(Checker* me, Ty const* source, Ty const* target, cstr* source_out, cstr* target_out); // Both, as a message shows them
Ty const* ty_report_target(Checker* me, Ty const* source, Ty const* target); // The target as a relation error's head names it (null and undefined stripped for a non-nullable source: tsc's isRelatedTo)
// Two types as printed, fully qualified if they'd look the same (tsc's getTypeNamesForErrorDisplay)
// tsc's getSingleBaseForNonAugmentingSubtype: the one base of a class or interface that declares no members of its own
// (which relates as that base), instantiated for the reference; null otherwise
Ty const* ty_single_base_for_non_augmenting_subtype(Checker* me, Ty const* ty);
void ty_display_pair_cstr(Checker* me, Ty const* a, Ty const* b, cstr* a_out, cstr* b_out);
void relation_cache_drop(RelationCache* me);
// conditional.c
// Null while the conditional type has to stay deferred. `mapper`: what instantiated the conditional type (the constraints
// of its `infer X extends C` are the root's)
Ty const* ty_conditional_resolve(
    Checker* me, Ty const* check_type, Ty const* extends, TyList infers, TyCondBranch const* if_true,
    TyCondBranch const* if_false, TyMapper const* mapper, Ty const* constraint_root, bool check_tuples
);
// Tuples of the same arity without optional or rest elements (tsc's isSimpleTupleType of both, of one length)
bool ty_conditional_simple_tuples(Ty const* check_type, Ty const* extends);
// Assignable whatever the type parameters in either side turn out to be (tsc's check on their restrictive instantiations)
bool ty_definitely_assignable(Checker* me, Ty const* source, Ty const* target);
// What a deferred conditional type's `infer` type parameters infer from its check type, null if that one is deferred
TyMapper const* ty_conditional_inferred_mapper(Checker* me, Ty const* conditional, TyList infers);
// Not assignable even with every type parameter in either side the wildcard (tsc's check on their permissive
// instantiations). False when unsure
bool ty_definitely_unassignable(Checker* me, Ty const* source, Ty const* target);

// access.c
// The type of `object[index]` (and `object.name`, the index being the name as a literal). Null if there's no such
// property or the index isn't one that can be looked up
Ty const* ty_access(Checker* me, Ty const* object, Ty const* index);
// What ty_access() gives for the property `name` of a single object type that has it, without the name's literal type
// (tsc makes that only for index signatures and mapped types, and when it's made is union order). Null when the object
// isn't such a type (a union, intersection, list...) or lacks it: then ask ty_access() with ty_prop_name_literal()
Ty const* ty_access_own_named_prop(Checker* me, Ty const* object, Atom const* name);
// tsc's getNarrowedTypeOfSymbol: an element of a const (or unassigned parameter) pattern of a union, where control flow
// narrows that union by the pattern's other elements (`const { kind, payload } = x; if (kind === "a") payload`). Null
// if nothing narrows it
Ty const* ty_destructured_narrowed_type(Checker* me, struct Binding const* binding, struct FlowNode const* flow);
// tsc's getNarrowedTypeOfSymbol for a parameter of a context-sensitive function whose contextual signature takes one
// rest parameter of a union of tuples: the union as control flow narrows it by the other parameters. Null if not
Ty const* ty_parameter_narrowed_type(Checker* me, struct Binding const* binding, struct FlowNode const* flow);
// An element of a pattern (no default, not `...`) read from `object` as its pattern's type: the property or element,
// with the flow type of the destructured reference, not widened (tsc's getBindingElementTypeFromParentType). Null (or
// the error type) if it has none
Ty const* ty_binding_element_type_from(Checker* me, struct Binding const* binding, Ty const* object);
// tsc's getRestType: `source` without the properties the keys (literal types, computed names' types) name
Ty const* ty_object_rest_type(Checker* me, Ty const* source, TyList keys);
// The symbol tsc's getRestType gives an object rest binding's type (the binding's), else null
Symbol const* ty_rest_type_symbol(Checker* me, Ty const* type);
Ty const* ty_destructured_assignment_flow_type(
    Checker* me, void const* owner, Expr const* source, Value key, Span key_span, Ty const* declared
);
// ty_access() of an expression that reads: with noUncheckedIndexedAccess, index signatures give undefined too
Ty const* ty_possibly_out_of_bounds(Checker* me, Ty const* element);
Ty const* ty_access_reading(Checker* me, Ty const* object, Ty const* index);

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
Ty const* ty_instantiate_contextual_type_for(Checker* me, Ty const* contextual, bool signature);
// tsc's getContextualThisParameterType: instantiated with the fixing mapper of the call inferring now
// What a ThisType<T> marker says while the generic call `serial` (ty_current_inference_serial(), 0: the one inferring
// now) infers: the type parameters it mentions are fixed
Ty const* ty_instantiate_fixing(Checker* me, Ty const* type, u64 serial);
u64 ty_current_inference_serial(Checker* me); // 0 if no generic call's arguments are being checked
Signature const* ty_fix_contextual_signature(Checker* me, Signature const* sig, struct FunctionImpl const* function);
// A context-sensitive member of an object or array literal was checked while the call inferring now fixes: what it
// is inferred from before the next type parameter is fixed (tsc's addIntraExpressionInferenceSite)
void ty_add_intra_expression_site(Checker* me, Ty const* type, Ty const* contextual);
Ty const* ty_contextual_signature_type_at(Checker* me, Signature const* sig, u32 index, bool as_rest);
// An array literal re-read as a tuple of its elements (tsc's checkArrayLiteral with forceTuple), from the elements'
// memos
Ty const* ty_array_literal_as_tuple(Checker* me, Expr const* expr, Ty const* contextual);
// An object or array literal's type as tsc's forceTuple check mode reads it (nested array literals tuples)
Ty const* ty_literal_forcing_tuples(Checker* me, Expr const* expr, Ty const* contextual);
Ty const* ty_element_index_type(Checker* me, Expr const* index); // `index` of `x[index]`
Ty const* ty_access_key_type(Checker* me, PropertyExpr const* access); // What `x.y`/`x[y]` looks up (`#y`: the class's)
Ty const* ty_compute_expr_without_context(Checker* me, Expr const* expr); // Only for the LazyType of an Expr
Ty const* ty_rest_binding_initializer_type(Checker* me, Expr const* expr);
Signature const* ty_call_resolved_signature(Checker* me, CallExpr const* call);
void ty_check_assertion_call_statement(Checker* me, Expr const* expr, ErrorCtx* errs);
// The contextual type of the value of `a = v` (null for none): what its target gives it (tsc's
// getContextualTypeForBinaryOperand and getContextualTypeForAssignmentDeclaration)
Ty const* ty_assigned_value_context(Checker* me, BinaryOpExpr const* assignment);
// Every reference to a variable, parameter, function, class... gets its type here (plan 03: narrowing goes here)
Ty const* ty_identifier_reference_type(Checker* me, IdentifierExpr const* identifier);
// What a declaration without type annotation makes of its initializer's type (tsc's getWidenedType, plus
// getWidenedLiteralType if `literals`: mutable locations)
Ty const* ty_widened_for_declaration(Checker* me, Ty const* ty, bool literals);
// tsc's isUnitType: one literal, enum member, unique symbol, null or undefined (not a union of them)
bool ty_is_unit(Ty const* ty);
// An inferred return type: literal types widen unless the contextual return type has room for them
Ty const* ty_widened_return_type(Checker* me, Ty const* ty, Ty const* contextual);
Ty const* ty_union_subtype_reduced(Checker* me, Ty const* const* types, u32 len);
// The type an unannotated parameter got from the contextual signature of its function expression, null if none
Ty const* ty_contextual_parameter_type(Checker* me, InitializedBinding const* parameter);
Ty const* ty_contextual_parameter_type_unwidened(Checker* me, InitializedBinding const* parameter); // Before a wider initializer replaced it
bool ty_contextual_parameter_is_pattern(Checker* me, Binding const* pattern);
// A JavaScript function's JSDoc `@type {F}` (FunctionImpl.jsdoc_signature_type) as its contextual signature, once
void ty_function_apply_jsdoc_type(Checker* me, FunctionImpl const* function, InitializedBinding const* evaluating);
// The contextual type of a property or an element of something whose contextual type is `contextual` (null: none)
Ty const* ty_contextual_prop_type(Checker* me, Ty const* contextual, Ty const* key);
Ty const* ty_contextual_element_type(Checker* me, Ty const* contextual, u32 index);
// The same for the element at `index` of `length` elements without spreads: the elements after a variadic one count from
// the end (tsc's getContextualTypeForElementExpression with a length)
Ty const* ty_contextual_element_type_of(Checker* me, Ty const* contextual, u32 index, u32 length);
Ty const* ty_contextual_return_type(Checker* me, FunctionImpl const* function); // null if none
// What the contextual signature of a function is to itself: its own (tsc's getReturnTypeFromBody keeps its unit return
// type), or a composite with its own while that is inferred (no contextual return type, but TS7024's circularity)
typedef enum ContextSelf { CONTEXT_OTHER, CONTEXT_OWN, CONTEXT_COMPOSITE_OWN } ContextSelf;
Ty const* ty_contextual_return_type_self(Checker* me, FunctionImpl const* function, ContextSelf* self);
// What its return expressions are contextually typed by: the contextual return type, or an IIFE's call's contextual type
Ty const* ty_return_expressions_context(Checker* me, FunctionImpl const* function);
// `import.meta` (the property access on the `import` keyword) and its type
bool ty_is_import_meta(struct Expr const* expr);
Ty const* ty_import_meta_type(Checker* me);
Ty const* ty_import_meta_expr_type(Checker* me, Expr const* expr);
Ty const* ty_contextual_this_type(Checker* me, FunctionImpl const* function); // null if nothing says what `this` is
// The contextual signature a function expression's parameters were typed by, if any (after it was checked)
Signature const* ty_function_assigned_contextual_signature(Checker* me, FunctionImpl const* function);
bool ty_function_closed(Checker* me, FunctionImpl const* function); // function.c: instantiating its type is the identity
// The type of an object literal accessor property: the getter's widened return type, else the setter's parameter
// TS2353 for a literal whose contextual type is an object binding pattern's implied type
void ty_check_object_literal_pattern(Checker* me, Expr const* expr, ErrorCtx* errs);
Ty const* ty_object_literal_accessor_type(Checker* me, FunctionImpl const* accessor, bool getter);
bool ty_expr_is_context_sensitive(Expr const* expr);
bool ty_function_is_context_sensitive(FunctionImpl const* function);
// The type of the property an item of an object literal made (what the `.types` baseline shows at its name)
Ty const* ty_object_literal_member_type(Checker* me, Expr const* object, ObjectExprItem const* item);
/// TS2300 for the late-bound members of an object literal that conflict (tsgo's lateBindMember among themselves, its
/// mergeSymbol with the early member of their name)
void ty_check_object_literal_late_members(Checker* me, Expr const* object, ErrorCtx* errs);
/// The declarations of an object literal member's symbol, as tsgo's binder and late binding make it (the early
/// members of a name first, then the late-bound ones that merge into it), *first its first member. Only the member
/// itself when it is alone, empty for a spread
VecSpan ty_object_literal_member_symbol(Checker* me, Expr const* object, ObjectExprItem const* item, ObjectExprItem const** first);
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
Ty const* ty_jsx_fragment_type(Checker* me, struct JsxElement const* element);
JsxIntrinsic ty_jsx_intrinsic_named(Checker* me, struct JsxElement const* element, csview name);
void ty_check_jsx_element(Checker* me, Expr const* expr, ErrorCtx* errs);
bool ty_is_js_literal_type(Checker* me, Ty const* type);
int ty_js_assignment_implicit_any(Checker* me, Symbol const* member, Expr const* value);
bool ty_is_empty_array_literal_type(Checker* me, Ty const* type); // lower.c
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
Ty const* ty_mapped_homomorphic_variable(Checker* me, Ty const* ty);
// The same with the mapper from its signatures' type parameters built (the type itself if it's about none of them)
#define MAX_TYPE_ARGS 32
TyList ty_alias_args_filled(Checker* me, Ty const* ty, Ty const* buf[MAX_TYPE_ARGS]);
Ty const* ty_apply_type_arguments_mapper(Checker* me, Ty const* ty, TyMapper const* mapper);
Ty const* ty_apply_type_arguments_mapper_of(Checker* me, Ty const* ty, TyMapper const* mapper, Ty const* query);
bool ty_instantiation_keeps_signature(Checker* me, Signature const* sig, TyMapper const* mapper);

typedef struct ObjectLiteralProp {
    Atom const* name;
    u32 flags; // PropFlags
    Ty const* type; // Null for an accessor (PROP_GETTER/PROP_SETTER): `accessor` has it
    FunctionImpl const* accessor;
    FunctionImpl const* setter; // See PropDecl.setter
    // The methods of the name in an object literal, in order, while every declaration of it is a method (one binder
    // symbol to tsc, whose type has the signatures of them all)
    FunctionImpl const* const* methods;
    u32 method_count;
    Span name_span;
    Span const* decl_spans;
    u32 decl_span_count;
    bool name_is_value;
    TypeNode const* asserted; // See PropDecl.asserted
    struct ObjectExprItem const* item; // See PropDecl.item
    // Made optional by a partial spread: the annotation of a required property, which says the same again once an
    // existing property takes this one's type without undefined (spread_into())
    TypeNode const* asserted_required;
    bool own; // Written in the literal itself, not spread into it: only those get excess property checks
    bool own_merged; // Merged by a spread with one written in the literal itself (whose declaration it keeps)
    uint8_t name_form; // See PropDecl.name_form
    Ty const* name_type; // See PropDecl.name_type
    Prop const* reuse; // The property is this one as it is (tsc: the same symbol), the other fields are ignored
    // A spread or widened copy of a declared member that tsc's getSpreadSymbol / getWidenedProperty would keep as the
    // same symbol: that member's container (Prop.parent), which names it in the symbols baseline
    Symbol const* parent;
    Symbol const* spread_variable; // See PropDeclRare.spread_variable
    Symbol const* spread_namespace;
    bool spread_fresh; // See PropDeclRare.spread_fresh
} ObjectLiteralProp;
typedef enum ObjectLiteralKind {
    OBJECT_SYNTHETIC, // Made up by the checker
    OBJECT_LITERAL, // The type of an object literal expression (tsc's ObjectFlags.ObjectLiteral), widened
    OBJECT_LITERAL_FRESH, // ... as the expression evaluates to it (TYF_FRESH_OBJECT)
    // ... no longer fresh but not widened yet either (tsc's getRegularTypeOfObjectLiteral): TYF_FRESH_OBJECT +
    // anonymous.regular. Still an object literal to relations (ty_is_unwidened_object_literal())
    OBJECT_LITERAL_REGULAR,
    OBJECT_JSX_ATTRIBUTES, // The attributes of a JSX element (tsc's ObjectFlags.JsxAttributes)
    OBJECT_JSX_ATTRIBUTES_FRESH,
    // Or'ed with the kind: an object literal of a JavaScript file, open-ended without noImplicitAny (tsc's
    // ObjectFlags.JSLiteral, isJSLiteralType)
    OBJECT_JS = 1 << 4,
    // Or'ed with OBJECT_SYNTHETIC: a type tsc makes without a symbol (createEmptyObjectTypeFromStringLiteral, binding patterns), which
    // has no inferable index (isObjectTypeWithInferableIndex)
    OBJECT_NO_INFERABLE_INDEX = 1 << 5,
    // Or'ed with an object literal kind: something was spread into it (tsc's ObjectFlags.ContainsSpread, what its
    // getSpreadType makes: `{ ...{} }` too, not `{ ...0 }`), kept by its regular version, not by its widened one
    OBJECT_CONTAINS_SPREAD = 1 << 6,
} ObjectLiteralKind;
Ty const* ty_object_literal(
    Checker* me, ObjectLiteralProp const* props, u32 count, IndexInfo const* index, u32 index_count, ObjectLiteralKind kind
);
Ty const* ty_object_literal_of(
    Checker* me, ObjectLiteralProp const* props, u32 count, IndexInfo const* index, u32 index_count, ObjectLiteralKind kind,
    void const* expr
);
// The same as one of several alternatives a spread of a union makes (1 on): each is a type of its own however equal,
// like tsc's getSpreadType makes a new anonymous type per member
Ty const* ty_object_literal_alternative(
    Checker* me, ObjectLiteralProp const* props, u32 count, IndexInfo const* index, u32 index_count, ObjectLiteralKind kind,
    void const* expr, u32 alternative
);

// function.c
Ty const* ty_function_type(Checker* me, FunctionImpl const* function);
// The return type of a function: its annotation, else inferred from the `return`s of its body
bool ty_is_recursive_return(Checker* me, Expr const* returned); // `return f()` in f: not part of f's return type
Ty const* ty_jsdoc_tag_return_type(Checker* me, FunctionImpl const* function); // members.c
// The parameter types an immediately invoked function gets from its call's arguments, for whoever types the function
// before the call is checked (tsc pulls them: getContextuallyTypedParameterType; tttt pushes them from the call)
void ty_assign_iife_parameter_types(Checker* me, CallExpr const* call);
u32 ty_iife_effective_arg_count(Checker* me, FunctionImpl const* function); // expr.c
bool ty_function_return_circular(Checker* me, FunctionImpl const* function); // TS7023/TS7024
Ty const* ty_function_return_type(Checker* me, FunctionImpl const* function);
// Without strictNullChecks: the inferred return type was null or undefined, widened to any (TS7010/TS7011)
// The inferred return type before widening if widening makes null or undefined in it any (without strictNullChecks), else
// null (tsc's reportErrorsFromWidening with WideningKind.FunctionReturn)
Ty const* ty_function_return_before_widening(Checker* me, FunctionImpl const* function);
// A pattern element's type before widening, if its null/undefined widened to any (after its type was asked for)
Ty const* ty_element_before_widening(Checker* me, struct Binding const* element);
// The return type of a function expression that is an argument of the generic call inferring now: the calls it returns
// see what that call has inferred so far (tsc's getInferenceContext finds the call through the argument)
Ty const* ty_argument_return_type(Checker* me, FunctionImpl const* function);
// An immediately invoked function's return type, computed while the call inferring now (if any) stays visible
Ty const* ty_iife_return_type(Checker* me, FunctionImpl const* function);
// A call is inferring its type arguments from the argument being checked now
bool ty_outer_inference_active(Checker* me);
bool ty_function_return_in_flight(Checker* me, FunctionImpl const* function);
bool ty_function_free_params(Checker* me, FunctionImpl const* function, TyList* out);
Ty const* ty_function_return_known(Checker* me, FunctionImpl const* function); // null if not inferred yet

// tsc's pushTypeResolution/popTypeResolution: one stack of what is being resolved (a declaration's type, an inferred
// return type, a property's type, an alias). When one of them is asked for again while in flight, it and everything
// pushed after it is part of the cycle: each finds out when it pops and becomes circular (TS7022, TS7023, TS2502...)
typedef enum ResolvingKind {
    RESOLVING_DECLARATION, // A LazyType of a declaration
    RESOLVING_FUNCTION_RETURN,
    RESOLVING_PROP,
    RESOLVING_ALIAS,
    RESOLVING_BINDING, // An annotated Binding, or an identifier in a pattern (see ty_binding_type())
    RESOLVING_PATTERN, // A LazyType of a root binding pattern (see lazy_type.c)
    RESOLVING_SIGNATURE_RETURN,
    RESOLVING_BASES, // The bases of a declared class or interface type
    RESOLVING_BASE_CONSTRAINT, // A type whose base constraint is computed (relate.c base_constraint())
    RESOLVING_BASE_CONSTRUCTOR, // A ClassDecl whose `extends` expression is evaluated (members.c class_base())
    // A mapped type's constraint being instantiated (a MappedInfo): tsc resolves it under its key parameter's
    // pushTypeResolution(ResolvedBaseConstraint) (getConstraintTypeFromMappedType). Nothing looks for it, it is only a
    // guard for members_reentered_unguarded()
    RESOLVING_MAPPED_CONSTRAINT,
} ResolvingKind;
typedef struct MembersInFlight {
    Ty const* ty;
    u32 resolving_len;
    u32 instantiation_depth;
} MembersInFlight;
typedef struct Resolving {
    void const* key;
    u32 kind;
    bool circular;
} Resolving;
void ty_resolving_grow(Checker* me);
static inline void ty_resolving_push(Checker* me, ResolvingKind kind, void const* key) {
    if (my resolving_len == my resolving_cap) ty_resolving_grow(me);
    my resolving[my resolving_len++] = (Resolving){ .key = key, .kind = kind };
}
// Whether it turned out circular
static inline bool ty_resolving_pop(Checker* me) {
    return my resolving[--my resolving_len].circular;
}
void ty_resolving_cycle(Checker* me, ResolvingKind kind, void const* key); // It was asked for while in flight
// The same, also below the barrier of a call resolution (resolving_start). tsc re-enters what is in flight below its
// resolutionStart: the second round resolves the same things again, now above the barrier (the calls on the way are
// already resolvingSignature, which doesn't move it), and finds the cycle there. For the bases of a class or interface,
// where that second round's TS2310/TS2506 are what's left of it
void ty_resolving_cycle_across_calls(Checker* me, ResolvingKind kind, void const* key);
// A root binding pattern's LazyType (`key`) was asked for while in flight: the cycle starts at the first of its names
// resolved since, if any was
void ty_resolving_pattern_cycle(Checker* me, void const* key, struct Binding const* pattern);
// Whether it is being resolved above the barrier (for what has no in-flight state of its own, tsc's
// findResolutionCycleStartIndex: below a barrier it is resolved again, what it reaches has in-flight states that stop it)
static inline bool ty_resolving_find(Checker* me, ResolvingKind kind, void const* key) {
    for (u32 i = my resolving_len; i-- > my resolving_start;) {
        if (my resolving[i].key == key && my resolving[i].kind == kind) return true;
    }
    return false;
}

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
// The same for the value of an assignment, checked with the contextual type its target gives it (tsc's
// getTypeOfExpression finds it up the tree): what the value is doesn't depend on whether its statement was checked yet
Ty const* ty_flow_assigned_value_type(Checker* me, BinaryOpExpr const* assignment);
// TS2454: a variable that is read where it may not have been assigned yet
typedef enum IdentifierFlowIssue {
    IDENTIFIER_FLOW_OK,
    IDENTIFIER_FLOW_UNASSIGNED, // TS2454
    IDENTIFIER_FLOW_IMPLICIT_ANY, // A `let x;` whose type control flow can't tell here: TS7034 + TS7005
    IDENTIFIER_FLOW_IMPLICIT_ANY_ARRAY, // ... a `let x = []`: any[]
} IdentifierFlowIssue;
IdentifierFlowIssue ty_identifier_flow_issue(Checker* me, IdentifierExpr const* identifier);
// Type facts, for the operators
Ty const* ty_truthy_part(Checker* me, Ty const* ty); // What is left of x where `x` was truthy
Ty const* ty_non_nullish_part(Checker* me, Ty const* ty);
Ty const* ty_non_nullable_if_needed(Checker* me, Ty const* ty);
Ty const* ty_remove_definitely_falsy(Checker* me, Ty const* ty); // Without the members that are always falsy
Ty const* ty_base_type_of_literal(Checker* me, Ty const* ty); // `"a"` -> string, `true` -> boolean
Ty const* ty_substitute_constraints(Checker* me, Ty const* type); // expr.c, see narrowable_type()
// Whether a reference (a name, a property access once computed) asks for its contextual type: its type has a type
// variable with a union constraint (tsc's getNarrowableTypeForReference, hasContextualTypeWithNoGenericTypes)
bool ty_reference_uses_context(Checker* me, Expr const* expr); // expr.c
bool ty_can_be_truthy(Checker* me, Ty const* ty);
bool ty_can_be_falsy(Checker* me, Ty const* ty);
bool ty_can_be_nullish(Checker* me, Ty const* ty);
bool ty_can_be_undefined(Checker* me, Ty const* ty); // tsc's hasTypeFacts(ty, TypeFacts.IsUndefined)
bool ty_can_equal_undefined(Checker* me, Ty const* ty); // tsc's TypeFacts.EQUndefined (any, unknown, void too)
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
// What a `for (k in x)` assigns an existing k (tsc's getIndexTypeOrString): Extract<keyof X, string>, or string if never
Ty const* ty_for_in_index_type_or_string(Checker* me, Ty const* iterable);
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
// The same with what the consumer sends to `next` (null: nothing to check)
Ty const* ty_iterated_type_sending(Checker* me, u32 use, Ty const* ty, Ty const* sent, Span span, ErrorCtx* errs);
TyIterationTypes const* ty_generator_return_iteration_types(Checker* me, Ty const* ret, bool async);
Ty const* ty_generator_yield_context(Checker* me, FunctionImpl const* generator);
TyIterationTypes const* ty_generator_context(Checker* me, FunctionImpl const* generator);
void ty_check_generator_annotation(Checker* me, FunctionImpl const* generator, ErrorCtx* errs);
// tsc's unwrapReturnType of a return type of the function: what an async function's promise resolves to, a generator's
// TReturn (null if there's none)
Ty const* ty_unwrap_return_type(Checker* me, FunctionImpl const* function, Ty const* ret);
Ty const* ty_unwrapped_return_type(Checker* me, FunctionImpl const* function);
Ty const* ty_generator_type(Checker* me, bool async, Ty const* yield_type, Ty const* return_type, Ty const* next_type);

// check.c: what the statement/expression walk checks, with tsc's errors
bool ty_check_assignable(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs); // TS2322 & co
void ty_report_no_property(Checker* me, Ty const* object, Ty const* key, Span span, ErrorCtx* errs);
void ty_check_comparable(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs); // TS2678 & co
void ty_check_type_predicate(Checker* me, Ty const* predicate, Ty const* param, Span span, ErrorCtx* errs); // TS2677
// The same for a source that is `expr`'s type: a literal or arrow function is reported at what inside it doesn't fit
// (tsc's elaborateError). Returns whether it was assignable
void ty_check_enum_member_value(Checker* me, Ty const* source, Span span, ErrorCtx* errs);
bool ty_check_assignable_expr(Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs);
// tsc's checkTypeAssignableTo with an error node: related once, explaining as it goes (no plain run first). True if
// assignable, else `lines` say why, for ty_report_explained_with_head() (its `ts_code` is `head_code`)
bool ty_relate_explaining(
    Checker* me, Ty const* source, Ty const* target, unsigned head_code, VecTyReportLine* lines, ErrorCtx* errs
);
// Error `ts_code` with message `head`, then `lines` (both consumed)
struct Error* ty_report_explained_with_head(ErrorCtx* errs, VecTyReportLine* lines, unsigned ts_code, cstr head, Span span);
// `source` isn't assignable to `target` (the caller checked): error `ts_code` with message `head` (consumed), then why
void ty_report_unassignable_with_head(Checker* me, Ty const* source, Ty const* target, unsigned ts_code, cstr head, Span span, ErrorCtx* errs);
bool ty_check_assignable_to_access(Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, Expr const* left, ErrorCtx* errs);
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
bool ty_base_constructors_fit(Checker* me, Ty const* constructor, u32 count, bool is_js);
bool ty_has_base_type(Checker* me, Ty const* source, Ty const* target);
// Whether the interface is on a cycle of `extends` (tsc reports TS2310 on each declaration of the interfaces in a
// cycle of base types, not on those that merely extend one)
bool ty_interface_in_base_cycle(Checker* me, Symbol const* symbol);
// Whether the members of the class or interface were asked for while its bases were resolved (tsc's getBaseTypes
// failing popTypeResolution: TS2310 at every declaration). Resolves them
bool ty_bases_reentered(Checker* me, Symbol const* symbol);
// Whether evaluating the class's `extends` expression needed the class itself (tsc's getBaseConstructorTypeOfClass
// failing popTypeResolution: TS2506 at the class). Resolves its bases
bool ty_class_base_constructor_circular(Checker* me, struct ClassDecl const* class_decl);
// Whether TS2310 goes on this declaration (a direct `extends` of itself only where it's written)
bool ty_interface_decl_reports_base_cycle(Checker* me, Symbol const* symbol, struct InterfaceDecl const* decl);
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
// Which member declarations of a class declaration are one symbol (tsc's binder with TypeScript 7's excludes, then
// lateBindMember, the late-bound symbols merged into the early ones of their names): per member, its symbol (an
// entry number, the same for the declarations of one symbol)
enum {
    CLASS_MEMBER_ALONE = -1, // It conflicted with the symbol of its name (TS2300): a symbol of its own, no member
    CLASS_MEMBER_NONE = -2, // Not a named member (a constructor, a static block, a name that names nothing)
};
typedef struct ClassMemberParam {
    struct FunctionArg const* arg; // A parameter property
    i32 entry;
} ClassMemberParam;
typedef struct ClassMemberSymbols {
    i32 const* item_entry; // Per class item, by index
    u32 item_count;
    ClassMemberParam const* params;
    u32 param_count;
    u32 entry_count;
    // Per entry: the item index of its first early-bound and first late-bound declaration (a parameter property's is
    // its constructor's), UINT32_MAX for none
    u32 const* first_early;
    u32 const* first_late;
    // Per entry: a late-bound symbol that didn't merge into the early symbol of its name, which one excludes
    bool const* shadowed;
    // Per class item, for an accessor with a computed name: the first accessor, getter and setter of its symbol among
    // the accessors with computed names (item indexes, -1 for none). See ty_class_accessor_first()
    i32 const* accessor_first;
    i32 const* accessor_getter;
    i32 const* accessor_setter;
} ClassMemberSymbols;
// The first accessor of the accessor pair a class's accessor `item` is part of (itself if it leads or stands alone),
// its getter and its setter (null for none). A written name's pair is the binder's (ClassItem.accessor_first), a
// computed name's is its symbol's (ty_class_member_symbols(): by the key's type, `[a]` and `[b]` pair when b is
// `typeof a`), among the accessors with computed names
struct ClassItem const* ty_class_accessor_first(Checker* me, struct ClassItem const* item);
struct ClassItem const* ty_class_accessor_getter(Checker* me, struct ClassItem const* item);
// (`class_decl` for a written name's: the binder links setters to getters only)
struct ClassItem const* ty_class_accessor_setter(Checker* me, ClassDecl const* class_decl, struct ClassItem const* item);
// Whether a declaration of `entry` comes before the member at item `index` (`late`: a late-bound one, which comes after
// every early-bound one) in tsc's order of the symbol's declarations
static inline bool class_member_declared_before(ClassMemberSymbols const* me, i32 entry, u32 index, bool late) {
    if (entry < 0) return false;
    if (late) return my first_early[entry] != UINT32_MAX || my first_late[entry] < index;
    return my first_early[entry] < index;
}
ClassMemberSymbols const* ty_class_member_symbols(Checker* me, ClassDecl const* class_decl);
// The same for the members of type literals and interfaces: `lists` are the member lists of the declarations (one for
// a type literal), whose binder table is shared; checkObjectTypeForDuplicateDeclarations runs per list
void ty_check_object_type_duplicate_members(Checker* me, VecInterfaceMember const* const* lists, u32 list_count, ErrorCtx* errs);
void ty_check_class_interface_property_types(Checker* me, Symbol const* symbol, ErrorCtx* errs);
void ty_check_parameter_property_redeclaration(Checker* me, Symbol const* class_symbol, struct FunctionArg const* arg, ErrorCtx* errs);
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
bool ty_is_valid_spread_type(Checker* me, Ty const* type); // tsc's isValidSpreadType
bool ty_object_rest_source_valid(Checker* me, Ty const* source);
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
bool ty_mapped_members_incomplete(Checker* me, Ty const* ty); // HACK: see MembersRare.index_signatures_incomplete
bool ty_is_generic_type(Checker* me, Ty const* ty); // tsc's isGenericType (expr.c)
// A function expression without a contextual signature, or one with a generic return type (TS7010/TS7011 from widening)
void ty_check_instanceof_has_instance(Checker* me, Ty const* left, Ty const* right, Span left_span, Span right_span, ErrorCtx* errs);
bool ty_function_widening_reportable(Checker* me, FunctionImpl const* function, bool yield);
void ty_function_decide_widening_reportable(Checker* me, FunctionImpl const* function); // when its return type is inferred
bool ty_function_context_is_self(Checker* me, FunctionImpl const* function);
Ty const* ty_generator_return_before_widening(Checker* me, FunctionImpl const* function); // A generator's returns, if they widen to any
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
void ty_check_export_equals_property_reference(Checker* me, struct IdentifierType const* identifier); // lower.c
// The symbols baseline's symbol of a type reference to a property of what `export =` names (the type it is, the
// global type a value-only one gives way to, the import itself when it is missing), null if not one (lower.c)
struct Symbol const* ty_export_equals_property_shown(Checker* me, struct IdentifierType const* identifier);
// Whether a type reference to an import of a value-only property of what `export =` names means the global type of
// that name (tsc's resolveName goes on past the import)
bool ty_export_equals_property_means_global(Checker* me, struct IdentifierType const* identifier);
Ty const* ty_union_any_index_type(Checker* me, Ty const* object, Ty const* index, u32 kinds); // `U[any]` of a union (access.c)
// TS4111 (noPropertyAccessFromIndexSignature): `o.k` that only an index signature has (check.c)
void ty_check_index_signature_dot_access(Checker* me, PropertyExpr const* access, Ty const* object, ErrorCtx* errs);
// A number, symbol or pattern index signature applies to the key (tsc's isApplicableIndexType, not for `string` keys)
bool ty_specific_index_applies(Checker* me, IndexInfo const* info, Ty const* index);
// Member modifiers at a property access (tsc's checkPropertyAccessibility and the readonly checks of
// checkPropertyAccessExpressionOrQualifiedName): TS2341, TS2445, TS2446, TS2340, TS2855, TS2540, TS2542
void ty_check_property_used_before_assigned(Checker* me, Expr const* expr, ErrorCtx* errs); // TS2565
void ty_check_member_modifiers(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs);
// `#x` in `#x in obj`: the instance type of the class that declares it (its constructor for a static `#x`), null if
// none does. `name` (may be null): the class's `#x`
Ty const* ty_private_name_owner(Checker* me, Expr const* private_name, Atom const** name);
void ty_check_in_expression(Checker* me, Expr const* left, Expr const* right, ErrorCtx* errs); // `left in right`
// `a.#x`: what tsc says about private names rather than TS2339 (TS18013, TS18014, TS18016). True if it reported
void ty_check_async_return_annotation(Checker* me, struct FunctionImpl const* function, ErrorCtx* errs); // check.c
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
void ty_check_destructuring_indexed_access(Checker* me, Ty const* object, Ty const* index, Span index_span, ErrorCtx* errs);
void ty_check_indexed_access_type(Checker* me, Ty const* object, Ty const* index, Span index_span, Span span, ErrorCtx* errs);
// TS2536 for `o[k]` with k generic (tsc's checkIndexedAccessIndexType; the expression's type is the error type)
// tsc's checkPropertyAccessExpressionOrQualifiedName: `t.x = v` of a generic `t` doesn't write through an index
// signature of its constraint (TS2339)
void ty_check_generic_index_write(Checker* me, struct PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs);
IndexInfo const* ty_generic_write_index_info(Checker* me, struct PropertyExpr const* access, Ty const* object, Ty const* key);
void ty_check_generic_element_access(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs);
Ty const* ty_invalid_generic_element_access(Checker* me, PropertyExpr const* access, Ty const* object);
bool ty_indexed_access_index_ok(Checker* me, Ty const* indexed);
bool ty_is_numeric_string_type(Checker* me, Ty const* ty); // `${number}` (tsc's numericStringType)
void ty_check_infer_constraints(Checker* me, struct InferredType const* inferred, ErrorCtx* errs);
// The constraint of type parameter `index` of `params`, instantiated with `args` (null if none or no argument)
Ty const* ty_type_argument_constraint(Checker* me, TyList params, VecSpannedType const* args, u32 index);
void ty_check_type_reference_constraints(Checker* me, Symbol const* symbol, VecSpannedType const* args, ErrorCtx* errs);

// call.c
// `contextual`: the contextual type of the call expression (return type inference), may be null
Ty const* ty_call_return_type(Checker* me, Ty const* callee, CallExpr const* call, Span call_span, Ty const* contextual);
void ty_call_typecheck(Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual, Span call_span, ErrorCtx* errs);
// A tagged template `` tag`a${x}` ``, the EXPR_TEMPLATE: a call of the tag with a TemplateStringsArray and the substitutions
Ty const* ty_tagged_template_return_type(Checker* me, Ty const* callee, Expr const* template, Ty const* contextual);
void ty_tagged_template_typecheck(Checker* me, Ty const* callee, Expr const* template, Ty const* contextual, ErrorCtx* errs);
// A decorator checked as a call (tsc's checkDecorator/resolveDecorator): of the class, of `item`, or of its
// parameter `param_index` (-1: not a parameter). `at`: the whole decorator from its `@`
struct ClassItem;
typedef struct DecoratorSite {
    ClassDecl const* class_decl;
    struct ClassItem* item;
    i32 param_index;
    Span at;
} DecoratorSite;
// Checks the decorator expression too, with the decorator's signature as its contextual type (tsc's
// getContextualTypeForDecorator): expr_typecheck() of it comes after
void ty_check_decorator(Checker* me, Expr const* decorator, DecoratorSite site, ErrorCtx* errs);
Ty const* ty_awaited(Checker* me, Ty const* ty);
Ty const* ty_promise_like_of(Checker* me, Ty const* ty);
u32 ty_call_signature_count(Checker* me, Ty const* ty); // call.c
bool ty_is_promise(Checker* me, Ty const* ty); // A thenable (tsc's getAwaitedTypeOfPromise), call.c
Ty const* ty_awaited_of_promise(Checker* me, Ty const* ty); // What a thenable awaits to, else null (call.c)
void ty_check_await_operand(Checker* me, Ty const* ty, Span at, ErrorCtx* errs);
Ty const* ty_promised_type(Checker* me, Ty const* ty); // What a thenable resolves to, else null (tsc's getPromisedTypeOfPromise)
Ty const* ty_awaited_no_alias(Checker* me, Ty const* ty);
Ty const* ty_async_contextual_awaited(Checker* me, Ty const* contextual); // function.c
Ty const* ty_async_return_expressions_context(Checker* me, Ty const* contextual); // function.c
Ty const* ty_promise_of(Checker* me, struct FunctionImpl const* function, Ty const* type); // Promise<type>, for an async function (function.c)
Ty const* ty_unwrap_awaited(Checker* me, Ty const* ty); // tsc's unwrapAwaitedType (call.c)

// ty_display.c
typedef struct TyDisplayVisit {
    Ty const* ty;
    struct TyDisplayVisit const* outer;
} TyDisplayVisit;

// The declarations whose type is being printed through their pseudo type further up, and that type (tsgo's
// context.enclosingSymbolTypes, addSymbolTypeToContext): a signature of the same declaration printed inside returns it
typedef struct TyDisplaySymbolType {
    void const* decl; // A FunctionImpl: the signature's declaration
    struct Ty const* type;
    struct TyDisplaySymbolType const* outer;
} TyDisplaySymbolType;

// The reverse mapped properties being printed further up (tsc's context.reverseMappedStack), innermost first
typedef struct TyDisplayReverse {
    Prop const* prop;
    struct TyDisplayReverse const* outer;
} TyDisplayReverse;

// tsc's GenerateNamesForShadowedTypeParams (the types baseline): a type parameter whose name another one printed in
// the same scope already has, or that names another type parameter in scope where the type is printed, prints as
// `T_1`, `T_2`... (typeParameterToName)
typedef struct TyParamName {
    Ty const* param;
    Atom const* name;
} TyParamName;
typedef struct TyParamNames {
    TyList in_scope; // The declared type parameters in scope where the type is printed, innermost last
    // Declaration emit copying an annotation as written (tsgo's declaration transformer visits it, the node builder
    // never sees it): no type parameter is renamed (`infer T` inside `type A<T>` stays T)
    bool as_written;
    // The names given so far (tsc's typeParameterNames and typeParameterNamesByText): a signature is a scope, it drops
    // the names given inside it on its way out
    TyParamName* given;
    u32 len;
    u32 cap;
    u32 used; // How many of the names given the print so far depends on (the highest one it used, see TyPrinted)
    // While an instantiated signature's type parameters are named: the names of its parameters that are copies (tsc's
    // instantiateSymbol), each a symbol other than the type parameter of its name it was declared with (the binder's
    // one symbol of both), so a type parameter of that name is shadowed (tsc's params fake scope)
    SigParam const* value_params;
    u32 value_param_count;
    struct FunctionImpl const* params_of; // While a signature's type parameters are named: its declaration
    // Conditional types printed so far and where (tsc's serializedTypes: printed again, a type is the same text, names
    // and all, and isn't worked out twice). Freed with ty_param_names_drop()
    struct TyPrinted* printed;
    u32 printed_len;
    u32 printed_cap;
} TyParamNames;
void ty_param_names_drop(TyParamNames* names);

// Declaration emit's SymbolTracker (tsc's declarations.ts symbolTracker, plan 40): what the printer reports while it
// prints a type the way a `.d.ts` would write it (TyDisplay.tracker)
typedef enum DeclTrackReport : uint8_t {
    DECL_REPORT_PRIVATE_IN_CLASS_LITERAL, // reportPrivateInBaseOfClassExpression (`name`: the property)
    DECL_REPORT_UNIQUE_SYMBOL, // reportInaccessibleUniqueSymbolError
    DECL_REPORT_THIS, // reportInaccessibleThisError
    DECL_REPORT_NON_SERIALIZABLE, // reportNonSerializableProperty (`name`: the property)
    DECL_REPORT_TRUNCATION, // reportTruncationError
    DECL_REPORT_CYCLIC, // reportCyclicStructureError (createCyclicStructurePlaceholder)
} DeclTrackReport;

struct PseudoError;
typedef struct DeclTracker {
    // A symbol the printer names (lookupSymbolChain's trackSymbol)
    void (*track_symbol)(struct DeclTracker* me, Symbol const* symbol, u32 meaning);
    // The computed name (`[a.b]`, as written) of a late bound property's declaration in `file` (trackComputedName)
    void (*track_computed_name)(struct DeclTracker* me, csview name, struct SourceFile const* file);
    void (*report)(struct DeclTracker* me, DeclTrackReport report, csview name);
    // A name the printer roots at `import("...")` of this file (symbolToTypeNode's import type: the specifier may only
    // reach it through a node_modules, TS2883 naming `symbol`)
    void (*track_module_file)(struct DeclTracker* me, struct SourceFile const* file, Symbol const* symbol);
    // ReportInferenceFallback: where the printer serializes a declaration's type from its syntax (pseudo.h), what the
    // syntax doesn't say (isolatedDeclarations' diagnostics)
    void (*report_fallback)(struct DeclTracker* me, struct PseudoError const* node);
    void* ctx;
    // The printed type passed the length tsc serializes (reportTruncationError, reported): the printer stops
    bool truncated;
} DeclTracker;

// The types one declaration's emit printed from their structure (tsgo's per-enclosing-declaration serializedTypes
// cache: printed again in the same scope, a type is a deep clone of the first print's nodes, which have no mappings).
// A hash set of (type, the print of the outermost printed signature with parameters); declaration_emit.c owns and
// resets it
typedef struct TyPrintedKey {
    Ty const* ty;
    void const* scope;
} TyPrintedKey;
typedef struct TyPrintedTypes {
    TyPrintedKey* slots;
    u32 cap; // A power of two, 0 when empty
    u32 count;
    u32 serial; // Signatures printed (TyDisplayParams.serial)
} TyPrintedTypes;
// Whether `ty` was printed before, adding it if not
bool ty_printed_types_add(TyPrintedTypes* me, Ty const* ty, void const* scope);
void ty_printed_types_clear(TyPrintedTypes* me);

typedef struct TyDisplay {
    cstr* target;
    // Where in `target` the type starts (declaration emit writes into the file's text: the length limit is the type's)
    isize_t target_start;
    // A message's type: tsc's approximateLength so far, which decides where it is cut short (see display_over()). Null
    // elsewhere, and inside a reused annotation (counted by its text)
    u32* approx;
    bool naming; // Inside a name being printed, which counts as a whole (see out_symbol_name())
    bool cut_off; // A message's type being printed, cut off after (see display_cut_off())
    // Declaration maps (decl_map.h): reused nodes are marked where they start and end when `comments` says so (its file
    // is the one being written), unless `no_map`. `param_declaration`: a type parameter printed is declared there (its
    // name gets its declaration's range). (In padding: the struct is copied on every call)
    bool no_map;
    TyPrintedTypes* printed; // Declaration emit: see TyPrintedTypes (null elsewhere)
    bool reprinted; // Inside a type the declaration printed before (no_map, see TyPrintedTypes)
    bool param_declaration;
    u32 depth;
    TyDisplayVisit const* visiting; // The anonymous types being printed further up the stack
    TyDisplayReverse const* reverse_stack;
    // Where the type is printed from (tsc's enclosingDeclaration): names are qualified as far as needed to be reachable
    // from there (`T` inside namespace N, `N.T` outside, an import's name, `import("a").T` when nothing names it).
    // Null: the bare name (tsc's lookupSymbolChainWorker without an enclosing declaration: what error messages show),
    // or fully qualified with `fully_qualified` (tsc's UseFullyQualifiedType, when a source and target print the same)
    struct Scope const* enclosing;
    // The types baseline: where in the file the type is printed (a parameter's name is only visible inside its
    // function, see index_components_printed())
    Span at;
    // The module specifier of the import or export declaration whose name is printed (tsc's
    // tryGetModuleSpecifierFromDeclaration of the enclosing declaration): its usage mode picks the specifier ending
    struct ModuleRef const* usage_ref;
    // ... and its import attributes (`with { type: "json" }`), which an `import("m")` naming that module repeats (tsgo's
    // getSpecifierForModuleSymbol: originalImportAttributesType)
    struct ImportAttributes const* usage_attributes;
    bool fully_qualified;
    // Printing for the types baseline or declaration emit: tsc's typeToTypeNode, which reuses annotation nodes from the
    // source (see display_signature()) and can't name a local alias
    bool types_baseline;
    bool reused; // Inside a node reused from the source
    // Reprint annotations where tsc's node builder reuses them (a message's type printed from its expression: tsc's
    // getTypeNamesForErrorDisplay). Only with `enclosing`
    bool reuse_nodes;
    // The type parameters of an instantiated generic signature being printed: tsc gives it fresh ones, so nothing that
    // mentions them is reused inside of it
    TyList fresh_params;
    // Declaration emit: the signature being printed is a method of a class instance type printed as a literal that tsgo
    // instantiates for `this` (not thisless: isThisless wants one declaration), so its own type parameters are fresh
    bool own_params_fresh;
    // The file of the signature whose annotations are being reused, if known: what a keyword node (no span of its
    // own) was written in, for declaration maps and comments (null: decided by the text at its offset)
    struct SourceFile const* node_file;
    // Print unique symbol types as `typeof sym` rather than `unique symbol` (tsc's typeToString without
    // AllowUniqueESSymbolType, which its default flags have: messages' fully qualified names, getTypeNameForErrorDisplay)
    bool typeof_unique_symbols;
    TyList infer_params; // Inside the extends type of a conditional type: its own `infer` type parameters
    // Reprinting an annotation of this instantiated signature (tsc's context.mapper): its type parameters that the
    // instantiation maps aren't reused, they print what they're mapped to
    struct Signature const* mapped_by;
    TyParamNames* param_names; // Null: type parameters print as they're called
    // The annotation being reprinted is an array's element type: a part of it that prints its type instead is
    // parenthesized as needed (tsc's factory, parenthesizeElementTypeOfArrayType). Only for the node itself
    bool array_element_node;
    // The same for a member of a union or intersection annotation (parenthesizeConstituentTypeOfUnionType: unions,
    // intersections, function and conditional types)
    bool constituent_node;
    // Inside the `extends` type of a conditional type node (tsgo's printer's inExtends): a conditional type node there
    // is parenthesized, `() => (A extends B ? 1 : 2)` (only the parts that keep the context have it: unions, arrays,
    // type operators, function return types...)
    bool in_extends;
    DeclTracker* tracker; // Declaration emit: null everywhere else
    // Printing the checker's type where the syntax already reported (tsgo's suppressReportInferenceFallback): what is
    // printed inside doesn't report to the tracker again
    bool suppress_fallback;
    bool in_object_literal; // Inside a type literal's members (tsc's InObjectTypeLiteral): `this` can't be written there
    // The parameters of the signatures being printed (innermost first): the fake scope tsc's enterNewScope makes for a
    // signature, where a reused `typeof x` must still mean the same (see name_accessible())
    struct TyDisplayParams const* printed_params;
    bool no_reduction; // tsc's NoTypeReduction: an intersection that is never prints as the intersection
    // The signature printed is one of several of an object type, one of which mentions what its instantiation maps:
    // tsc instantiated them all (getObjectTypeInstantiation looks at every declaration of the symbol)
    bool sibling_mentions_mapped;
    // The type literal being printed is the annotation of the declaration whose name is `at` (tsc's enclosing
    // declaration is the printed node's parent): a `unique symbol` written inside it can be reused
    Span decl_literal;
    // tsc's context.truncating once it is set by the depth limits (a conditional or anonymous type nested eleven times
    // in itself): sticky for the rest of the print, the truncation length's elisions apply from there on (`any` where
    // nothing truncates, see display_over()). One per print, made by the entry points
    bool* over;
    TyDisplaySymbolType const* symbol_types;
    // Declaration emit: object types and type literals are printed one member per line (tsc's MultilineObjectLiterals,
    // the printer's MultiLineTypeLiteralMembers), `indent` levels of 4 spaces deep where the type starts
    bool multiline;
    u32 indent;
    // Declaration emit: the comments of the file being written, copied around reused members (decl_comments.h)
    struct DeclComments const* comments;
    // Declaration emit: the node being printed is one the declaration transformer visits (a written annotation), not one
    // the node builder reuses: those of its mapped types and tuples aren't SingleLine (tryReuseExistingTypeNode sets it)
    bool transformer_node;
    // Declaration emit, inside such a node: the node builder reprints this part (a JavaScript property signature's type,
    // tsgo's ensureType through TryJSTypeNodeToTypeNode): its mapped types are SingleLine, its JSDoc literals' members
    // have no descriptions
    bool builder_part;
    // Declaration emit: where the node being reprinted (whose comments are written around it) starts and ends: a part
    // of it starting or ending there leaves them to it (the printer's containerPos and containerEnd). A caller that
    // writes a declaration's trailing comments itself sets the end to its annotation's
    char const* comments_container_start;
    char const* comments_container_end;
    // Declaration emit: a `@typedef`'s type literal (made of `@property` tags), whose members are written with their
    // tags' descriptions as JSDoc comments (tsgo's preservePartialJsDoc of the transformer's property signatures; not
    // in the literals deeper down, which the node builder reuses)
    struct Interface const* jsdoc_described;
} TyDisplay;
typedef struct TyDisplayParams {
    struct SigParam const* params;
    u32 count;
    // The signature's own parameters when `params` expanded a rest tuple: what the names mean (tsgo's fake scope has the
    // original parameter at each index where the expanded one differs)
    struct SigParam const* originals;
    u32 original_count;
    struct FunctionImpl const* function; // Whose parameters they are, if known
    struct TyDisplayParams const* outer;
    // Declaration maps: this print's number (TyPrintedTypes.serial), which tsgo's fake scope of a signature with
    // parameters is a fresh enclosing declaration of (its serializedTypes cache): see ty_display()
    u32 serial;
} TyDisplayParams;
void ty_display(Checker* me, TyDisplay d, Ty const* ty);
// Declaration emit (plan 64): a written type node as tsc's printer reprints it, a declaration's type parameter list
// (nothing without any), a pseudo type (pseudo.h) where it says the declaration's type `ty`, a binding pattern
void ty_display_node(Checker* me, TyDisplay d, struct TypeNode const* node);
void ty_display_type_params(Checker* me, TyDisplay d, struct Generic const* generic);
// A written signature: `<T>(x: T) => U` with `separator` " => ", `(x: T): U` with ": "
void ty_display_node_signature(Checker* me, TyDisplay d, struct FunctionImpl const* function, char const* separator);
struct Pseudo;
void ty_display_pseudo(Checker* me, TyDisplay d, struct Pseudo const* pseudo, Ty const* ty);
// A string literal node the emitter makes (tsc's factory.createStringLiteral() printed): ASCII-escaped, `"\u00E9"`
void ty_display_synthesized_string(TyDisplay d, csview sv);
void ty_append_binding_pattern(cstr* out, struct Binding const* binding);
// The same with declaration maps' marks for `map`, the file being written (decl_map.h)
void ty_append_binding_pattern_mapped(cstr* out, struct Binding const* binding, struct SourceFile const* map);
// The same with the comments around its elements (a parameter's pattern the declaration transformer visits)
struct DeclComments;
void ty_append_binding_pattern_commented(cstr* out, struct Binding const* binding, struct SourceFile const* map,
    struct DeclComments const* comments, u32 indent);
bool ty_is_identifier_name(csview name); // An identifier as written (`#x` too)
// Declaration emit: whether a JSDoc type reference's name, `found` as a type from where it is printed, is one tsgo tracks
// (trackExistingEntityName, or checkEntityNameVisibility): not a value the JSDoc fallback makes a type of
bool ty_jsdoc_name_is_type(struct Symbol const* found, struct IdentifierType const* identifier);
void ty_display_signature(Checker* me, TyDisplay d, Signature const* sig);
void ty_display_signature_arrow(Checker* me, TyDisplay d, Signature const* sig);
void ty_display_predicate(Checker* me, TyDisplay d, TyPredicate const* predicate);
cstr ty_to_cstr(Checker* me, Ty const* ty);
// Where a message prints a type from, null if from nowhere: an expression type that isn't context sensitive, from its
// expression (tsc's getTypeNamesForErrorDisplay: typeToString(type, symbol.valueDeclaration)), annotations reused
struct Scope const* ty_report_enclosing(Checker* me, Ty const* ty);
// The property name of `Symbol.<name>` (a unique symbol), null if the lib has no such symbol
Atom const* ty_well_known_symbol_name(Checker* me, char const* name);
cstr ty_prop_name_cstr(Checker* me, Atom const* name); // As messages show it: `x`, `[Symbol.iterator]`
cstr ty_prop_written_name(Checker* me, Prop const* prop); // As declared if not an identifier (`"a"`, `2.0`, `["a"]`)
