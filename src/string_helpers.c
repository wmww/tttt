#include "string_helpers.h"
#include "math.h"

#define LETTER_UPPER(p) ((p) & ~0x20)
#define P_VALID(p, end) (end ? p < end : *p)

// Parses an unsigned number in the string between start and end, returns one char after last char parsed, or
// null if it failed to parse anything
static const char* unsigned_string_to_double(const char* start, const char* end, char base, bool separators, double* ret) {
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
        } else if (*p == '_' && separators && p != start) {
            // Numeric separator. TODO: TS6188/TS6189 for misplaced ones
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
    bool separators,
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
            const char* num_end = unsigned_string_to_double(p, end, base, separators, &value);
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
    const char* whole_part_end = unsigned_string_to_double(p, end, 10, separators, &whole_part_value);
    if (whole_part_end) {
        p = whole_part_end;
    }
    if (P_VALID(p, end) && *p == '.') {
        p++;
        const char* decimal_part_end = unsigned_string_to_double(p, end, 10, separators, &decimal_part_value);
        if (decimal_part_end) {
            for (const char* digit = p; digit < decimal_part_end; digit++) {
                decimal_part_len += *digit != '_';
            }
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
        const char* exponent_end = unsigned_string_to_double(exp_p, end, 10, separators, &exponent_value);
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

const char* file_start_excluding_bom(const char* contents) {
    if (contents[0] == '\xEF' && contents[1] == '\xBB' && contents[2] == '\xBF') {
        // Skip Byte Order Mark nonsense
        contents += 3;
    }
    return contents;
}
