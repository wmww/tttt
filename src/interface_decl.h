#pragma once

#include "type.h"
#include "lazy_type.h"
#include "generic.h"
#include "expr.h"
#include "lazy_type.h"

typedef struct InterfaceDecl {
    Span name;
    Span full_span;
    Generic generic;
    OuterGenerics const* outer_generics; // The type parameters it captures (set by the resolve walk)
    VecSpannedType extends; // The first `extends` clause's entity names (`A.B<T>`)
    // Heritage tsc parses but doesn't check (the parser reports checkGrammarInterfaceDeclaration's TS1172/TS1176 about
    // it): later `extends` clauses and `implements` clauses. Only resolved, silently, for the baselines
    VecTypeNode unchecked_extends;
    // Elements that aren't entity names (`A?.B`, `f()`): the first invalid_extends_checked (those of the first `extends`
    // clause) are TS2499, and all are only resolved, silently
    VecExpr invalid_extends;
    u32 invalid_extends_checked;
    TypeNode type;
    Symbol* symbol;
    // A `this` keyword is written somewhere in it (tsc's NodeFlags.ContainsThis): set by the parser (which counts any
    // `this`, like tsc's binder), and by the resolve walk for a `this` type
    bool contains_this;
    // An accessor of it mentions a `this` type, which doesn't make the interface contain one (see
    // interface_member_lookup_names()): what a generic interface's `this` type is there (lower.c's declared_type)
    bool accessor_this;
} InterfaceDecl;

void interface_decl_drop(InterfaceDecl* me);
void interface_decl_add_names(InterfaceDecl* me, Scope* scope, DeclStatementProps props);
void interface_decl_lookup_names(InterfaceDecl* me, Scope* names, ErrorCtx* errs);
void interface_decl_typecheck(InterfaceDecl* me, ErrorCtx* errs);
