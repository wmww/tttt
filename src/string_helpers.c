#include "string_helpers.h"
#include "math.h"

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

#define LETTER_UPPER(p) ((p) & ~0x20)
#define P_VALID(p, end) (end ? p < end : *p)

// Parses an unsigned number in the string between start and end, returns one char after last char parsed, or
// null if it failed to parse anything
static const char* unsigned_string_to_double(const char* start, const char* end, char base, double* ret) {
    double tmp = 0;
    const char* p = start;
    for (; P_VALID(p, end); p++) {
        char digit = 100;
        if (*p >= '0' && *p <= '9') {
            digit = *p - '0';
        } else if (LETTER_UPPER(*p) >= 'A' && LETTER_UPPER(*p) <= 'Z') {
            digit = LETTER_UPPER(*p) - 'A' + 10;
        }
        if (digit < base) {
            tmp = digit + tmp * base;
        } else {
            break;
        }
    }
    if (p == start) {
        return nullptr;
    } else {
        *ret = tmp;
        return p;
    }
}

const char* base_indicator_of(const char* p, const char* end, bool leading_zero_octal, unsigned char* base) {
    if (P_VALID(p, end) && *p == '0' && P_VALID(p + 1, end)) {
        p++;
        switch (LETTER_UPPER(*p)) {
            case 'X': *base = 16; return p + 1;
            case 'O': *base = 8; return p + 1;
            case 'B': *base = 2; return p + 1;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                if (leading_zero_octal) {
                    *base = 8;
                    return p;
                }
                break;
        }
    }
    return nullptr;
}

const char* string_to_double(
    const char* p,
    const char* end,
    bool allow_alternate_bases,
    bool leading_zero_octal,
    double* ret
) {
    bool value_neg = false;
    if (!P_VALID(p, end)) {
        return nullptr;
    } else if (*p == '+') {
        p++;
    } else if (*p == '-') {
        value_neg = true;
        p++;
    }

    if (allow_alternate_bases) {
        unsigned char base;
        const char* base_indicator_end = base_indicator_of(p, end, leading_zero_octal, &base);
        if (base_indicator_end) {
            p = base_indicator_end;
            double value = 0;
            const char* num_end = unsigned_string_to_double(p, end, base, &value);
            *ret = value_neg ? -value : value;
            if (num_end) {
                return num_end;
            } else {
                return p;
            }
        }
    }

    double exponent_value = 0;
    double decimal_part_value = 0;
    intptr_t decimal_part_len = 0;
    double whole_part_value = 0;
    const char* whole_part_end = unsigned_string_to_double(p, end, 10, &whole_part_value);
    if (whole_part_end) {
        p = whole_part_end;
    }
    if (P_VALID(p, end) && *p == '.') {
        p++;
        const char* decimal_part_end = unsigned_string_to_double(p, end, 10, &decimal_part_value);
        if (decimal_part_end) {
            decimal_part_len = decimal_part_end - p;
            p = decimal_part_end;
        }
    }
    if (!whole_part_end && !decimal_part_len) {
        return nullptr;
    }
    if (P_VALID(p, end) && LETTER_UPPER(*p) == 'E') {
        const char* exp_p = p + 1;
        bool exp_neg = false;
        if (!P_VALID(p, end)) {
        } else if (*exp_p == '+') {
            exp_p++;
        } else if (*exp_p == '-') {
            exp_neg = true;
            exp_p++;
        }
        const char* exponent_end = unsigned_string_to_double(exp_p, end, 10, &exponent_value);
        if (exponent_end) {
            p = exponent_end;
            if (exp_neg) exponent_value *= -1;
        }
    }
    *ret = (value_neg ? -1 : 1) * (
        whole_part_value * pow(10, exponent_value) +
        decimal_part_value * pow(10, exponent_value - decimal_part_len)
    );
    return p;
}
