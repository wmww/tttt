#pragma once

#include "prelude.h"

typedef struct Span {
    const char* file_start;
    csview sv;
} Span;

#define span_null ((Span){0})
#define span_equals(span, lit) csview_equals_sv((span).sv, c_sv(lit))
#define c_SPAN(span) c_SV((span).sv)

static inline Span span_make(const char* file_start, const char* start, const char* end) {
    return (Span){
        .file_start = file_start,
        .sv = csview_from_n(start, end - start),
    };
}

static inline bool span_valid(Span me) { return me.file_start; }

static inline const char* span_start_ptr(Span me) {
    return me.sv.buf;
}

static inline const char* span_end_ptr(Span me) {
    return me.file_start ? me.sv.buf + me.sv.size : nullptr;
}

intptr_t span_line_index(Span me);
intptr_t span_col_index(Span me);
const char* span_line_start(Span me);
const char* span_line_end(Span me);
Span span_between(Span a, Span b);
int span_cmp(Span a, Span b);
void cstr_append_span(cstr* target, Span me);

static inline cstr cstr_from_span(Span me) {
    return cstr_from_sv(me.sv);
}

#define i_key_names Span, span
#include "wrap_stc/vec.h"
