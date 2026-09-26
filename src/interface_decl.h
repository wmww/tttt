#pragma once

#include "type.h"
#include "lazy_type.h"
#include "generic.h"
#include "lazy_type.h"

typedef struct InterfaceDecl {
    Span name;
    Span full_span;
    Generic generic;
    VecSpannedType extends;
    TypeNode type;
    Symbol* symbol;
    bool contains_this; // A `this` type is written somewhere in it (tsc's NodeFlags.ContainsThis), set when resolving
} InterfaceDecl;

void interface_decl_drop(InterfaceDecl* me);
void interface_decl_add_names(InterfaceDecl* me, Scope* scope, DeclExported exported);
void interface_decl_lookup_names(InterfaceDecl* me, Scope* names, ErrorCtx* errs);
void interface_decl_typecheck(InterfaceDecl* me, ErrorCtx* errs);
