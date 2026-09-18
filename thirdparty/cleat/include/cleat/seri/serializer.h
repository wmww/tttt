#pragma once

#include "token.h"
#include "../failable_minimal.h"

struct SeriSerializer {
    FailableResult (*serialize)(SeriSerializer* ser, SeriToken token);
};

static_inline_failable serialize_token(SeriSerializer* me, SeriToken token) {
    return me->serialize(me, token);
}

static_inline_failable seri_serialize_null(SeriSerializer* me) {
    return serialize_token(me, seri_token_make(SERI_TOKEN_NULL));
}

static_inline_failable seri_serialize_bool(SeriSerializer* me, bool x) {
    return serialize_token(me, seri_token_make(SERI_TOKEN_BOOL, .b = x));
}

static_inline_failable seri_serialize_int(SeriSerializer* me, i64 x) {
    return serialize_token(me, seri_token_make(SERI_TOKEN_INT, .i = x));
}

static_inline_failable seri_serialize_uint(SeriSerializer* me, u64 x) {
    return serialize_token(me, seri_token_make(SERI_TOKEN_UINT, .u = x));
}

static_inline_failable seri_serialize_float(SeriSerializer* me, double x) {
    return serialize_token(me, seri_token_make(SERI_TOKEN_FLOAT, .f = x));
}

static_inline_failable seri_serialize_csview(SeriSerializer* me, csview x) {
    return serialize_token(me, seri_token_make(SERI_TOKEN_CSVIEW, .sv = x));
}

static_inline_failable seri_serialize_cstr(SeriSerializer* me, cstr x) {
    return serialize_token(me, seri_token_make(SERI_TOKEN_CSTR, .s = x));
}
