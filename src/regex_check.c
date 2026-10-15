// A port of tsc's regular expression validation: checkGrammarRegularExpressionLiteral (checker.ts), which rescans a
// literal with reScanSlashToken(reportErrors) and scanRegularExpressionWorker (scanner.ts). tsc works on UTF-16 code
// units, so the literal is converted to UTF-16 first and positions are indices into that, mapped back to the UTF-8
// source for the spans. The functions are named after tsc's and keep their structure (and quirks) so they can be
// compared line by line
#include "string_helpers.h"
#include "regex_check.h"
#include "compiler_options.h"
#include "unicode_identifiers.h"
#include "source_file.h"
#include "scope.h"

enum {
    RF_HAS_INDICES = 1 << 0, // d
    RF_GLOBAL = 1 << 1, // g
    RF_IGNORE_CASE = 1 << 2, // i
    RF_MULTILINE = 1 << 3, // m
    RF_DOT_ALL = 1 << 4, // s
    RF_UNICODE = 1 << 5, // u
    RF_UNICODE_SETS = 1 << 6, // v
    RF_STICKY = 1 << 7, // y
    RF_ANY_UNICODE = RF_UNICODE | RF_UNICODE_SETS,
    RF_MODIFIERS = RF_IGNORE_CASE | RF_MULTILINE | RF_DOT_ALL,
};

#define RX_EOF (-1)

// A string value tsc's scanning functions return, reduced to what the checks look at: its length in UTF-16 code
// units (0 is tsc's falsy "") and its first code point (codePointAt(s, 0))
typedef struct RxStr {
    i32 len;
    i32 cp;
} RxStr;

typedef struct RxName { // A named capturing group, its name in Rx.pool
    i32 scope; // Which of tsc's namedCapturingGroupsScopeStack sets has it
    i32 off, len;
} RxName;

typedef struct RxRef { // A `\k<name>` (tsc's groupNameReferences) or a `\1` (decimalEscapes)
    i32 pos, end;
    i32 off, len; // The name in Rx.pool
    double value;
} RxRef;

typedef struct Rx {
    uint16_t const* text; // The literal in UTF-16, with two 0 units after it (for tsc's unchecked reads)
    i32 const* offs; // Byte offset in the literal of each unit (n + 1 entries)
    i32 n;
    i32 pos;
    i32 end;
    Span literal;
    ErrorCtx* errs;
    ScriptTarget target;

    bool has_last;
    i32 last_start, last_len;
    Error* last_error;

    bool unicode_sets_mode, any_unicode_mode, any_unicode_mode_or_non_annex_b, annex_b, named_capture_groups;
    bool may_contain_strings;
    i32 number_of_capturing_groups;

    char* pool;
    i32 pool_len, pool_cap;
    RxName* names;
    i32 names_len, names_cap;
    RxRef* refs;
    i32 refs_len, refs_cap;
    RxRef* decimals;
    i32 decimals_len, decimals_cap;
    i32* scopes; // tsgo's namedCapturingGroups: a stack of the enclosing alternatives' scope ids
    i32 scopes_len, scopes_cap;
    i32 scopes_inline[16];
    i32 next_scope;
} Rx;

#define RX_GROW(arr, len, cap) \
    do { \
        if ((len) == (cap)) { \
            (cap) = (cap) ? (cap) * 2 : 8; \
            (arr) = realloc((arr), sizeof(*(arr)) * (size_t)(cap)); \
            panic_if(!(arr), "out of memory"); \
        } \
    } while (0)

// ---- Characters

static i32 cu(Rx const* me, i32 p) { // charCodeUnchecked
    return p >= 0 && p < my n ? my text[p] : 0;
}

static i32 cc(Rx const* me, i32 p) { // charCodeChecked
    return p >= 0 && p < my end ? my text[p] : RX_EOF;
}

static i32 code_point_at(Rx const* me, i32 p) { // codePointAt(text, p)
    i32 c = cu(me, p);
    if (c >= 0xD800 && c <= 0xDBFF && p + 1 < my n) {
        i32 d = my text[p + 1];
        if (d >= 0xDC00 && d <= 0xDFFF) return 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
    }
    return c;
}

static i32 cpc(Rx const* me, i32 p) { // codePointChecked
    return p >= 0 && p < my end ? code_point_at(me, p) : RX_EOF;
}

static i32 char_size(i32 ch) {
    return ch >= 0x10000 ? 2 : ch == RX_EOF ? 0 : 1;
}

static bool is_digit(i32 ch) { return ch >= '0' && ch <= '9'; }
static bool is_octal_digit(i32 ch) { return ch >= '0' && ch <= '7'; }
static bool is_hex_digit(i32 ch) { return is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'); }
static bool is_ascii_letter(i32 ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); }
static bool is_word_character(i32 ch) { return is_ascii_letter(ch) || is_digit(ch) || ch == '_'; }
static i32 hex_value(i32 ch) { return is_digit(ch) ? ch - '0' : (ch | 0x20) - 'a' + 10; }

static bool is_identifier_start(i32 ch) {
    return is_ascii_letter(ch) || ch == '$' || ch == '_' ||
        (ch > 0x7F && unicode_is_identifier_start((u32)ch));
}

static bool is_identifier_part(i32 ch) {
    return is_ascii_letter(ch) || is_digit(ch) || ch == '$' || ch == '_' ||
        (ch > 0x7F && unicode_is_identifier_part((u32)ch));
}

static RxStr str_empty(void) { return (RxStr){ 0, 0 }; }
static RxStr str_unit(i32 unit) { return (RxStr){ 1, unit & 0xFFFF }; } // String.fromCharCode
static RxStr str_code_point(i32 cp) { return (RxStr){ char_size(cp), cp }; } // utf16EncodeAsString

static RxStr str_slice(Rx const* me, i32 start, i32 end) { // text.substring(start, end)
    if (end <= start) return str_empty();
    i32 cp = code_point_at(me, start);
    if (cp >= 0x10000 && end - start < 2) cp = cu(me, start);
    return (RxStr){ end - start, cp };
}

static u32 flag_of(i32 ch) { // characterCodeToRegularExpressionFlag
    switch (ch) {
        case 'd': return RF_HAS_INDICES;
        case 'g': return RF_GLOBAL;
        case 'i': return RF_IGNORE_CASE;
        case 'm': return RF_MULTILINE;
        case 's': return RF_DOT_ALL;
        case 'u': return RF_UNICODE;
        case 'v': return RF_UNICODE_SETS;
        case 'y': return RF_STICKY;
        default: return 0;
    }
}

// ---- Errors

static Span rx_span(Rx const* me, i32 start, i32 len) {
    i32 a = start < 0 ? 0 : start > my n ? my n : start;
    i32 b = start + len < a ? a : start + len > my n ? my n : start + len;
    return span_in_sv(my literal, csview_from_n(my literal.buf + my offs[a], my offs[b] - my offs[a]));
}

