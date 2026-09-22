#include "class_decl.h"
#include "binding.h"
#include "flow.h"
#include "ty/ty.h"
#include "statement.h"
#include "interface_member.h"
#include "callable_type.h"
#include "several_types.h"
#include "error.h"

void class_item_drop(ClassItem* me) {
    vec_expr_drop(&my decorators);
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_drop(&my constructor.func);
            break;

        case CLASS_ITEM_METHOD:
            cstr_drop(&my method.name_string);
            if (my method.computed_key) {
                expr_drop(my method.computed_key);
                free(my method.computed_key);
            }
            function_impl_drop(&my method.func);
            lazy_type_drop(&my method.accessor_type);
            break;

        case CLASS_ITEM_FIELD:
            cstr_drop(&my field.name_string);
            if (my field.computed_key) {
                expr_drop(my field.computed_key);
                free(my field.computed_key);
            }
            type_drop(&my field.explicit_type);
            expr_drop(&my field.value);
            lazy_type_drop(&my field.lazy_type);
            break;

        case CLASS_ITEM_INTERFACE_MEMBER:
            interface_member_drop(&my interface_memeber);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            block_drop(&my static_block);
            break;
    }
}

bool class_item_is_static(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR: return false;
        case CLASS_ITEM_METHOD: return my method.is_static;
        case CLASS_ITEM_FIELD: return my field.is_static;
        case CLASS_ITEM_INTERFACE_MEMBER: return false;
        case CLASS_ITEM_STATIC_BLOCK: return true;
    }
    impossible;
}

Ty const* class_item_field_evaluate_type(ClassItem* me) {
    check(my tag == CLASS_ITEM_FIELD);
    Checker* checker = current_checker;
    if (my field.has_explicit_type) return ty_of_node(&my field.explicit_type);
    Ty const* ret = ty_widened_for_declaration(checker, ty_expr_type(checker, &my field.value), !my field.is_readonly);
    // A unique symbol made for another declaration is only a symbol here (tsc's widenTypeForVariableLikeDeclaration)
    if ((ret->flags & TYF_UNIQUE_ES_SYMBOL) && ret->literal.unique->key != me) ret = ty_intrinsic(checker, TY_ES_SYMBOL);
    return ret;
}

Ty const* class_item_accessor_evaluate_type(ClassItem* me) {
    check(my tag == CLASS_ITEM_METHOD && my method.accessor);
    if (my method.accessor == CLASS_ACCESSOR_SET && my method.accessor_getter) {
        return lazy_type_get(&my method.accessor_getter->method.accessor_type);
    }
    Checker* checker = current_checker;
    Members const* members = ty_members(checker, ty_function_type(checker, &my method.func));
    if (members->call_count != 1) return ty_intrinsic(checker, TY_ANY);
    Signature const* sig = members->call[0];
    signature_resolve(checker, sig);
    if (my method.accessor == CLASS_ACCESSOR_GET) {
        return ty_widened_for_declaration(checker, signature_return_type(checker, sig), true);
    }
    return sig->param_count ? sig->params[0].type : ty_intrinsic(checker, TY_ANY);
}

Ty const* class_item_get_type(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR: return lazy_type_get(&my constructor.func.lazy_type);
        case CLASS_ITEM_METHOD:
            return lazy_type_get(my method.accessor ? &my method.accessor_type : &my method.func.lazy_type);
        case CLASS_ITEM_FIELD: return lazy_type_get(&my field.lazy_type);
        case CLASS_ITEM_INTERFACE_MEMBER: return ty_of_node(&my interface_memeber.type);
        case CLASS_ITEM_STATIC_BLOCK: return ty_intrinsic(current_checker, TY_NEVER);
    }
    impossible;
}

void class_decl_drop(ClassDecl* me) {
    vec_expr_drop(&my decorators);
    generic_drop(&my generic);
    expr_drop(&my extends);
    vec_type_drop(&my implements);
    vec_class_item_drop(&my items);
}

