#pragma once

#include "type.h"
#include "expr.h"
#include "generic.h"

typedef struct NameStack NameStack;

typedef struct NamedVariable {
    Span name_span;
    Type const* type;
} NamedVariable;

#define i_key_names NamedVariable, named_variable
#define i_no_atomic
#define i_is_forward
#include "wrap_stc/arc.h"

typedef struct NamedType {
    Span name_span;
    Type const* type;
    Generic const* generic;
    bool is_class;
} NamedType;

#define i_key_names NamedType, named_type
#define i_no_atomic
#define i_is_forward
#include "wrap_stc/arc.h"

NameStack* name_stack_new();
void name_stack_destroy(NameStack* me);
void name_stack_push_block_frame(NameStack* me);
void name_stack_push_function_frame(NameStack* me);
void name_stack_push_class_frame(NameStack* me);
void name_stack_push_function_frame(NameStack* me);
void name_stack_push_generic_frame(NameStack* me, Generic* generic, ErrorCtx* errs);
void name_stack_pop_frame(NameStack* me);
bool name_stack_add_variable(NameStack* me, cstr name, Span decl_name_span, Type const* type);
bool name_stack_add_type(
    NameStack* me,
    cstr name,
    Span decl_name_span,
    Type const* type,
    Generic const* generic,
    bool is_class
);
bool name_stack_has_function(NameStack* me);
ArcNamedVariable const* name_stack_get_variable(NameStack* me, csview name);
ArcNamedType const* name_stack_get_type(NameStack* me, csview name);
