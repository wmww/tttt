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

typedef struct KeyofType {
    Type interface;
    bool has_cache;
    Type cached; // Should be {0} if has_cache is false
} KeyofType;

void keyof_type_drop(KeyofType* me);
KeyofType keyof_type_clone(KeyofType me);
void cstr_append_keyof_type_debug(cstr* target, KeyofType* me, int indent);
void cstr_append_keyof_type_error_msg(cstr* target, KeyofType* me);
void keyof_type_resolve_names(KeyofType* me, NameStack* names, ErrorCtx* errs);
