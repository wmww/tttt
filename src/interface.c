#include "interface.h"
#include "interface_member.h"
#include "display.h"

void interface_drop(Interface* me) {
    vec_interface_member_drop(&my members);
}

Interface interface_clone(Interface me) {
    return (Interface) {
        .members = vec_interface_member_clone(me.members),
    };
}

Interface interface_from_merged(VecType const* types) {
    VecInterfaceMember members = vec_interface_member_init();
    c_foreach(type_i, vec_type, *types) {
        Type const* type = type_resolve(type_i.ref);
        if (type->tag != TYPE_INTERFACE) {
            cstr type_s = cstr_null;
            display_type(debug_display(&type_s), type);
            PANIC("Only interfaces can be merged, but one of the types in the merge list was %s", cstr_str(&type_s));
        }
        // PERF: do not merge interfaces this way, for obvious reasons
        c_foreach(new_member_i, vec_interface_member, type->interface.members) {
            InterfaceMember const* new_member = new_member_i.ref;
            InterfaceMember* matching = nullptr;
            c_foreach(existing_member_i, vec_interface_member, members) {
                InterfaceMember* existing_member = existing_member_i.ref;
                switch (new_member->key.tag) {
                    case INTERFACE_KEY_NAME:
                        if (cstr_equals_s(&new_member->key.name_string, existing_member->key.name_string)) {
                            matching = existing_member;
                        }
                        break;

                    case INTERFACE_KEY_INDEX:
                    case INTERFACE_KEY_EXPR:
                    case INTERFACE_KEY_MAPPED:
                    case INTERFACE_KEY_CALLABLE:
                        // TODO
                        if (new_member->key.tag == existing_member->key.tag) {
                            matching = existing_member;
                        }
                        break;
                }
                if (matching) {
                    break;
                }
            }
            if (matching) {
                // Somewhere we want to compare types and show errors if they're different, but that can be done in the
                // typecheck function of the interface declarations. For now just use the type of the first member we
                // encounter with each name.
                // TODO: add declarations
            } else {
                vec_interface_member_push(&members, interface_member_clone(*new_member));
            }
        }
    }
    return (Interface){
        .members = members,
    };
}
