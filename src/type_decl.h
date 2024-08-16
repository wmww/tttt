#pragma once

#include "type.h"
#include "lazy_type.h"
#include "generic.h"

typedef struct TypeDecl {
    Span full_span;
    bool is_interface;
    Span name;
    Generic generic;
    VecSpannedType interface_extends; // Only used if is_interface is true
    Type type;
    ResolveState type_resolve_state;
    Type resolved_type;
    NamedType* named_type;
} TypeDecl;

void type_decl_drop(TypeDecl* me);
void type_decl_add_names(TypeDecl* me, NameStack* names, ErrorCtx* errs);
Type const* type_decl_resolve_interface_type(TypeDecl* me);
void type_decl_typecheck(TypeDecl* me, ErrorCtx* errs);
