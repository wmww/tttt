#include <limits.h>

#include "seri/value.h"
#include "seri/pattern.h"
#include "display.h"
#include "failable.h"
#include "common_macros.h"

#undef DISPLAYABLES
#define DISPLAYABLES \
    CORE_DISPLAYABLES, \
    SeriValue:    seri_value_display, \
    SeriToken:    seri_token_display, \
    VecSeriToken: vec_seri_token_display

int seri_token_cmp(SeriToken const* a, SeriToken const* b) {
    if (a->tag == b->tag) {
        switch (a->tag) {
            case SERI_TOKEN_BOOL: return c_default_cmp(&a->b, &b->b);
            case SERI_TOKEN_INT: return c_default_cmp(&a->i, &b->i);
            case SERI_TOKEN_UINT: return c_default_cmp(&a->u, &b->u);
            case SERI_TOKEN_FLOAT: return c_default_cmp(&a->f, &b->f);
            case SERI_TOKEN_CSVIEW: goto string_cmp;
            case SERI_TOKEN_CSTR: goto string_cmp;
            case SERI_TOKEN_NULL:
            case SERI_TOKEN_VARIANT:
            case SERI_TOKEN_VEC:
            case SERI_TOKEN_MAP:
            case SERI_TOKEN_SOME:
            case SERI_TOKEN_END_CONTAINER:
            case SERI_TOKEN_EOF:
                return 0;
        }
    } else if (
        (a->tag == SERI_TOKEN_CSVIEW || a->tag == SERI_TOKEN_CSTR) &&
        (b->tag == SERI_TOKEN_CSVIEW || b->tag == SERI_TOKEN_CSTR)
    ) {
        string_cmp:
        csview a_sv = a->tag == SERI_TOKEN_CSVIEW ? a->sv : cstr_sv(&a->s);
        csview b_sv = b->tag == SERI_TOKEN_CSVIEW ? b->sv : cstr_sv(&b->s);
        return csview_cmp(&a_sv, &b_sv);
    } else if (
        (a->tag == SERI_TOKEN_INT || a->tag == SERI_TOKEN_UINT || a->tag == SERI_TOKEN_FLOAT) &&
        (b->tag == SERI_TOKEN_INT || b->tag == SERI_TOKEN_UINT || b->tag == SERI_TOKEN_FLOAT)
    ) {
        double a_f = a->tag == SERI_TOKEN_FLOAT ? a->f : (a->tag == SERI_TOKEN_INT ? (double)a->i : (double)a->u);
        double b_f = b->tag == SERI_TOKEN_FLOAT ? b->f : (b->tag == SERI_TOKEN_INT ? (double)b->i : (double)b->u);
        if (a_f != b_f || a->tag == SERI_TOKEN_FLOAT || b->tag == SERI_TOKEN_FLOAT) {
            return c_default_cmp(&a_f, &b_f);
        }
        // Casting to unsigned is safe because they could only both be negative if they had the same tag (caught above)
        // and if one was negative its float value wouldn't have been equal to a positive
        u64 a_u = a->tag == SERI_TOKEN_INT ? (u64)a->i : a->u;
        u64 b_u = b->tag == SERI_TOKEN_INT ? (u64)b->i : b->u;
        return c_default_cmp(&a_u, &b_u);
    }
    return c_default_cmp(&a->tag, &b->tag);
}

const char* seri_token_tag_to_str(SeriTokenTag tag) {
    switch (tag) {
        case SERI_TOKEN_NULL: return "null";
        case SERI_TOKEN_BOOL: return "bool";
        case SERI_TOKEN_INT: return "int";
        case SERI_TOKEN_UINT: return "uint";
        case SERI_TOKEN_FLOAT: return "float";
        case SERI_TOKEN_CSVIEW: return "csview";
        case SERI_TOKEN_CSTR: return "cstr";
        case SERI_TOKEN_VARIANT: return "variant";
        case SERI_TOKEN_VEC: return "vec";
        case SERI_TOKEN_MAP: return "map";
        case SERI_TOKEN_SOME: return "some";
        case SERI_TOKEN_END_CONTAINER: return "end";
        case SERI_TOKEN_EOF: return "EOF";
    }
    return "<INVALID>";
}

