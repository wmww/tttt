#include "baseline_test_case.h"
#include "filesystem.h"

#ifndef DEBUG_ALLOCATOR
#error debug allocator should be enabled for diffapp runner builds
#endif

int main(int argc, const char** argv) {
    ASSERT(argc == 2);
    csview test_path = csview_from(argv[1]);
    if (!fs_is_reg_file(&real_fs_noisy, test_path)) {
        PANIC("%.*s is not a file", c_SV(test_path));
    }

    BaselineTestCache cache = baseline_test_cache_init();
    cstr results = cstr_null;

    int c = '\n';
    while (c != EOF) {

#ifdef DEBUG_ALLOCATOR
        debug_allocation_count = 0;
#endif

        BaselineTestCase test_case = run_baseline_test_case(&cache, test_path, true);
        baseline_test_case_render_errors(&results, &test_case);
        printf("err %012zd\n%s\n", cstr_size(&results), cstr_str(&results));
        cstr_clear(&results);

        baseline_test_case_render_symbols(&results, &test_case);
        printf("sym %012zd\n%s\n", cstr_size(&results), cstr_str(&results));
        cstr_clear(&results);

        baseline_test_case_render_types(&results, &test_case);
        printf("typ %012zd\n%s\n", cstr_size(&results), cstr_str(&results));
        cstr_clear(&results);

        baseline_test_case_drop(&test_case);

#ifdef DEBUG_ALLOCATOR
        if (debug_allocation_count != 0) {
            // Run again in case the increase came from the cache
            debug_allocation_count = 0;
            BaselineTestCase rerun_case = run_baseline_test_case(&cache, test_path, false);
            baseline_test_case_drop(&rerun_case);
            if (debug_allocation_count != 0) {
                fprintf(stderr, "Detected %zd net allocations\n", debug_allocation_count);
                printf("mem\n");
            }
        }
#endif

        fflush(stderr);
        printf("end\n");
        fflush(stdout);
        c = getchar();
    }

    cstr_drop(&results);
    baseline_test_cache_drop(&cache);

    return 0;
}
