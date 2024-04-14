#pragma once

#include "type.h"

typedef struct TypeDecl {
    Span full_span;
    bool is_interface;
    cstr name;
    Span name_span;
    Type type;
} TypeDecl;

void type_decl_drop(TypeDecl* me);
void cstr_append_type_decl_debug(cstr* target, TypeDecl* me, int indent);
