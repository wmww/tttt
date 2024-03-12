#include "prelude.h"

void panic_internal(const char* error_type, const char* file, int line, const char* fmt, ...) {
    fprintf(stderr, CLR_PATH "%s:%d " CLR_ERR "%s: " CLR0, file, line, error_type);
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, CLR0 "\n");
    // gives us a stack trace in valgrind, there's probably a better way to do it
    fprintf(stderr, "%p\n", *(void**)nullptr);
    exit(1);
}
