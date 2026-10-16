#include "cached_fs.h"
#include <pthread.h>

// What is known about a path: each question asked once (and what one answer implies about the others)
enum {
    PROBE_EXISTS_KNOWN = 1 << 0,
    PROBE_EXISTS = 1 << 1,
    PROBE_FILE_KNOWN = 1 << 2,
    PROBE_FILE = 1 << 3,
    PROBE_DIRECTORY_KNOWN = 1 << 4,
    PROBE_DIRECTORY = 1 << 5,
};

typedef uint8_t ProbeBits;
#define g_pro_key SvStr, svstr
#define g_val ProbeBits, probe_bits
#include <cleat/generic/hmap.h>

// Sharded by path hash: the load pool's workers all resolve at once
enum { SHARD_COUNT = 32 };

typedef struct Shard {
    pthread_mutex_t lock;
    HMapProbeBits probes;
} Shard;

struct CachedFs {
    Fs fs; // First: the Fs const* handed out is the CachedFs
    Fs const* inner;
    Shard shards[SHARD_COUNT];
};

static Shard* shard_of(CachedFs* me, csview path) {
    uint32_t hash = 2166136261u;
    for (isize i = 0; i < path.size; i++) hash = (hash ^ (uint8_t)path.buf[i]) * 16777619u;
    return &my shards[hash % SHARD_COUNT];
}

// What answering `known` (with yes or no) tells about the rest
static ProbeBits implied(ProbeBits bits) {
    if (bits & PROBE_FILE) bits |= PROBE_EXISTS_KNOWN | PROBE_EXISTS | PROBE_DIRECTORY_KNOWN;
    if (bits & PROBE_DIRECTORY) bits |= PROBE_EXISTS_KNOWN | PROBE_EXISTS | PROBE_FILE_KNOWN;
    if ((bits & PROBE_EXISTS_KNOWN) && !(bits & PROBE_EXISTS)) bits |= PROBE_FILE_KNOWN | PROBE_DIRECTORY_KNOWN;
    return bits;
}

static bool probe(Fs const* fs, csview path, ProbeBits known, ProbeBits yes, bool (*ask)(Fs const*, csview)) {
    CachedFs* me = (CachedFs*)fs;
    Shard* shard = shard_of(me, path);
    pthread_mutex_lock(&shard->lock);
    auto cached = hmap_probe_bits_get(&shard->probes, path);
    ProbeBits bits = cached ? cached->second : 0;
    pthread_mutex_unlock(&shard->lock);
    if (bits & known) return bits & yes;
    // (Asked outside the lock: two threads may both ask, and agree)
    bool answer = ask(my inner, path);
    pthread_mutex_lock(&shard->lock);
    auto entry = hmap_probe_bits_insert(&shard->probes, cstr_from_sv(path), 0);
    entry.ref->second = implied(entry.ref->second | known | (answer ? yes : 0));
    pthread_mutex_unlock(&shard->lock);
    return answer;
}

static bool ask_exists(Fs const* inner, csview path) { return fs_exists(inner, path); }
static bool ask_file(Fs const* inner, csview path) { return fs_is_reg_file(inner, path); }
static bool ask_directory(Fs const* inner, csview path) { return fs_is_directory(inner, path); }

static bool cached_exists(Fs const* me, csview path) {
    return probe(me, path, PROBE_EXISTS_KNOWN, PROBE_EXISTS, ask_exists);
}

static bool cached_is_reg_file(Fs const* me, csview path) {
    return probe(me, path, PROBE_FILE_KNOWN, PROBE_FILE, ask_file);
}

static bool cached_is_directory(Fs const* me, csview path) {
    return probe(me, path, PROBE_DIRECTORY_KNOWN, PROBE_DIRECTORY, ask_directory);
}

static void forget(CachedFs* me) {
    for (int i = 0; i < SHARD_COUNT; i++) {
        pthread_mutex_lock(&my shards[i].lock);
        hmap_probe_bits_clear(&my shards[i].probes);
        pthread_mutex_unlock(&my shards[i].lock);
    }
}

static FsListdirIter cached_start_listdir(Fs const* me, csview dir_path) {
    return fs_listdir_begin(((CachedFs const*)me)->inner, dir_path);
}

static FailableResult cached_get_cwd(Fs const* me, returns cstr) {
    return fs_get_cwd(((CachedFs const*)me)->inner, _OUTPUT_PARAM);
}

static FailableResult cached_change_cwd(Fs const* me, csview path) {
    forget((CachedFs*)me);
    return fs_change_cwd(((CachedFs const*)me)->inner, path);
}

static FailableResult cached_read_file(Fs const* me, csview path, returns cstr) {
    return fs_read_file(((CachedFs const*)me)->inner, path, _OUTPUT_PARAM);
}

static FailableResult cached_write_file(Fs const* me, csview path, csview contents) {
    forget((CachedFs*)me);
    return fs_write_file(((CachedFs const*)me)->inner, path, contents);
}

static FailableResult cached_remove(Fs const* me, csview path, bool recursive) {
    forget((CachedFs*)me);
    return fs_remove(((CachedFs const*)me)->inner, path, recursive);
}

static FailableResult cached_mkdir(Fs const* me, csview path) {
    forget((CachedFs*)me);
    return fs_mkdir(((CachedFs const*)me)->inner, path);
}

CachedFs* cached_fs_new(Fs const* inner) {
    CachedFs* ret = c_new(CachedFs, {
        .fs = {
            .exists = cached_exists,
            .is_reg_file = cached_is_reg_file,
            .is_directory = cached_is_directory,
            .start_listdir = cached_start_listdir,
            .get_cwd = cached_get_cwd,
            .change_cwd = cached_change_cwd,
            .read_file = cached_read_file,
            .write_file = cached_write_file,
            .remove = cached_remove,
            .mkdir = cached_mkdir,
        },
        .inner = inner,
    });
    for (int i = 0; i < SHARD_COUNT; i++) pthread_mutex_init(&ret->shards[i].lock, nullptr);
    return ret;
}

void cached_fs_destroy(CachedFs* me) {
    if (!me) return;
    for (int i = 0; i < SHARD_COUNT; i++) {
        hmap_probe_bits_drop(&my shards[i].probes);
        pthread_mutex_destroy(&my shards[i].lock);
    }
    free(me);
}

Fs const* cached_fs_fs(CachedFs const* me) {
    return &my fs;
}
