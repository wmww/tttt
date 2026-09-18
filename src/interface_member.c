#include "interface_member.h"
#include "type.h"
#include "expr.h"
#include "scope.h"
#include "error.h"
#include "callable_type.h"
#include "function_impl.h"
#include "ty/ty.h"

void interface_member_drop(InterfaceMember* me) {
    vec_span_drop(&my declarations);
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: cstr_drop(&my key.name_string); break;
        case INTERFACE_KEY_INDEX: type_drop(&my key.index.type); break;
        case INTERFACE_KEY_EXPR:
            if (my key.expr.owned) {
                expr_drop(my key.expr.owned);
                free(my key.expr.owned);
            }
            break;
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
        case INTERFACE_KEY_INDEX: me.key.index.type = type_clone(me.key.index.type); break;
        case INTERFACE_KEY_EXPR: me.key.expr.owned = nullptr; break;
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

void interface_member_lookup_names(InterfaceMember* me, ScopeStack* names, ErrorCtx* errs) {
    Scope scope;
    bool scope_initialized = false;

    switch (my key.tag) {
        case INTERFACE_KEY_NAME: break;
        case INTERFACE_KEY_INDEX: type_lookup_names(&my key.index.type, names, errs); break;
        case INTERFACE_KEY_EXPR: expr_lookup_names(my key.expr.ref, names, errs); break;

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

void interface_member_typecheck(InterfaceMember* me, bool is_in_class, ErrorCtx* errs) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: break;

        case INTERFACE_KEY_INDEX:
            type_typecheck(&my key.index.type, errs);
            break;

        case INTERFACE_KEY_EXPR:
            Ty const* key_type = expr_get_type(my key.expr.ref);
            // `unique symbol` is parsed as plain symbol, and an any key is never an error
            if (!(key_type->flags & (TYF_LITERAL | TYF_ENUM | TYF_NULLABLE | TYF_ANY | TYF_ES_SYMBOL))) {
                if (is_in_class) {
                    if (my is_method &&
                        my type.tag == TYPE_CALLABLE &&
                        my type.callable->owned_function_impl &&
                        my type.callable->owned_function_impl->has_body
                    ) {
                        // Only overload signatures are restricted, a method with a body can have any key
                    } else if (my is_method) {
                        error_ctx_error_d(errs, my key.span, 1168,
                            "A computed property name in a method overload must refer to an expression whose type is a literal type or a 'unique symbol' type.");
                    } else {
                        error_ctx_error_d(errs, my key.span, 1166,
                            "A computed property name in a class property declaration must have a simple literal type or a 'unique symbol' type.");
                    }
                } else {
                    error_ctx_error_d(errs, my key.span, 1169,
                        "A computed property name in an interface must refer to an expression whose type is a literal type or a 'unique symbol' type.");
                }
            }
            expr_typecheck(my key.expr.ref, errs);
            break;

        case INTERFACE_KEY_MAPPED:
            type_typecheck(&my key.mapped.in_type, errs);
            type_typecheck(&my key.mapped.as_type, errs);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }
    type_typecheck(&my type, errs);
}