// The onError of checkGrammarRegularExpressionLiteral: the first error at a position wins, and a message (TS1369 "Did
// you mean") right where the last error is becomes its related information
static void rx_report(Rx* me, i32 start, i32 len, unsigned code, cstr message) {
    if (code == 1369 && my has_last && start == my last_start && len == my last_len) {
        error_add_detached_related_s(my last_error, code, message);
    } else if (!my has_last || start != my last_start) {
        my has_last = true;
        my last_start = start;
        my last_len = len;
        my last_error = error_ctx_error_s(my errs, rx_span(me, start, len), code, message);
    } else {
        cstr_drop(&message);
    }
}

#define rx_error(me, start, len, code, ...) rx_report(me, start, len, code, cstr_d(__VA_ARGS__))

static void check_flag_availability(Rx* me, u32 flag, i32 size) { // checkRegularExpressionFlagAvailability
    ScriptTarget from;
    char const* name;
    switch (flag) {
        case RF_HAS_INDICES: from = SCRIPT_TARGET_ES2022; name = "es2022"; break;
        case RF_DOT_ALL: from = SCRIPT_TARGET_ES2018; name = "es2018"; break;
        case RF_UNICODE: case RF_STICKY: from = SCRIPT_TARGET_ES2015; name = "es6"; break;
        case RF_UNICODE_SETS: from = SCRIPT_TARGET_ES2024; name = "es2024"; break;
        default: return;
    }
    if (my target < from) {
        rx_error(me, my pos, size, 1501, "This regular expression flag is only available when targeting '", name, "' or later.");
    }
}

// ---- Names

static void pool_push(Rx* me, char c) {
    RX_GROW(my pool, my pool_len, my pool_cap);
    my pool[my pool_len++] = c;
}

