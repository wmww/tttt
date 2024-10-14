#pragma once

#include "type.h"
#include "expr.h"
#include "declaration.h"

typedef Scope const* ScopeCPtr;
wrap_forward_hmap_str(ScopeCPtr, scope_cptr);

#define i_key_ssv
#define i_val_class_names ArcDeclaration, arc_declaration
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
    HmapStrArcDeclaration declarations;
    HmapStrScopeCPtr namespaces;
    HmapStrNamedImport imports;
    Declaration const* this_declaration;
    Declaration const* super_declaration;
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

// Checks that ret was previously null, creates a new declaration if needed
void scope_init_declaration(Scope* me, ArcDeclaration* ret, DeclProps props);

void scope_add_namespace(Scope* me, Span name, Scope const* scope); // scope is owned externally
void scope_add_import(Scope* me, NamedImport import);

void scope_process_imports(Scope* me);
Type scope_make_interface_from(Scope const* me);
