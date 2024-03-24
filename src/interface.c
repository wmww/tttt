#include "interface.h"
#include "interface_member.h"
#include "type.h"

void interface_drop(Interface* me) {
    vec_interface_member_drop(&my members);
}

Interface interface_clone(Interface me) {
    return (Interface) {
        .members = vec_interface_member_clone(me.members),
    };
}

void cstr_append_interface_debug(cstr* target, Interface const* me, int indent) {
    cstr_append_sv(target, c_sv("{\n"));
    c_foreach(i, vec_interface_member, my members) {
        APPEND_INDENT(target, indent + 1);
        cstr_append_sv(target, c_sv(CLR_NAME));
        cstr_append_s(target, i.ref->name);
        cstr_append_sv(target, c_sv(CLR0 ": "));
        cstr_append_type_debug(target, &i.ref->type, indent + 1);
        cstr_append_sv(target, c_sv(";\n"));
    }
    APPEND_INDENT(target, indent);
    cstr_append_sv(target, c_sv("}"));
}

void cstr_append_interface_error_msg(cstr* target, Interface const* me) {
    cstr_append_sv(target, c_sv("{"));
    c_foreach(i, vec_interface_member, my members) {
        cstr_append_s(target, i.ref->name);
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_error_msg(target, &i.ref->type);
        cstr_append_sv(target, c_sv("; "));
    }
    cstr_append_sv(target, c_sv("}"));
}
