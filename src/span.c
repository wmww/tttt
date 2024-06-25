#include "span.h"
#include "math_helpers.h"

int span_line_index(Span const* me) {
    if (!my file_start) {
        return 1;
    }
    const char* end = span_start_ptr(me);
    const char* pos = my file_start;
    int line = 0;
    while (pos < end) {
        if (*pos == '\n') {
            line++;
        }
        pos++;
    }
    return line;
}

int span_col_index(Span const* me) {
    return span_start_ptr(me) - span_line_start(me);
}

const char* span_line_start(Span const* me) {
    if (!my file_start) {
        return nullptr;
    }
    const char* pos = span_start_ptr(me);
    while (pos > my file_start && *pos != '\n') {
        pos--;
    }
    if (*pos == '\n') {
        pos++;
    }
    return pos;
}

const char* span_line_end(Span const* me) {
    if (!my file_start) {
        return nullptr;
    }
    const char* pos = span_start_ptr(me);
    while (*pos && *pos != '\n') {
        pos++;
    }
    return pos;
}

Span span_between(Span const* a, Span const* b) {
    if (!span_valid(a)) {
        return *b;
    } else if (!span_valid(b)) {
        return *a;
    }
    if (a->file_start != b->file_start) {
        PANIC("Tried to make a span between two different files");
    }
    unsigned start = umin(a->offset, b->offset);
    unsigned end = umax(a->offset + a->length, b->offset + b->length);
    return span_make(a->file_start, start, end - start);
}

int span_cmp(Span const* a, Span const* b) {
    if (a->file_start != b->file_start) {
        // TODO: compare filenames once available
        return a->file_start > b->file_start ? 1 : -1;
    } else if (a->offset != b->offset) {
        return a->offset > b->offset ? 1 : -1;
    } else if (a->length != b->length) {
        return a->length > b->length ? 1 : -1;
    } else {
        return 0;
    }
}

bool span_eq_str(Span const* me, const char* str) {
    const char* s = span_start_ptr(me);
    if (!s) {
        return false;
    }
    for (intptr_t i = 0; i < my length; i++) {
        if (s[i] != str[i] || !str[i]) {
            return false;
        }
    }
    return str[my length] == 0;
}

void cstr_append_span(cstr* target, Span const* me) {
    cstr_append_sv(target, span_sv(me));
}
