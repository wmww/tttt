#pragma once

#include "types.h"

// Mirrored in string_helpers.h
#define cstr_append_lit(str, lit) cstr_append_sv(str, c_sv(lit))
#define c_sv_const_init(str) {str, c_litstrlen(str)}
#define cstr_null cstr_init()
#define csview_null csview_init()
#define c_SV c_svarg

#define my me->
#define ignore (void)
#define return_struct return (ReturnType)

__attribute__((noreturn))
void default_log_fatal_abort_sv(csview file, i64 line, csview label, csview message);
#define panic_str(message) default_log_fatal_abort_sv(c_sv(__FILE__), __LINE__, csview_null, csview_from(message))
#define panic_if_str(cond, message) if (cond) panic_str(message)
#define check(cond) if (!(cond)) default_log_fatal_abort_sv(c_sv(__FILE__), __LINE__, c_sv("check failed"), c_sv(#cond))
#define impossible default_log_fatal_abort_sv(c_sv(__FILE__), __LINE__, c_sv("impossible \\x_x/"), csview_null)

#define for_each(key, C, ...) \
    for ([[maybe_unused]] isize key##_index = 0, _##key##_started = 0; !_##key##_started;) \
    for (C##_iter key##_iter = C##_begin(__VA_ARGS__); !_##key##_started;) \
    for (auto key = C##_iter_deref(&key##_iter); \
        _##key##_started = 1, (key = C##_iter_deref(&key##_iter)), key##_iter.ref; \
        key##_index++, C##_next(&key##_iter))

#define for_each_reverse(key, C, ...) \
    for ([[maybe_unused]] isize key##_index = 0, _##key##_started = 0; !_##key##_started;) \
    for (auto _##key##_container = __VA_ARGS__; key##_index = C##_size(_##key##_container) - 1, !_##key##_started;) \
    for (C##_iter key##_iter = C##_rbegin(_##key##_container); !_##key##_started;) \
    for (auto key = C##_iter_deref(&key##_iter); \
        _##key##_started = 1, (key = C##_iter_deref(&key##_iter)), key##_iter.ref; \
        key##_index--, C##_rnext(&key##_iter))

#define for_each_kv(key, val, C, ...) \
    for ([[maybe_unused]] isize key##_index = 0, _##key##_started = 0; !_##key##_started;) \
    for (C##_iter key##_iter = C##_begin(__VA_ARGS__); !_##key##_started;) \
    for ([[maybe_unused]] auto key = &C##_iter_deref(&key##_iter)->first; !_##key##_started;) \
    for ([[maybe_unused]] auto val = &C##_iter_deref(&key##_iter)->second; \
        _##key##_started = 1, \
            (key = C##_iter_deref(&key##_iter) ? &C##_iter_deref(&key##_iter)->first : 0), \
            (val = C##_iter_deref(&key##_iter) ? &C##_iter_deref(&key##_iter)->second : 0), \
            key##_iter.ref; \
        key##_index++, C##_next(&key##_iter))

#define cleat_main \
    cstr program_path = {0}; VecCStr args = {0}; \
    static_failable wrapped_main(); \
    int main(int argc, const char** argv) { \
        if (argc > 0) program_path = cstr_from(argv[0]); \
        for (int i = 1; i < argc; i++) vec_cstr_push(&args, cstr_from(argv[i])); \
        try (wrapped_main()) if_ok { return 0; } if_err (e) { \
            cstr message = error_get_debug(e); \
            fprintf(stderr, "%s\n", cstr_str(&message)); \
            current_error_destroy(); \
            return 1; \
        } \
    } \
    static_failable wrapped_main()
