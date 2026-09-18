#pragma once

#include "type.h"

// The memo of a LazyType: whether evaluation has started (to detect cycles) and the result
typedef struct LazyMemo {
    bool evaluation_started;
    bool is_self_referential;
    bool done;
    Type evaluated;
    // Expressions: the contextual type `evaluated` was computed with (a Ty), see ty_check_expr()
    void const* context;
} LazyMemo;

typedef struct LazyType {
    enum {
        // Makes it easy to show a warning if a LazyType is accidentally zero-initialized, otherwise shouldn't be used
        LAZY_TYPE_UNINITIALIZED = 0,

        // The type was known when this LazyType was created (see lazy_type_preeval())
        LAZY_TYPE_EVALUATED,

        // The rest means this LazyType is part of one of the following structs
        LAZY_TYPE_EXPR,
        LAZY_TYPE_INITIALIZED_BINDING,
        LAZY_TYPE_FOR_EACH_LOOP_VAR,
        LAZY_TYPE_FUNCTION,
        LAZY_TYPE_CLASS_INSTANCE,
        LAZY_TYPE_CLASS_CLASS,
        LAZY_TYPE_CLASS_ITEM_FIELD,
        LAZY_TYPE_CLASS_ITEM_ACCESSOR,
        LAZY_TYPE_ENUM_INSTANCE,
        LAZY_TYPE_ENUM_INTERFACE,
        LAZY_TYPE_INTERFACE_DECL,
        LAZY_TYPE_IDENTIFIER_TYPE,
        LAZY_TYPE_MERGED,
        LAZY_TYPE_SCOPE_VALUE_INTERFACE,
    } tag;
    // Where the memo is. 0: not bound yet (evaluating is a bug). LAZY_SLOT_INLINE: in `memo` below, which is only for
    // LazyTypes that aren't part of the AST or a Declaration (the ones inside types cloned during typechecking, which
    // live and die within one checker). Anything else: a slot in the current Checker, so that typechecking never
    // writes to the AST, and programs that share a lib AST don't share checker state
    u32 slot;
    // `memo.evaluated` is also where the type of a LAZY_TYPE_EVALUATED lives (put there when the LazyType was created,
    // never written during typechecking). Should be {0} otherwise
    LazyMemo memo;
} LazyType;

#define LAZY_SLOT_INLINE UINT32_MAX

// Slot numbers are process-wide and never reused, see hacks.md
u32 lazy_slot_new(void);

// The slot memos of one Checker. Three levels, allocated on first touch: most programs touch a tiny part of the lib
#define LAZY_PAGE_BITS 8
#define LAZY_DIR_BITS 10
#define LAZY_TOP_BITS 12
typedef struct LazySlots {
    LazyMemo** top[1 << LAZY_TOP_BITS];
} LazySlots;

void lazy_slots_drop(LazySlots* me);

// The slots of the current checker (see current_checker in ty/ty.h), null when nothing is being typechecked
extern thread_local LazySlots* current_lazy_slots;

// Should only be called via lazy_type_get()
Type const* lazy_type_evaluate(LazyType* me);

// The memo if there is one already (without evaluating or allocating anything), otherwise null
static inline LazyMemo* lazy_type_state_peek(LazyType* me) {
    if (my slot == LAZY_SLOT_INLINE) return &my memo;
    if (!current_lazy_slots) return nullptr;
    LazyMemo** dir = current_lazy_slots->top[my slot >> (LAZY_PAGE_BITS + LAZY_DIR_BITS)];
    if (!dir) return nullptr;
    LazyMemo* page = dir[(my slot >> LAZY_PAGE_BITS) & ((1 << LAZY_DIR_BITS) - 1)];
    return page ? &page[my slot & ((1 << LAZY_PAGE_BITS) - 1)] : nullptr;
}

void lazy_type_drop(LazyType* me);

// Stores a result computed by someone who knows more than lazy_type_evaluate() would (the contextual type of an
// expression). Replaces the memo if there is one
void lazy_type_set(LazyType* me, Type result, void const* context);

// Makes a LazyType that evaluates whatever contains it, according to tag
static inline LazyType lazy_type_init(int tag) {
    return (LazyType){ .tag = tag, .slot = lazy_slot_new() };
}

// Same, but in place and keeping the slot if there is one
static inline void lazy_type_bind(LazyType* me, int tag) {
    my tag = tag;
    if (!my slot) my slot = lazy_slot_new();
}

// For LazyTypes inside types that are created and dropped during typechecking
static inline LazyType lazy_type_init_inline(int tag) {
    return (LazyType){ .tag = tag, .slot = LAZY_SLOT_INLINE };
}

static inline LazyType lazy_type_preeval(Type type) {
    return (LazyType){
        .tag = LAZY_TYPE_EVALUATED,
        .memo.evaluated = type,
    };
}

static inline Type const* lazy_type_get(LazyType* me) {
    if (my tag == LAZY_TYPE_EVALUATED) {
        return &my memo.evaluated;
    }
    LazyMemo* state = lazy_type_state_peek(me);
    return state && state->done ? &state->evaluated : lazy_type_evaluate(me);
}
