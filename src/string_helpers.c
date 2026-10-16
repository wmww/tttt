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

    const char* start = p;
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
    // The parts only delimit the literal: strtod rounds correctly (and 1e999 is Infinity)
    cstr digits = cstr_init();
    for (const char* c = start; c < p; c++) {
        if (*c != '_') cstr_append_n(&digits, c, 1);
    }
    double value = strtod(cstr_str(&digits), nullptr);
    cstr_drop(&digits);
    *ret = value_neg ? -value : value;
    return p;
}

const char* file_start_excluding_bom(const char* contents) {
    if (contents[0] == '\xEF' && contents[1] == '\xBB' && contents[2] == '\xBF') {
        // Skip Byte Order Mark nonsense
        contents += 3;
    }
    return contents;
}

// The length of the well-formed UTF-8 character at b (at most `left` bytes), or of the maximal prefix of one that is
// ill-formed, negated (the WHATWG decoder replaces that prefix with one U+FFFD)
static int utf8_well_formed_size(const unsigned char* b, intptr_t left) {
    unsigned char c = b[0];
    if (c < 0x80) return 1;
    int size;
    unsigned char lo = 0x80, hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) size = 2;
    else if (c >= 0xE0 && c <= 0xEF) { size = 3; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
    else if (c >= 0xF0 && c <= 0xF4) { size = 4; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
    else return -1;
    for (int i = 1; i < size; i++) {
        if (i >= left || b[i] < lo || b[i] > hi) return -i;
        lo = 0x80, hi = 0xBF;
    }
    return size;
}

void text_decode(cstr* contents) {
    text_decode_utf16_bom(contents);
}

int utf8_rune_size(const char* c) {
    int n = utf8_rune_size_n(c, 4);
    return n > 0 ? n : 1;
}

int utf8_rune_size_n(const char* c, intptr_t left) {
    if (left <= 0 || (unsigned char)*c < 0x80) return 1;
    int n = utf8_well_formed_size((const unsigned char*)c, left);
    return n > 0 ? n : -1;
}

bool utf8_is_rune_error(const char* c) {
    unsigned char b = (unsigned char)*c;
    if (b < 0x80) return false;
    if (b == 0xEF && (unsigned char)c[1] == 0xBF && (unsigned char)c[2] == 0xBD) return true;
    return utf8_well_formed_size((const unsigned char*)c, 4) <= 0;
}

unsigned utf8_continuation_utf16_units(char const* c, char const* start) {
    for (int k = 1; k <= 3 && c - k >= start; k++) {
        unsigned char lead = (unsigned char)c[-k];
        if ((lead & 0xC0) == 0x80) continue;
        int n = utf8_well_formed_size((const unsigned char*)c - k, 4);
        if (n <= k) return 1; // (Not part of the sequence: a byte of its own, RuneError)
        return n == 4 && k == 2;
    }
    return 1;
}

void text_decode_utf16_bom(cstr* contents) {
    const unsigned char* b = (const unsigned char*)cstr_str(contents);
    intptr_t size = cstr_size(contents);
    if (size < 2) return;
    bool little = b[0] == 0xFF && b[1] == 0xFE;
    if (!little && !(b[0] == 0xFE && b[1] == 0xFF)) return;
    cstr out = cstr_with_capacity(size);
    for (intptr_t i = 2; i + 1 < size; i += 2) {
        u32 unit = little ? (u32)b[i] | (u32)b[i + 1] << 8 : (u32)b[i] << 8 | (u32)b[i + 1];
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < size) {
            u32 low = little ? (u32)b[i + 2] | (u32)b[i + 3] << 8 : (u32)b[i + 2] << 8 | (u32)b[i + 3];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        char buf[8];
        int n = cutf8_encode(buf, unit);
        cstr_append_n(&out, buf, n);
    }
    cstr_drop(contents);
    *contents = out;
}

bool path_is_normalized(csview path) {
    if (!path.size) return false;
    const char* c = path.buf;
    const char* end = path.buf + path.size;
    if (*c == '/' && ++c == end) return true;
    for (;;) {
        const char* next = memchr(c, '/', (size_t)(end - c));
        if (!next) next = end;
        isize n = next - c;
        if (!n || (c[0] == '.' && (n == 1 || (n == 2 && c[1] == '.')))) return false;
        if (next == end) return true;
        c = next + 1;
    }
}
