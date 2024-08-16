#include "type.h"
#include "parser.h"
#include "block.h"
#include "filesystem.h"
#include "error.h"
#include "engine.h"
#include "display.h"

int main(int argc, const char** argv) {
    cstr contents = cstr_null;
    ErrorCtx errs = error_ctx_init();
    Block ast = {0};
    int ret = 0;

    if (argc > 2) {
        printf("%sToo many arguments%s\n", color_error, color_normal);
        return 1;
    } else if (argc < 2) {
        printf("%sToo few arguments%s\n", color_error, color_normal);
        return 1;
    }

    if (!read_entire_file(argv[1], true, &contents)) {
        ret = 1;
        goto cleanup;
    }

    ast = parse_file(cstr_str(&contents), &errs);
    if (!error_ctx_empty(&errs)) {
        print_error_ctx(&errs);
        error_ctx_drop(&errs);
        errs = error_ctx_init();
        ret = 1;
    }

    block_resolve_names_toplevel(&ast, &errs);
    if (!error_ctx_empty(&errs)) {
        print_error_ctx(&errs);
        error_ctx_drop(&errs);
        errs = error_ctx_init();
        ret = 1;
    }

    block_typecheck_toplevel(&ast, &errs);

    if (error_ctx_empty(&errs)) {
        printf("%sTypecheck passed%s\n", color_good, color_normal);
    } else {
        printf("%sTypecheck failed%s\n", color_error, color_normal);
    }

    cstr parsed_str = cstr_null;
    display_block(debug_display(&parsed_str), &ast);
    printf("%sParsed:%s\n", color_good, color_normal);
    printf("%s\n\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);

    if (!error_ctx_empty(&errs)) {
        print_error_ctx(&errs);
        error_ctx_drop(&errs);
        errs = error_ctx_init();
        ret = 1;
    }

    printf("%sRunning...%s\n", color_good, color_normal);
    eval_program(&ast, arc_js_object_init());
    printf("%sRun succeeded%s\n", color_good, color_normal);

cleanup:
    error_ctx_drop(&errs);
    block_drop(&ast);
    cstr_drop(&contents);
    return ret;
}
