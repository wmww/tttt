#pragma once

#include "serializer.h"
#include "deserializer.h"
#include "value.h"

typedef struct JsonSerializer JsonSerializer;
typedef struct JsonDeserializer JsonDeserializer;

struct JsonSerializer {
    SeriSerializer ser;
    SeriTokenState state;
    cstr* target;
};

// Target is appended to, must be initialized
JsonSerializer json_serializer_make(cstr* target);
static inline void json_serializer_drop(JsonSerializer* me) {
    seri_token_state_drop(&me->state);
}

struct JsonDeserializer {
    SeriDeserializer de;
    csview remaining;
};

JsonDeserializer json_deserializer_make(csview json);
static inline void json_deserializer_drop(JsonDeserializer* me) {
    seri_deserializer_drop(&me->de);
}

static_inline_failable cstr_append_json(SeriValue const* value, cstr* target) {
    JsonSerializer ser = json_serializer_make(target);
    FailableResult result = serialize_seri_value(&ser.ser, value);
    json_serializer_drop(&ser);
    return result;
}

static_inline_failable into_json(SeriValue value, returns cstr) {
    cstr serialized = cstr_init();
    FailableResult result = cstr_append_json(&value, &serialized);
    seri_value_drop(&value);
    if (result.error) {
        cstr_drop(&serialized);
    } else {
        *_OUTPUT_PARAM = serialized;
    }
    return result;
}

static_inline_failable parse_json(csview json, returns SeriValue) {
    JsonDeserializer de = json_deserializer_make(json);
    FailableResult result = deserialize_seri_value(&de.de, _OUTPUT_PARAM);
    json_deserializer_drop(&de);
    return result;
}
