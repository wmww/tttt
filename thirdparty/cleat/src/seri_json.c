#include "seri/json.h"
#include "cleat.h"
#include "math.h"

#define JSON_VALID_STRING_TAGS ((1 << SERI_TOKEN_CSVIEW) | (1 << SERI_TOKEN_CSTR))

static_failable json_serialize_string(cstr* target, csview sv) {
    cstr_reserve(target, sv.size + 2);
    cstr_append_lit(target, "\"");
    for (i64 i = 0; i < sv.size; i++) {
        switch (sv.buf[i]) {
            case '"': cstr_append_lit(target, "\\\""); break;
            case '\\': cstr_append_lit(target, "\\\\"); break;
            case '\n': cstr_append_lit(target, "\\n"); break;
            case '\t': cstr_append_lit(target, "\\t"); break;
            case '\r': cstr_append_lit(target, "\\r"); break;
            default: cstr_append_sv(target, c_sv(&sv.buf[i], 1)); break;
        }
    }
    cstr_append_lit(target, "\"");
    return ok;
}

static_failable json_serialize_token(SeriSerializer* ser, SeriToken t) {
    auto me = c_container_of(ser, JsonSerializer, ser);

    if (my state.has_values && t.tag != SERI_TOKEN_END_CONTAINER && t.tag != SERI_TOKEN_EOF) {
        if (my state.container == SERI_TOKEN_MAP && my state.odd_values) {
            cstr_append_lit(my target, ": ");
        } else {
            cstr_append_lit(my target, ", ");
        }
    }

    switch (t.tag) {
        case SERI_TOKEN_NULL: cstr_append_lit(my target, "null"); goto success;
        case SERI_TOKEN_BOOL: cstr_append_sv(my target, t.b ? c_sv("true") : c_sv("false")); goto success;
        case SERI_TOKEN_INT: cstr_append_fmt(my target,"%lld", (long long)t.i); goto success;
        case SERI_TOKEN_UINT: cstr_append_fmt(my target,"%llu", (unsigned long long)t.u); goto success;

        case SERI_TOKEN_FLOAT:
            if (isfinite(t.f)) {
                cstr_append_fmt(my target,"%g", t.f); goto success;
            } else {
                return err_fmt("invalid JSON float: %g", t.f);
            }

        case SERI_TOKEN_CSVIEW: {
            FailableResult result = json_serialize_string(my target, t.sv);
            if (result.error) return result;
            else goto success;
        }

        case SERI_TOKEN_CSTR: {
            FailableResult result = json_serialize_string(my target, cstr_sv(&t.s));
            cstr_drop(&t.s);
            if (result.error) return result;
            else goto success;
        }

        case SERI_TOKEN_VARIANT:
        case SERI_TOKEN_VEC: cstr_append_lit(my target, "["); goto success;
        case SERI_TOKEN_MAP: cstr_append_lit(my target, "{"); goto success;
        case SERI_TOKEN_SOME: goto success;

        case SERI_TOKEN_END_CONTAINER: {
            switch (my state.container) {
                case SERI_TOKEN_VARIANT:
                case SERI_TOKEN_VEC: cstr_append_lit(my target, "]"); break;
                case SERI_TOKEN_MAP: cstr_append_lit(my target, "}"); break;
                case SERI_TOKEN_SOME: break;
                default: impossible;
            }
            goto success;
        }

        case SERI_TOKEN_EOF: goto success;
    }
    impossible;

    success:
    return seri_token_state_next(&my state, t.tag);
}

JsonSerializer json_serializer_make(cstr* target) {
    return (JsonSerializer){
        .ser.serialize = json_serialize_token,
        .state.valid_key_tags = JSON_VALID_STRING_TAGS,
        .target = target,
    };
}

static_inline_failable parse_json_number(csview token, returns SeriToken) {
    SeriToken ret;
    if (seri_string_to_number(token, false, false, &ret) == csview_end_ptr(token)) {
        return ok(ret);
    } else {
        return err_fmt("invalid JSON number '%.*s'", c_SV(token));
    }
}

