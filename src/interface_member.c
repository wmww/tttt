#include "interface_member.h"
#include "type.h"
#include "expr.h"
#include "scope.h"

void interface_member_drop(InterfaceMember* me) {
    vec_span_drop(&my declarations);
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: cstr_drop(&my key.name_string); break;
        case INTERFACE_KEY_INDEX: type_drop(&my key.index); break;
        case INTERFACE_KEY_EXPR: arc_expr_drop(&my key.expr); break;
        case INTERFACE_KEY_MAPPED:
            type_drop(&my key.mapped.in_type);
            type_drop(&my key.mapped.as_type);
            arc_declaration_drop(&my key.mapped.owned_declaration);
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
            me.key.mapped.owned_declaration = arc_declaration_init();
            break;
        case INTERFACE_KEY_CALLABLE: break;
    }
    me.type = type_clone(me.type);
    return me;
}

void interface_member_lookup_names(InterfaceMember* me, NameStack* names, ErrorCtx* errs) {
    Scope scope;
    bool scope_initialized = false;

    switch (my key.tag) {
        case INTERFACE_KEY_NAME: break;
        case INTERFACE_KEY_INDEX: type_lookup_names(&my key.index, names, errs); break;
        case INTERFACE_KEY_EXPR: expr_lookup_names(my key.expr.get, names, errs); break;

        case INTERFACE_KEY_MAPPED:
            type_lookup_names(&my key.mapped.in_type, names, errs);
            type_lookup_names(&my key.mapped.as_type, names, errs);
            scope = scope_init(errs);
            scope_init_declaration(&scope, &my key.mapped.owned_declaration, (DeclProps){
                .name = my key.mapped.type_name,
                .full = my key.mapped.type_name,
                .tag = DECLARATION_TYPE,
                .declared_type = type_any,
            });
            my key.mapped.declaration_ref = my key.mapped.owned_declaration.get;
            scope_push_to_stack(&scope, names);
            scope_initialized = true;
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }

    type_lookup_names(&my type, names, errs);

    if (scope_initialized) {
        scope_drop(&scope);
    }
}

void interface_member_typecheck(InterfaceMember* me, ErrorCtx* errs) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: break;

        case INTERFACE_KEY_INDEX:
            type_typecheck(&my key.index, errs);
            break;

        case INTERFACE_KEY_EXPR:
            expr_typecheck(my key.expr.get, errs);
            break;

        case INTERFACE_KEY_MAPPED:
            type_typecheck(&my key.mapped.in_type, errs);
            type_typecheck(&my key.mapped.as_type, errs);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }
    type_typecheck(&my type, errs);
}

bool interface_member_key_overlaps(InterfaceMember const* me, Type const* index) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: {
            // Don't clone the string, and don't drop it the type. Hacky but safe.
            Type key = type_value(value_string(my key.name_string));
            return type_extends(&key, index);
        }

        case INTERFACE_KEY_INDEX:
            return type_overlaps(&my key.index, index);

        case INTERFACE_KEY_EXPR:
        case INTERFACE_KEY_MAPPED:
        case INTERFACE_KEY_CALLABLE:
            // TODO
            return false;
    }
    UNREACHABLE;
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
