#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "types.h"
#include <stc/cstr.h>
#include <stc/csview.h>
#include <stc/zsview.h>

// Mirrored in common_macros.h
#define cstr_append_lit(str, lit) cstr_append_sv(str, c_sv(lit))
#define c_sv_const_init(str) {str, c_litstrlen(str)}
#define cstr_null cstr_init()
#define csview_null csview_init()
#define c_SV c_svarg

#define csview_iter_deref(iter) (*iter.chr)

#define g_pro_key CStr, cstr
#define g_opt (c_use_cmp)
#define g_display
#define g_header
#include "generic/vec.h"

#define g_type VecStr, vec_str
#define g_key cstr_raw, cstr_raw
#define g_display
#define g_header
#include "generic/vec.h"

#define g_pro_key CStr, cstr
#define g_opt (c_use_cmp)
#define g_display
#define g_header
#include "generic/hset.h"

// cstr with a csview as it's raw type (for use with containers)
// #define g_pro_key SvStr, svstr
typedef cstr   SvStr;
typedef csview svstr_raw;
#define svstr_clone        cstr_clone
#define svstr_drop         cstr_drop
#define svstr_from         cstr_from_sv
#define svstr_toraw        cstr_sv
#define svstr_raw_cmp      csview_cmp
#define svstr_raw_eq       csview_eq
#define svstr_raw_hash     csview_hash

char* cstr_into_owned_ptr(cstr me);

typedef struct lines_sv_iter {
    bool ref;
    csview line;
    csview remaining;
} lines_sv_iter;

lines_sv_iter lines_sv_begin(csview sv);
void lines_sv_next(lines_sv_iter* me);
static inline csview lines_sv_iter_deref(lines_sv_iter* iter) { return iter->line; }

#define lines_cstr_iter lines_sv_iter
#define lines_cstr_next lines_sv_next
#define lines_cstr_iter_deref lines_sv_iter_deref
lines_sv_iter lines_cstr_begin(cstr const* str);

csview csview_trim_whitespace(csview sv);
static inline const char* csview_end_ptr(csview sv) { return sv.buf + sv.size; }
static inline csview csview_from_range(const char* start, const char* end) { return csview_from_n(start, end - start); }
static inline csview csview_without_prefix(csview sv, const char* x) {
    return csview_starts_with(sv, x) ? csview_slice(sv, strlen(x), sv.size) : sv;
}
static inline csview csview_without_suffix(csview sv, const char* x) {
    return csview_ends_with(sv, x) ? csview_slice(sv, 0, sv.size - strlen(x)) : sv;
}

// Formats the message to the end of target if target is non-null, else prints to stderr
void print_to_cstr_or_stderr(cstr* target, const char* fmt, ...) __attribute__((format (printf, 2, 3)));

#define STRING_COLLECTION_DISPLAYABLES \
    VecCStr:  vec_cstr_display, \
    VecStr:   vec_str_display, \
    HSetCStr: hset_cstr_display
