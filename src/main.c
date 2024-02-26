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
    Scope parsed = parse_file(cstr_str(&contents));
    cstr parsed_str = Scope_to_cstr(&parsed, 0);
    printf("Parsed:\n%s\n", cstr_str(&parsed_str));
    cstr_drop(&contents);
    cstr_drop(&parsed_str);
    Scope_drop(&parsed);
    return 0;
}
