#include "indexed_type.h"

void indexed_type_drop(IndexedType* me) {
    type_drop(&my interface);
    type_drop(&my index);
}

IndexedType indexed_type_clone(IndexedType me) {
    return (IndexedType){
        .interface = type_clone(me.interface),
        .index = type_clone(me.index),
    };
}

void cstr_append_indexed_type_debug(cstr* target, IndexedType* me, int indent) {
    cstr_append_type_debug(target, &my interface, indent);
    cstr_append_sv(target, c_sv("["));
    cstr_append_type_debug(target, &my index, indent);
    cstr_append_sv(target, c_sv("]"));
}

void cstr_append_indexed_type_error_msg(cstr* target, IndexedType* me) {
    cstr_append_type_error_msg(target, &my interface);
    cstr_append_sv(target, c_sv("["));
    cstr_append_type_error_msg(target, &my index);
    cstr_append_sv(target, c_sv("]"));
}

void indexed_type_resolve_names(IndexedType* me, NameStack* names, ErrorCtx* errs) {
    type_resolve_names(&my interface, names, errs);
    type_resolve_names(&my index, names, errs);
}

void indexed_type_typecheck(IndexedType const* me, ErrorCtx* errs) {
    type_typecheck(&my interface, errs);
    type_typecheck(&my index, errs);
}

void keyof_type_drop(KeyofType* me) {
    type_drop(&my interface);
    type_drop(&my cached);
}

KeyofType keyof_type_clone(KeyofType me) {
    return (KeyofType){
        .interface = type_clone(me.interface),
        .has_cache = false,
        .cached = {0},
    };
}

void cstr_append_keyof_type_debug(cstr* target, KeyofType* me, int indent) {
    cstr_append_sv(target, c_sv("keyof "));
    cstr_append_type_debug(target, &my interface, indent);
}

void cstr_append_keyof_type_error_msg(cstr* target, KeyofType* me) {
    cstr_append_sv(target, c_sv("keyof "));
    cstr_append_type_error_msg(target, &my interface);
}

void keyof_type_resolve_names(KeyofType* me, NameStack* names, ErrorCtx* errs) {
    type_resolve_names(&my interface, names, errs);
}

void keyof_type_typecheck(KeyofType const* me, ErrorCtx* errs) {
    type_typecheck(&my interface, errs);
}
