#pragma once

#include "prelude.h"

typedef struct InterfaceMember InterfaceMember;
forward_vec(vec_InterfaceMember, InterfaceMember);

typedef struct Interface {
    vec_InterfaceMember members;
} Interface;

void Interface_drop(Interface* self);
Interface Interface_clone(Interface self);
void cstr_append_interface_debug(cstr* target, Interface* self, int indent);
