#pragma once

#include "extends_conditional.h"

typedef struct TernaryType {
    ExtendsConditional conditional;
    Type trueType;
    Type falseType;
} TernaryType;

void TernaryType_drop(TernaryType* self);
cstr TernaryType_to_cstr(TernaryType* self, int indent);
