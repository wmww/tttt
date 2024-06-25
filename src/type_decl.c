#include "type_decl.h"

void type_decl_drop(TypeDecl* me) {
    generic_drop(&my generic);
    vec_spanned_type_drop(&my interface_extends);
    type_drop(&my type);
}

void cstr_append_type_decl_debug(cstr* target, TypeDecl* me, int indent) {
    if (my is_interface) {
        cstr_append_sv(target, c_sv("interface " CLR_NAME));
        cstr_append_span(target, &my name);
        cstr_append_sv(target, c_sv(CLR0));
        cstr_append_generic_debug(target, &my generic, indent);
        if (!vec_spanned_type_empty(&my interface_extends)) {
            cstr_append_sv(target, c_sv(" extends "));
            c_foreach(i, vec_spanned_type, my interface_extends) {
                cstr_append_type_debug(target, &i.ref->type, indent);
                if (i.ref != i.end - 1) {
                    cstr_append_sv(target, c_sv(", "));
                }
            }
        }
        cstr_append_sv(target, c_sv(" "));
    } else {
        cstr_append_sv(target, c_sv("type " CLR_NAME));
        cstr_append_span(target, &my name);
        cstr_append_sv(target, c_sv(CLR0));
        cstr_append_generic_debug(target, &my generic, indent);
        cstr_append_sv(target, c_sv(" = "));
    }
    cstr_append_type_debug(target, &my type, indent);
}
