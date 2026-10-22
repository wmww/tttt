#include "prelude.h"
#include <pthread.h>
#include <stdatomic.h>

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
        .debug_relate = getenv("TTTT_DEBUG_RELATE") != nullptr,
        .debug_relate_nested = getenv("TTTT_DEBUG_RELATE_NESTED") != nullptr,
        .debug_variance = getenv("TTTT_DEBUG_VARIANCE") != nullptr,
        .parallel_resolve = getenv("TTTT_PARALLEL_RESOLVE") != nullptr,
        .file_times = getenv("TTTT_FILE_TIMES") != nullptr,
        .dup_census = getenv("TTTT_DUP_CENSUS") != nullptr,
        .debug_containers = getenv("TTTT_DEBUG_CONTAINERS") != nullptr,
        .resolve_times = getenv("TTTT_RESOLVE_TIMES") != nullptr,
        .huge_pages = getenv("TTTT_HUGEPAGES") ? (int8_t)(strcmp(getenv("TTTT_HUGEPAGES"), "0") != 0) : -1,
        .ast_poison = getenv("TTTT_AST_POISON") != nullptr,
        .ast_stats = getenv("TTTT_AST_STATS") != nullptr,
        .ast_malloc = getenv("TTTT_AST_MALLOC") != nullptr,
        .defer_stats = getenv("TTTT_DEFER_STATS") != nullptr,
        .order_stats = getenv("TTTT_ORDER_STATS") != nullptr,
        .order_seed = getenv("TTTT_ORDER_SEED") ? strtoull(getenv("TTTT_ORDER_SEED"), nullptr, 0) : 0,
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
