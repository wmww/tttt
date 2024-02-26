#include "type.h"
#include "parser.h"
#include "scope.h"
#include "filesystem.h"

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
    Scope parsed = {0};
    if (!parse_file(cstr_str(&contents), &parsed)) {
        cstr_drop(&contents);
        PANIC("failed to parse");
    }
    cstr_drop(&contents);
    cstr parsed_str = Scope_to_cstr(&parsed, 0);
    printf("Parsed:\n%s\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);
    parsed = scope_simplify(parsed);
    parsed_str = Scope_to_cstr(&parsed, 0);
    printf("Simplified:\n%s\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);
    Scope_drop(&parsed);
    return 0;
}
