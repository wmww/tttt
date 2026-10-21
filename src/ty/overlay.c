// Plan 86 stage 4f's shared words and each worker's Overlay (overlay.h)

#include "ty/ty_internal.h"
#include "ty/overlay.h"
#include "ty/graph.h"
#include <pthread.h>

thread_local uint64_t graph_taint_now;

#if !defined(TTTT_TAINT) && !defined(TTTT_GRAPH_TAINT)
void graph_taint_bump(void) {
    graph_taint_now++;
}
#endif

// A worker's words in flight (value SW_EMPTY) and its private values, by the word's address: open addressing with
// linear probing, deletion by backward shift
typedef struct Overlay {
    uintptr_t* keys;
    uintptr_t* values;
    u32 cap, len;
} Overlay;

static _Atomic u64 stat_published, stat_adopted, stat_poisoned, stat_private, stat_shadowed, stat_hits, stat_mine;

static inline u32 overlay_slot(Overlay const* o, uintptr_t key) {
    u64 h = (u64)key * 0x9e3779b97f4a7c15ull;
    return (u32)(h >> 32) & (o->cap - 1);
}

// The entry's index, or the empty slot where it would go
static u32 overlay_find(Overlay const* o, uintptr_t key) {
    u32 i = overlay_slot(o, key);
    while (o->keys[i] && o->keys[i] != key) i = (i + 1) & (o->cap - 1);
    return i;
}

static void overlay_put(Checker* me, uintptr_t key, uintptr_t value) {
    Overlay* o = my overlay;
    if (!o) {
        o = my overlay = calloc(1, sizeof(Overlay));
        panic_if(!o, "out of memory");
    }
    if ((o->len + 1) * 2 > o->cap) {
        Overlay bigger = { .cap = o->cap ? o->cap * 2 : 256, .len = o->len };
        bigger.keys = calloc(bigger.cap, sizeof(uintptr_t));
        bigger.values = calloc(bigger.cap, sizeof(uintptr_t));
        panic_if(!bigger.keys || !bigger.values, "out of memory");
        for (u32 i = 0; i < o->cap; i++) {
            if (!o->keys[i]) continue;
            u32 j = overlay_find(&bigger, o->keys[i]);
            bigger.keys[j] = o->keys[i];
            bigger.values[j] = o->values[i];
        }
        free(o->keys);
        free(o->values);
        *o = bigger;
    }
    u32 i = overlay_find(o, key);
    if (!o->keys[i]) o->len++;
    o->keys[i] = key;
    o->values[i] = value;
}

static bool overlay_get(Checker const* me, uintptr_t key, uintptr_t* value) {
    Overlay const* o = my overlay;
    if (!o || !o->len) return false;
    u32 i = overlay_find(o, key);
    if (!o->keys[i]) return false;
    *value = o->values[i];
    return true;
}

static void overlay_remove(Checker* me, uintptr_t key) {
    Overlay* o = my overlay;
    if (!o || !o->len) return; // (sw_finish() without a claim: ty_set_declared_type())
    u32 i = overlay_find(o, key);
    if (!o->keys[i]) return;
    o->keys[i] = 0;
    o->len--;
    // (Backward shift: the entries after it that probed past it move up)
    for (u32 j = (i + 1) & (o->cap - 1); o->keys[j]; j = (j + 1) & (o->cap - 1)) {
        u32 home = overlay_slot(o, o->keys[j]);
        if (((j - home) & (o->cap - 1)) >= ((j - i) & (o->cap - 1))) {
            o->keys[i] = o->keys[j];
            o->values[i] = o->values[j];
            o->keys[j] = 0;
            i = j;
        }
    }
}

