// --shared-graph's seeded deterministic scheduler (TTTT_GRAPH_SEED=N, plan 86 4f's gates; scripts/sharedgraph/seeds.sh):
// one token, held by the one worker thread allowed to run. At each scheduling point (a lazy field claimed, an identity
// published, memo entries flushed, a file started, every round of a wait for another worker's field) the holder draws
// from a seeded generator and, one time in TTTT_GRAPH_SEED_RATE (default 16), hands the token to a runnable thread the
// generator picks; a waiting holder always hands it on. Since only the holder runs and the generator is only drawn
// under the token, the whole interleaving is a function of the seed: a failing schedule replays. The run is about
// single-threaded speed (K1's), which is fine for a gate run on idle cores. Threads are interchangeable: they take
// their slots and shares in the order they get the token. Needs --checker-memory off (the graph-wide recycle's barrier
// blocks a thread outside the token: panics) and no materialize-ahead helpers (--threads equal to --checkers).
// Without the knob nothing here runs: graph_sched_on is false and every hook is one test of it, on --shared-graph's
// paths only
#include "ty/graph.h"
#include <pthread.h>
#include <stdatomic.h>

bool graph_sched_on;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t turns[64]; // each slot's: woken alone when it gets the token
static u64 rng;
static u32 rate = 16;
static int holder = -1; // the running slot, -1 before all have come or after all have left
static u32 expected, arrived, next_slot;
static uint8_t state[64]; // 0 not yet, 1 runnable, 2 left
static u64 handoffs, points;
static thread_local int my_slot = -1;

static u64 draw(void) {
    // (splitmix64)
    u64 z = (rng += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// A runnable slot, other than `not` if there is another; -1 if none (under the lock)
static int pick(int not) {
    int candidates[64];
    u32 n = 0;
    for (u32 i = 0; i < expected; i++) if (state[i] == 1 && (int)i != not) candidates[n++] = (int)i;
    if (!n) return not >= 0 && state[not] == 1 ? not : -1;
    return candidates[draw() % n];
}

static void wait_turn(void) {
    while (holder != my_slot) pthread_cond_wait(&turns[my_slot], &lock);
}

static void give(int next) {
    holder = next;
    if (next >= 0) pthread_cond_signal(&turns[next]);
}

void graph_sched_init(u32 threads) {
    char const* seed = getenv("TTTT_GRAPH_SEED");
    if (!seed || threads < 2 || threads > 64) return;
    rng = strtoull(seed, nullptr, 10) * 0x2545f4914f6cdd1dull + 1;
    char const* r = getenv("TTTT_GRAPH_SEED_RATE");
    rate = r && atoi(r) > 0 ? (u32)atoi(r) : 16;
    expected = threads;
    arrived = next_slot = 0;
    holder = -1;
    memset(state, 0, sizeof state);
    handoffs = points = 0;
    static bool made;
    if (!made) for (u32 i = 0; i < 64; i++) pthread_cond_init(&turns[i], nullptr);
    made = true;
    graph_sched_on = true;
}

void graph_sched_enter(void) {
    if (!graph_sched_on) return;
    pthread_mutex_lock(&lock);
    my_slot = (int)next_slot++;
    state[my_slot] = 1;
    if (++arrived == expected) give(pick(-1));
    wait_turn();
    pthread_mutex_unlock(&lock);
}

void graph_sched_leave(void) {
    if (!graph_sched_on || my_slot < 0) return;
    pthread_mutex_lock(&lock);
    state[my_slot] = 2;
    give(pick(-1));
    pthread_mutex_unlock(&lock);
    my_slot = -1;
}

void graph_sched_point_slow(bool waiting) {
    if (my_slot < 0) return;
    pthread_mutex_lock(&lock);
    panic_if(holder != my_slot, "TTTT_GRAPH_SEED: a thread ran without the token");
    points++;
    if (waiting || draw() % rate == 0) {
        int next = pick(my_slot);
        if (next >= 0 && next != my_slot) {
            handoffs++;
            give(next);
            wait_turn();
        }
    }
    pthread_mutex_unlock(&lock);
}

void graph_sched_print(void) {
    if (graph_sched_on) eprintln("graph: seeded scheduler: ", points, " points, ", handoffs, " handoffs");
}
