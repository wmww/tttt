#include "interface.h"
#include "interface_member.h"
#include "type.h"

void Interface_drop(Interface* self) {
    vec_InterfaceMember_drop(&self->members);
}

Interface Interface_clone(Interface self) {
    return (Interface) {
        .members = vec_InterfaceMember_clone(self.members),
    };
}

void cstr_append_interface_debug(cstr* target, Interface const* self, int indent) {
    cstr_append_sv(target, c_sv("{\n"));
    c_foreach(i, vec_InterfaceMember, self->members) {
        APPEND_INDENT(target, indent + 1);
        cstr_append_sv(target, c_sv(CLR_NAME));
        cstr_append_s(target, i.ref->name);
        cstr_append_sv(target, c_sv(CLR0 ": "));
        cstr_append_type_debug(target, &i.ref->type, indent + 1);
        cstr_append_sv(target, c_sv(",\n"));
    }
    APPEND_INDENT(target, indent);
    cstr_append_sv(target, c_sv("}"));
}
