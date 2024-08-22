#include "several_types.h"
#include "name_stack.h"
#include "error.h"

// Identifier

void identifier_type_drop(IdentifierType* me) {
    vec_span_drop(&my namespaces);
    vec_spanned_type_drop(&my generic_params);
    type_drop(&my degenericized);
    arc_named_type_drop(&my inferred_type);
}

IdentifierType identifier_type_clone(IdentifierType me) {
    return (IdentifierType){
        .namespaces = vec_span_clone(me.namespaces),
        .name = me.name,
        .type = me.type,
        .generic_params = vec_spanned_type_clone(me.generic_params),
        .degenericized = type_any,
        .inferred = me.inferred,
        .inferred_type = me.inferred_type.get ? arc_named_type_clone(me.inferred_type) : (ArcNamedType){0},
    };
}

void identifier_type_resolve_names(IdentifierType* me, NameStack* names, ErrorCtx* errs) {
    if (my inferred) {
        return;
    }
    if (!my type) {
        my type = name_stack_get_type(names, span_sv(&my name));
        if (!my type) {
            error_ctx_error(
                errs,
                my name,
                2304,
                "Cannot find name '%.*s'.",
                c_SV(span_sv(&my name))
            );
            my type = name_stack_get_type(names, c_sv("any"));
            ASSERT(my type);
        }
    }
    c_foreach(i, vec_spanned_type, my generic_params) {
        type_resolve_names(&i.ref->type, names, errs);
    }
}

void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs) {
    c_foreach(i, vec_spanned_type, my generic_params) {
        type_typecheck(&i.ref->type, errs);
    }
}

// Indexed

void indexed_type_drop(IndexedType* me) {
    type_drop(&my interface);
    type_drop(&my index);
    type_drop(&my cached);
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

// Keyof

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

// Ternary

void ternary_type_drop(TernaryType* me) {
    type_drop(&my maybe_sub);
    type_drop(&my maybe_super);
    type_drop(&my if_true);
    type_drop(&my if_false);
}

TernaryType ternary_type_clone(TernaryType me) {
    return (TernaryType) {
        .maybe_sub = type_clone(me.maybe_sub),
        .maybe_super = type_clone(me.maybe_super),
        .if_true = type_clone(me.if_true),
        .if_false = type_clone(me.if_false),
    };
}
