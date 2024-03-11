#include "string_helpers.h"

LinesSv_iter LinesSv_begin(csview* sv) {
    LinesSv_iter ret = {
        .ref = true,
        .line = c_sv(""),
        .remaining = *sv,
    };
    LinesSv_next(&ret);
    return ret;
}

void LinesSv_next(LinesSv_iter* self) {
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

LinesSv_iter LinesCstr_begin(cstr* str) {
    csview sv = cstr_sv(str);
    return LinesSv_begin(&sv);
}
