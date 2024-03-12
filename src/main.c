#include "type.h"
#include "parser.h"
#include "block.h"
#include "statement.h"
#include "name_stack.h"
#include "filesystem.h"
#include "error.h"
#include "engine.h"

int main(int argc, const char** argv) {
    cstr contents = cstr_null;
    ErrorCtx errs = {0};
    Block ast = {0};
    int ret = 0;

    if (argc > 2) {
        PANIC("too many arguments");
    } else if (argc < 2) {
        PANIC("too few arguments");
    }

    if (!read_entire_file(argv[1], true, &contents)) {
        ret = 1;
        goto cleanup;
    }

    ast = parse_file(cstr_str(&contents), &errs);
    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_display(&errs);
        ret = 1;
        goto cleanup;
    }

    NameStack names = NameStack_init();
    Block_resolve_names(&ast, &names, &errs);
    NameStack_drop(&names);
    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_display(&errs);
        ret = 1;
        goto cleanup;
    }

    StatementCtx statement_ctx = StatementCtx_toplevel();
    Block_typecheck(&ast, &statement_ctx, &errs);
    StatementCtx_drop(&statement_ctx);

    if (ErrorCtx_empty(&errs)) {
        printf(CLR_GOOD "Typecheck passed" CLR0 "\n");
    } else {
        printf(CLR_ERR "Typecheck failed" CLR0 "\n");
    }

    cstr parsed_str = cstr_null;
    cstr_append_block_debug(&parsed_str, &ast, 0);
    printf(CLR_GOOD "Parsed:" CLR0 "\n%s\n\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);

    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_display(&errs);
        ret = 1;
        goto cleanup;
    }

    printf(CLR_GOOD "Running..." CLR0 "\n");

    eval_program(&ast);

    printf(CLR_GOOD "Run succeeded" CLR0 "\n");

cleanup:
    cstr_drop(&contents);
    ErrorCtx_drop(&errs);
    Block_drop(&ast);
    return ret;
}
