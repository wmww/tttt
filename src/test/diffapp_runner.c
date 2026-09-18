#include "baseline_test_case.h"

#include <cleat/filesystem.h>

#ifndef DEBUG_ALLOCATOR
#error debug allocator should be enabled for diffapp runner builds
#endif

// The diffapp protocol prefixes each section with its length, always as exactly 12 digits
static void print_section(const char* tag, cstr const* body) {
    cstr size = cstr_d(cstr_size(body));
    isize padding = 12 - cstr_size(&size);
    println(tag, " ", repeated_d("0", padding > 0 ? padding : 0), consume_d(size), "\n", *body);
}

int main(int argc, const char** argv) {
    if (argc != 2) {
        eprintln("USAGE: diffapp_runner [TSPATH]");
        return 1;
    }
    csview test_path = csview_from(argv[1]);
    if (!fs_is_reg_file(fs, test_path)) {
        eprintln(test_path, " is not a file");
        return 1;
    }

    BaselineTestCache cache = baseline_test_cache_init();
    cstr results = cstr_null;

    int c = '\n';
    while (c != EOF) {

#ifdef DEBUG_ALLOCATOR
        debug_allocation_count = 0;
#endif

        BaselineTestCase test_case = run_baseline_test_case(&cache, test_path, c_sv(""), true, false);
        baseline_test_case_render_errors(&results, &test_case);
        print_section("err", &results);
        cstr_clear(&results);

        baseline_test_case_render_symbols(&results, &test_case);
        print_section("sym", &results);
        cstr_clear(&results);

        baseline_test_case_render_types(&results, &test_case);
        print_section("typ", &results);
        cstr_clear(&results);

        baseline_test_case_drop(&test_case);

#ifdef DEBUG_ALLOCATOR
        if (debug_allocation_count != 0) {
            // Run again in case the increase came from the cache
            debug_allocation_count = 0;
            BaselineTestCase rerun_case = run_baseline_test_case(&cache, test_path, c_sv(""), true, false);
            baseline_test_case_drop(&rerun_case);
            if (debug_allocation_count != 0) {
                eprintln("Detected ", debug_allocation_count, " net allocations");
                println("mem");
            }
        }
#endif

        fflush(stderr);
        println("end");
        fflush(stdout);
        c = getchar();
    }

    cstr_drop(&results);
    baseline_test_cache_drop(&cache);

    return 0;
}
