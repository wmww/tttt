#pragma once

#include "prelude.h"

typedef struct Span {
    const char* file_start;
    unsigned offset;
    unsigned length;
} Span;

#define span_null ((Span){0})

static inline Span span_make(const char* file_start, unsigned offset, unsigned length) {
    return (Span){
        .file_start = file_start,
        .offset = offset,
        .length = length,
    };
}

static inline bool span_valid(Span const* me) { return me->file_start; }

static inline const char* span_start_ptr(Span const* me) {
    return my file_start ? my file_start + my offset : nullptr;
}

static inline const char* span_end_ptr(Span const* me) {
    return my file_start ? my file_start + my offset + my length : nullptr;
}

int span_line_index(Span const* me);
int span_col_index(Span const* me);
const char* span_line_start(Span const* me);
const char* span_line_end(Span const* me);
Span span_between(Span const* a, Span const* b);
int span_cmp(Span const* a, Span const* b);
bool span_eq_str(Span const* me, const char* str);
void cstr_append_span(cstr* target, Span const* me);

static inline csview span_sv(Span const* me) {
    if (!my file_start) {
        return c_sv("");
    } else {
        return csview_from_n(my file_start + my offset, my length);
    }
}

static inline cstr cstr_from_span(Span const* me) {
    return cstr_from_sv(span_sv(me));
}

#define i_key_names Span, span
#include "wrap_stc/vec.h"
