#include <stc/csview.h>
#include <stc/cstr.h>
#include <stc/cregex.h>

#include "test.h"
#include "subproc.h"
#include "common_macros.h"
#include "string_helpers.h"
#include "display.h"
#include "test_runner.h"
#include "log.h"
#include "failable.h"

TestData test_data_clone(TestData me) {
    me.name = cstr_clone(me.name);
    me.module = cstr_clone(me.module);
    return me;
}

void test_data_drop(TestData* me) {
    if (my drop) my drop(my data);
    cstr_drop(&my module);
    cstr_drop(&my name);
}

void test_failure_drop(TestFailure* me) {
    cstr_drop(&my message);
    cstr_drop(&my expected);
    cstr_drop(&my actual);
}

TestFailure test_failure_clone(TestFailure me) {
    me.message = cstr_clone(me.message);
    me.expected = cstr_clone(me.expected);
    me.actual = cstr_clone(me.actual);
    return me;
}

struct TestCtx {
    VecTestFailure failures;
    cstr subprocess_stdout;
    cstr subprocess_stderr;
    int subprocess_exit;
};

void test_ctx_drop(TestCtx* me) {
    vec_test_failure_drop(&my failures);
    cstr_drop(&my subprocess_stdout);
    cstr_drop(&my subprocess_stderr);
}

#define g_class_key TestData, test_data
#include <generic/vec.h>

#define g_type TestModuleMap, test_module_map
#define g_pro_key CStr, cstr
#define g_class_val VecTestData, vec_test_data
#include <generic/smap.h>

static TestModuleMap test_modules = {0};

csview test_module_from_file(const char* file) {
    csview ret = debug_clean_up_path(csview_from(file));
    ret = csview_without_prefix(ret, "test/");
    ret = csview_without_prefix(ret, "tests/");
    ret = csview_without_suffix(ret, ".c");
    return ret;
}

void register_test(csview module, csview name, TestImpl impl, TestDrop drop, void* test_data) {
    TestData test = {
        .module = cstr_from_sv(module),
        .name = cstr_from_sv(name),
        .impl = impl,
        .drop = drop,
        .data = test_data,
    };
    test_module_map_value* tests = test_module_map_get_mut(&test_modules, cstr_str(&test.module));
    if (tests) {
        vec_test_data_push(&tests->second, test);
    } else {
        test_module_map_insert(&test_modules, cstr_clone(test.module), c_init(vec_test_data, {test}));
    }
}

static void test_problem_impl(TestCtx* test_ctx, void const* test_data) {
    cstr const* message = test_data;
    test_fail_message("%s", cstr_str(message));
}

static void test_problem_drop(void* test_data) {
    cstr* message = test_data;
    cstr_drop(message);
    free(message);
}

void register_test_problem(csview module, csview name, cstr message) {
    register_test(module, name, test_problem_impl, test_problem_drop, c_new(cstr, message));
}

cstr test_format_string(csview sv) {
    return display_into_cstr(string_debug_display(sv, (DisplayCtx){}));
}

cstr test_format_regex(csview sv) {
    cstr ret = cstr_lit("to match ");
    for (i64 i = 0; i < csview_size(sv); i++) {
        switch (sv.buf[i]) {
            case '\n': cstr_append_lit(&ret, "\\n"); break;
            case '\t': cstr_append_lit(&ret, "\\t"); break;
            case '\r': cstr_append_lit(&ret, "\\r"); break;
            default: cstr_append_sv(&ret, c_sv(sv.buf + i, 1)); break;
        }
    }
    return ret;
}

void test_fail_internal(TestCtx* test_ctx, const char* file, int line, cstr message, cstr expected, cstr actual) {
    vec_test_failure_push(&test_ctx->failures, (TestFailure){
        .message = message,
        .expected = expected,
        .actual = actual,
        .file = file,
        .line = line,
    });
}

bool test_has_failures(TestCtx* test_ctx) {
    return !vec_test_failure_is_empty(&test_ctx->failures);
}

