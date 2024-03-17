#pragma once

#include "type.h"

typedef struct ExtendsConditional {
    Type MaybeSub;
    Type MaybeSuper;
} ExtendsConditional;

void extends_conditional_drop(ExtendsConditional* self);
ExtendsConditional extends_conditional_clone(ExtendsConditional self);
bool extends_conditional_eval(ExtendsConditional* self);
