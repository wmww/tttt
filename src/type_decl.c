#include "type_decl.h"

void type_decl_drop(TypeDecl* me) {
    cstr_drop(&my name);
    type_drop(&my type);
}

void cstr_append_type_decl_debug(cstr* target, TypeDecl* me, int indent) {
    if (my is_interface) {
        cstr_append_sv(target, c_sv("interface " CLR_NAME));
        cstr_append_s(target, my name);
        cstr_append_sv(target, c_sv(CLR0 " "));
    } else {
        cstr_append_sv(target, c_sv("type " CLR_NAME));
        cstr_append_s(target, my name);
        cstr_append_sv(target, c_sv(CLR0 " = "));
    }
    cstr_append_type_debug(target, &my type, indent);
}
