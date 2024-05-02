#include "ternary_type.h"

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

void cstr_append_ternary_type_debug(cstr* target, TernaryType* me, int indent) {
    cstr_append_type_debug(target, &my maybe_sub, indent);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_debug(target, &my maybe_super, indent);
    cstr_append_sv(target, c_sv("\n"));
    APPEND_INDENT(target, indent + 1);
    cstr_append_sv(target, c_sv("? "));
    cstr_append_type_debug(target, &my if_true, indent + 1);
    cstr_append_sv(target, c_sv("\n"));
    APPEND_INDENT(target, indent + 1);
    cstr_append_sv(target, c_sv(": "));
    cstr_append_type_debug(target, &my if_false, indent + 1);
}

void cstr_append_ternary_type_error_msg(cstr* target, TernaryType* me) {
    cstr_append_type_error_msg(target, &my maybe_sub);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_error_msg(target, &my maybe_super);
    cstr_append_sv(target, c_sv(" ? "));
    cstr_append_type_error_msg(target, &my if_true);
    cstr_append_sv(target, c_sv(" : "));
    cstr_append_type_error_msg(target, &my if_false);
}

void ternary_type_resolve_names(TernaryType* me, NameStack* names, ErrorCtx* errs) {
    type_resolve_names(&my maybe_sub, names, errs);
    type_resolve_names(&my maybe_super, names, errs);
    type_resolve_names(&my if_true, names, errs);
    type_resolve_names(&my if_false, names, errs);
}
