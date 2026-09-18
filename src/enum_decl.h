#pragma once

#include "expr.h"
#include "scope.h"
#include "declaration.h"

typedef struct EnumCase {
    Span name; // Without the quotes if the name is a string literal
    Span full_name; // As written
    bool name_is_value;
    Expr value;
    Declaration typeof_declaration;
    ArcDeclaration declaration;
} EnumCase;

void enum_case_drop(EnumCase* me);
#define g_class_key EnumCase, enum_case
#define g_no_clone
#include <cleat/generic/vec.h>

typedef struct EnumDecl {
    Span name;
    Span full_span;
    bool is_const; // `const enum`, TODO: nothing checks the extra restrictions on these yet
    VecEnumCase cases;
    LazyType instance_type;
    LazyType interface_type;
    Scope namespace;
    Declaration typeof_declaration;
    ArcDeclaration declaration;
} EnumDecl;

void enum_decl_drop(EnumDecl* me);
void enum_decl_add_names(EnumDecl* me, Scope* scope, bool exported);
void enum_decl_lookup_names(EnumDecl* me, ScopeStack* names, ErrorCtx* errs);
Type enum_decl_evaluate_instance_type(EnumDecl* me);
Type enum_decl_evaluate_interface_type(EnumDecl* me);
void enum_decl_typecheck(EnumDecl* me, ErrorCtx* errs);
