#include "span.h"
#include "math_helpers.h"
#include "source_file.h"

unsigned span_line_index(Span me) {
    if (!span_valid(me) || !me.source) {
        return 1;
    }
    return source_file_line_index(me.source, span_start_ptr(me));
}

// In UTF-16 code units, like tsc's positions (utf8_byte_utf16_units)
unsigned span_col_index(Span me) {
    unsigned col = 0;
    for (const char* c = span_line_start(me); c < span_start_ptr(me); c++) col += utf8_byte_utf16_units(c);
    return col;
}

const char* span_line_start(Span me) {
    if (!span_valid(me)) {
        return nullptr;
    } else if (!me.source) {
        return me.sv.buf;
    } else {
        // A span that starts on a line break belongs to the line that line break ends
        return source_file_start(me.source) + me.source->line_starts[span_line_index(me)];
    }
}

// Where the span's first line's text ends (before its line break)
const char* span_line_end(Span me) {
    if (!span_valid(me)) {
        return nullptr;
    } else if (!me.source) {
        return me.sv.buf + me.sv.size;
    } else {
        SourceFile const* source = me.source;
        unsigned line = span_line_index(me);
        const char* text = source_file_start(source);
        if (line + 1 >= source->line_count) return text + cstr_size(&source->contents);
        const char* end = text + source->line_starts[line + 1];
        if (end[-1] == '\n') end -= end - 1 > text && end[-2] == '\r' ? 2 : 1;
        else if (end[-1] == '\r') end--;
        else end -= 3; // U+2028/9
        return end;
    }
}

Span span_with_semicolon(Span statement) {
    if (!span_valid(statement) || !statement.source) return statement;
    char const* end = span_end_ptr(statement);
    char const* file_end = cstr_str(&statement.source->contents) + cstr_size(&statement.source->contents);
    while (end < file_end && (*end == ' ' || *end == '\t')) end++;
    if (end < file_end && *end == ';') statement.sv.size = end + 1 - statement.sv.buf;
    return statement;
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

bool span_is_jsdoc_tag(Span me) {
    if (!span_is_written(me) || !me.source->is_js || *me.sv.buf != '@') return false;
    const char* file_start = source_file_start(me.source);
    const char* c = me.sv.buf;
    while (c > file_start && (c[-1] == ' ' || c[-1] == '\t')) c--;
    return c > file_start && c[-1] == '*';
}

bool span_is_written(Span me) {
    SourceFile const* file = me.source;
    char const* start = file ? source_file_start(file) : nullptr;
    return start && me.sv.buf >= start && me.sv.buf < start + cstr_size(&file->contents);
}
