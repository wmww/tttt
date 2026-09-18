#pragma once

#include "../metaprogramming.h"
#include "../failable.h"
#include "serializer.h"
#include "deserializer.h"
#include <stc/csview.h>

typedef struct SeriPatternContext SeriPatternContext;
struct SeriPatternContext {
    FailableResult result;
    SeriDeserializer* de;
    SeriSerializer* ser;
    SeriSerializer matcher; // while deserializing, tokens can be serialized into this to be matched
    bool inhibit_errors; // if true failed is set but ok is returned for standard failed to match errors
    bool failed;
};

failable seri_pattern_context_serialize(SeriSerializer* ser, SeriToken token);
void seri_report_unknown_map_key_error(SeriPatternContext* me);
static inline void seri_pattern_context_drop(SeriPatternContext* me) { (void)me; }

static_inline_failable seri_internal_serialize_nullptr(SeriSerializer* me, nullptr_t x) {
    (void)x;
    return serialize_token(me, seri_token_make(SERI_TOKEN_NULL));
}

static_inline_failable seri_internal_serialize_cstr_ptr(SeriSerializer* me, cstr const* x) {
    return serialize_token(me, seri_token_make(SERI_TOKEN_CSVIEW, .sv = cstr_sv(x)));
}

static_inline_failable seri_internal_serialize_str(SeriSerializer* me, const char* x) {
    return serialize_token(me, seri_token_make(SERI_TOKEN_CSVIEW, .sv = csview_from(x)));
}

static inline bool _seri_collect_errors_unconditional(SeriPatternContext* ctx, FailableResult next) {
    if (next.error) {
        add_error_to_collection(&ctx->result, next.error);
        ctx->failed = true;
    }
    return !next.error;
}

static inline bool _seri_collect_errors_if_not_inhibited(SeriPatternContext* ctx, FailableResult next) {
    if (next.error) {
        // right now this drops all errors when inhibit_errors is true, but ideally we would want to only drop "failed
        // to match" errors. If you try to deserialize an int and it's actually a string that error should get dropped
        // (or better never produced at all) because inhibit_errors indicates you're just trying one of meny arms of
        // a match or something. However if in the middle of a match you hit a parse error (for example) that should
        // still be bubbled up. I think the solution to this is to have a pattern version of all the deserialization
        // functions
        if (ctx->inhibit_errors) {
            error_base_destroy(next.error);
        } else {
            add_error_to_collection(&ctx->result, next.error);
        }
        ctx->failed = true;
    }
    return !next.error;
}

typedef struct SeriNoop { char x; } SeriNoop;
static_inline_failable seri_internal_serialize_noop(SeriSerializer* me, SeriNoop noop) {
    (void)noop; (void)me; return (FailableResult){nullptr};
}

static inline SeriNoop SERI_ETC(SeriPatternContext* me) {
    if (!me->failed) {
        _seri_collect_errors_if_not_inhibited(me, seri_deserializer_skip_to_container_end(me->de));
    }
    return (SeriNoop){0};
}

#define SERI_CTX _seri_ctx
#define SERI_EXPAND_NOTHING()

#define SERI_SERIALIZE_TOKEN(...) \
    _seri_collect_errors_unconditional(&SERI_CTX, serialize_token(SERI_CTX.ser, seri_token_make(__VA_ARGS__)))

#define SERI_SERIALIZE_VALUE(value) \
    _seri_collect_errors_unconditional(&SERI_CTX, _Generic((value), \
        nullptr_t: seri_internal_serialize_nullptr, \
        bool: seri_serialize_bool, \
        i64: seri_serialize_int, \
        i32: seri_serialize_int, \
        u64: seri_serialize_uint, \
        u32: seri_serialize_uint, \
        double: seri_serialize_float, \
        float: seri_serialize_float, \
        const char*: seri_internal_serialize_str, \
        char*: seri_internal_serialize_str, \
        csview: seri_serialize_csview, \
        cstr: seri_serialize_cstr, \
        cstr*: seri_internal_serialize_cstr_ptr, \
        cstr const*: seri_internal_serialize_cstr_ptr, \
        SeriNoop: seri_internal_serialize_noop \
    )(SERI_CTX.ser, value));

