#pragma once

#include "type.h"
#include "error.h"

typedef struct NamedType {
    Type const* type;
    bool is_generic;
    int generic_index;
} NamedType;
#define i_type hmap_str_NamedType
#define i_key_ssv
#define i_val NamedType
#include "stc/hmap.h"

typedef Type const* CTypePtr;
#define i_type hmap_str_CTypePtr
#define i_key_ssv
#define i_val CTypePtr
#include "stc/hmap.h"

typedef struct Namespace {
    hmap_str_NamedType types;
    hmap_str_CTypePtr variables;
    bool is_generic;
} Namespace;

Namespace Namespace_init(bool is_generic);
void Namespace_drop(Namespace* self);
Namespace Namespace_clone(Namespace self);

#define i_key_class Namespace
#include "stc/vec.h"

typedef struct NameStack {
    vec_Namespace stack;
} NameStack;


NameStack NameStack_init();
void NameStack_drop(NameStack* self);
void NameStack_push_frame(NameStack* self, bool is_generic);
void NameStack_pop_frame(NameStack* self);
bool NameStack_add_variable(NameStack* self, cstr name, Type const* type);
bool NameStack_add_type(NameStack* self, cstr name, Type const* type, bool is_generic, int generic_index);
Type const* NameStack_get_variable(NameStack* self, csview name);
NamedType const* NameStack_get_type(NameStack* self, csview name, int* optional_generic_depth);