impl_display_class(SeriToken, seri_token) {
    d = sv_display(csview_from(seri_token_tag_to_str(my tag)), d);
    d = lit_display(":", d);
    switch (my tag) {
        case SERI_TOKEN_BOOL:   return bool_display(my b, d);
        case SERI_TOKEN_INT:    return i64_display(my i, d);
        case SERI_TOKEN_UINT:   return u64_display(my u, d);
        case SERI_TOKEN_FLOAT:  return f64_display(my f, d);
        case SERI_TOKEN_CSVIEW: return string_debug_display(my sv, d);
        case SERI_TOKEN_CSTR:   return string_debug_display(cstr_sv(&my s), d);
        default:
    }
    return d;
}

int seri_value_cmp(SeriValue const* a, SeriValue const* b) {
    if (a->tag != b->tag) return c_default_cmp(&a->tag, &b->tag);
    switch (a->tag) {
        case SERI_VALUE_NULL: return 0;
        case SERI_VALUE_BOOL: return c_default_cmp(&a->b, &b->b);
        case SERI_VALUE_INT: return c_default_cmp(&a->i, &b->i);
        case SERI_VALUE_FLOAT: return c_default_cmp(&a->f, &b->f);
        case SERI_VALUE_STR: return cstr_cmp(&a->s, &b->s);
        case SERI_VALUE_VEC: {
            int ret = c_default_cmp(&a->l.size, &b->l.size);
            if (ret != 0) return ret;
            for (i64 i = 0; i < a->l.size; i++) {
                ret = seri_value_cmp(&a->l.data[i], &b->l.data[i]);
                if (ret != 0) return ret;
            }
            return 0;
        }
        case SERI_VALUE_MAP: {
            int ret = c_default_cmp(&a->m.size, &b->m.size);
            if (ret != 0) return ret;
            for (hmap_seri_value_iter ai = hmap_seri_value_begin(&a->m), bi = hmap_seri_value_begin(&b->m);
                ai.ref;
                hmap_seri_value_next(&ai), hmap_seri_value_next(&bi)
            ) {
                ret = cstr_cmp(&ai.ref->first, &bi.ref->first);
                if (ret != 0) return ret;
                ret = seri_value_cmp(&ai.ref->second, &bi.ref->second);
                if (ret != 0) return ret;
            }
            return 0;
        }
    }
    return 0;
}

impl_display_class(SeriValue, seri_value) {
    switch (my tag) {
        case SERI_VALUE_NULL:   return lit_display("null", d);
        case SERI_VALUE_BOOL:   return bool_display(my b, d);
        case SERI_VALUE_INT:    return lit_display("i", i64_display(my i, d));
        case SERI_VALUE_FLOAT:  return lit_display("f", f64_display(my f, d));
        case SERI_VALUE_STR:    return string_debug_display(cstr_sv(&my s), d);
        case SERI_VALUE_VEC:    return vec_seri_value_display(my l, d);
        case SERI_VALUE_MAP:    return hmap_seri_value_display(my m, d);
    }
    impossible;
}

failable serialize_seri_value_vec(SeriSerializer* me, VecSeriValue const* vec) {
    try (serialize_token(me, seri_token_make(SERI_TOKEN_VEC))) bubble;
    for (ptrdiff_t i = 0; i < vec->size; i++) {
        try (serialize_seri_value(me, &vec->data[i])) bubble;
    }
    try (serialize_token(me, seri_token_make(SERI_TOKEN_END_CONTAINER))) bubble;
    return ok;
}

