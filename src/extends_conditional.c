#include "extends_conditional.h"

void ExtendsConditional_drop(ExtendsConditional* self) {
    Type_drop(&self->maybeSub);
    Type_drop(&self->maybeSuper);
}

cstr ExtendsConditional_to_cstr(ExtendsConditional* self) {
    cstr subType = Type_to_cstr(&self->maybeSub);
    cstr superType = Type_to_cstr(&self->maybeSuper);
    cstr ret = cstr_from_fmt("%s extends %s", cstr_str(&subType), cstr_str(&superType));
    cstr_drop(&subType);
    cstr_drop(&superType);
    return ret;
}

bool extends_conditional_eval(ExtendsConditional* self) {
    return type_extends(&self->maybeSub, &self->maybeSuper);
}
