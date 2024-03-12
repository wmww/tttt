#pragma once

#include "prelude.h"

// cstr_vfmt seems to be a private API, but including the declaration works. Linking may break on STC update.
intptr_t cstr_vfmt(cstr* self, intptr_t start, const char* fmt, va_list args);

typedef struct LinesSv_iter {
    bool ref;
    csview line;
    csview remaining;
} LinesSv_iter;

LinesSv_iter LinesSv_begin(csview* sv);
void LinesSv_next(LinesSv_iter* self);

#define LinesCstr_iter LinesSv_iter
#define LinesCstr_next LinesSv_next
LinesSv_iter LinesCstr_begin(cstr* str);

static inline bool streq(const char* a, const char* b) {
    return (a == b || (a && b && strcmp(a, b) == 0));
}