failable serialize_seri_value_map(SeriSerializer* me, HMapSeriValue const* map) {
    try (serialize_token(me, seri_token_make(SERI_TOKEN_MAP))) bubble;
    for_each_kv (k, v, hmap_seri_value, map) {
        try (serialize_token(me, seri_token_make(SERI_TOKEN_CSVIEW, .sv = cstr_sv(k)))) bubble;
        try (serialize_seri_value(me, v)) bubble;
    }
    try (serialize_token(me, seri_token_make(SERI_TOKEN_END_CONTAINER))) bubble;
    return ok;
}

static_inline_failable deserialize_value_stringish(SeriDeserializer* me, returns cstr) {
    try (SeriToken token, deserialize_token(me, &token)) bubble;
    switch (token.tag) {
        case SERI_TOKEN_INT: return ok(cstr_from_fmt("%lld", (long long)token.i));
        case SERI_TOKEN_UINT: return ok(cstr_from_fmt("%llu", (unsigned long long)token.u));
        case SERI_TOKEN_CSVIEW: return ok(cstr_from_sv(token.sv));
        case SERI_TOKEN_CSTR: return ok(token.s);
        default: {
            seri_token_drop(&token);
            return err_fmt("%s is not stringish", seri_token_tag_to_str(token.tag));
        }
    }
}

failable deserialize_seri_value_variant(SeriDeserializer* me, returns SeriValue) {
    try (cstr tag, deserialize_value_stringish(me, &tag)) bubble;
    try (SeriTokenTag next, deserialize_peek_tag(me, &next)) cleanup cstr_drop(&tag);
    bool has_value = false;
    SeriValue value;
    if (next != SERI_TOKEN_END_CONTAINER) {
        has_value = true;
        try (deserialize_seri_value(me, &value)) cleanup cstr_drop(&tag);
    }
    try (deserialize_expect_tag(me, SERI_TOKEN_END_CONTAINER, nullptr)) cleanup {
        cstr_drop(&tag);
        if (has_value) seri_value_drop(&value);
    }
    if (has_value) {
        return ok(seri_value_vec(seri_value_s(tag), value));
    } else {
        return ok(seri_value_s(tag));
    }
}

failable deserialize_seri_value_vec(SeriDeserializer* me, returns VecSeriValue) {
    VecSeriValue ret = vec_seri_value_init();
    while (true) {
        try (SeriTokenTag next, deserialize_peek_tag(me, &next)) cleanup {
            vec_seri_value_drop(&ret);
        }
        if (next == SERI_TOKEN_END_CONTAINER) {
            try (deserialize_expect_tag(me, SERI_TOKEN_END_CONTAINER, nullptr)) or_panic;
            return ok(ret);
        }
        try (SeriValue value, deserialize_seri_value(me, &value)) cleanup {
            vec_seri_value_drop(&ret);
        }
        vec_seri_value_push(&ret, value);
    }
}

failable deserialize_seri_value_map(SeriDeserializer* me, returns HMapSeriValue) {
    HMapSeriValue ret = hmap_seri_value_init();
    while (true) {
        try (SeriTokenTag next, deserialize_peek_tag(me, &next)) cleanup {
            hmap_seri_value_drop(&ret);
        }
        if (next == SERI_TOKEN_END_CONTAINER) {
            try (deserialize_expect_tag(me, SERI_TOKEN_END_CONTAINER, nullptr)) or_panic;
            return ok(ret);
        }
        try (cstr key, deserialize_value_stringish(me, &key)) cleanup {
            hmap_seri_value_drop(&ret);
        }
        try (SeriValue value, deserialize_seri_value(me, &value)) cleanup {
            hmap_seri_value_drop(&ret);
        }
        hmap_seri_value_insert_or_assign(&ret, key, value);
    }
}

