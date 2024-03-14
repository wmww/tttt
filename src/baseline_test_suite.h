#pragma once

#include "baseline_test_case.h"
#include "value.h"

#include "stc/cregex.h"

#define i_key_class BaselineTestCase
#define i_no_clone
#define i_more
#define i_cmp(a, b) cstr_cmp(&a->source_path, &b->source_path)
#include "stc/vec.h"
#include "stc/algo/quicksort.h"

typedef struct BaselineTestSuite {
    const char* tests_dir;
    bool has_filter;
    cregex filter;
    arc_Object prev_results;
    vec_BaselineTestCase cases;
} BaselineTestSuite;

void BaselineTestSuite_drop(BaselineTestSuite* self);
void BaselineTestSuite_run(BaselineTestSuite* self, bool noisy);
void BaselineTestSuite_save_results(BaselineTestSuite* self, const char* results_path);

#define i_key_class BaselineTestSuite
#define i_no_clone
#define i_no_hash
#include "stc/box.h"

box_BaselineTestSuite load_baseline_test_suite(
    const char* tests_dir,
    const char* filter_re,
    const char* results_path
);
