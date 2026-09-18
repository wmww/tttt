#include "baseline_test_suite.h"
#include "string_helpers.h"

#include <cleat/filesystem.h>

csview const default_tests_dir = c_sv_const_init("thirdparty/tsc/tests");
csview tests_dir = default_tests_dir;
const char* test_filter = nullptr;
csview const default_results_path = c_sv_const_init("test/baseline_results.ts");
csview results_path = default_results_path;
bool show_errs = false;
bool show_syms = false;
bool show_typs = false;
bool summary_only = false;
bool save_results = false;

static void show_help(FILE* target) {
    fprintf(target, "tttt test runner\n\n\
  -h, --help                show this help message\n\n\
  --dir [PATH]              path to TypeScript tests directory\n\
                            (default = %.*s)\n\n\
  --summary                 only show tests who's results changed\n\n\
  -f, --filter [FILTER]     regex of tests to run, case insensitive\n\n\
  -s, --save                save test results to results path\n\n\
  --results-path [PATH]     path to file where test results are loaded/saved\n\
                            (default = %.*s)\n\n\
  -d, --diffs               if to show all diffs for failing tests\n\n\
  --err                     show errors diffs for failing tests\n\n\
  --sym                     show symbols diffs for failing tests\n\n\
  --typ                     show types diffs for failing tests\n\n\
", c_SV(default_tests_dir), c_SV(default_results_path));
}

static bool parse_args(int argc, const char** argv) {
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-h") || streq(argv[i], "--help")) {
            show_help(stdout);
            exit(0);
        } else if (streq(argv[i], "--dir")) {
            i++;
            if (i < argc && *argv[i] != '-') {
                tests_dir = csview_from(argv[i]);
            } else {
                fprintf(stderr, "%sExpected argument after %s%s\n\n", color_error, argv[i-1], color_normal);
                show_help(stderr);
                return false;
            }
        } else if (streq(argv[i], "-f") || streq(argv[i], "--filter")) {
            i++;
            if (i < argc && *argv[i] != '-') {
                test_filter = argv[i];
            } else {
                fprintf(stderr, "%sExpected argument after %s%s\n\n", color_error, argv[i-1], color_normal);
                show_help(stderr);
                return false;
            }
        } else if (streq(argv[i], "--summary")) {
            summary_only = true;
        } else if (streq(argv[i], "-d") || streq(argv[i], "--diffs")) {
            show_errs = true;
            show_syms = true;
            show_typs = true;
        } else if (streq(argv[i], "--err")) {
            show_errs = true;
        } else if (streq(argv[i], "--sym")) {
            show_syms = true;
        } else if (streq(argv[i], "--typ")) {
            show_typs = true;
        } else if (streq(argv[i], "--results-path")) {
            i++;
            if (i < argc && *argv[i] != '-') {
                results_path = csview_from(argv[i]);
            } else {
                fprintf(stderr, "%sExpected argument after %s%s\n\n", color_error, argv[i-1], color_normal);
                show_help(stderr);
                return false;
            }
        } else if (streq(argv[i], "-s") || streq(argv[i], "--save")) {
            save_results = true;
        } else {
            fprintf(stderr, "%sUnexpected argument %s%s\n\n", color_error, argv[i], color_normal);
            show_help(stderr);
            return false;
        }
    }

    if (!fs_is_directory(fs, tests_dir)) {
        fprintf(stderr, "%s%.*s is not a directory%s\n", color_error, c_SV(tests_dir), color_normal);
        return false;
    }
    if (!fs_is_reg_file(fs, results_path)) {
        fprintf(stderr, "%s%.*s is not a file%s\n", color_error, c_SV(results_path), color_normal);
        return false;
    }

    return true;
}

int main(int argc, const char** argv) {
    if (!parse_args(argc, argv)) {
        return 1;
    }

    BoxBaselineTestSuite suite = load_baseline_test_suite(tests_dir, test_filter, results_path, summary_only);
    baseline_test_suite_run(suite.get, summary_only, show_errs, show_syms, show_typs);

    if (save_results) {
        baseline_test_suite_save_results(suite.get, results_path);
    }

#ifdef DEBUG_BUILD
    box_baseline_test_suite_drop(&suite);
#endif

    return 0;
}