#define SERI_SERIALIZE_LOCAL(...) error_declaration_not_allowed_in_serialized_pattern
#define SERI_DECLARE_LOCAL(type, name) type name;
#define SERI_DESERIALIZE_LOCAL(func, name) \
    if (!SERI_CTX.failed) _seri_collect_errors_if_not_inhibited(&SERI_CTX, func(SERI_CTX.de, &name));

#define SERI_SERIALIZE_bool SERI_SERIALIZE_LOCAL(
#define SERI_DECLARE_bool SERI_DECLARE_LOCAL(bool,
#define SERI_DESERIALIZE_bool SERI_DESERIALIZE_LOCAL(seri_deserialize_bool,

#define SERI_SERIALIZE_i64 DECLARATION_INVALID_IN_SERIALIZATION(
#define SERI_DECLARE_i64 SERI_DECLARE_LOCAL(i64,
#define SERI_DESERIALIZE_i64 SERI_DESERIALIZE_LOCAL(seri_deserialize_int,

#define SERI_SERIALIZE_u64 DECLARATION_INVALID_IN_SERIALIZATION(
#define SERI_DECLARE_u64 SERI_DECLARE_LOCAL(u64,
#define SERI_DESERIALIZE_u64 SERI_DESERIALIZE_LOCAL(seri_deserialize_uint,

#define SERI_SERIALIZE_double DECLARATION_INVALID_IN_SERIALIZATION(
#define SERI_DECLARE_double SERI_DECLARE_LOCAL(double,
#define SERI_DESERIALIZE_double SERI_DESERIALIZE_LOCAL(seri_deserialize_float,

#define SERI_SERIALIZE_cstr DECLARATION_INVALID_IN_SERIALIZATION(
#define SERI_DECLARE_cstr SERI_DECLARE_LOCAL(cstr,
#define SERI_DESERIALIZE_cstr SERI_DESERIALIZE_LOCAL(seri_deserialize_cstr,

#define SERI_REMOVE_OPEN_PAREN(...) __VA_ARGS__)

#define SERI_SERIALIZE_EXPR(expr) \
    CLEAT_IF(CLEAT_IS_THING(expr)) ( \
        CLEAT_IF_ELSE(CLEAT_HAS_PEREN(expr)) ( \
            CLEAT_EXPAND_CALL(CLEAT_EXPAND_CAT, SERI_SERIALIZE_, SERI_REMOVE_OPEN_PAREN expr) \
        ) ( \
            SERI_SERIALIZE_VALUE(expr) \
        ) \
    )

#define SERI_DECLARE_EXPR(expr) \
    CLEAT_IF(CLEAT_IS_THING(expr)) ( \
        CLEAT_IF(CLEAT_HAS_PEREN(expr)) ( \
            CLEAT_EXPAND_CALL(CLEAT_EXPAND_CAT, SERI_DECLARE_, SERI_REMOVE_OPEN_PAREN expr) \
        ) \
    )

#define SERI_DESERIALIZE_EXPR(expr) \
    CLEAT_IF(CLEAT_IS_THING(expr)) ( \
        CLEAT_IF_ELSE(CLEAT_HAS_PEREN(expr)) ( \
            CLEAT_EXPAND_CALL(CLEAT_EXPAND_CAT, SERI_DESERIALIZE_, SERI_REMOVE_OPEN_PAREN expr) \
        ) ( \
            SERI_SERIALIZE_VALUE(expr) \
        ) \
    )

