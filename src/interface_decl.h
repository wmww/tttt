#pragma once

#include "type.h"
#include "lazy_type.h"
#include "generic.h"

typedef struct InterfaceDecl {
    Span name;
    Span full_span;
    Generic generic;
    VecSpannedType extends; // Only used if is_interface is true
    Type type;
    LazyType lazy_type;
    NamedType* named_type;
} InterfaceDecl;

void interface_decl_drop(InterfaceDecl* me);
void interface_decl_add_names(InterfaceDecl* me, NameStack* names, ErrorCtx* errs);
void interface_decl_resolve_names(InterfaceDecl* me, NameStack* names, ErrorCtx* errs);
Type interface_decl_evaluate_type(InterfaceDecl* me);
void interface_decl_typecheck(InterfaceDecl* me, ErrorCtx* errs);
