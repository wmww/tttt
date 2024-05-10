#include "interface_member.h"
#include "type.h"

void interface_member_drop(InterfaceMember* me) {
    cstr_drop(&my name);
    type_drop(&my type);
}

InterfaceMember interface_member_clone(InterfaceMember me) {
    return interface_member_make(me.span, me.readonly, cstr_clone(me.name), me.name_span, type_clone(me.type));
}
