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
    // TTTT_VARIANCE_CENSUS: one `VC` line per variance measurement on stderr (checker, subject, values, whether it was
    // clean by the taint counter, deep cuts, instructions-free cost proxies): scripts/variancecensus.py
    bool variance_census;
    bool debug_relate;         // TTTT_DEBUG_RELATE
    bool debug_relate_nested;  // TTTT_DEBUG_RELATE_NESTED
    bool debug_variance;       // TTTT_DEBUG_VARIANCE: the relations of variance measurements, like an instrumented tsgo
    bool parallel_resolve;     // TTTT_PARALLEL_RESOLVE: resolve names on --threads threads even for a few files (tests)
    bool file_times;           // TTTT_FILE_TIMES: each checked file's check time (with its deferred checks) on stderr
    bool dup_census;           // TTTT_DUP_CENSUS: cross-checker duplication by fingerprint after checking (ty/dupcensus.c)
    bool debug_containers;     // TTTT_DEBUG_CONTAINERS: getSymbolChain's sorted candidate containers and their specifiers
    bool resolve_times;        // TTTT_RESOLVE_TIMES: name resolution's steps (declare, link, the walk...) timed on stderr
    // TTTT_WALK_CHUNK_BYTES: every declaration file the chunked walk could take is walked in chunks of this many bytes
    // of statements (1: a statement each), whatever its size (program.c's chunked walk; its oracle)
    u32 walk_chunk_bytes;
    // TTTT_AST_SMALL_KB: a thread's first AST bytes on 4 KB pages (ast_alloc.c's populate()), its default when 0
    u32 ast_small_kb;
    bool ast_poison;           // TTTT_AST_POISON: a dropped file's AST pages fault on access instead of reading as zeroes
    bool ast_stats;            // TTTT_AST_STATS: the AST arenas' churn at exit (ast_alloc.h)
    bool ast_malloc;           // TTTT_AST_MALLOC: no AST arenas, the AST in malloc (for scripts/memtrack.sh)
    bool defer_stats;          // TTTT_DEFER_STATS: deferred function bodies' counts at exit (plan 78, source_file.h)
    bool order_stats;          // TTTT_ORDER_STATS: how often ty_compare_tie() fell back to type ids, by site and kind
    // TTTT_ORDER_SEED: a nonzero seed permutes ty_compare_tie()'s type-id fallbacks (is the id order observable?)
    u64 order_seed;
    // TTTT_NESTING_ORDER (sg2's id-order audit): what is_deeply_nested_as() takes for "newer": unset or "id" the type
    // ids (tsc's), "count" push order alone (every entry newer than the one below it: bun's id-free test without its
    // heights), "seed" the ids permuted by TTTT_ORDER_SEED, "content" bun's id-free test (relate.rs is_at_least_as_new:
    // the type's height, then push order on the stack; order.c ty_height())
    uint8_t nesting_order;
    // TTTT_ROLLBACK_STATS (sg2's rollback-rate audit): one `RB` line per checked file on stderr (program order, checker,
    // thread CPU ns, types made, cycle keys closed, limit hits, deep cuts), and the census' VC lines name the file
    bool rollback_stats;
    bool deep_hit_no_taint; // TTTT_DEEP_HIT_NO_TAINT: a relation cache hit concluded under a deep cut is no taint event
    bool vma_names;          // TTTT_VMA_NAMES: anonymous mappings named by kind in /proc/PID/smaps (vma_name(), scripts/rsstimeline.py)
} DebugEnv;
extern DebugEnv debug_env;

// The process's resident set, bytes (/proc/self/statm; 0 if unreadable). A read of an fd kept open: cheap enough per file
u64 process_resident_bytes(void);
// The memory the system could give the process now, bytes: /proc/meminfo's MemAvailable, and when it runs in cgroups
// (v2) with limits, the least that any of them still leaves (memory.max and memory.high minus memory.current, the
// cgroup's and each ancestor's), whichever is less; TTTT_MEM_AVAILABLE_MB overrides it (testing). 0: unknown
u64 memory_available_bytes(void);
// Under TTTT_VMA_NAMES, names an anonymous mapping `[anon:tttt-NAME]` in /proc/PID/smaps (PR_SET_VMA_ANON_NAME), for
// RSS attribution by kind (scripts/rsstimeline.py); a no-op otherwise
void vma_name(void* addr, size_t len, char const* name);

// calloc() for tables that are probed (read) before they are written: fresh pages calloc() gets from the kernel are
// mapped to the shared zero page on the first read, and the first write then copies it, which flushes the TLB of every
// thread in the process (inter-processor interrupts: +30% of the check phase's cycles with 4 checker threads).
// Writing the zeroes first faults the pages in directly. Panics when out of memory
static inline void* zeroed_table(size_t count, size_t size) {
    void* ret = malloc(count * size);
    panic_if(!ret, "out of memory");
    return memset(ret, 0, count * size);
}

// zeroed_table() for a checker's growing hash tables (intern, link and relation tables, which double), freed only
// with big_table_free(p, bytes). A table of 2 MB or more is pages of its own, and its free keeps them for the next table of its size (re-zeroed) rather than unmapping them: glibc's
// pages are faulted 4 KB at a time, and a munmap() is a TLB shootdown on every thread
void* big_table(size_t count, size_t size);
void big_table_free(void* table, size_t bytes);

// These are used in debugging, DBG should not appear in committed code. Other debugging logic can be tagged with // DBG
#define PANIC_DBG panic("AAaaa")
#define PANIC_AFTER_DBG(count, ...) do { \
        static int panic_count = 0; \
        panic_count++; \
        if (panic_count > count) panic(__VA_ARGS__); \
        else log_error(__VA_ARGS__); \
    } while (false)
