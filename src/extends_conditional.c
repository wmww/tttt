#include "extends_conditional.h"

void ExtendsConditional_drop(ExtendsConditional* self) {
    Type_drop(&self->maybeSub);
    Type_drop(&self->maybeSuper);
}

ExtendsConditional ExtendsConditional_clone(ExtendsConditional self) {
    return (ExtendsConditional) {
        .maybeSub = Type_clone(self.maybeSub),
        .maybeSuper = Type_clone(self.maybeSuper),
    };
}

bool ExtendsConditional_eval(ExtendsConditional* self) {
    return Type_extends(&self->maybeSub, &self->maybeSuper);
}
