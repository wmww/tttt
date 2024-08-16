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

void keyof_type_resolve_names(KeyofType* me, NameStack* names, ErrorCtx* errs) {
    type_resolve_names(&my interface, names, errs);
}

void keyof_type_typecheck(KeyofType const* me, ErrorCtx* errs) {
    type_typecheck(&my interface, errs);
}
