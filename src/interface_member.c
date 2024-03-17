#include "interface_member.h"
#include "type.h"

void interface_member_drop(InterfaceMember* self) {
    cstr_drop(&self->name);
    type_drop(&self->type);
}

InterfaceMember interface_member_clone(InterfaceMember self) {
    return interface_member_make(cstr_clone(self.name), type_clone(self.type));
}
