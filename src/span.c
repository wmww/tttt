#include "span.h"
#include "math_helpers.h"
#include "source_file.h"

unsigned span_line_index(Span me) {
    if (!span_valid(me) || !me.source) {
        return 1;
    }
    return source_file_line_index(me.source, span_start_ptr(me));
}

unsigned span_col_index(Span me) {
    return span_start_ptr(me) - span_line_start(me);
}

const char* span_line_start(Span me) {
    if (!span_valid(me)) {
        return nullptr;
    } else if (!me.source) {
        return me.sv.buf;
    } else {
        // A span that starts on a newline character belongs to the line that newline ends
        const char* pos = span_start_ptr(me);
        while (pos > source_file_start(me.source) && *(pos - 1) != '\n') {
            pos--;
        }
        return pos;
    }
}

const char* span_line_end(Span me) {
    if (!span_valid(me)) {
        return nullptr;
    } else if (!me.source) {
        return me.sv.buf + me.sv.size;
    } else {
        // Source files are null-terminated, so we can safely keep going until we hit null
        const char* pos = span_start_ptr(me);
        while (*pos && *pos != '\n') {
            pos++;
        }
        return pos;
    }
}

Span span_between(Span const a, Span const b) {
    if (!span_valid(a)) {
        return b;
    } else if (!span_valid(b)) {
        return a;
    }
    if (a.source != b.source) {
        panic("Tried to make a span between two different files");
    }
    return span_make(
        a.source,
        ccpmin(span_start_ptr(a), span_start_ptr(b)),
        ccpmax(  span_end_ptr(a),   span_end_ptr(b))
    );
}

int span_cmp(Span a, Span b) {
    switch (span_valid(a) | (span_valid(b) << 1)) {
        case 0: return 0; // Neither are valid
        case 1: return 1; // Only A is valid
        case 2: return -1; // Only b is valid
        default: // Both are valid
            break;
    }
    switch ((a.source != nullptr) | ((b.source != nullptr) << 1)) {
        case 1: return 1; // Only A is valid
        case 2: return -1; // Only b is valid
        default: // Both or neither are valid
            break;
    }
    if (a.source != b.source) {
        int cmp = cstr_cmp(&a.source->canonical_path, &b.source->canonical_path);
        panic_if(!cmp, "Two different source files both have path ", a.source->canonical_path);
        return cmp;
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
