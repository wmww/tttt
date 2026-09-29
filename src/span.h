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
// The same stretch of the same file
static inline bool span_eq(Span a, Span b) { return a.sv.buf == b.sv.buf && a.sv.size == b.sv.size; }

// Whether `inner` lies within `outer` (both valid)
static inline bool span_contains_span(Span outer, Span inner) {
    return outer.sv.buf && inner.sv.buf && inner.sv.buf >= outer.sv.buf &&
        inner.sv.buf + inner.sv.size <= outer.sv.buf + outer.sv.size;
}

static inline const char* span_start_ptr(Span me) {
    return me.sv.buf;
}

static inline const char* span_end_ptr(Span me) {
    return me.sv.buf ? me.sv.buf + me.sv.size : nullptr;
}

// Whether the span is text of its file (not a name made up for something that isn't written: the `argN` parameters
// of a JSDoc function type)
bool span_is_written(Span me);
// Starts at the `@` of a JSDoc tag (a declaration a tag makes: `@typedef`, `@property`...), sniffed from the text
bool span_is_jsdoc_tag(Span me);
unsigned span_line_index(Span me);
unsigned span_col_index(Span me);
// The UTF-16 code units (tsc's positions) the UTF-8 byte at c ends: 1 for a lead byte, and a 4-byte character's
// second unit ends at its third byte, so a span can start or end between the two surrogates (the regular expression
// checks, which see those separately without the u flag). c[-2] is readable for a continuation byte after another
static inline unsigned utf8_byte_utf16_units(char const* c) {
    unsigned char b = (unsigned char)*c;
    if ((b & 0xC0) != 0x80) return 1;
    return ((unsigned char)c[-1] & 0xC0) == 0x80 && (unsigned char)c[-2] >= 0xF0;
}
const char* span_line_start(Span me);
const char* span_line_end(Span me);
Span span_between(Span a, Span b);
// A statement's span with the `;` that follows it on the same line, if any (the parser's statement spans end before it,
// tsc's nodes include it)
Span span_with_semicolon(Span statement);
int span_cmp(Span a, Span b);
void cstr_append_span(cstr* target, Span me);

static inline cstr cstr_from_span(Span me) {
    return cstr_from_sv(me.sv);
}

#define g_key Span, span
#include <cleat/generic/vec.h>

static inline VecSpan vec_span_single(Span span) {
    return vec_span_from_n(&span, 1);
}
