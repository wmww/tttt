#include "prelude.h"
#include "display.h"

#ifdef INCLUDE_VALGRIND
#include <valgrind/valgrind.h>
#endif

#ifdef DEBUG_ALLOCATOR
intptr_t debug_allocation_count = 0;
#endif // DEBUG_ALLOCATOR

int nonfatal_inhibit_count = 0;

static inline const char* remove_file_prefix(const char* file) {
    const char* prefix = "../src/";
    const char* trimmed = file;
    while (*prefix && *prefix == *trimmed) {
        prefix++;
        trimmed++;
    }
    if (*prefix) {
        return file;
    } else {
        return trimmed;
    }
}

void panic_internal(const char* error_type, const char* file, int line, const char* fmt, ...) {
    file = remove_file_prefix(file);
    fprintf(
        stderr,
        "%s%s:%d %s%s: %s",
        color_path, file, line,
        color_error, error_type,
        color_normal
    );
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
#ifdef INCLUDE_VALGRIND
    VALGRIND_PRINTF_BACKTRACE("");
#endif
    exit(1);
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
    file = remove_file_prefix(file);
    fprintf(
        stderr,
        "%s%s:%d %s%s: %s",
        color_path, file, line,
        color_error, error_type,
        color_normal
    );
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
    clear_debug_cstrs();
#ifdef INCLUDE_VALGRIND
    VALGRIND_PRINTF_BACKTRACE("");
#endif
}

void print_debug_internal(const char* file, int line, const char* fmt, ...) {
    file = remove_file_prefix(file);
    fprintf(
        stderr,
        "%s%s:%d %sdebug: %s",
        color_path, file, line,
        color_implicit, color_normal
    );
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
    clear_debug_cstrs();
}
