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
    bool dump_flow;            // TTTT_DUMP_FLOW
    bool dump_variances;       // TTTT_DUMP_VARIANCES
    bool debug_relate;         // TTTT_DEBUG_RELATE
    bool debug_relate_nested;  // TTTT_DEBUG_RELATE_NESTED
} DebugEnv;
extern DebugEnv debug_env;

// These are used in debugging, DBG should not appear in committed code. Other debugging logic can be tagged with // DBG
#define PANIC_DBG panic("AAaaa")
#define PANIC_AFTER_DBG(count, ...) do { \
        static int panic_count = 0; \
        panic_count++; \
        if (panic_count > count) panic(__VA_ARGS__); \
        else log_error(__VA_ARGS__); \
    } while (false)
