#pragma once

#include "baseline_test_case.h"
#include "value.h"

#include "stc/cregex.h"

#define i_key_class_names BaselineTestCase, baseline_test_case
#define i_no_clone
#define i_more
#define i_cmp(a, b) cstr_cmp(&a->source_path, &b->source_path)
#include "wrap_stc/vec.h"
#include "wrap_stc/quicksort.h"

typedef struct BaselineTestSuite {
    const char* tests_dir;
    bool has_filter;
    cregex filter;
    ArcJsObject prev_results;
    VecBaselineTestCase cases;
} BaselineTestSuite;

void baseline_test_suite_drop(BaselineTestSuite* me);
void baseline_test_suite_run(BaselineTestSuite* me, bool noisy);
void baseline_test_suite_save_results(BaselineTestSuite* me, const char* results_path);

#define i_key_class_names BaselineTestSuite, baseline_test_suite
#define i_no_clone
#define i_no_hash
#include "wrap_stc/box.h"

BoxBaselineTestSuite load_baseline_test_suite(
    const char* tests_dir,
    const char* filter_re,
    const char* results_path
);
