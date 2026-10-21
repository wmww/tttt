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
#include "ty/overlay.h"

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
char const* graph_kind_name(u32 kind); // KEY_* by name

// TTTT_GRAPH_SEED=N (graph_sched.c): one worker runs at a time, handing off in a seeded order at the scheduling points
extern bool graph_sched_on;
void graph_sched_init(u32 threads); // before the threads start
void graph_sched_enter(void); // a worker thread starts (waits for its turn)
void graph_sched_leave(void); // and is done
void graph_sched_point_slow(bool waiting);
void graph_sched_print(void);
// A scheduling point: `waiting` for another worker (the token always goes on then)
static inline void graph_sched_point(bool waiting) {
    if (__builtin_expect(graph_sched_on, 0)) graph_sched_point_slow(waiting);
}

#ifdef TTTT_GRAPH_FINGERPRINT
// The fingerprint-at-publish oracle (graph_debug.c): record mode for the K1 check, then check mode for the workers
void graph_fp_start(bool record);
bool graph_fp_on(void);
void graph_fp_memo(u32 kind, void const** block, u64 const* words, u32 count); // a memo entry published
void graph_fp_record_tables(Checker* checker); // the K1 checker's memo entries, at its end
void graph_fp_print(void);
void graph_fp_relation(u32 relation, u64 key, unsigned value); // a relation result shared (relation_publish())
#endif

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
// Plan 86 stage 4f: a memo kind's entry is published only if its computation was clean. Its computation starts at the
// private and shared miss of its key (graph_memo_miss(), from intern_shared_find()) and ends when its value is there:
// tainted if the taint counter moved between the miss and the put, or while the put entry had no value yet (an event
// sweeps the log, graph_memo_sweep()). A tainted entry stays the checker's own, and a hit of it is an event
// (graph_memo_hit(), through InternTable.taint). Every put is logged while the checker is on a graph; published only
// once others read them (graph_memo_sharing())
void graph_memo_log_put(Checker* checker, InternTable const* table, void const** block, u32 kind);
void graph_memo_miss(InternTable const* table, u64 hash, u64 const* words, u32 count);
void graph_memo_sweep(Checker* checker); // A taint event (overlay.c's graph_taint_bump())
enum { MEMO_MISS_SLOTS = 4096, MEMO_FLOOR_SLOTS = 1024, MEMO_TAINT_BITS = 1 << 16 };
typedef struct MemoMiss {
    void const* table;
    u64 hash;
    u64 start;
    u32 gen; // (MemoTaint.gen when made: older ones are forgotten)
} MemoMiss;
typedef struct MemoTaint {
    u64 bits[MEMO_TAINT_BITS / 64]; // The tainted entries' bitmap by block address (or node link key), in front of
    uintptr_t* blocks; // the exact set, open addressing
    u32 cap, len;
    u32 gen; // Bumped at each file's start
    MemoMiss misses[MEMO_MISS_SLOTS];
    u64 floor_start[MEMO_FLOOR_SLOTS]; // The oldest start of a miss pushed out to the bucket, if floor_gen is gen
    u32 floor_gen[MEMO_FLOOR_SLOTS];
    void const* const** pending; // Entries put since the last event, clean so far
    u32 pending_len, pending_cap;
} MemoTaint;
static inline u64 memo_taint_mix(u64 h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return h;
}
static inline bool memo_taint_maybe(MemoTaint const* t, uintptr_t key) {
    u64 h = memo_taint_mix((u64)key);
    return t->bits[(h >> 6) & (MEMO_TAINT_BITS / 64 - 1)] >> (h & 63) & 1;
}
void graph_memo_hit_slow(MemoTaint const* taint, uintptr_t key);
static inline void graph_memo_hit(MemoTaint const* taint, void const* const* block) {
    if (memo_taint_maybe(taint, (uintptr_t)block)) graph_memo_hit_slow(taint, (uintptr_t)block);
}
void graph_memo_taint_block(Checker* checker, void const* const* block); // A private entry tainted: reading it is an event
void graph_link_tainted(Checker* checker, u64 key); // A node link memo's (ty_link_set_memo())
void graph_memo_file_start(Checker* checker); // Nothing in flight: the misses are forgotten (program.c)
void graph_memo_log_unput(Checker* checker, void const** block); // The last put, its value found shared
void graph_memo_flush(Checker* checker);

// Variances on threads (plan 86 4f, "Variances"): a slot per subject (the declared type or the alias symbol), its value a
// shared word: empty, the Variances its group's measurement published, or POISON. Nobody waits: a worker that finds it
// empty measures, and the subjects one outermost measurement claimed (its group, graph_variance_claimed()) are
// published together when it ends (graph_variances_publish()): if the group was clean each slot by CAS, first wins; a
// loser with equal values adopts the winner's, one with differing values keeps its own (private, a read of it a taint
// event); a tainted group poisons its slots and keeps its values private. Null without threads
typedef struct VarianceSlot {
    SharedWord value; // Variances const* published, or SW_POISON
    void const* subject;
    bool alias;
} VarianceSlot;
VarianceSlot* graph_variance_slot(Checker* checker, void const* subject, bool alias, bool create);
void graph_variance_claimed(Checker* checker, VarianceSlot* slot);
// The outermost measurement ended: each claimed slot is published from its private entry (`block_of`: the checker's
// intern block of the slot's variances), or poisoned if the group wasn't clean. Returns whether the checker's values are
// all the published ones (false: something it read is now private)
bool graph_variances_publish(
    Checker* checker, void const** (*block_of)(Checker* checker, VarianceSlot const* slot), bool (*equal)(void const* a, void const* b), bool clean
);

void graph_check_unshared(TyGraph* me, Checker* checker); // TTTT_DEBUG_UNSHARED=1 (program.c)
// Relation results shared (plan 86 4e item 2): definitive and clean only (SUCCEEDED or FAILED, 1 or 2; nothing
// unreliable, unmeasurable, overflowed, index-inferred or keyed by a per-checker generic relation number), first wins.
// 0: none
unsigned graph_relation_get(TyGraph* me, u32 relation, u64 key);
void graph_relation_put(TyGraph* me, u32 relation, u64 key, unsigned value);
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
