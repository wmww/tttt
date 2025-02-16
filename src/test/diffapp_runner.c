#include "baseline_test_case.h"
#include "filesystem.h"

int main(int argc, const char** argv) {
    ASSERT(argc == 2);
    csview test_path = csview_from(argv[1]);
    if (!fs_is_reg_file(&real_fs_noisy, test_path)) {
        PANIC("%.*s is not a file", c_SV(test_path));
    }

    BaselineTestCache cache = baseline_test_cache_init();
    BaselineTestCase test_case = run_baseline_test_case(&cache, test_path, true);
    cstr results = cstr_null;

    baseline_test_case_render_errors(&results, &test_case);
    printf("err %012zd\n%s\n", cstr_size(&results), cstr_str(&results));
    cstr_clear(&results);

    baseline_test_case_render_symbols(&results, &test_case);
    printf("sym %012zd\n%s\n", cstr_size(&results), cstr_str(&results));
    cstr_clear(&results);

    baseline_test_case_render_types(&results, &test_case);
    printf("typ %012zd\n%s\n", cstr_size(&results), cstr_str(&results));
    cstr_clear(&results);

    cstr_drop(&results);
    baseline_test_case_drop(&test_case);
    baseline_test_cache_drop(&cache);

    return 0;
}
