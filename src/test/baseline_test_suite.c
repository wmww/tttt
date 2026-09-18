#include "baseline_test_suite.h"
#include "baseline_test_case.h"
#include "baseline_test_result.h"
#include "program.h"
#include "engine.h"
#include "value.h"
#include "result.h"

#include <cleat/filesystem.h>
#include <cleat/subproc.h>
#include "stc/cregex.h"
#include <linux/limits.h>

typedef BaselineTestResult const* BaselineTestResultCPtr;
#define g_key BaselineTestResultCPtr, baseline_test_result_c_ptr
#include <cleat/generic/vec.h>

static bool test_passes_filter(BaselineTestSuite* me, csview name) {
    if (my has_filter) {
        // A null matches array is fine, we only care if the whole name matches and not about capture groups
        return cregex_match_sv(&my filter, name, nullptr, CREG_FULLMATCH) == CREG_OK;
    } else {
        return true;
    }
}

static void show_collected_test_counts(BaselineTestSuite* me) {
    printf(
        "\rCollected %s%ld%s tests      ",
        color_good,
        vec_baseline_test_result_size(&my results),
        color_normal
    );
}

// Recursively collects every .ts file below the given directory. Directories that can't be listed are silently
// skipped, as was the case before (listdir has no way to report an error anyway)
static void load_dir(BaselineTestSuite* me, csview dir_path) {
    for_each (item, fs_listdir, fs, dir_path) {
        if (fs_is_directory(fs, item.path)) {
            load_dir(me, item.path);
        } else if (fs_is_reg_file(fs, item.path) && csview_ends_with(item.path, ".ts")) {
            vec_baseline_test_result_push(
                &my results,
                make_baseline_test_result(cstr_from_sv(item.path))
            );

            if (vec_baseline_test_result_size(&my results) % 100 == 0) {
                show_collected_test_counts(me);
            }
        }
    }
}

static void load_subdir(BaselineTestSuite* me, csview subdir) {
    cstr full_dir = cstr_from_fmt("%.*s/cases/%.*s", c_SV(my tests_dir), c_SV(subdir));
    if (fs_is_directory(fs, cstr_sv(&full_dir))) {
        load_dir(me, cstr_sv(&full_dir));
    } else {
        fprintf(stderr, "%s%s is not a directory%s\n", color_error, cstr_str(&full_dir), color_normal);
    }
    cstr_drop(&full_dir);
}

void baseline_test_suite_drop(BaselineTestSuite* me) {
    arc_js_object_drop(&my prev_results);
    cregex_drop(&my filter);
    cstr_drop(&my prev_typescript_commit);
    cstr_drop(&my typescript_commit);
    vec_baseline_test_result_drop(&my results);
    baseline_test_cache_drop(&my cache);
}

static const char* color_for_state(bool is_pass, bool highlight) {
    if (highlight) {
        return is_pass ? color_highlighted_good : color_highlighted_error;
    } else {
        return is_pass ? color_good : color_error;
    }
}

static void append_test_subresult_to_cstr(cstr* target, const char* name, bool passed, bool prev, bool has_prev) {
    bool changed = !has_prev || passed != prev;
    cstr_append_fmt(target, "%s %s %s", color_for_state(passed, changed), passed ? " - " : name, color_normal);
}

static void append_test_result_to_cstr(cstr* target, BaselineTestResult const* test, bool highlight_name) {
    if (test->did_run) {
        append_test_subresult_to_cstr(target, "ERR", test->errors_passed,  test->errors_prev,  test->has_prev);
        append_test_subresult_to_cstr(target, "SYM", test->symbols_passed, test->symbols_prev, test->has_prev);
        append_test_subresult_to_cstr(target, "TYP", test->types_passed,   test->types_prev,   test->has_prev);
    } else {
        cstr_append_fmt(
            target,
            "%s  did not run  %s",
            color_for_state(false, baseline_test_result_has_new_fail(test)),
            color_normal
        );
    }
    bool test_has_change = baseline_test_result_has_new_pass(test) || baseline_test_result_has_new_fail(test);
    const char* name_color = test_has_change ?
        color_for_state(!baseline_test_result_has_new_fail(test), highlight_name) :
        (baseline_test_result_is_pass(test) ? color_good : "");
    cstr_append_fmt(
        target,
        "%s %.*s %s",
        name_color,
        c_SV(test_name_from_path(cstr_sv(&test->source_path))),
        color_normal
    );
}

