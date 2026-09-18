#pragma once

#include <stc/cstr.h>
#include "../types.h"
#include "../failable_minimal.h"
#include "../display_minimal.h"

typedef struct SeriToken SeriToken;
typedef struct SeriSerializer SeriSerializer;
typedef struct SeriDeserializer SeriDeserializer;
typedef struct SeriValue SeriValue;

typedef enum SeriTokenTag {
    SERI_TOKEN_NULL = 0,
    SERI_TOKEN_BOOL,
    SERI_TOKEN_INT,
    SERI_TOKEN_UINT,
    SERI_TOKEN_FLOAT,
    SERI_TOKEN_CSVIEW,
    SERI_TOKEN_CSTR,
    SERI_TOKEN_VARIANT, // next token is tag, and then optionally a value
    SERI_TOKEN_VEC,
    SERI_TOKEN_MAP, // keys and values alternate, must have an even number of items before container end
    SERI_TOKEN_SOME,
    SERI_TOKEN_END_CONTAINER,
    SERI_TOKEN_EOF,
} SeriTokenTag;
struct SeriToken {
    SeriTokenTag tag;
    union {
        i64 i;
        bool b;
        u64 u;
        double f;
        csview sv;
        cstr s;
    };
};

#define seri_token_make(...) ((SeriToken){.tag = __VA_ARGS__})

static inline void seri_token_drop(SeriToken* me) {
    if (me->tag == SERI_TOKEN_CSTR) cstr_drop(&me->s);
}

static inline SeriToken seri_token_clone(SeriToken me) {
    if (me.tag == SERI_TOKEN_CSTR) me.s = cstr_clone(me.s);
    return me;
}

static inline SeriToken seri_token_refclone(SeriToken const* me) {
    if (me->tag == SERI_TOKEN_CSTR) {
        return seri_token_make(
            SERI_TOKEN_CSVIEW,
            .sv = cstr_sv(&me->s)
        );
    } else {
        return *me;
    }
}

int seri_token_cmp(SeriToken const* a, SeriToken const* b);

declare_display(SeriToken, seri_token);
const char* seri_token_tag_to_str(SeriTokenTag tag);

#define g_key SeriTokenTag, seri_token_tag
#include "../generic/vec.h"

typedef struct SeriTokenState SeriTokenState;
struct SeriTokenState {
    VecSeriTokenTag all_but_top; // All containers but the top-most
    SeriTokenTag container; // the top-most container, null if at top level
    bool has_values; // If the top container has any keys/values yet
    bool odd_values; // If the top container has an odd number of values
    u32 valid_key_tags; // bitmask of which tags can be used as map/variant keys
};

static inline void seri_token_state_drop(SeriTokenState* me) {
    vec_seri_token_tag_drop(&me->all_but_top);
}

static_inline_failable seri_token_state_next(SeriTokenState* me, SeriTokenTag tag) {
    if (me->container == SERI_TOKEN_NULL &&
        tag != SERI_TOKEN_EOF &&
        tag != SERI_TOKEN_END_CONTAINER &&
        me->has_values
    ) {
        return err_fmt("multiple top-level values");
    }

    switch (tag) {
        case SERI_TOKEN_NULL:
        case SERI_TOKEN_BOOL:
        case SERI_TOKEN_INT:
        case SERI_TOKEN_UINT:
        case SERI_TOKEN_FLOAT:
        case SERI_TOKEN_CSVIEW:
        case SERI_TOKEN_CSTR:
            break;

        case SERI_TOKEN_VARIANT:
        case SERI_TOKEN_VEC:
        case SERI_TOKEN_MAP:
        case SERI_TOKEN_SOME:
            if (me->container) {
                if (vec_seri_token_tag_size(&me->all_but_top) > 1000) {
                    return err_fmt("structure too deeply nested");
                }
                vec_seri_token_tag_push(
                    &me->all_but_top,
                    me->container | (me->odd_values) << 8);
            }
            me->container = tag;
            me->odd_values = false;
            me->has_values = false;
            return (FailableResult){nullptr};

        case SERI_TOKEN_END_CONTAINER: {
            if (me->container == SERI_TOKEN_NULL) {
                return err_fmt("unmatched container end token");
            } else if (me->container == SERI_TOKEN_MAP && me->odd_values) {
                return err_fmt("map key has no value");
            }
            unsigned x = me->all_but_top.size ? me->all_but_top.data[--me->all_but_top.size] : 0;
            me->container = x & 0xFF;
            me->odd_values = (x >> 8) & 0x1;
        }   break;

        case SERI_TOKEN_EOF:
            if (me->container != SERI_TOKEN_NULL) {
                return err_fmt("EOF with open %s", seri_token_tag_to_str(me->container));
            } else if (!me->has_values) {
                return err_fmt("serialized nothing");
            }
            return (FailableResult){nullptr};
    }

    switch (me->container) {
        case SERI_TOKEN_SOME:
            if (me->has_values) {
                return err_fmt("multiple values in Some()");
            };
            break;

        case SERI_TOKEN_VARIANT:
            if (me->has_values && !me->odd_values) {
                return err_fmt("more than two items in variant");
            } else if (!me->has_values && !(me->valid_key_tags & (1 << tag))) {
                return err_fmt("invalid variant type: %s", seri_token_tag_to_str(tag));
            }
            break;

        case SERI_TOKEN_MAP:
            if (!me->odd_values && !(me->valid_key_tags & (1 << tag))) {
                return err_fmt("invalid map key type: %s", seri_token_tag_to_str(tag));
            }
            break;

        default: break;
    }

    me->has_values = true;
    me->odd_values = !me->odd_values;
    return (FailableResult){nullptr};
}
