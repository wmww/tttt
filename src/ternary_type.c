#include "ternary_type.h"

void TernaryType_drop(TernaryType* self) {
    ExtendsConditional_drop(&self->conditional);
    Type_drop(&self->trueType);
    Type_drop(&self->falseType);
}

TernaryType TernaryType_clone(TernaryType self) {
    return (TernaryType) {
        .conditional = ExtendsConditional_clone(self.conditional),
        .trueType = Type_clone(self.trueType),
        .falseType = Type_clone(self.falseType),
    };
}

void cstr_append_ternary_type_debug(cstr* target, TernaryType* self, int indent) {
    cstr_append_type_debug(target, &self->conditional.maybeSub, indent);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_debug(target, &self->conditional.maybeSuper, indent);
    cstr_append_sv(target, c_sv("\n"));
    APPEND_INDENT(target, indent + 1);
    cstr_append_sv(target, c_sv("? "));
    cstr_append_type_debug(target, &self->trueType, indent + 1);
    cstr_append_sv(target, c_sv("\n"));
    APPEND_INDENT(target, indent + 1);
    cstr_append_sv(target, c_sv(": "));
    cstr_append_type_debug(target, &self->falseType, indent + 1);
}

void cstr_append_ternary_type_error_msg(cstr* target, TernaryType* self) {
    cstr_append_type_error_msg(target, &self->conditional.maybeSub);
    cstr_append_sv(target, c_sv(" extends "));
    cstr_append_type_error_msg(target, &self->conditional.maybeSuper);
    cstr_append_sv(target, c_sv(" ? "));
    cstr_append_type_error_msg(target, &self->trueType);
    cstr_append_sv(target, c_sv(" : "));
    cstr_append_type_error_msg(target, &self->falseType);
}

Type* TernaryType_resolve(TernaryType* self) {
    if (ExtendsConditional_eval(&self->conditional)) {
        return &self->trueType;
    } else {
        return &self->falseType;
    }
}