static void pool_push_code_point(Rx* me, u32 cp) {
    if (cp < 0x80) {
        pool_push(me, (char)cp);
    } else if (cp < 0x800) {
        pool_push(me, (char)(0xC0 | (cp >> 6)));
        pool_push(me, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        pool_push(me, (char)(0xE0 | (cp >> 12)));
        pool_push(me, (char)(0x80 | ((cp >> 6) & 0x3F)));
        pool_push(me, (char)(0x80 | (cp & 0x3F)));
    } else {
        pool_push(me, (char)(0xF0 | (cp >> 18)));
        pool_push(me, (char)(0x80 | ((cp >> 12) & 0x3F)));
        pool_push(me, (char)(0x80 | ((cp >> 6) & 0x3F)));
        pool_push(me, (char)(0x80 | (cp & 0x3F)));
    }
}

static csview pool_sv(Rx const* me, i32 off, i32 len) {
    return csview_from_n(my pool + off, len);
}

// namedCapturingGroupsContains: whether an alternative on the scope stack defines the name
static bool scopes_have(Rx const* me, csview name) {
    for (i32 i = 0; i < my names_len; i++) {
        if (!csview_equals_sv(pool_sv(me, my names[i].off, my names[i].len), name)) continue;
        for (i32 k = 0; k < my scopes_len; k++) {
            if (my scopes[k] == my names[i].scope) return true;
        }
    }
    return false;
}

static bool group_specifiers_have(Rx const* me, csview name) {
    for (i32 i = 0; i < my names_len; i++) {
        if (csview_equals_sv(pool_sv(me, my names[i].off, my names[i].len), name)) return true;
    }
    return false;
}

static void scopes_push(Rx* me, i32 scope) {
    if (my scopes_len == my scopes_cap) {
        i32 cap = my scopes_cap * 2;
        i32* grown = my scopes == my scopes_inline ? malloc(sizeof(i32) * (size_t)cap) : realloc(my scopes, sizeof(i32) * (size_t)cap);
        panic_if(!grown, "out of memory");
        if (my scopes == my scopes_inline) memcpy(grown, my scopes_inline, sizeof(my scopes_inline));
        my scopes = grown;
        my scopes_cap = cap;
    }
    my scopes[my scopes_len++] = scope;
}

// ---- The scanner

static void scan_disjunction(Rx* me, bool is_in_group);
static void scan_class_set_expression(Rx* me);
static bool scan_character_class_escape(Rx* me);
static RxStr scan_character_escape(Rx* me, bool atom_escape);

static void scan_digits(Rx* me) {
    while (is_digit(cc(me, my pos))) my pos++;
}

static double digits_value(Rx const* me, i32 start, i32 end) {
    double value = 0;
    for (i32 p = start; p < end; p++) value = value * 10 + (my text[p] - '0');
    return value;
}

// compareDecimalStrings: exact for any number of digits
static i32 compare_digits(Rx const* me, i32 a, i32 a_end, i32 b, i32 b_end) {
    while (a < a_end - 1 && my text[a] == '0') a++;
    while (b < b_end - 1 && my text[b] == '0') b++;
    if (a_end - a != b_end - b) return a_end - a < b_end - b ? -1 : 1;
    for (; a < a_end; a++, b++) {
        if (my text[a] != my text[b]) return my text[a] < my text[b] ? -1 : 1;
    }
    return 0;
}

static void scan_expected_char(Rx* me, char ch) {
    if (cc(me, my pos) == ch) {
        my pos++;
    } else {
        rx_error(me, my pos, 0, 1005, "'", ch, "' expected.");
    }
}

static RxStr scan_source_character(Rx* me) {
    i32 size = my any_unicode_mode ? char_size(cpc(me, my pos)) : 1;
    my pos += size;
    return size > 0 ? str_slice(me, my pos - size, my pos) : str_empty();
}

// scanHexDigits(1, /*scanAsManyAsPossible*/ true): the value, or -1 without digits (saturated past 0x10FFFF)
static i64 scan_hex_digits_value(Rx* me) {
    i64 value = 0;
    i32 count = 0;
    while (is_hex_digit(cu(me, my pos))) {
        value = value * 16 + hex_value(cu(me, my pos));
        if (value > 0x110000) value = 0x110000;
        count++;
        my pos++;
    }
    return count ? value : -1;
}

static RxStr scan_extended_unicode_escape(Rx* me) {
    i32 start = my pos;
    my pos += 3;
    i32 escaped_start = my pos;
    i64 escaped_value = scan_hex_digits_value(me);
    bool invalid = false;
    if (escaped_value < 0) {
        rx_error(me, my pos, 0, 1125, "Hexadecimal digit expected.");
        invalid = true;
    } else if (escaped_value > 0x10FFFF) {
        rx_error(me, escaped_start, my pos - escaped_start, 1198,
            "An extended Unicode escape value must be between 0x0 and 0x10FFFF inclusive.");
        invalid = true;
    }
    if (my pos >= my end) {
        rx_error(me, my pos, 0, 1126, "Unexpected end of text.");
        invalid = true;
    } else if (cu(me, my pos) == '}') {
        my pos++;
    } else {
        rx_error(me, my pos, 0, 1199, "Unterminated Unicode escape sequence.");
        invalid = true;
    }
    if (invalid) return str_slice(me, start, my pos);
    return str_code_point((i32)escaped_value);
}

static i32 parse_hex(Rx const* me, i32 start, i32 end) {
    i32 value = 0;
    for (i32 p = start; p < end; p++) value = value * 16 + hex_value(my text[p]);
    return value;
}

// scanEscapeSequence with the RegularExpression flags (which always report errors)
static RxStr scan_escape_sequence(Rx* me, bool atom_escape) {
    i32 start = my pos;
    my pos++;
    if (my pos >= my end) {
        rx_error(me, my pos, 0, 1126, "Unexpected end of text.");
        return str_empty();
    }
    i32 ch = cu(me, my pos);
    my pos++;
    switch (ch) {
        case '0':
            if (my pos >= my end || !is_digit(cu(me, my pos))) return str_unit(0);
            [[fallthrough]];
        case '1': case '2': case '3':
            if (my pos < my end && is_octal_digit(cu(me, my pos))) my pos++;
            [[fallthrough]];
        case '4': case '5': case '6': case '7': {
            if (my pos < my end && is_octal_digit(cu(me, my pos))) my pos++;
            i32 code = 0;
            for (i32 p = start + 1; p < my pos; p++) code = code * 8 + (my text[p] - '0');
            char hex[8];
            snprintf(hex, sizeof hex, "%02x", code);
            if (!atom_escape && ch != '0') {
                rx_error(me, start, my pos - start, 1536, "Octal escape sequences and backreferences are not allowed in a "
                    "character class. If this was intended as an escape sequence, use the syntax '\\x", hex, "' instead.");
            } else {
                rx_error(me, start, my pos - start, 1487, "Octal escape sequences are not allowed. Use the syntax '\\x", hex, "'.");
            }
            return str_unit(code);
        }
        case '8': case '9':
            if (!atom_escape) {
                rx_error(me, start, my pos - start, 1537,
                    "Decimal escape sequences and backreferences are not allowed in a character class.");
            } else {
                rx_error(me, start, my pos - start, 1488, "Escape sequence '\\", (char)ch, "' is not allowed.");
            }
            return str_unit(ch);
        case 'b': return str_unit('\b');
        case 't': return str_unit('\t');
        case 'n': return str_unit('\n');
        case 'v': return str_unit('\v');
        case 'f': return str_unit('\f');
        case 'r': return str_unit('\r');
        case '\'': return str_unit('\'');
        case '"': return str_unit('"');
        case 'u': {
            if (my pos < my end && cu(me, my pos) == '{') {
                // '\u{DDDDDD}'
                my pos -= 2;
                RxStr result = scan_extended_unicode_escape(me);
                if (!my any_unicode_mode) {
                    rx_error(me, start, my pos - start, 1538,
                        "Unicode escape sequences are only available when the Unicode (u) flag or the Unicode Sets (v) flag is set.");
                }
                return result;
            }
            // '\uDDDD'
            for (; my pos < start + 6; my pos++) {
                if (!(my pos < my end && is_hex_digit(cu(me, my pos)))) {
                    rx_error(me, my pos, 0, 1125, "Hexadecimal digit expected.");
                    return str_slice(me, start, my pos);
                }
            }
            i32 value = parse_hex(me, start + 2, my pos);
            if (my any_unicode_mode && value >= 0xD800 && value <= 0xDBFF && my pos + 6 < my end &&
                cu(me, my pos) == '\\' && cu(me, my pos + 1) == 'u' && cu(me, my pos + 2) != '{'
            ) {
                // In a Unicode mode, \u HexLeadSurrogate \u HexTrailSurrogate is a single character
                i32 next_start = my pos;
                i32 next_pos = my pos + 2;
                for (; next_pos < next_start + 6; next_pos++) {
                    if (!is_hex_digit(cu(me, next_pos))) return str_unit(value);
                }
                i32 next_value = parse_hex(me, next_start + 2, next_pos);
                if (next_value >= 0xDC00 && next_value <= 0xDFFF) {
                    my pos = next_pos;
                    return (RxStr){ 2, 0x10000 + ((value - 0xD800) << 10) + (next_value - 0xDC00) };
                }
            }
            return str_unit(value);
        }
        case 'x':
            // '\xDD'
            for (; my pos < start + 4; my pos++) {
                if (!(my pos < my end && is_hex_digit(cu(me, my pos)))) {
                    rx_error(me, my pos, 0, 1125, "Hexadecimal digit expected.");
                    return str_slice(me, start, my pos);
                }
            }
            return str_unit(parse_hex(me, start + 2, my pos));
        case '\r':
            if (my pos < my end && cu(me, my pos) == '\n') my pos++;
            [[fallthrough]];
        case '\n': case 0x2028: case 0x2029:
            return str_empty();
        default:
            if (my any_unicode_mode || (!my annex_b && is_identifier_part(ch))) {
                rx_error(me, my pos - 2, 2, 1535, "This character cannot be escaped in a regular expression.");
            }
            return str_unit(ch);
    }
}

// scanUnicodeEscape(false) at `at` (a backslash followed by 'u'), either form: the code point, or -1 when invalid;
// *end_out is where it stops
static i32 scan_unicode_escape_at(Rx const* me, i32 at, i32* end_out) {
    i32 p = at + 2;
    i64 value = 0;
    if (cu(me, p) == '{') {
        p++;
        i32 count = 0;
        while (is_hex_digit(cu(me, p))) {
            value = value * 16 + hex_value(cu(me, p));
            if (value > 0x110000) value = 0x110000;
            count++;
            p++;
        }
        bool invalid = !count || value > 0x10FFFF;
        if (count && p >= my n) invalid = true;
        else if (count && cu(me, p) == '}') p++;
        else invalid = true;
        *end_out = p;
        return invalid ? -1 : (i32)value;
    }
    for (i32 k = 0; k < 4; k++, p++) {
        if (!is_hex_digit(cu(me, p))) {
            *end_out = p;
            return -1;
        }
        value = value * 16 + hex_value(cu(me, p));
    }
    *end_out = p;
    return (i32)value;
}

// scanIdentifierEscape(isValid, allowSurrogatePairEscape = true): a `\uXXXX` or `\u{X}` escape whose code point
// passes the check, or (group names only) a `\uHigh\uLow` pair; appends it to the pool
static bool scan_identifier_escape(Rx* me, bool start) {
    if (cu(me, my pos + 1) != 'u') return false;
    i32 end;
    i32 escaped = scan_unicode_escape_at(me, my pos, &end);
    if (escaped >= 0 && (start ? is_identifier_start(escaped) : is_identifier_part(escaped))) {
        my pos = end;
        pool_push_code_point(me, (u32)escaped);
        return true;
    }
    if (cu(me, my pos + 2) != '{' && escaped >= 0xD800 && escaped <= 0xDBFF && cu(me, end) == '\\' &&
        cu(me, end + 1) == 'u' && cu(me, end + 2) != '{'
    ) {
        i32 low_end;
        i32 low = scan_unicode_escape_at(me, end, &low_end);
        if (low >= 0xDC00 && low <= 0xDFFF) {
            i32 cp = 0x10000 + ((escaped - 0xD800) << 10) + (low - 0xDC00);
            if (start ? is_identifier_start(cp) : is_identifier_part(cp)) {
                my pos = low_end;
                pool_push_code_point(me, (u32)cp);
                return true;
            }
        }
    }
    return false;
}

// scanIdentifierParts, appending the value to the pool
static void scan_identifier_parts(Rx* me) {
    while (true) {
        i32 ch = code_point_at(me, my pos);
        if (my pos < my n && is_identifier_part(ch)) {
            pool_push_code_point(me, (u32)ch);
            my pos += char_size(ch);
        } else if (ch == '\\' && scan_identifier_escape(me, false)) {
            continue;
        } else {
            break;
        }
    }
}

static void scan_group_name(Rx* me, bool is_reference) {
    i32 token_start = my pos;
    i32 off = my pool_len;
    // scanIdentifier(0, identifierVariantRegExpGroupName)
    i32 ch = cpc(me, my pos);
    if (is_identifier_start(ch)) {
        pool_push_code_point(me, (u32)ch);
        my pos += char_size(ch);
        scan_identifier_parts(me);
    } else if (ch == '\\' && scan_identifier_escape(me, true)) {
        scan_identifier_parts(me);
    }
    csview name = pool_sv(me, off, my pool_len - off);
    if (my pos == token_start) {
        rx_error(me, my pos, 0, 1514, "Expected a capturing group name.");
    } else if (is_reference) {
        RX_GROW(my refs, my refs_len, my refs_cap);
        my refs[my refs_len++] = (RxRef){ token_start, my pos, off, my pool_len - off, 0 };
        return;
    } else {
        if (scopes_have(me, name)) {
            rx_error(me, token_start, my pos - token_start, 1515,
                "Named capturing groups with the same name must be mutually exclusive to each other.");
        } else {
            // A previous definition can only have come from a mutually exclusive alternative. Below ES2018 the group
            // itself is already reported
            if (group_specifiers_have(me, name) && my target >= SCRIPT_TARGET_ES2018 && my target < SCRIPT_TARGET_ES2025) {
                rx_error(me, token_start, my pos - token_start, 18063,
                    "Duplicate named capturing groups are only available when targeting 'es2025' or later.");
            }
            RX_GROW(my names, my names_len, my names_cap);
            my names[my names_len++] = (RxName){ my scopes_len ? my scopes[my scopes_len - 1] : 0, off, my pool_len - off };
            return;
        }
    }
    my pool_len = off;
}

static u32 scan_pattern_modifiers(Rx* me, u32 curr_flags) {
    while (true) {
        i32 ch = cpc(me, my pos);
        if (ch == RX_EOF || !is_identifier_part(ch)) break;
        i32 size = char_size(ch);
        u32 flag = flag_of(ch);
        if (!flag) {
            rx_error(me, my pos, size, 1499, "Unknown regular expression flag.");
        } else if (curr_flags & flag) {
            rx_error(me, my pos, size, 1500, "Duplicate regular expression flag.");
        } else if (!(flag & RF_MODIFIERS)) {
            rx_error(me, my pos, size, 1509, "This regular expression flag cannot be toggled within a subpattern.");
        } else {
            // Modifier syntax itself requires ES2025, later than any flag that can appear here
            curr_flags |= flag;
        }
        my pos += size;
    }
    return curr_flags;
}

static bool scan_decimal_escape(Rx* me) {
    i32 ch = cc(me, my pos);
    if (ch >= '1' && ch <= '9') {
        i32 start = my pos;
        scan_digits(me);
        RX_GROW(my decimals, my decimals_len, my decimals_cap);
        my decimals[my decimals_len++] = (RxRef){ start, my pos, 0, 0, digits_value(me, start, my pos) };
        return true;
    }
    return false;
}

static void scan_atom_escape(Rx* me) {
    switch (cc(me, my pos)) {
        case 'k':
            my pos++;
            if (cc(me, my pos) == '<') {
                my pos++;
                scan_group_name(me, true);
                scan_expected_char(me, '>');
            } else if (my any_unicode_mode_or_non_annex_b || my named_capture_groups) {
                rx_error(me, my pos - 2, 2, 1510, "'\\k' must be followed by a capturing group name enclosed in angle brackets.");
            }
            break;
        case 'q':
            if (my unicode_sets_mode) {
                my pos++;
                rx_error(me, my pos - 2, 2, 1511, "'\\q' is only available inside character class.");
                break;
            }
            [[fallthrough]];
        default:
            if (!scan_character_class_escape(me) && !scan_decimal_escape(me)) scan_character_escape(me, true);
            break;
    }
}

static RxStr scan_character_escape(Rx* me, bool atom_escape) {
    i32 ch = cc(me, my pos);
    switch (ch) {
        case RX_EOF:
            rx_error(me, my pos - 1, 1, 1513, "Undetermined character escape.");
            return str_unit('\\');
        case 'c':
            my pos++;
            ch = cc(me, my pos);
            if (is_ascii_letter(ch)) {
                my pos++;
                return str_unit(ch & 0x1f);
            }
            if (my any_unicode_mode_or_non_annex_b) {
                rx_error(me, my pos - 2, 2, 1512, "'\\c' must be followed by an ASCII letter.");
            } else if (atom_escape) {
                // Annex B treats `\` followed by a `c` without a control letter as the single character `\`
                my pos--;
                return str_unit('\\');
            }
            return str_unit(ch);
        case '^': case '$': case '/': case '\\': case '.': case '*': case '+': case '?': case '(': case ')':
        case '[': case ']': case '{': case '}': case '|':
            my pos++;
            return str_unit(ch);
        default:
            my pos--;
            return scan_escape_sequence(me, atom_escape);
    }
}

static bool is_class_content_exit(Rx const* me, i32 ch) {
    return ch == ']' || ch == RX_EOF || my pos >= my end;
}

// ClassAtom
static RxStr scan_class_atom(Rx* me) {
    if (cc(me, my pos) == '\\') {
        my pos++;
        i32 ch = cc(me, my pos);
        switch (ch) {
            case 'b':
                my pos++;
                return str_unit('\b');
            case '-':
                my pos++;
                return str_unit(ch);
            default:
                if (scan_character_class_escape(me)) return str_empty();
                return scan_character_escape(me, false);
        }
    }
    return scan_source_character(me);
}

static bool is_range_out_of_order(RxStr min, RxStr max) {
    return min.len == char_size(min.cp) && max.len == char_size(max.cp) && min.cp > max.cp;
}

// ClassRanges ::= '^'? (ClassAtom ('-' ClassAtom)?)*
static void scan_class_ranges(Rx* me) {
    if (cc(me, my pos) == '^') my pos++;
    while (true) {
        i32 ch = cc(me, my pos);
        if (is_class_content_exit(me, ch)) return;
        i32 min_start = my pos;
        RxStr min = scan_class_atom(me);
        if (cc(me, my pos) == '-') {
            my pos++;
            ch = cc(me, my pos);
            if (is_class_content_exit(me, ch)) return;
            if (!min.len && my any_unicode_mode_or_non_annex_b) {
                rx_error(me, min_start, my pos - 1 - min_start, 1516,
                    "A character class range must not be bounded by another character class.");
            }
            i32 max_start = my pos;
            RxStr max = scan_class_atom(me);
            if (!max.len && my any_unicode_mode_or_non_annex_b) {
                rx_error(me, max_start, my pos - max_start, 1516,
                    "A character class range must not be bounded by another character class.");
                continue;
            }
            if (!min.len) continue;
            if (is_range_out_of_order(min, max)) {
                rx_error(me, min_start, my pos - min_start, 1517, "Range out of order in character class.");
            }
        }
    }
}

static bool is_reserved_double_punctuator(i32 ch) {
    switch (ch) {
        case '&': case '!': case '#': case '%': case '*': case '+': case ',': case '.': case ':': case ';': case '<':
        case '=': case '>': case '?': case '@': case '`': case '~':
            return true;
        default:
            return false;
    }
}

// ClassSetCharacter
static RxStr scan_class_set_character(Rx* me) {
    i32 ch = cc(me, my pos);
    if (ch == RX_EOF) return str_empty();
    if (ch == '\\') {
        my pos++;
        ch = cc(me, my pos);
        switch (ch) {
            case 'b':
                my pos++;
                return str_unit('\b');
            case '&': case '-': case '!': case '#': case '%': case ',': case ':': case ';': case '<': case '=': case '>':
            case '@': case '`': case '~':
                my pos++;
                return str_unit(ch);
            default:
                return scan_character_escape(me, false);
        }
    } else if (ch == cc(me, my pos + 1) && is_reserved_double_punctuator(ch)) {
        rx_error(me, my pos, 2, 1522,
            "A character class must not contain a reserved double punctuator. Did you mean to escape it with backslash?");
        my pos += 2;
        return str_slice(me, my pos - 2, my pos);
    }
    switch (ch) {
        case '/': case '(': case ')': case '[': case ']': case '{': case '}': case '-': case '|':
            rx_error(me, my pos, 1, 1508, "Unexpected '", (char)ch, "'. Did you mean to escape it with backslash?");
            my pos++;
            return str_unit(ch);
    }
    return scan_source_character(me);
}

// ClassStringDisjunctionContents ::= ClassSetCharacter* ('|' ClassSetCharacter*)*
static void scan_class_string_disjunction_contents(Rx* me) {
    i32 character_count = 0;
    while (true) {
        switch (cc(me, my pos)) {
            case RX_EOF:
                return;
            case '}':
                if (character_count != 1) my may_contain_strings = true;
                return;
            case '|':
                if (character_count != 1) my may_contain_strings = true;
                my pos++;
                character_count = 0;
                break;
            default:
                scan_class_set_character(me);
                character_count++;
                break;
        }
    }
}

// ClassSetOperand
static RxStr scan_class_set_operand(Rx* me) {
    my may_contain_strings = false;
    switch (cc(me, my pos)) {
        case RX_EOF:
            return str_empty();
        case '[':
            my pos++;
            scan_class_set_expression(me);
            scan_expected_char(me, ']');
            return str_empty();
        case '\\':
            my pos++;
            if (scan_character_class_escape(me)) {
                return str_empty();
            } else if (cc(me, my pos) == 'q') {
                my pos++;
                if (cc(me, my pos) == '{') {
                    my pos++;
                    scan_class_string_disjunction_contents(me);
                    scan_expected_char(me, '}');
                    return str_empty();
                }
                rx_error(me, my pos - 2, 2, 1521, "'\\q' must be followed by string alternatives enclosed in braces.");
                return str_unit('q');
            }
            my pos--;
            [[fallthrough]];
        default:
            return scan_class_set_character(me);
    }
}

// Unicode strings on the UTF-16 text for an error message argument
static void unexpected_char(Rx* me, i32 at, i32 ch) {
    char buf[8] = {0};
    u32 cp = (u32)(ch < 0 ? 0xFFFF : ch);
    if (cp < 0x80) {
        buf[0] = (char)cp;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6)); buf[1] = (char)(0x80 | (cp & 0x3F));
    } else {
        buf[0] = (char)(0xE0 | (cp >> 12)); buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[2] = (char)(0x80 | (cp & 0x3F));
    }
    rx_error(me, at, 1, 1508, "Unexpected '", buf, "'. Did you mean to escape it with backslash?");
}

