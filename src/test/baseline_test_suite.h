#pragma once

#include "baseline_test_result.h"
#include "value.h"

#include "stc/cregex.h"

#define g_class_key BaselineTestResult, baseline_test_result
#define g_no_clone
#define g_cmp(a, b) cstr_cmp(&a->source_path, &b->source_path)
#include <cleat/generic/vec.h>

typedef struct BaselineTestSuite {
    csview tests_dir;
    bool has_filter;
    cregex filter;
    cstr prev_typescript_commit;
    cstr typescript_commit;
    ArcJsObject prev_results;
    VecBaselineTestResult results;
    BaselineTestCache cache;
} BaselineTestSuite;

void baseline_test_suite_drop(BaselineTestSuite* me);
void baseline_test_suite_run(BaselineTestSuite* me, bool summary_only, bool show_errs, bool show_syms, bool show_typs);
void baseline_test_suite_save_results(BaselineTestSuite* me, csview results_path);

#define g_class_key BaselineTestSuite, baseline_test_suite
#define g_no_clone
#define g_no_hash
#include <cleat/generic/box.h>

// Compiles the filter, loads the previous run's results and collects every test below tests_dir. Fails if the filter
// isn't a valid regex, the saved results can't be read, or the test directories aren't there
failable load_baseline_test_suite(
    csview tests_dir,
    const char* filter_re,
    csview results_path,
    bool summary_only,
    returns BoxBaselineTestSuite
);
