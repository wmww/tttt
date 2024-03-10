#include "interface_member.h"
#include "type.h"

void InterfaceMember_drop(InterfaceMember* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
}

InterfaceMember InterfaceMember_clone(InterfaceMember self) {
    return InterfaceMember_make(cstr_clone(self.name), Type_clone(self.type));
}
