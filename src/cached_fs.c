// readdir()'s d_type and PATH_MAX, which strict -std=c2x hides
#define _DEFAULT_SOURCE
#include "cached_fs.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdalign.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>

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

// A directory's entries read once (on the real filesystem only: d_type), which answer the probes of the paths in it
// without a stat each: module resolution tries a handful of extensions per import, nearly all missing (nest: 14k
// stats of 12.6k paths in 1.4k directories)
enum {
    ENTRY_FILE = 1,
    ENTRY_DIRECTORY,
    ENTRY_OTHER, // (A fifo, a socket, a device: exists, neither a file nor a directory)
    ENTRY_ASK, // A symlink or an unknown d_type: stat() it
};

typedef struct DirEntry {
    u32 name; // Offset in names
    uint16_t size;
    uint8_t kind;
} DirEntry;

typedef struct DirListing {
    bool usable; // Read (or known missing); else every probe in it stats (an unreadable but searchable directory...)
    u32 count, mask;
    DirEntry* entries;
    u32* slots; // Open addressing: index + 1 into entries, 0 empty
    char* names;
} DirListing;

// A directory's listing, made at the first probe of a path in it the path cache can't answer
typedef struct DirState {
    DirListing* listing;
} DirState;

static void dir_state_drop(DirState* me) {
    if (my listing) {
        free(my listing->entries);
        free(my listing->slots);
        free(my listing->names);
        free(my listing);
    }
}
#define g_pro_key SvStr, svstr
#define g_class_val DirState, dir_state
#define g_no_clone
#include <cleat/generic/hmap.h>

// Sharded by path hash: the load pool's workers all resolve at once
enum { SHARD_COUNT = 32 };

typedef struct Shard {
    pthread_mutex_t lock;
    HMapProbeBits probes;
    HMapDirState dirs; // By normalized absolute path
} Shard;

struct CachedFs {
    Fs fs; // First: the Fs const* handed out is the CachedFs
    Fs const* inner;
    bool listings; // inner is the real filesystem: directories can be listed with their entries' types
    pthread_mutex_t cwd_lock;
    cstr cwd; // Once known (relative paths are listed by absolute directory), forgotten with the rest
    _Atomic bool cwd_known;
    // Listings forget() dropped, freed with the cache: a probe on another thread may be reading one
    DirListing** retired;
    isize retired_count, retired_cap;
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

static u32 name_hash(char const* name, isize size) {
    uint32_t hash = 2166136261u;
    for (isize i = 0; i < size; i++) hash = (hash ^ (uint8_t)name[i]) * 16777619u;
    return hash;
}

// The directory's entries, or an unusable listing if it can't be read (a missing directory is a usable empty one)
static DirListing* list_directory(char const* dir) {
    DirListing* ret = c_new(DirListing, {0});
    // (getdents64() itself: opendir() fstat()s too)
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        ret->usable = errno == ENOENT || errno == ENOTDIR;
        return ret;
    }
    isize names_size = 0, names_cap = 1024, cap = 16;
    ret->names = malloc((size_t)names_cap);
    ret->entries = malloc(sizeof(DirEntry) * (size_t)cap);
    panic_if(!ret->names || !ret->entries, "out of memory");
    bool failed = false;
    alignas(8) char buf[32768];
    for (;;) {
        long got = syscall(SYS_getdents64, fd, buf, sizeof(buf));
        if (got <= 0) {
            failed = got < 0;
            break;
        }
        for (long at = 0; at < got;) {
            struct linux_dirent64 { uint64_t ino; int64_t off; unsigned short reclen; unsigned char type; char name[]; };
            struct linux_dirent64 const* e = (void const*)(buf + at);
            at += e->reclen;
            isize size = (isize)strlen(e->name);
            if ((size == 1 && e->name[0] == '.') || (size == 2 && e->name[0] == '.' && e->name[1] == '.')) continue;
            if (size > UINT16_MAX) continue; // (Not a name any probe can ask for)
            uint8_t kind = e->type == DT_REG ? ENTRY_FILE
                : e->type == DT_DIR ? ENTRY_DIRECTORY
                : e->type == DT_LNK || e->type == DT_UNKNOWN ? ENTRY_ASK
                : ENTRY_OTHER;
            if (ret->count == cap) {
                cap *= 2;
                ret->entries = realloc(ret->entries, sizeof(DirEntry) * (size_t)cap);
                panic_if(!ret->entries, "out of memory");
            }
            while (names_size + size > names_cap) {
                names_cap *= 2;
                ret->names = realloc(ret->names, (size_t)names_cap);
                panic_if(!ret->names, "out of memory");
            }
            memcpy(ret->names + names_size, e->name, (size_t)size);
            ret->entries[ret->count++] = (DirEntry){ .name = (u32)names_size, .size = (uint16_t)size, .kind = kind };
            names_size += size;
        }
    }
    close(fd);
    u32 slots = 16;
    while (slots < ret->count * 2) slots *= 2;
    ret->mask = slots - 1;
    ret->slots = calloc(slots, sizeof(u32));
    panic_if(!ret->slots, "out of memory");
    for (u32 i = 0; i < ret->count; i++) {
        u32 at = name_hash(ret->names + ret->entries[i].name, ret->entries[i].size) & ret->mask;
        while (ret->slots[at]) at = (at + 1) & ret->mask;
        ret->slots[at] = i + 1;
    }
    ret->usable = !failed;
    return ret;
}

