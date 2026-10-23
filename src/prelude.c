// madvise(), which strict -std=c2x hides
#define _DEFAULT_SOURCE
#include "prelude.h"
#include <pthread.h>
#include <stdatomic.h>
#include <sys/mman.h>

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
        .huge_pages = getenv("TTTT_HUGEPAGES") ? (int8_t)(strcmp(getenv("TTTT_HUGEPAGES"), "0") != 0) : -1,
        .ast_small_kb = getenv("TTTT_AST_SMALL_KB") ? (u32)strtoul(getenv("TTTT_AST_SMALL_KB"), nullptr, 0) : 0,
        .ast_poison = getenv("TTTT_AST_POISON") != nullptr,
        .ast_stats = getenv("TTTT_AST_STATS") != nullptr,
        .ast_malloc = getenv("TTTT_AST_MALLOC") != nullptr,
        .defer_stats = getenv("TTTT_DEFER_STATS") != nullptr,
        .order_stats = getenv("TTTT_ORDER_STATS") != nullptr,
        .order_seed = getenv("TTTT_ORDER_SEED") ? strtoull(getenv("TTTT_ORDER_SEED"), nullptr, 0) : 0,
        .nesting_order = !getenv("TTTT_NESTING_ORDER") ? 0 : !strcmp(getenv("TTTT_NESTING_ORDER"), "count") ? 1 :
            !strcmp(getenv("TTTT_NESTING_ORDER"), "seed") ? 2 :
            !strcmp(getenv("TTTT_NESTING_ORDER"), "content") ? 3 : 0,
        .rollback_stats = getenv("TTTT_ROLLBACK_STATS") != nullptr,
        .deep_hit_no_taint = getenv("TTTT_DEEP_HIT_NO_TAINT") != nullptr,
    };
}

static atomic_llong huge_page_budget;
static pthread_once_t huge_page_budget_once = PTHREAD_ONCE_INIT;

// The free memory in blocks of 2 MB and more
static void read_huge_page_budget(void) {
    FILE* file = fopen("/proc/buddyinfo", "r");
    if (!file) return;
    long long bytes = 0;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        // "Node 0, zone   Normal 104016 78420 ..." : the count of free blocks of each order, from 4 KB up
        char const* counts = strstr(line, "zone");
        if (!counts) continue;
        counts += 4;
        while (*counts == ' ') counts++;
        while (*counts && *counts != ' ') counts++;
        for (int order = 0; order <= 20; order++) {
            char* end;
            long long count = strtoll(counts, &end, 10);
            if (end == counts) break;
            counts = end;
            if (order >= 9) bytes += count << (order + 12);
        }
    }
    fclose(file);
    atomic_store(&huge_page_budget, bytes);
}

bool huge_pages_take(size_t bytes) {
    if (debug_env.huge_pages >= 0) return debug_env.huge_pages;
    pthread_once(&huge_page_budget_once, read_huge_page_budget);
    if (atomic_fetch_sub(&huge_page_budget, (long long)bytes) >= (long long)bytes) return true;
    atomic_fetch_add(&huge_page_budget, (long long)bytes);
    return false;
}

void huge_pages_give(size_t bytes) {
    if (debug_env.huge_pages < 0) atomic_fetch_add(&huge_page_budget, (long long)bytes);
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
    // (2 MB aligned: map 2 MB more and cut the ends off)
    size_t align = (size_t)1 << BIG_TABLE_MIN_SHIFT;
    char* raw = mmap(nullptr, mapped + align, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    panic_if(raw == MAP_FAILED, "out of memory");
    char* start = (char*)(((uintptr_t)raw + align - 1) & ~(uintptr_t)(align - 1));
    if (start > raw) munmap(raw, (size_t)(start - raw));
    if (start + mapped < raw + mapped + align) munmap(start + mapped, (size_t)(raw + mapped + align - (start + mapped)));
    if (huge_pages_take(mapped)) madvise(start, mapped, MADV_HUGEPAGE);
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
