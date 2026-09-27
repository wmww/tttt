#include "interface_member.h"
#include "type.h"
#include "expr.h"
#include "scope.h"
#include "error.h"
#include "callable_type.h"
#include "function_impl.h"
#include "ty/ty.h"
#include "flow.h"

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
            break;
        case INTERFACE_KEY_CALLABLE: break;
    }
    type_drop(&my type);
    if (my has_write_type) type_drop(&my write_type);
}

void interface_member_lookup_names(InterfaceMember* me, Scope* names, ErrorCtx* errs) {
    // `readonly k: unique symbol`. The owner is set by the interface declaration, if there is one
    if (my type.tag == TYPE_UNIQUE_SYMBOL && my readonly && my key.tag == INTERFACE_KEY_NAME) {
        my type.unique_symbol.key = me;
        my type.unique_symbol.member = my key.span;
    }
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: break;
        case INTERFACE_KEY_INDEX: {
            // (The key is a parameter)
            TypeFlow const* saved_flow = type_flow_enter_parameter();
            type_lookup_names(&my key.index.type, names, errs);
            type_flow_restore(saved_flow);
        }   break;
        case INTERFACE_KEY_EXPR: expr_lookup_names(my key.expr.ref, names, errs); break;

        case INTERFACE_KEY_MAPPED:
            type_lookup_names(&my key.mapped.in_type, names, errs);
            names = scope_new(names, SCOPE_BLOCK);
            scope_init_declaration(names, &my key.mapped.owned_symbol, (DeclProps){
                .name = my key.mapped.type_name,
                .full = my key.mapped.type_name,
                .kind = SYMBOL_DECL_MAPPED_KEY,
                .node = me,
            });
            my key.mapped.symbol_ref = my key.mapped.owned_symbol;
            // The `as` clause sees the key parameter
            type_lookup_names(&my key.mapped.as_type, names, errs);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }

    type_lookup_names(&my type, names, errs);
    if (my has_write_type) type_lookup_names(&my write_type, names, errs);
}

void interface_member_typecheck(InterfaceMember* me, bool is_in_class, ErrorCtx* errs) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: break;

        case INTERFACE_KEY_INDEX: {
            type_typecheck(&my key.index.type, errs);
            // tsc's checkGrammarIndexSignatureParameters, the type part
            Checker* checker = current_checker;
            Ty const* key = ty_of_node(&my key.index.type);
            TyList parts = key->kind == TY_UNION ? key->composite.types : (TyList){ .data = &key, .len = 1 };
            bool literal = false, valid = true;
            for (u32 i = 0; i < parts.len; i++) {
                literal |= (parts.data[i]->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL)) != 0;
                valid &= ty_is_valid_index_key(parts.data[i]);
            }
            if (literal || ty_is_generic_object(checker, key) || ty_is_generic_index(key)) {
                error_ctx_grammar_error_d(errs, my key.index.name, 1337, "An index signature parameter type cannot be a "
                    "literal type or generic type. Consider using a mapped object type instead.");
            } else if (!valid) {
                error_ctx_grammar_error_d(errs, my key.index.name, 1268,
                    "An index signature parameter type must be 'string', 'number', 'symbol', or a template literal type.");
            }
        }   break;

        case INTERFACE_KEY_EXPR:
            ty_check_computed_name(current_checker, my key.expr.ref, my key.span, true, errs);
            if (expr_is_non_bindable_dynamic_name(my key.expr.ref)) {
                if (is_in_class) {
                    if (my is_method &&
                        my type.tag == TYPE_CALLABLE &&
                        my type.callable->function->has_body
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
    if (my has_write_type) type_typecheck(&my write_type, errs);
}
