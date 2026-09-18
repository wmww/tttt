#define g_implement
#include "string_helpers.h"
#include "common_macros.h"

char* cstr_into_owned_ptr(cstr me) {
    if (cstr_is_long(&me)) {
        return me.lon.data;
    } else {
        size_t size_inc_null = cstr_s_size(&me) + 1;
        char* ret = malloc(size_inc_null);
        memcpy(ret, &cstr_s_data(&me), size_inc_null);
        return ret;
    }
}

lines_sv_iter lines_sv_begin(csview sv) {
    lines_sv_iter ret = {
        .ref = true,
        .line = c_sv(""),
        .remaining = sv,
    };
    lines_sv_next(&ret);
    return ret;
}

void lines_sv_next(lines_sv_iter* me) {
    const char* start = my remaining.buf;
    const char* end = start + my remaining.size;
    if (start >= end) {
        my ref = false;
        return;
    }
    const char* pos = start;
    while (pos < end && *pos != '\n') pos++;
    const char* next_start = pos < end && *pos == '\n' ? pos + 1 : pos;
    if (pos > start && *(pos - 1) == '\n') pos--;
    if (pos > start && *(pos - 1) == '\r') pos--;
    my remaining = csview_from_n(next_start, end - next_start);
    my line = csview_from_n(start, pos - start);
}

lines_sv_iter lines_cstr_begin(cstr const* str) {
    csview sv = cstr_sv(str);
    return lines_sv_begin(sv);
}

static inline bool is_whitespace(char c) {
    switch (c) {
        case ' ': case '\t': case '\n': case '\r': return true;
        default: return false;
    }
}

csview csview_trim_whitespace(csview sv) {
    const char* start = sv.buf;
    const char* end = sv.buf + sv.size;
    while (start < end && is_whitespace(*start)) { start++; }
    while (start < end && is_whitespace(*(end - 1))) { end--; }
    return csview_from_n(start, end - start);
}

void print_to_cstr_or_stderr(cstr* target, const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    if (target) {
        cstr_vfmt(target, cstr_size(target), fmt, va);
    } else {
        fprintf(stderr, "error: ");
        vfprintf(stderr, fmt, va);
        fprintf(stderr, "\n");
    }
    va_end(va);
}
