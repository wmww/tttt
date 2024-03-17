#include "interface_member.h"
#include "type.h"

void interface_member_drop(InterfaceMember* me) {
    cstr_drop(&me->name);
    type_drop(&me->type);
}

InterfaceMember interface_member_clone(InterfaceMember me) {
    return interface_member_make(cstr_clone(me.name), type_clone(me.type));
}
