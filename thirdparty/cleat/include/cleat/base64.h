#pragma once

#include <stc/cstr.h>
#include <stc/csview.h>
#include "failable_minimal.h"

typedef struct Base64Params {
    // 65 characters long (64 characters in alphabet + padding). May be any values 0-127 but should obviously be unique
    const char* encode_alphabet;
    // 128 characters long. -1 for unused, 0-63 for a character and 64 for padding
    const signed char* decode_alphabet;
    bool padding;
} Base64Params;

extern const Base64Params base64_standard_params;
extern const Base64Params base64_url_params;

extern ErrorVt const Base64DecodeErrorVt;
typedef struct Base64DecodeError Base64DecodeError;

void base64_encode_append(Base64Params params, csview data, cstr* target);
failable base64_decode_append(Base64Params params, csview base64, cstr* target);
