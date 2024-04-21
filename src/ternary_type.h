#pragma once

#include "extends_conditional.h"

typedef struct TernaryType {
    ExtendsConditional conditional;
    Type TrueType;
    Type FalseType;
} TernaryType;

void ternary_type_drop(TernaryType* me);
TernaryType ternary_type_clone(TernaryType me);
void cstr_append_ternary_type_debug(cstr* target, TernaryType* me, int indent);
void cstr_append_ternary_type_error_msg(cstr* target, TernaryType* me);
void ternary_type_resolve_names(TernaryType* me, NameStack* names, ErrorCtx* errs);
// Does not return ownership to the returned type
Type const* ternary_type_resolve(TernaryType* me);
