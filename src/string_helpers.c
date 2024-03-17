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

void lines_sv_next(lines_sv_iter* self) {
    const char* start = self->remaining.buf;
    const char* end = start + self->remaining.size;
    if (start >= end) {
        self->ref = false;
        return;
    }
    const char* pos = start;
    while (pos < end && *pos != '\n') pos++;
    const char* next_start = *pos == '\n' && pos < end ? pos + 1 : pos;
    if (pos > start && *(pos - 1) == '\n') pos--;
    if (pos > start && *(pos - 1) == '\r') pos--;
    self->remaining = csview_from_n(next_start, end - next_start);
    self->line = csview_from_n(start, pos - start);
}

lines_sv_iter lines_cstr_begin(cstr* str) {
    csview sv = cstr_sv(str);
    return lines_sv_begin(&sv);
}
