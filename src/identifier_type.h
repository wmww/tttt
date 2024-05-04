#pragma once

#include "type.h"

typedef struct ErrorCtx ErrorCtx;

typedef struct IdentifierType {
    cstr name;
    Span span;
    ArcNamedType type;
    VecType generic_params;
    Type degenericized; // A copy of the type with the generic parameters swapped in
} IdentifierType;

void identifier_type_drop(IdentifierType* me);
IdentifierType identifier_type_clone(IdentifierType me);
void cstr_append_identifier_type_debug(cstr* target, IdentifierType* me, int indent);
void cstr_append_identifier_type_error_msg(cstr* target, IdentifierType* me);
void identifier_type_resolve_names(IdentifierType* me, NameStack* names, ErrorCtx* errs);
