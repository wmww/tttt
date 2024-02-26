#include "type.h"
#include "parser.h"
#include "scope.h"

int main() {
    Scope parsed = parse_file("interface Foo { a: boolean, b: any, c: { a: undefined, c: null } }");
    cstr parsed_str = Scope_to_cstr(&parsed, 0);
    printf("Parsed:\n%s\n", cstr_str(&parsed_str));
    cstr_drop(&parsed_str);
    Scope_drop(&parsed);
    return 0;
}
