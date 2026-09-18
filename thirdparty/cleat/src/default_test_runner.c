#include <stdio.h>
#include <stc/cstr.h>

#include "test.h"
#include "test_runner.h"
#include "common_macros.h"
#include "log.h"

static void print_help(FILE* target) {
    fprintf(target,
// 80:   01234567890123456789012345678901234567890123456789012345678901234567890123456789
        "cleat test runner\n"
        "  FILTER: Regex filter. Test will run if its name, module, or name::module is a\n"
        "          fullmatch. Multiple filters can be given.\n\n"
        "  --help: Show this help message\n\n"
    );
}

int main(int argc, const char** argv) {
    RunTestsArgs run_args = {0};
    cstr filter = cstr_lit("");
    int ret = 1;
    for (i64 i = 1; i < argc; i++) {
        csview arg = csview_from(argv[i]);
        if (csview_starts_with(arg, "-")) {
            if (csview_equals(arg, "--help") || csview_equals(arg, "-h")) {
                print_help(stdout);
                ret = 0;
                goto cleanup;
            } else {
                fprintf(stderr, "Unkown flag %.*s\n\n", c_SV(arg));
                print_help(stderr);
                goto cleanup;
            }
        } else {
            if (cstr_size(&filter)) {
                cstr_append_lit(&filter, "|");
            }
            // Using CREG_FULLMATCH causes the filter (.*) to not match anything?? so emulate it with ^ and $ instead
            cstr_append_lit(&filter, "(^");
            cstr_append_sv(&filter, arg);
            cstr_append_lit(&filter, "$)");
        }
    }
    if (cstr_size(&filter)) {
        run_args.filter = cstr_str(&filter);
    }
    ret = run_tests(run_args) ? 0 : 1;

cleanup:
    cstr_drop(&filter);
    drop_test_modules();
    return ret;
}
