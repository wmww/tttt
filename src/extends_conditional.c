#include "extends_conditional.h"

void ExtendsConditional_drop(ExtendsConditional* self) {
    Type_drop(&self->maybeSub);
    Type_drop(&self->maybeSuper);
}

cstr ExtendsConditional_to_cstr(ExtendsConditional* self, int indent) {
    cstr subType = Type_to_cstr(&self->maybeSub, indent);
    cstr superType = Type_to_cstr(&self->maybeSuper, indent);
    cstr ret = cstr_from_fmt("%s extends %s", cstr_str(&subType), cstr_str(&superType));
    cstr_drop(&subType);
    cstr_drop(&superType);
    return ret;
}

bool ExtendsConditional_eval(ExtendsConditional* self) {
    return Type_extends(&self->maybeSub, &self->maybeSuper);
}
