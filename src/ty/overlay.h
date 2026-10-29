#pragma once

// Plan 86 stage 4f's primitives: write-once shared words, and each worker's own state for them.
//
// A lazily computed value reachable from a shared object lives in one SharedWord. A word goes from empty to final
// once, by CAS with release, and is read with acquire. Final means a clean value, or POISON: the value was tainted for
// the worker that finished first, so every worker keeps its own. Nothing else about the computation is ever visible
// on the shared object.
// - Without --shared-graph there is one worker per graph. The word is written as the value whatever the taint, and
//   in flight is a LazyState byte next to the word (`state`, NOT_STARTED or IN_FLIGHT only): today's semantics.
// - With it, in flight and every private value live in the worker's Overlay (a map by the word's address). No worker
//   reads, waits for or learns of another's in-flight state. A finished computation publishes if it was clean:
//   - if another clean value won, it adopts that one (nothing can have read its own, see plan 86 "Stage 4f");
//   - if it was tainted, it poisons the word and keeps its value in its Overlay;
//   - if it was tainted and a clean value won meanwhile, it is "shadowed": its fast paths take the slow path (Overlay
//     first) until its file ends, and then the worker is retired (program.c).
//
// The pattern of a site (ty_non_augmenting_base() is the worked example):
//     uintptr_t v = sw_get(me, &state->word);          // the fast path: published and not shadowed
//     if (v) return SW_PTR(v);
//     switch (sw_look(me, &state->word, &state->state, &v)) {
//         case SW_LOOK_DONE: return SW_PTR(v);         // published, or this worker's own private value
//         case SW_LOOK_MINE: return <its cycle answer>; // in flight in this worker (a taint event was counted)
//         case SW_LOOK_COMPUTE: break;
//     }
//     u64 taint = sw_claim(me, &state->word, &state->state);
//     ... compute `value` ...
//     SW_FP(TK_KIND, subject, value, taint);          // the fingerprint oracle's point (taint.h's TK_*)
//     return SW_PTR(sw_finish(me, &state->word, &state->state, SW_VAL(value), taint));

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef struct SharedWord {
    _Atomic uintptr_t v;
} SharedWord;

// Empty: not computed (or, with the flag, nothing published). POISON: tainted where it was first finished. NONE:
// computed, and the value is null
enum { SW_EMPTY = 0, SW_POISON = 1, SW_NONE = 2 };
// (Functions, not macros: `SW_PTR(sw_finish(...))` is the sites' pattern, and a macro evaluated the finish twice)
static inline uintptr_t sw_val(void const* p) { return p ? (uintptr_t)p : (uintptr_t)SW_NONE; }
static inline void const* sw_ptr(uintptr_t v) { return v == SW_NONE ? nullptr : (void const*)v; }
#define SW_VAL(p) sw_val(p)
#define SW_PTR(v) sw_ptr(v)

static inline uintptr_t sw_load(SharedWord const* w) {
    return atomic_load_explicit(&w->v, memory_order_acquire);
}

struct Checker;
typedef enum SwLook { SW_LOOK_DONE, SW_LOOK_MINE, SW_LOOK_COMPUTE } SwLook;

// A worker's own state for the words (Checker.overlay), never seen by another worker:
// - its claims, a stack (claims nest: a computation's nested claims end before it does), with `filter` counting the
//   claims by a hash of the word's address, so that "is this word in flight here" is one load when it isn't;
// - its private values (tainted, computed after a poison, or shadowed) and the copies of words in flight here
//   (sw_copy_in_flight(): in flight for good, value SW_EMPTY), a map by the word's address. The map is looked in only
//   when the word isn't published, the worker is shadowed, or it has such copies (`copies`)
enum { SW_FILTER_BITS = 10 };
typedef struct OverlayMap {
    uintptr_t* keys;
    uintptr_t* values;
    uint32_t cap, len;
} OverlayMap;
// TTTT_GRAPH_WORK=1 (a measuring knob, plan 86 "Stage 4f: the perf accounting"): the cycles spent in each
// computation of a word, exclusive of the nested ones, by its outcome and by when it started (bins of the run)
enum { SW_OUT_PUBLISHED, SW_OUT_ADOPTED, SW_OUT_POISONED, SW_OUT_PRIVATE, SW_OUT_SHADOWED, SW_OUT_DROPPED, SW_OUTS };
enum { SW_TIME_BINS = 5 };
typedef struct OverlayStats {
    uint64_t published, adopted, poisoned, private_, shadowed, hits, mine;
    uint64_t work[SW_OUTS][SW_TIME_BINS], works[SW_OUTS];
} OverlayStats;
typedef struct Overlay {
    uintptr_t* claims; // Bottom first; 0 is a claim a nested computation finished (popped once it is on top)
    uint32_t depth, cap;
    uint32_t copies; // Map entries of copies in flight
    OverlayMap map;
    OverlayStats stats; // TTTT_GRAPH_STATS, added to the totals when the checker is dropped
    uint16_t filter[1u << SW_FILTER_BITS];
} Overlay;
static inline uint32_t sw_filter_slot(SharedWord const* w) {
    return (uint32_t)(((uintptr_t)w >> 3) * 0x9e3779b97f4a7c15ull >> (64 - SW_FILTER_BITS));
}

