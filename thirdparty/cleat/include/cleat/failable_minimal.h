// This header is designed to be included by headers, see failable.h for the full error handling API and
// docs/error_handling.md for documentation

#pragma once

#include "types.h"

typedef struct FailableResult FailableResult;
typedef struct ErrorVt ErrorVt;
typedef struct ErrorBase ErrorBase;
typedef struct StringError StringError;
typedef struct DisplayCtx DisplayCtx;
typedef struct csview csview;

struct FailableResult {
    // nullptr if the function succeeds
    // error must be destroyed if non-null
    // "returned" output parameter (if present) is only valid if error is non-null
    ErrorBase* error;
};
// DisplayCtx failable_result_display(FailableResult const* result, DisplayCtx d);
// DisplayCtx failable_result_consume_display(FailableResult result, DisplayCtx d);

struct ErrorVt {
    const char* type_name;
    ErrorVt const* super;
    DisplayCtx (*display)(ErrorBase const* error, DisplayCtx d);
    void (*drop)(ErrorBase* error);
};

struct ErrorBase {
    ErrorVt const* vt;
};
void error_base_destroy(ErrorBase* me);
DisplayCtx error_base_display(ErrorBase const* error, DisplayCtx d);
cstr error_base_get_message(ErrorBase const* error); // Just wraps error_base_display()
void error_base_eprintln(ErrorBase const* error); // Just wraps eprintln(error)

#define failable [[nodiscard]] FailableResult
#define static_failable [[nodiscard]] static FailableResult
#define static_inline_failable [[nodiscard]] static inline FailableResult

// Parentheses are intentionally unbalanced. The open parentheses from `returns` matches against the close parentheses
// at the end of the function's argument list. _FAILABLE_RETURNS_INTERNAL adds back the closing parentheses so it all
// parses in the end.
#define returns _FAILABLE_RETURNS_INTERNAL(
#define _FAILABLE_RETURNS_INTERNAL(...) __VA_ARGS__* const _OUTPUT_PARAM)

// Create a string error with a format string
failable err_fmt(const char* fmt, ...) __attribute__((format (printf, 1, 2)));

// Aborts the program with the given error (error should not be null)
__attribute__((noreturn))
void error_base_panic_internal(ErrorBase const* error, const char* file, long long line);
#define panic_from_error(error) error_base_panic_internal(error, __FILE__, __LINE__)

// Aborts the program if the given result is an error
void failable_result_check_internal(FailableResult result, const char* file, long long line);
#define check_result(result) failable_result_check_internal(result, __FILE__, __LINE__);

// If result.error is non-null, destroys it and sets it to null
void failable_result_drop(FailableResult* result);
