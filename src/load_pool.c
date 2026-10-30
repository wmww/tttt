// pthread_attr_setstack(), MAP_STACK, which strict -std=c2x hides
#define _GNU_SOURCE
#include "load_pool.h"
#ifndef MAP_STACK
#define MAP_STACK 0 // (macOS: none, any mapping can be a stack)
#endif
#include "parser.h"
#include "error.h"
#include "embedded_libs.h"
#include <pthread.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>

typedef enum LoadState {
    LOAD_QUEUED, // Nobody has started it
    LOAD_PARSING, // A worker is on it
    LOAD_DONE, // Parsed (or unreadable), waiting to be taken
    LOAD_TAKEN, // The loader has it, or parses it itself: nobody else touches it
} LoadState;

// Where the loader will want a file, roughly: the indices of the path to it from the loader's first wants (libs, roots in
// order, automatic types), a root's i-th import [root, i], its j-th [root, i, j]. The loader takes files in that
// order (depth first, preorder) when the import graph is a tree; below RANK_DEPTH it's queue order
enum { RANK_DEPTH = 16 };

typedef struct Rank {
    u32 at[RANK_DEPTH];
    u32 depth;
} Rank;

static int rank_compare(Rank const* a, Rank const* b) {
    u32 n = a->depth < b->depth ? a->depth : b->depth;
    for (u32 i = 0; i < n; i++) {
        if (a->at[i] != b->at[i]) return a->at[i] < b->at[i] ? -1 : 1;
    }
    return a->depth < b->depth ? -1 : a->depth > b->depth;
}

typedef struct LoadEntry {
    cstr path;
    // LoadState. Leaving LOAD_QUEUED and LOAD_PARSING happens under the pool's lock; LOAD_DONE is left only by the
    // loader, which takes a parsed file without the lock (load_pool_take()). Nothing writes rank, file or errs once the
    // entry is past LOAD_QUEUED but the worker that parses it, before its release store of LOAD_DONE
    _Atomic int state;
    bool is_stdlib;
    bool sized; // In a heap (or not to be, if taken before): its size is known
    i64 size; // The size function's guess
    // When the loader will want it: the lowest rank it was queued with (load_pool_queue()). Workers take files in rank
    // order
    Rank rank;
    isize order; // Queue order, between equal ranks
    bool is_helped; // Parsed by the loader while it waited for another
    double queued_at, ranked_at; // (TTTT_LOAD_POOL_INLINE: when it was queued, and its rank last improved)
    SourceFile* file; // LOAD_DONE: the worker's file, null if unreadable
    ErrorCtx errs; // LOAD_DONE: the file's parse errors
} LoadEntry;

typedef LoadEntry* LoadEntryPtr;
#define g_key LoadEntryPtr, load_entry_ptr
#include <cleat/generic/vec.h>

// A heap item: an entry under a key (an entry may be in a heap several times, under its old ranks: only the item with
// its current one counts)
typedef struct HeapItem {
    i64 size;
    Rank rank;
    LoadEntry* entry;
} HeapItem;

typedef struct Heap {
    HeapItem* items;
    isize count, cap;
    bool by_size; // Biggest first, else by rank
} Heap;

#define g_pro_key SvStr, svstr
#define g_val LoadEntryPtr, load_entry_ptr
#include <cleat/generic/hmap.h>

// The entries by path, sharded by the path's hash with a lock each, apart from the pool's lock: every worker queues
// each file's imports and the loader takes every file, and with one lock for the lookups too the loader spent 17-50% of
// vscode's and openclaw's load+parse waiting for it at 16 threads (the workers 10-20% of their parse time)
enum { POOL_SHARDS = 64 };
typedef struct PoolShard {
    pthread_mutex_t lock;
    HMapLoadEntryPtr by_path;
} __attribute__((aligned(64))) PoolShard;

