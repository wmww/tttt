#pragma once

#include "type.h"
#include "expr.h"
#include "generic.h"
#include "display.h" // HsetNamedTypeCPtr is forward-declared in display.h for reasons

typedef struct NameStack NameStack;

typedef struct NamedVariable {
    Span name;
    Span full;
    Type type;
    Generic const* generic;
} NamedVariable;

NamedVariable* make_named_variable(Span name, Span declaration_full, Type type, Generic const* generic);
void named_variable_destroy(NamedVariable* me);

typedef struct NamedType {
    Span name;
    VecSpan declarations;
    Type type;
    Generic const* generic;
    bool is_class;
    bool is_typeof_class;
    bool is_mergeable;
    bool is_merged;
    VecType merged_types;   // All the interface types that will be merged together to make our type, not used if this
                            // is not a merged declaration and cleared once the merge happens
    LazyType lazy_type; // Only used if this is a merged declaration. In that case, `type` is set to be a lazy that
                        // points to this LazyType.
    // THREADSAFETY: this will need to be atomic
    intptr_t ref_count_minus_1; // 0 means there is 1 reference
} NamedType;

#define i_key_names NamedTypeCPtr, named_type_cptr
#define i_is_forward
#include "wrap_stc/hset.h"

NamedType* make_named_type(Span name, Span declaration_full, Type type, Generic const* generic);
void named_type_destroy(NamedType* me);
Type named_type_evaluate_type(NamedType* me);

typedef struct NamedImport {
    Span decl_name;
    Span source_name;
    SourceFile const* source_file;
    bool error_reported;
} NamedImport;

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
