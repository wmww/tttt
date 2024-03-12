#include "prelude.h"

int nonfatal_inhibit_count = 0;

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

void nonfatal_internal(const char* error_type, const char* file, int line, const char* fmt, ...) {
    if (nonfatal_inhibit_count > 0) {
        return;
    }
    fprintf(stderr, CLR_PATH "%s:%d " CLR_ERR "%s: " CLR0, file, line, error_type);
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, CLR0 "\n");
}

void inhibit_nonfatals() {
    nonfatal_inhibit_count++;
}

void uninhibit_nonfatals() {
    nonfatal_inhibit_count--;
}
