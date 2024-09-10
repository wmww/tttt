#pragma once

#include "namespace.h"

typedef struct NameStack NameStack;

NameStack* name_stack_new(ErrorCtx* errs);
void name_stack_destroy(NameStack* me);
void name_stack_push_block_frame(NameStack* me);
void name_stack_push_function_frame(NameStack* me);
void name_stack_push_generic_frame(NameStack* me, Generic const* generic);
void name_stack_push_name_stack_frame(NameStack* me, NameStack const* ref);
void name_stack_pop_frame(NameStack* me);
NameStack* name_stack_pull_frame_into_new_stack(NameStack* me);
Type name_stack_make_interface_from(NameStack const* ref);
void name_stack_add_variable(NameStack* me, NamedVariable* var);
void name_stack_add_type(NameStack* me, NamedType* type);
void name_stack_add_variable_s(NameStack* me, cstr name_string, NamedVariable* var);
void name_stack_add_type_s(NameStack* me, cstr name_string, NamedType* type);
NamedType* name_stack_add_mergeable_type(
    NameStack* me,
    Span name,
    Span declaration_full,
    Type type,
    Generic const* generic,
    bool is_class
);
void name_stack_add_namespace(NameStack* me, Span name, NameStack const* ns); // ns is owned externally
void name_stack_add_import(NameStack* me, NamedImport import);
void name_stack_process_imports(NameStack* me);
bool name_stack_has_function(NameStack* me);
NamedVariable* name_stack_get_variable(NameStack const* me, csview name);
NamedType* name_stack_get_type(NameStack const* me, csview name);
NameStack const* name_stack_get_namespace(NameStack const* me, csview name);
