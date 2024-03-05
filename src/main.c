#include "type.h"
#include "parser.h"
#include "block.h"
#include "filesystem.h"
#include "error.h"
#include "engine.h"

int main(int argc, const char** argv) {
    if (argc > 2) {
        PANIC("too many arguments");
    } else if (argc < 2) {
        PANIC("too few arguments");
    }

    cstr contents = cstr_null;
    if (!read_entire_file(argv[1], true, &contents)) {
        PANIC("failed to read file");
    }

    ErrorCtx errs = {0};
    Block parsed = parse_file(cstr_str(&contents), &errs);

    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_display(&errs, cstr_str(&contents));
        ErrorCtx_drop(&errs);
        Block_drop(&parsed);
        cstr_drop(&contents);
        return 1;
    }

    cstr parsed_str = cstr_null;
    cstr_append_block_debug(&parsed_str, &parsed, 0);
    printf(CLR_GOOD "Parsed:" CLR0 "\n%s\n\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);

    Block_typecheck(&parsed, &errs);

    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_display(&errs, cstr_str(&contents));
        ErrorCtx_drop(&errs);
        Block_drop(&parsed);
        cstr_drop(&contents);
        return 1;
    }

    printf(CLR_GOOD "Typecheck passed, running..." CLR0 "\n");

    eval_program(&parsed);

    printf(CLR_GOOD "Run succeeded" CLR0 "\n");

    cstr_drop(&contents);
    ErrorCtx_drop(&errs);
    Block_drop(&parsed);
    return 0;
}