void baseline_test_suite_run(BaselineTestSuite* me, bool summary_only, bool show_errs, bool show_syms, bool show_typs) {
    intptr_t test_count = vec_baseline_test_result_size(&my results);
    int passed = 0, failed = 0, new_passed = 0, new_failed = 0;
    cstr display = cstr_null;
    VecBaselineTestResultCPtr newly_failed = vec_baseline_test_result_c_ptr_init();
    VecBaselineTestResultCPtr newly_passed = vec_baseline_test_result_c_ptr_init();
    VecBaselineTestResultCPtr memory_issues = vec_baseline_test_result_c_ptr_init();

    if (!summary_only) printf("%sRunning tests...%s\n\n", color_status, color_normal);

    c_foreach(i, vec_baseline_test_result, my results) {
        intptr_t index = i.ref - my results.data;
        csview name = test_name_from_path(cstr_sv(&i.ref->source_path));
        if (!test_passes_filter(me, name)) {
            continue;
        }

        if (!summary_only) {
            cstr_clear(&display);
            cstr_append_fmt(&display, "%ld%%", index * 100 / test_count);
            printf("        %s%5s %s  %.*s ", color_run_percent, cstr_str(&display), color_normal, c_SV(name));
            fflush(stdout);

            if (show_errs || show_syms || show_typs) {
                printf("- Testing %s\n", cstr_str(&i.ref->source_path));
            }
        }

        Result prev_result = js_object_get(my prev_results.get, name);
        JsObject* prev_obj = prev_result.value.tag == VALUE_OBJECT ? prev_result.value.object.get : nullptr;

#ifdef DEBUG_ALLOCATOR
        debug_allocation_count = 0;
#endif

        baseline_test_result_run(i.ref, me, prev_obj, show_errs, show_syms, show_typs);

#ifdef DEBUG_ALLOCATOR
        if (debug_allocation_count != 0) {
            // Run again in case the "leaked" memory was actually added to the cache. This lowers the chance we'll catch
            // flaky tests but those don't seem too much of a problem
            debug_allocation_count = 0;
            baseline_test_result_run(i.ref, me, prev_obj, false, false, false);
            if (debug_allocation_count != 0) {
                printf(" %s %zd net allocations! %s\n",
                    color_highlighted_error, debug_allocation_count, color_normal);
                vec_baseline_test_result_c_ptr_push(&memory_issues, i.ref);
            }
        }
#endif

        result_drop(&prev_result);

        bool is_pass = baseline_test_result_is_pass(i.ref);
        bool is_change = is_pass != baseline_test_result_was_pass(i.ref);
        if (is_pass) {
            passed++;
            new_passed += is_change ? 1 : 0;
        } else {
            failed++;
            new_failed += is_change ? 1 : 0;
        }

        if (baseline_test_result_has_new_pass(i.ref)) {
            vec_baseline_test_result_c_ptr_push(&newly_passed, i.ref);
        }

        if (baseline_test_result_has_new_fail(i.ref)) {
            vec_baseline_test_result_c_ptr_push(&newly_failed, i.ref);
        }

        if (!summary_only) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, i.ref, true);
            printf("\r%s\n", cstr_str(&display));
        }
    }

    printf(
        "\n  %s%d%s passed, %s%d%s failed, %ld total\n\n",
        color_good,
        passed,
        color_normal,
        color_error,
        failed,
        color_normal,
        vec_baseline_test_result_size(&my results)
    );

    intptr_t newly_passed_size = vec_baseline_test_result_c_ptr_size(&newly_passed);
    if (newly_passed_size) {
        printf(
            "%s%ld%s test%s have newly passing components:\n",
            color_good, newly_passed_size, color_normal, newly_passed_size == 1 ? "" : "s"
        );

        c_foreach(i, vec_baseline_test_result_c_ptr, newly_passed) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, *i.ref, false);
            printf("%s (%s)\n", cstr_str(&display), cstr_str(&(*i.ref)->source_path));
        }
        printf("\n");
    }

    intptr_t newly_failed_size = vec_baseline_test_result_c_ptr_size(&newly_failed);
    if (newly_failed_size) {
        printf(
            "%s%ld%s test%s have newly failing components:\n",
            color_error, newly_failed_size, color_normal, newly_failed_size == 1 ? "" : "s"
        );

        c_foreach(i, vec_baseline_test_result_c_ptr, newly_failed) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, *i.ref, false);
            printf("%s (%s)\n", cstr_str(&display), cstr_str(&(*i.ref)->source_path));
        }
        printf("\n");
    }

    intptr_t memory_issues_size = vec_baseline_test_result_c_ptr_size(&memory_issues);
    if (memory_issues_size) {
        printf(
            "%s%ld test%s have memory-related issues:%s\n",
            color_highlighted_error, memory_issues_size, memory_issues_size == 1 ? "" : "s", color_normal
        );

        c_foreach(i, vec_baseline_test_result_c_ptr, memory_issues) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, *i.ref, false);
            printf("%s\n", cstr_str(&display));
        }
        printf("\n");
    }

    if (new_passed || new_failed) {
        printf(
            "  %s%d%s new passed, %s%d%s new failed\n\n",
            new_passed ? color_good : "", new_passed, color_normal,
            new_failed ? color_error : "", new_failed, color_normal
        );
    }

    if (!cstr_eq(&my prev_typescript_commit, &my typescript_commit)) {
        printf(
            "Note: the TypeScript commit changed %s%s%s -> %s%s%s.\n\n",
            color_for_state(false, true), cstr_str(&my prev_typescript_commit), color_normal,
            color_for_state(true,  true), cstr_str(&my typescript_commit),      color_normal
        );
    }

    vec_baseline_test_result_c_ptr_drop(&newly_failed);
    vec_baseline_test_result_c_ptr_drop(&newly_passed);
    vec_baseline_test_result_c_ptr_drop(&memory_issues);
    cstr_drop(&display);
}

