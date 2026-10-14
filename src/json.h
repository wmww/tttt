#pragma once

#include "prelude.h"

// A small JSON reader for package.json and tsconfig.json: comments and trailing commas are fine (tsconfig), and the
// members of an object keep their order (the conditions of a package.json `exports` are tried in the order written).
// A value owns its children; strings are unescaped

typedef enum JsonTag {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
    // Something that isn't a value where one is expected: a missing one (`"a": }`), a word, an omitted array element
    // (`[, 1]`). tsc keeps the property and reports it when converting (TS1328, TS5024 for an option); at the end of
    // what comes before it, as tsc's node position (its leading trivia included), to the end of the word if any
    JSON_INVALID,
} JsonTag;

typedef struct JsonValue JsonValue;
typedef struct JsonMember JsonMember;

struct JsonValue {
    JsonTag tag;
    bool boolean;
    double number;
    cstr string;
    JsonValue* items; // JSON_ARRAY
    JsonMember* members; // JSON_OBJECT
    isize count; // Of items or members
    // Where it is in the text json_parse() read (byte offsets, end exclusive; a string with its quotes)
    u32 start;
    u32 end;
};

struct JsonMember {
    cstr key;
    u32 key_start; // Offsets of the key in the text, with its quotes
    u32 key_end;
    JsonValue value;
};

// False if text has no JSON value at all (*out is a null value then). What isn't JSON in it is recovered from like tsc's
// parseJsonText does (see parse_container())
bool json_parse(csview text, JsonValue* out);
// False (and *out a null value) if text has anything tsc's parseConfigFileTextToJson reports: its readJson, which
// takes a package.json with any syntax error (or a key that isn't a double-quoted string) as empty
bool json_parse_strict(csview text, JsonValue* out);
void json_drop(JsonValue* me);

// Null unless me is an object with that member. The last one wins, like JSON.parse
JsonValue const* json_get(JsonValue const* me, const char* key);
// Null unless me is an object with a member of that type
JsonValue const* json_get_tagged(JsonValue const* me, const char* key, JsonTag tag);
// The string member of that name, an empty view if there is none
csview json_get_string(JsonValue const* me, const char* key);

// Appends text as a JSON string literal (quoted, escaped)
void json_append_string(cstr* out, csview text);
// Appends value as compact JSON text (no whitespace; members in their order; integral numbers without a fraction)
void json_write(cstr* out, JsonValue const* value);
