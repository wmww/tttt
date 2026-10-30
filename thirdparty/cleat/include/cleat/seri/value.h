#pragma once

#include "token.h"
#include "serializer.h"
#include "deserializer.h"
#include "../generic/types.h"

g_declare_vec(SeriValue, seri_value);
g_declare_hmap(cstr, SeriValue, seri_value);

struct SeriValue {
    enum {
        SERI_VALUE_NULL = SERI_TOKEN_NULL,
        SERI_VALUE_BOOL = SERI_TOKEN_BOOL,
        SERI_VALUE_INT = SERI_TOKEN_INT,
        SERI_VALUE_FLOAT = SERI_TOKEN_FLOAT,
        SERI_VALUE_STR = SERI_TOKEN_CSTR,
        SERI_VALUE_VEC = SERI_TOKEN_VEC,
        SERI_VALUE_MAP = SERI_TOKEN_MAP,
    } tag;
    union {
        bool b;
        i64 i;
        double f;
        cstr s;
        VecSeriValue l;
        HMapSeriValue m;
    };
};

static inline void seri_value_drop(SeriValue* me);
static inline SeriValue seri_value_clone(SeriValue me);
int seri_value_cmp(SeriValue const* a, SeriValue const* b);
declare_display(SeriValue, seri_value);

#define g_class_key SeriValue, seri_value
#define g_declared
#define g_display
#include "../generic/vec.h"

#define g_pro_key CStr, cstr
#define g_class_val SeriValue, seri_value
#define g_declared
#define g_display
#include "../generic/hmap.h"

#define seri_value_null ((SeriValue){.tag = SERI_VALUE_NULL})
static inline SeriValue seri_value_bool(bool b) { return (SeriValue){.tag = SERI_VALUE_BOOL, .b = b}; }
static inline SeriValue seri_value_int(i64 i) { return (SeriValue){.tag = SERI_VALUE_INT, .i = i}; }
static inline SeriValue seri_value_float(double f) { return (SeriValue){.tag = SERI_VALUE_FLOAT, .f = f}; }
static inline SeriValue seri_value_s(cstr s) { return (SeriValue){.tag = SERI_VALUE_STR, .s = s}; }
static inline SeriValue seri_value_str(const char* s) { return (SeriValue){.tag = SERI_VALUE_STR, .s = cstr_from(s)}; }
static inline SeriValue seri_value_sv(csview s) { return (SeriValue){.tag = SERI_VALUE_STR, .s = cstr_from_sv(s)}; }
static inline SeriValue seri_value_vec_from_vec(VecSeriValue l) { return (SeriValue){.tag = SERI_VALUE_VEC, .l = l}; }
#define seri_value_vec(...) seri_value_vec_from_vec(c_init(vec_seri_value, {__VA_ARGS__}))
static inline SeriValue seri_value_vec_init() { return seri_value_vec_from_vec(vec_seri_value_init()); }
static inline SeriValue seri_value_map_from_hmap(HMapSeriValue m) { return (SeriValue){.tag = SERI_VALUE_MAP, .m = m}; }
#define seri_value_map(...) seri_value_map_from_hmap(c_init(hmap_seri_value, {__VA_ARGS__}))
static inline SeriValue seri_value_map_init() { return seri_value_map_from_hmap(hmap_seri_value_init()); }

static inline void seri_value_drop(SeriValue* me) {
    switch (me->tag) {
        case SERI_VALUE_NULL:
        case SERI_VALUE_BOOL:
        case SERI_VALUE_INT:
        case SERI_VALUE_FLOAT:
            break;

        case SERI_VALUE_STR: cstr_drop(&me->s); break;
        case SERI_VALUE_VEC: vec_seri_value_drop(&me->l); break;
        case SERI_VALUE_MAP: hmap_seri_value_drop(&me->m); break;
    }
}

static inline SeriValue seri_value_clone(SeriValue me) {
    switch (me.tag) {
        case SERI_VALUE_NULL:
        case SERI_VALUE_BOOL:
        case SERI_VALUE_INT:
        case SERI_VALUE_FLOAT: break;
        case SERI_VALUE_STR: me.s = cstr_clone(me.s); break;
        case SERI_VALUE_VEC: me.l = vec_seri_value_clone(me.l); break;
        case SERI_VALUE_MAP: me.m = hmap_seri_value_clone(me.m); break;
    }
    return me;
}

failable serialize_seri_value_vec(SeriSerializer* me, VecSeriValue const* vec);
failable serialize_seri_value_map(SeriSerializer* me, HMapSeriValue const* map);

