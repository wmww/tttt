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
        cstr_append_interface_member_debug(target, i.ref, indent + 1);
        cstr_append_sv(target, c_sv(",\n"));
    }
    APPEND_INDENT(target, indent);
    cstr_append_sv(target, c_sv("}"));
}

void cstr_append_interface_error_msg(cstr* target, Interface const* me) {
    if (vec_interface_member_size(&my members) == 1 && my members.data->key.tag == INTERFACE_KEY_CALLABLE) {
        cstr_append_type_error_msg(target, &my members.data->type);
        return;
    }

    cstr_append_sv(target, c_sv("{"));
    if (!vec_interface_member_empty(&my members)) {
        cstr_append_sv(target, c_sv(" "));
    }
    c_foreach(i, vec_interface_member, my members) {
        cstr_append_interface_member_error_msg(target, i.ref);
        cstr_append_sv(target, c_sv("; "));
    }
    cstr_append_sv(target, c_sv("}"));
}
