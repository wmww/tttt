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

void cstr_append_extends_conditional_debug(cstr* target, ExtendsConditional* self, int indent) {
    cstr_append_type_debug(target, &self->maybeSub, indent);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_debug(target, &self->maybeSuper, indent);
}

bool ExtendsConditional_eval(ExtendsConditional* self) {
    return Type_extends(&self->maybeSub, &self->maybeSuper);
}
