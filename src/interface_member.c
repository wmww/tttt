#include "interface_member.h"
#include "type.h"
#include "expr.h"
#include "name_stack.h"

void interface_member_drop(InterfaceMember* me) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: cstr_drop(&my key.name_string); break;
        case INTERFACE_KEY_INDEX: type_drop(&my key.index); break;
        case INTERFACE_KEY_EXPR: arc_expr_drop(&my key.expr); break;
        case INTERFACE_KEY_MAPPED:
            type_drop(&my key.mapped.in_type);
            type_drop(&my key.mapped.as_type);
            break;
        case INTERFACE_KEY_CALLABLE: break;
    }
    type_drop(&my type);
}

InterfaceMember interface_member_clone(InterfaceMember me) {
    switch (me.key.tag) {
        case INTERFACE_KEY_NAME: me.key.name_string = cstr_clone(me.key.name_string); break;
        case INTERFACE_KEY_INDEX: me.key.index = type_clone(me.key.index); break;
        case INTERFACE_KEY_EXPR: me.key.expr = arc_expr_clone(me.key.expr); break;
        case INTERFACE_KEY_MAPPED:
            me.key.mapped.in_type = type_clone(me.key.mapped.in_type);
            me.key.mapped.as_type = type_clone(me.key.mapped.as_type);
            break;
        case INTERFACE_KEY_CALLABLE: break;
    }
    me.type = type_clone(me.type);
    return me;
}

void cstr_append_interface_member_debug(cstr* target, InterfaceMember const* me, int indent) {
    if (my readonly) {
        cstr_append_sv(target, c_sv("readonly "));
    }
    switch (my key.tag) {
        case INTERFACE_KEY_NAME:
            cstr_append_sv(target, c_sv(CLR_NAME));
            cstr_append_s(target, my key.name_string);
            cstr_append_sv(target, c_sv(CLR0));
            break;

        case INTERFACE_KEY_INDEX:
            cstr_append_sv(target, c_sv("[" CLR_NAME));
            cstr_append_sv(target, span_sv(&my key.span));
            cstr_append_sv(target, c_sv(CLR0 ": "));
            cstr_append_type_debug(target, &my key.index, indent);
            cstr_append_sv(target, c_sv("]"));
            break;

        case INTERFACE_KEY_EXPR:
            cstr_append_sv(target, c_sv("["));
            cstr_append_expr_debug(target, my key.expr.get, indent);
            cstr_append_sv(target, c_sv("]"));
            break;

        case INTERFACE_KEY_MAPPED:
            if (my key.mapped.readonly) {
                cstr_append_sv(target, my key.mapped.readonly > 0 ? c_sv("+") : c_sv("-"));
                cstr_append_sv(target, c_sv("readonly "));
            }
            cstr_append_sv(target, c_sv("["));
            cstr_append_span(target, &my key.mapped.type_name);
            cstr_append_sv(target, c_sv(" in "));
            cstr_append_type_debug(target, &my key.mapped.in_type, indent);
            if (my key.mapped.has_as_type) {
                cstr_append_sv(target, c_sv(" as "));
                cstr_append_type_debug(target, &my key.mapped.as_type, indent);
            }
            cstr_append_sv(target, c_sv("]"));
            if (my key.mapped.optional) {
                cstr_append_sv(target, my key.mapped.optional > 0 ? c_sv("+?") : c_sv("-?"));
            }
            break;

        case INTERFACE_KEY_CALLABLE:
            break;
    }

    if (my key.tag != INTERFACE_KEY_CALLABLE) {
        if (my optional) {
            cstr_append_sv(target, c_sv("?"));
        }
        cstr_append_sv(target, c_sv(": "));
    }

    cstr_append_type_debug(target, &my type, indent);
}

void cstr_append_interface_member_error_msg(cstr* target, InterfaceMember const* me) {
    if (my readonly) {
            cstr_append_sv(target, c_sv("readonly "));
        }
        switch (my key.tag) {
        case INTERFACE_KEY_NAME:
            cstr_append_s(target, my key.name_string);
            cstr_append_sv(target, c_sv(": "));
            break;

        case INTERFACE_KEY_INDEX:
            cstr_append_sv(target, c_sv("["));
            cstr_append_sv(target, span_sv(&my key.span));
            cstr_append_sv(target, c_sv(": "));
            cstr_append_type_error_msg(target, &my key.index);
            cstr_append_sv(target, c_sv("]: "));
            break;

        case INTERFACE_KEY_EXPR:
            cstr_append_sv(target, c_sv("["));
            cstr_append_sv(target, span_sv(&my key.expr.get->span));
            cstr_append_sv(target, c_sv("]: "));
            break;

        case INTERFACE_KEY_MAPPED:
            if (my key.mapped.readonly) {
                cstr_append_sv(target, my key.mapped.readonly > 0 ? c_sv("+") : c_sv("-"));
                cstr_append_sv(target, c_sv("readonly "));
            }
            cstr_append_sv(target, c_sv("["));
            cstr_append_span(target, &my key.mapped.type_name);
            cstr_append_sv(target, c_sv(" in "));
            cstr_append_type_error_msg(target, &my key.mapped.in_type);
            if (my key.mapped.has_as_type) {
                cstr_append_sv(target, c_sv(" as "));
                cstr_append_type_error_msg(target, &my key.mapped.as_type);
            }
            cstr_append_sv(target, c_sv("]"));
            if (my key.mapped.optional) {
                cstr_append_sv(target, my key.mapped.optional > 0 ? c_sv("+?") : c_sv("-?"));
            }
            cstr_append_sv(target, c_sv(": "));
            break;

        case INTERFACE_KEY_CALLABLE:
            break;
    }
    cstr_append_type_error_msg(target, &my type);
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
            name_stack_add_type(
                names,
                my key.mapped.type_name,
                &type_any,
                nullptr
            );
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }

    type_resolve_names(&my type, names, errs);

    if (my key.tag == INTERFACE_KEY_MAPPED) {
        name_stack_pop_frame(names);
    }
}