typedef enum ClassSetExpressionType {
    CLASS_UNION,
    CLASS_INTERSECTION,
    CLASS_SUBTRACTION,
} ClassSetExpressionType;

static char const* const operators_mixed = "Operators must not be mixed within a character class. Wrap it in a nested class instead.";
static char const* const negated_strings =
    "Anything that would possibly match more than a single character is invalid inside a negated character class.";

static void scan_class_set_sub_expression(Rx* me, ClassSetExpressionType type) {
    bool expression_may_contain_strings = my may_contain_strings;
    while (true) {
        i32 ch = cc(me, my pos);
        if (is_class_content_exit(me, ch)) break;
        switch (ch) {
            case '-':
                my pos++;
                if (cc(me, my pos) == '-') {
                    my pos++;
                    if (type != CLASS_SUBTRACTION) rx_error(me, my pos - 2, 2, 1519, operators_mixed);
                } else {
                    rx_error(me, my pos - 1, 1, 1519, operators_mixed);
                }
                break;
            case '&':
                my pos++;
                if (cc(me, my pos) == '&') {
                    my pos++;
                    if (type != CLASS_INTERSECTION) rx_error(me, my pos - 2, 2, 1519, operators_mixed);
                    if (cc(me, my pos) == '&') {
                        unexpected_char(me, my pos, ch);
                        my pos++;
                    }
                } else {
                    unexpected_char(me, my pos - 1, ch);
                }
                break;
            default:
                if (type == CLASS_SUBTRACTION) rx_error(me, my pos, 0, 1005, "'--' expected.");
                else if (type == CLASS_INTERSECTION) rx_error(me, my pos, 0, 1005, "'&&' expected.");
                break;
        }
        ch = cc(me, my pos);
        if (is_class_content_exit(me, ch)) {
            rx_error(me, my pos, 0, 1520, "Expected a class set operand.");
            break;
        }
        scan_class_set_operand(me);
        // Used only if the type is an intersection
        expression_may_contain_strings = expression_may_contain_strings && my may_contain_strings;
    }
    my may_contain_strings = expression_may_contain_strings;
}

