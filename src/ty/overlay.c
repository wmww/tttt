// Plan 86 stage 4f's shared words and each worker's Overlay (overlay.h)

#include "ty/ty_internal.h"
#include "ty/overlay.h"
#include "ty/graph.h"
#include <pthread.h>

thread_local uint64_t graph_taint_now;

#ifndef TTTT_TAINT
void graph_taint_bump(void) {
    graph_taint_now++;
    // (The memo entries put that have no value yet were in flight across it: tainted, graph.c)
    Checker* checker = current_checker;
    if (checker && checker->memo_taint && checker->memo_taint->pending_len) graph_memo_sweep(checker);
}
#endif

// The private values and copies in flight (Overlay.map): open addressing with linear probing, deletion by backward
// shift
static OverlayStats stat_total;
static pthread_mutex_t stat_lock = PTHREAD_MUTEX_INITIALIZER;

// TTTT_GRAPH_WORK=1 (overlay.h): a stack of the claims' start cycles per thread
#include <x86intrin.h>
#include <time.h>
static int work_on = -1;
static _Atomic u64 work_base;
static struct timespec work_base_ts;
typedef struct WorkFrame { SharedWord const* w; u64 start, child; } WorkFrame;
static thread_local WorkFrame* work_stack;
static thread_local u32 work_depth, work_cap;
static bool work_enabled(void) {
    int on = __atomic_load_n(&work_on, __ATOMIC_RELAXED);
    if (on < 0) {
        on = getenv("TTTT_GRAPH_WORK") != nullptr;
        __atomic_store_n(&work_on, on, __ATOMIC_RELAXED);
    }
    return on;
}
static u32 work_bin(u64 start) {
    // (Bins of the run since the first claim: 0-25, 25-50, 50-100, 100-200 ms, later; calibrated at the first claim)
    u64 base = atomic_load_explicit(&work_base, memory_order_relaxed);
    static u64 per_ms;
    if (!per_ms) per_ms = 3000000; // (Refined at print; bins use a nominal 3 GHz)
    u64 ms = (start - base) / per_ms;
    return ms < 25 ? 0 : ms < 50 ? 1 : ms < 100 ? 2 : ms < 200 ? 3 : 4;
}
[[gnu::noinline, gnu::cold]] static void work_push(SharedWord const* w) {
    u64 now = __rdtsc();
    u64 zero = 0;
    if (atomic_compare_exchange_strong(&work_base, &zero, now)) timespec_get(&work_base_ts, TIME_UTC);
    if (work_depth == work_cap) {
        work_cap = work_cap ? work_cap * 2 : 256;
        work_stack = realloc(work_stack, sizeof(WorkFrame) * work_cap);
    }
    work_stack[work_depth++] = (WorkFrame){ w, now, 0 };
}
[[gnu::noinline, gnu::cold]] static void work_pop(Overlay* o, SharedWord const* w, u32 outcome) {
    u64 now = __rdtsc();
    // (The frame of `w`, innermost; frames above it that never finished are dropped into it)
    u32 i = work_depth;
    while (i-- > 0 && work_stack[i].w != w) {}
    if (i == UINT32_MAX) return;
    while (work_depth > i + 1) {
        WorkFrame f = work_stack[--work_depth];
        work_stack[work_depth - 1].child += 0; // (Their time stays in the frame below, counted as its own)
        (void)f;
    }
    WorkFrame f = work_stack[--work_depth];
    u64 incl = now - f.start;
    u64 excl = incl > f.child ? incl - f.child : 0;
    o->stats.work[outcome][work_bin(f.start)] += excl;
    o->stats.works[outcome]++;
    if (work_depth) work_stack[work_depth - 1].child += incl;
}

static inline u32 map_slot(OverlayMap const* o, uintptr_t key) {
    u64 h = (u64)key * 0x9e3779b97f4a7c15ull;
    return (u32)(h >> 32) & (o->cap - 1);
}

