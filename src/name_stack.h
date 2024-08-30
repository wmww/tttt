#pragma once

#include "type.h"
#include "expr.h"
#include "generic.h"

typedef struct NameStack NameStack;

typedef struct NamedVariable {
    Span name;
    Span full;
    Type type;
    Generic const* generic;
} NamedVariable;

NamedVariable* make_named_variable(Span name, Span full, Type type, Generic const* generic);
void named_variable_destroy(NamedVariable* me);

typedef struct NamedType {
    Span name;
    Span full;
    Type type;
    Generic const* generic;
    bool is_class;
    bool is_typeof_class;
} NamedType;

NamedType* make_named_type(Span name, Span full, Type type, Generic const* generic);
void named_type_destroy(NamedType* me);

NameStack* name_stack_new(ErrorCtx* errs);
void name_stack_destroy(NameStack* me);
void name_stack_push_block_frame(NameStack* me);
void name_stack_push_function_frame(NameStack* me);
void name_stack_push_generic_frame(NameStack* me, Generic const* generic);
void name_stack_push_name_stack_frame(NameStack* me, NameStack const* ref);
void name_stack_pop_frame(NameStack* me);
NameStack* name_stack_pull_frame_into_new_stack(NameStack* me);
Type name_stack_make_interface_from(NameStack const* me);
void name_stack_add_variable(NameStack* me, NamedVariable* var);
void name_stack_add_type(NameStack* me, NamedType* type);
void name_stack_add_variable_s(NameStack* me, cstr name_string, NamedVariable* var);
void name_stack_add_type_s(NameStack* me, cstr name_string, NamedType* type);
void name_stack_add_namespace(NameStack* me, Span name, NameStack* ref);
bool name_stack_has_function(NameStack* me);
NamedVariable* name_stack_get_variable(NameStack const* me, csview name);
NamedType* name_stack_get_type(NameStack const* me, csview name);
NameStack const* name_stack_get_namespace(NameStack const* me, csview name);
