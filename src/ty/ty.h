#pragma once

// The new type core (agents/plans/02-type-core.md). Semantic types are `Ty`: immutable once published, allocated in
// the arena of a `Checker`, and interned, so pointer equality is type identity. The legacy `Type` struct is on its way
// to being syntax only (the future `TypeNode`); `ty_from_node()` in lower.c is the only producer of `Ty` from it.
//
// The legacy checker uses it for every type relation, and gets the types of classes, interfaces and enums from it
// (carried as TYPE_NEW, see legacy_view.c).

#include "type.h"
#include "lazy_type.h"
#include "generic.h"

typedef struct Ty Ty;
typedef struct Checker Checker;
typedef struct Prop Prop;
typedef struct Signature Signature;
typedef struct Members Members;
typedef struct TyMapper TyMapper;
typedef struct TyCondBranch TyCondBranch;
typedef struct GenericItem GenericItem;
typedef struct Program Program;

// Arena

typedef struct ArenaChunk ArenaChunk;
typedef struct Arena {
    ArenaChunk* head;
    size_t total; // bytes handed out, for measurements
} Arena;

void* arena_alloc(Arena* me, size_t size); // zeroed, 8 byte aligned
void arena_drop(Arena* me);
#define arena_new(arena, T) ((T*)arena_alloc(arena, sizeof(T)))
#define arena_new_n(arena, T, n) ((T*)arena_alloc(arena, sizeof(T) * (size_t)(n)))

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
    TYF_FRESH_LITERAL                = 1 << 26, // TODO(stage 4): fresh/regular literal pairs for widening

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
    // The legacy tree being lowered may be freed while the checker lives (a clone or temporary made during checking,
    // stage 3-4 scaffolding): nothing may keep a pointer into it, so object types are lowered eagerly and interned by
    // structure instead of by node. Parsed nodes and the memos of slotted LazyTypes are stable again. See lower.c
    bool transient;
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
    Type const* constraint_node;
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
    Declaration const* alias;
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
            Declaration const* enum_decl;
            Declaration const* enum_member;
        } literal;
        struct {
            Declaration const* decl; // null for synthesized ones
            Atom const* name;
            TyParamState* state;
        } param;
        struct {
            TyDeclaredKind kind;
            Declaration const* decl; // null for tuple targets and synthetics
            Scope const* namespace_scope; // TY_DECLARED_NAMESPACE only
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
            Type const* node; // TYPE_INTERFACE or TYPE_CALLABLE. Null for instantiations
            Type const* const* overloads; // If set, the type of an overloaded method: TYPE_CALLABLEs, node is the first
            u32 overload_count;
            Ty const* target; // Instantiations: the uninstantiated TY_ANONYMOUS
            TyMapper const* mapper;
            // An instantiation expression (`Bar<string>` as a value): target is any object type, and the mapper is
            // about the type parameters of its signatures, which lose them
            bool applies_type_args;
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
        } conditional;
    };
};

struct TyCondBranch {
    Ty const* type; // null until lowered if `node` is set
    Type const* node;
    LowerCtx ctx;
    TyMapper const* mapper;
    LazyState state;
};

typedef struct TyDeferredRef {
    Type const* node; // TYPE_ARRAY or TYPE_TUPLE
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
    Declaration const* parent; // the declared type it was declared in, may be null
    // Exactly one source of the type: a node to lower, a legacy type to lower, or a target to instantiate
    Type const* node;
    Type const* const* overloads; // An overloaded method: all of its TYPE_CALLABLE nodes, node being the first
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
} SigParam;

