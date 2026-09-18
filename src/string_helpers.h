#pragma once

#include "prelude.h"

// Most of what used to live here now comes from cleat (cstr_into_owned_ptr(), the lines_sv/lines_cstr iterators,
// csview_trim_whitespace(), csview_end_ptr(), csview_from_range()). What's left is either TypeScript-specific or
// otherwise not worth pushing upstream yet.

static inline bool streq(const char* a, const char* b) {
    return (a == b || (a && b && strcmp(a, b) == 0));
}

// Parses a double from the string start. Does not access string beyond end. If end is null assumes start is
// null-terminated. Either sets ret and returns a pointer to the end of the section parsed (one past last character
// used), or does not change ret and returns null. Fails if the string starts with whitespace or anything else not a
// number. Handles exponents, optionally handles hex/binary.
const char* string_to_double(
    const char* start,
    const char* end,
    bool allow_alternate_bases,
    bool leading_zero_octal,
    bool separators, // `1_000`, as in source code
    double* ret
);

// Skip the Byte Order Mark
const char* file_start_excluding_bom(const char* contents);
