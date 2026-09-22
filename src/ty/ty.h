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
typedef struct Checker Checker;
typedef struct Prop Prop;
typedef struct Signature Signature;
typedef struct Members Members;
typedef struct TyMapper TyMapper;
typedef struct TyCondBranch TyCondBranch;
typedef struct GenericItem GenericItem;
typedef struct Program Program;

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
    // Cached facts
    TYF_ERROR                        = 1 << 24, // the `any` that stands for "an error was already reported"
    TYF_COULD_CONTAIN_TYPE_VARIABLES = 1 << 25, // if unset, instantiate() is the identity
    TYF_FRESH_LITERAL                = 1 << 26, // TODO: fresh/regular literal pairs for widening

    TYF_NULLABLE = TYF_UNDEFINED | TYF_NULL,
    TYF_LITERAL = TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_BIGINT_LITERAL | TYF_BOOLEAN_LITERAL,
} TyFlags;

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
    Members const* members;
} TyObjectState;

typedef struct TyParamState {
    GenericItem const* item; // Null for type parameters that were only ever seen by reference (and infer/mapped ones)
    TypeNode const* constraint_node;
    LazyState constraint_state;
    Ty const* constraint; // null if unconstrained
    LazyState fallback_state;
    Ty const* fallback;
    bool is_infer; // declared by `infer X` in the extends type of a conditional type
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
            // Set if this is the value of a declaration with several value declarations (a namespace merged with a
            // function, class or enum): the members are those of all of them, see ty_merged_value_type()
            Symbol const* merged_value;
            Atom const* name;
            TyList type_params;
            TupleShape const* tuple;
            TyObjectState* state;
        } declared;
        struct {
            Ty const* target; // TY_DECLARED
            TyList args; // Don't read directly, use ty_reference_args(): empty until resolved if `deferred` is set
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
            TyObjectState* state;
        } anonymous;
        struct {
            TyList types; // sorted by id, no duplicates
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
    TypeNode const* node; // TYPE_ARRAY or TYPE_TUPLE
    LowerCtx ctx;
    TyMapper const* mapper; // may be null
    LazyState state;
    TyList args;
} TyDeferredRef;

typedef enum PropFlags {
    PROP_OPTIONAL = 1 << 0,
    PROP_READONLY = 1 << 1,
    PROP_METHOD   = 1 << 2,
} PropFlags;

// A property of an object type. Deliberately the seed of plan 03's persistent symbol.
struct Prop {
    Atom const* name;
    u32 flags;
    Symbol const* parent; // the declared type it was declared in, may be null
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
    // Where it was declared, for the symbols baseline (not copied to instantiations, follow `target`)
    Span name_span;
    Span const* decl_spans;
    u32 decl_span_count;
    bool name_is_value; // The name was written as a string or number literal
    // Types baseline only: an object literal property `a: x as T` prints its type by reusing `T` (see ty_display.c)
    TypeNode const* asserted;
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
    // The return type is lazier than the rest, use signature_return_type(): it's often inferred from a function body
    // that is in the middle of being checked
    LazyState ret_state;
    Ty const* ret;
    TyPredicate const* predicate; // Use signature_predicate()
    bool predicate_done;
};

typedef struct IndexInfo {
    Ty const* key;
    Ty const* value;
    Atom const* param_name;
    bool readonly;
} IndexInfo;

typedef struct MappedInfo {
    Ty const* type_param;
    Ty const* constraint;
    Ty const* name_type; // `as` clause, may be null
    Ty const* template;
    Ty const* modifiers_type; // `T` of a homomorphic `[K in keyof T]`: where optional/readonly come from. May be null
    signed char readonly; // -1 remove, +1 add
    signed char optional;
} MappedInfo;

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
    MappedInfo const* mapped; // non-null for a mapped type, which then has no other members (yet, see hacks.md)
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
} SymbolLinks;

