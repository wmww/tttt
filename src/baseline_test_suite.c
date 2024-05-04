#include "baseline_test_suite.h"
#include "baseline_test_case.h"
#include "filesystem.h"
#include "parser.h"
#include "engine.h"
#include "value.h"
#include "block.h"
#include "result.h"
#include "subprocess.h"

#include "stc/cregex.h"
#include <errno.h>

#define i_key BaselineTestCase const*
#define i_type vec_baseline_test_case_c_ptr
#include "wrap_stc/vec.h"
typedef vec_baseline_test_case_c_ptr VecBaselineTestCaseCPtr;

static cstr test_name_from_path(const char* path) {
    csview name = csview_from(basename_of(path));
    ASSERT(csview_ends_with(name, ".ts"));
    name = csview_substr(name, 0, csview_size(name) - 3);
    return cstr_from_sv(name);
}

static bool test_passes_filter(BaselineTestSuite* me, const char* name) {
    return !my has_filter || cregex_find(&my filter, name, nullptr, CREG_FULLMATCH) == CREG_OK;
}

static void show_collected_test_counts(BaselineTestSuite* me) {
    printf(
        "\rCollected " CLR_GOOD "%ld" CLR0 " tests      ",
        vec_baseline_test_case_size(&my cases)
    );
}

static void load_path(void* data, const char* dir_path, const char* name) {
    BaselineTestSuite* me = data;
    cstr full_path = cstr_from_fmt("%s/%s", dir_path, name);

    if (path_is_directory(cstr_str(&full_path))) {
        for_each_item_in_dir(cstr_str(&full_path), load_path, me);
        cstr_drop(&full_path);
    } else if (path_is_reg_file(cstr_str(&full_path)) && cstr_ends_with(&full_path, ".ts")) {
        cstr name = test_name_from_path(cstr_str(&full_path));
        Result prev_result = js_object_get(my prev_results.get, cstr_sv(&name));
        JsObject* prev_obj = prev_result.value.tag == VALUE_OBJECT ? prev_result.value.object.get : nullptr;

        vec_baseline_test_case_push(
            &my cases,
            make_baseline_test_case(me, name, full_path, prev_obj)
        );

        result_drop(&prev_result);

        if (vec_baseline_test_case_size(&my cases) % 100 == 0) {
            show_collected_test_counts(me);
        }
    } else {
        cstr_drop(&full_path);
    }
}

static void load_subdir(BaselineTestSuite* me, const char* subdir) {
    cstr full_dir = cstr_from_fmt("%s/cases/%s", my tests_dir, subdir);
    if (path_is_directory(cstr_str(&full_dir))) {
        for_each_item_in_dir(cstr_str(&full_dir), load_path, me);
    } else {
        fprintf(stderr, CLR_ERR "%s is not a directory\n" CLR0, cstr_str(&full_dir));
    }
    cstr_drop(&full_dir);
}

void baseline_test_suite_drop(BaselineTestSuite* me) {
    arc_js_object_drop(&my prev_results);
    cregex_drop(&my filter);
    cstr_drop(&my prev_typescript_commit);
    cstr_drop(&my typescript_commit);
    vec_baseline_test_case_drop(&my cases);
}

static const char* color_for_state(bool is_pass, bool highlight) {
    if (highlight) {
        return is_pass ? "\x1b[1;42m" : "\x1b[1;41m";
    } else {
        return is_pass ? CLR_GOOD : CLR_ERR;
    }
}

static void append_test_subresult_to_cstr(cstr* target, const char* name, bool passed, bool prev, bool has_prev) {
    bool changed = !has_prev || passed != prev;
    cstr_append_fmt(target, "%s %s " CLR0, color_for_state(passed, changed), passed ? " - " : name);
}

