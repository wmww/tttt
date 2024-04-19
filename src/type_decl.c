#include "type_decl.h"

void type_decl_drop(TypeDecl* me) {
    cstr_drop(&my name);
    generic_drop(&my generic);
    type_drop(&my type);
}

void cstr_append_type_decl_debug(cstr* target, TypeDecl* me, int indent) {
    if (my is_interface) {
        cstr_append_sv(target, c_sv("interface " CLR_NAME));
        cstr_append_s(target, my name);
        cstr_append_sv(target, c_sv(CLR0));
        cstr_append_generic_debug(target, &my generic, indent);
        cstr_append_sv(target, c_sv(" "));
    } else {
        cstr_append_sv(target, c_sv("type " CLR_NAME));
        cstr_append_s(target, my name);
        cstr_append_sv(target, c_sv(CLR0));
        cstr_append_generic_debug(target, &my generic, indent);
        cstr_append_sv(target, c_sv(" = "));
    }
    cstr_append_type_debug(target, &my type, indent);
}
