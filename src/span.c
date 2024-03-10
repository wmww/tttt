#include "span.h"

int span_line_number(Span const* span) {
    if (!span->file_start) {
        return 1;
    }
    const char* end = span_start_ptr(span);
    const char* pos = span->file_start;
    int count = 1;
    while (pos < end) {
        if (*pos == '\n') {
            count++;
        }
        pos++;
    }
    return count;
}

const char* span_line_start(Span const* span) {
    if (!span->file_start) {
        return nullptr;
    }
    const char* pos = span_start_ptr(span);
    while (pos > span->file_start && *pos != '\n') {
        pos--;
    }
    if (*pos == '\n') {
        pos++;
    }
    return pos;
}

const char* span_line_end(Span const* span) {
    if (!span->file_start) {
        return nullptr;
    }
    const char* pos = span_start_ptr(span);
    while (*pos && *pos != '\n') {
        pos++;
    }
    return pos;
}

csview span_sv(Span const* span) {
    if (!span->file_start) {
        return c_sv("");
    } else {
        return csview_from_n(span->file_start + span->offset, span->length);
    }
}
