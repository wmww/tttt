#include "baseline_test_suite.h"
#include "baseline_test_case.h"
#include "baseline_test_result.h"
#include "program.h"
#include "string_helpers.h"
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
    if (my has_filter && cstr_size(&my filter_names)) {
        cstr line = cstr_d("\n", name, "\n");
        bool found = cstr_find(&my filter_names, cstr_str(&line)) != c_NPOS;
        cstr_drop(&line);
        return found;
    } else if (my has_filter) {
        // A null matches array is fine, we only care if the whole name matches and not about capture groups
        return cregex_match_sv(&my filter, name, nullptr, CREG_FULLMATCH) == CREG_OK;
    } else {
        return true;
    }
}

static void show_collected_test_counts(BaselineTestSuite* me) {
    print("\rCollected ", color_good, vec_baseline_test_result_size(&my results), color_normal, " tests      ");
}

// tsgo's skippedTests (internal/testrunner/compiler_runner.go): tests of the API built from the JS compiler, and tests
// of options 7.0 removed outright (they fail to parse)
static char const* const tsgo_skipped_tests[] = {
    "APILibCheck.ts", "APISample_Watch.ts", "APISample_WatchWithDefaults.ts", "APISample_WatchWithOwnWatchHost.ts",
    "APISample_compile.ts", "APISample_jsdoc.ts", "APISample_linter.ts", "APISample_parseConfig.ts",
    "APISample_transform.ts", "APISample_watcher.ts",
    "preserveUnusedImports.ts", "noCrashWithVerbatimModuleSyntaxAndImportsNotUsedAsValues.ts",
    "verbatimModuleSyntaxCompat.ts", "verbatimModuleSyntaxCompat2.ts", "verbatimModuleSyntaxCompat3.ts",
    "verbatimModuleSyntaxCompat4.ts", "preserveValueImports.ts", "preserveValueImports_importsNotUsedAsValues.ts",
    "preserveValueImports_errors.ts", "preserveValueImports_mixedImports.ts", "preserveValueImports_module.ts",
    "importsNotUsedAsValues_error.ts", "alwaysStrictNoImplicitUseStrict.ts", "nonPrimitiveIndexingWithForInSupressError.ts",
    "parameterInitializerBeforeDestructuringEmit.ts", "mappedTypeUnionConstraintInferences.ts",
    "lateBoundConstraintTypeChecksCorrectly.ts", "keyofDoesntContainSymbols.ts", "noStrictGenericChecks.ts",
    "noImplicitUseStrict_umd.ts", "noImplicitUseStrict_system.ts", "noImplicitUseStrict_es6.ts",
    "noImplicitUseStrict_commonjs.ts", "noImplicitAnyIndexingSuppressed.ts", "excessPropertyErrorsSuppressed.ts",
    "moduleNoneDynamicImport.ts", "moduleNoneErrors.ts",
    "noErrorUsingImportExportModuleAugmentationInDeclarationFile1.ts",
    "noErrorUsingImportExportModuleAugmentationInDeclarationFile2.ts",
    "noErrorUsingImportExportModuleAugmentationInDeclarationFile3.ts",
    "requireOfJsonFileWithModuleEmitNone.ts", "requireOfJsonFileWithModuleNodeResolutionEmitNone.ts",
};

static bool tsgo_skips_test(csview path) {
    csview base = basename_of(path);
    for (u32 i = 0; i < c_arraylen(tsgo_skipped_tests); i++) {
        if (csview_equals(base, tsgo_skipped_tests[i])) return true;
    }
    return false;
}

// Recursively collects every .ts and .tsx file below the given directory. Directories that can't be listed are silently
// skipped, as was the case before (listdir has no way to report an error anyway)
static void load_dir(BaselineTestSuite* me, csview dir_path) {
    for_each (item, fs_listdir, fs, dir_path) {
        if (fs_is_directory(fs, item.path)) {
            load_dir(me, item.path);
        } else if (fs_is_reg_file(fs, item.path) &&
                   (csview_ends_with(item.path, ".ts") || csview_ends_with(item.path, ".tsx")) &&
                   !tsgo_skips_test(item.path)) {
            // One test per configuration ("@target: es5,es2015" is two). Most files have exactly one
            cstr contents = cstr_null;
            VecCStr configs = {0};
            try (fs_read_file(fs, item.path, &contents)) or_log(error);
            text_decode_utf16_bom(&contents);
            baseline_test_configurations(cstr_sv(&contents), &configs);
            for_each (config, vec_cstr, &configs) {
                vec_baseline_test_result_push(
                    &my results,
                    make_baseline_test_result(cstr_from_sv(item.path), cstr_move(config))
                );
            }
            vec_cstr_drop(&configs);
            cstr_drop(&contents);

            if (my show_progress && vec_baseline_test_result_size(&my results) % 100 == 0) {
                show_collected_test_counts(me);
            }
        }
    }
}

