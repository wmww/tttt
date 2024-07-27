#include "identifier_type.h"
#include "name_stack.h"
#include "type.h"
#include "error.h"

void identifier_type_drop(IdentifierType* me) {
    vec_span_drop(&my namespaces);
    if (my inferred) {
        named_type_destroy(my type);
    }
    vec_spanned_type_drop(&my generic_params);
    type_drop(&my degenericized);
}

IdentifierType identifier_type_clone(IdentifierType me) {
    return (IdentifierType){
        .namespaces = vec_span_clone(me.namespaces),
        .name = me.name,
        .type = me.inferred ? nullptr : me.type,
        .generic_params = vec_spanned_type_clone(me.generic_params),
        .degenericized = type_any,
        .inferred = me.inferred,
    };
}

void cstr_append_identifier_type_debug(cstr* target, IdentifierType* me, int indent) {
    if (my inferred) {
        cstr_append_sv(target, c_sv("infer "));
    }
    if (my type && my type->is_typeof_class) {
        cstr_append_sv(target, c_sv("typeof "));
    }
    cstr_append_sv(target, c_sv(CLR_TYPE));
    c_foreach(i, vec_span, my namespaces) {
        cstr_append_sv(target, span_sv(i.ref));
        cstr_append_sv(target, c_sv("."));
    }
    cstr_append_span(target, &my name);
    cstr_append_sv(target, c_sv(CLR0));
    if (!vec_spanned_type_empty(&my generic_params)) {
        cstr_append_sv(target, c_sv("<"));
        c_foreach(i, vec_spanned_type, my generic_params) {
            cstr_append_type_debug(target, &i.ref->type, indent);
            if (i.ref != i.end - 1) {
                cstr_append_sv(target, c_sv(", "));
            }
        }
        cstr_append_sv(target, c_sv(">"));
    }
    if (!my type) {
        cstr_append_sv(target, c_sv(CLR_ERR " /* unresolved */" CLR0));
    } else if (!lazy_type_try_get(&my type->type)) {
        cstr_append_sv(target, c_sv(CLR_ERR " /* untypechecked */" CLR0));
    }
}

void cstr_append_identifier_type_error_msg(cstr* target, IdentifierType* me) {
    if (my inferred) {
        cstr_append_sv(target, c_sv("infer "));
    }
    if (my type && my type->is_typeof_class) {
        cstr_append_sv(target, c_sv("typeof "));
    }
    cstr_append_span(target, &my name);
    if (!vec_spanned_type_empty(&my generic_params)) {
        cstr_append_sv(target, c_sv("<"));
        c_foreach(i, vec_spanned_type, my generic_params) {
            cstr_append_type_error_msg(target, &i.ref->type);
            if (i.ref != i.end - 1) {
                cstr_append_sv(target, c_sv(", "));
            }
        }
        cstr_append_sv(target, c_sv(">"));
    }
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
