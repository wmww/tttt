#pragma once

#include "type.h"

typedef struct InterfaceMember {
    cstr name;
    Type type;
} InterfaceMember;

void InterfaceMember_drop(InterfaceMember* self);
InterfaceMember InterfaceMember_clone(InterfaceMember self);

#define i_key_class InterfaceMember
#define i_is_forward
#include "stc/vec.h"
