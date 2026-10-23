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

// A path that normalizing (absolute_path_of(), relative_path_of()) leaves alone, absolute or not: not empty, and no
// empty, `.` or `..` segment (no `//`, no trailing slash). `/` is
bool path_is_normalized(csview path);

// tsgo's nativepath.IsNetworkPath of the path normalized (slashes, `.` and `..`) as written, which tsgo reports as
// TS5116 wherever a config or a reference names one (`//server/share`, `\\?\UNC\...`, `/??/...`). A relative path is
// never one (the directory it is relative to isn't)
bool path_is_network(csview path);

// Skip the Byte Order Mark
const char* file_start_excluding_bom(const char* contents);

// A file that starts with a UTF-16 byte order mark (either endianness) is decoded to UTF-8 without it, like tsc's
// sys.readFile. Anything else is left alone
void text_decode_utf16_bom(cstr* contents);
// A source file's text as tsgo sees it: UTF-16 decoded (above). Bytes that aren't UTF-8 stay: tsgo's scanner decodes
// each as utf8.RuneError one byte wide (utf8_rune_size()), which it takes for a binary file like a written U+FFFD
void text_decode(cstr* contents);
// The size of the UTF-8 character at c: a well-formed sequence's, else 1 (Go's utf8.DecodeRune: an invalid byte is a
// RuneError of its own). Reads at most 4 bytes, stops at a NUL
int utf8_rune_size(const char* c);
// The same within `left` bytes, negative (-1) for a byte that isn't well-formed UTF-8
int utf8_rune_size_n(const char* c, intptr_t left);
// Whether c decodes to utf8.RuneError: a written U+FFFD, or a byte that starts no well-formed sequence
bool utf8_is_rune_error(const char* c);
// The UTF-16 units a continuation byte ends (see utf8_byte_utf16_units()): 0 inside a well-formed sequence (but 1 at a
// 4-byte one's third byte), 1 for a stray one (a RuneError). Looks back no further than `start`
unsigned utf8_continuation_utf16_units(char const* c, char const* start);
