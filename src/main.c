#include "type.h"
#include "extends_conditional.h"
#include "ternary_type.h"

int main() {
    Type t = type_ternary_new(c_new(TernaryType, {
        .conditional = (ExtendsConditional){
            .maybeSub = type_boolean_new(),
            .maybeSuper = type_number_new(),
        },
        .trueType = type_number_new(),
        .falseType = type_string_new(),
    }));
    cstr initial_s = Type_to_cstr(&t);
    printf("   Initial: %s\n", cstr_str(&initial_s));
    cstr_drop(&initial_s);
    t = type_simplify(t);
    cstr simplified_s = Type_to_cstr(&t);
    printf("Simplified: %s\n", cstr_str(&simplified_s));
    cstr_drop(&simplified_s);
    return 0;
}
