#include "prelude.h"

#ifdef DEBUG_ALLOCATOR_ACTIVE
intptr_t debug_allocation_count = 0;
#endif

int nonfatal_inhibit_count = 0;

// The macros below all used to print file:line, and cleat's logger doesn't by default
__attribute__((constructor))
static void configure_logger() {
    logger_config.file_line = true;
}

// Formats fmt/va into a fresh DisplayCtx. cleat's logger takes ownership of the context and drops it
static DisplayCtx vformat_display(const char* fmt, va_list va) {
    DisplayCtx d = logger_config.display_ctx;
    cstr_vfmt(&d.target, 0, fmt, va);
    return d;
}

// Calls the logger directly rather than going through log_debug()/log_error(), which deliberately bypasses
// logger_config.threshold: NONFATAL() and PRINT_DBG() have always printed unconditionally, and raising the threshold
// to suit them would also turn on cleat's own debug logging (run_subproc() announcing every command, say)
static void log_at_level(LogLevel level, const char* label, const char* file, int line, const char* fmt, va_list va) {
    logger_config.func(&logger_config, csview_from(file), line, level, csview_from(label), vformat_display(fmt, va));
}

// Same thing cleat's panic() does, but with printf-style formatting. Never returns
void panic_internal(const char* error_type, const char* file, int line, const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    DisplayCtx d = vformat_display(fmt, va);
    va_end(va);
    default_log_fatal_abort(csview_from(file), line, csview_from(error_type), d);
}

#define DEBUG_CSTRS_CAP 20
static cstr debug_cstrs[DEBUG_CSTRS_CAP];
static int debug_cstrs_len = 0;
bool pending_debug_cstr = false;
cstr* acquire_debug_cstr() {
    debug_cstrs_len++;
    PANIC_IF(
        pending_debug_cstr,
        "FMT_DBG()/get_debug_cstr() called without matching get_latest_debug_str(), "
        "perhaps FMT_DBG() is being used within the call stack of a format argument?"
    );
    PANIC_IF(
        debug_cstrs_len > DEBUG_CSTRS_CAP,
        "FMT_DBG()/get_debug_cstr() used too many times in a row, "
        "it should only be used in the format args of PRINT_DBG/NONFATAL(_DBG)/PANIC"
    );
    pending_debug_cstr = true;
    debug_cstrs[debug_cstrs_len - 1] = cstr_null;
    return &debug_cstrs[debug_cstrs_len - 1];
}

const char* get_latest_debug_str() {
    PANIC_IF(!pending_debug_cstr, "get_latest_debug_str() called without matching acquire_debug_cstr()");
    pending_debug_cstr = false;
    return cstr_str(&debug_cstrs[debug_cstrs_len - 1]);
}

static inline void clear_debug_cstrs() {
    for (int i = 0; i < debug_cstrs_len; i++) {
        cstr_drop(&debug_cstrs[i]);
    }
    debug_cstrs_len = 0;
}

void nonfatal_internal(const char* error_type, const char* file, int line, const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    log_at_level(LOG_LEVEL_ERROR, error_type, file, line, fmt, va);
    va_end(va);
    clear_debug_cstrs();
}

void print_debug_internal(const char* file, int line, const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    log_at_level(LOG_LEVEL_DEBUG, "debug", file, line, fmt, va);
    va_end(va);
    clear_debug_cstrs();
}
