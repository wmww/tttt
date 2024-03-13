#include "span.h"
#include "math_helpers.h"

const Span span_null = {0};

int span_line_index(Span const* self) {
    if (!self->file_start) {
        return 1;
    }
    const char* end = span_start_ptr(self);
    const char* pos = self->file_start;
    int line = 0;
    while (pos < end) {
        if (*pos == '\n') {
            line++;
        }
        pos++;
    }
    return line;
}

int span_col_index(Span const* self) {
    return span_start_ptr(self) - span_line_start(self);
}

const char* span_line_start(Span const* self) {
    if (!self->file_start) {
        return nullptr;
    }
    const char* pos = span_start_ptr(self);
    while (pos > self->file_start && *pos != '\n') {
        pos--;
    }
    if (*pos == '\n') {
        pos++;
    }
    return pos;
}

const char* span_line_end(Span const* self) {
    if (!self->file_start) {
        return nullptr;
    }
    const char* pos = span_start_ptr(self);
    while (*pos && *pos != '\n') {
        pos++;
    }
    return pos;
}

csview span_sv(Span const* self) {
    if (!self->file_start) {
        return c_sv("");
    } else {
        return csview_from_n(self->file_start + self->offset, self->length);
    }
}

Span span_between(Span const* a, Span const* b) {
    if (a->file_start != b->file_start) {
        PANIC("Tried to make a span between two different files");
    }
    unsigned start = umin(a->offset, b->offset);
    unsigned end = umax(a->offset + a->length, b->offset + b->length);
    return span_make(a->file_start, start, end - start);
}
