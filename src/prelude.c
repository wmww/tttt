// madvise(), which strict -std=c2x hides
#define _DEFAULT_SOURCE
#include "prelude.h"
#include <pthread.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef DEBUG_ALLOCATOR_ACTIVE
_Atomic intptr_t debug_allocation_count = 0;
#endif

// The macros this replaced all used to print file:line, and cleat's logger doesn't by default. Everything else about
// logger_config is left at cleat's defaults; set logger_config.threshold to LOG_LEVEL_NEVER to silence log_error()
// (which is what the baseline test runner does for quiet test cases) or to LOG_LEVEL_DEBUG to see everything.
__attribute__((constructor))
static void configure_logger() {
    logger_config.file_line = true;
}

DebugEnv debug_env;

__attribute__((constructor))
static void read_debug_env() {
    debug_env = (DebugEnv){
        .debug_cycle = getenv("TTTT_DEBUG_CYCLE") != nullptr,
        .debug_reentry = getenv("TTTT_DEBUG_REENTRY") != nullptr,
        .dump_flow = getenv("TTTT_DUMP_FLOW") != nullptr,
        .dump_variances = getenv("TTTT_DUMP_VARIANCES") != nullptr,
        .variance_census = getenv("TTTT_VARIANCE_CENSUS") != nullptr,
        .debug_relate = getenv("TTTT_DEBUG_RELATE") != nullptr,
        .debug_relate_nested = getenv("TTTT_DEBUG_RELATE_NESTED") != nullptr,
        .debug_variance = getenv("TTTT_DEBUG_VARIANCE") != nullptr,
        .parallel_resolve = getenv("TTTT_PARALLEL_RESOLVE") != nullptr,
        .file_times = getenv("TTTT_FILE_TIMES") != nullptr,
        .dup_census = getenv("TTTT_DUP_CENSUS") != nullptr,
        .debug_containers = getenv("TTTT_DEBUG_CONTAINERS") != nullptr,
        .resolve_times = getenv("TTTT_RESOLVE_TIMES") != nullptr,
        .walk_chunk_bytes = getenv("TTTT_WALK_CHUNK_BYTES") ? (u32)strtoul(getenv("TTTT_WALK_CHUNK_BYTES"), nullptr, 0) : 0,
        .ast_small_kb = getenv("TTTT_AST_SMALL_KB") ? (u32)strtoul(getenv("TTTT_AST_SMALL_KB"), nullptr, 0) : 0,
        .ast_poison = getenv("TTTT_AST_POISON") != nullptr,
        .ast_stats = getenv("TTTT_AST_STATS") != nullptr,
        .ast_malloc = getenv("TTTT_AST_MALLOC") != nullptr,
        .defer_stats = getenv("TTTT_DEFER_STATS") != nullptr,
        .order_stats = getenv("TTTT_ORDER_STATS") != nullptr,
        .order_seed = getenv("TTTT_ORDER_SEED") ? strtoull(getenv("TTTT_ORDER_SEED"), nullptr, 0) : 0,
        .nesting_order = !getenv("TTTT_NESTING_ORDER") ? NESTING_DEFAULT :
            !strcmp(getenv("TTTT_NESTING_ORDER"), "id") ? NESTING_ID :
            !strcmp(getenv("TTTT_NESTING_ORDER"), "count") ? NESTING_COUNT :
            !strcmp(getenv("TTTT_NESTING_ORDER"), "seed") ? NESTING_SEED :
            !strcmp(getenv("TTTT_NESTING_ORDER"), "content") ? NESTING_CONTENT :
            !strcmp(getenv("TTTT_NESTING_ORDER"), "walk") ? NESTING_WALK : NESTING_DEFAULT,
        .nesting_log = getenv("TTTT_NESTING_LOG") != nullptr,
        .rollback_stats = getenv("TTTT_ROLLBACK_STATS") != nullptr,
        .deep_hit_no_taint = getenv("TTTT_DEEP_HIT_NO_TAINT") != nullptr,
        .vma_names = getenv("TTTT_VMA_NAMES") != nullptr,
    };
}

