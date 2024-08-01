#pragma once

#include "type.h"

typedef struct ErrorCtx ErrorCtx;

typedef struct IdentifierType {
    VecSpan namespaces;
    Span name;
    NamedType* type;
    VecSpannedType generic_params;
    ResolveState degenericized_state;
    Type degenericized; // A copy of the type with the generic parameters swapped in
    bool inferred;
    ArcNamedType inferred_type;
} IdentifierType;

void identifier_type_drop(IdentifierType* me);
IdentifierType identifier_type_clone(IdentifierType me);
void cstr_append_identifier_type_debug(cstr* target, IdentifierType* me, int indent);
void cstr_append_identifier_type_error_msg(cstr* target, IdentifierType* me);
void identifier_type_resolve_names(IdentifierType* me, NameStack* names, ErrorCtx* errs);
void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs);
