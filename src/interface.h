#pragma once

#include "prelude.h"

typedef struct InterfaceMember InterfaceMember;
forward_vec(vec_InterfaceMember, InterfaceMember);

typedef struct Interface {
    vec_InterfaceMember members;
} Interface;

void Interface_drop(Interface* self);
Interface Interface_clone(Interface self);
cstr Interface_to_cstr(Interface* self, int indent);

typedef struct NamedInterface {
    cstr name;
    Interface content;
} NamedInterface;

void NamedInterface_drop(NamedInterface* self);
cstr NamedInterface_to_cstr(NamedInterface* self, int indent);
