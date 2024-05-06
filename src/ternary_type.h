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
void cstr_append_ternary_type_debug(cstr* target, TernaryType* me, int indent);
void cstr_append_ternary_type_error_msg(cstr* target, TernaryType* me);
