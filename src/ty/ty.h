#pragma once

// The new type core (agents/plans/02-type-core.md). Semantic types are `Ty`: immutable once published, allocated in
// the arena of a `Checker`, and interned, so pointer equality is type identity. The legacy `Type` struct is on its way
// to being syntax only (the future `TypeNode`); `ty_from_node()` in lower.c is the only producer of `Ty` from it.
//
// Nothing outside src/ty/ depends on this yet except the `--shadow-types` hook (shadow.c).

#include "type.h"

typedef struct Ty Ty;
typedef struct Checker Checker;
typedef struct Prop Prop;
typedef struct Signature Signature;
typedef struct Members Members;
typedef struct TyMapper TyMapper;
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
    TY_DECLARED_ENUM,
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
            Declaration const* enum_member; // null unless an enum literal
            Atom const* enum_name;
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
            TyList args;
            TyObjectState* state;
        } reference;
        struct {
            Type const* node; // TYPE_INTERFACE or TYPE_CALLABLE. Null for instantiations
            Type const* const* overloads; // If set, the type of an overloaded method: TYPE_CALLABLEs, node is the first
            u32 overload_count;
            Ty const* target; // Instantiations: the uninstantiated TY_ANONYMOUS
            TyMapper const* mapper;
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
            Ty const* if_true;
            Ty const* if_false;
        } conditional;
    };
};

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
};

typedef enum SignatureFlags {
    SIGNATURE_CONSTRUCT = 1 << 0,
    SIGNATURE_ABSTRACT  = 1 << 1,
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
    LowerCtx ctx;
    Signature const* target;
    TyMapper const* mapper;
    // Resolved lazily by signature_resolve()
    LazyState state;
    TyList type_params;
    SigParam const* params;
    u32 param_count;
    u32 min_args;
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
    signed char readonly; // -1 remove, +1 add
    signed char optional;
} MappedInfo;

struct Members {
    Prop* const* props; // declaration order
    u32 prop_count;
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

typedef struct DeclLinks {
    LazyState declared_state;
    Ty const* declared_type;
} DeclLinks;

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
    Arena arena;
    u32 next_type_id;
    Ty const* intrinsics[TY_INTRINSIC_COUNT];
    InternTable atoms;
    InternTable types;  // literals, unions, intersections, references, deferred, instantiated anonymous, tuple targets
    InternTable links;  // Declaration -> DeclLinks, AST node -> Ty, Generic -> type parameter list...
    Ty const* array_target;
    Ty const* readonly_array_target;
    u32 instantiation_depth;
    u32 instantiation_count;
    bool instantiation_limit_hit; // TODO(stage 3): report TS2589 where this gets set
    // --shadow-types bookkeeping, see shadow.c
    struct ShadowState* shadow;
};

// checker.c
Checker* checker_new(Program* program);
void checker_drop(Checker* me);
Atom const* checker_atom(Checker* me, csview sv);
static inline Ty const* ty_intrinsic(Checker const* me, TyIntrinsic i) { return my intrinsics[i]; }

// Intern key building. Keys are small, build them on the stack: u64 key[1 + n] = { n, ... }
void const** intern_slot(Checker* me, InternTable* table, u64 const* words, u32 count);

Ty* ty_alloc(Checker* me, TyKind kind, u32 flags);
TyList ty_list_copy(Checker* me, Ty const* const* data, u32 len);

Ty const* ty_literal(Checker* me, Value const* value, Declaration const* enum_member, Atom const* enum_name);
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
Ty const* ty_conditional(Checker* me, Ty const* check, Ty const* extends, Ty const* if_true, Ty const* if_false);

// members.c
Members const* ty_members(Checker* me, Ty const* ty); // Never null. Empty for non-object types
Prop const* ty_get_prop(Checker* me, Ty const* ty, Atom const* name);
Ty const* prop_type(Checker* me, Prop const* prop);
void signature_resolve(Checker* me, Signature const* sig);
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
TyList ty_type_params_of(Checker* me, Generic const* generic);
Ty const* ty_anonymous_from_node(Checker* me, Type const* node, LowerCtx ctx, Declaration const* alias, TyList alias_args);

// ty_display.c
// The old display's `underline` mode underlines a type's whole rendering, so the `.types` traversal can do that from
// the string length when it switches over (stage 4); no display mode needed here.
typedef struct TyDisplay {
    cstr* target;
    u32 depth;
} TyDisplay;
void ty_display(Checker* me, TyDisplay d, Ty const* ty);
cstr ty_to_cstr(Checker* me, Ty const* ty);

// shadow.c
bool ty_shadow_enabled(void);
void ty_shadow_set_enabled(bool enabled);
void ty_shadow_record_node(Type const* node);
void ty_shadow_record_declaration(Declaration const* decl);
void ty_shadow_begin(Program* program);
void ty_shadow_finish(Program* program);
