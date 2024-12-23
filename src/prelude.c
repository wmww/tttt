#include "prelude.h"
#include "display.h"

#ifdef INCLUDE_VALGRIND
#include <valgrind/valgrind.h>
#endif

#ifdef DEBUG_ALLOCATOR
intptr_t debug_allocation_count = 0;
#endif // DEBUG_ALLOCATOR

int nonfatal_inhibit_count = 0;

void panic_internal(const char* error_type, const char* file, int line, const char* fmt, ...) {
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

void nonfatal_internal(const char* error_type, const char* file, int line, const char* fmt, ...) {
    if (nonfatal_inhibit_count > 0) {
        return;
    }
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
}

void inhibit_nonfatals() {
    nonfatal_inhibit_count++;
}

void uninhibit_nonfatals() {
    nonfatal_inhibit_count--;
}
