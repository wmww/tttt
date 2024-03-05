#pragma once

#include "type.h"
#include "error.h"

typedef Type const* CTypePtr;
#define i_type hmap_str_CTypePtr
#define i_key_ssv
#define i_val CTypePtr
#include "stc/hmap.h"

typedef struct NameStackFrame {
    hmap_str_CTypePtr types;
    hmap_str_CTypePtr values;
} NameStackFrame;

NameStackFrame NameStackFrame_init();
void NameStackFrame_drop(NameStackFrame* self);

#define i_key_class NameStackFrame
#define i_no_clone
#include "stc/vec.h"

typedef struct NameStack {
    vec_NameStackFrame stack;
} NameStack;


NameStack NameStack_init();
void NameStack_drop(NameStack* self);
void NameStack_push_frame(NameStack* self);
void NameStack_pop_frame(NameStack* self);
