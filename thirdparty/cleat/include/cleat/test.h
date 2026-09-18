#pragma once

#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stc/csview.h>
#include <stc/cstr.h>
#include "types.h"

typedef struct TestCtx TestCtx;
typedef void (*TestImpl)(TestCtx* test_ctx, void const* test_data);
typedef void (*TestDrop)(void* test_data);
#define CLEAT_TEST_CONCAT2(a, b) a##b
#define CLEAT_TEST_CONCAT(a, b) CLEAT_TEST_CONCAT2(a, b)

#ifndef auto_decl
// Can be replaced with auto once we're sure we don't need clang 17 support
#define auto_decl(name, ...) typeof(__VA_ARGS__) name = __VA_ARGS__
#endif

// --- Test helpers ---

// Create a test with the given name. name must be a valid C identifier. Test body should be in a following code block.
#define test(name) \
    static void test_##name(TestCtx*, void const*); \
    static void __attribute__((constructor)) register_##name(void) { \
        register_test(test_module_from_file(__FILE__), c_sv(#name), test_##name, nullptr, nullptr); \
    } \
    static void test_##name([[maybe_unused]] TestCtx* test_ctx, [[maybe_unused]] void const* _null_test_data)

// The following block will be called at startup. It may call register_test() any number of times. Useful if the
// number/name of tests can't be known at compile time
#define register_dynamic_tests \
    static void __attribute__((constructor)) CLEAT_TEST_CONCAT(register_test_, __COUNTER__)(void)

// Fail the test with the given format string/arguments
#define test_fail_strings(message, expected, actual) \
    test_fail_internal(test_ctx, __FILE__, __LINE__, message, expected, actual)
#define test_fail_message(...) \
    test_fail_strings(cstr_from_fmt(__VA_ARGS__), cstr_init(), cstr_init())
#define test_fail_expectation(expr, expected, ...) \
    test_fail_strings(cstr_lit(#expr), cstr_lit(#expected), cstr_from_fmt(__VA_ARGS__))
#define test_fail_expectation_s(expr, expected, actual) \
    test_fail_strings(cstr_lit(#expr), cstr_lit(#expected), actual)
#define test_fail_with_error(expr, er) test_fail_strings(cstr_from(expr), cstr_lit("ok"), error_base_get_message(er))

// Run the following block of code in a fork. Afterwords the stdout, stderr and exit code will be available as
// captured.out, captured.err and captured.exit
#define test_capture for (; test_fork_internal(test_ctx); exit(0))
#define captured test_ctx_get_captured(test_ctx)

// --- Test assertions ---

#define test_true(a) if (!(a)) test_fail_message("%s", #a " is false")

#define test_false(a) if (a) test_fail_message("%s", #a " is true")

#define test_ptr_eq(expr, expected) do { \
    auto_decl(_tmp_value, expr); \
    if (_tmp_value != expected) test_fail_expectation(expr, expected, "%p", (void*)_tmp_value); \
} while (0)

#define test_null(a) test_ptr_eq(a, nullptr)
#define test_nonnull(a) if (a == nullptr) test_fail_message("%s", #a " is null")

#define test_int_eq(expr, expected) do { \
    auto_decl(_tmp_value, expr); \
    if (_tmp_value != expected) test_fail_expectation(expr, expected, "%zd", (ptrdiff_t)_tmp_value); \
} while (0)

#define test_str_eq(expr, expected) do { \
    const char* _tmp_value = expr; \
    if (strcmp(_tmp_value, expected) != 0) \
        test_fail_expectation_s(expr, expected, test_format_string(csview_from(_tmp_value))); \
} while (0)

#define test_sv_eq(expr, expected) do { \
    csview _tmp_value = expr; \
    if (!csview_equals(_tmp_value, expected)) \
        test_fail_expectation_s(expr, expected, test_format_string(_tmp_value)); \
} while (0)

#define test_cstr_eq(expr, expected) do { \
    cstr _tmp_value = expr; \
    if (!cstr_equals(&_tmp_value, expected)) \
        test_fail_expectation_s(expr, expected, test_format_string(cstr_sv(&_tmp_value))); \
    cstr_drop(&_tmp_value); \
} while (0)

