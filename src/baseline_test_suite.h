#pragma once

#include "prelude.h"
#include "baseline_test_case.h"

#include "stc/cregex.h"

#define i_key_class BaselineTestCase
#define i_no_clone
#include "stc/vec.h"

typedef struct BaselineTestSuite {
    const char* tests_dir;
    bool has_filter;
    cregex filter;
    vec_BaselineTestCase cases;
    long skipped_count;
    long total_count;
} BaselineTestSuite;

void BaselineTestSuite_drop(BaselineTestSuite* self);
void BaselineTestSuite_run(BaselineTestSuite* self);

#define i_key_class BaselineTestSuite
#define i_no_clone
#define i_no_hash
#include "stc/box.h"

box_BaselineTestSuite load_baseline_test_suite(const char* tests_dir, const char* filter_re);
