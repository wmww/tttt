#pragma once

#include "prelude.h"

// JavaScript's String.prototype.toUpperCase()/toLowerCase() (tsgo's ToUpperJS/ToLowerJS): Unicode's default case
// mappings, several code points for some (ß is SS), Final_Sigma in lowercase. Lone surrogates (their WTF-8) are kept
void unicode_to_upper_js(cstr* out, csview text);
void unicode_to_lower_js(cstr* out, csview text);
// A code point's lowercase when that is one code point, else itself (Go's unicode.ToLower, near enough)
u32 unicode_simple_lower(u32 c);
// The size in bytes of the first character (a lone surrogate's WTF-8 is one), 0 for empty text
isize unicode_first_char_size(csview text);

typedef struct CaseMapping {
    u32 code;
    uint16_t lower, lower_len, upper, upper_len, conditional_lower, conditional_lower_len; // In unicode_case_pool
    u32 final_sigma; // The conditional lowercase applies in Final_Sigma context
} CaseMapping;

extern CaseMapping const unicode_case_mappings[];
extern u32 const unicode_case_mappings_count;
extern char const* const unicode_case_pool;
extern u32 const unicode_cased_ranges[];
extern u32 const unicode_cased_ranges_len;
extern u32 const unicode_case_ignorable_ranges[];
extern u32 const unicode_case_ignorable_ranges_len;