static_inline_failable parse_json_string(csview* remaining, returns SeriToken) {
    const char* start = remaining->buf + 1; // Skip leading "
    const char* p = start;
    const char* end = csview_end_ptr(*remaining);
    i64 surrogate_start = 0;
    bool using_str = false;
    cstr str = cstr_null;
    while (p < end) {
        switch (*p) {
            case '"':
                *remaining = csview_from_range(p + 1, end);
                if (using_str) {
                    cstr_append_sv(&str, csview_from_range(start, p));
                    return ok(seri_token_make(SERI_TOKEN_CSTR, .s = str));
                } else {
                    return ok(seri_token_make(SERI_TOKEN_CSVIEW, .sv = csview_from_range(start, p)));
                }

            case '\\':
                const char* new_p = p + 1;
                if (p + 1 < end) {
                    char c[4];
                    int count = 1;
                    switch (*(p + 1)) {
                        case '"': c[0] = '"'; break;
                        case '\\':c[0] = '\\'; break;
                        case '/': c[0] = '/'; break;
                        case 'b': c[0] = '\b'; break;
                        case 'f': c[0] = '\f'; break;
                        case 'n': c[0] = '\n'; break;
                        case 'r': c[0] = '\r'; break;
                        case 't': c[0] = '\t'; break;
                        case 'u':
                            if (p + 5 < end) {
                                new_p = p + 5;
                                u64 codepoint;
                                if (!seri_hex_digits_to_int(csview_from_n(p + 2, 4), &codepoint)) {
                                    if (using_str) cstr_drop(&str);
                                    return err_fmt("invalid hex in JSON string escape code");
                                }
                                if (!surrogate_start && codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                                    surrogate_start = codepoint;
                                    count = 0;
                                } else {
                                    if (surrogate_start) {
                                        codepoint = 0x10000 + ((surrogate_start - 0xD800) << 10) + (codepoint - 0xDC00);
                                        surrogate_start = 0;
                                    }
                                    count = cutf8_encode(c, codepoint);
                                }
                            } else {
                                goto unterminated;
                            }
                            break;
                        default:
                            if (using_str) cstr_drop(&str);
                            return err_fmt("invalid JSON escape character '%c'", *(p + 1));
                    }
                    using_str = true;
                    cstr_append_sv(&str, csview_from_range(start, p));
                    cstr_append_sv(&str, csview_from_n(c, count));
                    start = new_p + 1;
                    p = new_p;
                } else {
                    goto unterminated;
                }
        }
        p++;
    }
    unterminated:
    if (using_str) cstr_drop(&str);
    return err_fmt("unterminated JSON string");
}

// Checks if the pointed-at character matches the regex [a-zA-Z0-9\.\+\-]
// To regenerate run cleat/src/gen_chartest.py is_num_or_sym 'a-zA-Z0-9\.\+\-'
static inline bool is_num_or_sym(char const* char_ptr) {
    static const u64 magic[] = {0x3ff680000000000, 0x7fffffe07fffffe, 0x0, 0x0};
    unsigned char c = *(unsigned char*)char_ptr;
    return (magic[(c >> 6) & 0x3] >> (c & 0x3f)) & 0x1;
}

static_failable json_deserialize_token(SeriDeserializer* de, returns SeriToken) {
    auto me = c_container_of(de, JsonDeserializer, de);
    bool has_sep = false;
    char sep_char =
        (!my de.state.has_values || my de.state.container == SERI_TOKEN_NULL) ? '\0' :
        (my de.state.container == SERI_TOKEN_MAP && my de.state.odd_values) ? ':' :
        ',';
    trim_whitespace:
    if (my remaining.size) {
        switch (*my remaining.buf) {
            case ' ': case '\t': case '\n': case '\r':
                my remaining.size--, my remaining.buf++;
                goto trim_whitespace;

            case ',': case ':':
                if (!has_sep && *my remaining.buf == sep_char) {
                    has_sep = true;
                    my remaining.size--, my remaining.buf++;
                    goto trim_whitespace;
                }
        }
    }
    if (!my remaining.size) return ok(seri_token_make(SERI_TOKEN_EOF));
    csview token_sv = csview_from_n(my remaining.buf, 1);
    if (sep_char && !has_sep && *token_sv.buf != ']' && *token_sv.buf != '}') {
        return err_fmt("invalid JSON, expected '%c'", sep_char);
    }
    if (is_num_or_sym(token_sv.buf)) {
        while (token_sv.size < my remaining.size && is_num_or_sym(csview_end_ptr(token_sv))) token_sv.size++;
        my remaining.size -= token_sv.size, my remaining.buf += token_sv.size;
        if (*token_sv.buf <= '9') return parse_json_number(token_sv, _OUTPUT_PARAM);
        else if (csview_equals(token_sv, "null")) return ok(seri_token_make(SERI_TOKEN_NULL));
        else if (csview_equals(token_sv, "true")) return ok(seri_token_make(SERI_TOKEN_BOOL, .b = true));
        else if (csview_equals(token_sv, "false")) return ok(seri_token_make(SERI_TOKEN_BOOL, .b = false));
        else goto invalid_token;
    } else if (*token_sv.buf == '"') {
        return parse_json_string(&my remaining, _OUTPUT_PARAM);
    } else {
        my remaining.size--, my remaining.buf++;
        switch (*token_sv.buf) {
            case '[': return ok(seri_token_make(SERI_TOKEN_VEC));
            case '{': return ok(seri_token_make(SERI_TOKEN_MAP));
            case ']': if (de->state.container != SERI_TOKEN_VEC || has_sep) goto invalid_token;
                      return ok(seri_token_make(SERI_TOKEN_END_CONTAINER));
            case '}': if (de->state.container != SERI_TOKEN_MAP || has_sep) goto invalid_token;
                      return ok(seri_token_make(SERI_TOKEN_END_CONTAINER));
            default:  goto invalid_token;
        }
    }

    invalid_token:
    return err_fmt("invalid JSON token '%.*s'", c_SV(token_sv));
}

JsonDeserializer json_deserializer_make(csview json) {
    return (JsonDeserializer){
        .de.deserialize = json_deserialize_token,
        .de.state.valid_key_tags = JSON_VALID_STRING_TAGS,
        .remaining = json,
    };
}
