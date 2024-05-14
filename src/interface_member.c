#include "interface_member.h"
#include "type.h"
#include "expr.h"

void interface_member_drop(InterfaceMember* me) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: cstr_drop(&my key.name); break;
        case INTERFACE_KEY_INDEX: type_drop(&my key.index); break;
        case INTERFACE_KEY_MAPPED: arc_expr_drop(&my key.mapped); break;
        case INTERFACE_KEY_CALLABLE: break;
    }
    type_drop(&my type);
}

InterfaceMember interface_member_clone(InterfaceMember me) {
    switch (me.key.tag) {
        case INTERFACE_KEY_NAME: me.key.name = cstr_clone(me.key.name); break;
        case INTERFACE_KEY_INDEX: me.key.index = type_clone(me.key.index); break;
        case INTERFACE_KEY_MAPPED: me.key.mapped = arc_expr_clone(me.key.mapped); break;
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
            cstr_append_s(target, my key.name);
            cstr_append_sv(target, c_sv(CLR0 ": "));
            break;

        case INTERFACE_KEY_INDEX:
            cstr_append_sv(target, c_sv("[" CLR_NAME));
            cstr_append_sv(target, span_sv(&my key.span));
            cstr_append_sv(target, c_sv(CLR0 ": "));
            cstr_append_type_debug(target, &my key.index, indent);
            cstr_append_sv(target, c_sv("]: "));
            break;

        case INTERFACE_KEY_MAPPED:
            cstr_append_sv(target, c_sv("["));
            cstr_append_expr_debug(target, my key.mapped.get, indent);
            cstr_append_sv(target, c_sv("]: "));
            break;

        case INTERFACE_KEY_CALLABLE:
            break;
    }
    cstr_append_type_debug(target, &my type, indent);
}

void cstr_append_interface_member_error_msg(cstr* target, InterfaceMember const* me) {
    if (my readonly) {
            cstr_append_sv(target, c_sv("readonly "));
        }
        switch (my key.tag) {
        case INTERFACE_KEY_NAME:
            cstr_append_s(target, my key.name);
            cstr_append_sv(target, c_sv(": "));
            break;

        case INTERFACE_KEY_INDEX:
            cstr_append_sv(target, c_sv("["));
            cstr_append_sv(target, span_sv(&my key.span));
            cstr_append_sv(target, c_sv(": "));
            cstr_append_type_error_msg(target, &my key.index);
            cstr_append_sv(target, c_sv("]: "));
            break;

        case INTERFACE_KEY_MAPPED:
            cstr_append_sv(target, c_sv("["));
            if (my key.mapped.get->tag == EXPR_LITERAL) {
                cstr_append_value_error_msg(target, &my key.mapped.get->literal);
            } else {
                cstr_append_sv(target, span_sv(&my key.mapped.get->span));
            }
            cstr_append_sv(target, c_sv("]: "));
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
        case INTERFACE_KEY_MAPPED: expr_resolve_names(my key.mapped.get, names, errs); break;
        case INTERFACE_KEY_CALLABLE: break;
    }
    type_resolve_names(&my type, names, errs);
}
