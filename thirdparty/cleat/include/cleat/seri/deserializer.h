#pragma once

#include "token.h"
#include "../failable_minimal.h"
#include "../common_macros.h"

#define g_class_key SeriToken, seri_token
#define g_display
#include "../generic/vec.h"

struct SeriDeserializer {
    FailableResult (*deserialize)(SeriDeserializer* de, returns SeriToken);
    // Note that the state reflects the state of the end of what has been deserialized so far, not the current position
    // after a checkpoint has been restored.
    SeriTokenState state;
    struct {
        bool has; // If the peaked token is valid
        SeriToken token;
    } peaked;
    i64 checkpoint_count;
    i64 checkpoint_generation;
    i64 memory_pos;
    VecSeriToken memory;
};

typedef struct SeriCheckpoint SeriCheckpoint;
struct SeriCheckpoint {
    i64 pos;
    i64 generation;
};

static inline SeriCheckpoint seri_deserializer_checkpoint(SeriDeserializer* me) {
    me->checkpoint_count++;
    return (SeriCheckpoint){
        .pos = me->memory_pos,
        .generation = me->checkpoint_generation,
    };
}

// Does not auto-dismiss checkpoint, same checkpoint can be used multiple times
static inline void seri_deserializer_restore(SeriDeserializer* me, SeriCheckpoint checkpoint) {
    check(checkpoint.generation == me->checkpoint_generation);
    me->memory_pos = checkpoint.pos;
}

static inline void _seri_deserializer_maybe_reset_memory(SeriDeserializer* me) {
    if (me->checkpoint_count == 0 && me->memory_pos >= me->memory.size) {
        vec_seri_token_resize(&me->memory, 0, (SeriToken){0});
        me->memory_pos = 0;
        me->checkpoint_generation++;
    }
}

static inline void seri_deserializer_dismiss(SeriDeserializer* me, SeriCheckpoint checkpoint) {
    check(checkpoint.generation == me->checkpoint_generation);
    check(me->checkpoint_count > 0);
    me->checkpoint_count--;
    _seri_deserializer_maybe_reset_memory(me);
}

static inline void seri_deserializer_drop(SeriDeserializer* me) {
    if (me->peaked.has) seri_token_drop(&me->peaked.token);
    seri_token_state_drop(&me->state);
    vec_seri_token_drop(&me->memory);
}

static_inline_failable _seri_deserializer_deserialize_token(SeriDeserializer* me, SeriToken* ret) {
    FailableResult result;
    result = me->deserialize(me, ret);
    if (result.error) return result;
    result = seri_token_state_next(&me->state, ret->tag);
    if (result.error) {
        seri_token_drop(ret);
        return result;
    }
    if (me->checkpoint_count > 0 && ret->tag != SERI_TOKEN_EOF) {
        vec_seri_token_push(&me->memory, *ret);
        *ret = seri_token_refclone(vec_seri_token_back(&me->memory));
        me->memory_pos = me->memory.size;
    }
    return (FailableResult){nullptr};
}

static_inline_failable deserialize_token(SeriDeserializer* me, SeriToken* ret) {
    if (me->peaked.has) {
        me->peaked.has = false;
        *ret = me->peaked.token;
        me->peaked.token = (SeriToken){0};
    } else if (me->memory_pos < me->memory.size) {
        if (me->checkpoint_count == 0) {
            *ret = me->memory.data[me->memory_pos];
            me->memory.data[me->memory_pos] = seri_token_make(SERI_TOKEN_NULL);
            me->memory_pos++;
            _seri_deserializer_maybe_reset_memory(me);
        } else {
            *ret = seri_token_refclone(&me->memory.data[me->memory_pos]);
            me->memory_pos++;
        }
    } else {
        SeriToken token;
        FailableResult result;
        result = _seri_deserializer_deserialize_token(me, &token);
        if (result.error) return result;
        if (me->state.container == SERI_TOKEN_NULL && me->state.has_values) {
            // There should only be one top-level value, so future tokens should all be EOF. The token state will verify
            // this, but we need to pump it to make that happen.
            SeriToken expected_eof;
            result = _seri_deserializer_deserialize_token(me, &expected_eof);
            if (result.error) {
                seri_token_drop(&token);
                return result;
            }
            seri_token_drop(&expected_eof);
        }
        *ret = token;
    }
    return (FailableResult){nullptr};
}

// A peaked token never needs to be dropped but is only valid until the next call
static_inline_failable deserialize_peek_token(SeriDeserializer* me, SeriToken* ret) {
    if (!me->peaked.has) {
        FailableResult result = deserialize_token(me, &me->peaked.token);
        if (result.error) return result;
        me->peaked.has = true;
    }
    *ret = seri_token_refclone(&me->peaked.token);
    return (FailableResult){nullptr};
}

static_inline_failable deserialize_peek_tag(SeriDeserializer* me, SeriTokenTag* ret) {
    SeriToken token;
    FailableResult result = deserialize_peek_token(me, &token);
    if (!result.error) *ret = token.tag;
    return result;
}

static_inline_failable deserialize_expect_tag(SeriDeserializer* me, SeriTokenTag expected, SeriToken* opt_ret) {
    SeriToken token;
    FailableResult result = deserialize_peek_token(me, &token);
    if (result.error) return result;
    if (token.tag == expected) {
        result = deserialize_token(me, &token);
        if (result.error) return result;
        if (opt_ret) *opt_ret = token;
        else seri_token_drop(&token);
        return (FailableResult){nullptr};
    } else {
        return err_fmt(
            "expected %s, got %s",
            seri_token_tag_to_str(expected),
            seri_token_tag_to_str(token.tag)
        );
    }
}

