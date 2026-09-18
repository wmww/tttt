#pragma once

#include "type.h"

// The memo of a LazyType: whether evaluation has started (to detect cycles) and the result
typedef struct LazyMemo {
    bool evaluation_started;
    bool is_self_referential;
    bool done;
    struct Ty const* evaluated;
    // Expressions: the contextual type `evaluated` was computed with, see ty_check_expr()
    struct Ty const* context;
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
        LAZY_TYPE_MERGED,
        LAZY_TYPE_SCOPE_VALUE_INTERFACE,
    } tag;
    // Where the memo is: a slot in the current Checker, so that typechecking never writes to the AST, and programs
    // that share a lib AST don't share checker state. 0: not bound yet (evaluating is a bug)
    u32 slot;
    // LAZY_TYPE_EVALUATED: the type, as a node to lower
    TypeNode preevaluated;
} LazyType;

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
struct Ty const* lazy_type_evaluate(LazyType* me);

// The memo if there is one already (without evaluating or allocating anything), otherwise null
static inline LazyMemo* lazy_type_state_peek(LazyType* me) {
    if (!current_lazy_slots) return nullptr;
    LazyMemo** dir = current_lazy_slots->top[my slot >> (LAZY_PAGE_BITS + LAZY_DIR_BITS)];
    if (!dir) return nullptr;
    LazyMemo* page = dir[(my slot >> LAZY_PAGE_BITS) & ((1 << LAZY_DIR_BITS) - 1)];
    return page ? &page[my slot & ((1 << LAZY_PAGE_BITS) - 1)] : nullptr;
}

void lazy_type_drop(LazyType* me);

// Stores a result computed by someone who knows more than lazy_type_evaluate() would (the contextual type of an
// expression). Replaces the memo if there is one
void lazy_type_set(LazyType* me, struct Ty const* result, struct Ty const* context);

// Makes a LazyType that evaluates whatever contains it, according to tag
static inline LazyType lazy_type_init(int tag) {
    return (LazyType){ .tag = tag, .slot = lazy_slot_new() };
}

// Same, but in place and keeping the slot if there is one
static inline void lazy_type_bind(LazyType* me, int tag) {
    my tag = tag;
    if (!my slot) my slot = lazy_slot_new();
}

static inline LazyType lazy_type_preeval(TypeNode type) {
    return (LazyType){
        .tag = LAZY_TYPE_EVALUATED,
        .preevaluated = type,
    };
}

static inline struct Ty const* lazy_type_get(LazyType* me) {
    LazyMemo* state = my tag == LAZY_TYPE_EVALUATED ? nullptr : lazy_type_state_peek(me);
    return state && state->done ? state->evaluated : lazy_type_evaluate(me);
}