static_failable load_subdir(BaselineTestSuite* me, csview subdir) {
    cstr full_dir = cstr_d(my tests_dir, "/cases/", subdir);
    if (!fs_is_directory(fs, cstr_sv(&full_dir))) {
        FailableResult ret = err_d(full_dir, " is not a directory");
        cstr_drop(&full_dir);
        return ret;
    }
    load_dir(me, cstr_sv(&full_dir));
    cstr_drop(&full_dir);
    return ok;
}

// tttt's own tests (BaselineTestSuite.own_dir): their names must not collide with tsgo's, so they all start with their
// origin, tttt (written here) or bun (bun's differential check cases, scripts/buncases.py --write-tests)
static_failable load_own_tests(BaselineTestSuite* me) {
    cstr cases = cstr_d(my own_dir, "/cases");
    if (!fs_is_directory(fs, cstr_sv(&cases))) {
        FailableResult ret = err_d(cases, " is not a directory");
        cstr_drop(&cases);
        return ret;
    }
    intptr_t first = vec_baseline_test_result_size(&my results);
    load_dir(me, cstr_sv(&cases));
    cstr_drop(&cases);
    for (intptr_t i = first; i < vec_baseline_test_result_size(&my results); i++) {
        csview name = cstr_sv(&vec_baseline_test_result_at(&my results, i)->name);
        if (!csview_starts_with(name, "tttt") && !csview_starts_with(name, "bun")) {
            return err_d(vec_baseline_test_result_at(&my results, i)->source_path,
                ": own tests must be named tttt* or bun* (not to collide with tsgo's)");
        }
    }
    return ok;
}

void baseline_test_suite_drop(BaselineTestSuite* me) {
    arc_js_object_drop(&my prev_results);
    cregex_drop(&my filter);
    cstr_drop(&my filter_names);
    cstr_drop(&my prev_typescript_commit);
    cstr_drop(&my typescript_commit);
    vec_baseline_test_result_drop(&my results);
    baseline_test_cache_drop(&my cache);
}

static AnsiColor color_for_state(bool is_pass, bool highlight) {
    if (highlight) {
        return is_pass ? color_highlighted_good : color_highlighted_error;
    } else {
        return is_pass ? color_good : color_error;
    }
}

static void append_test_subresult_to_cstr(cstr* target, const char* name, bool passed, bool prev, bool has_prev) {
    bool changed = !has_prev || passed != prev;
    cstr_append_d(
        target, PRINT_DISPLAY,
        color_for_state(passed, changed), " ", passed ? " - " : name, " ", color_normal
    );
}

static void append_test_result_to_cstr(cstr* target, BaselineTestResult const* test, bool highlight_name) {
    if (test->did_run) {
        append_test_subresult_to_cstr(target, "ERR", test->errors_passed,  test->errors_prev,  test->has_prev);
        append_test_subresult_to_cstr(target, "SYM", test->symbols_passed, test->symbols_prev, test->has_prev);
        append_test_subresult_to_cstr(target, "TYP", test->types_passed,   test->types_prev,   test->has_prev);
        if (test->has_dts) {
            append_test_subresult_to_cstr(target, "DTS", test->dts_passed, test->dts_prev, test->has_dts_prev);
        } else {
            cstr_append_lit(target, "     ");
        }
    } else {
        cstr_append_d(
            target, PRINT_DISPLAY,
            color_for_state(false, baseline_test_result_has_new_fail(test)), "  did not run  ", color_normal
        );
    }
    bool test_has_change = baseline_test_result_has_new_pass(test) || baseline_test_result_has_new_fail(test);
    AnsiColor name_color = test_has_change ?
        color_for_state(!baseline_test_result_has_new_fail(test), highlight_name) :
        (baseline_test_result_is_pass(test) ? color_good : color_none);
    cstr_append_d(
        target, PRINT_DISPLAY,
        name_color, " ", test->name, " ", color_normal
    );
}

