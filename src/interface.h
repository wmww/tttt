#pragma once

#include "prelude.h"

typedef struct InterfaceMember InterfaceMember;
forward_vec(VecInterfaceMember, InterfaceMember);

typedef struct Interface {
    VecInterfaceMember members;
} Interface;

void interface_drop(Interface* self);
Interface interface_clone(Interface self);
void cstr_append_interface_debug(cstr* target, Interface const* self, int indent);
void cstr_append_interface_error_msg(cstr* target, Interface const* self);
