#pragma once

#include "prelude.h"

// Most of what used to live here now comes from cleat (cstr_into_owned_ptr(), the lines_sv/lines_cstr iterators,
// csview_trim_whitespace(), csview_end_ptr(), csview_from_range()). What's left is either TypeScript-specific or
// otherwise not worth pushing upstream yet.

static inline bool streq(const char* a, const char* b) {
    return (a == b || (a && b && strcmp(a, b) == 0));
}

static inline bool sv_starts_with(csview s, csview prefix) {
    return s.size >= prefix.size && !memcmp(s.buf, prefix.buf, (size_t)prefix.size);
}

static inline bool sv_ends_with(csview s, csview suffix) {
    return s.size >= suffix.size && !memcmp(s.buf + s.size - suffix.size, suffix.buf, (size_t)suffix.size);
}

static inline bool sv_equals_ignore_case(csview a, csview b) {
    if (a.size != b.size) return false;
    for (isize i = 0; i < a.size; i++) {
        if (tolower((unsigned char)a.buf[i]) != tolower((unsigned char)b.buf[i])) return false;
    }
    return true;
}

// The value of a hexadecimal digit, -1 for anything else
static inline int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static inline bool path_contains_node_modules(csview path) {
    return csview_find_sv(path, c_sv("/node_modules/")) != c_NPOS;
}

// A path pattern's first `*` replaced by `star` (tsc's module resolution and specifier patterns)
static inline cstr replace_first_star(csview text, csview star) {
    isize at = csview_find_sv(text, c_sv("*"));
    if (at == c_NPOS) return cstr_from_sv(text);
    return cstr_d(csview_slice(text, 0, at), star, csview_slice(text, at + 1, text.size));
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

// The size of an absolute path's root, 0 for a relative path. `/` everywhere (1). On Windows also a drive `C:/` (3) or
// `C:` (2), tsgo's getEncodedRootLength; elsewhere those are relative, a directory named `c:` (module_resolution.h's
// path_is_drive_rooted()). Windows paths are kept with forward slashes. A UNC path `//server/share` has the root `/`
// as on Linux (tsgo reads nothing on a network path, nativepath.IsNetworkPath, and tttt reports TS5116 where it does),
// so that a name joined to the root `/` with a slash of its own (`//package.json`) stays what it means on Linux
#if defined _WIN32 && !defined TTTT_WINDOWS_PATHS
// (Windows' path rules: on _WIN32, and on Linux built with -DTTTT_WINDOWS_PATHS=1 to run the suite under them)
#define TTTT_WINDOWS_PATHS 1
#endif
static inline isize path_root_size(csview path) {
    if (!path.size) return 0;
#if TTTT_WINDOWS_PATHS
    char c0 = path.buf[0] | 0x20;
    if (path.size >= 2 && c0 >= 'a' && c0 <= 'z' && path.buf[1] == ':') return path.size >= 3 && path.buf[2] == '/' ? 3 : 2;
#endif
    return path.buf[0] == '/' ? 1 : 0;
}
static inline bool path_is_rooted(csview path) { return path_root_size(path) > 0; }
// Backslashes made forward slashes in place: tsgo's normalizeSlashes, which every path given on the command line or in
// a config goes through on every platform (`-p C:\\proj`, `"include": ["src\\**\\*"]`)
static inline void path_normalize_slashes(char* path, isize size) {
    for (char* c = memchr(path, '\\', (size_t)size); c; c = memchr(c + 1, '\\', (size_t)(path + size - c - 1))) *c = '/';
}
// dir and rest joined by one slash (none added after one dir ends with: `/x`, not `//x`, which on Windows starts a UNC
// path)
static inline cstr path_concat(csview dir, csview rest) {
    return dir.size && dir.buf[dir.size - 1] == '/' ? cstr_d(dir, rest) : cstr_d(dir, "/", rest);
}
// A root (`/`, `C:/`), nothing below it
static inline bool path_is_root(csview path) { isize root = path_root_size(path); return root && path.size <= root; }
// Ends with a separator: a name is appended to it without one (a root, `/` or `C:/`)
static inline bool path_ends_with_slash(csview path) { return path.size && path.buf[path.size - 1] == '/'; }

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
