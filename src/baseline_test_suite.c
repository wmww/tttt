#include "baseline_test_suite.h"
#include "baseline_test_case.h"

int main(int argc, const char** argv) {
    (void)argc;
    (void)argv;
    BaselineTestSuite suite = {
        .typescript_tests_dir = "/home/code/TypeScript/tests",
    };
    return run_baseline_test_case(
        &suite,
        "emitArrowFunctionsAsIs",
        "conformance/es6/arrowFunction/emitArrowFunctionsAsIs.ts"
    ) ? 1 : 0;
}
