#pragma once

#include "type.h"
#include "lazy_type.h"
#include "generic.h"
#include "declaration.h"

typedef struct InterfaceDecl {
    Span name;
    Span full_span;
    Generic generic;
    VecSpannedType extends;
    TypeNode type;
    LazyType lazy_type;
    DeclHandle declaration;
} InterfaceDecl;

void interface_decl_drop(InterfaceDecl* me);
void interface_decl_add_names(InterfaceDecl* me, Scope* scope, bool exported);
void interface_decl_lookup_names(InterfaceDecl* me, Scope* names, ErrorCtx* errs);
Ty const* interface_decl_evaluate_type(InterfaceDecl* me);
void interface_decl_typecheck(InterfaceDecl* me, ErrorCtx* errs);
