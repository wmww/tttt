#include "interface_member.h"
#include "type.h"

void InterfaceMember_drop(InterfaceMember* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
}

InterfaceMember InterfaceMember_clone(InterfaceMember self) {
    return (InterfaceMember) {
        .name = cstr_clone(self.name),
        .type = Type_clone(self.type),
    };
}