static void append_test_result_to_cstr(cstr* target, BaselineTestCase const* test, bool highlight_name) {
    if (test->has_run) {
        append_test_subresult_to_cstr(target, "ERR", test->errors_passed,  test->errors_prev,  test->has_prev);
        append_test_subresult_to_cstr(target, "SYM", test->symbols_passed, test->symbols_prev, test->has_prev);
        append_test_subresult_to_cstr(target, "TYP", test->types_passed,   test->types_prev,   test->has_prev);
    } else {
        cstr_append_fmt(
            target,
            "%s  did not run  " CLR0,
            color_for_state(false, baseline_test_case_has_new_fail(test))
        );
    }
    bool test_has_change = baseline_test_case_has_new_pass(test) || baseline_test_case_has_new_fail(test);
    const char* name_color = test_has_change ?
        color_for_state(!baseline_test_case_has_new_fail(test), highlight_name) :
        "";
    cstr_append_fmt(target, "%s %s " CLR0, name_color, cstr_str(&test->name));
}

void baseline_test_suite_run(BaselineTestSuite* me, bool noisy) {
    intptr_t test_count = vec_baseline_test_case_size(&my cases);
    int passed = 0, failed = 0, new_passed = 0, new_failed = 0;
    cstr display = cstr_null;
    VecBaselineTestCaseCPtr newly_failed = vec_baseline_test_case_c_ptr_init();
    VecBaselineTestCaseCPtr newly_passed = vec_baseline_test_case_c_ptr_init();

    printf(CLR_STATUS "Running tests...\n\n" CLR0);
    c_foreach(i, vec_baseline_test_case, my cases) {
        intptr_t index = i.ref - my cases.data;
        if (!test_passes_filter(me, cstr_str(&i.ref->name))) {
            continue;
        }

        cstr_clear(&display);
        cstr_append_fmt(&display, "%ld%%", index * 100 / test_count);
        printf("        \x1b[1;44m%5s " CLR0 "  %s", cstr_str(&display), cstr_str(&i.ref->name));
        fflush(stdout);

        baseline_test_case_run(i.ref, noisy);
        baseline_test_case_clear_detail(i.ref);

        bool is_pass = baseline_test_case_is_pass(i.ref);
        bool is_change = is_pass != baseline_test_case_was_pass(i.ref);
        if (is_pass) {
            passed++;
            new_passed += is_change ? 1 : 0;
        } else {
            failed++;
            new_failed += is_change ? 1 : 0;
        }

        if (baseline_test_case_has_new_pass(i.ref)) {
            vec_baseline_test_case_c_ptr_push(&newly_passed, i.ref);
        }

        if (baseline_test_case_has_new_fail(i.ref)) {
            vec_baseline_test_case_c_ptr_push(&newly_failed, i.ref);
        }

        cstr_clear(&display);
        append_test_result_to_cstr(&display, i.ref, true);
        printf("\r%s\n", cstr_str(&display));
    }

    printf(
        "\n  " CLR_GOOD "%d" CLR0 " passed, " CLR_ERR "%d" CLR0 " failed, %ld total\n\n",
        passed,
        failed,
        vec_baseline_test_case_size(&my cases)
    );

    intptr_t newly_passed_size = vec_baseline_test_case_c_ptr_size(&newly_passed);
    if (newly_passed_size) {
        printf(
            CLR_GOOD "%ld" CLR0 " test%s have newly passing components:\n",
            newly_passed_size, newly_passed_size == 1 ? "" : "s");

        c_foreach(i, vec_baseline_test_case_c_ptr, newly_passed) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, *i.ref, false);
            printf("%s (%s)\n", cstr_str(&display), cstr_str(&(*i.ref)->source_path));
        }
        printf("\n");
    }

    intptr_t newly_failed_size = vec_baseline_test_case_c_ptr_size(&newly_failed);
    if (newly_failed_size) {
        printf(
            CLR_ERR "%ld" CLR0 " test%s have newly failing components:\n",
            newly_failed_size, newly_failed_size == 1 ? "" : "s");

        c_foreach(i, vec_baseline_test_case_c_ptr, newly_failed) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, *i.ref, false);
            printf("%s (%s)\n", cstr_str(&display), cstr_str(&(*i.ref)->source_path));
        }
        printf("\n");
    }

    if (new_passed || new_failed) {
        printf(
            "  %s%d" CLR0 " new passed, %s%d" CLR0 " new failed\n\n",
            new_passed ? CLR_GOOD : "", new_passed,
            new_failed ? CLR_ERR : "", new_failed
        );
    }

    if (!cstr_eq(&my prev_typescript_commit, &my typescript_commit)) {
        printf(
            "Note: the TypeScript commit changed %s%s%s -> %s%s%s.\n\n",
            color_for_state(false, true), cstr_str(&my prev_typescript_commit), CLR0,
            color_for_state(true,  true), cstr_str(&my typescript_commit),      CLR0
        );
    }

    vec_baseline_test_case_c_ptr_drop(&newly_failed);
    vec_baseline_test_case_c_ptr_drop(&newly_passed);
    cstr_drop(&display);
}