typedef struct LoadPool {
    LoadPoolParse parse;
    LoadPoolDrop drop;
    void* ctx;
    pthread_mutex_t lock; // Everything below
    pthread_cond_t work; // Something was queued, or stop
    pthread_cond_t done; // A file was parsed
    PoolShard shards[POOL_SHARDS];
    _Atomic isize entry_count; // Each entry's order: queue order, roughly
    // The queued entries (and ones taken since), min-heaps. Files of at least BIG_FILE bytes first, biggest first: one
    // big file started late would be the loader's critical path (tsc's checker.ts, lib.dom.d.ts). Then the rest by rank
    // (the order the loader will want them in: by size, small roots sat queued until the loader parsed them itself)
    Heap big, ranked;
    // Queued entries whose size isn't known yet, from unsized_next on (when there's a size function)
    VecLoadEntryPtr unsized;
    isize unsized_next;
    i64 (*size)(void* ctx, csview path);
    void* size_ctx;
    bool stop;
    bool stopped; // load_pool_stop()
    // Workers waiting on `work`, and whether the loader waits on `done` (for loader_awaits): wakeups go only to threads
    // that sleep and have something to do, after the lock is released (nest: every queued path signalled `work` under
    // the lock, and every parsed file broadcast `done`; the woken thread went straight to sleep again on the lock)
    u32 idle;
    bool loader_waiting;
    LoadEntry* loader_awaits;
    pthread_t* threads;
    u32 thread_count;
    // TTTT_LOAD_POOL_STATS (TTTT_LOAD_POOL_WAITS: each wait over 2 ms)
    isize taken, inline_parses, inline_unqueued; // (Parsed by the loader: of those, never queued)
    double wait_ms, busy_ms, busy_cpu_ms, started_at, helped_ms, inline_ms;
    double loader_cpu_at; // The loader's CPU time when the pool started (it makes and destroys it)
    // Big declaration files' chunks offered to the pool's threads (parse_file_chunked()), taken before files
    ParseHelpers helpers;
    ParseChunkJob* jobs[16];
    u32 job_count;
    bool stats; // TTTT_LOAD_POOL_STATS: CPU time is a real syscall (no vDSO for CLOCK_THREAD_CPUTIME_ID), 2 a file
} LoadPool;

static double thread_cpu_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return (double)now.tv_sec * 1e3 + (double)now.tv_nsec / 1e6;
}

// Under the lock: whether a worker would find something to do (a heap's items may be stale: a guess, erring towards yes)
static bool load_pool_has_work(LoadPool* me) {
    return my big.count || my ranked.count || my job_count || vec_load_entry_ptr_size(&my unsized) > my unsized_next;
}

// After the lock is released: wakes up to `units` sleeping workers (a stale `idle` only costs a wakeup or delays one
// until the next: a worker increments it under the lock before it waits, so one that waits on work queued before
// this sees it)
static void load_pool_wake(LoadPool* me, isize units) {
    u32 idle = __atomic_load_n(&my idle, __ATOMIC_RELAXED);
    if (units >= idle) {
        if (idle) pthread_cond_broadcast(&my work);
        return;
    }
    for (isize i = 0; i < units; i++) pthread_cond_signal(&my work);
}

// (Address space only: pages are committed as the stack grows)
size_t worker_stack_size(void) {
    struct rlimit limit;
    size_t stack = 64u << 20;
    if (!getrlimit(RLIMIT_STACK, &limit) && limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur > stack) stack = limit.rlim_cur;
    if (stack > (256u << 20)) stack = 256u << 20;
    return stack;
}

#ifdef _WIN32
// Windows: a thread's stack is the system's, reserved at the size the executable asks (meson.build's --stack: 256 MB,
// committed as it grows); a stack of ours can't be handed over (winpthreads would take its size as memory to commit)
int worker_thread_create(pthread_t* thread, void* (*fn)(void*), void* arg) {
    return pthread_create(thread, nullptr, fn, arg);
}

void worker_thread_join(pthread_t thread) {
    pthread_join(thread, nullptr);
}
#else
// (A guard page below each stack, as glibc's own)
enum { STACK_GUARD = 64 << 10 };
static struct {
    pthread_mutex_t lock;
    char** free; // Stack bases (above the guard)
    u32 free_count, free_cap;
    struct { pthread_t thread; char* stack; }* used;
    u32 used_count, used_cap;
    size_t size;
} stacks = { .lock = PTHREAD_MUTEX_INITIALIZER };

