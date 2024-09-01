#include "interface_member.h"
#include "type.h"
#include "expr.h"
#include "name_stack.h"

void interface_member_drop(InterfaceMember* me) {
    vec_span_drop(&my declarations);
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: cstr_drop(&my key.name_string); break;
        case INTERFACE_KEY_INDEX: type_drop(&my key.index); break;
        case INTERFACE_KEY_EXPR: arc_expr_drop(&my key.expr); break;
        case INTERFACE_KEY_MAPPED:
            type_drop(&my key.mapped.in_type);
            type_drop(&my key.mapped.as_type);
            named_type_destroy(my key.mapped.named_type);
            break;
        case INTERFACE_KEY_CALLABLE: break;
    }
    type_drop(&my type);
}

InterfaceMember interface_member_clone(InterfaceMember me) {
    me.declarations = vec_span_clone(me.declarations);
    switch (me.key.tag) {
        case INTERFACE_KEY_NAME: me.key.name_string = cstr_clone(me.key.name_string); break;
        case INTERFACE_KEY_INDEX: me.key.index = type_clone(me.key.index); break;
        case INTERFACE_KEY_EXPR: me.key.expr = arc_expr_clone(me.key.expr); break;
        case INTERFACE_KEY_MAPPED:
            me.key.mapped.in_type = type_clone(me.key.mapped.in_type);
            me.key.mapped.as_type = type_clone(me.key.mapped.as_type);
            me.key.mapped.named_type = nullptr;
            break;
        case INTERFACE_KEY_CALLABLE: break;
    }
    me.type = type_clone(me.type);
    return me;
}

void interface_member_resolve_names(InterfaceMember* me, NameStack* names, ErrorCtx* errs) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: break;
        case INTERFACE_KEY_INDEX: type_resolve_names(&my key.index, names, errs); break;
        case INTERFACE_KEY_EXPR: expr_resolve_names(my key.expr.get, names, errs); break;

        case INTERFACE_KEY_MAPPED:
            type_resolve_names(&my key.mapped.in_type, names, errs);
            type_resolve_names(&my key.mapped.as_type, names, errs);
            name_stack_push_block_frame(names);
            if (!my key.mapped.named_type) {
                my key.mapped.named_type = make_named_type(
                    my key.mapped.type_name,
                    my key.mapped.type_name,
                    type_any,
                    nullptr
                );
            }
            name_stack_add_type(names, my key.mapped.named_type);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }

    type_resolve_names(&my type, names, errs);

    if (my key.tag == INTERFACE_KEY_MAPPED) {
        name_stack_pop_frame(names);
    }
}

void interface_member_typecheck(InterfaceMember* me, ErrorCtx* errs) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: break;

        case INTERFACE_KEY_INDEX:
            type_typecheck(&my key.index, errs);
            break;

        case INTERFACE_KEY_EXPR: {
            Type t = expr_get_type_and_typecheck(my key.expr.get, errs);
            type_drop(&t);
        }   break;

        case INTERFACE_KEY_MAPPED:
            type_typecheck(&my key.mapped.in_type, errs);
            type_typecheck(&my key.mapped.as_type, errs);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }
    type_typecheck(&my type, errs);
}

bool interface_key_extends(InterfaceMember const* sub, InterfaceMember const* super) {
    if (sub->key.tag == INTERFACE_KEY_CALLABLE && super->key.tag == INTERFACE_KEY_CALLABLE) {
        return true;
    } else if (sub->key.tag == INTERFACE_KEY_NAME &&
        super->key.tag == INTERFACE_KEY_NAME &&
        cstr_eq(&sub->key.name_string, &super->key.name_string)
    ) {
        return true;
    } else if (sub->key.tag == INTERFACE_KEY_INDEX && super->key.tag == INTERFACE_KEY_INDEX) {
        return type_extends(&sub->key.index, &super->key.index);
    } else {
        // TODO
        return false;
    }
}
