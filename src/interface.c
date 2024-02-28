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

cstr Interface_to_cstr(Interface* self, int indent) {
    cstr ret = cstr_lit("{\n");
    c_foreach(i, vec_InterfaceMember, self->members) {
        APPEND_INDENT(&ret, indent + 1);
        cstr_append(&ret, CLR_NAME);
        cstr_append_s(&ret, i.ref->name);
        cstr_append_sv(&ret, c_sv(CLR0 ": "));
        cstr type = Type_to_cstr(&i.ref->type, indent + 1);
        cstr_append_s(&ret, type);
        cstr_drop(&type);
        cstr_append(&ret, ",\n");
    }
    APPEND_INDENT(&ret, indent);
    cstr_append_sv(&ret, c_sv("}"));
    return ret;
}

void NamedInterface_drop(NamedInterface* self) {
    cstr_drop(&self->name);
    Interface_drop(&self->content);
}

cstr NamedInterface_to_cstr(NamedInterface* self, int indent) {
    cstr ret = cstr_lit("interface " CLR_NAME);
    cstr_append_s(&ret, self->name);
    cstr_append(&ret, CLR0 " ");
    cstr content = Interface_to_cstr(&self->content, indent);
    cstr_append_s(&ret, content);
    cstr_drop(&content);
    return ret;
}
