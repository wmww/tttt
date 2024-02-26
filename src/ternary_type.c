#include "ternary_type.h"

void TernaryType_drop(TernaryType* self) {
    ExtendsConditional_drop(&self->conditional);
    Type_drop(&self->trueType);
    Type_drop(&self->falseType);
}

cstr TernaryType_to_cstr(TernaryType* self) {
    cstr conditional = ExtendsConditional_to_cstr(&self->conditional);
    cstr trueType = Type_to_cstr(&self->trueType);
    cstr falseType = Type_to_cstr(&self->falseType);
    cstr ret = cstr_from_fmt("%s ? %s : %s", cstr_str(&conditional), cstr_str(&trueType), cstr_str(&falseType));
    cstr_drop(&conditional);
    cstr_drop(&trueType);
    cstr_drop(&falseType);
    return ret;
}