int worker_thread_create(pthread_t* thread, void* (*fn)(void*), void* arg) {
    pthread_mutex_lock(&stacks.lock);
    if (!stacks.size) stacks.size = worker_stack_size();
    char* stack;
    if (stacks.free_count) {
        stack = stacks.free[--stacks.free_count];
    } else {
        char* raw = mmap(nullptr, stacks.size + STACK_GUARD, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
        if (raw == MAP_FAILED) {
            pthread_mutex_unlock(&stacks.lock);
            return EAGAIN;
        }
        vma_name(raw, stacks.size + STACK_GUARD, "stack");
        // (Nothing is mapped in yet: no flush)
        mprotect(raw, STACK_GUARD, PROT_NONE);
        stack = raw + STACK_GUARD;
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, stacks.size);
    int ret = pthread_create(thread, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    if (ret) {
        if (stacks.free_count == stacks.free_cap) {
            stacks.free_cap = stacks.free_cap ? stacks.free_cap * 2 : 32;
            stacks.free = realloc(stacks.free, sizeof(char*) * stacks.free_cap);
            check(stacks.free);
        }
        stacks.free[stacks.free_count++] = stack;
    } else {
        if (stacks.used_count == stacks.used_cap) {
            stacks.used_cap = stacks.used_cap ? stacks.used_cap * 2 : 32;
            stacks.used = realloc(stacks.used, sizeof(*stacks.used) * stacks.used_cap);
            check(stacks.used);
        }
        stacks.used[stacks.used_count].thread = *thread;
        stacks.used[stacks.used_count++].stack = stack;
    }
    pthread_mutex_unlock(&stacks.lock);
    return ret;
}

void worker_thread_join(pthread_t thread) {
    pthread_join(thread, nullptr);
    // (Joined, the thread is gone: its stack, TLS and descriptor included, glibc's at the stack's top)
    pthread_mutex_lock(&stacks.lock);
    u32 i = 0;
    while (i < stacks.used_count && !pthread_equal(stacks.used[i].thread, thread)) i++;
    check(i < stacks.used_count);
    char* stack = stacks.used[i].stack;
    stacks.used[i] = stacks.used[--stacks.used_count];
    if (stacks.free_count == stacks.free_cap) {
        stacks.free_cap = stacks.free_cap ? stacks.free_cap * 2 : 32;
        stacks.free = realloc(stacks.free, sizeof(char*) * stacks.free_cap);
        check(stacks.free);
    }
    stacks.free[stacks.free_count++] = stack;
    pthread_mutex_unlock(&stacks.lock);
}
#endif

// Unsized paths a worker stats at once
enum { SIZE_BATCH = 32 };

// (A bigger file is a longer parse than a typical module: ~1 ms)
enum { BIG_FILE = 128 << 10 };

static bool item_before(Heap const* heap, HeapItem const* a, HeapItem const* b) {
    if (heap->by_size && a->size != b->size) return a->size > b->size;
    int c = heap->by_size ? 0 : rank_compare(&a->rank, &b->rank);
    return c ? c < 0 : a->entry->order < b->entry->order;
}

static void heap_push(Heap* heap, LoadEntry* entry) {
    if (heap->count == heap->cap) {
        heap->cap = heap->cap ? heap->cap * 2 : 256;
        heap->items = realloc(heap->items, sizeof(HeapItem) * (size_t)heap->cap);
        panic_if(!heap->items, "out of memory");
    }
    HeapItem* items = heap->items;
    isize i = heap->count++;
    items[i] = (HeapItem){ .size = entry->size, .rank = entry->rank, .entry = entry };
    while (i > 0) {
        isize parent = (i - 1) / 2;
        if (!item_before(heap, &items[i], &items[parent])) break;
        c_swap(&items[i], &items[parent]);
        i = parent;
    }
}

static HeapItem heap_pop(Heap* heap) {
    HeapItem* items = heap->items;
    HeapItem ret = items[0];
    isize size = --heap->count;
    items[0] = items[size];
    for (isize i = 0;;) {
        isize best = i, left = 2 * i + 1, right = left + 1;
        if (left < size && item_before(heap, &items[left], &items[best])) best = left;
        if (right < size && item_before(heap, &items[right], &items[best])) best = right;
        if (best == i) break;
        c_swap(&items[i], &items[best]);
        i = best;
    }
    return ret;
}

// Under the lock: a sized queued entry into its heap
static void load_pool_schedule(LoadPool* me, LoadEntry* entry) {
    entry->sized = true;
    heap_push(entry->size >= BIG_FILE ? &my big : &my ranked, entry);
}

// The next queued entry, now parsing, null if there's none. Under the lock
static LoadEntry* load_pool_next(LoadPool* me) {
    while (my big.count) {
        LoadEntry* entry = heap_pop(&my big).entry;
        if (atomic_load_explicit(&entry->state, memory_order_relaxed) != LOAD_QUEUED) continue;
        atomic_store_explicit(&entry->state, LOAD_PARSING, memory_order_relaxed);
        return entry;
    }
    while (my ranked.count) {
        HeapItem item = heap_pop(&my ranked);
        if (atomic_load_explicit(&item.entry->state, memory_order_relaxed) != LOAD_QUEUED || rank_compare(&item.rank, &item.entry->rank)) continue;
        atomic_store_explicit(&item.entry->state, LOAD_PARSING, memory_order_relaxed);
        return item.entry;
    }
    return nullptr;
}

// The rank of the file this thread is parsing (a worker, or the loader helping), or the loader took last: what it
// finds in it ranks below it, in the order it is queued. None (depth 0) for the loader's own queueing before it took
// anything: libs, roots, automatic types, in queue order
static thread_local Rank current_rank;
static thread_local u32 current_children;

// The rank of the next path this thread queues
static Rank next_rank(isize order) {
    Rank ret = current_rank;
    if (!ret.depth) ret = (Rank){ .at = { (u32)order }, .depth = 1 };
    else if (ret.depth < RANK_DEPTH) ret.at[ret.depth++] = current_children++;
    return ret;
}

// Parses a LOAD_PARSING entry, unlocking meanwhile
static void load_pool_run(LoadPool* me, LoadEntry* entry) {
    pthread_mutex_unlock(&my lock);
    // (Nobody else touches the entry's path, errs or file while it's parsing)
    struct timespec start, end;
    timespec_get(&start, TIME_UTC);
    double cpu_start = my stats ? thread_cpu_ms() : 0;
    Rank outer_rank = current_rank;
    u32 outer_children = current_children;
    current_rank = entry->rank;
    current_children = 0;
    SourceFile* file = my parse(my ctx, me, cstr_sv(&entry->path), entry->is_stdlib, &entry->errs);
    current_rank = outer_rank;
    current_children = outer_children;
    timespec_get(&end, TIME_UTC);
    double cpu = my stats ? thread_cpu_ms() - cpu_start : 0;
    pthread_mutex_lock(&my lock);
    my busy_cpu_ms += cpu;
    my busy_ms += (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
    if (entry->is_helped) my helped_ms += (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
    entry->file = file;
    atomic_store_explicit(&entry->state, LOAD_DONE, memory_order_release);
    // (The loader, waiting, wants its file, or queued work to help with meanwhile)
    if (my loader_waiting && (entry == my loader_awaits || load_pool_has_work(me))) {
        my loader_waiting = false;
        pthread_mutex_unlock(&my lock);
        pthread_cond_signal(&my done);
        pthread_mutex_lock(&my lock);
    }
}

// Sizes a batch of the unsized entries, unlocking meanwhile: true if there were some. (The stats are the workers', not
// the loader's: a root list of thousands, and every new path a worker finds)
static bool load_pool_size_batch(LoadPool* me) {
    isize pending = vec_load_entry_ptr_size(&my unsized) - my unsized_next;
    if (!pending) return false;
    LoadEntry* batch[SIZE_BATCH];
    isize count = pending < SIZE_BATCH ? pending : SIZE_BATCH;
    for (isize i = 0; i < count; i++) batch[i] = my unsized.data[my unsized_next++];
    if (my unsized_next == vec_load_entry_ptr_size(&my unsized)) {
        vec_load_entry_ptr_clear(&my unsized);
        my unsized_next = 0;
    }
    pthread_mutex_unlock(&my lock);
    // (path and size are this batch's until it's in the heap: nobody else writes them)
    // (An embedded lib has no file to stat: its size is the binary's)
    for (isize i = 0; i < count; i++) {
        EmbeddedLib const* lib = embedded_lib_at(cstr_sv(&batch[i]->path));
        batch[i]->size = lib ? (i64)lib->size : my size(my size_ctx, cstr_sv(&batch[i]->path));
    }
    pthread_mutex_lock(&my lock);
    isize scheduled = 0;
    for (isize i = 0; i < count; i++) {
        if (atomic_load_explicit(&batch[i]->state, memory_order_relaxed) == LOAD_QUEUED) {
            load_pool_schedule(me, batch[i]);
            scheduled++;
        }
    }
    // (This worker takes one of them; the others for whoever sleeps)
    if (scheduled > 1 && my idle) {
        pthread_mutex_unlock(&my lock);
        load_pool_wake(me, scheduled - 1);
        pthread_mutex_lock(&my lock);
    }
    return true;
}

static void pool_offer(void* ctx, ParseChunkJob* job) {
    LoadPool* me = ctx;
    pthread_mutex_lock(&my lock);
    // (More at once than that: the rest parse their chunks themselves)
    if (my job_count < c_arraylen(my jobs)) my jobs[my job_count++] = job;
    pthread_cond_broadcast(&my work);
    pthread_cond_broadcast(&my done);
    pthread_mutex_unlock(&my lock);
}

// Under the lock
static void pool_remove_job(LoadPool* me, u32 i) {
    memmove(&my jobs[i], &my jobs[i + 1], sizeof(ParseChunkJob*) * (my job_count - i - 1));
    my job_count--;
}

static void pool_retract(void* ctx, ParseChunkJob* job) {
    LoadPool* me = ctx;
    pthread_mutex_lock(&my lock);
    for (u32 i = 0; i < my job_count; i++) {
        if (my jobs[i] == job) {
            pool_remove_job(me, i);
            break;
        }
    }
    pthread_mutex_unlock(&my lock);
}

// Under the lock: helps with an offered chunk job, unlocking meanwhile; false if there was none to help with
static bool pool_help(LoadPool* me) {
    while (my job_count) {
        ParseChunkJob* job = my jobs[0];
        if (!parse_chunk_job_join(job)) {
            pool_remove_job(me, 0);
            continue;
        }
        pthread_mutex_unlock(&my lock);
        parse_chunk_job_help(job);
        pthread_mutex_lock(&my lock);
        return true;
    }
    return false;
}

ParseHelpers const* load_pool_helpers(LoadPool* me) {
    return my thread_count > 1 ? &my helpers : nullptr;
}

static void* load_pool_worker(void* arg) {
    LoadPool* me = arg;
    parse_set_helpers(load_pool_helpers(me));
    pthread_mutex_lock(&my lock);
    while (!my stop) {
        if (pool_help(me)) continue;
        if (load_pool_size_batch(me)) continue;
        LoadEntry* entry = load_pool_next(me);
        if (entry) load_pool_run(me, entry);
        else {
            // (Atomic: load_pool_wake() reads it without the lock)
            __atomic_fetch_add(&my idle, 1, __ATOMIC_RELAXED);
            pthread_cond_wait(&my work, &my lock);
            __atomic_fetch_sub(&my idle, 1, __ATOMIC_RELAXED);
        }
    }
    pthread_mutex_unlock(&my lock);
    return nullptr;
}

LoadPool* load_pool_new(
    u32 threads, LoadPoolParse parse, LoadPoolDrop drop, void* ctx, i64 (*size)(void* ctx, csview path), void* size_ctx
) {
    check(threads > 0);
    LoadPool* me = c_new(LoadPool, {
        .parse = parse, .drop = drop, .ctx = ctx, .thread_count = threads, .size = size, .size_ctx = size_ctx,
        .big = { .by_size = true },
    });
    my helpers = (ParseHelpers){ .ctx = me, .offer = pool_offer, .retract = pool_retract };
    pthread_mutex_init(&my lock, nullptr);
    for (u32 i = 0; i < POOL_SHARDS; i++) pthread_mutex_init(&my shards[i].lock, nullptr);
    pthread_cond_init(&my work, nullptr);
    pthread_cond_init(&my done, nullptr);
    my threads = c_new_n(pthread_t, threads);
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    my started_at = (double)now.tv_sec * 1e3 + (double)now.tv_nsec / 1e6;
    my stats = getenv("TTTT_LOAD_POOL_STATS");
    my loader_cpu_at = my stats ? thread_cpu_ms() : 0;
    for (u32 t = 0; t < threads; t++) {
        panic_if(worker_thread_create(&my threads[t], load_pool_worker, me), "couldn't start a loader thread");
    }
    return me;
}

static PoolShard* pool_shard(LoadPool* me, csview path) {
    uint32_t hash = 2166136261u;
    for (isize i = 0; i < path.size; i++) hash = (hash ^ (uint8_t)path.buf[i]) * 16777619u;
    return &my shards[hash % POOL_SHARDS];
}

// The entry for path, null if there's none
static LoadEntry* pool_find(LoadPool* me, csview path) {
    PoolShard* shard = pool_shard(me, path);
    pthread_mutex_lock(&shard->lock);
    auto found = hmap_load_entry_ptr_get(&shard->by_path, path);
    LoadEntry* ret = found ? found->second : nullptr;
    pthread_mutex_unlock(&shard->lock);
    return ret;
}

// The entry for path, made in state (and *added set) if there's none. Its rank is the next one this thread queues,
// either way (rank: that rank for an entry that was there)
static LoadEntry* pool_find_or_add(LoadPool* me, csview path, bool is_stdlib, LoadState state, bool* added, Rank* rank) {
    PoolShard* shard = pool_shard(me, path);
    pthread_mutex_lock(&shard->lock);
    auto found = hmap_load_entry_ptr_get(&shard->by_path, path);
    LoadEntry* entry;
    if (found) {
        entry = found->second;
        *rank = next_rank(entry->order);
        *added = false;
    } else {
        isize order = atomic_fetch_add_explicit(&my entry_count, 1, memory_order_relaxed);
        entry = c_new(LoadEntry, { .path = cstr_from_sv(path), .is_stdlib = is_stdlib, .rank = next_rank(order), .order = order });
        atomic_init(&entry->state, state);
        hmap_load_entry_ptr_insert(&shard->by_path, cstr_from_sv(path), entry);
        *rank = entry->rank;
        *added = true;
    }
    pthread_mutex_unlock(&shard->lock);
    return entry;
}

// Under the lock: a found entry the loader will want sooner than its rank says moves up (an import of a later root from
// an earlier one)
static void load_pool_rerank_locked(LoadPool* me, LoadEntry* entry, Rank const* rank) {
    if (atomic_load_explicit(&entry->state, memory_order_relaxed) == LOAD_QUEUED && rank_compare(rank, &entry->rank) < 0) {
        entry->rank = *rank;
        entry->ranked_at = wall_ms();
        if (entry->sized && entry->size < BIG_FILE) heap_push(&my ranked, entry);
    }
}

// Under the lock: a new entry waits for its size (a stat) in the unsized list, which workers take in batches
static void load_pool_schedule_new_locked(LoadPool* me, LoadEntry* entry) {
    entry->queued_at = entry->ranked_at = wall_ms();
    if (my size) vec_load_entry_ptr_push(&my unsized, entry);
    else load_pool_schedule(me, entry);
}

// Under the lock, which it releases: wakes workers for `added` new paths (unsized ones are sized SIZE_BATCH at a time:
// a worker per batch, which wakes others for what it sized), and the loader if it waits with nothing to help with
static void load_pool_queued(LoadPool* me, isize added) {
    bool wake_loader = added && my loader_waiting && added > my idle;
    if (wake_loader) my loader_waiting = false;
    pthread_mutex_unlock(&my lock);
    if (!added) return;
    load_pool_wake(me, my size ? (added + SIZE_BATCH - 1) / SIZE_BATCH : added);
    if (wake_loader) pthread_cond_signal(&my done);
}

// Queues the paths: each looked up (or added) under its shard's lock, then what the pool's own state needs of them
// (new entries to size and schedule, queued ones to rerank) under the pool's lock, once, and only if there is any
void load_pool_queue_each(LoadPool* me, csview const* paths, bool const* is_stdlib, isize count) {
    enum { STEP = 64 };
    for (isize from = 0; from < count; from += STEP) {
        isize n = count - from < STEP ? count - from : STEP;
        LoadEntry* entries[STEP];
        Rank ranks[STEP];
        bool added[STEP];
        isize pending = 0, new_count = 0;
        for (isize i = 0; i < n; i++) {
            LoadEntry* entry = pool_find_or_add(me, paths[from + i], is_stdlib[from + i], LOAD_QUEUED, &added[pending], &ranks[pending]);
            // (Past LOAD_QUEUED nothing more to do: it never goes back)
            if (!added[pending] && atomic_load_explicit(&entry->state, memory_order_relaxed) != LOAD_QUEUED) continue;
            new_count += added[pending];
            entries[pending++] = entry;
        }
        if (!pending) continue;
        pthread_mutex_lock(&my lock);
        for (isize i = 0; i < pending; i++) {
            if (added[i]) load_pool_schedule_new_locked(me, entries[i]);
            else load_pool_rerank_locked(me, entries[i], &ranks[i]);
        }
        load_pool_queued(me, new_count);
    }
}

void load_pool_queue(LoadPool* me, csview path, bool is_stdlib) {
    load_pool_queue_each(me, &path, &is_stdlib, 1);
}

void load_pool_queue_many(LoadPool* me, csview const* paths, isize count, bool is_stdlib) {
    enum { STEP = 64 };
    bool flags[STEP];
    for (isize i = 0; i < STEP; i++) flags[i] = is_stdlib;
    for (isize from = 0; from < count; from += STEP) load_pool_queue_each(me, paths + from, flags, count - from < STEP ? count - from : STEP);
}

SourceFile* load_pool_take(LoadPool* me, csview path, bool is_stdlib, ErrorCtx* errs) {
    LoadEntry* entry = pool_find(me, path);
    // A parsed file is taken without the pool's lock: only the loader leaves LOAD_DONE, and nothing changes what a worker
    // wrote before it stored LOAD_DONE
    if (entry && atomic_load_explicit(&entry->state, memory_order_acquire) == LOAD_DONE) {
        SourceFile* ret = nullptr;
        if (entry->file && entry->is_stdlib == is_stdlib) {
            ret = entry->file;
            entry->file = nullptr;
            my taken++;
        } else {
            // (A file parsed as the other kind stays the pool's, dropped at the end)
            my inline_parses++;
        }
        atomic_store_explicit(&entry->state, LOAD_TAKEN, memory_order_relaxed);
        current_rank = entry->rank;
        current_children = 0;
        if (ret) error_ctx_merge(errs, &entry->errs);
        return ret;
    }
    bool found = entry != nullptr;
    if (!found) {
        bool added;
        Rank rank;
        entry = pool_find_or_add(me, path, is_stdlib, LOAD_TAKEN, &added, &rank);
        // (Queued by a worker meanwhile: as if found)
        found = !added;
    }
    pthread_mutex_lock(&my lock);
    my inline_unqueued += !found;
    if (atomic_load_explicit(&entry->state, memory_order_relaxed) == LOAD_PARSING) {
        struct timespec start, end;
        timespec_get(&start, TIME_UTC);
        // (Rather than idle, the loader parses what's queued, like a worker)
        while (atomic_load_explicit(&entry->state, memory_order_relaxed) == LOAD_PARSING) {
            if (pool_help(me)) continue;
            LoadEntry* other = load_pool_next(me);
            if (other) {
                other->is_helped = true;
                load_pool_run(me, other);
            }
            else {
                my loader_waiting = true;
                my loader_awaits = entry;
                pthread_cond_wait(&my done, &my lock);
                my loader_waiting = false;
            }
        }
        timespec_get(&end, TIME_UTC);
        double waited = (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
        my wait_ms += waited;
        if (waited > 2 && getenv("TTTT_LOAD_POOL_WAITS")) eprintln("waited ", waited, " ms for ", path);
    }
    LoadState state = atomic_load_explicit(&entry->state, memory_order_relaxed);
    SourceFile* ret = nullptr;
    // (A file parsed as the other kind stays the pool's, dropped at the end)
    if (state == LOAD_DONE && entry->file && entry->is_stdlib == is_stdlib) {
        ret = entry->file;
        entry->file = nullptr;
        my taken++;
    } else {
        my inline_parses++;
        if (found && getenv("TTTT_LOAD_POOL_INLINE")) {
            cstr rank = cstr_null;
            for (u32 i = 0; i < entry->rank.depth; i++) cstr_append_d(&rank, DEFAULT_DISPLAY, i ? "." : "", (i64)entry->rank.at[i]);
            eprintln("load pool: the loader parses ", path, " (rank ", rank, " of ", atomic_load(&my entry_count),
                ", ", entry->size, " bytes, queued ", wall_ms() - entry->queued_at, " ms ago, ranked ",
                wall_ms() - entry->ranked_at, " ms ago, ",
                state == LOAD_QUEUED ? entry->sized ? "waiting" : "unsized" : "parsed as the other kind", ")");
            cstr_drop(&rank);
        }
    }
    bool had_errors = state == LOAD_DONE && ret;
    atomic_store_explicit(&entry->state, LOAD_TAKEN, memory_order_relaxed);
    current_rank = entry->rank;
    current_children = 0;
    pthread_mutex_unlock(&my lock);
    // (The entry is the loader's now)
    if (had_errors) error_ctx_merge(errs, &entry->errs);
    return ret;
}

bool load_pool_in_flight(LoadPool* me, csview path) {
    LoadEntry* entry = pool_find(me, path);
    if (!entry) return false;
    LoadState state = atomic_load_explicit(&entry->state, memory_order_relaxed);
    return state == LOAD_PARSING || state == LOAD_QUEUED;
}

void load_pool_note_inline(LoadPool* me, double ms) {
    my inline_ms += ms;
}

void load_pool_stop(LoadPool* me) {
    if (!me || my stopped) return;
    my stopped = true;
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    double lifetime = (double)now.tv_sec * 1e3 + (double)now.tv_nsec / 1e6 - my started_at;
    pthread_mutex_lock(&my lock);
    my stop = true;
    pthread_cond_broadcast(&my work);
    pthread_mutex_unlock(&my lock);
    for (u32 t = 0; t < my thread_count; t++) worker_thread_join(my threads[t]);
    free(my threads);
    my threads = nullptr;
    isize wasted = 0, never = 0;
    for (u32 s = 0; s < POOL_SHARDS; s++) {
        for_each (i, hmap_load_entry_ptr, &my shards[s].by_path) {
            LoadEntry* entry = i->second;
            if (entry->file) {
                wasted++;
                my drop(my ctx, entry->file);
                entry->file = nullptr;
            }
            never += atomic_load_explicit(&entry->state, memory_order_relaxed) == LOAD_QUEUED;
        }
    }
    if (my stats) {
        eprintln("load pool: ", my thread_count, " threads, ", atomic_load(&my entry_count), " paths, ", my taken,
            " preparsed files taken, ", my inline_parses, " parsed by the loader (", my inline_unqueued, " never queued), ", wasted, " parsed for nothing, ", never,
            " never started");
        eprintln("load pool: the loader waited ", my wait_ms, " ms (parsing queued files for ", my helped_ms,
            " ms of it); workers busy ", my busy_ms, " ms of ", lifetime * my thread_count, " (on a CPU for ", my busy_cpu_ms, " ms of it)");
        eprintln("load pool: the loader parsed ", my inline_ms, " ms itself; its own work the rest of the pool's ",
            lifetime, " ms: ", lifetime - my wait_ms - my inline_ms);
        // (CPU time, which a loaded machine's preemption and lock waits don't inflate: what the loader did)
        eprintln("load pool: the loader's CPU time ", thread_cpu_ms() - my loader_cpu_at, " ms");
    }
}

void load_pool_destroy(LoadPool* me) {
    if (!me) return;
    load_pool_stop(me);
    for (u32 s = 0; s < POOL_SHARDS; s++) {
        PoolShard* shard = &my shards[s];
        for_each (i, hmap_load_entry_ptr, &shard->by_path) {
            LoadEntry* entry = i->second;
            error_ctx_drop(&entry->errs);
            cstr_drop(&entry->path);
            free(entry);
        }
        hmap_load_entry_ptr_drop(&shard->by_path);
        pthread_mutex_destroy(&shard->lock);
    }
    free(my big.items);
    free(my ranked.items);
    vec_load_entry_ptr_drop(&my unsized);
    pthread_cond_destroy(&my work);
    pthread_cond_destroy(&my done);
    pthread_mutex_destroy(&my lock);
    free(me);
}
