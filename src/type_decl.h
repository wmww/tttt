#pragma once

#include "type.h"
#include "lazy_type.h"
#include "generic.h"

typedef struct TypeDecl {
    Span full_span;
    bool is_interface;
    Span name;
    Generic generic;
    VecSpannedType interface_extends; // Only used if is_interface is true
    Type type;
    NamedType* named_type;
} TypeDecl;

void type_decl_drop(TypeDecl* me);
void cstr_append_type_decl_debug(cstr* target, TypeDecl* me, int indent);
void type_decl_add_names(TypeDecl* me, NameStack* names, ErrorCtx* errs);
Type type_decl_make_interface_type(TypeDecl* me);
void type_decl_typecheck(TypeDecl* me, ErrorCtx* errs);
