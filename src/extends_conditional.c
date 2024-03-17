#include "extends_conditional.h"

void extends_conditional_drop(ExtendsConditional* me) {
    type_drop(&my MaybeSub);
    type_drop(&my MaybeSuper);
}

ExtendsConditional extends_conditional_clone(ExtendsConditional me) {
    return (ExtendsConditional) {
        .MaybeSub = type_clone(me.MaybeSub),
        .MaybeSuper = type_clone(me.MaybeSuper),
    };
}

bool extends_conditional_eval(ExtendsConditional* me) {
    return type_extends(&my MaybeSub, &my MaybeSuper);
}
