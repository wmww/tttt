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
csview dump_dir = c_sv_const_init("");

static void show_help(FILE* target) {
    print_display(target, display_sequence(PRINT_DISPLAY,
        "tttt test runner\n\n"
        "  -h, --help                show this help message\n\n"
        "  --dir [PATH]              path to TypeScript tests directory\n"
        "                            (default = ", default_tests_dir, ")\n\n"
        "  --summary                 only show tests who's results changed\n\n"
        "  -f, --filter [FILTER]     regex of tests to run, case insensitive\n\n"
        "  -s, --save                save test results to results path\n\n"
        "  --results-path [PATH]     path to file where test results are loaded/saved\n"
        "                            (default = ", default_results_path, ")\n\n"
        "  --dump [DIR]              write tttt's rendered errors/symbols/types for every test into DIR and\n"
        "                            leave checker log output on (used by ./failstats.py)\n\n"
        "  -d, --diffs               if to show all diffs for failing tests\n\n"
        "  --err                     show errors diffs for failing tests\n\n"
        "  --sym                     show symbols diffs for failing tests\n\n"
        "  --typ                     show types diffs for failing tests\n\n"
    ));
}

// Reads the value that follows a flag, failing if the next argument is missing or is itself a flag. Advances *i past
// the value it consumed
static_failable next_arg_value(int argc, const char** argv, int* i, returns const char*) {
    const char* flag = argv[*i];
    (*i)++;
    if (*i >= argc || *argv[*i] == '-') {
        return err_d("Expected argument after ", flag);
    }
    return ok(argv[*i]);
}

// Fails on a bad command line, in which case main() prints the message along with the help text. Exits directly for
// --help, which isn't an error
static_failable parse_args(int argc, const char** argv) {
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-h") || streq(argv[i], "--help")) {
            show_help(stdout);
            exit(0);
        } else if (streq(argv[i], "--dir")) {
            try (const char* dir, next_arg_value(argc, argv, &i, &dir)) bubble;
            tests_dir = csview_from(dir);
        } else if (streq(argv[i], "-f") || streq(argv[i], "--filter")) {
            try (const char* filter, next_arg_value(argc, argv, &i, &filter)) bubble;
            test_filter = filter;
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
            try (const char* path, next_arg_value(argc, argv, &i, &path)) bubble;
            results_path = csview_from(path);
        } else if (streq(argv[i], "--dump")) {
            try (const char* dir, next_arg_value(argc, argv, &i, &dir)) bubble;
            dump_dir = csview_from(dir);
        } else if (streq(argv[i], "-s") || streq(argv[i], "--save")) {
            save_results = true;
        } else {
            return err_d("Unexpected argument ", argv[i]);
        }
    }

    if (!fs_is_directory(fs, tests_dir)) {
        return err_d(tests_dir, " is not a directory");
    }
    if (csview_size(dump_dir) && !fs_is_directory(fs, dump_dir)) {
        return err_d(dump_dir, " is not a directory");
    }
    if (!fs_is_reg_file(fs, results_path)) {
        return err_d(results_path, " is not a file");
    }

    return ok;
}

int main(int argc, const char** argv) {
    try (parse_args(argc, argv)) if_err (e) {
        error_base_eprintln(e);
        current_error_destroy();
        eprintln();
        show_help(stderr);
        return 1;
    }

    try (
        BoxBaselineTestSuite suite,
        load_baseline_test_suite(tests_dir, test_filter, results_path, summary_only, &suite)
    ) if_err (e) {
        error_base_eprintln(e);
        current_error_destroy();
        return 1;
    }

    suite.get->dump_dir = dump_dir;
    baseline_test_suite_run(suite.get, summary_only, show_errs, show_syms, show_typs);

    if (save_results) {
        baseline_test_suite_save_results(suite.get, results_path);
    }

#ifdef DEBUG_BUILD
    box_baseline_test_suite_drop(&suite);
#endif

    return 0;
}
