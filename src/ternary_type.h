#pragma once

#include "extends_conditional.h"

typedef struct TernaryType {
    ExtendsConditional conditional;
    Type trueType;
    Type falseType;
} TernaryType;

void TernaryType_drop(TernaryType* self);
TernaryType TernaryType_clone(TernaryType self);
void cstr_append_ternary_type_debug(cstr* target, TernaryType* self, int indent);
// Does not return ownership to the returned type
Type* TernaryType_resolve(TernaryType* self);
