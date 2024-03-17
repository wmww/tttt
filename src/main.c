#include "type.h"
#include "parser.h"
#include "block.h"
#include "filesystem.h"
#include "error.h"
#include "engine.h"

int main(int argc, const char** argv) {
    cstr contents = cstr_null;
    ErrorCtx errs = {0};
    Block ast = {0};
    int ret = 0;

    if (argc > 2) {
        printf(CLR_ERR "too many arguments\n" CLR0);
        return 1;
    } else if (argc < 2) {
        printf(CLR_ERR "too few arguments\n" CLR0);
        return 1;
    }

    if (!read_entire_file(argv[1], true, &contents)) {
        ret = 1;
        goto cleanup;
    }

    ast = parse_file(cstr_str(&contents), &errs);
    if (!error_ctx_empty(&errs)) {
        error_ctx_display(&errs);
        ret = 1;
        goto cleanup;
    }

    block_resolve_names_toplevel(&ast, &errs);
    if (!error_ctx_empty(&errs)) {
        error_ctx_display(&errs);
        ret = 1;
        goto cleanup;
    }

    block_typecheck_toplevel(&ast, &errs);

    if (error_ctx_empty(&errs)) {
        printf(CLR_GOOD "Typecheck passed" CLR0 "\n");
    } else {
        printf(CLR_ERR "Typecheck failed" CLR0 "\n");
    }

    cstr parsed_str = cstr_null;
    cstr_append_block_debug(&parsed_str, &ast, 0);
    printf(CLR_GOOD "Parsed:" CLR0 "\n%s\n\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);

    if (!error_ctx_empty(&errs)) {
        error_ctx_display(&errs);
        ret = 1;
        goto cleanup;
    }

    printf(CLR_GOOD "Running..." CLR0 "\n");

    eval_program(&ast, arc_object_init());

    printf(CLR_GOOD "Run succeeded" CLR0 "\n");

cleanup:
    cstr_drop(&contents);
    error_ctx_drop(&errs);
    block_drop(&ast);
    return ret;
}
