#include "string_helpers.h"

lines_sv_iter lines_sv_begin(csview* sv) {
    lines_sv_iter ret = {
        .ref = true,
        .line = c_sv(""),
        .remaining = *sv,
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
    const char* next_start = *pos == '\n' && pos < end ? pos + 1 : pos;
    if (pos > start && *(pos - 1) == '\n') pos--;
    if (pos > start && *(pos - 1) == '\r') pos--;
    my remaining = csview_from_n(next_start, end - next_start);
    my line = csview_from_n(start, pos - start);
}

lines_sv_iter lines_cstr_begin(cstr* str) {
    csview sv = cstr_sv(str);
    return lines_sv_begin(&sv);
}

csview csview_trim_whitespace(csview sv) {
    const char* start = sv.buf;
    const char* end = sv.buf + sv.size;
    while (start < end) {
        switch (*start) {
            case ' ': case '\t':
                start++;
                continue;
        }
        break;
    }
    while (start < end) {
        switch (*(end - 1)) {
            case ' ': case '\t':
                end--;
                continue;
        }
        break;
    }
    return csview_from_n(start, end - start);
}
