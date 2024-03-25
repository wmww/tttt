#pragma once

#include "prelude.h"

// cstr_vfmt seems to be a private API, but including the declaration works. Linking may break on STC update.
intptr_t cstr_vfmt(cstr* me, intptr_t start, const char* fmt, va_list args);

typedef struct lines_sv_iter {
    bool ref;
    csview line;
    csview remaining;
} lines_sv_iter;

lines_sv_iter lines_sv_begin(csview* sv);
void lines_sv_next(lines_sv_iter* me);

#define lines_cstr_iter lines_sv_iter
#define lines_cstr_next lines_sv_next
lines_sv_iter lines_cstr_begin(cstr* str);

static inline bool streq(const char* a, const char* b) {
    return (a == b || (a && b && strcmp(a, b) == 0));
}

csview csview_trim_whitespace(csview sv);
static inline const char* csview_end_ptr(csview sv) { return sv.buf + sv.size; }
