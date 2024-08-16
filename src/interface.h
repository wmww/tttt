#pragma once

#include "prelude.h"

typedef struct InterfaceMember InterfaceMember;
wrap_forward_vec(InterfaceMember, interface_member);

typedef struct Interface {
    VecInterfaceMember members;
} Interface;

void interface_drop(Interface* me);
Interface interface_clone(Interface me);
void cstr_append_interface_error_msg(cstr* target, Interface const* me);
