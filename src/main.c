#include "type.h"
#include "parser.h"
#include "scope.h"
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
    Scope parsed = parse_file(cstr_str(&contents), &errs);
    cstr_drop(&contents);

    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_display(&errs);
        ErrorCtx_drop(&errs);
        Scope_drop(&parsed);
        return 1;
    }

    cstr parsed_str = Scope_to_cstr(&parsed, 0);
    printf(CLR_GOOD "Parsed:" CLR0 "\n%s\n\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);

    Scope_typecheck(&parsed, &errs);

    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_display(&errs);
        ErrorCtx_drop(&errs);
        Scope_drop(&parsed);
        return 1;
    }

    printf(CLR_GOOD "Typecheck passed, running..." CLR0 "\n");

    eval_program(&parsed);

    printf(CLR_GOOD "Run succeeded" CLR0 "\n");

    ErrorCtx_drop(&errs);
    Scope_drop(&parsed);
    return 0;
}
