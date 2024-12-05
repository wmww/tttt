#pragma once

#include "type.h"
#include "expr.h"
#include "declaration.h"

#define i_key_ssv
#define i_val_class_names ArcDeclaration, arc_declaration
#include "wrap_stc/hmap.h"

typedef struct ImportItem ImportItem;
typedef ImportItem* ImportItemPtr;

#define i_key_ssv
#define i_val_names ImportItemPtr, import_item_ptr
#include "wrap_stc/hmap.h"

#define i_key_names ImportItemPtr, import_item_ptr
#include "wrap_stc/vec.h"

typedef struct Scope {
    enum {
        SCOPE_DEFAULT,
        SCOPE_EXPORT,
        SCOPE_FUNCTION,
        SCOPE_GENERIC,
    } tag;
    ErrorCtx* errs;
    csview default_export;
    bool currently_adding_import; // If the current add_import() call stack has already seen this scope
                                  // Threadsafety: may have to change this system
    HmapStrArcDeclaration declarations;
    HmapStrImportItemPtr imports;
    VecImportItemPtr wildcard_reexports;
    Declaration const* this_declaration;
    Declaration const* super_declaration;
    ScopeStack* current_scope_stack; // The ScopeStack this scope is in, or null if none
    Scope* export_scope; // Owning pointer if this scope is file's toplevel scope, else null
} Scope;

void scope_drop(Scope* me);
Scope scope_clone(Scope me);

static inline Scope scope_init(ErrorCtx* errs) {
    return (Scope){
        .tag = SCOPE_DEFAULT,
        .errs = errs,
    };
}

static inline Scope scope_init_toplevel(ErrorCtx* errs) {
    return (Scope){
        .tag = SCOPE_DEFAULT,
        .errs = errs,
        .export_scope = c_new(Scope, {
            .tag = SCOPE_EXPORT,
            .errs = errs,
        }),
    };
}

static inline Scope scope_init_function(ErrorCtx* errs) {
    return (Scope){
        .tag = SCOPE_FUNCTION,
        .errs = errs,
    };
}

void scope_push_to_stack(Scope* me, ScopeStack* stack);
// Cannot be a scope_init_* function because me needs to be stable
void scope_push_generic_to_stack(Scope* me, Generic* generic, ScopeStack* stack);
void scope_pop_from_stack(Scope* me); // This call is not needed if scope is dropped while on stack

// Checks that ret was previously null, creates a new declaration if needed
void scope_init_declaration(Scope* me, ArcDeclaration* ret, DeclProps props);
void scope_add_import(Scope* me, ImportItem* import);

void scope_process_imports(Scope* me);
Type scope_make_value_interface(Scope const* me);

typedef Scope const* ScopeCPtr;
#define i_key_names ScopeCPtr, scope_cptr
#include "wrap_stc/vec.h"