// 0 if name isn't in the directory
static uint8_t listing_kind(DirListing const* me, csview name) {
    if (!my count) return 0;
    for (u32 at = name_hash(name.buf, name.size) & my mask; my slots[at]; at = (at + 1) & my mask) {
        DirEntry const* e = &my entries[my slots[at] - 1];
        if (e->size == name.size && !memcmp(my names + e->name, name.buf, (size_t)name.size)) return e->kind;
    }
    return 0;
}

static void known_cwd(CachedFs* me) {
    if (atomic_load_explicit(&my cwd_known, memory_order_acquire)) return;
    pthread_mutex_lock(&my cwd_lock);
    if (!atomic_load_explicit(&my cwd_known, memory_order_relaxed)) {
        cstr cwd = cstr_null;
        try (fs_get_cwd(my inner, &cwd)) if_err (e) {
            ignore e;
            current_error_destroy();
            cwd = cstr_null;
        }
        my cwd = cwd;
        atomic_store_explicit(&my cwd_known, true, memory_order_release);
    }
    pthread_mutex_unlock(&my cwd_lock);
}

// path's directory as a normalized absolute path in dir (a C string) and its last component, or false if path isn't
// one a listing can answer for: empty or `.` components are skipped, `..` only leads (the kernel resolves those
// against the physical working directory getcwd() names; after a symlink a lexical `..` would go elsewhere)
static bool split_for_listing(CachedFs* me, csview path, char* dir, isize* dir_size, csview* base) {
    if (!path.size || path.buf[path.size - 1] == '/') return false;
    isize slash = path.size;
    while (slash > 0 && path.buf[slash - 1] != '/') slash--;
    *base = csview_slice(path, slash, path.size);
    if (csview_equals(*base, ".") || csview_equals(*base, "..")) return false;
    isize size = 0;
    if (path.buf[0] != '/') {
        known_cwd(me);
        if (!cstr_size(&my cwd) || cstr_str(&my cwd)[0] != '/') return false;
        size = cstr_size(&my cwd);
        if (size >= PATH_MAX) return false;
        memcpy(dir, cstr_str(&my cwd), (size_t)size);
        if (size == 1) size = 0; // ("/": components append "/name")
    }
    bool leading = path.buf[0] != '/';
    for (isize i = 0; i < slash;) {
        isize end = i;
        while (end < slash && path.buf[end] != '/') end++;
        isize n = end - i;
        if (n == 2 && path.buf[i] == '.' && path.buf[i + 1] == '.') {
            if (!leading) return false;
            while (size > 0 && dir[size - 1] != '/') size--;
            if (size > 0) size--;
        } else if (n && !(n == 1 && path.buf[i] == '.')) {
            leading = false;
            if (size + 1 + n >= PATH_MAX) return false;
            dir[size++] = '/';
            memcpy(dir + size, path.buf + i, (size_t)n);
            size += n;
        }
        i = end + 1;
    }
    if (!size) dir[size++] = '/';
    dir[size] = 0;
    *dir_size = size;
    return true;
}

