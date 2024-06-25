#pragma once

#include "type.h"
#include "generic.h"

typedef struct TypeDecl {
    Span full_span;
    bool is_interface;
    Span name;
    Generic generic;
    VecSpannedType interface_extends; // Only used if is_interface is true
    Type type;
} TypeDecl;

void type_decl_drop(TypeDecl* me);
void cstr_append_type_decl_debug(cstr* target, TypeDecl* me, int indent);
