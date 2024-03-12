#include "baseline_test_suite.h"
#include "baseline_test_case.h"
#include "filesystem.h"
#include "string_helpers.h"

const char* const default_tests_dir = "../TypeScript/tests";
const char* tests_dir = default_tests_dir;
const char* test_filter = "";

static void show_help(FILE* target) {
    fprintf(target, "tttt test runner\n\n\
  -h, --help                show this help message\n\n\
  -d, --dir [PATH]          path to TypeScript tests directory\n\
                            (%s by default)\n\n\
  -f, --filter [FILTER]     regex of tests to run, case insensitive\n\
\n", default_tests_dir);
}

static bool parse_args(int argc, const char** argv) {
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-h") || streq(argv[i], "--help")) {
            show_help(stdout);
            exit(0);
        } else if (streq(argv[i], "-d") || streq(argv[i], "--dir")) {
            i++;
            if (i < argc && *argv[i] != '-') {
                tests_dir = argv[i];
            } else {
                fprintf(stderr, CLR_ERR "Expected argument after %s\n\n" CLR0, argv[i-1]);
                show_help(stderr);
                return false;
            }
        } else if (streq(argv[i], "-f") || streq(argv[i], "--filter")) {
            i++;
            if (i < argc && *argv[i] != '-') {
                test_filter = argv[i];
            } else {
                fprintf(stderr, CLR_ERR "Expected argument after %s\n\n" CLR0, argv[i-1]);
                show_help(stderr);
                return false;
            }
        } else {
            fprintf(stderr, CLR_ERR "Unexpected argument %s\n\n" CLR0, argv[i]);
            show_help(stderr);
            return false;
        }
    }

    if (!path_is_directory(tests_dir)) {
        fprintf(stderr, CLR_ERR "%s is not a directory\n" CLR0, tests_dir);
        return false;
    }

    return true;
}

int main(int argc, const char** argv) {
    if (!parse_args(argc, argv)) {
        return 1;
    }

    box_BaselineTestSuite suite = load_baseline_test_suite(tests_dir, test_filter);
    BaselineTestSuite_run(suite.get);
    box_BaselineTestSuite_drop(&suite);

    return 0;
}
