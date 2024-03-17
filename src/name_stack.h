#pragma once

#include "type.h"
#include "error.h"

typedef struct NamedType {
    Type const* type;
    bool is_generic;
    int generic_index;
} NamedType;
#define i_type HmapStrNamedType
#define i_key_ssv
#define i_val NamedType
#include "stc/hmap.h"

typedef Type const* CTypePtr;
#define i_type HmapStrCTypePtr
#define i_key_ssv
#define i_val CTypePtr
#include "stc/hmap.h"

typedef struct Namespace {
    HmapStrNamedType types;
    HmapStrCTypePtr variables;
    bool is_generic;
} Namespace;

Namespace namespace_init(bool is_generic);
void namespace_drop(Namespace* self);
Namespace namespace_clone(Namespace self);

#define i_key_class Namespace
#include "stc/vec.h"

typedef struct NameStack {
    VecNamespace stack;
} NameStack;

NameStack name_stack_init();
void name_stack_drop(NameStack* self);
void name_stack_push_frame(NameStack* self, bool is_generic);
void name_stack_pop_frame(NameStack* self);
bool name_stack_add_variable(NameStack* self, cstr name, Type const* type);
bool name_stack_add_type(NameStack* self, cstr name, Type const* type, bool is_generic, int generic_index);
Type const* name_stack_get_variable(NameStack* self, csview name);
NamedType const* name_stack_get_type(NameStack* self, csview name, int* optional_generic_depth);