struct Signature {
    u32 flags;
    CallableType const* node;
    // Instead of `node`: a legacy type (the LazyType of a constructor, in an arena wrapper) that evaluates to the
    // callable when the signature is resolved
    Type const* lazy_node;
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
    // that the legacy checker may be in the middle of evaluating
    Type const* ret_node;
    LazyState ret_state;
    Ty const* ret;
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

typedef struct DeclLinks {
    LazyState declared_state;
    Ty const* declared_type;
} DeclLinks;

typedef enum TyRelation {
    TY_RELATION_ASSIGNABLE,
    TY_RELATION_COMPARABLE,
    // Assignable, except that null and undefined are never assignable to other types, whatever strictNullChecks says.
    // HACK(stage 4b): what the legacy operator checks mean by "extends number"
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
    TY_NON_PRIMITIVE,
    TY_INTRINSIC_COUNT,
} TyIntrinsic;

struct Checker {
    Program* program;
    LazySlots lazy; // The memos of every LazyType in the AST and in Declarations, see lazy_type.h
    Arena arena;
    u32 next_type_id;
    Ty const* intrinsics[TY_INTRINSIC_COUNT];
    InternTable atoms;
    InternTable types;  // literals, unions, intersections, references, deferred, instantiated anonymous, tuple targets
    InternTable links;  // Declaration -> DeclLinks, AST node -> Ty, Generic -> type parameter list...
    Ty const* array_target;
    Ty const* readonly_array_target;
    Ty const* global_types[8]; // GlobalType in relate.c: String, Number... looked up on first use
    bool global_types_resolved[8];
    u32 instantiation_depth;
    u32 instantiation_count;
    bool instantiation_limit_hit; // Set when ty_instantiate() gave up; whoever reports errors turns it into TS2589
    RelationCache relations[TY_RELATION_COUNT];
    struct RelationState* relating; // The relation check in progress, if any (relate.c)
    u32 variance_depth; // Variances are being measured, which is only reliable while the marker types are compared
    bool variance_unreliable; // structurally: set when they go through something that isn't (relate.c)
    Type** views; // Ty id -> legacy view, see legacy_view.c
    u32 views_len;
    // --shadow-types bookkeeping, see shadow.c
    struct ShadowState* shadow;
};

// checker.c
Checker* checker_new(Program* program);
void checker_drop(Checker* me);

// The checker that legacy code without a context parameter works for: lazy_type_get() uses its slots (through
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

Ty const* ty_literal(Checker* me, Value const* value);
Ty const* ty_enum_literal(Checker* me, Value const* value, Declaration const* enum_decl, Declaration const* member);
Ty const* ty_enum_computed_member(Checker* me, Declaration const* enum_decl, Declaration const* member);
Ty const* ty_string_literal(Checker* me, csview sv);
Ty const* ty_union(Checker* me, Ty const* const* types, u32 len, Declaration const* alias, TyList alias_args);
Ty const* ty_intersection(Checker* me, Ty const* const* types, u32 len, Declaration const* alias, TyList alias_args);
Ty const* ty_type_param(Checker* me, Declaration const* decl);
Ty const* ty_synthetic_type_param(Checker* me, csview name);
Ty const* ty_reference(Checker* me, Ty const* target, TyList args);
Ty const* ty_array(Checker* me, Ty const* element, bool readonly);
Ty const* ty_tuple_target(Checker* me, TupleShape const* shape);
Ty const* ty_keyof(Checker* me, Ty const* operand);
Ty const* ty_indexed(Checker* me, Ty const* object, Ty const* index);
Ty const* ty_conditional(
    Checker* me, Ty const* check, Ty const* extends, TyCondBranch const* if_true, TyCondBranch const* if_false,
    bool distributive
);
Ty const* ty_cond_branch_type(Checker* me, TyCondBranch* branch);
static inline Ty const* ty_conditional_branch(Checker* me, Ty const* conditional, bool which) {
    return ty_cond_branch_type(me, &conditional->conditional.branches[which]);
}

// members.c
Members const* ty_members(Checker* me, Ty const* ty); // Never null. Empty for non-object types
Prop const* ty_get_prop(Checker* me, Ty const* ty, Atom const* name);
Ty const* prop_type(Checker* me, Prop const* prop);
void signature_resolve(Checker* me, Signature const* sig);
Ty const* signature_return_type(Checker* me, Signature const* sig);
TyList ty_reference_args(Checker* me, Ty const* reference);
Ty const* ty_param_constraint(Checker* me, Ty const* param); // null if unconstrained
Ty const* ty_param_default(Checker* me, Ty const* param); // null if none

// instantiate.c
TyMapper const* ty_mapper_new(Checker* me, TyList sources, TyList targets);
Ty const* ty_instantiate(Checker* me, Ty const* ty, TyMapper const* mapper);

// lower.c
Ty const* ty_from_node(Checker* me, Type const* node, LowerCtx ctx);
// Like ty_from_node(), but also follows the legacy checker's TYPE_LAZY/TYPE_REF (stage 1-4 scaffolding)
Ty const* ty_from_legacy(Checker* me, Type const* type, LowerCtx ctx);
Ty const* ty_declared_type_of(Checker* me, Declaration const* decl);
Atom const* ty_qualified_name(Checker* me, Declaration const* decl); // `A.B.name`
void ty_set_declared_type(Checker* me, Declaration const* decl, Ty const* type);
TyList ty_type_params_of(Checker* me, Generic const* generic);
Ty const* ty_anonymous_from_node(Checker* me, Type const* node, LowerCtx ctx, Declaration const* alias, TyList alias_args);

// legacy_view.c: legacy code carrying and consuming `Ty` (TYPE_NEW), plan 02 stage 4 scaffolding
Ty const* type_new_ty(Checker* me, Type const* type_new); // The Ty of a TYPE_NEW, resolving its lazy forms
void ty_legacy_views_drop(Checker* me);
Type const* ty_legacy_view(Checker* me, Ty const* ty); // One structural layer as a legacy type, owned by the checker
Ty const* ty_instantiate_with_refinements(Checker* me, Ty const* ty, VecTypeRefinement const* refinements);
static inline Type type_new_prop(Prop const* prop) { return (Type){ .tag = TYPE_NEW, .new_.prop = prop }; }
static inline Type type_new_return(Signature const* sig) { return (Type){ .tag = TYPE_NEW, .new_.ret_of = sig }; }

// Which declarations' types the legacy checker gets from the new core (as TYPE_NEW) instead of evaluating them itself
bool ty_declaration_is_evaluated_by_new_core(Declaration const* decl);
Ty const* ty_from_identifier_type(Checker* me, IdentifierType const* identifier); // An identifier type evaluated by legacy code

// enums.c
Ty const* ty_resolve_enum(Checker* me, Declaration const* decl); // Only ty_declared_type_of() should call this
Ty const* ty_enum_of_member(Checker* me, Ty const* member); // The enum type an enum literal belongs to
bool ty_is_enum_union(Ty const* ty);

// relate.c
bool ty_is_related(Checker* me, Ty const* source, Ty const* target, TyRelation relation);
// Why source isn't related to target, outermost line first. The first line is the natural head of the error: TS2322
// "Type 'S' is not assignable to type 'T'." unless something more specific replaces it (TS2741 and friends)
void ty_explain_unrelated(Checker* me, Ty const* source, Ty const* target, TyRelation relation, VecTyReportLine* out);
Ty const* ty_widened_literal(Checker* me, Ty const* ty);
void relation_cache_drop(RelationCache* me);
// conditional.c
// Null while the conditional type has to stay deferred
Ty const* ty_conditional_resolve(
    Checker* me, Ty const* check_type, Ty const* extends, TyCondBranch const* if_true, TyCondBranch const* if_false
);

// ty_display.c
// The old display's `underline` mode underlines a type's whole rendering, so the `.types` traversal can do that from
// the string length when it switches over (stage 4); no display mode needed here.
typedef struct TyDisplay {
    cstr* target;
    u32 depth;
    // Literal types inside object types that came from expressions print as their primitive: tsc widens the
    // properties of an object literal when it types it, the legacy checker only when it reports. HACK(stage 4b)
    bool widen_literals;
    bool unqualified_names; // See out_name() in ty_display.c
} TyDisplay;
void ty_display(Checker* me, TyDisplay d, Ty const* ty);
void ty_display_signature(Checker* me, TyDisplay d, Signature const* sig);
cstr ty_to_cstr(Checker* me, Ty const* ty);

// shadow.c
bool ty_shadow_enabled(void);
void ty_shadow_set_enabled(bool enabled);
void ty_shadow_record_node(Type const* node);
void ty_shadow_record_declaration(Declaration const* decl);
void ty_shadow_begin(Program* program);
void ty_shadow_finish(Program* program);