void overlay_drop(Checker* me) {
    if (!my overlay) return;
    free(my overlay->keys);
    free(my overlay->values);
    free(my overlay);
    my overlay = nullptr;
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

static void debug_mark(Checker* me, SharedWord const* w) {
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
static void debug_finish(Checker* me, SharedWord const* w, bool private_) {
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

SwLook sw_look(Checker* me, SharedWord const* w, uint8_t const* state, uintptr_t* out) {
    if (!my graph) {
        uintptr_t v = __atomic_load_n(&w->v, __ATOMIC_RELAXED);
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
    uintptr_t own;
    if (overlay_get(me, (uintptr_t)w, &own)) {
        // (Its own cycle, or a private value: tainted, so reading it is an event too)
        TAINT_EVENT(own ? TAINT_READ_TAINTED : TAINT_IN_FLIGHT_OTHER);
        if (!own) {
            atomic_fetch_add_explicit(&stat_mine, 1, memory_order_relaxed);
            return SW_LOOK_MINE;
        }
        atomic_fetch_add_explicit(&stat_hits, 1, memory_order_relaxed);
        *out = own;
        return SW_LOOK_DONE;
    }
    uintptr_t v = sw_load(w);
    if (v > SW_POISON) {
        *out = v;
        return SW_LOOK_DONE;
    }
    return SW_LOOK_COMPUTE;
}

uint64_t sw_claim(Checker* me, SharedWord* w, uint8_t* state) {
    if (!my graph) *state = LAZY_STATE_IN_FLIGHT;
    else {
        overlay_put(me, (uintptr_t)w, SW_EMPTY);
        if (__builtin_expect(__atomic_load_n(&debug_publish, __ATOMIC_RELAXED) != 0, 0) && debug_publish_on()) debug_mark(me, w);
    }
    return graph_taint_now;
}

uintptr_t sw_finish(Checker* me, SharedWord* w, uint8_t* state, uintptr_t value, uint64_t taint) {
    check(value != SW_EMPTY && value != SW_POISON);
    if (!my graph) {
        // (One worker: its value is the only history, tainted or not)
        __atomic_store_n(&w->v, value, __ATOMIC_RELEASE);
        *state = LAZY_STATE_NOT_STARTED;
        return value;
    }
    overlay_remove(me, (uintptr_t)w);
    bool clean = graph_taint_now == taint;
    bool debug = __builtin_expect(__atomic_load_n(&debug_publish, __ATOMIC_RELAXED) > 0, 0);
    uintptr_t seen = SW_EMPTY;
    if (__atomic_compare_exchange_n(&w->v, &seen, clean ? value : (uintptr_t)SW_POISON, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
        if (debug) debug_finish(me, w, !clean);
        if (clean) {
            atomic_fetch_add_explicit(&stat_published, 1, memory_order_relaxed);
            return value;
        }
        atomic_fetch_add_explicit(&stat_poisoned, 1, memory_order_relaxed);
        overlay_put(me, (uintptr_t)w, value);
        return value;
    }
    if (debug) debug_finish(me, w, true);
    if (seen == SW_POISON) {
        // (Someone's was tainted: every worker keeps its own, clean or not)
        atomic_fetch_add_explicit(&stat_private, 1, memory_order_relaxed);
        overlay_put(me, (uintptr_t)w, value);
        return value;
    }
    // (TTTT_GRAPH_FORCE_SHADOW=1: a clean loser is treated as a tainted one, to exercise the shadowed path and the
    // retire at the file's end; the output must not move)
    static int force_shadow = -1;
    int force = __atomic_load_n(&force_shadow, __ATOMIC_RELAXED);
    if (force < 0) {
        force = getenv("TTTT_GRAPH_FORCE_SHADOW") && *getenv("TTTT_GRAPH_FORCE_SHADOW") == '1';
        __atomic_store_n(&force_shadow, force, __ATOMIC_RELAXED);
    }
    if (clean && !force) {
        // (Nothing read its own before now: a read of a value in flight is a taint event)
        atomic_fetch_add_explicit(&stat_adopted, 1, memory_order_relaxed);
        return seen;
    }
    // A tainted value whose word another worker published meanwhile: this worker keeps its own, and goes by its Overlay
    // first until its file ends (program.c retires it then)
    atomic_fetch_add_explicit(&stat_shadowed, 1, memory_order_relaxed);
    overlay_put(me, (uintptr_t)w, value);
    my graph_shadowed = true;
    return value;
}

bool sw_mine_in_flight(Checker const* me, SharedWord const* w, uint8_t const* state) {
    if (!my graph) return *state == LAZY_STATE_IN_FLIGHT;
    uintptr_t own;
    return overlay_get(me, (uintptr_t)w, &own) && !own;
}

void sw_unclaim(Checker* me, SharedWord const* w) {
    if (my graph && __builtin_expect(__atomic_load_n(&debug_publish, __ATOMIC_RELAXED) > 0, 0)) debug_finish(me, w, true);
}

void sw_abandon(Checker* me, SharedWord const* w, uint8_t* state) {
    if (!my graph) {
        *state = LAZY_STATE_NOT_STARTED;
        return;
    }
    overlay_remove(me, (uintptr_t)w);
    sw_unclaim(me, w);
}

uintptr_t sw_peek(Checker const* me, SharedWord const* w) {
    if (!my graph) return __atomic_load_n(&w->v, __ATOMIC_RELAXED);
    uintptr_t own;
    if (overlay_get(me, (uintptr_t)w, &own)) {
        if (own) TAINT_EVENT(TAINT_READ_TAINTED);
        return own;
    }
    uintptr_t v = sw_load(w);
    return v > SW_POISON ? v : SW_EMPTY;
}

void overlay_print_stats(void) {
    eprintln("graph: shared words published ", (u64)stat_published, ", adopted ", (u64)stat_adopted, ", poisoned ",
        (u64)stat_poisoned, ", computed privately after poison ", (u64)stat_private, ", shadowed ", (u64)stat_shadowed,
        ", overlay hits ", (u64)stat_hits, ", own in flight ", (u64)stat_mine);
}