u64 process_resident_bytes(void) {
    static _Atomic int statm = -1;
    int fd = atomic_load_explicit(&statm, memory_order_relaxed);
    if (fd < 0) {
        int opened = open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
        if (opened < 0) return 0;
        int expected = -1;
        if (atomic_compare_exchange_strong(&statm, &expected, opened)) fd = opened;
        else close(opened), fd = expected;
    }
    char text[128];
    ssize_t len = pread(fd, text, sizeof text - 1, 0);
    if (len <= 0) return 0;
    text[len] = 0;
    unsigned long long size = 0, resident = 0;
    if (sscanf(text, "%llu %llu", &size, &resident) != 2) return 0;
    return (u64)resident * (u64)sysconf(_SC_PAGESIZE);
}

// A cgroup file's number, UINT64_MAX for "max" or unreadable
static u64 cgroup_number(char const* dir, char const* name) {
    char path[4200];
    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) return UINT64_MAX;
    FILE* file = fopen(path, "r");
    if (!file) return UINT64_MAX;
    unsigned long long value = UINT64_MAX;
    if (fscanf(file, "%llu", &value) != 1) value = UINT64_MAX;
    fclose(file);
    return (u64)value;
}

u64 memory_available_bytes(void) {
    char const* forced = getenv("TTTT_MEM_AVAILABLE_MB");
    if (forced && *forced) return (u64)strtoull(forced, nullptr, 10) << 20;
    u64 available = 0;
    FILE* meminfo = fopen("/proc/meminfo", "r");
    if (meminfo) {
        char line[256];
        unsigned long long kb;
        while (fgets(line, sizeof line, meminfo)) {
            if (sscanf(line, "MemAvailable: %llu kB", &kb) == 1) {
                available = (u64)kb << 10;
                break;
            }
        }
        fclose(meminfo);
    }
    // (cgroup v2: one "0::/path" line; each ancestor's limit applies too)
    FILE* cgroup = fopen("/proc/self/cgroup", "r");
    if (cgroup) {
        char line[4096];
        while (fgets(line, sizeof line, cgroup)) {
            if (strncmp(line, "0::", 3)) continue;
            line[strcspn(line, "\n")] = 0;
            char dir[4096];
            if (snprintf(dir, sizeof dir, "/sys/fs/cgroup%.4000s", line + 3) >= (int)sizeof dir) break;
            for (;;) {
                u64 max = cgroup_number(dir, "memory.max"), high = cgroup_number(dir, "memory.high");
                u64 limit = max < high ? max : high;
                u64 current = limit == UINT64_MAX ? 0 : cgroup_number(dir, "memory.current");
                if (limit != UINT64_MAX && current != UINT64_MAX) {
                    u64 left = limit > current ? limit - current : 0;
                    if (!available || left < available) available = left;
                }
                char* slash = strrchr(dir, '/');
                if (!slash || slash - dir <= (long)strlen("/sys/fs/cgroup")) break;
                *slash = 0;
            }
            break;
        }
        fclose(cgroup);
    }
    return available;
}

void vma_name(void* addr, size_t len, char const* name) {
    if (!debug_env.vma_names) return;
    char full[64];
    snprintf(full, sizeof full, "tttt-%s", name);
    // (PR_SET_VMA, PR_SET_VMA_ANON_NAME: CONFIG_ANON_VMA_NAME kernels, 5.17 on; an older one just says no)
    prctl(0x53564d41, 0, (unsigned long)addr, len, (unsigned long)full);
}

// Big tables by size class (log2 of the pages' bytes, from 2 MB): what big_table_free() kept
enum { BIG_TABLE_MIN_SHIFT = 21 };
static pthread_mutex_t big_tables_lock = PTHREAD_MUTEX_INITIALIZER;
static void* big_tables_free[64];

