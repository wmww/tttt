#pragma once

// Plan 86 stage 4, track S: the type graph checkers share (`--shared-graph`), for now only for checkers run one after
// another (`--threads 1 --checker-memory off`). APPROACH.md at the repository root has the design; in short:
// - atoms, type ids and the id -> Ty table are handed over to whichever checker is current (graph_enter(), from
//   checker_make_current()), as are the lib singletons made on first use. In 4d: a global atom table, an atomic id
//   counter and a CAS-allocated chunk directory, the singletons made before the workers start
// - the intern tables (types, links, intersection_props) and node_links have a shared counterpart each, looked up
//   after the checker's own on a miss (InternTable.shared, graph_link_get()). A checker makes a private entry only
//   when the shared table has none, so the two never disagree
// - identity kinds are published at creation (intern_set()), memo kinds when their value is there (graph_memo_flush(),
//   first wins), scratch kinds never; variances on threads through a slot per subject (graph_variance_slot())
// - declaration LazyType memos: a shared memo store by slot, taken before evaluating (graph_memo_take())
// - nothing of a checker is freed before the program's end, so what a shared type points to stays

#include "ty/ty.h"

typedef struct TyGraph TyGraph;
struct LazyMemo;

// The class of a KEY_* kind (and of the intersection_props table's untagged keys, GRAPH_KIND_INTERSECTION_PROPS)
typedef enum GraphClass {
    GRAPH_SCRATCH, // Per expression, flow, inference, report-once, per-checker numbering: never shared
    GRAPH_IDENTITY, // A type's or a symbol's/node's identity (constructors, roots): always shared
    GRAPH_MEMO, // A computed value: shared when its computation was clean
} GraphClass;

enum { GRAPH_KIND_INTERSECTION_PROPS = 254 };

GraphClass graph_kind_class(u32 kind);

// Made by program_typecheck() before its other checkers, from its first one (current, holding the state)
TyGraph* graph_new(Checker* first);
void graph_drop(TyGraph* me); // After every checker
// Makes the checker the graph's current holder (checker_make_current()); null: none
void graph_enter(TyGraph* me, Checker* checker);
void graph_set_concurrent(TyGraph* me, bool concurrent); // Checkers on threads of their own: no hand-over
// A new checker of the graph (checker_new()): the first one's intrinsics, the graph's tables as its shared ones
void graph_adopt(TyGraph* me, Checker* checker);
// Before checker_drop() frees what it doesn't own
void graph_release(TyGraph* me, Checker* checker);

// Publishes what the checker computed (the end of its share)
void graph_publish(TyGraph* me, Checker* checker);

// node_links' (ty_link_get())
void const* graph_link_get(TyGraph const* me, u32 kind, void const* subject);

// Declaration memos (lazy_type.c): whether the shared store had a done memo for the slot (copied into `memo`)
bool graph_memo_take(TyGraph* me, u32 slot, struct LazyMemo* memo);
// A declaration memo finished in the checker: published with the rest, if clean
void graph_memo_done(Checker* checker, u32 slot, bool tainted);

// Memo kinds published when their computation ends (plan 86 4d step 4): a memo kind's new private intern entry is
// logged (graph_memo_log_put(), from intern_put()), and graph_memo_flush() inserts the logged entries that have their
// value into the shared tables (first wins: a loser's entry takes the winner's value), every few dozen entries, at
// each file's end and at the share's. Node link memos (ty_link_set()) and declaration memos (graph_memo_done()) go at
// once. TTTT_GRAPH_MEMO_EAGER=0: all of them at the share's end, as in track S
bool graph_memo_eager(void);
bool graph_memo_sharing(Checker const* checker); // Eager, and another checker to read them
bool graph_memo_late(u32 kind);
void graph_memo_log_put(Checker* checker, InternTable const* table, void const** block, u32 kind);
void graph_memo_log_unput(Checker* checker, void const** block); // The last put, its value found shared
void graph_memo_flush(Checker* checker);

// Variances on threads (plan 86 4d): one measurement per type or alias for the program, like tsc's, whoever asks
// first. A slot per subject (the declared type or the alias symbol), claimed with LAZY_CLAIM() by the worker that
// measures it, which every other one waits for (LAZY_SNAP()); claimed slots go done as one group when the claiming
// worker's outermost measurement ends (graph_variances_publish()). Null without threads
typedef struct VarianceSlot {
    unsigned state; // LazyState, owner-tagged
    void const* value; // The Variances once done
    void const* subject;
    bool alias;
} VarianceSlot;
unsigned* graph_variance_lock(Checker* checker); // Owner-tagged like a lazy state: one measuring worker at a time
VarianceSlot* graph_variance_slot(Checker* checker, void const* subject, bool alias, bool create);
void graph_variance_claimed(Checker* checker, VarianceSlot* slot);
// The outermost measurement ended: each claimed slot gets `value_of`'s answer (null: not started again)
void graph_variances_publish(Checker* checker, void const* (*value_of)(Checker* checker, VarianceSlot const* slot));

void graph_check_unshared(TyGraph* me, Checker* checker); // TTTT_DEBUG_UNSHARED=1 (program.c)
void graph_print_stats(TyGraph const* me);
// The shared tables' insertion and node_links' (checker.c)
void link_table_each(LinkTable const* table, void (*each)(void* arg, u32 kind, void const* subject, void const* value), void* arg);
// The cheap taint's intern hooks (checker.c, only for tables with a shared counterpart): a private miss that may start
// a computation, a put (clean if no event since the miss, else marked; pending until the next event if its value isn't
// there yet), a hit of a private entry (tainted: an event)
void graph_taint_miss(InternTable const* table, u64 hash);
void graph_taint_put(InternTable const* table, u64 hash, void const** block, u32 kind);
void graph_taint_read_block(void const* const* block);
void graph_count_lost(TyGraph* me, u32 kind); // An identity another checker published first (intern_publish())
void const* graph_link_insert(TyGraph* me, u32 kind, void const* subject, void const* value); // First wins
void graph_count_shared_hit(InternTable const* table, u64 const* words, u32 count); // (checker.c)

// A redo of a file after a broken wait cycle (plan 86 4d): whether one may start (threads on, room for a fresh worker,
// TTTT_GRAPH_REDO=0 not set), and the giving-up checker retired: its in-flight fields are reset by whoever meets them
bool graph_can_redo(TyGraph const* me);
void graph_retire(TyGraph* me, Checker* checker);
