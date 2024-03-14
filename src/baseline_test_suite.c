#include "baseline_test_suite.h"
#include "baseline_test_case.h"
#include "filesystem.h"
#include "parser.h"
#include "engine.h"
#include "value.h"
#include "block.h"
#include "result.h"

#include "stc/cregex.h"
#include <errno.h>

#define i_key BaselineTestCase const*
#define i_tag BaselineTestCaseCPtr
#include "stc/vec.h"

static cstr test_name_from_path(const char* path) {
    csview name = csview_from(basename_of(path));
    ASSERT(csview_ends_with(name, ".ts"));
    name = csview_substr(name, 0, csview_size(name) - 3);
    return cstr_from_sv(name);
}

static bool test_passes_filter(BaselineTestSuite* self, const char* name) {
    return !self->has_filter || cregex_is_match(&self->filter, name);
}

static void show_collected_test_counts(BaselineTestSuite* self) {
    printf(
        "\rCollected " CLR_GOOD "%ld" CLR0 " tests      ",
        vec_BaselineTestCase_size(&self->cases)
    );
}

static void load_path(void* data, const char* dir_path, const char* name) {
    BaselineTestSuite* self = data;
    cstr full_path = cstr_from_fmt("%s/%s", dir_path, name);

    if (path_is_directory(cstr_str(&full_path))) {
        for_each_item_in_dir(cstr_str(&full_path), load_path, self);
        cstr_drop(&full_path);
    } else if (path_is_reg_file(cstr_str(&full_path)) && cstr_ends_with(&full_path, ".ts")) {
        cstr name = test_name_from_path(cstr_str(&full_path));
        Result prev_result = ObjectValue_get(self->prev_results.get, cstr_sv(&name));
        ObjectValue* prev_obj = prev_result.value.tag == VALUE_OBJECT ? prev_result.value.object.get : nullptr;

        vec_BaselineTestCase_push(
            &self->cases,
            make_baseline_test_case(self, name, full_path, prev_obj)
        );

        Result_drop(&prev_result);

        if (vec_BaselineTestCase_size(&self->cases) % 100 == 0) {
            show_collected_test_counts(self);
        }
    } else {
        cstr_drop(&full_path);
    }
}

static void load_subdir(BaselineTestSuite* self, const char* subdir) {
    cstr full_dir = cstr_from_fmt("%s/cases/%s", self->tests_dir, subdir);
    if (path_is_directory(cstr_str(&full_dir))) {
        for_each_item_in_dir(cstr_str(&full_dir), load_path, self);
    } else {
        fprintf(stderr, CLR_ERR "%s is not a directory\n" CLR0, cstr_str(&full_dir));
    }
    cstr_drop(&full_dir);
}

void BaselineTestSuite_drop(BaselineTestSuite* self) {
    arc_Object_drop(&self->prev_results);
    cregex_drop(&self->filter);
    vec_BaselineTestCase_drop(&self->cases);
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
            color_for_state(false, BaselineTestCase_has_new_fail(test))
        );
    }
    bool test_has_change = BaselineTestCase_has_new_pass(test) || BaselineTestCase_has_new_fail(test);
    const char* name_color = test_has_change ?
        color_for_state(!BaselineTestCase_has_new_fail(test), highlight_name) :
        "";
    cstr_append_fmt(target, "%s %s " CLR0, name_color, cstr_str(&test->name));
}

