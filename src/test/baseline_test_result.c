#include "baseline_test_result.h"
#include "baseline_test_suite.h"
#include "baseline_test_case.h"
#include "subprocess.h"
#include "value.h"
#include "result.h"

#include <cleat/filesystem.h>

BaselineTestResult make_baseline_test_result(cstr source_path) {
    return (BaselineTestResult){
        .source_path = source_path,
    };
}

void baseline_test_result_drop(BaselineTestResult* me) {
    cstr_drop(&my source_path);
}

static bool compare_outputs(
    csview test_name,
    const char* output_name,
    csview path_of_expected,
    csview our_result,
    bool show_diff_on_error
) {
    cstr expected = cstr_null;
    if (fs_is_reg_file(fs, path_of_expected)) {
        try (fs_read_file(fs, path_of_expected, &expected)) if_err (e) {
            // A missing or unreadable baseline just means the test component fails
            if (show_diff_on_error) {
                println(test_name, ": ", path_of_expected, " could not be read");
                error_base_eprintln(e);
            }
            current_error_destroy();
            return false;
        }
    }

    csview expected_sv = cstr_sv(&expected);
    if (csview_cmp(&our_result, &expected_sv) == 0) {
        cstr_drop(&expected);
        return true;
    } else {
        cstr result_display = cstr_d("tttt ", output_name, ":\n", our_result, "\n");
        cstr expected_display = cstr_d(path_of_expected, ":\n", expected_sv, "\n");

        if (show_diff_on_error) {
            println(test_name, ": ", output_name, " did not match:");
            run_diff_subprocess(cstr_sv(&result_display), cstr_sv(&expected_display));
        }

        c_drop(cstr, &expected, &result_display, &expected_display);
        return false;
    }
}

void baseline_test_result_run(
    BaselineTestResult* me,
    BaselineTestSuite* suite,
    JsObject const* previous,
    bool show_errs,
    bool show_syms,
    bool show_typs
) {
    Result errors_prev  = previous ? js_object_get(previous, c_sv("errors"))  : result_ok(value_boolean(false));
    Result symbols_prev = previous ? js_object_get(previous, c_sv("symbols")) : result_ok(value_boolean(false));
    Result types_prev   = previous ? js_object_get(previous, c_sv("types"))   : result_ok(value_boolean(false));

    my has_prev = previous != nullptr;
    my errors_prev = value_to_boolean(&errors_prev.value);
    my symbols_prev = value_to_boolean(&symbols_prev.value);
    my types_prev = value_to_boolean(&types_prev.value);

    result_drop(&errors_prev);
    result_drop(&symbols_prev);
    result_drop(&types_prev);

    bool noisy = show_errs || show_syms || show_typs;
    csview name = test_name_from_path(cstr_sv(&my source_path));
    cstr errors_path = cstr_d(suite->tests_dir, "/baselines/reference/", name, ".errors.txt");
    cstr symbols_path = cstr_d(suite->tests_dir, "/baselines/reference/", name, ".symbols");
    cstr types_path = cstr_d(suite->tests_dir, "/baselines/reference/", name, ".types");

    // It's ok if errors file doesn't exist

    if (!fs_is_reg_file(fs, cstr_sv(&symbols_path))) {
        if (noisy) {
            eprintln(color_error, name, ":", color_normal, " ", symbols_path, " does not exist");
        }
        goto finish;
    }

    if (!fs_is_reg_file(fs, cstr_sv(&types_path))) {
        if (noisy) {
            eprintln(color_error, name, ":", color_normal, " ", types_path, " does not exist");
        }
        goto finish;
    }

    BaselineTestCase test_case = run_baseline_test_case(&suite->cache, cstr_sv(&my source_path), noisy);
    my did_run = test_case.did_run;

    if (my did_run) {
        cstr our_result = cstr_null;

        baseline_test_case_render_errors(&our_result, &test_case);
        my errors_passed = compare_outputs(
            name,
            "errors",
            cstr_sv(&errors_path),
            cstr_sv(&our_result),
            show_errs
        );
        cstr_clear(&our_result);

        baseline_test_case_render_symbols(&our_result, &test_case);
        my symbols_passed = compare_outputs(
            name,
            "symbols",
            cstr_sv(&symbols_path),
            cstr_sv(&our_result),
            show_syms
        );
        cstr_clear(&our_result);

        baseline_test_case_render_types(&our_result, &test_case);
        my types_passed = compare_outputs(
            name,
            "types",
            cstr_sv(&types_path),
            cstr_sv(&our_result),
            show_typs
        );
        cstr_drop(&our_result);
    }

    baseline_test_case_drop(&test_case);

finish:
    c_drop(cstr,
        &errors_path,
        &symbols_path,
        &types_path,
    );
}

bool baseline_test_result_is_pass(BaselineTestResult const* me) {
    return my errors_passed && my symbols_passed && my types_passed;
}

bool baseline_test_result_was_pass(BaselineTestResult const* me) {
    return my errors_prev && my symbols_prev && my types_prev;
}

bool baseline_test_result_has_new_pass(BaselineTestResult const* me) {
    return (my errors_passed  && (!my has_prev || !my errors_prev )) ||
           (my symbols_passed && (!my has_prev || !my symbols_prev)) ||
           (my types_passed   && (!my has_prev || !my types_prev  ));
}

bool baseline_test_result_has_new_fail(BaselineTestResult const* me) {
    return (!my errors_passed  && my errors_prev ) ||
           (!my symbols_passed && my symbols_prev) ||
           (!my types_passed   && my types_prev  );
}
