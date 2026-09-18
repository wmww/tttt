#pragma once

#include "type.h"
#include "expr.h"
#include "declaration.h"

#define g_pro_key SvStr, svstr
#define g_class_val ArcDeclaration, arc_declaration
#include <cleat/generic/hmap.h>

typedef struct PortItem PortItem;
typedef PortItem* PortItemPtr;
typedef struct Program Program;

#define g_pro_key SvStr, svstr
#define g_val PortItemPtr, port_item_ptr
#include <cleat/generic/hmap.h>

#define g_key PortItemPtr, port_item_ptr
#include <cleat/generic/vec.h>

typedef struct Scope {
    ErrorCtx* errs;
    bool is_function; // If this scope represents a function
    csview default_export;
    VecSpan namespaces;
    bool currently_adding_import; // If the current add_import() call stack has already seen this scope
                                  // Threadsafety: may have to change this system
    HMapArcDeclaration declarations;
    HMapPortItemPtr ports;
    VecPortItemPtr wildcard_reexports;
    Declaration const* this_declaration;
    Declaration const* super_declaration;
    LazyType value_interface;
    ScopeStack* current_scope_stack; // The ScopeStack this scope is in, or null if none
    // Owning pointer of the scope to put exported members into, or null if this scope cannot have exports or is itself
    // an export scope
    Scope* export_scope;
} Scope;

void scope_drop(Scope* me);
Scope scope_clone(Scope me);

#define SCOPE_INIT_DEFAULT \
    .errs = errs, \
    .value_interface = lazy_type_init(LAZY_TYPE_SCOPE_VALUE_INTERFACE)

static inline Scope scope_init(ErrorCtx* errs) {
    return (Scope){
        SCOPE_INIT_DEFAULT,
    };
}

static inline Scope scope_init_namespace(Span name, Scope const* parent, ErrorCtx* errs) {
    VecSpan namespaces = vec_span_clone(parent->namespaces);
    vec_span_push(&namespaces, name);
    return (Scope){
        .namespaces = namespaces,
        SCOPE_INIT_DEFAULT,
    };
}

static inline Scope scope_init_toplevel(ErrorCtx* errs) {
    return (Scope){
        .export_scope = c_new(Scope, {
            .errs = errs,
        }),
        SCOPE_INIT_DEFAULT,
    };
}

static inline Scope scope_init_function(ErrorCtx* errs) {
    return (Scope){
        .is_function = true,
        SCOPE_INIT_DEFAULT,
    };
}

void scope_push_to_stack(Scope* me, ScopeStack* stack);
// Cannot be a scope_init_* function because me needs to be stable
void scope_push_generic_to_stack(Scope* me, Generic* generic, ScopeStack* stack);
void scope_pop_from_stack(Scope* me); // This call is not needed if scope is dropped while on stack
void scope_copy_declarations_from(Scope* me, Scope const* source);

// Checks that ret was previously null, creates a new declaration if needed
void scope_init_declaration(Scope* me, ArcDeclaration* ret, DeclProps props);
void scope_add_port(Scope* me, PortItem* item);
void scope_process_imports(Scope* me);
Type scope_evaluate_value_interface(Scope const* me);
Declaration const* scope_get_declaration(Scope const* me, Span name);

typedef Scope const* ScopeCPtr;
#define g_key ScopeCPtr, scope_cptr
#include <cleat/generic/vec.h>
