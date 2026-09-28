#include "load_pool.h"
#include "error.h"
#include <pthread.h>
#include <sys/resource.h>
#include <time.h>

typedef enum LoadState {
    LOAD_QUEUED, // Nobody has started it
    LOAD_PARSING, // A worker is on it
    LOAD_DONE, // Parsed (or unreadable), waiting to be taken
    LOAD_TAKEN, // The loader has it, or parses it itself: nobody else touches it
} LoadState;

typedef struct LoadEntry {
    cstr path;
    bool is_stdlib;
    LoadState state;
    SourceFile* file; // LOAD_DONE: the worker's file, null if unreadable
    ErrorCtx errs; // LOAD_DONE: the file's parse errors
} LoadEntry;

typedef LoadEntry* LoadEntryPtr;
#define g_key LoadEntryPtr, load_entry_ptr
#include <cleat/generic/vec.h>

#define g_pro_key SvStr, svstr
#define g_val LoadEntryPtr, load_entry_ptr
#include <cleat/generic/hmap.h>

typedef struct LoadPool {
    LoadPoolParse parse;
    LoadPoolDrop drop;
    void* ctx;
    pthread_mutex_t lock; // Everything below
    pthread_cond_t work; // Something was queued, or stop
    pthread_cond_t done; // A file was parsed
    VecLoadEntryPtr entries; // In queue order
    HMapLoadEntryPtr by_path;
    isize next; // The first entry no worker has looked at
    bool stop;
    pthread_t* threads;
    u32 thread_count;
    // TTTT_LOAD_POOL_STATS
    isize taken, inline_parses;
    double wait_ms;
} LoadPool;

size_t worker_stack_size(void) {
    struct rlimit limit;
    size_t stack = 8u << 20;
    if (!getrlimit(RLIMIT_STACK, &limit) && limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur > stack) stack = limit.rlim_cur;
    if (stack > (256u << 20)) stack = 256u << 20;
    return stack;
}

static void* load_pool_worker(void* arg) {
    LoadPool* me = arg;
    pthread_mutex_lock(&my lock);
    while (true) {
        while (!my stop && my next >= vec_load_entry_ptr_size(&my entries)) pthread_cond_wait(&my work, &my lock);
        if (my stop) break;
        LoadEntry* entry = *vec_load_entry_ptr_at(&my entries, my next++);
        if (entry->state != LOAD_QUEUED) continue;
        entry->state = LOAD_PARSING;
        pthread_mutex_unlock(&my lock);
        // (Nobody else touches the entry's path, errs or file while it's parsing)
        SourceFile* file = my parse(my ctx, me, cstr_sv(&entry->path), entry->is_stdlib, &entry->errs);
        pthread_mutex_lock(&my lock);
        entry->file = file;
        entry->state = LOAD_DONE;
        pthread_cond_broadcast(&my done);
    }
    pthread_mutex_unlock(&my lock);
    return nullptr;
}

LoadPool* load_pool_new(u32 threads, LoadPoolParse parse, LoadPoolDrop drop, void* ctx) {
    check(threads > 0);
    LoadPool* me = c_new(LoadPool, { .parse = parse, .drop = drop, .ctx = ctx, .thread_count = threads });
    pthread_mutex_init(&my lock, nullptr);
    pthread_cond_init(&my work, nullptr);
    pthread_cond_init(&my done, nullptr);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, worker_stack_size());
    my threads = c_new_n(pthread_t, threads);
    for (u32 t = 0; t < threads; t++) {
        panic_if(pthread_create(&my threads[t], &attr, load_pool_worker, me), "couldn't start a loader thread");
    }
    pthread_attr_destroy(&attr);
    return me;
}

// Under the lock
static LoadEntry* load_pool_add(LoadPool* me, csview path, bool is_stdlib, LoadState state) {
    LoadEntry* entry = c_new(LoadEntry, { .path = cstr_from_sv(path), .is_stdlib = is_stdlib, .state = state });
    vec_load_entry_ptr_push(&my entries, entry);
    hmap_load_entry_ptr_insert(&my by_path, cstr_from_sv(path), entry);
    return entry;
}

void load_pool_queue(LoadPool* me, csview path, bool is_stdlib) {
    pthread_mutex_lock(&my lock);
    if (!hmap_load_entry_ptr_get(&my by_path, path)) {
        load_pool_add(me, path, is_stdlib, LOAD_QUEUED);
        pthread_cond_signal(&my work);
    }
    pthread_mutex_unlock(&my lock);
}

SourceFile* load_pool_take(LoadPool* me, csview path, bool is_stdlib, ErrorCtx* errs) {
    pthread_mutex_lock(&my lock);
    auto found = hmap_load_entry_ptr_get(&my by_path, path);
    LoadEntry* entry = found ? found->second : load_pool_add(me, path, is_stdlib, LOAD_TAKEN);
    if (entry->state == LOAD_PARSING) {
        struct timespec start, end;
        timespec_get(&start, TIME_UTC);
        while (entry->state == LOAD_PARSING) pthread_cond_wait(&my done, &my lock);
        timespec_get(&end, TIME_UTC);
        my wait_ms += (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
    }
    SourceFile* ret = nullptr;
    // (A file parsed as the other kind stays the pool's, dropped at the end)
    if (entry->state == LOAD_DONE && entry->file && entry->is_stdlib == is_stdlib) {
        ret = entry->file;
        entry->file = nullptr;
        my taken++;
    } else {
        my inline_parses++;
    }
    bool had_errors = entry->state == LOAD_DONE && ret;
    entry->state = LOAD_TAKEN;
    pthread_mutex_unlock(&my lock);
    // (The entry is the loader's now)
    if (had_errors) error_ctx_merge(errs, &entry->errs);
    return ret;
}

void load_pool_destroy(LoadPool* me) {
    if (!me) return;
    pthread_mutex_lock(&my lock);
    my stop = true;
    pthread_cond_broadcast(&my work);
    pthread_mutex_unlock(&my lock);
    for (u32 t = 0; t < my thread_count; t++) pthread_join(my threads[t], nullptr);
    free(my threads);
    isize wasted = 0, never = 0;
    for_each (i, vec_load_entry_ptr, &my entries) {
        LoadEntry* entry = *i;
        if (entry->file) {
            wasted++;
            my drop(my ctx, entry->file);
        }
        never += entry->state == LOAD_QUEUED;
        error_ctx_drop(&entry->errs);
        cstr_drop(&entry->path);
        free(entry);
    }
    if (getenv("TTTT_LOAD_POOL_STATS")) {
        eprintln("load pool: ", my thread_count, " threads, ", vec_load_entry_ptr_size(&my entries), " paths, ", my taken,
            " preparsed files taken, ", my inline_parses, " parsed by the loader, ", wasted, " parsed for nothing, ", never,
            " never started; the loader waited ", my wait_ms, " ms");
    }
    vec_load_entry_ptr_drop(&my entries);
    hmap_load_entry_ptr_drop(&my by_path);
    pthread_cond_destroy(&my work);
    pthread_cond_destroy(&my done);
    pthread_mutex_destroy(&my lock);
    free(me);
}
