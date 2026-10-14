#include "json.h"

typedef struct JsonParser {
    const char* text;
    const char* at;
    const char* end;
    int depth;
    // Something tsc's parseJsonText or convertToJson would report was recovered from (json_parse_strict())
    bool dirty;
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

static unsigned parse_hex4(JsonParser* me) {
    unsigned code = 0;
    for (int i = 0; i < 4 && my at < my end; i++, my at++) {
        char h = *my at;
        unsigned digit = h >= '0' && h <= '9' ? (unsigned)(h - '0') :
            h >= 'a' && h <= 'f' ? (unsigned)(h - 'a' + 10) :
            h >= 'A' && h <= 'F' ? (unsigned)(h - 'A' + 10) : 0;
        code = code * 16 + digit;
    }
    return code;
}

// An unterminated string (at a line break or the end) is what it has so far, like tsc's scanner's
static void parse_string(JsonParser* me, cstr* out) {
    char quote = *my at; // tsc's config parser takes single quotes too
    if (quote != '"') my dirty = true;
    my at++;
    *out = cstr_null;
    while (my at < my end && *my at != quote) {
        if (*my at == '\n' || *my at == '\r') {
            my dirty = true;
            return;
        }
        if (*my at != '\\') {
            const char* run = my at;
            while (my at < my end && *my at != quote && *my at != '\\' && *my at != '\n' && *my at != '\r') my at++;
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
                unsigned code = parse_hex4(me);
                // (A surrogate pair is one character: `\ud83d\ude00`)
                if (code >= 0xD800 && code < 0xDC00 && my end - my at >= 6 && my at[0] == '\\' && my at[1] == 'u') {
                    char const* save = my at;
                    my at += 2;
                    unsigned low = parse_hex4(me);
                    if (low >= 0xDC00 && low < 0xE000) code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    else my at = save;
                }
                append_utf8(out, code);
            }   break;
            default: cstr_append_n(out, &c, 1); break;
        }
    }
    if (my at < my end) my at++;
    else my dirty = true;
}

static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
        (unsigned char)c >= 0x80;
}

static bool parse_value(JsonParser* me, JsonValue* out);

// Recovers from what isn't JSON the way tsc's parseJsonText does, as far as what is read goes (the config's syntax
// errors come from parsing it as a TypeScript JSON file): missing commas and colons, unquoted keys, an unterminated
// container (ends with the text, or at another container's closing bracket), a value that isn't one (dropped)
static bool parse_container(JsonParser* me, JsonValue* out, bool is_object) {
    isize cap = 0;
    my at++;
    bool after_element = false; // (An element not followed by a comma yet)
    while (true) {
        u32 full_start = (u32)(my at - my text); // (Before the trivia: where tsc's node for an element starts)
        skip_trivia(me);
        if (my at >= my end) {
            my dirty = true;
            return true;
        }
        if (*my at == (is_object ? '}' : ']')) {
            my at++;
            return true;
        }
        if (*my at == (is_object ? ']' : '}')) { // (The enclosing container's)
            my dirty = true;
            return true;
        }
        if (*my at == ',') { // Also forgives `[,]`, which nobody writes
            if (!after_element) {
                my dirty = true;
                // An omitted array element is one that isn't a value
                if (!is_object) {
                    if (out->count == cap) {
                        cap = cap ? cap * 2 : 4;
                        out->items = realloc(out->items, sizeof(JsonValue) * (size_t)cap);
                    }
                    out->items[out->count++] = (JsonValue){ .tag = JSON_INVALID, .start = full_start, .end = full_start };
                }
            }
            after_element = false;
            my at++;
            continue;
        }
        if (after_element) my dirty = true; // (A missing comma)
        after_element = true;
        if (out->count == cap) {
            cap = cap ? cap * 2 : 4;
            if (is_object) {
                out->members = realloc(out->members, sizeof(JsonMember) * (size_t)cap);
            } else {
                out->items = realloc(out->items, sizeof(JsonValue) * (size_t)cap);
            }
        }
        const char* before = my at;
        if (is_object) {
            JsonMember* member = &out->members[out->count];
            *member = (JsonMember){ .key_start = (u32)(my at - my text) };
            if (*my at == '"' || *my at == '\'') {
                parse_string(me, &member->key);
            } else if (is_word_char(*my at)) {
                const char* word = my at;
                while (my at < my end && is_word_char(*my at)) my at++;
                member->key = cstr_from_sv((csview){ word, my at - word });
                my dirty = true; // (Not a double-quoted string)
            } else {
                my at++; // (Not a property)
                my dirty = true;
                continue;
            }
            member->key_end = (u32)(my at - my text);
            out->count++; // From here on json_drop() owns it
            skip_trivia(me);
            if (my at < my end && *my at == ':') my at++;
            else my dirty = true;
            u32 value_full_start = (u32)(my at - my text);
            if (!parse_value(me, &member->value)) {
                my dirty = true;
                // A property whose value isn't one is kept with an invalid value (tsc reports it converting the config)
                // (Nothing but trivia: a missing value, empty where tsc's missing node is)
                u32 end = member->value.end > member->value.start ? member->value.end : value_full_start;
                json_drop(&member->value);
                member->value = (JsonValue){ .tag = JSON_INVALID, .start = value_full_start, .end = end };
                if (my at == before) my at++;
            }
        } else {
            out->items[out->count] = (JsonValue){0};
            out->count++;
            JsonValue* item = &out->items[out->count - 1];
            if (!parse_value(me, item)) {
                my dirty = true;
                if (item->end > item->start) {
                    // A word: an element that isn't a value (an invalid one, like tsc's)
                    u32 end = item->end;
                    json_drop(item);
                    *item = (JsonValue){ .tag = JSON_INVALID, .start = full_start, .end = end };
                } else {
                    json_drop(&out->items[--out->count]);
                }
                if (my at == before) my at++;
            }
        }
    }
}

