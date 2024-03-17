#include "extends_conditional.h"

void extends_conditional_drop(ExtendsConditional* self) {
    type_drop(&self->MaybeSub);
    type_drop(&self->MaybeSuper);
}

ExtendsConditional extends_conditional_clone(ExtendsConditional self) {
    return (ExtendsConditional) {
        .MaybeSub = type_clone(self.MaybeSub),
        .MaybeSuper = type_clone(self.MaybeSuper),
    };
}

bool extends_conditional_eval(ExtendsConditional* self) {
    return type_extends(&self->MaybeSub, &self->MaybeSuper);
}
