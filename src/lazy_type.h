#pragma once

#include "type.h"
#include "arena.h"
#include "ty/taint.h"
#include <stdatomic.h>

// The memo of a LazyType: whether evaluation has started (to detect cycles) and the result. Types are held by id
// (Ty.id, 0 for none; lazy_memo_evaluated() and lazy_memo_context() look them up): a big program has a memo for
// most of its expressions (vscode: 10M), 12 bytes each instead of 24
typedef struct LazyMemo {
    u32 evaluated;
    // Expressions: the contextual type `evaluated` was computed with, see ty_check_expr()
    u32 context;
    bool evaluation_started : 1;
    bool is_self_referential : 1;
    bool done : 1;
    // A destructuring pattern found circular once: not remembered, see lazy_type_evaluate_inner()
    bool pattern_retried : 1;
    // --shared-graph: its evaluation was tainted (ty/graph.c), so reading it is a taint event (lazy_type_evaluate())
    bool tainted : 1;
    // Call expressions: tsc's getEffectsSignature found nothing for it as a statement's call / an expression's (flow.c
    // call_effects(): read at every flow query that walks past it, a memo next to the type's is cheaper than node links)
    bool no_call_effects : 1;
    bool no_call_effects_in_expr : 1;
    // Expressions: re-entrant evaluations so far. (In-flight state, but an expression's memo is the checker's own,
    // never shared: a declaration's in-flight state is its RESOLVING_DECLARATION record, lazy_type_evaluate_inner())
    unsigned char hits;
#ifdef TTTT_SURVIVORS
    // ty/survivors.c's file census: what the memo is of (LAZY_CENSUS_*) and the file it was first touched for
    unsigned char census_tag;
    u32 census_owner;
#endif
} LazyMemo;
#ifdef TTTT_SURVIVORS
enum { LAZY_CENSUS_EXPR = 1, LAZY_CENSUS_LOCAL, LAZY_CENSUS_DECL, LAZY_CENSUS_LOOP, LAZY_CENSUS_FIELD, LAZY_CENSUS_OTHER };
#endif

typedef struct LazyType {
    enum {
        // Makes it easy to show a warning if a LazyType is accidentally zero-initialized, otherwise shouldn't be used
        LAZY_TYPE_UNINITIALIZED = 0,

        // Nothing to evaluate: any (expr_implicit_any())
        LAZY_TYPE_EVALUATED,

        // The rest means this LazyType is part of one of the following structs
        LAZY_TYPE_EXPR,
        LAZY_TYPE_INITIALIZED_BINDING,
        LAZY_TYPE_FOR_EACH_LOOP_VAR,
        LAZY_TYPE_CLASS_ITEM_FIELD,
        LAZY_TYPE_CLASS_ITEM_ACCESSOR,
    } tag;
    // Where the memo is: a slot in the current Checker, so that typechecking never writes to the AST, and programs
    // that share a lib AST don't share checker state. 0: not bound yet (evaluating is a bug)
    u32 slot;
} LazyType;

// Slot numbers are process-wide and never reused, see hacks.md
u32 lazy_slot_new(void);
// The thread's next slot starts a memo page of its own: called when a file's parse starts (and its bodies'
// materialization), so that a page holds one file's slots
void lazy_slots_new_block(void);

// The slot memos of one Checker. Three levels, allocated on first touch: most programs touch a tiny part of the lib
#define LAZY_PAGE_BITS 8
#define LAZY_DIR_BITS 10
#define LAZY_TOP_BITS 12
// The checker's types by id (two levels, chunks allocated as types are made, see lazy_slots_add_type())
#define LAZY_TYPE_CHUNK_BITS 16
#define LAZY_TYPE_DIR_BITS 16
typedef struct LazySlots {
    LazyMemo** top[1 << LAZY_TOP_BITS];
    // The chunks of the program's TypeDirectory this checker reads: its own (it makes their types) and, with
    // --shared-graph, the other checkers' (lazy_slots_sync()). A copy of the directory's pointers, so that reading an
    // id costs what it did when the table was the checker's own
    struct Ty const** types[1 << LAZY_TYPE_DIR_BITS];
    struct TypeDirectory* directory;
    u32* own; // The chunks it owns (its id blocks), freed with it
    u32 own_len, own_cap;
    // The checker's arena, which the memo pages and directories are bumped out of (they die with the checker): as
    // malloc they grew each checker thread's glibc heap a page at a time (a non-main arena's heap has no top pad: an
    // mprotect() a page, 7k on vscode, each taking the mm lock for writing). Null: malloc
    // (checker_memory() counts them in the arena's total)
    Arena* arena;
} LazySlots;

// The program's types by id (plan 86 stage 4c): ids come in blocks of a chunk (65536) from one atomic counter, so the
// types one checker makes stay in creation order and every id is the program's; a block's chunk belongs to the checker
// that took it (freed when it is dropped, like the per-checker table this replaces)
typedef struct TypeDirectory {
    _Atomic u32 next_block;
    struct Ty const** _Atomic chunks[1 << LAZY_TYPE_DIR_BITS];
    // --shared-graph: the checkers' tables, each told of a new chunk when it is made (before any of its types can be
    // published), so that reading another checker's id costs nothing more
    struct LazySlots* readers[64];
    _Atomic u32 reader_count;
} TypeDirectory;

