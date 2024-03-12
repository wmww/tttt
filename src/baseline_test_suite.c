#include "baseline_test_suite.h"
#include "baseline_test_case.h"

int main(int argc, const char** argv) {
    (void)argc;
    (void)argv;
    BaselineTestSuite suite = {
        .tests_dir = "/home/code/TypeScript/tests",
    };
    BaselineTestCase test_case = make_baseline_test_case(
        &suite,
        "/home/code/TypeScript/tests/cases/conformance/es6/arrowFunction/emitArrowFunctionsAsIs.ts"
    );
    bool ret = BaselineTestCase_run(&test_case, false);
    BaselineTestCase_drop(&test_case);
    return ret ? 0 : 1;
}
