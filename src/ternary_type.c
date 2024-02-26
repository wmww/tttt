#include "ternary_type.h"

void TernaryType_drop(TernaryType* self) {
    ExtendsConditional_drop(&self->conditional);
    Type_drop(&self->trueType);
    Type_drop(&self->falseType);
}

cstr TernaryType_to_cstr(TernaryType* self, int indent) {
    cstr conditional = ExtendsConditional_to_cstr(&self->conditional, indent);
    cstr trueType = Type_to_cstr(&self->trueType, indent + 1);
    cstr falseType = Type_to_cstr(&self->falseType, indent + 1);
    cstr ret = cstr_null;
    cstr_append_s(&ret, conditional);
    cstr_append(&ret, "\n");
    APPEND_INDENT(&ret, indent + 1);
    cstr_append(&ret, "? ");
    cstr_append_s(&ret, trueType);
    cstr_append(&ret, "\n");
    APPEND_INDENT(&ret, indent + 1);
    cstr_append(&ret, ": ");
    cstr_append_s(&ret, falseType);
    cstr_drop(&conditional);
    cstr_drop(&trueType);
    cstr_drop(&falseType);
    return ret;
}
