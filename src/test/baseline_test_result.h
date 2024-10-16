#pragma once

#include "baseline_test_case.h"

typedef struct JsObject JsObject;
typedef struct BaselineTestSuite BaselineTestSuite;

typedef struct BaselineTestResult {
    cstr source_path;
    bool did_run;
    bool has_prev;
    bool errors_prev;
    bool symbols_prev;
    bool types_prev;
    bool errors_passed;
    bool symbols_passed;
    bool types_passed;
} BaselineTestResult;

BaselineTestResult make_baseline_test_result(cstr source_path);
void baseline_test_result_drop(BaselineTestResult* me);
void baseline_test_result_run(
    BaselineTestResult* me,
    BaselineTestSuite* suite,
    JsObject const* previous,
    bool show_errs,
    bool show_syms,
    bool show_typs
);
bool baseline_test_result_is_pass(BaselineTestResult const* me);
bool baseline_test_result_was_pass(BaselineTestResult const* me);
// Both *_has_new_pass() and *_has_new_fail() can be true at once, _has_new_pass() does not imply *_is_pass()
bool baseline_test_result_has_new_pass(BaselineTestResult const* me);
bool baseline_test_result_has_new_fail(BaselineTestResult const* me);