static_inline_failable serialize_seri_value(SeriSerializer* me, SeriValue const* value) {
    switch (value->tag) {
        case SERI_VALUE_NULL: return seri_serialize_null(me);
        case SERI_VALUE_BOOL: return seri_serialize_bool(me, value->b);
        case SERI_VALUE_INT: return seri_serialize_int(me, value->i);
        case SERI_VALUE_FLOAT: return seri_serialize_float(me, value->f);
        case SERI_VALUE_STR: return seri_serialize_csview(me, cstr_sv(&value->s));
        case SERI_VALUE_VEC: return serialize_seri_value_vec(me, &value->l);
        case SERI_VALUE_MAP: return serialize_seri_value_map(me, &value->m);
    }
    return err_fmt("invalid SeriValue tag");
}

failable deserialize_seri_value_variant(SeriDeserializer* me, returns SeriValue);
failable deserialize_seri_value_vec(SeriDeserializer* me, returns VecSeriValue);
failable deserialize_seri_value_map(SeriDeserializer* me, returns HMapSeriValue);
failable deserialize_seri_value(SeriDeserializer* me, returns SeriValue);

static_inline_failable seri_value_check_type(SeriValue const* me, typeof((SeriValue){}.tag) expected) {
    return me->tag == expected ?
        (FailableResult){nullptr} :
        err_fmt("expected %s, got %s",
            seri_token_tag_to_str((SeriTokenTag)expected),
            seri_token_tag_to_str((SeriTokenTag)me->tag));
}

static inline void seri_value_vec_push(SeriValue* vec, SeriValue item) {
    panic_if_str(vec->tag != SERI_VALUE_VEC, "tried to push to non-vec value");
    vec_seri_value_push(&vec->l, item);
}

static inline void seri_value_map_set(SeriValue* map, cstr key, SeriValue val) {
    panic_if_str(map->tag != SERI_VALUE_MAP, "tried to set value of non-map value");
    hmap_seri_value_insert_or_assign(&map->m, key, val);
}

static_inline_failable seri_value_get_bool(SeriValue const* me, returns bool) {
    FailableResult result = seri_value_check_type(me, SERI_VALUE_BOOL);
    if (result.error) return result;
    *_OUTPUT_PARAM = me->b;
    return (FailableResult){nullptr};
}

static_inline_failable seri_value_get_int(SeriValue const* me, returns i64) {
    FailableResult result = seri_value_check_type(me, SERI_VALUE_INT);
    if (result.error) return result;
    *_OUTPUT_PARAM = me->i;
    return (FailableResult){nullptr};
}

static_inline_failable seri_value_get_float(SeriValue const* me, returns double) {
    if (me->tag == SERI_VALUE_INT) {
        *_OUTPUT_PARAM = me->i;
    } else {
        FailableResult result = seri_value_check_type(me, SERI_VALUE_FLOAT);
        if (result.error) return result;
        *_OUTPUT_PARAM = me->f;
    }
    return (FailableResult){nullptr};
}

static_inline_failable seri_value_get_sv(SeriValue const* me, returns csview) {
    FailableResult result = seri_value_check_type(me, SERI_VALUE_STR);
    if (result.error) return result;
    *_OUTPUT_PARAM = cstr_sv(&me->s);
    return (FailableResult){nullptr};
}

static_inline_failable seri_value_into_cstr(SeriValue me, returns cstr) {
    FailableResult result = seri_value_check_type(&me, SERI_VALUE_STR);
    if (result.error) {
        seri_value_drop(&me);
        return result;
    }
    *_OUTPUT_PARAM = me.s;
    return (FailableResult){nullptr};
}

static inline i64 seri_value_vec_size(SeriValue const* vec) {
    return vec->tag == SERI_VALUE_VEC ? vec_seri_value_size(&vec->l) : 0;
}

static_inline_failable seri_value_vec_at(SeriValue const* me, i64 i, returns SeriValue const*) {
    FailableResult result = seri_value_check_type(me, SERI_VALUE_VEC);
    if (result.error) return result;
    if (i < 0 || i >= me->l.size) return err_fmt("vec index %lld out of range", (long long)i);
    *_OUTPUT_PARAM = &me->l.data[i];
    return (FailableResult){nullptr};
}

static_inline_failable seri_value_map_get(SeriValue const* me, const char* key, returns SeriValue const*) {
    FailableResult result = seri_value_check_type(me, SERI_VALUE_MAP);
    if (result.error) return result;
    auto item = hmap_seri_value_get(&me->m, key);
    if (!item) return err_fmt("map does not have key '%s'", key);
    *_OUTPUT_PARAM = &item->second;
    return (FailableResult){nullptr};
}

typedef struct seri_value_iter seri_value_iter;
struct seri_value_iter {
    SeriValue* ref;
    bool is_map;
    csview key;
    union {
        vec_seri_value_iter vec;
        hmap_seri_value_iter map;
    };
};

seri_value_iter seri_value_begin(SeriValue const* me);
void seri_value_next(seri_value_iter* me);
static inline SeriValue* seri_value_iter_deref(seri_value_iter* me) { return me->ref; }