// The slow path (sw_look() in ty.h is the inline first probe). DONE with *out the value (null-free: NONE stands for null)
SwLook sw_look_slow(struct Checker* me, SharedWord const* w, uint8_t const* state, uintptr_t* out);
// Marks it in flight in this worker; returns the taint counter to give sw_finish()
uint64_t sw_claim(struct Checker* me, SharedWord* w, uint8_t* state);
// The value this worker uses from now on (its own, or the winner's)
uintptr_t sw_finish(struct Checker* me, SharedWord* w, uint8_t* state, uintptr_t value, uint64_t taint);
// sw_finish() of a word whose value is an identity (an interface's or a class's declared type): clean whatever happened
// since its claim, and exempt from the forced knobs (TTTT_GRAPH_FORCE_POISON/SHADOW), which no schedule can make it meet
uintptr_t sw_finish_identity(struct Checker* me, SharedWord* w, uint8_t* state, uintptr_t value);
// Whether this worker's claim of `w` is still unfinished: false once a nested computation of the same word (a re-entry
// that computes again, like function_return_type()'s) finished it. No taint event
bool sw_mine_in_flight(struct Checker const* me, SharedWord const* w, uint8_t const* state);
// The end of a claim whose word a nested computation finished (sw_mine_in_flight() false): its own result is dropped
void sw_unclaim(struct Checker* me, SharedWord const* w);
// A claim given up without a value (still in flight here: a cycle found the computation can't finish): not computed
void sw_abandon(struct Checker* me, SharedWord const* w, uint8_t* state);
// sw_peek()'s slow path (sw_peek() in ty.h: a done-ness peek, TAINT_PEEK, no event): the value this worker sees if the
// word is finished for it (published, or its own private value: that read is an event), else SW_EMPTY (not computed,
// poisoned, or in flight here). It looks only at the word's final value and the caller's own Overlay: another
// worker's in flight is never seen (it leaves the word empty)
uintptr_t sw_peek_slow(struct Checker const* me, SharedWord const* w);

// Whether a computation that started at `taint` (sw_claim()'s) is clean: always without a graph
#define SW_CLEAN(me, taint) (!(me)->graph || graph_taint_now == (taint))

// -DTTTT_GRAPH_FINGERPRINT's publication point of a converted site (graph_debug.c): the result is compared with K1's
// only if it is clean, so only if it is published
#ifdef TTTT_GRAPH_FINGERPRINT
void graph_fp_result(unsigned kind, void const* subject, void const* result, bool tainted);
#define SW_FP(kind, subject, result, taint) graph_fp_result((kind), (subject), (result), graph_taint_now != (taint))
#else
#define SW_FP(kind, subject, result, taint) ((void)0)
#endif

// A copy of a word in flight here (`*to = *from` of its holder): in flight here too, for good (nothing finishes it: a
// read of it is a cycle, as a copied LazyState byte was)
void sw_copy_in_flight(struct Checker* me, SharedWord* to, uint8_t* to_state);
// TTTT_DEBUG_PUBLISH=1: aborts if `ref`, held by a value being published, was made by a computation whose result stayed
// private (overlay.c). For the kinds whose values hold non-identity objects (Props, Signatures, Members: P1's walks)
void sw_debug_check_ref(void const* ref, char const* what);
void overlay_drop(struct Checker* me);
// TTTT_GRAPH_STATS: the totals of the dropped checkers and of `live`
void overlay_print_stats(struct Checker* const* live, uint32_t count);

// The cheap taint counter (taint.h): bumped by every taint event, per thread, always compiled
extern thread_local uint64_t graph_taint_now;
