#pragma once

// Plan 86 stage 4, track S: the type graph checkers share (`--shared-graph`), for now only for checkers run one after
// another (`--threads 1 --checker-memory off`). APPROACH.md at the repository root has the design; in short:
// - atoms, type ids and the id -> Ty table are handed over to whichever checker is current (graph_enter(), from
//   checker_make_current()), as are the lib singletons made on first use. In 4d: a global atom table, an atomic id
//   counter and a CAS-allocated chunk directory, the singletons made before the workers start
// - the intern tables (types, links, intersection_props) and node_links have a shared counterpart each, looked up
//   after the checker's own on a miss (InternTable.shared, graph_link_get()). A checker makes a private entry only
//   when the shared table has none, so the two never disagree
// - a checker's private entries are published when its share ends (graph_publish()): identity kinds always, memo
//   kinds when their computation was clean, scratch kinds never. Sequentially that gives the next checker what
//   publishing at each computation's end would; in 4d graph_publish_entry() moves to the computation's end, as a CAS
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
