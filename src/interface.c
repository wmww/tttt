#include "interface.h"
#include "interface_member.h"
#include "type.h"

void Interface_drop(Interface* self) {
    vec_InterfaceMember_drop(&self->members);
}

cstr Interface_to_cstr(Interface* self) {
    cstr ret = cstr_lit("{ ");
    bool start = false;
    c_foreach(i, vec_InterfaceMember, self->members) {
        if (!start) {
            cstr_append_sv(&ret, c_sv(", "));
        }
        cstr_append_s(&ret, i.ref->name);
        cstr_append_sv(&ret, c_sv(": "));
        cstr type = Type_to_cstr(&i.ref->type);
        cstr_append_s(&ret, type);
        cstr_drop(&type);
    }
    cstr_append_sv(&ret, c_sv(" }"));
    return ret;
}
