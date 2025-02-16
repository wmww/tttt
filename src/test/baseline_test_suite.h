#pragma once

#include "baseline_test_result.h"
#include "value.h"

#include "stc/cregex.h"

#define i_key_class_names BaselineTestResult, baseline_test_result
#define i_no_clone
#define i_more
#define i_cmp(a, b) cstr_cmp(&a->source_path, &b->source_path)
#include "wrap_stc/vec.h"
#include "wrap_stc/quicksort.h"

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
void baseline_test_suite_run(BaselineTestSuite* me, bool show_errs, bool show_syms, bool show_typs);
void baseline_test_suite_save_results(BaselineTestSuite* me, csview results_path);

#define i_key_class_names BaselineTestSuite, baseline_test_suite
#define i_no_clone
#define i_no_hash
#include "wrap_stc/box.h"

BoxBaselineTestSuite load_baseline_test_suite(
    csview tests_dir,
    const char* filter_re,
    csview results_path
);