// The entry's index, or the empty slot where it would go
static u32 map_find(OverlayMap const* o, uintptr_t key) {
    u32 i = map_slot(o, key);
    while (o->keys[i] && o->keys[i] != key) i = (i + 1) & (o->cap - 1);
    return i;
}

static void map_put(Overlay* ov, uintptr_t key, uintptr_t value) {
    OverlayMap* o = &ov->map;
    if ((o->len + 1) * 2 > o->cap) {
        OverlayMap bigger = { .cap = o->cap ? o->cap * 2 : 256, .len = o->len };
        bigger.keys = calloc(bigger.cap, sizeof(uintptr_t));
        bigger.values = calloc(bigger.cap, sizeof(uintptr_t));
        panic_if(!bigger.keys || !bigger.values, "out of memory");
        for (u32 i = 0; i < o->cap; i++) {
            if (!o->keys[i]) continue;
            u32 j = map_find(&bigger, o->keys[i]);
            bigger.keys[j] = o->keys[i];
            bigger.values[j] = o->values[i];
        }
        free(o->keys);
        free(o->values);
        *o = bigger;
    }
    u32 i = map_find(o, key);
    if (!o->keys[i]) o->len++;
    else if (!o->values[i]) ov->copies--;
    if (!value) ov->copies++;
    o->keys[i] = key;
    o->values[i] = value;
}

static bool map_get(OverlayMap const* o, uintptr_t key, uintptr_t* value) {
    if (!o->len) return false;
    u32 i = map_find(o, key);
    if (!o->keys[i]) return false;
    *value = o->values[i];
    return true;
}

static void map_remove(Overlay* ov, uintptr_t key) {
    OverlayMap* o = &ov->map;
    if (!o->len) return; // (sw_finish() without a claim: ty_set_declared_type())
    u32 i = map_find(o, key);
    if (!o->keys[i]) return;
    if (!o->values[i]) ov->copies--;
    o->keys[i] = 0;
    o->len--;
    // (Backward shift: the entries after it that probed past it move up)
    for (u32 j = (i + 1) & (o->cap - 1); o->keys[j]; j = (j + 1) & (o->cap - 1)) {
        u32 home = map_slot(o, o->keys[j]);
        if (((j - home) & (o->cap - 1)) >= ((j - i) & (o->cap - 1))) {
            o->keys[i] = o->keys[j];
            o->values[i] = o->values[j];
            o->keys[j] = 0;
            i = j;
        }
    }
}

// The claims (Overlay.claims and its filter)

[[gnu::noinline, gnu::cold]] static void claims_grow(Overlay* o) {
    o->cap = o->cap ? o->cap * 2 : 256;
    o->claims = realloc(o->claims, sizeof(uintptr_t) * o->cap);
    panic_if(!o->claims, "out of memory");
}

[[gnu::noinline, gnu::cold]] static void claims_overflow(void) {
    panic("sw_claim(): too many claims");
}

static inline void claim_push(Overlay* o, SharedWord const* w) {
    if (__builtin_expect(o->depth == o->cap, 0)) claims_grow(o);
    o->claims[o->depth++] = (uintptr_t)w;
    uint16_t* n = &o->filter[sw_filter_slot(w)];
    if (__builtin_expect(*n == UINT16_MAX, 0)) claims_overflow();
    ++*n;
}

static inline bool claim_has(Overlay const* o, SharedWord const* w) {
    if (__builtin_expect(!o->filter[sw_filter_slot(w)], 1)) return false;
    for (u32 i = o->depth; i-- > 0;) if (o->claims[i] == (uintptr_t)w) return true;
    return false;
}

// Every claim of `w` ends (a re-entry that computed it again claimed it twice: the one finish ends both, as the map
// entry it was ended them)
[[gnu::noinline]] static void claim_drop_slow(Overlay* o, SharedWord const* w, uint16_t* n) {
    for (u32 i = o->depth; *n && i-- > 0;) {
        if (o->claims[i] != (uintptr_t)w) continue;
        o->claims[i] = 0;
        --*n;
    }
    while (o->depth && !o->claims[o->depth - 1]) o->depth--;
}