static inline const char* result_element_to_str(bool did_run, bool passed) {
    return did_run ?
        (passed ? "true" : "false") :
        "undefined";
}

void baseline_test_suite_save_results(BaselineTestSuite* me, csview results_path) {
    printf("%sSaving results...%s\n", color_status, color_normal);

    cstr results = cstr_null;
    cstr_reserve(&results, vec_baseline_test_result_size(&my results) * 150);
    cstr_append_fmt(&results, "const tsCommit = \"%s\"\n", cstr_str(&my typescript_commit));
    cstr_append_fmt(&results, "const baselines = {\n");

    c_foreach(i, vec_baseline_test_result, my results) {
        csview name = test_name_from_path(cstr_sv(&i.ref->source_path));
        ASSERT(!csview_contains(name, "\\") && !csview_contains(name, "'"));
        cstr_append_fmt(&results, "    '%.*s': {\n", c_SV(name));
        cstr_append_fmt(&results, "        errors: %s,\n", result_element_to_str(i.ref->did_run, i.ref->errors_passed));
        cstr_append_fmt(&results, "        symbols: %s,\n", result_element_to_str(i.ref->did_run, i.ref->symbols_passed));
        cstr_append_fmt(&results, "        types: %s,\n", result_element_to_str(i.ref->did_run, i.ref->types_passed));
        cstr_append_fmt(&results, "    },\n");
    }

    cstr_append_fmt(&results, "}\n");

    bool saved = true;
    try (fs_write_file(fs, results_path, cstr_sv(&results))) if_err (e) {
        error_base_eprintln(e);
        saved = false;
    }
    cstr_drop(&results);

    if (saved) {
        printf("%sSaving done%s\n", color_good, color_normal);
    } else {
        printf("%sSaving failed%s\n", color_error, color_normal);
    }
}

