#pragma once

#include "type.h"

typedef struct InterfaceMember {
    cstr name;
    Type type;
} InterfaceMember;

static inline InterfaceMember InterfaceMember_make(cstr name, Type type) {
    return (InterfaceMember) {
        .name = cstr_clone(name),
        .type = Type_clone(type),
    };
}

void InterfaceMember_drop(InterfaceMember* self);
InterfaceMember InterfaceMember_clone(InterfaceMember self);

#define i_key_class InterfaceMember
#define i_is_forward
#include "stc/vec.h"