typedef enum TyRelation {
    TY_RELATION_ASSIGNABLE,
    TY_RELATION_COMPARABLE,
    // Assignable, except that null and undefined are never assignable to other types, whatever strictNullChecks says.
    // HACK: what the legacy operator checks mean by "extends number"
    TY_RELATION_ASSIGNABLE_STRICT_NULLS,
    // What union (subtype) reduction uses: `any` is only a subtype of `any`/`unknown`, and a type that lacks an
    // optional property of the target isn't a subtype of it
    TY_RELATION_STRICT_SUBTYPE,
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
} TyReportLine;
static inline void ty_report_line_drop(TyReportLine* me) { cstr_drop(&my message); }
static inline TyReportLine ty_report_line_clone(TyReportLine me) { me.message = cstr_clone(me.message); return me; }
#define g_class_key TyReportLine, ty_report_line
#include <cleat/generic/vec.h>

typedef enum TyIntrinsic {
    // Creation order copies tsc's, since union members print in id order
    TY_ANY,
    TY_ERROR,
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
    Ty const* global_types[9]; // GlobalType in relate.c: String, Number... looked up on first use
    bool global_types_resolved[9];
    u32 instantiation_depth;
    u32 instantiation_count;
    bool instantiation_limit_hit; // Set when ty_instantiate() gave up; whoever reports errors turns it into TS2589
    RelationCache relations[TY_RELATION_COUNT];
    struct RelationState* relating; // The relation check in progress, if any (relate.c)
    Ty const* return_context; // The contextual type of `return` expressions in the function being looked at, or null
    // Control flow (flow.c). While flow_uncached is set expressions are evaluated without reading unfinished memos or
    // writing any: what they evaluate to may depend on the incomplete types of a loop, see ty_flow_expr_type()
    u32 flow_uncached;
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
Atom const* checker_atom(Checker* me, csview sv);
static inline Ty const* ty_intrinsic(Checker const* me, TyIntrinsic i) { return my intrinsics[i]; }

// Intern key building. Keys are small, build them on the stack: u64 key[1 + n] = { n, ... }
void const** intern_slot(Checker* me, InternTable* table, u64 const* words, u32 count);

Ty* ty_alloc(Checker* me, TyKind kind, u32 flags);
TyList ty_list_copy(Checker* me, Ty const* const* data, u32 len);

Ty const* ty_unresolved(Checker* me, csview name);
Ty const* ty_literal(Checker* me, Value const* value);
Ty const* ty_enum_literal(Checker* me, Value const* value, Symbol const* enum_decl, Symbol const* member);
Ty const* ty_enum_computed_member(Checker* me, Symbol const* enum_decl, Symbol const* member);
Ty const* ty_string_literal(Checker* me, csview sv);
Ty const* ty_union(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args);
Ty const* ty_intersection(Checker* me, Ty const* const* types, u32 len, Symbol const* alias, TyList alias_args);
Ty const* ty_type_param(Checker* me, Symbol const* decl);
Ty const* ty_synthetic_type_param(Checker* me, csview name);
Ty const* ty_reference(Checker* me, Ty const* target, TyList args);
Ty const* ty_array(Checker* me, Ty const* element, bool readonly);
Ty const* ty_tuple_target(Checker* me, TupleShape const* shape);
Ty const* ty_keyof(Checker* me, Ty const* operand);
Ty const* ty_indexed(Checker* me, Ty const* object, Ty const* index);
Ty const* ty_conditional(
    Checker* me, Ty const* check, Ty const* extends, TyCondBranch const* if_true, TyCondBranch const* if_false,
    Ty const* root, TyMapper const* mapper, // null for a root
    bool distributive
);
Ty const* ty_realias(Checker* me, Ty const* ty, Symbol const* alias, TyList alias_args);
Ty const* ty_non_nullable(Checker* me, Ty const* ty);
Ty const* ty_cond_branch_type(Checker* me, TyCondBranch* branch);
static inline Ty const* ty_conditional_branch(Checker* me, Ty const* conditional, bool which) {
    return ty_cond_branch_type(me, &conditional->conditional.branches[which]);
}

// members.c
Members const* ty_members(Checker* me, Ty const* ty); // Never null. Empty for non-object types
Prop const* ty_get_prop(Checker* me, Ty const* ty, Atom const* name);
// The type whose members a value of `ty` has: `String` for a string, the constraint of a type parameter...
Ty const* ty_apparent_type(Checker* me, Ty const* ty);
Ty const* prop_type(Checker* me, Prop const* prop);
bool ty_has_unresolved_base(Checker* me, Ty const* ty); // HACK: see has_unresolved_base()
void signature_resolve(Checker* me, Signature const* sig);
Ty const* signature_return_type(Checker* me, Signature const* sig);
TyPredicate const* signature_predicate(Checker* me, Signature const* sig);
bool signature_declared_never(Signature const* sig);
typedef struct CallEffects {
    bool has_predicate;
    bool returns_never;
    TyPredicate predicate; // With the type arguments of the call
} CallEffects;
CallEffects ty_call_effects(Checker* me, Ty const* callee, CallExpr const* call);
TyList ty_reference_args(Checker* me, Ty const* reference);
Ty const* ty_param_constraint(Checker* me, Ty const* param); // null if unconstrained
Ty const* ty_base_constraint(Checker* me, Ty const* ty); // of a type parameter or deferred type, null if there's none
Ty const* ty_param_default(Checker* me, Ty const* param); // null if none

// instantiate.c
TyMapper const* ty_mapper_new(Checker* me, TyList sources, TyList targets);
Ty const* ty_instantiate(Checker* me, Ty const* ty, TyMapper const* mapper);

// lower.c
Ty const* ty_from_node(Checker* me, TypeNode const* node, LowerCtx ctx);
// The type of a binding (a variable, a parameter, an element of a destructuring pattern): what is written (lowered
// with `ctx`), else what is assigned to it
Ty const* ty_binding_type(Checker* me, Binding const* binding, LowerCtx ctx);
// A class as written inside itself: `C<T, U>` with its own type parameters as the arguments
Ty const* ty_class_instance_type(Checker* me, Symbol const* class_symbol);
Ty const* ty_binding_assigned_type(Checker* me, Binding const* binding);
Ty const* ty_declared_type_of(Checker* me, Symbol const* decl);
Ty const* ty_namespace_type(Checker* me, struct Symbol const* symbol);
Ty const* ty_static_type_of(Checker* me, Symbol const* symbol);
Ty const* ty_type_of_symbol(Checker* me, Symbol const* symbol);
Ty const* ty_merged_value_type(Checker* me, Symbol const* symbol);
u32 ty_merged_value_entries(Checker* me, Symbol const* symbol, Ty const*** out); // malloc'd
Atom const* ty_qualified_name(Checker* me, Symbol const* decl); // `A.B.name`
void ty_set_declared_type(Checker* me, Symbol const* decl, Ty const* type);
TyList ty_type_params_of(Checker* me, Generic const* generic);
Ty const* ty_anonymous_from_node(Checker* me, TypeNode const* node, LowerCtx ctx, Symbol const* alias, TyList alias_args);

// enums.c
Ty const* ty_resolve_enum(Checker* me, Symbol const* decl); // Only ty_declared_type_of() should call this
Ty const* ty_enum_of_member(Checker* me, Ty const* member); // The enum type an enum literal belongs to
bool ty_is_enum_union(Ty const* ty);

// relate.c
bool ty_is_related(Checker* me, Ty const* source, Ty const* target, TyRelation relation);
// Why source isn't related to target, outermost line first. The first line is the natural head of the error: TS2322
// "Type 'S' is not assignable to type 'T'." unless something more specific replaces it (TS2741 and friends)
void ty_explain_unrelated(Checker* me, Ty const* source, Ty const* target, TyRelation relation, VecTyReportLine* out);
Ty const* ty_widened_literal(Checker* me, Ty const* ty);
// How the source of a failed relation shows in the error: widened unless the target has room for literal types
cstr ty_report_source_cstr(Checker* me, Ty const* source, Ty const* target);
void relation_cache_drop(RelationCache* me);
// conditional.c
// Null while the conditional type has to stay deferred
Ty const* ty_conditional_resolve(
    Checker* me, Ty const* check_type, Ty const* extends, TyCondBranch const* if_true, TyCondBranch const* if_false
);

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
Ty const* ty_contextual_return_type(Checker* me, FunctionImpl const* function); // null if none
bool ty_expr_is_context_sensitive(Expr const* expr);
// The type of the property an item of an object literal made (what the `.types` baseline shows at its name)
Ty const* ty_object_literal_member_type(Checker* me, Expr const* object, ObjectExprItem const* item);
// `x as const`
typedef struct AsTypeExpr AsTypeExpr;
bool as_type_is_const(AsTypeExpr const* as_type);
// `f<A, B>` as a value: the type arguments applied to the signatures that take that many
Ty const* ty_apply_type_arguments(Checker* me, Ty const* ty, TyList args);

typedef struct ObjectLiteralProp {
    Atom const* name;
    u32 flags; // PropFlags
    Ty const* type;
    Span name_span;
    Span const* decl_spans;
    u32 decl_span_count;
    bool name_is_value;
    TypeNode const* asserted; // See Prop.asserted
} ObjectLiteralProp;
Ty const* ty_object_literal(Checker* me, ObjectLiteralProp const* props, u32 count, IndexInfo const* index, u32 index_count);

// function.c
Ty const* ty_function_type(Checker* me, FunctionImpl const* function);
// The return type of a function: its annotation, else inferred from the `return`s of its body
Ty const* ty_function_return_type(Checker* me, FunctionImpl const* function);

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
bool ty_identifier_used_before_assigned(Checker* me, IdentifierExpr const* identifier);
// Type facts, for the operators
Ty const* ty_truthy_part(Checker* me, Ty const* ty); // What is left of x where `x` was truthy
Ty const* ty_non_nullish_part(Checker* me, Ty const* ty);
Ty const* ty_remove_definitely_falsy(Checker* me, Ty const* ty); // Without the members that are always falsy
Ty const* ty_base_type_of_literal(Checker* me, Ty const* ty); // `"a"` -> string, `true` -> boolean
bool ty_can_be_truthy(Checker* me, Ty const* ty);
bool ty_can_be_falsy(Checker* me, Ty const* ty);
bool ty_can_be_nullish(Checker* me, Ty const* ty);
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

// check.c: what the statement/expression walk checks, with tsc's errors
void ty_check_assignable(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs); // TS2322 & co
void ty_report_unfit_argument(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs); // TS2345
bool ty_is_comparable_either_way(Checker* me, Ty const* a, Ty const* b);
Ty const* ty_of_node(TypeNode const* node); // A written type, by the current checker
// `object[index]` / `object.name`: TS2538, TS2493, TS2339...
void ty_check_property_access(
    Checker* me, Ty const* object, Ty const* index, Span object_span, Span index_span, bool object_is_named,
    bool error_if_not_found, ErrorCtx* errs
);
bool ty_type_argument_satisfies_constraint(Checker* me, TyList params, VecSpannedType const* args, u32 index);

// call.c
// `contextual`: the contextual type of the call expression (return type inference), may be null
Ty const* ty_call_return_type(Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual);
void ty_call_typecheck(Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual, Span call_span, ErrorCtx* errs);
Ty const* ty_awaited(Checker* me, Ty const* ty);

// ty_display.c
// The old display's `underline` mode underlines a type's whole rendering, so the `.types` traversal can do that from
// the string length when it switches over (stage 4); no display mode needed here.
typedef struct TyDisplayVisit {
    Ty const* ty;
    struct TyDisplayVisit const* outer;
} TyDisplayVisit;

typedef struct TyDisplay {
    cstr* target;
    u32 depth;
    TyDisplayVisit const* visiting; // The anonymous types being printed further up the stack
    // Literal types inside object types that came from expressions print as their primitive: tsc widens the
    // properties of an object literal when it types it, the legacy checker only when it reports. HACK
    bool widen_literals;
    // Where the type is printed from (tsc's enclosingDeclaration): names are qualified as far as needed to be reachable
    // from there (`T` inside namespace N, `N.T` outside, an import's name, `import("a").T` when nothing names it).
    // Null: always fully qualified, which is what error messages show
    struct Scope const* enclosing;
    // The types baseline's `^^^` line: every character printed gets a `^`, or a space where tsc's declaration
    // emitter would have reused a node from the source (`reused`, see display_signature()). Null: not wanted
    cstr* underline;
    bool reused;
    // The type parameters of an instantiated generic signature being printed: tsc gives it fresh ones, so nothing that
    // mentions them is reused inside of it
    TyList fresh_params;
} TyDisplay;
void ty_display(Checker* me, TyDisplay d, Ty const* ty);
void ty_display_signature(Checker* me, TyDisplay d, Signature const* sig);
void ty_display_predicate(Checker* me, TyDisplay d, TyPredicate const* predicate);
cstr ty_to_cstr(Checker* me, Ty const* ty);