static bool at_double_operator(Rx const* me) { // text.slice(pos, pos + 2) is "--" or "&&"
    i32 a = cu(me, my pos);
    return (a == '-' || a == '&') && my pos + 1 < my n && cu(me, my pos + 1) == a;
}

// ClassSetExpression ::= '^'? (ClassUnion | ClassIntersection | ClassSubtraction)
static void scan_class_set_expression(Rx* me) {
    bool is_character_complement = false;
    if (cc(me, my pos) == '^') {
        my pos++;
        is_character_complement = true;
    }
    bool expression_may_contain_strings = false;
    i32 ch = cc(me, my pos);
    if (is_class_content_exit(me, ch)) return;
    i32 start = my pos;
    RxStr operand = str_empty();
    if (at_double_operator(me)) {
        rx_error(me, my pos, 0, 1520, "Expected a class set operand.");
        my may_contain_strings = false;
    } else {
        operand = scan_class_set_operand(me);
    }
    switch (cc(me, my pos)) {
        case '-':
            if (cc(me, my pos + 1) == '-') {
                if (is_character_complement && my may_contain_strings) rx_error(me, start, my pos - start, 1518, negated_strings);
                expression_may_contain_strings = my may_contain_strings;
                scan_class_set_sub_expression(me, CLASS_SUBTRACTION);
                my may_contain_strings = !is_character_complement && expression_may_contain_strings;
                return;
            }
            break;
        case '&':
            if (cc(me, my pos + 1) == '&') {
                scan_class_set_sub_expression(me, CLASS_INTERSECTION);
                if (is_character_complement && my may_contain_strings) rx_error(me, start, my pos - start, 1518, negated_strings);
                expression_may_contain_strings = my may_contain_strings;
                my may_contain_strings = !is_character_complement && expression_may_contain_strings;
                return;
            }
            break;
        default:
            if (is_character_complement && my may_contain_strings) rx_error(me, start, my pos - start, 1518, negated_strings);
            expression_may_contain_strings = my may_contain_strings;
            break;
    }
    while (true) {
        ch = cc(me, my pos);
        if (ch == RX_EOF) break;
        switch (ch) {
            case '-': {
                my pos++;
                ch = cc(me, my pos);
                if (is_class_content_exit(me, ch)) {
                    my may_contain_strings = !is_character_complement && expression_may_contain_strings;
                    return;
                }
                if (ch == '-') {
                    my pos++;
                    rx_error(me, my pos - 2, 2, 1519, operators_mixed);
                    start = my pos - 2;
                    operand = str_slice(me, start, my pos);
                    continue;
                }
                if (!operand.len) {
                    rx_error(me, start, my pos - 1 - start, 1516, "A character class range must not be bounded by another character class.");
                }
                i32 second_start = my pos;
                RxStr second_operand = scan_class_set_operand(me);
                if (is_character_complement && my may_contain_strings) {
                    rx_error(me, second_start, my pos - second_start, 1518, negated_strings);
                }
                expression_may_contain_strings = expression_may_contain_strings || my may_contain_strings;
                if (!second_operand.len) {
                    rx_error(me, second_start, my pos - second_start, 1516,
                        "A character class range must not be bounded by another character class.");
                    break;
                }
                if (!operand.len) break;
                if (is_range_out_of_order(operand, second_operand)) {
                    rx_error(me, start, my pos - start, 1517, "Range out of order in character class.");
                }
            }   break;
            case '&':
                // A lone '&' is a class set character (tsgo's scanClassSetExpression)
                if (cc(me, my pos + 1) == '&') {
                    start = my pos;
                    my pos += 2;
                    rx_error(me, my pos - 2, 2, 1519, operators_mixed);
                    if (cc(me, my pos) == '&') {
                        unexpected_char(me, my pos, ch);
                        my pos++;
                    }
                    operand = str_slice(me, start, my pos);
                    continue;
                }
                break;
        }
        if (is_class_content_exit(me, cc(me, my pos))) break;
        start = my pos;
        if (at_double_operator(me)) {
            rx_error(me, my pos, 2, 1519, operators_mixed);
            my pos += 2;
            operand = str_slice(me, start, my pos);
        } else {
            operand = scan_class_set_operand(me);
            if (is_character_complement && my may_contain_strings) rx_error(me, start, my pos - start, 1518, negated_strings);
            expression_may_contain_strings = expression_may_contain_strings || my may_contain_strings;
        }
    }
    my may_contain_strings = !is_character_complement && expression_may_contain_strings;
}