static u32 big_table_class(size_t bytes) {
    u32 shift = BIG_TABLE_MIN_SHIFT;
    while (((size_t)1 << shift) < bytes) shift++;
    return shift;
}

void* big_table(size_t count, size_t size) {
    size_t bytes = count * size;
    if (bytes < ((size_t)1 << BIG_TABLE_MIN_SHIFT)) return zeroed_table(count, size);
    u32 shift = big_table_class(bytes);
    size_t mapped = (size_t)1 << shift;
    pthread_mutex_lock(&big_tables_lock);
    void* ret = big_tables_free[shift];
    if (ret) big_tables_free[shift] = *(void**)ret;
    pthread_mutex_unlock(&big_tables_lock);
    if (ret) return memset(ret, 0, bytes);
    char* start = mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    panic_if(start == MAP_FAILED, "out of memory");
    vma_name(start, mapped, "table");
    // (Written before read: no zero page to copy)
    madvise(start, bytes, MADV_POPULATE_WRITE);
    return start;
}

void big_table_free(void* table, size_t bytes) {
    if (!table) return;
    if (bytes < ((size_t)1 << BIG_TABLE_MIN_SHIFT)) {
        free(table);
        return;
    }
    u32 shift = big_table_class(bytes);
    pthread_mutex_lock(&big_tables_lock);
    *(void**)table = big_tables_free[shift];
    big_tables_free[shift] = table;
    pthread_mutex_unlock(&big_tables_lock);
}

enum { ZERO_PAGES_CHUNK = 32 << 20, ZERO_PAGES_OWN = 4 << 20, ZERO_PAGES_AHEAD = 1 << 20 };

void zero_pages_init(ZeroPages* me) {
    *me = (ZeroPages){};
    pthread_mutex_init(&my lock, nullptr);
}

static char* zero_pages_map(ZeroPages* me, size_t bytes) {
    // (Reserved, not committed: what isn't handed out is never touched)
    char* start = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    panic_if(start == MAP_FAILED, "out of memory");
    vma_name(start, bytes, "graph");
    ((void**)start)[0] = my mappings;
    ((size_t*)start)[1] = bytes;
    my mappings = start;
    return start + 64;
}

void* zero_pages_alloc(ZeroPages* me, size_t bytes) {
    bytes = (bytes + 63) & ~(size_t)63;
    char* ret;
    char* from = nullptr;
    char* to = nullptr;
    pthread_mutex_lock(&my lock);
    if (bytes > ZERO_PAGES_OWN) {
        ret = zero_pages_map(me, bytes + 64);
        from = (char*)((uintptr_t)ret & ~(uintptr_t)4095);
        to = ret + bytes;
    } else {
        if ((size_t)(my end - my at) < bytes) {
            my at = my populated = zero_pages_map(me, ZERO_PAGES_CHUNK);
            my end = my at - 64 + ZERO_PAGES_CHUNK;
        }
        ret = my at;
        my at += bytes;
        if (my at > my populated) {
            // (A megabyte ahead: a madvise() per memo page was 50k of them on vscode)
            from = (char*)((uintptr_t)my populated & ~(uintptr_t)4095);
            to = my at + ZERO_PAGES_AHEAD < my end ? my at + ZERO_PAGES_AHEAD : my end;
            my populated = to;
        }
    }
    pthread_mutex_unlock(&my lock);
    // (Outside the lock: another thread handed a page of this range before it is faulted in only reads the zero page)
    if (from) madvise(from, (size_t)(((uintptr_t)to + 4095) & ~(uintptr_t)4095) - (uintptr_t)from, MADV_POPULATE_WRITE);
    return ret;
}

void zero_pages_drop(ZeroPages* me) {
    for (void* m = my mappings; m;) {
        void* before = ((void**)m)[0];
        munmap(m, ((size_t*)m)[1]);
        m = before;
    }
    pthread_mutex_destroy(&my lock);
    *me = (ZeroPages){};
}