void baseline_test_suite_save_results(BaselineTestSuite* me, const char* results_path) {
    printf(CLR_STATUS "Saving results...\n" CLR0);
    FILE *f = fopen(results_path, "w");
    if (!f) {
        PANIC("Failed to write to %s: %s\n", results_path, strerror(errno));
    }
    fprintf(f, "const tsCommit = \"%s\"\n", cstr_str(&my typescript_commit));
    fprintf(f, "const baselines = {\n");

    c_foreach(i, vec_baseline_test_case, my cases) {
        ASSERT(!strchr(cstr_str(&i.ref->name), '\\') && !strchr(cstr_str(&i.ref->name), '\''));
        fprintf(f, "    '%s': {\n", cstr_str(&i.ref->name));
        fprintf(f, "        errors: %s,\n", i.ref->errors_passed ? "true" : "false");
        fprintf(f, "        symbols: %s,\n", i.ref->symbols_passed ? "true" : "false");
        fprintf(f, "        types: %s,\n", i.ref->types_passed ? "true" : "false");
        fprintf(f, "    },\n");
    }

    fprintf(f, "}\n");
    fclose(f);
    printf(CLR_GOOD "Saving done\n" CLR0);
}

static void load_saved_results(BaselineTestSuite* me, const char* results_path) {
    cstr results_source;

    if (!read_entire_file(results_path, true, &results_source)) {
        my prev_results = arc_js_object_make((JsObject){0});
    }

    ErrorCtx errs = error_ctx_init();
    Block ast = parse_file(cstr_str(&results_source), &errs);
    block_resolve_names_toplevel(&ast, &errs);
    block_typecheck_toplevel(&ast, &errs);
    if (!error_ctx_empty(&errs)) {
        error_ctx_display(&errs);
        error_ctx_drop(&errs);
        block_drop(&ast);
        cstr_drop(&results_source);
        my prev_results = arc_js_object_make((JsObject){0});
    }

    ArcObject root = arc_js_object_make((JsObject){0});
    eval_program(&ast, arc_js_object_clone(root));

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
    block_drop(&ast);
    error_ctx_drop(&errs);
    cstr_drop(&results_source);
}

static cstr get_git_hash_of_repo(const char* repo) {
    const char* args[] = {
        "git",
        "-C",
        repo,
        "rev-parse",
        "HEAD",
        nullptr,
    };
    cstr out_str = cstr_null, err_str = cstr_null;
    if (run_subprocess_get_output(args, &out_str, &err_str) == 0) {
        if (cstr_size(&out_str)) cstr_resize(&out_str, cstr_size(&out_str) - 1, ' '); // strip trailing newline
        cstr_drop(&err_str);
        return out_str;
    } else {
        PANIC("getting git hash failed: %s", cstr_str(&err_str));
    }
}

BoxBaselineTestSuite load_baseline_test_suite(
    const char* tests_dir,
    const char* filter_re,
    const char* results_path
) {
    BoxBaselineTestSuite ret = box_baseline_test_suite_make((BaselineTestSuite){
        .tests_dir = tests_dir,
        .has_filter = filter_re != nullptr,
        .filter = filter_re ? cregex_from(filter_re, CREG_ICASE) : cregex_init(),
    });
    ASSERT(!ret.get->filter.error);

    printf(CLR_STATUS "Loading saved results...\n" CLR0);
    load_saved_results(ret.get, results_path);

    printf(CLR_STATUS "Collecting tests...\n" CLR0);
    ret.get->typescript_commit = get_git_hash_of_repo(tests_dir);
    load_subdir(ret.get, "conformance");
    load_subdir(ret.get, "compiler");
    show_collected_test_counts(ret.get);
    printf("\n");
    printf(CLR_STATUS "Sorting tests...\n" CLR0);
    vec_baseline_test_case_quicksort(&ret.get->cases);
    return ret;
}