static bool table_has(csview const* table, isize count, csview name) {
    for (isize i = 0; i < count; i++) {
        if (csview_equals_sv(table[i], name)) return true;
    }
    return false;
}

static void suggest(Rx* me, i32 start, i32 len, csview name, csview const* candidates, isize count) {
    isize best = spelling_suggestion_ex(name, candidates, count, spelling_compare_strings, (void*)candidates);
    if (best >= 0) rx_error(me, start, len, 1369, "Did you mean '", candidates[best], "'?");
}

static csview const non_binary_properties[] = { // nonBinaryUnicodeProperties
    { "General_Category", 16 }, { "gc", 2 }, { "Script", 6 }, { "sc", 2 }, { "Script_Extensions", 17 }, { "scx", 3 },
};

// CharacterClassEscape ::= 'd' | 'D' | 's' | 'S' | 'w' | 'W' | [+UnicodeMode] ('P' | 'p') '{' UnicodePropertyValueExpression '}'
static bool scan_character_class_escape(Rx* me) {
    bool is_character_complement = false;
    i32 start = my pos - 1;
    i32 ch = cc(me, my pos);
    switch (ch) {
        case 'd': case 'D': case 's': case 'S': case 'w': case 'W':
            my pos++;
            return true;
        case 'P':
            is_character_complement = true;
            [[fallthrough]];
        case 'p':
            my pos++;
            if (cc(me, my pos) == '{') {
                my pos++;
                i32 name_start = my pos;
                while (is_word_character(cc(me, my pos))) my pos++;
                // Word characters are ASCII, so the UTF-8 text is the value
                csview name = span_sv(rx_span(me, name_start, my pos - name_start));
                if (cc(me, my pos) == '=') {
                    i32 property = -1; // 0 General_Category, 1 Script and Script_Extensions
                    for (i32 i = 0; i < (i32)c_arraylen(non_binary_properties); i++) {
                        if (csview_equals_sv(non_binary_properties[i], name)) property = i < 2 ? 0 : 1;
                    }
                    if (my pos == name_start) {
                        rx_error(me, my pos, 0, 1523, "Expected a Unicode property name.");
                    } else if (property < 0) {
                        rx_error(me, name_start, my pos - name_start, 1524, "Unknown Unicode property name.");
                        suggest(me, name_start, my pos - name_start, name, non_binary_properties, c_arraylen(non_binary_properties));
                    }
                    my pos++;
                    i32 value_start = my pos;
                    while (is_word_character(cc(me, my pos))) my pos++;
                    csview value = span_sv(rx_span(me, value_start, my pos - value_start));
                    csview const* values = property == 0 ? regex_general_category_values : regex_script_values;
                    isize values_count = property == 0 ? regex_general_category_values_count : regex_script_values_count;
                    if (my pos == value_start) {
                        rx_error(me, my pos, 0, 1525, "Expected a Unicode property value.");
                    } else if (property >= 0 && !table_has(values, values_count, value)) {
                        rx_error(me, value_start, my pos - value_start, 1526, "Unknown Unicode property value.");
                        suggest(me, value_start, my pos - value_start, value, values, values_count);
                    }
                } else {
                    if (my pos == name_start) {
                        rx_error(me, my pos, 0, 1527, "Expected a Unicode property name or value.");
                    } else if (table_has(regex_binary_properties_of_strings, regex_binary_properties_of_strings_count, name)) {
                        if (!my unicode_sets_mode) {
                            rx_error(me, name_start, my pos - name_start, 1528, "Any Unicode property that would possibly match "
                                "more than a single character is only available when the Unicode Sets (v) flag is set.");
                        } else if (is_character_complement) {
                            rx_error(me, name_start, my pos - name_start, 1518, negated_strings);
                        } else {
                            my may_contain_strings = true;
                        }
                    } else if (!table_has(regex_general_category_values, regex_general_category_values_count, name) &&
                        !table_has(regex_binary_properties, regex_binary_properties_count, name)
                    ) {
                        rx_error(me, name_start, my pos - name_start, 1529, "Unknown Unicode property name or value.");
                        isize count = regex_general_category_values_count + regex_binary_properties_count +
                            regex_binary_properties_of_strings_count;
                        csview* candidates = malloc(sizeof(csview) * (size_t)count);
                        panic_if(!candidates, "out of memory");
                        isize at = 0;
                        for (isize i = 0; i < regex_general_category_values_count; i++) candidates[at++] = regex_general_category_values[i];
                        for (isize i = 0; i < regex_binary_properties_count; i++) candidates[at++] = regex_binary_properties[i];
                        for (isize i = 0; i < regex_binary_properties_of_strings_count; i++) candidates[at++] = regex_binary_properties_of_strings[i];
                        suggest(me, name_start, my pos - name_start, name, candidates, count);
                        free(candidates);
                    }
                }
                scan_expected_char(me, '}');
                if (!my any_unicode_mode) {
                    rx_error(me, start, my pos - start, 1530, "Unicode property value expressions are only available when "
                        "the Unicode (u) flag or the Unicode Sets (v) flag is set.");
                }
            } else if (my any_unicode_mode_or_non_annex_b) {
                rx_error(me, my pos - 2, 2, 1531, "'\\", (char)ch,
                    "' must be followed by a Unicode property value expression enclosed in braces.");
            } else {
                my pos--;
                return false;
            }
            return true;
    }
    return false;
}

