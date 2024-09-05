#include "type.h"
#include "program.h"
#include "engine.h"
#include "error.h"
#include "display.h"
#include "source_file.h"
#include "string_helpers.h"

int main(int argc, const char** argv) {
    if (argc > 2) {
        printf("%sToo many arguments%s\n", color_error, color_normal);
        return 1;
    } else if (argc < 2) {
        printf("%sToo few arguments%s\n", color_error, color_normal);
        return 1;
    } else if (streq(argv[1], "--help") || streq(argv[1], "-h")) {
        printf(
            "The Tess TypeScript Typechecker\n\n"
            "USAGE: tttt [TSPATH]\n"
        );
        return 0;
    }

    ErrorCtx errs = error_ctx_init();
    error_config_enable_strict(&errs.conf);
    Program* program = program_new(&errs, nullptr);
    int ret = 0;

    SourceFile const* source = program_load(program, argv[1]);

    if (!error_ctx_empty(&errs)) {
        print_error_ctx(&errs);
        error_ctx_drop(&errs);
        errs = error_ctx_init();
        ret = 1;
    }

    program_typecheck(program, true);

    if (error_ctx_empty(&errs)) {
        printf("%sTypecheck passed%s\n", color_good, color_normal);
    } else {
        printf("%sTypecheck failed%s\n", color_error, color_normal);
    }

    cstr parsed_str = cstr_null;
    display_block(debug_display(&parsed_str), &source->ast);
    printf("%sParsed:%s\n", color_good, color_normal);
    printf("%s\n\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);

    if (!error_ctx_empty(&errs)) {
        print_error_ctx(&errs);
        errs = error_ctx_init();
        error_ctx_drop(&errs);
        ret = 1;
    }

    printf("%sRunning...%s\n", color_good, color_normal);
    run_in_engine(source, arc_js_object_init());
    printf("%sRun succeeded%s\n", color_good, color_normal);

    program_destroy(program);
    error_ctx_drop(&errs);
    return ret;
}
