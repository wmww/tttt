#include "ternary_type.h"

void ternary_type_drop(TernaryType* self) {
    extends_conditional_drop(&self->conditional);
    type_drop(&self->TrueType);
    type_drop(&self->FalseType);
}

TernaryType ternary_type_clone(TernaryType self) {
    return (TernaryType) {
        .conditional = extends_conditional_clone(self.conditional),
        .TrueType = type_clone(self.TrueType),
        .FalseType = type_clone(self.FalseType),
    };
}

void cstr_append_ternary_type_debug(cstr* target, TernaryType* self, int indent) {
    cstr_append_type_debug(target, &self->conditional.MaybeSub, indent);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_debug(target, &self->conditional.MaybeSuper, indent);
    cstr_append_sv(target, c_sv("\n"));
    APPEND_INDENT(target, indent + 1);
    cstr_append_sv(target, c_sv("? "));
    cstr_append_type_debug(target, &self->TrueType, indent + 1);
    cstr_append_sv(target, c_sv("\n"));
    APPEND_INDENT(target, indent + 1);
    cstr_append_sv(target, c_sv(": "));
    cstr_append_type_debug(target, &self->FalseType, indent + 1);
}

void cstr_append_ternary_type_error_msg(cstr* target, TernaryType* self) {
    cstr_append_type_error_msg(target, &self->conditional.MaybeSub);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_error_msg(target, &self->conditional.MaybeSuper);
    cstr_append_sv(target, c_sv(" ? "));
    cstr_append_type_error_msg(target, &self->TrueType);
    cstr_append_sv(target, c_sv(" : "));
    cstr_append_type_error_msg(target, &self->FalseType);
}

Type* ternary_type_resolve(TernaryType* self) {
    if (extends_conditional_eval(&self->conditional)) {
        return &self->TrueType;
    } else {
        return &self->FalseType;
    }
}
