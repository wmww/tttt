#pragma once

#include "type.h"

typedef struct InterfaceMember {
    cstr name;
    Type type;
} InterfaceMember;

void InterfaceMember_drop(InterfaceMember* self);

#define i_key_class InterfaceMember
#define i_is_forward
#define i_no_clone
#include "stc/vec.h"