void baseline_test_suite_run(BaselineTestSuite* me, bool summary_only, bool show_errs, bool show_syms, bool show_typs) {
    intptr_t test_count = vec_baseline_test_result_size(&my results);
    int passed = 0, failed = 0, new_passed = 0, new_failed = 0, skipped = 0;
    int dts_passed = 0, dts_total = 0, dts_js = 0, dts_js_passed = 0;
    cstr display = cstr_null;
    VecBaselineTestResultCPtr newly_failed = vec_baseline_test_result_c_ptr_init();
    VecBaselineTestResultCPtr newly_passed = vec_baseline_test_result_c_ptr_init();
    VecBaselineTestResultCPtr memory_issues = vec_baseline_test_result_c_ptr_init();

    if (!summary_only) println(color_status, "Running tests...", color_normal, "\n");

    for_each (i, vec_baseline_test_result, &my results) {
        intptr_t index = i_index;
        csview name = cstr_sv(&i->name);
        // "-f someTest" means all configurations of it
        if (!test_passes_filter(me, name) && !test_passes_filter(me, test_name_from_path(cstr_sv(&i->source_path)))) {
            continue;
        }

        if (!summary_only) {
            cstr_clear(&display);
            cstr_append_d(&display, DEFAULT_DISPLAY, index * 100 / test_count, "%");
            print(
                "        ", color_run_percent,
                repeated_d(" ", cstr_size(&display) < 5 ? 5 - cstr_size(&display) : 0), display,
                " ", color_normal, "  ", name, " "
            );
            fflush(stdout);

            if (show_errs || show_syms || show_typs) {
                println("- Testing ", i->source_path);
            }
        }

        Result prev_result = js_object_get(my prev_results.get, name);
        JsObject* prev_obj = prev_result.value.tag == VALUE_OBJECT ? prev_result.value.object.get : nullptr;

#ifdef DEBUG_ALLOCATOR
        debug_allocation_count = 0;
#endif

        baseline_test_result_run(i, me, prev_obj, show_errs, show_syms, show_typs);

#ifdef DEBUG_ALLOCATOR
        if (debug_allocation_count != 0) {
            // Run again in case the "leaked" memory was actually added to the cache. This lowers the chance we'll catch
            // flaky tests but those don't seem too much of a problem
            debug_allocation_count = 0;
            baseline_test_result_run(i, me, prev_obj, false, false, false);
            if (debug_allocation_count != 0) {
                println(" ", color_highlighted_error, " ", (i64)debug_allocation_count,
                    " net allocations! ", color_normal);
                vec_baseline_test_result_c_ptr_push(&memory_issues, i);
            }
        }
#endif

        result_drop(&prev_result);
        if (i->skipped) {
            skipped++;
            if (!summary_only) print("\r\x1b[2K");
            continue;
        }

        dts_total += i->has_dts;
        dts_passed += i->has_dts && i->dts_passed;
        dts_js += i->dts_js;
        dts_js_passed += i->dts_js && i->dts_passed;
        bool is_pass = baseline_test_result_is_pass(i);
        bool is_change = is_pass != baseline_test_result_was_pass(i);
        if (is_pass) {
            passed++;
            new_passed += is_change ? 1 : 0;
        } else {
            failed++;
            new_failed += is_change ? 1 : 0;
        }

        if (baseline_test_result_has_new_pass(i)) {
            vec_baseline_test_result_c_ptr_push(&newly_passed, i);
        }

        if (baseline_test_result_has_new_fail(i)) {
            vec_baseline_test_result_c_ptr_push(&newly_failed, i);
        }

        if (!summary_only) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, i, true);
            println("\r", display);
        }
    }

    println(
        "\n  ", color_good, passed, color_normal, " passed, ",
        color_error, failed, color_normal, " failed, ",
        passed + failed, " total\n"
    );
    if (dts_total) {
        println("  dts (declaration emit, counted above): ", color_good, dts_passed, color_normal, " / ", dts_total,
            " (with JavaScript inputs: ", dts_js_passed, " / ", dts_js, ")\n");
    }
    if (skipped) println("  (", skipped, " configurations skipped: tsgo doesn't run them, an option 7.0 removed)\n");
    if (my has_filter && passed + failed == 0) {
        // STC's cregex silently matches nothing once a pattern gets long (a few hundred alternatives)
        println(color_error, "The filter matched no tests", color_normal,
            " (a long regex alternation can silently match nothing: use -f @names.txt)\n");
    }

    intptr_t newly_passed_size = vec_baseline_test_result_c_ptr_size(&newly_passed);
    if (newly_passed_size) {
        println(
            color_good, newly_passed_size, color_normal,
            " test", newly_passed_size == 1 ? "" : "s", " have newly passing components:"
        );

        for_each (i, vec_baseline_test_result_c_ptr, &newly_passed) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, *i, false);
            println(display, " (", (*i)->source_path, ")");
        }
        println();
    }

    intptr_t newly_failed_size = vec_baseline_test_result_c_ptr_size(&newly_failed);
    if (newly_failed_size) {
        println(
            color_error, newly_failed_size, color_normal,
            " test", newly_failed_size == 1 ? "" : "s", " have newly failing components:"
        );

        for_each (i, vec_baseline_test_result_c_ptr, &newly_failed) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, *i, false);
            println(display, " (", (*i)->source_path, ")");
        }
        println();
    }

    intptr_t memory_issues_size = vec_baseline_test_result_c_ptr_size(&memory_issues);
    if (memory_issues_size) {
        println(
            color_highlighted_error, memory_issues_size,
            " test", memory_issues_size == 1 ? "" : "s", " have memory-related issues:", color_normal
        );

        for_each (i, vec_baseline_test_result_c_ptr, &memory_issues) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, *i, false);
            println(display);
        }
        println();
    }

    if (new_passed || new_failed) {
        println(
            "  ", new_passed ? color_good : color_none, new_passed, color_normal, " new passed, ",
            new_failed ? color_error : color_none, new_failed, color_normal, " new failed\n"
        );
    }

    if (!cstr_eq(&my prev_typescript_commit, &my typescript_commit)) {
        println(
            "Note: the TypeScript commit changed ",
            color_for_state(false, true), my prev_typescript_commit, color_normal, " -> ",
            color_for_state(true,  true), my typescript_commit,      color_normal, ".\n"
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
    println(color_status, "Saving results...", color_normal);

    cstr results = cstr_null;
    cstr_reserve(&results, vec_baseline_test_result_size(&my results) * 150);
    cstr_append_d(&results, DEFAULT_DISPLAY, "const tsCommit = \"", my typescript_commit, "\"\nconst baselines = {\n");

    for_each (i, vec_baseline_test_result, &my results) {
        if (i->skipped) continue;
        csview name = cstr_sv(&i->name);
        check(!csview_contains(name, "\\") && !csview_contains(name, "'"));
        cstr_append_d(
            &results, DEFAULT_DISPLAY,
            "    '", name, "': {\n",
            "        errors: ", result_element_to_str(i->did_run, i->errors_passed), ",\n",
            "        symbols: ", result_element_to_str(i->did_run, i->symbols_passed), ",\n",
            "        types: ", result_element_to_str(i->did_run, i->types_passed), ",\n"
        );
        if (i->has_dts) cstr_append_d(&results, DEFAULT_DISPLAY, "        dts: ", i->dts_passed ? "true" : "false", ",\n");
        cstr_append_lit(&results, "    },\n");
    }

    cstr_append_lit(&results, "}\n");

    bool saved = true;
    try (fs_write_file(fs, results_path, cstr_sv(&results))) if_err (e) {
        error_base_eprintln(e);
        saved = false;
    }
    cstr_drop(&results);

    if (saved) {
        println(color_good, "Saving done", color_normal);
    } else {
        println(color_error, "Saving failed", color_normal);
    }
}

static void destroy_program(Program* program, ErrorCtx* errs) {
    program_destroy(program);
    error_ctx_drop(errs);
}

static char const* results_file_name(SourceFile const* source) {
    return cstr_str(&source->canonical_path);
}

static_failable load_saved_results(BaselineTestSuite* me, csview results_path) {
    ErrorCtx errs = error_ctx_init();
    Program* program = program_new(&errs, fs);

    try (SourceFile* results_source, program_load(program, results_path, &results_source)) cleanup {
        destroy_program(program, &errs);
    }
    // Don't bother typechecking

    if (!error_ctx_empty(&errs)) {
        cstr text = cstr_null;
        for_each (i, vec_error, &errs.errors) error_format_plain(&text, i, "\n", results_file_name);
        fputs(cstr_str(&text), stdout);
        cstr_drop(&text);
        destroy_program(program, &errs);
        return err_d("Failed to load saved results from ", results_path);
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
    destroy_program(program, &errs);
    return ok;
}

static cstr get_git_hash_of_repo(csview repo_path) {
    fs_path_from_csview(repo_str, repo_path, {
        panic("Invalid repo path ", repo_path);
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

// cregex reports compile failures as a negative code in the `error` field, which on its own tells the user nothing
static const char* cregex_error_str(int error) {
    switch (error) {
        case CREG_OK: return "no error";
        case CREG_NOMATCH: return "no match";
        case CREG_MATCHERROR: return "match error";
        case CREG_OUTOFMEMORY: return "out of memory";
        case CREG_UNMATCHEDLEFTPARENTHESIS: return "unmatched '('";
        case CREG_UNMATCHEDRIGHTPARENTHESIS: return "unmatched ')'";
        case CREG_TOOMANYSUBEXPRESSIONS: return "too many subexpressions";
        case CREG_TOOMANYCHARACTERCLASSES: return "too many character classes";
        case CREG_MALFORMEDCHARACTERCLASS: return "malformed character class";
        case CREG_MISSINGOPERAND: return "missing operand";
        case CREG_UNKNOWNOPERATOR: return "unknown operator";
        case CREG_OPERANDSTACKOVERFLOW: return "operand stack overflow";
        case CREG_OPERATORSTACKOVERFLOW: return "operator stack overflow";
        case CREG_OPERATORSTACKUNDERFLOW: return "operator stack underflow";
        default: return "unknown error";
    }
}

failable load_baseline_test_suite(
    csview tests_dir,
    csview own_dir,
    const char* filter_re,
    csview results_path,
    bool summary_only,
    returns BoxBaselineTestSuite
) {
    // `-f @file`: a list of names instead of a regex (cregex silently matches nothing once a pattern is a few kB long)
    cstr names = cstr_null;
    if (filter_re && filter_re[0] == '@') {
        FILE* file = fopen(filter_re + 1, "r");
        if (!file) return err_d("Can't read the filter list '", filter_re + 1, "'");
        names = cstr_lit("\n");
        char line[4096];
        while (fgets(line, sizeof(line), file)) {
            csview trimmed = csview_trim(csview_from(line));
            if (trimmed.size) cstr_append_sv(&names, trimmed), cstr_append(&names, "\n");
        }
        fclose(file);
    }
    BoxBaselineTestSuite ret = box_baseline_test_suite_make((BaselineTestSuite){
        .tests_dir = tests_dir,
        .own_dir = own_dir,
        .has_filter = filter_re != nullptr,
        .filter = filter_re && !cstr_size(&names) ? cregex_make(filter_re, CREG_ICASE) : (cregex){0},
        .filter_names = names,
        .cache = baseline_test_cache_init(),
        .show_progress = !summary_only,
    });

    if (ret.get->filter.error) {
        FailableResult failed = err_d(
            "Invalid filter regex '", filter_re, "': ", cregex_error_str(ret.get->filter.error));
        box_baseline_test_suite_drop(&ret);
        return failed;
    }

    if (!summary_only) println(color_status, "Loading saved results...", color_normal);
    try (load_saved_results(ret.get, results_path)) cleanup {
        box_baseline_test_suite_drop(&ret);
    }

    if (!summary_only) println(color_status, "Collecting tests...", color_normal);
    ret.get->typescript_commit = get_git_hash_of_repo(tests_dir);

    // Collect both problems before giving up, so a botched --dir doesn't have to be fixed one message at a time
    FailableResult dirs_result = {0};
    try (load_subdir(ret.get, c_sv("conformance"))) or_collect_into (&dirs_result);
    try (load_subdir(ret.get, c_sv("compiler"))) or_collect_into (&dirs_result);
    if (csview_size(own_dir)) {
        try (load_own_tests(ret.get)) or_collect_into (&dirs_result);
    }
    try (dirs_result) cleanup {
        box_baseline_test_suite_drop(&ret);
    }

    if (!summary_only) {
        show_collected_test_counts(ret.get);
        println();
    }
    if (!summary_only) println(color_status, "Sorting tests...", color_normal);
    vec_baseline_test_result_sort(&ret.get->results);
    return ok(ret);
}
