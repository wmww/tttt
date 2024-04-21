#include "ternary_type.h"

void ternary_type_drop(TernaryType* me) {
    extends_conditional_drop(&my conditional);
    type_drop(&my TrueType);
    type_drop(&my FalseType);
}

TernaryType ternary_type_clone(TernaryType me) {
    return (TernaryType) {
        .conditional = extends_conditional_clone(me.conditional),
        .TrueType = type_clone(me.TrueType),
        .FalseType = type_clone(me.FalseType),
    };
}

void cstr_append_ternary_type_debug(cstr* target, TernaryType* me, int indent) {
    cstr_append_type_debug(target, &my conditional.MaybeSub, indent);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_debug(target, &my conditional.MaybeSuper, indent);
    cstr_append_sv(target, c_sv("\n"));
    APPEND_INDENT(target, indent + 1);
    cstr_append_sv(target, c_sv("? "));
    cstr_append_type_debug(target, &my TrueType, indent + 1);
    cstr_append_sv(target, c_sv("\n"));
    APPEND_INDENT(target, indent + 1);
    cstr_append_sv(target, c_sv(": "));
    cstr_append_type_debug(target, &my FalseType, indent + 1);
}

void cstr_append_ternary_type_error_msg(cstr* target, TernaryType* me) {
    cstr_append_type_error_msg(target, &my conditional.MaybeSub);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_error_msg(target, &my conditional.MaybeSuper);
    cstr_append_sv(target, c_sv(" ? "));
    cstr_append_type_error_msg(target, &my TrueType);
    cstr_append_sv(target, c_sv(" : "));
    cstr_append_type_error_msg(target, &my FalseType);
}

void ternary_type_resolve_names(TernaryType* me, NameStack* names, ErrorCtx* errs) {
    type_resolve_names(&my conditional.MaybeSub, names, errs);
    type_resolve_names(&my conditional.MaybeSuper, names, errs);
    type_resolve_names(&my TrueType, names, errs);
    type_resolve_names(&my FalseType, names, errs);
}

Type const* ternary_type_resolve(TernaryType* me) {
    if (extends_conditional_eval(&my conditional)) {
        return &my TrueType;
    } else {
        return &my FalseType;
    }
}