static char const* const unexpected_suffix = "'. Did you mean to escape it with backslash?";

// Alternative ::= Term*
static void scan_alternative(Rx* me, bool is_in_group) {
    bool is_previous_term_quantifiable = false;
    while (true) {
        i32 start = my pos;
        i32 ch = cc(me, my pos);
        switch (ch) {
            case RX_EOF:
                return;
            case '^': case '$':
                my pos++;
                is_previous_term_quantifiable = false;
                break;
            case '\\':
                my pos++;
                switch (cc(me, my pos)) {
                    case 'b': case 'B':
                        my pos++;
                        is_previous_term_quantifiable = false;
                        break;
                    default:
                        scan_atom_escape(me);
                        is_previous_term_quantifiable = true;
                        break;
                }
                break;
            case '(':
                my pos++;
                if (cc(me, my pos) == '?') {
                    my pos++;
                    switch (cc(me, my pos)) {
                        case '=': case '!':
                            my pos++;
                            // In Annex B, `(?=Disjunction)` and `(?!Disjunction)` are quantifiable
                            is_previous_term_quantifiable = !my any_unicode_mode_or_non_annex_b;
                            break;
                        case '<': {
                            i32 group_name_start = my pos;
                            my pos++;
                            switch (cc(me, my pos)) {
                                case '=': case '!':
                                    my pos++;
                                    is_previous_term_quantifiable = false;
                                    break;
                                default:
                                    scan_group_name(me, false);
                                    scan_expected_char(me, '>');
                                    if (my target < SCRIPT_TARGET_ES2018) {
                                        rx_error(me, group_name_start, my pos - group_name_start, 1503,
                                            "Named capturing groups are only available when targeting 'ES2018' or later.");
                                    }
                                    my number_of_capturing_groups++;
                                    is_previous_term_quantifiable = true;
                                    break;
                            }
                        }   break;
                        default: {
                            i32 modifiers_start = my pos;
                            u32 set_flags = scan_pattern_modifiers(me, 0);
                            if (cc(me, my pos) == '-') {
                                my pos++;
                                scan_pattern_modifiers(me, set_flags);
                                if (my pos == modifiers_start + 1) {
                                    rx_error(me, modifiers_start, my pos - modifiers_start, 1504,
                                        "Subpattern flags must be present when there is a minus sign.");
                                }
                            }
                            // Modifier characters were consumed: `(?flags:` rather than a plain `(?:` group
                            if (my pos != modifiers_start && my target < SCRIPT_TARGET_ES2025) {
                                rx_error(me, modifiers_start, my pos - modifiers_start, 18062,
                                    "Regular expression pattern modifiers are only available when targeting 'es2025' or later.");
                            }
                            scan_expected_char(me, ':');
                            is_previous_term_quantifiable = true;
                        }   break;
                    }
                } else {
                    my number_of_capturing_groups++;
                    is_previous_term_quantifiable = true;
                }
                scan_disjunction(me, true);
                scan_expected_char(me, ')');
                break;
            case '{': {
                my pos++;
                i32 digits_start = my pos;
                scan_digits(me);
                i32 min_end = my pos;
                bool has_min = min_end > digits_start;
                if (!my any_unicode_mode_or_non_annex_b && !has_min) {
                    is_previous_term_quantifiable = true;
                    break;
                }
                if (cc(me, my pos) == ',') {
                    my pos++;
                    i32 max_start = my pos;
                    scan_digits(me);
                    bool has_max = my pos > max_start;
                    if (!has_min) {
                        if (has_max || cc(me, my pos) == '}') {
                            rx_error(me, digits_start, 0, 1505, "Incomplete quantifier. Digit expected.");
                        } else {
                            rx_error(me, start, 1, 1508, "Unexpected '", (char)ch, unexpected_suffix);
                            is_previous_term_quantifiable = true;
                            break;
                        }
                    } else if (has_max && compare_digits(me, digits_start, min_end, max_start, my pos) > 0 &&
                        (my any_unicode_mode_or_non_annex_b || cc(me, my pos) == '}')
                    ) {
                        rx_error(me, digits_start, my pos - digits_start, 1506, "Numbers out of order in quantifier.");
                    }
                } else if (!has_min) {
                    if (my any_unicode_mode_or_non_annex_b) {
                        rx_error(me, start, 1, 1508, "Unexpected '", (char)ch, unexpected_suffix);
                    }
                    is_previous_term_quantifiable = true;
                    break;
                }
                if (cc(me, my pos) != '}') {
                    if (my any_unicode_mode_or_non_annex_b) {
                        rx_error(me, my pos, 0, 1005, "'}' expected.");
                        my pos--;
                    } else {
                        is_previous_term_quantifiable = true;
                        break;
                    }
                }
            }
                [[fallthrough]];
            case '*': case '+': case '?':
                my pos++;
                if (cc(me, my pos) == '?') my pos++; // Non-greedy
                if (!is_previous_term_quantifiable) {
                    rx_error(me, start, my pos - start, 1507, "There is nothing available for repetition.");
                }
                is_previous_term_quantifiable = false;
                break;
            case '.':
                my pos++;
                is_previous_term_quantifiable = true;
                break;
            case '[':
                my pos++;
                if (my unicode_sets_mode) scan_class_set_expression(me);
                else scan_class_ranges(me);
                scan_expected_char(me, ']');
                is_previous_term_quantifiable = true;
                break;
            case ')':
                if (is_in_group) return;
                [[fallthrough]];
            case ']': case '}':
                if (my any_unicode_mode_or_non_annex_b || ch == ')') {
                    rx_error(me, my pos, 1, 1508, "Unexpected '", (char)ch, unexpected_suffix);
                }
                my pos++;
                is_previous_term_quantifiable = true;
                break;
            case '/': case '|':
                return;
            default:
                scan_source_character(me);
                is_previous_term_quantifiable = true;
                break;
        }
    }
}

