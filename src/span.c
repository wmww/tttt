#include "span.h"
#include "math_helpers.h"

unsigned span_line_index(Span me) {
    if (!me.file_start) {
        return 1;
    }
    const char* end = span_start_ptr(me);
    const char* pos = me.file_start;
    unsigned line = 0;
    while (pos < end) {
        if (*pos == '\n') {
            line++;
        }
        pos++;
    }
    return line;
}

unsigned span_col_index(Span me) {
    return span_start_ptr(me) - span_line_start(me);
}

const char* span_line_start(Span me) {
    if (!me.file_start) {
        return nullptr;
    }
    const char* pos = span_start_ptr(me);
    while (pos > me.file_start && *pos != '\n') {
        pos--;
    }
    if (*pos == '\n') {
        pos++;
    }
    return pos;
}

const char* span_line_end(Span me) {
    if (!me.file_start) {
        return nullptr;
    }
    const char* pos = span_start_ptr(me);
    while (*pos && *pos != '\n') {
        pos++;
    }
    return pos;
}

Span span_between(Span const a, Span const b) {
    if (!span_valid(a)) {
        return b;
    } else if (!span_valid(b)) {
        return a;
    }
    if (a.file_start != b.file_start) {
        PANIC("Tried to make a span between two different files");
    }
    return span_make(
        a.file_start,
        ccpmin(span_start_ptr(a), span_start_ptr(b)),
        ccpmax(  span_end_ptr(a),   span_end_ptr(b))
    );
}

int span_cmp(Span a, Span b) {
    if (a.file_start != b.file_start) {
        // TODO: compare filenames once available
        return a.file_start > b.file_start ? 1 : -1;
    } else if (a.sv.buf != b.sv.buf) {
        return a.sv.buf > b.sv.buf ? 1 : -1;
    } else if (a.sv.size != b.sv.size) {
        return a.sv.size > b.sv.size ? 1 : -1;
    } else {
        return 0;
    }
}

void cstr_append_span(cstr* target, Span me) {
    cstr_append_sv(target, me.sv);
}
