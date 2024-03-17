#pragma once

#include "type.h"

typedef struct ExtendsConditional {
    Type MaybeSub;
    Type MaybeSuper;
} ExtendsConditional;

void extends_conditional_drop(ExtendsConditional* me);
ExtendsConditional extends_conditional_clone(ExtendsConditional me);
bool extends_conditional_eval(ExtendsConditional* me);