static void load_saved_results(BaselineTestSuite* me, csview results_path) {
    ErrorCtx errs = error_ctx_init();
    Program* program = program_new(&errs, fs);

    SourceFile const* results_source = program_load(program, results_path);
    // Don't bother typechecking

    if (!error_ctx_empty(&errs)) {
        print_error_ctx(&errs);
        PANIC("Failed to load saved results");
    }

    ArcObject root = arc_js_object_make((JsObject){0});
    run_in_engine(results_source, arc_js_object_clone(root));

    Result prev_commit_result = js_object_get(root.get, c_sv("tsCommit"));
    my prev_typescript_commit = prev_commit_result.value.tag == VALUE_STRING ?
        cstr_clone(prev_commit_result.value.string) :
        cstr_null;

    Result baselines_result = js_object_get(root.get, c_sv("baselines"));
    my prev_results = baselines_result.value.tag == VALUE_OBJECT ?
        arc_js_object_clone(baselines_result.value.object) :
        arc_js_object_make((JsObject){0});

    result_drop(&prev_commit_result);
    result_drop(&baselines_result);
    arc_js_object_drop(&root);
    program_destroy(program);
    error_ctx_drop(&errs);
}

static cstr get_git_hash_of_repo(csview repo_path) {
    fs_path_from_csview(repo_str, repo_path, {
        PANIC("Invalid repo path %.*s", c_SV(repo_path));
    });
    cstr out_str = cstr_null;
    int exit_code = 0;
    // git's stderr is swallowed and a nonzero exit code just means we don't know the commit (the tests directory may
    // not be a git repo at all), but failing to run git in the first place is worth reporting
    try (run_subproc((SubprocArgs){
        .cmd = {"git", "-C", repo_str, "rev-parse", "HEAD"},
        .stdout = &out_str,
        .exit_code = &exit_code,
    })) or_log(error);
    if (exit_code == 0) {
        if (cstr_size(&out_str)) cstr_resize(&out_str, cstr_size(&out_str) - 1, ' '); // strip trailing newline
        return out_str;
    } else {
        cstr_drop(&out_str);
        return cstr_null;
    }
}

BoxBaselineTestSuite load_baseline_test_suite(
    csview tests_dir,
    const char* filter_re,
    csview results_path,
    bool summary_only
) {
    BoxBaselineTestSuite ret = box_baseline_test_suite_make((BaselineTestSuite){
        .tests_dir = tests_dir,
        .has_filter = filter_re != nullptr,
        .filter = filter_re ? cregex_make(filter_re, CREG_ICASE) : (cregex){0},
        .cache = baseline_test_cache_init(),
    });
    ASSERT(!ret.get->filter.error);

    if (!summary_only) printf("%sLoading saved results...%s\n", color_status, color_normal);
    load_saved_results(ret.get, results_path);

    if (!summary_only) printf("%sCollecting tests...%s\n", color_status, color_normal);
    ret.get->typescript_commit = get_git_hash_of_repo(tests_dir);
    load_subdir(ret.get, c_sv("conformance"));
    load_subdir(ret.get, c_sv("compiler"));
    show_collected_test_counts(ret.get);
    printf("\n");
    if (!summary_only) printf("%sSorting tests...%s\n", color_status, color_normal);
    vec_baseline_test_result_sort(&ret.get->results);
    return ret;
}
