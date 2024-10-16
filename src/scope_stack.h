#pragma once

#include "type.h"

typedef struct ScopeStack ScopeStack;
typedef struct Declaration Declaration;

ScopeStack* scope_stack_new();
void scope_stack_destroy(ScopeStack* me);
bool scope_stack_has_function(ScopeStack* me);
Declaration const* scope_stack_get_value(ScopeStack const* me, csview name);
Declaration const* scope_stack_get_type(ScopeStack const* me, csview name);
Declaration const* scope_stack_get_namespace(ScopeStack const* me, csview name);
Declaration const* scope_stack_get_keyword(ScopeStack const* me, KeywordIdentifier keyword);
