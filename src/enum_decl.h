#pragma once

#include "expr.h"
#include "scope.h"
#include "lazy_type.h"

typedef struct EnumDecl EnumDecl;

typedef struct EnumCase {
    EnumDecl const* parent; // Set when names are added
    Span name; // Without the quotes if the name is a string literal
    Span full_name; // As written
    bool name_is_value;
    Expr value;
    Symbol* symbol;
} EnumCase;

void enum_case_drop(EnumCase* me);
#define g_class_key EnumCase, enum_case
#define g_no_clone
#include <cleat/generic/vec.h>

struct EnumDecl {
    Span name;
    Span full_span;
    bool is_const; // `const enum`, TODO: nothing checks the extra restrictions on these yet
    VecEnumCase cases;
    Scope* namespace;
    Symbol* symbol;
};

void enum_decl_drop(EnumDecl* me);
void enum_decl_add_names(EnumDecl* me, Scope* scope, DeclExported exported, bool declared);
void enum_decl_lookup_names(EnumDecl* me, Scope* names, ErrorCtx* errs);
void enum_decl_typecheck(EnumDecl* me, ErrorCtx* errs);
