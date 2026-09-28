#include "prelude.h"

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

DebugEnv debug_env;

__attribute__((constructor))
static void read_debug_env() {
    debug_env = (DebugEnv){
        .debug_cycle = getenv("TTTT_DEBUG_CYCLE") != nullptr,
        .dump_flow = getenv("TTTT_DUMP_FLOW") != nullptr,
        .dump_variances = getenv("TTTT_DUMP_VARIANCES") != nullptr,
        .debug_relate = getenv("TTTT_DEBUG_RELATE") != nullptr,
        .debug_relate_nested = getenv("TTTT_DEBUG_RELATE_NESTED") != nullptr,
    };
}
