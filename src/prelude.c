#include "prelude.h"

#include <sys/resource.h>

#ifdef DEBUG_ALLOCATOR_ACTIVE
intptr_t debug_allocation_count = 0;
#endif

// The macros this replaced all used to print file:line, and cleat's logger doesn't by default. Everything else about
// logger_config is left at cleat's defaults; set logger_config.threshold to LOG_LEVEL_NEVER to silence log_error()
// (which is what the baseline test runner does for quiet test cases) or to LOG_LEVEL_DEBUG to see everything.
__attribute__((constructor))
static void configure_logger() {
    logger_config.file_line = true;
}

// The parser and checker recurse once per nesting level, and long binary chains (binderBinaryExpressionStress) need
// more than the usual 8 MB default with some compilers' frame sizes (GCC 13 overflows it). Linux grows
// the main thread's stack up to the soft limit at fault time, so raising it here is enough. Recursion depth is
// still unbounded: see agents/hacks.md
__attribute__((constructor))
static void raise_stack_limit() {
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) != 0) return;
    rlim_t wanted = (rlim_t)64 << 20;
    if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max < wanted) wanted = limit.rlim_max;
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur >= wanted) return;
    limit.rlim_cur = wanted;
    setrlimit(RLIMIT_STACK, &limit);
}
