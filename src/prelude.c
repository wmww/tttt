#include "prelude.h"
#include "display.h"

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
#ifdef DEBUG_BUILD
    // gives us a stack trace in valgrind, there's probably a better way to do it
    fprintf(stderr, "%p\n", *(void**)nullptr);
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
}

void inhibit_nonfatals() {
    nonfatal_inhibit_count++;
}

void uninhibit_nonfatals() {
    nonfatal_inhibit_count--;
}
