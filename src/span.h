#pragma once

typedef struct Span {
    const char* file_start;
    unsigned offset;
    unsigned length;
} Span;