static inline void claim_drop(Overlay* o, SharedWord const* w) {
    uint16_t* n = &o->filter[sw_filter_slot(w)];
    if (!*n) return;
    if (__builtin_expect(*n == 1 && o->claims[o->depth - 1] == (uintptr_t)w, 1)) {
        *n = 0;
        if (--o->depth && __builtin_expect(!o->claims[o->depth - 1], 0)) claim_drop_slow(o, w, n);
        return;
    }
    claim_drop_slow(o, w, n);
}

void overlay_drop(Checker* me) {
    Overlay* o = &my overlay;
    pthread_mutex_lock(&stat_lock);
    stat_total.published += o->stats.published;
    stat_total.adopted += o->stats.adopted;
    stat_total.poisoned += o->stats.poisoned;
    stat_total.private_ += o->stats.private_;
    stat_total.shadowed += o->stats.shadowed;
    stat_total.hits += o->stats.hits;
    stat_total.mine += o->stats.mine;
    for (u32 k = 0; k < SW_OUTS; k++) {
        stat_total.works[k] += o->stats.works[k];
        for (u32 b = 0; b < SW_TIME_BINS; b++) stat_total.work[k][b] += o->stats.work[k][b];
    }
    pthread_mutex_unlock(&stat_lock);
    free(o->map.keys);
    free(o->map.values);
    free(o->claims);
    *o = (Overlay){0};
}

// TTTT_DEBUG_PUBLISH=1 (plan 86 stage 4f, "identity of lost-race objects"): every computation's arena range is recorded
// when it finishes, as private (tainted, or lost) or public (published). sw_debug_check_ref() aborts when a pointer a
// published value holds lies in a private range whose innermost recording is private: an object made by a computation
// whose result no other worker may see. Identities (types) made there are shared at creation, so only the kinds that
// make non-identity objects (Members, Props, Signatures, SigResolved: P1) check their referents
typedef struct DebugMark {
    SharedWord const* word;
    ArenaChunk const* chunk;
    size_t used;
} DebugMark;
typedef struct DebugRange {
    char const* from;
    char const* to;
    bool private_;
} DebugRange;
static thread_local DebugMark* debug_marks;
static thread_local u32 debug_marks_len, debug_marks_cap;
static DebugRange* debug_ranges;
static u32 debug_ranges_len, debug_ranges_cap;
static pthread_mutex_t debug_lock = PTHREAD_MUTEX_INITIALIZER;
static int debug_publish = -1;

static bool debug_publish_on(void) {
    int on = __atomic_load_n(&debug_publish, __ATOMIC_RELAXED);
    if (on < 0) {
        on = getenv("TTTT_DEBUG_PUBLISH") && *getenv("TTTT_DEBUG_PUBLISH") == '1';
        __atomic_store_n(&debug_publish, on, __ATOMIC_RELAXED);
    }
    return on;
}

[[gnu::noinline, gnu::cold]] static void debug_mark(Checker* me, SharedWord const* w) {
    if (debug_marks_len == debug_marks_cap) {
        debug_marks_cap = debug_marks_cap ? debug_marks_cap * 2 : 64;
        debug_marks = realloc(debug_marks, sizeof(DebugMark) * debug_marks_cap);
        panic_if(!debug_marks, "out of memory");
    }
    debug_marks[debug_marks_len++] = (DebugMark){ w, my arena.head, my arena.head ? my arena.head->used : 0 };
}

static void debug_range_add(char const* from, char const* to, bool private_) {
    if (from == to) return;
    if (debug_ranges_len == debug_ranges_cap) {
        debug_ranges_cap = debug_ranges_cap ? debug_ranges_cap * 2 : 1024;
        debug_ranges = realloc(debug_ranges, sizeof(DebugRange) * debug_ranges_cap);
        panic_if(!debug_ranges, "out of memory");
    }
    debug_ranges[debug_ranges_len++] = (DebugRange){ from, to, private_ };
}