void BaselineTestSuite_run(BaselineTestSuite* self) {
    intptr_t test_count = vec_BaselineTestCase_size(&self->cases);
    int passed = 0, failed = 0;
    cstr display = cstr_null;
    vec_BaselineTestCaseCPtr newly_failed = vec_BaselineTestCaseCPtr_init();
    vec_BaselineTestCaseCPtr newly_passed = vec_BaselineTestCaseCPtr_init();

    printf(CLR_STATUS "Running tests...\n\n" CLR0);
    c_foreach(i, vec_BaselineTestCase, self->cases) {
        intptr_t index = i.ref - self->cases.data;
        if (!test_passes_filter(self, cstr_str(&i.ref->name))) {
            continue;
        }

        cstr_clear(&display);
        cstr_append_fmt(&display, "%ld%%", index * 100 / test_count);
        printf("        \x1b[1;44m%5s " CLR0 "  %s", cstr_str(&display), cstr_str(&i.ref->name));
        fflush(stdout);

        BaselineTestCase_run(i.ref, false);
        BaselineTestCase_clear_detail(i.ref);

        if (BaselineTestCase_is_pass(i.ref)) {
            passed++;
        } else {
            failed++;
        }

        if (BaselineTestCase_has_new_pass(i.ref)) {
            vec_BaselineTestCaseCPtr_push(&newly_passed, i.ref);
        }

        if (BaselineTestCase_has_new_fail(i.ref)) {
            vec_BaselineTestCaseCPtr_push(&newly_failed, i.ref);
        }

        cstr_clear(&display);
        append_test_result_to_cstr(&display, i.ref, true);
        printf("\r%s\n", cstr_str(&display));
    }

    printf("\n  " CLR_GOOD "%d" CLR0 " passed, " CLR_ERR "%d" CLR0 " failed\n\n", passed, failed);

    intptr_t newly_passed_size = vec_BaselineTestCaseCPtr_size(&newly_passed);
    if (newly_passed_size) {
        printf(
            CLR_GOOD "%ld" CLR0 " test%s have newly passing components:\n",
            newly_passed_size, newly_passed_size == 1 ? "" : "s");

        c_foreach(i, vec_BaselineTestCaseCPtr, newly_passed) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, *i.ref, false);
            printf("%s (%s)\n", cstr_str(&display), cstr_str(&(*i.ref)->source_path));
        }
        printf("\n");
    }

    intptr_t newly_failed_size = vec_BaselineTestCaseCPtr_size(&newly_failed);
    if (newly_failed_size) {
        printf(
            CLR_ERR "%ld" CLR0 " test%s have newly failing components:\n",
            newly_failed_size, newly_failed_size == 1 ? "" : "s");

        c_foreach(i, vec_BaselineTestCaseCPtr, newly_failed) {
            cstr_clear(&display);
            append_test_result_to_cstr(&display, *i.ref, false);
            printf("%s (%s)\n", cstr_str(&display), cstr_str(&(*i.ref)->source_path));
        }
        printf("\n");
    }

    c_drop(vec_BaselineTestCaseCPtr, &newly_failed, &newly_passed);
    cstr_drop(&display);
}

void BaselineTestSuite_save_results(BaselineTestSuite* self, const char* results_path) {
    printf(CLR_STATUS "Saving results...\n" CLR0);
    FILE *f = fopen(results_path, "w");
    if (!f) {
        PANIC("Failed to write to %s: %s\n", results_path, strerror(errno));
    }
    fprintf(f, "const baselines = {\n");

    c_foreach(i, vec_BaselineTestCase, self->cases) {
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

arc_Object load_saved_results(const char* results_path) {
    cstr results_source;

    if (!read_entire_file(results_path, true, &results_source)) {
        return arc_Object_init();
    }

    ErrorCtx errs = ErrorCtx_init();
    Block ast = parse_file(cstr_str(&results_source), &errs);
    Block_resolve_names_toplevel(&ast, &errs);
    Block_typecheck_toplevel(&ast, &errs);
    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_display(&errs);
        ErrorCtx_drop(&errs);
        Block_drop(&ast);
        cstr_drop(&results_source);
        return arc_Object_init();
    }

    arc_Object root = arc_Object_make((ObjectValue){0});
    eval_program(&ast, arc_Object_clone(root));

    Result baselines_result = ObjectValue_get(root.get, c_sv("baselines"));
    arc_Object ret = baselines_result.value.tag == VALUE_OBJECT ?
        arc_Object_clone(baselines_result.value.object) :
        arc_Object_make((ObjectValue){0});

    Result_drop(&baselines_result);
    arc_Object_drop(&root);
    Block_drop(&ast);
    ErrorCtx_drop(&errs);
    cstr_drop(&results_source);

    return ret;
}

box_BaselineTestSuite load_baseline_test_suite(
    const char* tests_dir,
    const char* filter_re,
    const char* results_path
) {
    printf(CLR_STATUS "Loading saved results...\n" CLR0);
    arc_Object prev_results = load_saved_results(results_path);

    box_BaselineTestSuite ret = box_BaselineTestSuite_make((BaselineTestSuite){
        .tests_dir = tests_dir,
        .has_filter = filter_re != nullptr,
        .filter = filter_re ? cregex_from(filter_re, CREG_ICASE) : cregex_init(),
        .prev_results = prev_results,
        .cases = vec_BaselineTestCase_init(),
    });

    printf(CLR_STATUS "Collecting tests...\n" CLR0);
    load_subdir(ret.get, "conformance");
    load_subdir(ret.get, "compiler");
    show_collected_test_counts(ret.get);
    printf("\n");
    printf(CLR_STATUS "Sorting tests...\n" CLR0);
    vec_BaselineTestCase_quicksort(&ret.get->cases);
    return ret;
}
