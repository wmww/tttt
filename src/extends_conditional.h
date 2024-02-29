#pragma once

#include "type.h"

typedef struct ExtendsConditional {
    Type maybeSub;
    Type maybeSuper;
} ExtendsConditional;

void ExtendsConditional_drop(ExtendsConditional* self);
ExtendsConditional ExtendsConditional_clone(ExtendsConditional self);
void cstr_append_extends_conditional_debug(cstr* target, ExtendsConditional* self, int indent);
bool ExtendsConditional_eval(ExtendsConditional* self);