// Disjunction ::= Alternative ('|' Alternative)*
// The names any of the alternatives define are unioned and, in a group, bubbled up into the enclosing alternative's
// scope once the group closes, so `(?:(?<a>x))(?<a>z)` is a duplicate (tsgo's scanDisjunction)
static void scan_disjunction(Rx* me, bool is_in_group) {
    i32 first_name = my names_len;
    while (true) {
        scopes_push(me, ++my next_scope);
        scan_alternative(me, is_in_group);
        my scopes_len--;
        if (cc(me, my pos) != '|') break;
        my pos++;
    }
    if (is_in_group && my scopes_len) {
        for (i32 i = first_name; i < my names_len; i++) my names[i].scope = my scopes[my scopes_len - 1];
    }
}

static void scan_regular_expression_worker(Rx* me, u32 flags) {
    my unicode_sets_mode = flags & RF_UNICODE_SETS;
    my any_unicode_mode = flags & RF_ANY_UNICODE;
    my annex_b = true;
    my any_unicode_mode_or_non_annex_b = my any_unicode_mode || !my annex_b;
    scan_disjunction(me, false);

    for (i32 i = 0; i < my refs_len; i++) {
        RxRef const* reference = &my refs[i];
        csview name = pool_sv(me, reference->off, reference->len);
        if (group_specifiers_have(me, name)) continue;
        rx_error(me, reference->pos, reference->end - reference->pos, 1532,
            "There is no capturing group named '", name, "' in this regular expression.");
        if (my names_len) {
            csview* candidates = malloc(sizeof(csview) * (size_t)my names_len);
            panic_if(!candidates, "out of memory");
            for (i32 j = 0; j < my names_len; j++) candidates[j] = pool_sv(me, my names[j].off, my names[j].len);
            suggest(me, reference->pos, reference->end - reference->pos, name, candidates, my names_len);
            free(candidates);
        }
    }
    for (i32 i = 0; i < my decimals_len; i++) {
        RxRef const* escape = &my decimals[i];
        // Although a DecimalEscape greater than the number of capturing groups is an octal or identity escape in Annex
        // B, it's most likely a mistake
        if (escape->value > my number_of_capturing_groups) {
            if (my number_of_capturing_groups) {
                rx_error(me, escape->pos, escape->end - escape->pos, 1533, "This backreference refers to a group that does "
                    "not exist. There are only ", my number_of_capturing_groups, " capturing groups in this regular expression.");
            } else {
                rx_error(me, escape->pos, escape->end - escape->pos, 1534, "This backreference refers to a group that does "
                    "not exist. There are no capturing groups in this regular expression.");
            }
        }
    }
}

static bool is_line_break(i32 ch) {
    return ch == '\n' || ch == '\r' || ch == 0x2028 || ch == 0x2029;
}

// reScanSlashToken(reportErrors) on a terminated literal: the flags, then the body
static void rescan_slash_token(Rx* me) {
    // Quickly get to the end of the regex to know the flags
    i32 pos = 1;
    bool in_escape = false;
    bool in_character_class = false;
    while (true) {
        i32 ch = pos < my n ? my text[pos] : RX_EOF;
        if (ch == RX_EOF || is_line_break(ch)) return; // Unterminated: the parser reported it
        if (in_escape) {
            in_escape = false;
        } else if (ch == '/' && !in_character_class) {
            break;
        } else if (ch == '[') {
            in_character_class = true;
        } else if (ch == '\\') {
            in_escape = true;
        } else if (ch == ']') {
            in_character_class = false;
        } else if (!in_character_class && ch == '(' && cu(me, pos + 1) == '?' && cu(me, pos + 2) == '<' &&
            cu(me, pos + 3) != '=' && cu(me, pos + 3) != '!'
        ) {
            my named_capture_groups = true;
        }
        pos++;
    }
    i32 end_of_body = pos;
    my pos = pos + 1;
    my end = my n;
    u32 flags = 0;
    while (true) {
        i32 ch = cpc(me, my pos);
        if (ch == RX_EOF || !is_identifier_part(ch)) break;
        i32 size = char_size(ch);
        u32 flag = flag_of(ch);
        if (!flag) {
            rx_error(me, my pos, size, 1499, "Unknown regular expression flag.");
        } else if (flags & flag) {
            rx_error(me, my pos, size, 1500, "Duplicate regular expression flag.");
        } else if (((flags | flag) & RF_ANY_UNICODE) == RF_ANY_UNICODE) {
            rx_error(me, my pos, size, 1502, "The Unicode (u) flag and the Unicode Sets (v) flag cannot be set simultaneously.");
        } else {
            flags |= flag;
            check_flag_availability(me, flag, size);
        }
        my pos += size;
    }
    my pos = 1;
    my end = end_of_body;
    scan_regular_expression_worker(me, flags);
}

// (Not inlined: expr_typecheck() recurses as deep as expressions nest, see binderBinaryExpressionStress)
__attribute__((noinline)) void regex_check_literal(Span literal, ErrorCtx* errs) {
    if (!errs || !span_source(literal) || span_source(literal)->has_parse_errors || literal.size < 2) return;
    // To UTF-16, keeping each unit's byte offset
    enum { INLINE = 256 };
    uint16_t text_inline[INLINE + 2];
    i32 offs_inline[INLINE + 1];
    isize size = literal.size;
    bool heap = size > INLINE; // A UTF-8 byte is at most one UTF-16 unit
    uint16_t* text = heap ? malloc(sizeof(uint16_t) * (size_t)(size + 2)) : text_inline;
    i32* offs = heap ? malloc(sizeof(i32) * (size_t)(size + 1)) : offs_inline;
    panic_if(!text || !offs, "out of memory");
    unsigned char const* bytes = (unsigned char const*)literal.buf;
    i32 n = 0;
    for (isize i = 0; i < size;) {
        u32 b = bytes[i];
        i32 len = b < 0x80 ? 1 : utf8_rune_size((char const*)bytes + i);
        if (i + len > size) len = 1;
        u32 cp = len == 1 ? b : b & (0xFF >> (len + 1));
        for (i32 k = 1; k < len; k++) cp = (cp << 6) | (bytes[i + k] & 0x3F);
        if (len == 1 && b >= 0x80) cp = 0xFFFD; // (a byte that isn't UTF-8: Go's RuneError)
        if (cp >= 0x10000) {
            cp -= 0x10000;
            offs[n] = (i32)i;
            text[n++] = (uint16_t)(0xD800 + (cp >> 10));
            offs[n] = (i32)i; // (tsgo's scanSourceCharacter: the high surrogate doesn't advance, the low one starts there too)
            text[n++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
        } else {
            offs[n] = (i32)i;
            text[n++] = (uint16_t)cp;
        }
        i += len;
    }
    offs[n] = (i32)size;
    text[n] = text[n + 1] = 0;

    Rx rx = {
        .text = text, .offs = offs, .n = n, .end = n, .literal = literal, .errs = errs,
        .target = compiler_options()->target,
    };
    rx.scopes = rx.scopes_inline;
    rx.scopes_cap = c_arraylen(rx.scopes_inline);
    rescan_slash_token(&rx);

    free(rx.pool);
    free(rx.names);
    free(rx.refs);
    free(rx.decimals);
    if (rx.scopes != rx.scopes_inline) free(rx.scopes);
    if (heap) {
        free(text);
        free(offs);
    }
}
