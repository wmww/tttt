#include "identifier_type.h"
#include "name_stack.h"
#include "type.h"
#include "error.h"

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