// The computation of `w` ended: its allocations since its claim, chunk by chunk (newer chunks are in front)
[[gnu::noinline, gnu::cold]] static void debug_finish(Checker* me, SharedWord const* w, bool private_) {
    if (!debug_marks_len || debug_marks[debug_marks_len - 1].word != w) return; // (Claimed before the knob was read)
    DebugMark mark = debug_marks[--debug_marks_len];
    pthread_mutex_lock(&debug_lock);
    for (ArenaChunk const* c = my arena.head; c; c = c->next) {
        bool first = c == mark.chunk;
        debug_range_add(c->data + (first ? mark.used : 0), c->data + c->used, private_);
        if (first || !mark.chunk) break;
    }
    pthread_mutex_unlock(&debug_lock);
}

void sw_debug_check_ref(void const* ref, char const* what) {
    if (!debug_publish_on() || !ref) return;
    char const* p = ref;
    pthread_mutex_lock(&debug_lock);
    DebugRange const* innermost = nullptr;
    for (u32 i = 0; i < debug_ranges_len; i++) {
        DebugRange const* r = &debug_ranges[i];
        if (p < r->from || p >= r->to) continue;
        if (!innermost || r->to - r->from < innermost->to - innermost->from) innermost = r;
    }
    bool bad = innermost && innermost->private_;
    pthread_mutex_unlock(&debug_lock);
    panic_if(bad, "TTTT_DEBUG_PUBLISH: a published ", what, " refers to ", (u64)(uintptr_t)ref,
        ", made by a computation whose result is private (tainted or lost)");
}

SwLook sw_look_slow(Checker* me, SharedWord const* w, uint8_t const* state, uintptr_t* out) {
    if (!my graph) {
        uintptr_t v = atomic_load_explicit(&w->v, memory_order_relaxed);
        if (v) {
            *out = v;
            return SW_LOOK_DONE;
        }
        if (*state == LAZY_STATE_IN_FLIGHT) {
            TAINT_EVENT(TAINT_IN_FLIGHT_OTHER);
            return SW_LOOK_MINE;
        }
        return SW_LOOK_COMPUTE;
    }
    (void)state;
    // (This worker's own first: its in-flight computation, or a value it keeps, whatever the word says now)
    Overlay* o = &my overlay;
    if (claim_has(o, w)) {
        // (Its own cycle: an event)
        TAINT_EVENT(TAINT_IN_FLIGHT_OTHER);
        o->stats.mine++;
        return SW_LOOK_MINE;
    }
    uintptr_t v = sw_load(w);
    uintptr_t own;
    if ((v == SW_POISON || my graph_shadowed || o->copies) && map_get(&o->map, (uintptr_t)w, &own)) {
        // (A copy in flight, a cycle, or a private value: tainted, so reading it is an event too)
        TAINT_EVENT(own ? TAINT_READ_TAINTED : TAINT_IN_FLIGHT_OTHER);
        if (!own) {
            o->stats.mine++;
            return SW_LOOK_MINE;
        }
        o->stats.hits++;
        *out = own;
        return SW_LOOK_DONE;
    }
    if (v > SW_POISON) {
        *out = v;
        return SW_LOOK_DONE;
    }
    return SW_LOOK_COMPUTE;
}

void sw_copy_in_flight(Checker* me, SharedWord* to, uint8_t* to_state) {
    if (!my graph) *to_state = LAZY_STATE_IN_FLIGHT;
    else map_put(&my overlay, (uintptr_t)to, SW_EMPTY);
}

// (The seeded scheduler's and TTTT_DEBUG_PUBLISH's claim)
[[gnu::noinline, gnu::cold]] static void sw_claim_debug(Checker* me, SharedWord const* w) {
    // (A scheduling point of the seeded scheduler (TTTT_GRAPH_SEED) (as the old lazy claims were): a claim is where
    // another worker's computation of the same word can interleave)
    graph_sched_point(false);
    claim_push(&my overlay, w);
    if (debug_publish_on()) debug_mark(me, w);
    if (work_enabled()) work_push(w);
}