// What the path's directory listing says about it (the bits probe() caches), or 0 if it doesn't: an entry to stat, a
// directory that can't be read, a path that isn't one a listing answers for
static ProbeBits probe_listing(CachedFs* me, csview path) {
    char dir[PATH_MAX];
    isize dir_size = 0;
    csview base;
    if (!my listings || !split_for_listing(me, path, dir, &dir_size, &base)) return 0;
    csview key = csview_from_n(dir, dir_size);
    Shard* shard = shard_of(me, key);
    pthread_mutex_lock(&shard->lock);
    auto state = hmap_dir_state_get_mut(&shard->dirs, key);
    DirListing const* listing = state ? state->second.listing : nullptr;
    pthread_mutex_unlock(&shard->lock);
    if (!listing) {
        // (Listed outside the lock: two threads may both list it, and agree. Listing at the first probe beat waiting
        // for a few: nest 23.3k probing syscalls without listings, 17.8k listing at the third, 15.6k at the first;
        // vscode, whose include glob lists its sources already, 78k, 78k and 74k)
        DirListing* made = list_directory(dir);
        pthread_mutex_lock(&shard->lock);
        // (If another thread's got there first, insert drops ours)
        listing = hmap_dir_state_insert(&shard->dirs, cstr_from_sv(key), (DirState){ .listing = made }).ref->second.listing;
        pthread_mutex_unlock(&shard->lock);
    }
    if (!listing || !listing->usable) return 0;
    switch (listing_kind(listing, base)) {
        case 0: return implied(PROBE_EXISTS_KNOWN);
        case ENTRY_FILE: return implied(PROBE_FILE_KNOWN | PROBE_FILE);
        case ENTRY_DIRECTORY: return implied(PROBE_DIRECTORY_KNOWN | PROBE_DIRECTORY);
        case ENTRY_OTHER: return PROBE_EXISTS_KNOWN | PROBE_EXISTS | PROBE_FILE_KNOWN | PROBE_DIRECTORY_KNOWN;
        default: return 0;
    }
}

static bool probe(Fs const* fs, csview path, ProbeBits known, ProbeBits yes, bool (*ask)(Fs const*, csview)) {
    CachedFs* me = (CachedFs*)fs;
    Shard* shard = shard_of(me, path);
    pthread_mutex_lock(&shard->lock);
    auto cached = hmap_probe_bits_get(&shard->probes, path);
    ProbeBits bits = cached ? cached->second : 0;
    pthread_mutex_unlock(&shard->lock);
    if (bits & known) return bits & yes;
    // (Not cached by path: a listing's answers aren't, they're one lookup away already)
    ProbeBits listed = probe_listing(me, path);
    if (listed & known) return listed & yes;
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
        for (c_each(entry, hmap_dir_state, my shards[i].dirs)) {
            if (!entry.ref->second.listing) continue;
            if (my retired_count == my retired_cap) {
                my retired_cap = my retired_cap ? my retired_cap * 2 : 64;
                my retired = realloc(my retired, sizeof(DirListing*) * (size_t)my retired_cap);
                panic_if(!my retired, "out of memory");
            }
            my retired[my retired_count++] = entry.ref->second.listing;
            entry.ref->second.listing = nullptr;
        }
        hmap_dir_state_clear(&my shards[i].dirs);
        pthread_mutex_unlock(&my shards[i].lock);
    }
    pthread_mutex_lock(&my cwd_lock);
    cstr_drop(&my cwd);
    atomic_store(&my cwd_known, false);
    pthread_mutex_unlock(&my cwd_lock);
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
        .listings = inner == fs && !(getenv("TTTT_NO_LISTINGS") && strcmp(getenv("TTTT_NO_LISTINGS"), "0")),
    });
    pthread_mutex_init(&ret->cwd_lock, nullptr);
    for (int i = 0; i < SHARD_COUNT; i++) pthread_mutex_init(&ret->shards[i].lock, nullptr);
    return ret;
}

void cached_fs_destroy(CachedFs* me) {
    if (!me) return;
    for (int i = 0; i < SHARD_COUNT; i++) {
        hmap_probe_bits_drop(&my shards[i].probes);
        hmap_dir_state_drop(&my shards[i].dirs);
        pthread_mutex_destroy(&my shards[i].lock);
    }
    for (isize i = 0; i < my retired_count; i++) dir_state_drop(&(DirState){ .listing = my retired[i] });
    free(my retired);
    pthread_mutex_destroy(&my cwd_lock);
    cstr_drop(&my cwd);
    free(me);
}

Fs const* cached_fs_fs(CachedFs const* me) {
    return &my fs;
}

Fs const* cached_fs_inner(CachedFs const* me) {
    return my inner;
}
