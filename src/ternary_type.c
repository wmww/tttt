#include "ternary_type.h"

void ternary_type_drop(TernaryType* me) {
    extends_conditional_drop(&me->conditional);
    type_drop(&me->TrueType);
    type_drop(&me->FalseType);
}

TernaryType ternary_type_clone(TernaryType me) {
    return (TernaryType) {
        .conditional = extends_conditional_clone(me.conditional),
        .TrueType = type_clone(me.TrueType),
        .FalseType = type_clone(me.FalseType),
    };
}

void cstr_append_ternary_type_debug(cstr* target, TernaryType* me, int indent) {
    cstr_append_type_debug(target, &me->conditional.MaybeSub, indent);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_debug(target, &me->conditional.MaybeSuper, indent);
    cstr_append_sv(target, c_sv("\n"));
    APPEND_INDENT(target, indent + 1);
    cstr_append_sv(target, c_sv("? "));
    cstr_append_type_debug(target, &me->TrueType, indent + 1);
    cstr_append_sv(target, c_sv("\n"));
    APPEND_INDENT(target, indent + 1);
    cstr_append_sv(target, c_sv(": "));
    cstr_append_type_debug(target, &me->FalseType, indent + 1);
}

void cstr_append_ternary_type_error_msg(cstr* target, TernaryType* me) {
    cstr_append_type_error_msg(target, &me->conditional.MaybeSub);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_error_msg(target, &me->conditional.MaybeSuper);
    cstr_append_sv(target, c_sv(" ? "));
    cstr_append_type_error_msg(target, &me->TrueType);
    cstr_append_sv(target, c_sv(" : "));
    cstr_append_type_error_msg(target, &me->FalseType);
}

Type* ternary_type_resolve(TernaryType* me) {
    if (extends_conditional_eval(&me->conditional)) {
        return &me->TrueType;
    } else {
        return &me->FalseType;
    }
}