uint64_t sw_claim(Checker* me, SharedWord* w, uint8_t* state) {
    if (!my graph) *state = LAZY_STATE_IN_FLIGHT;
    else if (__builtin_expect(graph_sched_on || __atomic_load_n(&debug_publish, __ATOMIC_RELAXED) != 0 || __atomic_load_n(&work_on, __ATOMIC_RELAXED) != 0, 0)) sw_claim_debug(me, w);
    else claim_push(&my overlay, w);
    return graph_taint_now;
}

// TTTT_GRAPH_FORCE_SHADOW=1: a clean loser is treated as a tainted one, to exercise the shadowed path and the retire at
// the file's end; the output must not move
static int force_shadow = -1;
[[gnu::cold]] static bool force_shadow_on(void) {
    int force = __atomic_load_n(&force_shadow, __ATOMIC_RELAXED);
    if (force < 0) {
        force = getenv("TTTT_GRAPH_FORCE_SHADOW") && *getenv("TTTT_GRAPH_FORCE_SHADOW") == '1';
        __atomic_store_n(&force_shadow, force, __ATOMIC_RELAXED);
    }
    return force;
}

uintptr_t sw_finish(Checker* me, SharedWord* w, uint8_t* state, uintptr_t value, uint64_t taint) {
    check(value != SW_EMPTY && value != SW_POISON);
    if (!my graph) {
        // (One worker: its value is the only history, tainted or not)
        atomic_store_explicit(&w->v, value, memory_order_release);
        *state = LAZY_STATE_NOT_STARTED;
        return value;
    }
    Overlay* o = &my overlay;
    claim_drop(o, w);
    // (A private value is kept only for a poisoned word, or by a shadowed worker)
    uintptr_t seen = sw_load(w);
    if ((seen == SW_POISON || o->copies || my graph_shadowed) && o->map.len) map_remove(o, (uintptr_t)w);
    bool clean = graph_taint_now == taint;
    bool debug = __builtin_expect(__atomic_load_n(&debug_publish, __ATOMIC_RELAXED) > 0, 0);
    // (Tested before the CAS: a final word's line isn't taken exclusively by a CAS that can't win)
    if (seen == SW_EMPTY &&
        atomic_compare_exchange_strong_explicit(&w->v, &seen, clean ? value : (uintptr_t)SW_POISON, memory_order_release, memory_order_acquire)) {
        if (debug) debug_finish(me, w, !clean);
        if (work_on > 0) work_pop(o, w, clean ? SW_OUT_PUBLISHED : SW_OUT_POISONED);
        if (clean) {
            o->stats.published++;
            return value;
        }
        o->stats.poisoned++;
        map_put(o, (uintptr_t)w, value);
        return value;
    }
    if (debug) debug_finish(me, w, true);
    if (work_on > 0) work_pop(o, w, seen == SW_POISON ? SW_OUT_PRIVATE : clean ? SW_OUT_ADOPTED : SW_OUT_SHADOWED);
    if (seen == SW_POISON) {
        // (Someone's was tainted: every worker keeps its own, clean or not)
        o->stats.private_++;
        map_put(o, (uintptr_t)w, value);
        return value;
    }
    if (clean && !(__builtin_expect(__atomic_load_n(&force_shadow, __ATOMIC_RELAXED) != 0, 0) && force_shadow_on())) {
        // (Nothing read its own before now: a read of a value in flight is a taint event)
        o->stats.adopted++;
        return seen;
    }
    // A tainted value whose word another worker published meanwhile: this worker keeps its own, and goes by its Overlay
    // first until its file ends (program.c retires it then)
    o->stats.shadowed++;
    map_put(o, (uintptr_t)w, value);
    my graph_shadowed = true;
    return value;
}

