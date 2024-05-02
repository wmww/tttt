#pragma once

#include "type.h"

typedef struct ErrorCtx ErrorCtx;

typedef struct IndexedType {
    Type interface;
    Type index;
} IndexedType;

void indexed_type_drop(IndexedType* me);
IndexedType indexed_type_clone(IndexedType me);
void cstr_append_indexed_type_debug(cstr* target, IndexedType* me, int indent);
void cstr_append_indexed_type_error_msg(cstr* target, IndexedType* me);
void indexed_type_resolve_names(IndexedType* me, NameStack* names, ErrorCtx* errs);
