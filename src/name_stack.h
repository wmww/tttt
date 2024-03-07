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

typedef struct NameStackFrame {
    hmap_str_NamedType types;
    hmap_str_CTypePtr values;
    bool is_generic;
} NameStackFrame;

NameStackFrame NameStackFrame_init(bool is_generic);
void NameStackFrame_drop(NameStackFrame* self);
NameStackFrame NameStackFrame_clone(NameStackFrame self);

#define i_key_class NameStackFrame
#include "stc/vec.h"

typedef struct NameStack {
    vec_NameStackFrame stack;
} NameStack;


NameStack NameStack_init();
void NameStack_drop(NameStack* self);
void NameStack_push_frame(NameStack* self, bool is_generic);
void NameStack_pop_frame(NameStack* self);
