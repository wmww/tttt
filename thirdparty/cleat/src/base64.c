#include "base64.h"
#include "display.h"
#include "failable.h"
#include "common_macros.h"

// python -c 'print(["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=".find(chr(i)) for i in range(128)])'
static const signed char base64_standard_decode_alphabet[] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, 64, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};
const Base64Params base64_standard_params = {
    .encode_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=",
    .decode_alphabet = base64_standard_decode_alphabet,
    .padding = true,
};

// python -c 'print(["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_=".find(chr(i)) for i in range(128)])'
static const signed char base64_url_decode_alphabet[] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};
const Base64Params base64_url_params = {
    .encode_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_=",
    .decode_alphabet = base64_url_decode_alphabet,
    .padding = false,
};

struct Base64DecodeError {
    ErrorBase error;
};

static DisplayCtx base64_decode_error_display(ErrorBase const* error, DisplayCtx d) {
    // auto me = c_container_of(error, Base64ErrorPrivate, error);
    ignore error;
    return lit_display("invalid base64-encoded string", d);
}

ErrorVt const Base64DecodeErrorVt = {
    .type_name = "Base64DecodeError",
    .super = &ErrorBaseVt,
    .display = base64_decode_error_display,
    .drop = nullptr,
};

void base64_encode_append(Base64Params params, csview data, cstr* target) {
    size_t encoded_size = ((data.size + 2) / 3) * 4;
    cstr_reserve(target, cstr_size(target) + encoded_size);

    for (ptrdiff_t i = 0; i < data.size; i += 3) {
        unsigned char b0 = data.buf[i];
        unsigned char b1 = (i + 1 < data.size) ? data.buf[i + 1] : 0;
        unsigned char b2 = (i + 2 < data.size) ? data.buf[i + 2] : 0;

        cstr_append_sv(target, csview_from_n(params.encode_alphabet + (b0 >> 2), 1));
        cstr_append_sv(target, csview_from_n(params.encode_alphabet + (((b0 & 0x03) << 4) | (b1 >> 4)), 1));

        if (i + 1 < data.size) {
            cstr_append_sv(target, csview_from_n(params.encode_alphabet + (((b1 & 0x0F) << 2) | (b2 >> 6)), 1));
        } else if (params.padding) {
            cstr_append_sv(target, csview_from_n(params.encode_alphabet + 64, 1));
        }

        if (i + 2 < data.size) {
            cstr_append_sv(target, csview_from_n(params.encode_alphabet + (b2 & 0x3F), 1));
        } else if (params.padding) {
            cstr_append_sv(target, csview_from_n(params.encode_alphabet + 64, 1));
        }
    }
}

static inline signed char index_decode_alphabet(Base64Params params, csview base64, ptrdiff_t i) {
    if (i < base64.size && (unsigned char)base64.buf[i] < 128) {
        return params.decode_alphabet[(unsigned char)base64.buf[i]];
    } else {
        return -1;
    }
}

failable base64_decode_append(Base64Params params, csview base64, cstr* target) {
    size_t initial_target_size = cstr_size(target);

    // Remove padding
    while (base64.size > 0 && index_decode_alphabet(params, base64, base64.size - 1) == 64) {
        base64.size--;
    }

    size_t decoded_size = (base64.size * 3) / 4;
    cstr_reserve(target, cstr_size(target) + decoded_size);

    for (ptrdiff_t i = 0; i < base64.size; i += 4) {
        signed char c0 = index_decode_alphabet(params, base64, i);
        signed char c1 = index_decode_alphabet(params, base64, i + 1);
        signed char c2 = index_decode_alphabet(params, base64, i + 2);
        signed char c3 = index_decode_alphabet(params, base64, i + 3);

        if (c0 == -1 || c0 == 64 || c1 == -1 || c1 == 64) {
            goto error;
        }

        unsigned char b0 = (c0 << 2) | (c1 >> 4);
        cstr_append_sv(target, csview_from_n((char*)&b0, 1));

        if (i + 2 < base64.size) {
            if (c2 == -1 || c2 == 64) {
                goto error;
            }
            unsigned char b1 = ((c1 & 0x0F) << 4) | (c2 >> 2);
            cstr_append_sv(target, csview_from_n((char*)&b1, 1));
        }

        if (i + 3 < base64.size) {
            if (c3 == -1 || c3 == 64) {
                goto error;
            }
            unsigned char b2 = ((c2 & 0x03) << 6) | c3;
            cstr_append_sv(target, csview_from_n((char*)&b2, 1));
        }
    }

    return ok;

error:
    // If we're given an empty string and we fail, dropping the string should not be required. However if we appended to
    // it then the string may hold an allocation, even after resizing. This is why we don't resize for both cases.
    if (initial_target_size == 0) cstr_take(target, cstr_null);
    else cstr_resize(target, initial_target_size, '*');
    return err(&c_new(Base64DecodeError, {
        .error.vt = &Base64DecodeErrorVt,
    })->error);
}
