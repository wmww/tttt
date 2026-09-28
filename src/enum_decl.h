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
    // `[e] = 1`: a computed name that isn't a string or number literal (TS1164; the member is computed and the name
    // expression isn't checked, tsc's isComputedNonLiteralName). `name` is the whole `[e]`
    bool computed_name;
    // A numeric literal name (`1`, `0x1`): 1 if it's a finite number (TS2452), 2 if not. The name is its source text,
    // not the number's canonical text like tsc's
    uint8_t numeric_name;
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
