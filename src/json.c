#include "json.h"

typedef struct JsonParser {
    const char* at;
    const char* end;
    int depth;
} JsonParser;

static void skip_trivia(JsonParser* me) {
    while (my at < my end) {
        char c = *my at;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            my at++;
        } else if (c == '/' && my at + 1 < my end && my at[1] == '/') {
            while (my at < my end && *my at != '\n') my at++;
        } else if (c == '/' && my at + 1 < my end && my at[1] == '*') {
            my at += 2;
            while (my at + 1 < my end && !(my at[0] == '*' && my at[1] == '/')) my at++;
            my at = my at + 2 <= my end ? my at + 2 : my end;
        } else if ((unsigned char)c == 0xEF && my at + 2 < my end && (unsigned char)my at[1] == 0xBB) {
            my at += 3; // BOM
        } else {
            break;
        }
    }
}

static void append_utf8(cstr* out, unsigned code) {
    char buf[4];
    isize len;
    if (code < 0x80) {
        buf[0] = (char)code;
        len = 1;
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
        len = 2;
    } else if (code < 0x10000) {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
        len = 3;
    } else {
        buf[0] = (char)(0xF0 | (code >> 18));
        buf[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (code & 0x3F));
        len = 4;
    }
    cstr_append_n(out, buf, len);
}

static bool parse_string(JsonParser* me, cstr* out) {
    char quote = *my at; // tsc's config parser takes single quotes too
    my at++;
    *out = cstr_null;
    while (my at < my end && *my at != quote) {
        if (*my at == '\n') break;
        if (*my at != '\\') {
            const char* run = my at;
            while (my at < my end && *my at != quote && *my at != '\\' && *my at != '\n') my at++;
            cstr_append_n(out, run, my at - run);
            continue;
        }
        my at++;
        if (my at >= my end) break;
        char c = *my at++;
        switch (c) {
            case 'n': cstr_append_lit(out, "\n"); break;
            case 't': cstr_append_lit(out, "\t"); break;
            case 'r': cstr_append_lit(out, "\r"); break;
            case 'b': cstr_append_lit(out, "\b"); break;
            case 'f': cstr_append_lit(out, "\f"); break;
            case 'u': {
                unsigned code = 0;
                for (int i = 0; i < 4 && my at < my end; i++, my at++) {
                    char h = *my at;
                    unsigned digit = h >= '0' && h <= '9' ? (unsigned)(h - '0') :
                        h >= 'a' && h <= 'f' ? (unsigned)(h - 'a' + 10) :
                        h >= 'A' && h <= 'F' ? (unsigned)(h - 'A' + 10) : 0;
                    code = code * 16 + digit;
                }
                append_utf8(out, code);
            }   break;
            default: cstr_append_n(out, &c, 1); break;
        }
    }
    if (my at >= my end || *my at != quote) {
        cstr_drop(out);
        *out = cstr_null;
        return false;
    }
    my at++;
    return true;
}

static bool parse_value(JsonParser* me, JsonValue* out);

static bool parse_container(JsonParser* me, JsonValue* out, bool is_object) {
    isize cap = 0;
    my at++;
    while (true) {
        skip_trivia(me);
        if (my at >= my end) return false;
        if (*my at == (is_object ? '}' : ']')) {
            my at++;
            return true;
        }
        if (*my at == ',') { // Also forgives `[,]`, which nobody writes
            my at++;
            continue;
        }
        if (out->count == cap) {
            cap = cap ? cap * 2 : 4;
            if (is_object) {
                out->members = realloc(out->members, sizeof(JsonMember) * (size_t)cap);
            } else {
                out->items = realloc(out->items, sizeof(JsonValue) * (size_t)cap);
            }
        }
        if (is_object) {
            if (*my at != '"' && *my at != '\'') return false;
            JsonMember* member = &out->members[out->count];
            *member = (JsonMember){0};
            if (!parse_string(me, &member->key)) return false;
            out->count++; // From here on json_drop() owns it
            skip_trivia(me);
            if (my at >= my end || *my at != ':') return false;
            my at++;
            if (!parse_value(me, &member->value)) return false;
        } else {
            out->items[out->count] = (JsonValue){0};
            out->count++;
            if (!parse_value(me, &out->items[out->count - 1])) return false;
        }
    }
}

static bool parse_literal(JsonParser* me, const char* word) {
    isize len = (isize)strlen(word);
    if (my end - my at < len || memcmp(my at, word, (size_t)len)) return false;
    my at += len;
    return true;
}

static bool parse_value(JsonParser* me, JsonValue* out) {
    *out = (JsonValue){0};
    skip_trivia(me);
    if (my at >= my end || my depth > 200) return false;
    switch (*my at) {
        case '{':
        case '[': {
            bool is_object = *my at == '{';
            out->tag = is_object ? JSON_OBJECT : JSON_ARRAY;
            my depth++;
            bool parsed = parse_container(me, out, is_object);
            my depth--;
            return parsed;
        }
        case '"':
        case '\'':
            out->tag = JSON_STRING;
            return parse_string(me, &out->string);
        case 't':
            out->tag = JSON_BOOL;
            out->boolean = true;
            return parse_literal(me, "true");
        case 'f':
            out->tag = JSON_BOOL;
            return parse_literal(me, "false");
        case 'n':
            return parse_literal(me, "null");
        default: {
            char buf[64];
            isize len = 0;
            while (my at + len < my end && len < 63 && (
                (my at[len] >= '0' && my at[len] <= '9') || my at[len] == '-' || my at[len] == '+' ||
                my at[len] == '.' || my at[len] == 'e' || my at[len] == 'E'
            )) len++;
            if (!len) return false;
            memcpy(buf, my at, (size_t)len);
            buf[len] = 0;
            out->tag = JSON_NUMBER;
            out->number = strtod(buf, nullptr);
            my at += len;
            return true;
        }
    }
}

bool json_parse(csview text, JsonValue* out) {
    JsonParser parser = { .at = text.buf, .end = text.buf + text.size };
    if (parse_value(&parser, out)) return true;
    json_drop(out);
    return false;
}

void json_drop(JsonValue* me) {
    cstr_drop(&my string);
    if (my tag == JSON_ARRAY) {
        for (isize i = 0; i < my count; i++) json_drop(&my items[i]);
        free(my items);
    } else if (my tag == JSON_OBJECT) {
        for (isize i = 0; i < my count; i++) {
            cstr_drop(&my members[i].key);
            json_drop(&my members[i].value);
        }
        free(my members);
    }
    *me = (JsonValue){0};
}

JsonValue const* json_get(JsonValue const* me, const char* key) {
    if (!me || my tag != JSON_OBJECT) return nullptr;
    for (isize i = my count - 1; i >= 0; i--) {
        if (cstr_equals(&my members[i].key, key)) return &my members[i].value;
    }
    return nullptr;
}

JsonValue const* json_get_tagged(JsonValue const* me, const char* key, JsonTag tag) {
    JsonValue const* found = json_get(me, key);
    return found && found->tag == tag ? found : nullptr;
}

csview json_get_string(JsonValue const* me, const char* key) {
    JsonValue const* found = json_get_tagged(me, key, JSON_STRING);
    return found ? cstr_sv(&found->string) : c_sv("");
}
