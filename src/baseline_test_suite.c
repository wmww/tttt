#include "baseline_test_suite.h"
#include "baseline_test_case.h"
#include "filesystem.h"
#include "string_helpers.h"

static void load_path(void* data, const char* dir_path, const char* name) {
    BaselineTestSuite* self = data;
    cstr full_path = cstr_from_fmt("%s/%s", dir_path, name);
    if (path_is_directory(cstr_str(&full_path))) {
        for_each_item_in_dir(cstr_str(&full_path), load_path, self);
        cstr_drop(&full_path);
    } else if (path_is_reg_file(cstr_str(&full_path)) && cstr_ends_with(&full_path, ".ts")) {
        vec_BaselineTestCase_push(&self->cases, make_baseline_test_case(self, full_path));
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

BaselineTestSuite load_baseline_test_suite(const char* tests_dir) {
    BaselineTestSuite ret = {
        .tests_dir = tests_dir,
        .cases = vec_BaselineTestCase_init(),
    };
    load_subdir(&ret, "conformance");
    load_subdir(&ret, "compiler");
    return ret;
}

void BaselineTestSuite_drop(BaselineTestSuite* self) {
    vec_BaselineTestCase_drop(&self->cases);
}

void BaselineTestSuite_run(BaselineTestSuite* self) {
    c_foreach(i, vec_BaselineTestCase, self->cases) {
        if (BaselineTestCase_run(i.ref, false)) {
            printf("%s " CLR_GOOD "passed\n" CLR0, cstr_str(&i.ref->name));
        } else {
            printf("%s " CLR_ERR "failed\n" CLR0, cstr_str(&i.ref->name));
        }
        BaselineTestCase_clear_detail(i.ref);
    }
}
