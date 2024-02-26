#include "prelude.h"

void panic(const char* file, int line, const char* fmt, ...) {
    fprintf(stderr, "\x1b[31m%s:%d panic:\x1b[0m ", file, line);
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
    exit(1);
}
