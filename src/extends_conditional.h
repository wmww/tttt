#pragma once

#include "type.h"

typedef struct ExtendsConditional {
    Type maybeSub;
    Type maybeSuper;
} ExtendsConditional;

void ExtendsConditional_drop(ExtendsConditional* self);
ExtendsConditional ExtendsConditional_clone(ExtendsConditional self);
bool ExtendsConditional_eval(ExtendsConditional* self);
