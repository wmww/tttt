#include "interface.h"
#include "interface_member.h"

void interface_drop(Interface* me) {
    vec_interface_member_drop(&my members);
}

Interface interface_clone(Interface me) {
    return (Interface) {
        .members = vec_interface_member_clone(me.members),
    };
}
