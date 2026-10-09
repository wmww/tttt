// JavaScript's case mappings, a port of tsgo's js_case.go over the tables unicode_case.py generates
#include "unicode_case.h"

// One character of a string (WTF-8: a lone surrogate is its three-byte form); a byte that starts none is itself
static u32 decode(char const* p, char const* end, isize* size) {
    unsigned char b = (unsigned char)p[0];
    isize n = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
    if (p + n > end) n = 1;
    for (isize i = 1; i < n; i++) {
        if (((unsigned char)p[i] & 0xC0) != 0x80) n = 1;
    }
    *size = n;
    if (n == 1) return b;
    u32 c = b & (0xFF >> (n + 1));
    for (isize i = 1; i < n; i++) c = (c << 6) | ((unsigned char)p[i] & 0x3F);
    return c;
}

static bool in_ranges(u32 c, u32 const* ranges, u32 len) {
    if (c < ranges[0]) return false;
    u32 lo = 0, hi = len / 2;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (c < ranges[mid * 2]) hi = mid;
        else if (c > ranges[mid * 2 + 1]) lo = mid + 1;
        else return true;
    }
    return false;
}

static CaseMapping const* find(u32 c) {
    u32 lo = 0, hi = unicode_case_mappings_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (c < unicode_case_mappings[mid].code) hi = mid;
        else if (c > unicode_case_mappings[mid].code) lo = mid + 1;
        else return &unicode_case_mappings[mid];
    }
    return nullptr;
}

static bool is_cased(u32 c) { return in_ranges(c, unicode_cased_ranges, unicode_cased_ranges_len); }
static bool is_case_ignorable(u32 c) { return in_ranges(c, unicode_case_ignorable_ranges, unicode_case_ignorable_ranges_len); }

static bool is_ascii(csview text) {
    for (isize i = 0; i < text.size; i++) {
        if ((unsigned char)text.buf[i] >= 0x80) return false;
    }
    return true;
}

void unicode_to_upper_js(cstr* out, csview text) {
    if (is_ascii(text)) {
        for (isize i = 0; i < text.size; i++) {
            char c = text.buf[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            cstr_append_n(out, &c, 1);
        }
        return;
    }
    char const* end = text.buf + text.size;
    for (char const* p = text.buf; p < end;) {
        isize size;
        u32 c = decode(p, end, &size);
        CaseMapping const* m = c >= 0xD800 && c <= 0xDFFF ? nullptr : find(c);
        if (m) cstr_append_n(out, unicode_case_pool + m->upper, m->upper_len);
        else cstr_append_n(out, p, size);
        p += size;
    }
}

// Final_Sigma's forward half: the next character that isn't Case_Ignorable is cased
static bool cased_after(char const* p, char const* end) {
    while (p < end) {
        isize size;
        u32 c = decode(p, end, &size);
        p += size;
        if (!is_case_ignorable(c)) return is_cased(c);
    }
    return false;
}

void unicode_to_lower_js(cstr* out, csview text) {
    if (is_ascii(text)) {
        for (isize i = 0; i < text.size; i++) {
            char c = text.buf[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            cstr_append_n(out, &c, 1);
        }
        return;
    }
    char const* end = text.buf + text.size;
    bool cased_before = false; // Final_Sigma's backward half, tracked as we go
    for (char const* p = text.buf; p < end;) {
        isize size;
        u32 c = decode(p, end, &size);
        CaseMapping const* m = c >= 0xD800 && c <= 0xDFFF ? nullptr : find(c);
        if (!m) {
            cstr_append_n(out, p, size);
        } else if (m->final_sigma && cased_before && !cased_after(p + size, end)) {
            cstr_append_n(out, unicode_case_pool + m->conditional_lower, m->conditional_lower_len);
        } else {
            cstr_append_n(out, unicode_case_pool + m->lower, m->lower_len);
        }
        if (!is_case_ignorable(c)) cased_before = is_cased(c);
        p += size;
    }
}

isize unicode_first_char_size(csview text) {
    if (!text.size) return 0;
    isize size;
    decode(text.buf, text.buf + text.size, &size);
    return size;
}

u32 unicode_simple_lower(u32 c) {
    CaseMapping const* m = c >= 0xD800 && c <= 0xDFFF ? nullptr : find(c);
    if (!m) return c;
    isize size;
    u32 lower = decode(unicode_case_pool + m->lower, unicode_case_pool + m->lower + m->lower_len, &size);
    return size == m->lower_len ? lower : c;
}
