#pragma once

#include "expr.h"
#include "declaration.h"

typedef struct EnumCase {
    Span name;
    Expr value;
    Declaration declaration;
    Type identifier_type;
} EnumCase;

void enum_case_drop(EnumCase* me);
#define i_key_class_names EnumCase, enum_case
#define i_no_clone
#include "wrap_stc/vec.h"

typedef struct EnumDecl {
    Span name;
    Span full_span;
    VecEnumCase cases;
    LazyType instance_type;
    LazyType interface_type;
    ArcDeclaration declaration; // Types are identifiers that refer to interface_declaration and instance_declaration
    Declaration interface_declaration; // Not Arc because not added to scope directly
    Declaration instance_declaration; // Not Arc because not added to scope directly
} EnumDecl;

void enum_decl_drop(EnumDecl* me);
void enum_decl_add_names(EnumDecl* me, Scope* scope);
void enum_decl_lookup_names(EnumDecl* me, NameStack* names, ErrorCtx* errs);
Type enum_decl_evaluate_instance_type(EnumDecl* me);
Type enum_decl_evaluate_interface_type(EnumDecl* me);
void enum_decl_typecheck(EnumDecl* me, ErrorCtx* errs);
