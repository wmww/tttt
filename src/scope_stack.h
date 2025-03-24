#pragma once

#include "type.h"

typedef struct ScopeStack ScopeStack;
typedef struct Declaration Declaration;

ScopeStack* scope_stack_new();
void scope_stack_destroy(ScopeStack* me);
bool scope_stack_has_function(ScopeStack* me);
void scope_stack_push_scope(ScopeStack* me, Scope const* scope); // Generally called via scope_push_to_stack()
void scope_stack_pop_scope(ScopeStack* me, Scope const* scope); // Generally called via scope_pop_from_stack()
Declaration const* scope_stack_get_value(ScopeStack const* me, csview name);
Declaration const* scope_stack_get_type(ScopeStack const* me, csview name);
Declaration const* scope_stack_get_namespace(ScopeStack const* me, csview name);
Declaration const* scope_stack_get_keyword(ScopeStack const* me, KeywordIdentifier keyword, bool type_else_value);