#define test_regex_matches(expr, regex) do { \
    csview _tmp_value = expr; \
    if (!regex_matches_internal(regex, _tmp_value)) \
        test_fail_internal(test_ctx, __FILE__, __LINE__, cstr_lit(#expr), \
            test_format_regex(c_sv(regex)), \
            test_format_string(_tmp_value)); \
} while (0)

#define test_captured_eq(expected_out, expected_err, expected_exit) do { \
    test_str_eq(captured.err, expected_err); \
    test_str_eq(captured.out, expected_out); \
    test_int_eq(captured.exit, expected_exit); \
} while (0)

#undef try
#define try(...) c_MACRO_OVERLOAD(_TEST_TRY, __VA_ARGS__)
#define _TEST_TRY_INIT(expr) for (const char* _test_try_expr = #expr; _test_try_expr; _test_try_expr = nullptr)
#define _TEST_TRY_1(expr) _TEST_TRY_INIT(expr) _FAILABLE_TRY_1(expr)
#define _TEST_TRY_2(init, expr) init FAILABLE_TRY_INITIALIZER; _TEST_TRY_INIT(expr) _TEST_TRY_1(expr)

#define test_ok if_err (e) do { \
    test_fail_with_error(_test_try_expr, e); \
} while (0)

#define test_ok_or_bail if_err (e) do { \
    test_fail_with_error(_test_try_expr, e); \
    current_error_destroy(); \
    return; \
} while (0)

#define test_err_custom(type, expected, is_pass) if_ok { \
    test_fail_strings(cstr_from(_test_try_expr), cstr_from_fmt("%s(%s)", #type, #expected), cstr_lit("ok")); \
} if_err (e) do { \
    cstr _actual_message = error_base_get_message(e); \
    if (!error_downcast(e, &type##Vt) || !(is_pass)) { \
        test_fail_strings(cstr_from(_test_try_expr), cstr_from_fmt("%s: %s", #type, expected), _actual_message); \
    } else { \
        cstr_drop(&_actual_message); \
    } \
} while (0)

#define test_err(type, expected) test_err_custom(type, expected, cstr_equals(&_actual_message, expected))
#define test_err_regex(type, regex) test_err_custom(type, regex, regex_matches_internal(regex, cstr_sv(&_actual_message)))

// Strips prefixes (like "../") and suffexes (like ".c") from the __FILE__ macro to give a nice clean module name
csview test_module_from_file(const char* file);

// Adds a test to be run. Must be called at startup (generally within a register_tests block or something a
// register_tests block calls). May not be called wihin a test.
// module: tests with the same module are grouped. May not contain ':' or newline. test_module_from_file(__FILE__) is
//   common, but any valid string can be used. csview does not need to remain valid after the call.
// name: the name of the test, should be unique within the module. csview does not need to remain valid after the call.
// impl: the function to run the test
// drop: the function to drop the test data (or null)
// test_data: the data to supply to impl and drop (or null). Pointer must remain valid after call
void register_test(csview module, csview name, TestImpl impl, TestDrop drop, void* test_data);

// Adds a test that always fails with the given message if run, useful for reporting errors when registering tests.
void register_test_problem(csview module, csview name, cstr message);

// --- Internal functions for use by above macros ---

cstr test_format_string(csview sv);
cstr test_format_regex(csview sv);
void test_fail_internal(TestCtx* test_ctx, const char* file, int line, cstr message, cstr expected, cstr actual);
bool test_has_failures(TestCtx* test_ctx);

typedef struct TestCtxCaptured {
    const char* out;
    const char* err;
    int exit;
} TestCtxCaptured;
bool test_fork_internal(TestCtx* test_ctx); // Returns true if in fork
TestCtxCaptured test_ctx_get_captured(TestCtx* test_ctx);

bool regex_matches_internal(const char* regex, csview value);
