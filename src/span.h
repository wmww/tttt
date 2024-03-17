#pragma once

#include "prelude.h"

typedef struct Span {
    const char* file_start;
    unsigned offset;
    unsigned length;
} Span;

static inline Span span_make(const char* file_start, unsigned offset, unsigned length) {
    return (Span){
        .file_start = file_start,
        .offset = offset,
        .length = length,
    };
}

static inline const char* span_start_ptr(Span const* me) {
    return my file_start ? my file_start + my offset : nullptr;
}

static inline const char* span_end_ptr(Span const* me) {
    return my file_start ? my file_start + my offset + my length : nullptr;
}

int span_line_index(Span const* me);
int span_col_index(Span const* me);
const char* span_line_start(Span const* me);
const char* span_line_end(Span const* me);
csview span_sv(Span const* me);
Span span_between(Span const* a, Span const* b);

extern const Span span_null;
