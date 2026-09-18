#include <limits.h>
#include "cleat.h"
#include "math.h"
#include "seri/token.h"

#define LETTER_UPPER(p) ((p) & ~0x20)

// Parses alphanumeric digit if valid under base, else returns -1
static inline signed char parse_digit(char c, unsigned char base) {
    signed char digit = 127;
    if (c >= '0' && c <= '9') {
        digit = c - '0';
    } else if (LETTER_UPPER(c) >= 'A' && LETTER_UPPER(c) <= 'Z') {
        digit = LETTER_UPPER(c) - 'A' + 10;
    }
    if (digit < base) {
        return digit;
    } else {
        return -1;
    }
}

static inline u64 unsigned_string_parse_f64(
    const char* start,
    const char* end,
    unsigned char base,
    f64* ret
) {
    const char* p = start;
    *ret = 0;
    for (signed char digit;
         p < end && (digit = parse_digit(*p, base)) >= 0;
         p++
    ) {
        *ret = digit + (*ret) * base;
    }
    return p - start;
}

static inline u64 unsigned_string_parse_u64(
    const char* start,
    const char* end,
    unsigned char base,
    bool* overflow,
    u64* ret
) {
    const char* p = start;
    *ret = 0;
    *overflow = false;
    u64 prev = 0;
    for (signed char digit;
         p < end && (digit = parse_digit(*p, base)) >= 0;
         p++
    ) {
        *ret = digit + (*ret) * base;
        if (*ret < prev) *overflow = true;
        prev = *ret;
    }
    return p - start;
}

static inline u64 unsigned_string_parse_number(
    const char* p,
    const char* end,
    unsigned char base,
    SeriToken* ret
) {
    u64 tmp_u64;
    bool overflow;
    u64 len = unsigned_string_parse_u64(p, end, base, &overflow, &tmp_u64);
    if (overflow) {
        f64 tmp_f64;
        len = unsigned_string_parse_f64(p, end, base, &tmp_f64);
        *ret = seri_token_make(SERI_TOKEN_FLOAT, .f = tmp_f64);
    } else if (tmp_u64 <= LLONG_MAX) {
        *ret = seri_token_make(SERI_TOKEN_INT, .i = tmp_u64);
    } else {
        *ret = seri_token_make(SERI_TOKEN_UINT, .u = tmp_u64);
    }
    return len;
}

static const char* base_indicator_of(const char* p, const char* end, bool leading_zero_octal, unsigned char* base) {
    if (p + 1 < end && *p == '0') {
        p++;
        switch (LETTER_UPPER(*p)) {
            case 'X': *base = 16; return p + 1;
            case 'O': *base = 8;  return p + 1;
            case 'B': *base = 2;  return p + 1;
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

static inline void negate_number(SeriToken* token) {
    switch (token->tag) {
        case SERI_TOKEN_FLOAT:
            token->f = -token->f;
            break;

        case SERI_TOKEN_INT:
            if (token->i == LLONG_MIN) {
                *token = seri_token_make(SERI_TOKEN_UINT, .u = -token->i);
            } else {
                token->i = -token->i;
            }
            break;

        case SERI_TOKEN_UINT:
            if (token->u <= LLONG_MAX) {
                *token = seri_token_make(SERI_TOKEN_INT, .i = -(i64)token->u);
            } else if (token->u == (u64)LLONG_MAX + 1) {
                *token = seri_token_make(SERI_TOKEN_INT, .i = -(i64)(token->u - 1) - 1);
            } else {
                *token = seri_token_make(SERI_TOKEN_FLOAT, .f = -(double)token->u);
            }
            break;

        default: break;
    }
}

const char* seri_string_to_number(
    csview string,
    bool allow_alternate_bases,
    bool leading_zero_octal,
    SeriToken* ret
) {
    const char* p = string.buf;
    const char* end = csview_end_ptr(string);
    bool value_neg = false;
    if (p >= end) {
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
            p += unsigned_string_parse_number(p, end, base, ret);
            if (value_neg) negate_number(ret);
            return p;
        }
    }

    f64 exponent_value = 0;
    f64 decimal_part_value = 0;
    i64 decimal_part_len = 0;
    SeriToken whole_part_value;
    u64 whole_part_len = unsigned_string_parse_number(p, end, 10, &whole_part_value);
    p += whole_part_len;
    if (p < end && *p == '.') {
        p++;
        decimal_part_len = unsigned_string_parse_f64(p, end, 10, &decimal_part_value);
        p += decimal_part_len;
    }
    if (!whole_part_len && !decimal_part_len) return nullptr;
    if (p < end && LETTER_UPPER(*p) == 'E') {
        const char* exp_p = p + 1;
        bool exp_neg = false;
        if (exp_p >= end) {
        } else if (*exp_p == '+') {
            exp_p++;
        } else if (*exp_p == '-') {
            exp_neg = true;
            exp_p++;
        }
        u64 exp_len = unsigned_string_parse_f64(exp_p, end, 10, &exponent_value);
        exp_p += exp_len;
        if (exp_len) p = exp_p;
        if (exp_neg) exponent_value *= -1;
    }
    if (exponent_value || decimal_part_len) {
        f64 whole_part = 0;
        switch (whole_part_value.tag) {
            case SERI_TOKEN_INT:   whole_part = whole_part_value.i; break;
            case SERI_TOKEN_UINT:  whole_part = whole_part_value.u; break;
            case SERI_TOKEN_FLOAT: whole_part = whole_part_value.f; break;
            default: break;
        }
        *ret = seri_token_make(SERI_TOKEN_FLOAT, .f =
            (value_neg ? -1 : 1) * (
                whole_part * pow(10, exponent_value) +
                decimal_part_value * pow(10, exponent_value - decimal_part_len)
            )
        );
    } else {
        if (value_neg) {
            negate_number(&whole_part_value);
        }
        *ret = whole_part_value;
    }
    return p;
}

bool seri_hex_digits_to_int(csview string, u64* ret) {
    bool overflow;
    u64 len = unsigned_string_parse_u64(string.buf, csview_end_ptr(string), 16, &overflow, ret);
    return len == (u64)string.size;
}