void class_item_lookup_names(ClassItem* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_expr, &my decorators) {
        expr_lookup_names(i, names, errs);
    }
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_lookup_names(&my constructor.func, false, names, errs);
            break;

        case CLASS_ITEM_METHOD:
            if (my method.computed_key) expr_lookup_names(my method.computed_key, names, errs);
            lazy_type_bind(&my method.accessor_type, LAZY_TYPE_CLASS_ITEM_ACCESSOR);
            my method.func.is_method = !my method.accessor;
            my method.func.accessor = (uint8_t)my method.accessor;
            my method.func.member_name = my method.name_span;
            function_impl_lookup_names(&my method.func, false, names, errs);
            break;

        case CLASS_ITEM_FIELD:
            if (my field.computed_key) expr_lookup_names(my field.computed_key, names, errs);
            lazy_type_bind(&my field.lazy_type, LAZY_TYPE_CLASS_ITEM_FIELD);
            type_lookup_names(&my field.explicit_type, names, errs);
            if (my field.value.tag == EXPR_IMPLICIT_ANY) {
                expr_lookup_names(&my field.value, names, errs);
            } else {
                // An initializer is a control flow container of its own
                FlowBuilder* outer = names->flow;
                FlowBuilder flow;
                flow_builder_init(&flow, outer, names, nullptr, errs);
                flow.current = flow_new_start(&flow, nullptr, false);
                names->flow = &flow;
                expr_lookup_names(&my field.value, names, errs);
                flow_builder_finish(&flow, c_sv("field"), my field.value.outer_span);
                names->flow = outer;
            }
            break;

        case CLASS_ITEM_INTERFACE_MEMBER:
            interface_member_lookup_names(&my interface_memeber, names, errs);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            Scope* block_scope = scope_new(names, SCOPE_STATIC_BLOCK);
            block_add_names(&my static_block, block_scope);
            block_lookup_names_inline_container(&my static_block, block_scope, errs);
            break;
    }
}

void class_decl_add_names(ClassDecl* me, Scope* scope, bool exported) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .kind = SYMBOL_DECL_CLASS,
        .node = me,
    });
}

static void class_members_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs, bool static_members) {
    Scope* this_scope = scope_new(names, SCOPE_BLOCK);
    this_scope->this_ref = (ThisRef){ .kind = static_members ? THIS_CLASS_STATIC : THIS_CLASS_INSTANCE, .symbol = my symbol };

    for_each (i, vec_class_item, &my items) {
        if (class_item_is_static(i) == static_members) {
            class_item_lookup_names(i, this_scope, errs);
        }
    }
}

void class_decl_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_expr, &my decorators) {
        expr_lookup_names(i, names, errs);
    }

    names = scope_new_generic(names, &my generic, errs);

    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_CONSTRUCTOR) {
            if (i->constructor.func.ret_explicit) {
                error_ctx_error_d(
                    errs,
                    i->constructor.func.ret_span,
                    1093,
                    "Type annotation cannot appear on a constructor declaration."
                );
                type_drop(&i->constructor.func.ret);
            }
            check(i->constructor.func.ret.tag == TYPE_ANY);
            i->constructor.func.ret_explicit = false;
            i->constructor.func.constructed_class = my symbol;
        }
    }

    for_each (i, vec_class_item, &my items) {
        if (i->tag == CLASS_ITEM_FIELD && i->field.is_static && i->field.is_readonly && !i->field.computed_key) {
            unique_symbol_decl_mark(
                (UniqueSymbolDecl){ .key = i, .owner = my symbol, .member = i->field.name_span },
                &i->field.explicit_type, &i->field.value
            );
        }
        if (i->tag == CLASS_ITEM_METHOD && i->method.accessor) {
            for_each (other, vec_class_item, &my items) {
                if (other->tag == CLASS_ITEM_METHOD &&
                    other->method.accessor &&
                    other->method.is_static == i->method.is_static &&
                    cstr_eq(&other->method.name_string, &i->method.name_string)
                ) {
                    if (!i->method.accessor_first) {
                        i->method.accessor_first = other;
                    }
                    if (other->method.accessor == CLASS_ACCESSOR_GET && !i->method.accessor_getter) {
                        i->method.accessor_getter = other;
                        if (i->method.accessor == CLASS_ACCESSOR_SET) function_impl_pair_setter(&i->method.func, &other->method.func);
                    }
                }
            }
        }
    }

    expr_lookup_names(&my extends, names, errs);
    for_each (i, vec_type, &my implements) {
        type_lookup_names(i, names, errs);
    }

    class_members_lookup_names(me, names, errs, true);
    class_members_lookup_names(me, names, errs, false);
}

