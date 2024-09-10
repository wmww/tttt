#pragma once

#include "scope.h"

typedef struct NameStack NameStack;

NameStack* name_stack_new();
void name_stack_destroy(NameStack* me);
bool name_stack_has_function(NameStack* me);
NamedVariable* name_stack_get_variable(NameStack const* me, csview name);
NamedType* name_stack_get_type(NameStack const* me, csview name);
Scope const* name_stack_get_namespace(NameStack const* me, csview name);
