// This file has what you need to implement a custom test runner/test output. If you're just writing and running tests
// normally the default runner and stdout output should be fine.

#pragma once

#include "test.h"

typedef struct TestData {
    cstr module;
    cstr name;
    TestImpl impl;
    TestDrop drop;
    void* data;
} TestData;

TestData test_data_clone(TestData me);
void test_data_drop(TestData* me);

typedef struct TestFailure {
    cstr message;
    cstr expected;
    cstr actual;
    const char* file;
    int line;
} TestFailure;

void test_failure_drop(TestFailure* me);
TestFailure test_failure_clone(TestFailure me);

#define g_class_key TestFailure, test_failure
#include "generic/vec.h"

typedef struct TestOutput TestOutput;
typedef struct TestOutput {
    void (*run_started)(void* data);
    void (*module_started)(void* data, csview module);
    void (*test_started)(void* data, TestData const* test);
    // If this function is implemented it must take ownership of failures
    void (*test_finished)(void* data, TestData const* test, bool is_pass, VecTestFailure failures);
    void (*module_finished)(void* data, csview module);
    void (*run_finished)(void* data, u64 passed, u64 failed, u64 skipped);
    void* data;
} TestOutput;

TestOutput* make_stdout_test_output();
void drop_stdout_test_output(TestOutput* data);

typedef struct {
    // Regex filter. Only matching tests will be run. Must be a fullmatch with either module, name, or module::name.
    // Can be null.
    const char* filter;
    // Reports status of ongoing test run, defaults to make_stdout_test_output()
    TestOutput* output;
} RunTestsArgs;
// Returns true on success false on failure
bool run_tests(RunTestsArgs args);
void drop_test_modules();