static bool parse_literal(JsonParser* me, const char* word) {
    isize len = (isize)strlen(word);
    if (my end - my at < len || memcmp(my at, word, (size_t)len) || (my end - my at > len && is_word_char(my at[len]))) {
        // Not a value: a word is skipped whole
        while (my at < my end && is_word_char(*my at)) my at++;
        return false;
    }
    my at += len;
    return true;
}

static bool parse_value_at(JsonParser* me, JsonValue* out);

static bool parse_value(JsonParser* me, JsonValue* out) {
    *out = (JsonValue){0};
    skip_trivia(me);
    u32 start = (u32)(my at - my text);
    bool parsed = parse_value_at(me, out);
    out->start = start;
    out->end = (u32)(my at - my text);
    return parsed;
}

static bool parse_value_at(JsonParser* me, JsonValue* out) {
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
            parse_string(me, &out->string);
            return true;
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
            if (!len) {
                // Not a value: a word is skipped whole
                while (my at < my end && is_word_char(*my at)) my at++;
                return false;
            }
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
    JsonParser parser = { .text = text.buf, .at = text.buf, .end = text.buf + text.size };
    if (parse_value(&parser, out)) return true;
    json_drop(out);
    return false;
}

bool json_parse_strict(csview text, JsonValue* out) {
    JsonParser parser = { .text = text.buf, .at = text.buf, .end = text.buf + text.size };
    bool parsed = parse_value(&parser, out);
    skip_trivia(&parser);
    if (parsed && !parser.dirty && parser.at == parser.end) return true;
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

void json_append_string(cstr* out, csview text) {
    cstr_append_lit(out, "\"");
    for (isize i = 0; i < text.size; i++) {
        unsigned char c = (unsigned char)text.buf[i];
        if (c == '"') cstr_append_lit(out, "\\\"");
        else if (c == '\\') cstr_append_lit(out, "\\\\");
        else if (c == '\n') cstr_append_lit(out, "\\n");
        else if (c == '\r') cstr_append_lit(out, "\\r");
        else if (c == '\t') cstr_append_lit(out, "\\t");
        else if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            cstr_append(out, buf);
        } else {
            cstr_append_n(out, (char const*)&text.buf[i], 1);
        }
    }
    cstr_append_lit(out, "\"");
}

void json_write(cstr* out, JsonValue const* value) {
    switch (value->tag) {
    case JSON_BOOL:
        cstr_append(out, value->boolean ? "true" : "false");
        break;
    case JSON_NUMBER: {
        char buf[32];
        if (value->number == (double)(i64)value->number) snprintf(buf, sizeof(buf), "%lld", (long long)value->number);
        else snprintf(buf, sizeof(buf), "%.17g", value->number);
        cstr_append(out, buf);
        break;
    }
    case JSON_STRING:
        json_append_string(out, cstr_sv(&value->string));
        break;
    case JSON_ARRAY:
        cstr_append_lit(out, "[");
        for (isize i = 0; i < value->count; i++) {
            if (i) cstr_append_lit(out, ",");
            json_write(out, &value->items[i]);
        }
        cstr_append_lit(out, "]");
        break;
    case JSON_OBJECT:
        cstr_append_lit(out, "{");
        for (isize i = 0; i < value->count; i++) {
            if (i) cstr_append_lit(out, ",");
            json_append_string(out, cstr_sv(&value->members[i].key));
            cstr_append_lit(out, ":");
            json_write(out, &value->members[i].value);
        }
        cstr_append_lit(out, "}");
        break;
    default:
        cstr_append_lit(out, "null");
        break;
    }
}
