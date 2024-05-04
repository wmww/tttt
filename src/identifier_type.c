#include "identifier_type.h"
#include "name_stack.h"
#include "type.h"
#include "error.h"

void identifier_type_drop(IdentifierType* me) {
    cstr_drop(&my name);
    arc_named_type_drop(&my type);
    vec_type_drop(&my generic_params);
    type_drop(&my degenericized);
}

IdentifierType identifier_type_clone(IdentifierType me) {
    return (IdentifierType){
        .name =  cstr_clone(me.name),
        .span = me.span,
        .type = arc_named_type_clone(me.type),
        .generic_params = vec_type_clone(me.generic_params),
        .degenericized = type_clone(me.degenericized),
    };
}

void cstr_append_identifier_type_debug(cstr* target, IdentifierType* me, int indent) {
    cstr_append_sv(target, c_sv(CLR_TYPE));
    cstr_append_s(target, my name);
    cstr_append_sv(target, c_sv(CLR0));
    if (!vec_type_empty(&my generic_params)) {
        cstr_append_sv(target, c_sv("<"));
        c_foreach(i, vec_type, my generic_params) {
            cstr_append_type_debug(target, i.ref, indent);
            if (i.ref != i.end - 1) {
                cstr_append_sv(target, c_sv(", "));
            }
        }
        cstr_append_sv(target, c_sv(">"));
    }
}

void cstr_append_identifier_type_error_msg(cstr* target, IdentifierType* me) {
    cstr_append_s(target, my name);
    if (!vec_type_empty(&my generic_params)) {
        cstr_append_sv(target, c_sv("<"));
        c_foreach(i, vec_type, my generic_params) {
            cstr_append_type_error_msg(target, i.ref);
            if (i.ref != i.end - 1) {
                cstr_append_sv(target, c_sv(", "));
            }
        }
        cstr_append_sv(target, c_sv(">"));
    }
}

void identifier_type_resolve_names(IdentifierType* me, NameStack* names, ErrorCtx* errs) {
    ArcNamedType const* result = name_stack_get_type(names, cstr_sv(&my name));
    if (!result) {
        error_ctx_error(
            errs,
            my span,
            2304,
            "Cannot find name '%s'.",
            cstr_str(&my name)
        );
        result = name_stack_get_type(names, c_sv("any"));
    }
    if (result) {
        ASSERT(!my type.get);
        my type = arc_named_type_clone(*result);
    }
    c_foreach(i, vec_type, my generic_params) {
        type_resolve_names(i.ref, names, errs);
    }
}
