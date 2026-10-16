#pragma once

#include "baseline_test_case.h"

typedef struct JsObject JsObject;
typedef struct BaselineTestSuite BaselineTestSuite;

typedef struct BaselineTestResult {
    cstr source_path;
    cstr config; // One of baseline_test_configurations(), usually empty
    cstr name;   // What the baselines are called: the file's name, plus "(config)" if there is one
    bool did_run;
    bool skipped; // A configuration tsgo's harness skips (BaselineTestCase.skipped): not counted, not saved
    bool errors_only; // @noTypesAndSymbols: only the errors are compared, the other two count as passed
    bool has_prev;
    bool errors_prev;
    bool symbols_prev;
    bool types_prev;
    bool errors_passed;
    bool symbols_passed;
    bool types_passed;
    // The dts component (plan 64): the declaration files in the `.js` baseline, for tests that have some and whose
    // inputs are TypeScript (JavaScript declaration emit isn't done: `dts_js` tests are counted apart). Not part of
    // baseline_test_result_is_pass() yet
    bool has_dts;
    bool dts_js;
    bool has_dts_prev; // The saved results have a dts entry
    bool dts_prev;
    bool dts_passed; // (For a dts_js test: its JavaScript declaration outputs match, counted apart)
} BaselineTestResult;

extern bool baseline_show_dts; // baseline_tests --dts: show the dts component's diffs
extern bool baseline_dts_js; // baseline_tests --dts-js: show the diffs of the tests with JavaScript inputs instead

// Takes ownership of both
BaselineTestResult make_baseline_test_result(cstr source_path, cstr config);
void baseline_test_result_drop(BaselineTestResult* me);
static inline int baseline_test_result_cmp(BaselineTestResult const* a, BaselineTestResult const* b) {
    int by_path = cstr_cmp(&a->source_path, &b->source_path);
    return by_path ? by_path : cstr_cmp(&a->config, &b->config);
}
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
