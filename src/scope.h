#pragma once

#include "type.h"
#include "expr.h"
#include "generic.h"
#include "display.h" // HsetNamedTypeCPtr is forward-declared in display.h for reasons

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
    SourceFile* source_file;
    bool error_reported;
} NamedImport;

typedef Scope const* ScopeCPtr;
wrap_forward_hmap_str(ScopeCPtr, scope_cptr);

typedef NamedType* NamedTypePtr;
typedef NamedVariable* NamedVariablePtr;

#define i_key_ssv
#define i_val_names NamedTypePtr, named_type_ptr
#include "wrap_stc/hmap.h"

#define i_key_ssv
#define i_val_names NamedVariablePtr, named_variable_ptr
#include "wrap_stc/hmap.h"

#define i_key_ssv
#define i_val_names NamedImport, named_import
#include "wrap_stc/hmap.h"

#define i_key_ssv
#define i_val_names ScopeCPtr, scope_cptr
#define i_is_forward
#include "wrap_stc/hmap.h"

typedef struct Scope {
    enum {
        SCOPE_DEFAULT,
        SCOPE_FUNCTION,
        SCOPE_GENERIC
    } tag;
    ErrorCtx* errs;
    csview default_export;
    bool currently_adding_import; // If the current add_import() call stack has already seen this stack frame
                                  // Threadsafety: may have to change this system
    HmapStrNamedTypePtr types;
    HmapStrNamedVariablePtr variables;
    HmapStrScopeCPtr namespaces;
    HmapStrNamedImport imports;
    NameStack* current_name_stack; // The NameStack this scope is in, or null if none
} Scope;

void scope_drop(Scope* me);
Scope scope_clone(Scope me);

static inline Scope scope_init(ErrorCtx* errs) {
    return (Scope){
        .tag = SCOPE_DEFAULT,
        .errs = errs,
    };
}

static inline Scope scope_init_function(ErrorCtx* errs) {
    return (Scope){
        .tag = SCOPE_FUNCTION,
        .errs = errs,
    };
}

void scope_push_to_stack(Scope* me, NameStack* stack);
// Cannot be a scope_init_* function because me needs to be stable
void scope_push_generic_to_stack(Scope* me, Generic* generic, NameStack* stack);
void scope_pop_from_stack(Scope* me); // This call is not needed if scope is dropped while on stack

void scope_add_variable(Scope* me, NamedVariable* var);
void scope_add_type(Scope* me, NamedType* type);
void scope_add_variable_s(Scope* me, cstr name_string, NamedVariable* var);
void scope_add_type_s(Scope* me, cstr name_string, NamedType* type);
NamedType* scope_add_mergeable_type(
    Scope* me,
    Span name,
    Span declaration_full,
    Type type,
    Generic const* generic,
    bool is_class
);
void scope_add_namespace(Scope* me, Span name, Scope const* scope); // scope is owned externally
void scope_add_import(Scope* me, NamedImport import);

void scope_process_imports(Scope* me);
Type scope_make_interface_from(Scope const* me);
