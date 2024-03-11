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

static inline const char* span_start_ptr(Span const* self) {
    return self->file_start ? self->file_start + self->offset : nullptr;
}

static inline const char* span_end_ptr(Span const* self) {
    return self->file_start ? self->file_start + self->offset + self->length : nullptr;
}

int span_line_index(Span const* self);
int span_col_index(Span const* self);
const char* span_line_start(Span const* self);
const char* span_line_end(Span const* self);
csview span_sv(Span const* self);