TypeDirectory* type_directory_new(void);
void type_directory_drop(TypeDirectory* me); // After its checkers
// The first id of a fresh block
u32 type_directory_block(TypeDirectory* me);

// Every type the checker makes is added, by its id
void lazy_slots_add_type(LazySlots* me, struct Ty const* ty);
// Copies the directory's chunks it doesn't have yet (--shared-graph: another checker's types are read)
void lazy_slots_sync(LazySlots* me);
// --shared-graph: the table reads every checker's ids from now on (each new chunk is put in it as it is made)
void lazy_slots_share(LazySlots* me);

#ifdef TTTT_DEBUG_IDS
void lazy_slots_missing(LazySlots const* me, u32 id); // (lazy_type.c: aborts saying why)
static inline struct Ty const* lazy_slots_type(LazySlots const* me, u32 id) {
    if (!id) return nullptr;
    struct Ty const** chunk = __atomic_load_n(&my types[id >> LAZY_TYPE_CHUNK_BITS], __ATOMIC_ACQUIRE);
    struct Ty const* ret = chunk ? __atomic_load_n(&chunk[id & ((1 << LAZY_TYPE_CHUNK_BITS) - 1)], __ATOMIC_ACQUIRE) : nullptr;
    if (!ret) lazy_slots_missing(me, id);
    return ret;
}
#else
// (--shared-graph: a chunk and its slots are another worker's hand-off, stored with release, lazy_slots_add_type(); an
// acquire load is a plain one on x86)
static inline struct Ty const* lazy_slots_type(LazySlots const* me, u32 id) {
    if (!id) return nullptr;
    struct Ty const** chunk = __atomic_load_n(&my types[id >> LAZY_TYPE_CHUNK_BITS], __ATOMIC_ACQUIRE);
    return __atomic_load_n(&chunk[id & ((1 << LAZY_TYPE_CHUNK_BITS) - 1)], __ATOMIC_ACQUIRE);
}
#endif

void lazy_slots_drop(LazySlots* me);

// The slots of the current checker (see current_checker in ty/ty.h), null when nothing is being typechecked
extern thread_local LazySlots* current_lazy_slots;

static inline struct Ty const* lazy_memo_evaluated(LazyMemo const* me) {
    return lazy_slots_type(current_lazy_slots, my evaluated);
}

static inline struct Ty const* lazy_memo_context(LazyMemo const* me) {
    return lazy_slots_type(current_lazy_slots, my context);
}

// Should only be called via lazy_type_get()
struct Ty const* lazy_type_evaluate(LazyType* me);

// The memo if there is one already (without evaluating or allocating anything), otherwise null
static inline LazyMemo* lazy_type_state_peek(LazyType* me) {
    // (slot 0 isn't one: lazy_type_evaluate() refuses it, rather than share one memo between everything unbound)
    if (!current_lazy_slots || !my slot) return nullptr;
    LazyMemo** dir = current_lazy_slots->top[my slot >> (LAZY_PAGE_BITS + LAZY_DIR_BITS)];
    if (!dir) return nullptr;
    LazyMemo* page = dir[(my slot >> LAZY_PAGE_BITS) & ((1 << LAZY_DIR_BITS) - 1)];
    return page ? &page[my slot & ((1 << LAZY_PAGE_BITS) - 1)] : nullptr;
}

void lazy_type_drop(LazyType* me);
// The memo, allocated if it wasn't (the slot must be bound)
LazyMemo* lazy_type_memo(LazyType* me);

// Stores a result computed by someone who knows more than lazy_type_evaluate() would (the contextual type of an
// expression). Replaces the memo if there is one
void lazy_type_set(LazyType* me, struct Ty const* result, struct Ty const* context);

// Makes the LazyType evaluate whatever contains it, according to tag, keeping its slot if it has one
static inline void lazy_type_bind(LazyType* me, int tag) {
    my tag = tag;
    if (!my slot) my slot = lazy_slot_new();
}

static inline struct Ty const* lazy_type_get(LazyType* me) {
    LazyMemo* state = my tag == LAZY_TYPE_EVALUATED ? nullptr : lazy_type_state_peek(me);
#ifdef TTTT_TAINT
    if (state && state->done) TAINT_READ(my tag == LAZY_TYPE_EXPR ? TK_LAZY_EXPR : TK_LAZY_DECL + my tag, me);
#endif
    // (--shared-graph: a declaration whose evaluation was tainted is this worker's own, reading it is a taint event,
    // lazy_type_evaluate_inner(); the bit is never set without a graph)
#ifdef TTTT_HUB_TRACE
    void hub_trace_hit(void const* key);
    if (state && state->done && !state->tainted) hub_trace_hit(me);
#endif
    return state && state->done && !state->tainted ? lazy_memo_evaluated(state) : lazy_type_evaluate(me);
}
