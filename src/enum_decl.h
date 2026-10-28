#pragma once

#include "ast_alloc.h"
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
    Expr* computed_key; // Owned, the `e` of such a name (only the types baseline looks at it: any)
    // A numeric literal name (`1`, `0x1`): 1 if it's a finite number (TS2452), 2 if not. The name is its source text,
    // not the number's canonical text like tsc's
    uint8_t numeric_name;
    // The member's name where that isn't its source text (tsc's escapedName, getPropertyNameForPropertyNameNode): a
    // string's cooked value (`'a\u0062'` is `ab`), a number's canonical text (`0xF00D` is `61453`), `__missing` for a
    // bigint. Empty otherwise. The symbols baseline still names the member as written
    cstr canonical_name;
    Expr value;
    Symbol* symbol;
} EnumCase;

void enum_case_drop(EnumCase* me);
#define g_class_key EnumCase, enum_case
#define g_no_clone
#define g_allocator ast
#include <cleat/generic/vec.h>

struct EnumDecl {
    Span name;
    Span full_span;
    bool is_const; // `const enum` (its restrictions: TS2474/TS2477/TS2478 in enum_decl.c, TS2475 expr.c, TS2476 check.c)
    bool ambient; // This declaration is ambient (`declare`, in a `declare namespace` or a .d.ts), set by add_names
    VecEnumCase cases;
    Scope* namespace;
    Symbol* symbol;
};

void enum_decl_drop(EnumDecl* me);
void enum_decl_add_names(EnumDecl* me, Scope* scope, DeclStatementProps props);
void enum_decl_lookup_names(EnumDecl* me, Scope* names, ErrorCtx* errs);
void enum_decl_typecheck(EnumDecl* me, ErrorCtx* errs);