static_inline_failable deserialize_maybe_expect_eof(SeriDeserializer* me, bool expect_eof) {
    if (expect_eof) {
        return deserialize_expect_tag(me, SERI_TOKEN_EOF, nullptr);
    } else {
        return (FailableResult){nullptr};
    }
}

static_inline_failable seri_deserializer_skip_to_container_end(SeriDeserializer* me);

static_inline_failable seri_deserializer_skip_item(SeriDeserializer* me) {
    SeriToken tok;
    FailableResult result = deserialize_token(me, &tok);
    if (result.error) return result;
    switch (tok.tag) {
        case SERI_TOKEN_NULL:
        case SERI_TOKEN_BOOL:
        case SERI_TOKEN_INT:
        case SERI_TOKEN_UINT:
        case SERI_TOKEN_FLOAT:
        case SERI_TOKEN_CSVIEW:
        case SERI_TOKEN_CSTR:
            seri_token_drop(&tok);
            return (FailableResult){nullptr};

        case SERI_TOKEN_VEC:
        case SERI_TOKEN_MAP:
        case SERI_TOKEN_SOME:
        case SERI_TOKEN_VARIANT:
            result = seri_deserializer_skip_to_container_end(me);
            if (result.error) return result;
            return deserialize_token(me, &tok);

        case SERI_TOKEN_END_CONTAINER:
        case SERI_TOKEN_EOF:
            break;
    }
    return err_fmt("deserializing: no items to skip");
}

static_inline_failable seri_deserializer_skip_to_container_end(SeriDeserializer* me) {
    while (true) {
        SeriTokenTag next_tag;
        FailableResult result = deserialize_peek_tag(me, &next_tag);
        if (result.error) return result;
        if (next_tag == SERI_TOKEN_END_CONTAINER) return (FailableResult){nullptr};
        result = seri_deserializer_skip_item(me);
        if (result.error) return result;
    }
}

static_inline_failable seri_deserialize_null(SeriDeserializer* me) {
    return deserialize_expect_tag(me, SERI_TOKEN_NULL, nullptr);
}

static_inline_failable seri_deserialize_bool(SeriDeserializer* me, returns bool) {
    SeriToken tok;
    FailableResult result = deserialize_expect_tag(me, SERI_TOKEN_BOOL, &tok);
    if (!result.error) *_OUTPUT_PARAM = tok.b;
    return result;
}

static_inline_failable seri_deserialize_int(SeriDeserializer* me, returns i64) {
    FailableResult result;
    SeriToken tok;
    result = deserialize_token(me, &tok);
    if (result.error) return result;
    switch (tok.tag) {
        case SERI_TOKEN_UINT: *_OUTPUT_PARAM = tok.u; break;
        case SERI_TOKEN_INT: *_OUTPUT_PARAM = tok.i; break;
        case SERI_TOKEN_FLOAT:
            if ((i64)tok.f == tok.f) {
                *_OUTPUT_PARAM = tok.f;
                break;
            } // else fallthrough
        default:
            seri_token_drop(&tok);
            return err_fmt("expected int, got %s", seri_token_tag_to_str(tok.tag));
    }
    return (FailableResult){nullptr};
}

static_inline_failable seri_deserialize_uint(SeriDeserializer* me, returns u64) {
    FailableResult result;
    SeriToken tok;
    result = deserialize_token(me, &tok);
    if (result.error) return result;
    switch (tok.tag) {
        case SERI_TOKEN_UINT: *_OUTPUT_PARAM = tok.u; break;
        case SERI_TOKEN_INT: *_OUTPUT_PARAM = tok.i; break;
        case SERI_TOKEN_FLOAT:
            if ((u64)tok.f == tok.f) {
                *_OUTPUT_PARAM = tok.f;
                break;
            } // else fallthrough
        default:
            seri_token_drop(&tok);
            return err_fmt("expected uint, got %s", seri_token_tag_to_str(tok.tag));
    }
    return (FailableResult){nullptr};
}

static_inline_failable seri_deserialize_float(SeriDeserializer* me, returns double) {
    FailableResult result;
    SeriToken tok;
    result = deserialize_token(me, &tok);
    if (result.error) return result;
    switch (tok.tag) {
        case SERI_TOKEN_UINT: *_OUTPUT_PARAM = tok.u; break;
        case SERI_TOKEN_INT: *_OUTPUT_PARAM = tok.i; break;
        case SERI_TOKEN_FLOAT: *_OUTPUT_PARAM = tok.f; break;
        default:
            seri_token_drop(&tok);
            return err_fmt("expected float, got %s", seri_token_tag_to_str(tok.tag));
    }
    return (FailableResult){nullptr};
}

// No seri_deserialize_csview because there's no good way to implement it with support for cstr tokens, and having it
// not support cstr tokens would be a footgun

static_inline_failable seri_deserialize_cstr(SeriDeserializer* me, returns cstr) {
    FailableResult result;
    SeriToken tok;
    result = deserialize_token(me, &tok);
    if (result.error) return result;
    switch (tok.tag) {
        case SERI_TOKEN_CSVIEW: *_OUTPUT_PARAM = cstr_from_sv(tok.sv); break;
        case SERI_TOKEN_CSTR: *_OUTPUT_PARAM = tok.s; break;
        default:
            seri_token_drop(&tok);
            return err_fmt("expected string, got %s", seri_token_tag_to_str(tok.tag));
    }
    return (FailableResult){nullptr};
}

// Parses the string as hex, returns true on success
bool seri_hex_digits_to_int(csview string, u64* ret);

// Handles exponents, optionally handles hex/binary.
const char* seri_string_to_number(
    csview string,
    bool allow_alternate_bases,
    bool leading_zero_octal,
    SeriToken* ret
);