#define SERI_SERIALIZE_VEC_SECOND_PASS(serialize_items, decl_locals, deserialize_items) \
    SERI_SERIALIZE_TOKEN(SERI_TOKEN_VEC); \
    CLEAT_EXPAND serialize_items \
    SERI_SERIALIZE_TOKEN(SERI_TOKEN_END_CONTAINER); \
    SERI_EXPAND_NOTHING(

#define SERI_DECLARE_VEC_SECOND_PASS(serialize_items, decl_locals, deserialize_items) \
    CLEAT_EXPAND decl_locals \
    SERI_EXPAND_NOTHING(

#define SERI_DESERIALIZE_VEC_SECOND_PASS(serialize_items, decl_locals, deserialize_items) \
    SERI_SERIALIZE_TOKEN(SERI_TOKEN_VEC); \
    CLEAT_EXPAND deserialize_items \
    SERI_SERIALIZE_TOKEN(SERI_TOKEN_END_CONTAINER); \
    SERI_EXPAND_NOTHING(

#define SERI_VEC_ITEM(macro, item, index) macro(item)
#define SERI_VEC_FIRST_PASS(empty, ...) VEC_SECOND_PASS( \
    (CLEAT_MAP((SERI_VEC_ITEM, SERI_SERIALIZE_EXPR), __VA_ARGS__)), \
    (CLEAT_MAP((SERI_VEC_ITEM, SERI_DECLARE_EXPR), __VA_ARGS__)), \
    (CLEAT_MAP((SERI_VEC_ITEM, SERI_DESERIALIZE_EXPR), __VA_ARGS__)) \
) ) // unbalanced
#define seri_vec SERI_VEC_FIRST_PASS(

#define SERI_SERIALIZE_MAP_SECOND_PASS(item_count_floor, serialize_items, decl_locals, deserialize_items) \
    SERI_SERIALIZE_TOKEN(SERI_TOKEN_MAP); \
    CLEAT_EXPAND serialize_items \
    SERI_SERIALIZE_TOKEN(SERI_TOKEN_END_CONTAINER); \
    SERI_EXPAND_NOTHING(

#define SERI_SERIALIZE_MAP_ITEM(key, val, index) \
    SERI_SERIALIZE_EXPR(key) \
    SERI_SERIALIZE_EXPR(val) \

#define SERI_DECLARE_MAP_SECOND_PASS(item_count_floor, serialize_items, decl_locals, deserialize_items) \
    CLEAT_EXPAND decl_locals \
    SERI_EXPAND_NOTHING(

#define SERI_DECLARE_MAP_ITEM(key, val, index) \
    SERI_DECLARE_EXPR(key) \
    SERI_DECLARE_EXPR(val) \

#define SERI_DESERIALIZE_MAP_SECOND_PASS(item_count_floor, serialize_items, decl_locals, deserialize_items) \
    if (!SERI_CTX.failed) { \
        SERI_SERIALIZE_TOKEN(SERI_TOKEN_MAP); \
        i32 _map_keys_found_count = 0; \
        [[maybe_unused]] bool _map_keys_found[item_count_floor] = {0}; \
        [[maybe_unused]] bool _map_prev_inhibit = SERI_CTX.inhibit_errors; \
        while (true) { \
            bool _map_key_found = false; \
            SeriCheckpoint _map_key_cp = seri_deserializer_checkpoint(SERI_CTX.de); \
            i32 _map_keys_expected_count = 0; \
            CLEAT_EXPAND deserialize_items \
            if (_map_keys_expected_count == _map_keys_found_count) break; \
            else if (!_map_key_found) { \
                seri_deserializer_restore(SERI_CTX.de, _map_key_cp); \
                seri_report_unknown_map_key_error(&SERI_CTX); \
                seri_deserializer_dismiss(SERI_CTX.de, _map_key_cp); \
                break; \
            } \
        } \
        SERI_SERIALIZE_TOKEN(SERI_TOKEN_END_CONTAINER); \
    } \
    SERI_EXPAND_NOTHING(

#define SERI_DESERIALIZE_MAP_ITEM(key, val, index) \
    _map_keys_expected_count++; \
    if (!_map_keys_found[index] && !_map_key_found && !SERI_CTX.failed) { \
        seri_deserializer_restore(SERI_CTX.de, _map_key_cp); \
        SERI_CTX.inhibit_errors = true; \
        SERI_DESERIALIZE_EXPR(key) \
        SERI_CTX.inhibit_errors = _map_prev_inhibit; \
        if (SERI_CTX.failed) { \
            SERI_CTX.failed = false; \
        } else { \
            seri_deserializer_dismiss(SERI_CTX.de, _map_key_cp); \
            _map_keys_found[index] = true; \
            _map_key_found = true; \
            _map_keys_found_count++; \
            SERI_DESERIALIZE_EXPR(val) \
        } \
    }

#define SERI_SERIALIZE_MAP_ITEM_SERI_ETC(...) error_etc_not_allowed_in_serialized_pattern
#define SERI_DECLARE_MAP_ITEM_SERI_ETC(...)
#define SERI_DESERIALIZE_MAP_ITEM_SERI_ETC(...) \
    if (!_map_key_found && !SERI_CTX.failed) { \
        seri_deserializer_restore(SERI_CTX.de, _map_key_cp); \
        SeriTokenTag next_tag = {0}; \
        _seri_collect_errors_unconditional(&SERI_CTX, deserialize_peek_tag(SERI_CTX.de, &next_tag)); \
        if (next_tag == SERI_TOKEN_END_CONTAINER) { \
            seri_deserializer_dismiss(SERI_CTX.de, _map_key_cp); \
            if (_map_keys_expected_count != _map_keys_found_count) { \
                seri_report_unknown_map_key_error(&SERI_CTX); \
            } \
            break; \
        } \
        _seri_collect_errors_unconditional(&SERI_CTX, seri_deserializer_skip_item(SERI_CTX.de)); \
        seri_deserializer_dismiss(SERI_CTX.de, _map_key_cp); \
        _seri_collect_errors_unconditional(&SERI_CTX, seri_deserializer_skip_item(SERI_CTX.de)); \
        _map_key_found = true; \
    } \
    if (_map_keys_expected_count == _map_keys_found_count && !SERI_CTX.failed) { \
        _seri_collect_errors_unconditional(&SERI_CTX, seri_deserializer_skip_to_container_end(SERI_CTX.de)); \
    }

#define SERI_MAP_ITEM_SERI_ETC(...)
#define SERI_MAP_ITEM_GET_KEY(key, val) key
#define SERI_MAP_ITEM_GET_VAL(key, val) val
#define SERI_MAP_ITEM(macro, item, index) \
    CLEAT_IF_ELSE(CLEAT_HAS_PEREN(item)) ( \
        macro(SERI_MAP_ITEM_GET_KEY item, SERI_MAP_ITEM_GET_VAL item, index) \
    ) ( \
        CLEAT_IF(CLEAT_IS_THING(item)) ( \
            CLEAT_EXPAND_CAT(macro##_, item) \
        ) \
    )
#define SERI_MAP_FIRST_PASS(empty, ...) MAP_SECOND_PASS( \
    CLEAT_COUNT_AT_LEAST_1(__VA_ARGS__), \
    (CLEAT_MAP((SERI_MAP_ITEM, SERI_SERIALIZE_MAP_ITEM), __VA_ARGS__)), \
    (CLEAT_MAP((SERI_MAP_ITEM, SERI_DECLARE_MAP_ITEM), __VA_ARGS__)), \
    (CLEAT_MAP((SERI_MAP_ITEM, SERI_DESERIALIZE_MAP_ITEM), __VA_ARGS__)) \
) ) // unbalanced
#define seri_map SERI_MAP_FIRST_PASS(

#define SERI_SERIALIZE_ANY error_any_not_allowed_in_serialized_pattern SERI_EXPAND_NOTHING(
#define SERI_DECLARE_ANY SERI_EXPAND_NOTHING(
#define SERI_DESERIALIZE_ANY \
    if (!SERI_CTX.failed) \
        _seri_collect_errors_if_not_inhibited(&SERI_CTX, seri_deserializer_skip_item(SERI_CTX.de)); \
    SERI_EXPAND_NOTHING(
#define seri_any (ANY)

#define seri_etc SERI_ETC(&SERI_CTX)

#define seri_serialize(target, expr) \
    for (struct { \
        FailableResult result; \
        bool try_result; \
        bool exit; \
    } _seri_loop = {0}; !_seri_loop.exit; ) \
        if (!_seri_loop.try_result) { \
            SeriPatternContext SERI_CTX = { \
                .ser = target, \
            }; \
            SERI_SERIALIZE_EXPR(expr) \
            _seri_loop.try_result = true; \
            _seri_loop.result = SERI_CTX.result; \
        } else try ((_seri_loop.exit = true, _seri_loop.result))

#define seri_deserialize(source_de, expr) \
    SERI_DECLARE_EXPR(expr) \
    for (struct { \
        FailableResult result; \
        bool try_result; \
        bool exit; \
    } _seri_loop = {0}; !_seri_loop.exit; ) \
        if (!_seri_loop.try_result) { \
            SeriPatternContext SERI_CTX = { \
                .de = source_de, \
                .matcher.serialize = seri_pattern_context_serialize, \
            }; \
            SERI_CTX.ser = &SERI_CTX.matcher; \
            SERI_DESERIALIZE_EXPR(expr) \
            _seri_loop.try_result = true; \
            _seri_loop.result = SERI_CTX.result; \
        } else try ((_seri_loop.exit = true, _seri_loop.result))
