#pragma once

#include "type.h"

typedef struct TernaryType {
    Type maybe_sub;
    Type maybe_super;
    Type if_true;
    Type if_false;
} TernaryType;

void ternary_type_drop(TernaryType* me);
TernaryType ternary_type_clone(TernaryType me);
