#pragma once

#include "prelude.h"

typedef struct InterfaceMember InterfaceMember;
forward_vec(vec_InterfaceMember, InterfaceMember);

typedef struct Interface {
    vec_InterfaceMember members;
} Interface;

void Interface_drop(Interface* self);
cstr Interface_to_cstr(Interface* self);
