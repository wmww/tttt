#include "interface_member.h"
#include "type.h"

void InterfaceMember_drop(InterfaceMember* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
}