// TS1166/TS1168: the key of a property or of a method overload has to be something the class can be typed by
static void class_computed_key_typecheck(ClassItem* item, ErrorCtx* errs) {
    bool field = item->tag == CLASS_ITEM_FIELD;
    Expr* key = field ? item->field.computed_key : item->method.computed_key;
    expr_typecheck(key, errs);
    if (!field && item->method.func.has_body) return; // Only overload signatures are restricted
    Ty const* key_type = expr_get_type(key);
    // tsc's checkGrammarForInvalidDynamicName: an entity name that isn't a literal is a late bound index signature
    if (key_type->flags & (TYF_LITERAL | TYF_ENUM | TYF_UNIQUE_ES_SYMBOL | TYF_ANY) || expr_is_entity_name(key)) return;
    Span span = field ? item->field.name_span : item->method.name_span;
    if (field) {
        error_ctx_error_d(errs, span, 1166,
            "A computed property name in a class property declaration must have a simple literal type or a 'unique symbol' type.");
    } else {
        error_ctx_error_d(errs, span, 1168,
            "A computed property name in a method overload must refer to an expression whose type is a literal type or a 'unique symbol' type.");
    }
}

void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs) {
    if (my has_typechecked) {
        return;
    }
    my has_typechecked = true;

    for_each (i, vec_expr, &my decorators) {
        expr_typecheck(i, errs);
    }
    for_each (i, vec_class_item, &my items) {
        for_each (decorator, vec_expr, &i->decorators) {
            expr_typecheck(decorator, errs);
        }
    }

    generic_typecheck(&my generic, errs);

    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        expr_typecheck(&my extends, errs);
        Ty const* extends_type = ty_expr_type(current_checker, &my extends);
        if (extends_type->flags & TYF_NULL) {
        } else {
            CallExpr call_expr = {
                .uses_new = true,
                .is_extends = true,
                .callable = expr_implicit_any(my extends.outer_span),
                .args = vec_expr_init(),
            };
            ty_call_typecheck(current_checker, extends_type, &call_expr, nullptr, my extends.outer_span, errs);
        }
    }

    int constructor_count = 0; // Implementations
    for_each (i, vec_class_item, &my items) {
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                function_impl_typecheck(&i->constructor.func, errs);
                if (i->constructor.func.has_body) constructor_count++; // Overloads are fine
                break;

            case CLASS_ITEM_METHOD:
                if (i->method.computed_key) class_computed_key_typecheck(i, errs);
                function_impl_typecheck(&i->method.func, errs);
                break;

            case CLASS_ITEM_FIELD: {
                if (i->field.computed_key) class_computed_key_typecheck(i, errs);
                expr_typecheck(&i->field.value, errs);
                if (i->field.has_explicit_type) {
                    ty_check_assignable(
                        current_checker, expr_get_type(&i->field.value), ty_of_node(&i->field.explicit_type),
                        i->field.name_span, errs
                    );
                }
            }   break;

            case CLASS_ITEM_INTERFACE_MEMBER:
                interface_member_typecheck(&i->interface_memeber, true, errs);
                break;

            case CLASS_ITEM_STATIC_BLOCK:
                block_typecheck(&i->static_block, (BlockTypecheckCtx){0}, errs);
                break;
        }
    }

    if (constructor_count > 1) {
        for_each (i, vec_class_item, &my items) {
            if (i->tag == CLASS_ITEM_CONSTRUCTOR && i->constructor.func.has_body) {
                check(span_valid(i->constructor.name_span));
                check(span_valid(i->constructor.func.full_span));
                error_ctx_error_d(
                    errs,
                    span_between(i->constructor.name_span, i->constructor.func.full_span),
                    2392,
                    "Multiple constructor implementations are not allowed."
                );
            }
        }
    }

    for_each (i, vec_type, &my implements) {
        ignore i;
        // TODO: check that we do, in fact, implement each type
    }
}
