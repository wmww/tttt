#include "baseline_test_suite.h"
#include "baseline_test_case.h"
#include "filesystem.h"
#include "string_helpers.h"
#include "stc/cregex.h"

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
        "\rCollected %ld, skipped %ld            ",
        vec_BaselineTestCase_size(&self->cases),
        self->skipped_count
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
        if (test_passes_filter(self, cstr_str(&name))) {
            vec_BaselineTestCase_push(&self->cases, make_baseline_test_case(self, name, full_path));
        } else {
            self->skipped_count++;
            cstr_drop(&name);
            cstr_drop(&full_path);
        }
        self->total_count++;
        if (self->total_count % 100 == 0) {
            show_collected_test_counts(self);
        }
    } else {
        cstr_drop(&full_path);
    }
}

static void load_subdir(BaselineTestSuite* self, const char* subdir) {
    cstr full_dir = cstr_from_fmt("%s/cases/%s", self->tests_dir, subdir);
    if (!path_is_directory(cstr_str(&full_dir))) {
        PANIC("%s is not a directory", cstr_str(&full_dir));
    }
    for_each_item_in_dir(cstr_str(&full_dir), load_path, self);
    cstr_drop(&full_dir);
}

void BaselineTestSuite_drop(BaselineTestSuite* self) {
    cregex_drop(&self->filter);
    vec_BaselineTestCase_drop(&self->cases);
}

void BaselineTestSuite_run(BaselineTestSuite* self) {
    printf(CLR_GOOD "Running tests...\n" CLR0);
    int passed = 0, failed = 0;
    c_foreach(i, vec_BaselineTestCase, self->cases) {
        printf("%s... ", cstr_str(&i.ref->name));
        fflush(stdout);
        if (BaselineTestCase_run(i.ref, false)) {
            printf(CLR_GOOD "passed\n" CLR0);
            passed++;
        } else {
            printf(CLR_ERR "failed\n" CLR0);
            failed++;
        }
        BaselineTestCase_clear_detail(i.ref);
    }
    printf("\n  " CLR_GOOD "%d" CLR0 " passed, " CLR_ERR "%d" CLR0 " failed\n\n", passed, failed);
}

box_BaselineTestSuite load_baseline_test_suite(const char* tests_dir, const char* filter_re) {
    printf(CLR_GOOD "Collecting tests...\n" CLR0);

    box_BaselineTestSuite ret = box_BaselineTestSuite_make((BaselineTestSuite){
        .tests_dir = tests_dir,
        .has_filter = filter_re != nullptr,
        .filter = filter_re ? cregex_from(filter_re, CREG_ICASE) : cregex_init(),
        .cases = vec_BaselineTestCase_init(),
        .total_count = 0,
        .skipped_count = 0,
    });
    load_subdir(ret.get, "conformance");
    load_subdir(ret.get, "compiler");
    show_collected_test_counts(ret.get);
    printf("\n");
    return ret;
}
