#include <stdio.h>
#include <stdarg.h>
#include <stc/cstr.h>

#include "common_macros.h"
#include "test_runner.h"
#include "failable.h"
#include "uri.h"
#include "log.h"

static const char* color_normal = "\x1b[0m";
static const char* color_error = "\x1b[1;31m";
static const char* color_good = "\x1b[32m";
static const char* color_meh = "\x1b[33m";
static const char* color_header = "\x1b[1;34m";
static const char* color_subtle = "\x1b[1;30m";

typedef struct StdoutTestOutput {
    TestOutput super;
    int width;
} StdoutTestOutput;

static void stdout_test_output_module_started(void* data, csview module) {
    StdoutTestOutput* me = data;
    printf("\n");
    for (int i = 0; i < (my width - csview_size(module)) / 2; i++) printf(" ");
    printf("%s%.*s%s\n", color_header, c_SV(module), color_normal);
}

static void stdout_test_output_test_started(void* data, TestData const* test) {
    StdoutTestOutput* me = data;
    ignore me;
    printf("%s ", cstr_str(&test->name));
    fflush(stdout);
}

static void show_test_failure(TestFailure* failure) {
    cstr file_link_uri = cstr_null;
    try (existing_file_uri_encode_append(csview_from(failure->file), failure->line, &file_link_uri)) or_log(warn);
    printf(
        "%.*s%s%.*s%s%.*s:%d: %s%s%s%.*s\n",
        c_SV(terminal_link_begin_uri), cstr_str(&file_link_uri), c_SV(terminal_link_uri_to_text),
        color_header, c_SV(debug_clean_up_path(csview_from(failure->file))), failure->line,
        color_error, cstr_str(&failure->message), color_normal, c_SV(terminal_link_end_text)
    );
    cstr_drop(&file_link_uri);
    if (cstr_size(&failure->expected) || cstr_size(&failure->actual)) {
        printf("  expected: %s%s%s\n", color_meh, cstr_str(&failure->expected), color_normal);
        printf("    actual: %s%s%s\n", color_error, cstr_str(&failure->actual), color_normal);
    }
}

static void stdout_test_output_test_finished(void* data, TestData const* test, bool is_pass, VecTestFailure failures) {
    StdoutTestOutput* me = data;
    ignore me;
    ignore test;
    ignore is_pass;
    int result_chars = is_pass ? 5 : 9;
    if (!is_pass) printf("%s", color_subtle);
    for (int i = 0; i < my width - cstr_size(&test->name) - 1 - result_chars; i++) {
        if (is_pass || (i % 2)) printf(" ");
        else printf("-");
    }
    if (is_pass) {
        printf(" %syay!%s\n", color_good, color_normal);
    } else {
        printf(" %soh no :(%s\n", color_error, color_normal);
    }
    for_each (failure, vec_test_failure, &failures) {
        if (!failure_index) printf("\n");
        show_test_failure(failure);
        printf("\n");
    }
    vec_test_failure_drop(&failures);
}

static void stdout_test_output_run_finished(void* data, u64 passed, u64 failed, u64 skipped) {
    StdoutTestOutput* me = data;
    ignore me;
    printf("\n");
    if (failed == 0 && passed == 0) {
        printf("%sNo tests ran%s\n", color_meh, color_normal);
    } else if (failed == 0) {
        if (skipped > 0) {
            printf(
                "%s%llu test%s passed%s\n",
                color_good, (unsigned long long)passed, passed > 1 ? "s" : "", color_normal
            );
        } else {
            printf(
                "%sAll %llu test%s passed%s\n",
                color_good, (unsigned long long)passed, passed > 1 ? "s" : "", color_normal
            );
        }
    } else {
        printf(
            "%llu test%s passed, %s%llu test%s failed%s\n",
            (unsigned long long)passed, passed > 1 ? "s" : "",
            color_error, (unsigned long long)failed, failed > 1 ? "s" : "", color_normal
        );
    }
    printf("\n");
}

TestOutput* make_stdout_test_output() {
    StdoutTestOutput* me = c_new(StdoutTestOutput, {
        .super = {
            .module_started = stdout_test_output_module_started,
            .test_started = stdout_test_output_test_started,
            .test_finished = stdout_test_output_test_finished,
            .run_finished = stdout_test_output_run_finished,
        },
        .width = 80,
    });
    me->super.data = me;
    return &me->super;
}

void drop_stdout_test_output(TestOutput* data) {
    StdoutTestOutput* me = (void*)data;
    free(me);
}
