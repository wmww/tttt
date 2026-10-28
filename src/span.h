#pragma once

#include "prelude.h"
#include "ast_alloc.h"
#include <stdatomic.h>

typedef struct SourceFile SourceFile;

// A stretch of a file's text: 16 bytes (the AST holds tens of millions). The file is an index into the process-wide
// file table (source_file_register()), 0 for none (a span of text that isn't a file's: sv_make_span())
typedef struct Span {
    char const* buf;
    i32 size;
    u32 file;
} Span;

#define span_null ((Span){0})

// The process-wide file table: chunks of 1 << SPAN_FILE_CHUNK_BITS, indexes never reused (a dropped file's is null)
#define SPAN_FILE_CHUNK_BITS 12
#define SPAN_FILE_DIR_BITS 20
extern SourceFile const** span_files[1 << SPAN_FILE_DIR_BITS];
static inline SourceFile const* span_file_of(u32 file) {
    return file ? span_files[file >> SPAN_FILE_CHUNK_BITS][file & ((1u << SPAN_FILE_CHUNK_BITS) - 1)] : nullptr;
}
static inline SourceFile const* span_source(Span me) { return span_file_of(me.file); }
static inline csview span_sv(Span me) { return (csview){ .buf = me.buf, .size = me.size }; }

// A member name written as a bigint literal (`{ 1n: x }`, TS1539; a value name: name_is_value): tsc's binder declares
// no member for it (getDeclarationName gives __missing, a symbol outside the table), so types don't have it
static inline bool span_is_bigint_name(Span name, bool name_is_value) {
    return name_is_value && name.size > 1 && name.buf[0] >= '0' && name.buf[0] <= '9' && name.buf[name.size - 1] == 'n';
}
// A file's index in the table: SourceFile starts with it (static_assert in source_file.h)
static inline u32 source_file_id(SourceFile const* file) { return file ? *(u32 const*)file : 0; }
// Gives a file (at its final address) its index, before any span of it is made
void source_file_register(SourceFile* file);
// Empties the file's entry (when it is freed): its spans have no file any more
void source_file_unregister(SourceFile const* file);

// The text of a declaration's name as tsc's declarationNameToString has it: "(Missing)" where recovery left none
static inline csview span_name_text(Span me) { return me.size ? span_sv(me) : c_sv("(Missing)"); }
#define span_equals(span, lit) csview_equals_sv(span_sv(span), c_sv(lit))

static inline Span span_make(SourceFile const* source, const char* start, const char* end) {
    return (Span){ .buf = start, .size = (i32)(end - start), .file = source_file_id(source) };
}

// A span of the same file as `like`
static inline Span span_make_in(Span like, const char* start, const char* end) {
    return (Span){ .buf = start, .size = (i32)(end - start), .file = like.file };
}

static inline Span span_from_sv(SourceFile const* source, csview sv) {
    return (Span){ .buf = sv.buf, .size = (i32)sv.size, .file = source_file_id(source) };
}

static inline Span span_in_sv(Span like, csview sv) {
    return (Span){ .buf = sv.buf, .size = (i32)sv.size, .file = like.file };
}

static inline void span_set_sv(Span* me, csview sv) {
    my buf = sv.buf;
    my size = (i32)sv.size;
}

static inline Span sv_make_span(csview sv) {
    return (Span){ .buf = sv.buf, .size = (i32)sv.size };
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

static inline bool span_valid(Span me) { return me.buf; }
// The same stretch of the same file
static inline bool span_eq(Span a, Span b) { return a.buf == b.buf && a.size == b.size; }

// Whether `inner` lies within `outer` (both valid)
static inline bool span_contains_span(Span outer, Span inner) {
    return outer.buf && inner.buf && inner.buf >= outer.buf && inner.buf + inner.size <= outer.buf + outer.size;
}

static inline const char* span_start_ptr(Span me) {
    return me.buf;
}

static inline const char* span_end_ptr(Span me) {
    return me.buf ? me.buf + me.size : nullptr;
}

// Whether the span is text of its file (not a name made up for something that isn't written: the `argN` parameters
// of a JSDoc function type)
bool span_is_written(Span me);
// Starts at the `@` of a JSDoc tag (a declaration a tag makes: `@typedef`, `@property`...), sniffed from the text
bool span_is_jsdoc_tag(Span me);
unsigned span_line_index(Span me);
unsigned span_col_index(Span me);
// The UTF-16 code units (tsc's positions) the UTF-8 byte at c ends: 1 for a lead byte, and a 4-byte character's
// second unit ends at its third byte (tsc 6 could start or end a span between the two surrogates; tsgo's positions are
// UTF-8 ones, its regular expression checks never do). A byte that isn't part of a well-formed sequence is one unit
// (Go's RuneError). Looks back no further than `start` (a line's start)
unsigned utf8_continuation_utf16_units(char const* c, char const* start);
static inline unsigned utf8_byte_utf16_units(char const* c, char const* start) {
    unsigned char b = (unsigned char)*c;
    if ((b & 0xC0) != 0x80) return 1;
    return utf8_continuation_utf16_units(c, start);
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
/// The `,` that follows `after` (whitespace and comments between), else null
Span span_comma_after(Span after);
/// The token (punctuation) that follows `after` (whitespace and comments between), else null
Span span_token_after(Span after, char const* token);
int span_cmp(Span a, Span b);
void cstr_append_span(cstr* target, Span me);

static inline cstr cstr_from_span(Span me) {
    return cstr_from_sv(span_sv(me));
}

#define g_key Span, span
#include <cleat/generic/vec.h>

// The AST's lists of spans: in the file's (or a materialization's) arena while it's parsed or walked
#define g_key Span, span
#define g_type AstSpans, ast_spans
#define g_allocator ast
#include <cleat/generic/vec.h>

static inline VecSpan vec_span_single(Span span) {
    return vec_span_from_n(&span, 1);
}
