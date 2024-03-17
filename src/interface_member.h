#pragma once

#include "type.h"

typedef struct InterfaceMember {
    cstr name;
    Type type;
} InterfaceMember;

static inline InterfaceMember interface_member_make(cstr name, Type type) {
    return (InterfaceMember) {
        .name = name,
        .type = type,
    };
}

void interface_member_drop(InterfaceMember* me);
InterfaceMember interface_member_clone(InterfaceMember me);

#define i_key_class_names InterfaceMember, interface_member
#define i_is_forward
#include "wrap_stc/vec.h"
