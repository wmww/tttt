#pragma once

#include "type.h"
#include "lazy_type.h"
#include "generic.h"
#include "declaration.h"

typedef struct InterfaceDecl {
    Span name;
    Span full_span;
    Generic generic;
    VecSpannedType extends; // Only used if is_interface is true
    Type type;
    LazyType lazy_type;
    ArcDeclaration declaration;
} InterfaceDecl;

void interface_decl_drop(InterfaceDecl* me);
void interface_decl_add_names(InterfaceDecl* me, Scope* scope, bool exported);
void interface_decl_lookup_names(InterfaceDecl* me, ScopeStack* names, ErrorCtx* errs);
Type interface_decl_evaluate_type(InterfaceDecl* me);
void interface_decl_typecheck(InterfaceDecl* me, ErrorCtx* errs);
