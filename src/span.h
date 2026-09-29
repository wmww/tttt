#pragma once

#include "prelude.h"
#include <stdatomic.h>

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

// An identifier's name with its unicode escapes decoded (`\u0061` is `a`, tsc's escapedText): what names are keyed and
// compared by (symbol tables, checker_atom()). Messages that quote the source (and symbol names as printed) keep the
// raw text. Only a view of the text of a file that has escapes (names_register_source()) is cooked, so a string that
// is already a value (a string literal's) never is. Costs a load of names_escaped_files in a program without escaped names
csview name_cooked_slow(csview sv);
// How many files have an identifier with an escape (registered by the parser when it scans one): usually none, and
// then no name needs cooking
extern _Atomic u32 names_escaped_files;
static inline csview name_cooked(csview sv) {
    if (!atomic_load_explicit(&names_escaped_files, memory_order_relaxed)) return sv;
    if (sv.size <= 0 || !memchr(sv.buf, '\\', (size_t)sv.size)) return sv;
    return name_cooked_slow(sv);
}
static inline bool name_equals(csview a, csview b) {
    return csview_equals_sv(name_cooked(a), name_cooked(b));
}
// Called by the parser for every file, and when a file is dropped
void names_register_source(SourceFile* file);
void names_unregister_source(SourceFile* file);

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
// A type member's node ends after its `;` or `,` (tsc's parseTypeMemberSemicolon)
Span span_with_member_separator(Span member);
// Whether only whitespace and comments (and one `;` if allowed) are between the two positions: tsc's `a.end === b.pos`
// for adjacent nodes
bool span_only_trivia_between(char const* from, char const* to, bool allow_semicolon);
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
