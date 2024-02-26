#pragma once

#include "prelude.h"

typedef struct Type Type;

typedef struct Value {
    enum {
        VALUE_NULL,
        VALUE_UNDEFINED,
        VALUE_BOOLEAN,
        VALUE_NUMBER,
        VALUE_STRING,
    } tag;
    union {
        bool boolean;
        double number;
        cstr string;
    } data;
} Value;

static inline Value Value_null_new() { return (Value){ .tag = VALUE_NULL }; }
static inline Value Value_undefined_new() { return (Value){ .tag = VALUE_UNDEFINED }; }
static inline Value Value_boolean_new(bool value) { return (Value){ .tag = VALUE_BOOLEAN, .data.boolean = value }; }
static inline Value Value_number_new(double value) { return (Value){ .tag = VALUE_NUMBER, .data.number = value }; }
static inline Value Value_string_new(cstr value) { return (Value){ .tag = VALUE_NUMBER, .data.string = value }; }

void Value_drop(Value* self);
Value Value_clone(Value self);
cstr Value_to_cstr(Value const* self, int indent);
bool Value_strict_eq(Value const* a, Value const* b);
