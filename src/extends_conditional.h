#pragma once

#include "type.h"

typedef struct ExtendsConditional {
    Type maybeSub;
    Type maybeSuper;
} ExtendsConditional;

void ExtendsConditional_drop(ExtendsConditional* self);
cstr ExtendsConditional_to_cstr(ExtendsConditional* self, int indent);
bool ExtendsConditional_eval(ExtendsConditional* self);
