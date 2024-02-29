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

typedef struct NamedInterface {
    cstr name;
    Interface content;
} NamedInterface;

void NamedInterface_drop(NamedInterface* self);
void cstr_append_named_interface_debug(cstr* target, NamedInterface* self, int indent);
