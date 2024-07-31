#pragma once

#include "type.h"
#include "lazy_type.h"
#include "expr.h"
#include "generic.h"

typedef struct NameStack NameStack;

typedef struct NamedVariable {
    Span name;
    LazyType type;
    Generic const* generic;
} NamedVariable;

NamedVariable* make_named_variable(Span name, LazyType type, Generic const* generic);
void named_variable_destroy(NamedVariable* me);

typedef struct NamedType {
    Span name;
    LazyType type;
    Generic const* generic;
    bool is_class;
    bool is_typeof_class;
} NamedType;

NamedType* make_named_type(Span name, LazyType type, Generic const* generic);
void named_type_drop(NamedType* me);
void named_type_destroy(NamedType* me);

#define i_key_class_names NamedType, named_type
#define i_opt c_no_atomic
#define i_is_forward
#define i_no_hash
#include "wrap_stc/arc.h"

NameStack* name_stack_new();
void name_stack_destroy(NameStack* me);
void name_stack_push_block_frame(NameStack* me);
void name_stack_push_function_frame(NameStack* me);
void name_stack_push_generic_frame(NameStack* me, Generic* generic, ErrorCtx* errs);
void name_stack_pop_frame(NameStack* me);
bool name_stack_add_variable(NameStack* me, NamedVariable* var);
bool name_stack_add_type(NameStack* me, NamedType* type);
bool name_stack_add_variable_s(NameStack* me, cstr name_string, NamedVariable* var);
bool name_stack_add_type_s(NameStack* me, cstr name_string, NamedType* type);
bool name_stack_has_function(NameStack* me);
NamedVariable* name_stack_get_variable(NameStack* me, csview name);
NamedType* name_stack_get_type(NameStack* me, csview name);
