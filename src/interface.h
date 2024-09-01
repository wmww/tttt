#pragma once

#include "prelude.h"

typedef struct InterfaceMember InterfaceMember;
typedef struct Type Type;
wrap_forward_vec(InterfaceMember, interface_member);
wrap_forward_vec(Type, type);

typedef struct Interface {
    VecInterfaceMember members;
} Interface;

void interface_drop(Interface* me);
Interface interface_clone(Interface me);
Interface interface_from_merged(VecType const* types);
