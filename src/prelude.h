#pragma once

// The allocation counter (debug_allocator.h) is on in debug builds and in the test runners (meson.build passes
// -DDEBUG_ALLOCATOR to them, for their per-test leak check), off in the release tttt binary. Has to come before any
// STC/cleat header, see debug_allocator.h.
#if defined DEBUG_BUILD || defined DEBUG_ALLOCATOR
#include "debug_allocator.h"
#endif

#include <stdio.h>

// cleat.h pulls in cleat's types, string helpers, common macros, display, log, failable and generic/types headers,
// plus STC's common/csview/cstr. See thirdparty/cleat/readme.md
//
// Error reporting comes from cleat too: panic()/panic_if() and check() from common_macros.h, impossible from the same
// place for unreachable branches, and log_error()/log_warn()/log_debug() from log.h. All of them take a display
// sequence rather than a format string, and display.h adds tttt's own types (Span, Type, Expr, ...) to DISPLAYABLES
// so they can be passed straight in.
#include <cleat.h>
#include <stc/zsview.h>

#define MUST_USE __attribute__((warn_unused_result))

// The TTTT_* debugging environment variables (see CLAUDE.md), read once at startup: checkers on several threads read
// them, so nothing may cache them lazily
typedef struct DebugEnv {
    bool debug_cycle;          // TTTT_DEBUG_CYCLE
    // TTTT_DEBUG_REENTRY: what is asked for again while in flight below a resolution barrier, scripts/reentrytrace.py's
    // tttt side (`TTTT EVENT KIND BARRIER file:line` on stderr)
    bool debug_reentry;
    bool dump_flow;            // TTTT_DUMP_FLOW
    bool dump_variances;       // TTTT_DUMP_VARIANCES
    bool debug_relate;         // TTTT_DEBUG_RELATE
    bool debug_relate_nested;  // TTTT_DEBUG_RELATE_NESTED
    bool debug_variance;       // TTTT_DEBUG_VARIANCE: the relations of variance measurements, like an instrumented tsgo
    bool parallel_resolve;     // TTTT_PARALLEL_RESOLVE: resolve names on --threads threads even for a few files (tests)
    bool file_times;           // TTTT_FILE_TIMES: each checked file's check time (with its deferred checks) on stderr
    bool debug_containers;     // TTTT_DEBUG_CONTAINERS: getSymbolChain's sorted candidate containers and their specifiers
    bool resolve_times;        // TTTT_RESOLVE_TIMES: name resolution's steps (declare, link, the walk...) timed on stderr
    // TTTT_HUGEPAGES: 1 madvises every arena chunk for huge pages, 0 none; unset, as many as the system has free
    // (huge_pages_take())
    int8_t huge_pages;
    bool ast_poison;           // TTTT_AST_POISON: a dropped file's AST pages fault on access instead of reading as zeroes
    bool ast_stats;            // TTTT_AST_STATS: the AST arenas' churn at exit (ast_alloc.h)
    bool ast_malloc;           // TTTT_AST_MALLOC: no AST arenas, the AST in malloc (for scripts/memtrack.sh)
    bool defer_stats;          // TTTT_DEFER_STATS: deferred function bodies' counts at exit (plan 78, source_file.h)
    bool order_stats;          // TTTT_ORDER_STATS: how often ty_compare_tie() fell back to type ids, by site and kind
    // TTTT_ORDER_SEED: a nonzero seed permutes ty_compare_tie()'s type-id fallbacks (is the id order observable?)
    u64 order_seed;
} DebugEnv;
extern DebugEnv debug_env;

// Whether an arena chunk of `bytes` may be madvise(MADV_HUGEPAGE)d. Huge pages take ~5-10% off a mid-size project's
// wall time, but under the common THP setting (defrag=madvise) a madvised fault that finds no free 2 MB block compacts
// memory synchronously: on a machine with fragmented memory (a long uptime, a big page cache) madvising all of vscode's
// arenas took it from 5.9 to 9.4 s (14k compaction stalls a run). So chunks are madvised only while the free blocks of
// order >= 9 that /proc/buddyinfo listed at the first call cover them (TTTT_HUGEPAGES overrides)
bool huge_pages_take(size_t bytes);
// A madvised chunk of `bytes` that huge_pages_take() allowed is unmapped: its blocks are free again
void huge_pages_give(size_t bytes);

// calloc() for tables that are probed (read) before they are written: fresh pages calloc() gets from the kernel are
// mapped to the shared zero page on the first read, and the first write then copies it, which flushes the TLB of every
// thread in the process (inter-processor interrupts: +30% of the check phase's cycles with 4 checker threads).
// Writing the zeroes first faults the pages in directly. Panics when out of memory
static inline void* zeroed_table(size_t count, size_t size) {
    void* ret = malloc(count * size);
    panic_if(!ret, "out of memory");
    return memset(ret, 0, count * size);
}

// These are used in debugging, DBG should not appear in committed code. Other debugging logic can be tagged with // DBG
#define PANIC_DBG panic("AAaaa")
#define PANIC_AFTER_DBG(count, ...) do { \
        static int panic_count = 0; \
        panic_count++; \
        if (panic_count > count) panic(__VA_ARGS__); \
        else log_error(__VA_ARGS__); \
    } while (false)
