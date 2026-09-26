#include "baseline_test_result.h"
#include "baseline_test_suite.h"
#include "baseline_test_case.h"
#include "subprocess.h"
#include "value.h"
#include "result.h"

#include <cleat/filesystem.h>

BaselineTestResult make_baseline_test_result(cstr source_path, cstr config) {
    csview base = test_name_from_path(cstr_sv(&source_path));
    return (BaselineTestResult){
        .name = cstr_size(&config) ? cstr_d(base, "(", config, ")") : cstr_from_sv(base),
        .source_path = source_path,
        .config = config,
    };
}

void baseline_test_result_drop(BaselineTestResult* me) {
    cstr_drop(&my source_path);
    cstr_drop(&my config);
    cstr_drop(&my name);
}

static bool is_underline_line(csview line) {
    csview t = csview_trim_whitespace(line);
    if (t.size < 2 || t.buf[0] != '>') return false;
    isize colon = csview_find_sv(t, c_sv(" : "));
    if (colon == c_NPOS) return false;
    for (isize i = 1; i < colon; i++) if (t.buf[i] != ' ') return false;
    for (isize i = colon + 3; i < t.size; i++) if (t.buf[i] != '^' && t.buf[i] != ' ') return false;
    return true;
}

static cstr without_underlines(csview text) {
    cstr ret = cstr_null;
    for_each (i, lines_sv, text) {
        if (is_underline_line(i)) continue;
        cstr_append_sv(&ret, i);
        cstr_append_lit(&ret, "\n");
    }
    return ret;
}

// tsc's harness puts some of its checker's counters (relation cache sizes, type and instantiation counts, rounded) at
// the top of a types baseline when they're big (harnessIO.ts generateBaseLine). They measure tsc's implementation, not
// the types, so they aren't compared: the block goes from the expected baseline
static csview perf_stats_block(csview expected) {
    csview marker = c_sv("=== Performance Stats ===\n");
    isize_t start = csview_find_sv(expected, marker);
    if (start == c_NPOS) return (csview){0};
    csview rest = csview_slice(expected, start, expected.size);
    isize_t end = csview_find_sv(rest, c_sv("\n\n"));
    if (end == c_NPOS) return (csview){0};
    return csview_slice(expected, start, start + end + 2);
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
    csview stats = strcmp(output_name, "types") == 0 ? perf_stats_block(expected_sv) : (csview){0};
    if (stats.size) {
        cstr without = cstr_from_sv(csview_slice(expected_sv, 0, stats.buf - expected_sv.buf));
        cstr_append_sv(&without, csview_slice(expected_sv, stats.buf + stats.size - expected_sv.buf, expected_sv.size));
        cstr_drop(&expected);
        expected = without;
        expected_sv = cstr_sv(&expected);
    }
    if (getenv("TTTT_IGNORE_UNDERLINES")) {
        // Measurement: how the types component would do if the `^^^` underline lines (tsc's declaration emitter
        // marking which parts of a printed type it synthesised, see agents/plans/07_underlines.md) were right
        cstr ours = without_underlines(our_result);
        cstr theirs = without_underlines(expected_sv);
        bool eq = cstr_eq(&ours, &theirs);
        c_drop(cstr, &ours, &theirs);
        if (eq) {
            cstr_drop(&expected);
            return true;
        }
    }
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

static void dump_output(BaselineTestSuite* suite, csview name, const char* extension, csview contents) {
    if (!csview_size(suite->dump_dir)) return;
    cstr path = cstr_d(suite->dump_dir, "/", name, extension);
    try (fs_write_file(fs, cstr_sv(&path), contents)) or_log(error);
    cstr_drop(&path);
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
    csview name = cstr_sv(&my name);
    cstr errors_path = cstr_d(suite->tests_dir, "/baselines/reference/", name, ".errors.txt");
    cstr symbols_path = cstr_d(suite->tests_dir, "/baselines/reference/", name, ".symbols");
    cstr types_path = cstr_d(suite->tests_dir, "/baselines/reference/", name, ".types");

    // It's ok if errors file doesn't exist, and with @noTypesAndSymbols the other two don't either. Whether a test
    // has that is only known after loading it, so a test without baselines runs and is then dropped if it turns out
    // it should have had them (a handful of tests tsc itself doesn't produce baselines for)
    bool has_baselines = fs_is_reg_file(fs, cstr_sv(&symbols_path)) && fs_is_reg_file(fs, cstr_sv(&types_path));

    bool dumping = csview_size(suite->dump_dir) != 0;
    if (dumping) {
        // Lets whatever reads our stderr attribute the log lines that follow to a test
        eprintln("### TEST ", name);
    }

    BaselineTestCase test_case = run_baseline_test_case(
        &suite->cache, cstr_sv(&my source_path), cstr_sv(&my config), noisy, dumping);
    my errors_only = test_case.no_types_and_symbols;
    my did_run = test_case.did_run && (has_baselines || my errors_only);
    if (test_case.did_run && !my did_run && noisy) {
        eprintln(color_error, name, ":", color_normal, " ", symbols_path, " or ", types_path, " does not exist");
    }

    if (my did_run) {
        cstr our_result = cstr_null;

        baseline_test_case_render_errors(&our_result, &test_case);
        dump_output(suite, name, ".errors.txt", cstr_sv(&our_result));
        my errors_passed = compare_outputs(
            name,
            "errors",
            cstr_sv(&errors_path),
            cstr_sv(&our_result),
            show_errs
        );
        cstr_drop(&our_result);
    }

    if (my did_run && my errors_only) {
        my symbols_passed = true;
        my types_passed = true;
    } else if (my did_run) {
        cstr our_result = cstr_null;

        baseline_test_case_render_symbols(&our_result, &test_case);
        dump_output(suite, name, ".symbols", cstr_sv(&our_result));
        my symbols_passed = compare_outputs(
            name,
            "symbols",
            cstr_sv(&symbols_path),
            cstr_sv(&our_result),
            show_syms
        );
        cstr_clear(&our_result);

        baseline_test_case_render_types(&our_result, &test_case, fs_is_reg_file(fs, cstr_sv(&errors_path)));
        dump_output(suite, name, ".types", cstr_sv(&our_result));
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