bool sw_mine_in_flight(Checker const* me, SharedWord const* w, uint8_t const* state) {
    if (!my graph) return *state == LAZY_STATE_IN_FLIGHT;
    Overlay const* o = &my overlay;
    if (claim_has(o, w)) return true;
    uintptr_t own;
    return o->copies && map_get(&o->map, (uintptr_t)w, &own) && !own;
}

void sw_unclaim(Checker* me, SharedWord const* w) {
    if (my graph && work_on > 0) work_pop(&my overlay, w, SW_OUT_DROPPED);
    if (my graph && __builtin_expect(__atomic_load_n(&debug_publish, __ATOMIC_RELAXED) > 0, 0)) debug_finish(me, w, true);
}

void sw_abandon(Checker* me, SharedWord const* w, uint8_t* state) {
    if (!my graph) {
        *state = LAZY_STATE_NOT_STARTED;
        return;
    }
    claim_drop(&my overlay, w);
    map_remove(&my overlay, (uintptr_t)w);
    sw_unclaim(me, w);
}

uintptr_t sw_peek_slow(Checker const* me, SharedWord const* w) {
    if (!my graph) return atomic_load_explicit(&w->v, memory_order_relaxed);
    Overlay const* o = &my overlay;
    if (claim_has(o, w)) return SW_EMPTY;
    uintptr_t v = sw_load(w);
    uintptr_t own;
    if ((v == SW_POISON || my graph_shadowed || o->copies) && map_get(&o->map, (uintptr_t)w, &own)) {
        if (own) TAINT_EVENT(TAINT_READ_TAINTED);
        return own;
    }
    return v > SW_POISON ? v : SW_EMPTY;
}

void overlay_print_stats(Checker* const* live, u32 count) {
    pthread_mutex_lock(&stat_lock);
    OverlayStats t = stat_total;
    pthread_mutex_unlock(&stat_lock);
    for (u32 i = 0; i < count; i++) {
        if (!live[i]) continue;
        OverlayStats const* s = &live[i]->overlay.stats;
        t.published += s->published, t.adopted += s->adopted, t.poisoned += s->poisoned, t.private_ += s->private_;
        t.shadowed += s->shadowed, t.hits += s->hits, t.mine += s->mine;
        for (u32 k = 0; k < SW_OUTS; k++) {
            t.works[k] += s->works[k];
            for (u32 b = 0; b < SW_TIME_BINS; b++) t.work[k][b] += s->work[k][b];
        }
    }
    if (work_on > 0) {
        static char const* names[SW_OUTS] = { "published", "adopted", "poisoned", "private", "shadowed", "dropped" };
        struct timespec now_ts;
        timespec_get(&now_ts, TIME_UTC);
        double ms = (double)(now_ts.tv_sec - work_base_ts.tv_sec) * 1e3 + (double)(now_ts.tv_nsec - work_base_ts.tv_nsec) / 1e6;
        fprintf(stderr, "graph: word work: TSC %.0f cycles per ms (bins assume 3e6)\n", (double)(__rdtsc() - atomic_load(&work_base)) / ms);
        eprintln("graph: word work (G cycles, exclusive; computations; bins 0-25/25-50/50-100/100-200/200+ ms):");
        for (u32 k = 0; k < SW_OUTS; k++) {
            u64 sum = 0;
            for (u32 b = 0; b < SW_TIME_BINS; b++) sum += t.work[k][b];
            fprintf(stderr, "graph:   %-9s %7.3f  n=%-9llu  %.3f %.3f %.3f %.3f %.3f\n", names[k], (double)sum / 1e9,
                (unsigned long long)t.works[k], (double)t.work[k][0] / 1e9, (double)t.work[k][1] / 1e9,
                (double)t.work[k][2] / 1e9, (double)t.work[k][3] / 1e9, (double)t.work[k][4] / 1e9);
        }
    }
    eprintln("graph: shared words published ", t.published, ", adopted ", t.adopted, ", poisoned ", t.poisoned,
        ", computed privately after poison ", t.private_, ", shadowed ", t.shadowed, ", overlay hits ", t.hits,
        ", own in flight ", t.mine);
}
