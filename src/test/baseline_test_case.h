#pragma once

#include "error.h"

typedef struct Span Span;
typedef struct Type Type;
typedef struct JsObject JsObject;
typedef struct BaselineTestSuite BaselineTestSuite;
typedef struct LineNumberedStr LineNumberedStr;

wrap_forward_vec(LineNumberedStr, line_numbered_str);

// Get the name of a test given it's path (just strips the dir and extension). output is only valid as long as input is.
csview test_name_from_path(const char* path);

typedef struct BaselineTestCase {
    const char* source_path; // Not owned
    bool did_run;
    // We keep an owned source buffer with a czview pointing into it instead of just a cstr because errors have spans
    // that point into the source buffer. If we used a cstr it could be optimized to a small string which would then
    // change memory addresses when this struct is returned by value.
    char* owned_source;
    czview source_view;
    ErrorCtx error_ctx;
    VecLineNumberedStr symbols;
    VecLineNumberedStr types;
} BaselineTestCase;

// Never prints to stdout, only prints to stderr if noisy is true, or if it panics
BaselineTestCase run_baseline_test_case(const char* source_path, bool noisy);
void baseline_test_case_drop(BaselineTestCase* me);

// These are used to get the data into string form, so it can be compared with known correct results or serialized
void baseline_test_case_render_errors(cstr* target, BaselineTestCase const* me);
void baseline_test_case_render_symbols(cstr* target, BaselineTestCase const* me);
void baseline_test_case_render_types(cstr* target, BaselineTestCase const* me);

// These are used by traversial to assemble the test case data while the test is being run. After the test has completed
// (and run_baseline_test_case() has returned) these should no longer be called.
void baseline_test_case_add_symbol(BaselineTestCase* me, csview name, Span span, Span decl_span);
void baseline_test_case_add_symbol_with_scope(
    BaselineTestCase* me,
    csview scope,
    csview name,
    Span span,
    bool name_is_value,
    Span decl_span
);
void baseline_test_case_add_type(BaselineTestCase* me, Span span, Type const* type, bool underline);
void baseline_test_case_add_type_recursive(
    BaselineTestCase* me,
    Span outer_span,
    Span inner_span,
    Type const* type,
    bool underline
);