bool test_fork_internal(TestCtx* test_ctx) {
    cstr_take(&test_ctx->subprocess_stdout, cstr_null);
    cstr_take(&test_ctx->subprocess_stderr, cstr_null);
    test_ctx->subprocess_exit = -1;
    ForkSubprocRet fork_ret;
    try (fork_subproc(true, true, &fork_ret)) or_panic;
    if (fork_ret.in_child) {
        return true;
    } else {
        try (subproc_wait(
            fork_ret,
            &test_ctx->subprocess_stdout,
            &test_ctx->subprocess_stderr,
            &test_ctx->subprocess_exit,
            nullptr
        )) or_panic;
        return false;
    }
}

TestCtxCaptured test_ctx_get_captured(TestCtx* test_ctx) {
    return (TestCtxCaptured) {
        .out = cstr_str(&test_ctx->subprocess_stdout),
        .err = cstr_str(&test_ctx->subprocess_stderr),
        .exit = test_ctx->subprocess_exit,
    };
}

bool regex_matches_internal(const char* regex, csview value) {
    return cregex_match_aio_sv(regex, value, nullptr, CREG_DEFAULT) == CREG_OK;
}

bool run_test(TestData* data, TestOutput* output) {
    TestCtx ctx = {0};
    if (output->test_started) output->test_started(output->data, data);
    data->impl(&ctx, data->data);
    bool is_pass = vec_test_failure_is_empty(&ctx.failures);
    if (output->test_finished) output->test_finished(output->data, data, is_pass, vec_test_failure_move(&ctx.failures));
    test_ctx_drop(&ctx);
    return is_pass;
}

bool run_tests(RunTestsArgs args) {
    u64 passed = 0, failed = 0, skipped = 0;
    struct {
        bool exists;
        cregex re;
    } filter = {0};
    if (args.filter) {
        filter.exists = true;
        filter.re = cregex_make(args.filter, CREG_DOTALL | CREG_ICASE);
        panic_if(filter.re.error != CREG_OK, "Invalid regex ", args.filter);
    }
    TestOutput* stdout_test_output = nullptr;
    if (!args.output) {
        args.output = stdout_test_output = make_stdout_test_output();
    }
    if (args.output->run_started) args.output->run_started(args.output->data);
    for_each_kv (module, tests, test_module_map, &test_modules) {
        bool has_reported_module_started = false;
        bool module_passed_filter =
            !filter.exists ||
            cregex_match_sv(&filter.re, cstr_sv(module), nullptr, CREG_DEFAULT) == CREG_OK;
        if (module_passed_filter) {
            if (args.output->module_started) args.output->module_started(args.output->data, cstr_sv(module));
            has_reported_module_started = true;
        }
        for_each (test, vec_test_data, tests) {
            bool passed_filter = module_passed_filter;
            if (!passed_filter) {
                passed_filter = cregex_match_sv(&filter.re, cstr_sv(&test->name), nullptr, CREG_DEFAULT) == CREG_OK;
            }
            if (!passed_filter) {
                cstr combined_name = cstr_from_fmt("%s::%s", cstr_str(module), cstr_str(&test->name));
                passed_filter = cregex_match_sv(&filter.re, cstr_sv(&combined_name), nullptr, CREG_DEFAULT) == CREG_OK;
                cstr_drop(&combined_name);
            }
            if (passed_filter) {
                if (!has_reported_module_started) {
                    if (args.output->module_started) args.output->module_started(args.output->data, cstr_sv(module));
                    has_reported_module_started = true;
                }
                (*(run_test(test, args.output) ? &passed : &failed))++;
            } else {
                skipped++;
            }
        }
        if (has_reported_module_started) {
            if (args.output->module_finished) args.output->module_finished(args.output->data, cstr_sv(module));
        }
    }
    if (args.output->run_finished) args.output->run_finished(args.output->data, passed, failed, skipped);
    if (filter.exists) {
        cregex_drop(&filter.re);
    }
    if (stdout_test_output) {
        drop_stdout_test_output(stdout_test_output);
    }
    return passed > 0 && failed == 0;
}

void drop_test_modules() {
    test_module_map_drop(&test_modules);
    test_modules = test_module_map_init();
}
