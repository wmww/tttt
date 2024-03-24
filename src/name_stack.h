#pragma once

#include "type.h"

typedef struct NameStack NameStack;

typedef enum StackFrameTag {
    STACK_FRAME_BLOCK,
    STACK_FRAME_GENERIC,
    STACK_FRAME_CLASS,
} StackFrameTag;

typedef struct NamedType {
    Type const* type;
    bool is_generic;
    int generic_index;
} NamedType;

NameStack* name_stack_new();
void name_stack_destroy(NameStack* me);
void name_stack_push_frame(NameStack* me, StackFrameTag tag);
void name_stack_pop_frame(NameStack* me);
bool name_stack_add_variable(NameStack* me, cstr name, Type const* type);
bool name_stack_add_type(NameStack* me, cstr name, Type const* type);
bool name_stack_add_generic_type(NameStack* me, cstr name, Type const* type, int generic_index);
Type const* name_stack_get_variable(NameStack* me, csview name);
NamedType const* name_stack_get_type(NameStack* me, csview name, int* optional_generic_depth);
