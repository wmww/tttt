#pragma once

#include "prelude.h"

typedef struct SourceFile SourceFile;

typedef struct Span {
    SourceFile const* source;
    csview sv;
} Span;

#define span_null ((Span){0})
#define span_equals(span, lit) csview_equals_sv((span).sv, c_sv(lit))
#define c_SPAN(span) c_SV((span).sv)

static inline Span span_make(SourceFile const* source, const char* start, const char* end) {
    return (Span){
        .source = source,
        .sv = csview_from_n(start, end - start),
    };
}

static inline Span sv_make_span(csview sv) {
    return (Span){
        .source = nullptr,
        .sv = sv,
    };
}

static inline bool span_valid(Span me) { return me.sv.buf; }

static inline const char* span_start_ptr(Span me) {
    return me.sv.buf;
}

static inline const char* span_end_ptr(Span me) {
    return me.sv.buf ? me.sv.buf + me.sv.size : nullptr;
}

unsigned span_line_index(Span me);
unsigned span_col_index(Span me);
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

static inline VecSpan vec_span_single(Span span) {
    return vec_span_from_n(&span, 1);
}