failable deserialize_seri_value(SeriDeserializer* me, returns SeriValue) {
    SeriToken token;
    FailableResult result = deserialize_token(me, &token);
    if (result.error) return result;
    switch (token.tag) {
        case SERI_TOKEN_NULL: return ok(seri_value_null);
        case SERI_TOKEN_BOOL: return ok(seri_value_bool(token.b));
        case SERI_TOKEN_INT:  return ok(seri_value_int(token.i));
        case SERI_TOKEN_UINT:
            if (token.u <= LLONG_MAX) return ok(seri_value_int(token.u));
            else                      return ok(seri_value_float(token.u));
        case SERI_TOKEN_FLOAT:  return ok(seri_value_float(token.f));
        case SERI_TOKEN_CSVIEW: return ok(seri_value_sv(token.sv));
        case SERI_TOKEN_CSTR:   return ok(seri_value_s(token.s));

        case SERI_TOKEN_VARIANT: {
            SeriValue val;
            result = deserialize_seri_value_variant(me, &val);
            if (result.error) return result;
            return ok(val);
        }

        case SERI_VALUE_VEC: {
            VecSeriValue vec;
            result = deserialize_seri_value_vec(me, &vec);
            if (result.error) return result;
            return ok(seri_value_vec_from_vec(vec));
        }

        case SERI_TOKEN_MAP: {
            HMapSeriValue map;
            result = deserialize_seri_value_map(me, &map);
            if (result.error) return result;
            return ok(seri_value_map_from_hmap(map));
        }

        case SERI_TOKEN_SOME: {
            SeriValue val;
            result = deserialize_seri_value(me, &val);
            if (result.error) return result;
            result = deserialize_expect_tag(me, SERI_TOKEN_END_CONTAINER, nullptr);
            if (result.error) {
                seri_value_drop(&val);
                return result;
            }
            return ok(val);
        }

        case SERI_TOKEN_END_CONTAINER: return err_fmt("unexpected container end");
        case SERI_TOKEN_EOF: return err_fmt("unexpected EOF");
    }
    return err_fmt("invalid SeriToken tag");
}

seri_value_iter seri_value_begin(SeriValue const* me) {
    switch (me->tag) {
        case SERI_VALUE_VEC: {
            auto iter = vec_seri_value_begin(&me->l);
            return (seri_value_iter){
                .ref = iter.ref,
                .is_map = false,
                .vec = iter,
            };
        }
        case SERI_VALUE_MAP: {
            auto iter = hmap_seri_value_begin(&me->m);
            return (seri_value_iter){
                .ref = iter.ref ? &iter.ref->second : nullptr,
                .is_map = true,
                .key = iter.ref ? cstr_sv(&iter.ref->first) : (csview){0},
                .map = iter,
            };
        }
        default: return (seri_value_iter){0};
    }
}

void seri_value_next(seri_value_iter* me) {
    if (me->ref) {
        if (!me->is_map) {
            vec_seri_value_next(&me->vec);
            me->ref = me->vec.ref;
        } else {
            hmap_seri_value_next(&me->map);
            me->ref = me->map.ref ? &me->map.ref->second : nullptr;
            me->key = me->map.ref ? cstr_sv(&me->map.ref->first) : (csview){0};
        }
    }
}

failable seri_pattern_context_serialize(SeriSerializer* ser, SeriToken token) {
    auto me = c_container_of(ser, SeriPatternContext, matcher);
    if (my failed) return ok;
    SeriToken actual;
    FailableResult result = deserialize_token(my de, &actual);
    if (result.error) {
        my failed = true;
        seri_token_drop(&token);
        return result;
    }
    if (seri_token_cmp(&token, &actual) != 0) {
        my failed = true;
        seri_token_drop(&token);
        seri_token_drop(&actual);
        if (my inhibit_errors > 0) {
            return ok;
        } else {
            return err_d("got ", actual, ", expected ", token);
        }
    } else {
        seri_token_drop(&token);
        seri_token_drop(&actual);
        return ok;
    }
}

void seri_report_unknown_map_key_error(SeriPatternContext* me) {
    if (!my inhibit_errors) {
        try (SeriValue val, deserialize_seri_value(my de, &val)) if_ok {
            add_error_to_collection(&my result, err_d("unknown map key ", val).error);
        } if_err (e) {
            add_error_to_collection(&my result, err_d("deserializing map key: ", e).error);
        }
    }
    my failed = true;
}
