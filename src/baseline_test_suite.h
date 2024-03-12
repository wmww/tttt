#pragma once

#include "prelude.h"
#include "baseline_test_case.h"

#define i_key_class BaselineTestCase
#define i_no_clone
#include "stc/vec.h"

typedef struct BaselineTestSuite {
    const char* tests_dir;
    vec_BaselineTestCase cases;
} BaselineTestSuite;

BaselineTestSuite load_baseline_test_suite(const char* tests_dir);
void BaselineTestSuite_drop(BaselineTestSuite* self);
void BaselineTestSuite_run(BaselineTestSuite* self);
