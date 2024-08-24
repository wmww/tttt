#pragma once

#include "prelude.h"

// cstr_vfmt seems to be a private API, but including the declaration works. Linking may break on STC update.
intptr_t cstr_vfmt(cstr* me, intptr_t start, const char* fmt, va_list args);

char* cstr_into_owned_ptr(cstr me);

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

// Parses a double from the string start. Does not access string beyond end. If end is null assumes start is
// null-terminated. Either sets ret and returns a pointer to the end of the section parsed (one past last character
// used), or does not change ret and returns null. Fails if the string starts with whitespace or anything else not a
// number. Handles exponents, optionally handles hex/binary.
const char* string_to_double(
    const char* start,
    const char* end,
    bool allow_alternate_bases,
    bool leading_zero_octal,
    double* ret
);
