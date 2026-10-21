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

typedef struct SharedWord {
    _Atomic uintptr_t v;
} SharedWord;

// Empty: not computed (or, with the flag, nothing published). POISON: tainted where it was first finished. NONE:
// computed, and the value is null
enum { SW_EMPTY = 0, SW_POISON = 1, SW_NONE = 2 };
#define SW_VAL(p) ((p) ? (uintptr_t)(p) : (uintptr_t)SW_NONE)
#define SW_PTR(v) ((v) == SW_NONE ? nullptr : (void const*)(v))

static inline uintptr_t sw_load(SharedWord const* w) {
    return __atomic_load_n(&w->v, __ATOMIC_ACQUIRE);
}

struct Checker;
typedef enum SwLook { SW_LOOK_DONE, SW_LOOK_MINE, SW_LOOK_COMPUTE } SwLook;
// The slow path. DONE with *out the value (null-free: NONE stands for null)
SwLook sw_look(struct Checker* me, SharedWord const* w, uint8_t const* state, uintptr_t* out);
// Marks it in flight in this worker; returns the taint counter to give sw_finish()
uint64_t sw_claim(struct Checker* me, SharedWord* w, uint8_t* state);
// The value this worker uses from now on (its own, or the winner's)
uintptr_t sw_finish(struct Checker* me, SharedWord* w, uint8_t* state, uintptr_t value, uint64_t taint);
// Whether this worker's claim of `w` is still unfinished: false once a nested computation of the same word (a re-entry
// that computes again, like function_return_type()'s) finished it. No taint event
bool sw_mine_in_flight(struct Checker const* me, SharedWord const* w, uint8_t const* state);
// The end of a claim whose word a nested computation finished (sw_mine_in_flight() false): its own result is dropped
void sw_unclaim(struct Checker* me, SharedWord const* w);
// A claim given up without a value (still in flight here: a cycle found the computation can't finish): not computed
void sw_abandon(struct Checker* me, SharedWord const* w, uint8_t* state);
// A done-ness peek (TAINT_PEEK, no event): the value this worker sees if the word is finished for it (published, or
// its own private value: that read is an event), else SW_EMPTY (not computed, poisoned, or in flight here). It looks
// only at the word's final value and the caller's own Overlay/ClaimSet: another worker's in flight is never seen (it
// leaves the word empty)
uintptr_t sw_peek(struct Checker const* me, SharedWord const* w);

// Whether a computation that started at `taint` (sw_claim()'s) is clean: always without a graph. The fingerprint build
// (-DTTTT_GRAPH_FINGERPRINT) checks only those against K1's record: `if (SW_CLEAN(me, taint)) TAINT_END(...)`
#define SW_CLEAN(me, taint) (!(me)->graph || graph_taint_now == (taint))

// -DTTTT_GRAPH_FINGERPRINT's publication point of a converted site (graph_debug.c): the result is compared with K1's
// only if it is clean, so only if it is published
#ifdef TTTT_GRAPH_FINGERPRINT
void graph_fp_result(unsigned kind, void const* subject, void const* result, bool tainted);
#define SW_FP(kind, subject, result, taint) graph_fp_result((kind), (subject), (result), graph_taint_now != (taint))
#else
#define SW_FP(kind, subject, result, taint) ((void)0)
#endif

// TTTT_DEBUG_PUBLISH=1: aborts if `ref`, held by a value being published, was made by a computation whose result stayed
// private (overlay.c). For the kinds whose values hold non-identity objects (Props, Signatures, Members: P1's walks)
void sw_debug_check_ref(void const* ref, char const* what);
void overlay_drop(struct Checker* me);
void overlay_print_stats(void); // TTTT_GRAPH_STATS

// The cheap taint counter (taint.h): bumped by every taint event, per thread, always compiled
extern thread_local uint64_t graph_taint_now;
