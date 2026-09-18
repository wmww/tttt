#include "class_decl.h"
#include "scope_stack.h"
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
            function_impl_drop(&my method.func);
            lazy_type_drop(&my method.accessor_type);
            break;

        case CLASS_ITEM_FIELD:
            cstr_drop(&my field.name_string);
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

Type class_item_field_evaluate_type(ClassItem* me) {
    check(my tag == CLASS_ITEM_FIELD);
    Type const* ref = my field.has_explicit_type ?
        &my field.explicit_type :
        lazy_type_get(&my field.value.lazy_type);
    Type ret = type_clone(*ref);
    if (!my field.is_readonly) {
        type_abstract_literals(&ret);
    }
    return ret;
}

Type class_item_accessor_evaluate_type(ClassItem* me) {
    check(my tag == CLASS_ITEM_METHOD && my method.accessor);
    if (my method.accessor == CLASS_ACCESSOR_SET && my method.accessor_getter) {
        return type_clone(*lazy_type_get(&my method.accessor_getter->method.accessor_type));
    }
    Type const* func = lazy_type_get(&my method.func.lazy_type);
    if (func->tag != TYPE_CALLABLE) {
        return type_any;
    } else if (my method.accessor == CLASS_ACCESSOR_GET) {
        Type ret = type_clone(func->callable->ret);
        type_abstract_literals(&ret);
        return ret;
    } else if (vec_callable_arg_size(&func->callable->args)) {
        return type_clone(vec_callable_arg_front(&func->callable->args)->type);
    } else {
        return type_any;
    }
}

Type class_item_get_type(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR: return type_lazy(&my constructor.func.lazy_type);
        case CLASS_ITEM_METHOD:
            return type_lazy(my method.accessor ? &my method.accessor_type : &my method.func.lazy_type);
        case CLASS_ITEM_FIELD: return type_lazy(&my field.lazy_type);
        case CLASS_ITEM_INTERFACE_MEMBER: return type_refcopy(&my interface_memeber.type);
        case CLASS_ITEM_STATIC_BLOCK: return type_never;
    }
    impossible;
}

void class_decl_drop(ClassDecl* me) {
    vec_expr_drop(&my decorators);
    generic_drop(&my generic);
    expr_drop(&my extends);
    vec_type_drop(&my implements);
    vec_class_item_drop(&my items);
    lazy_type_drop(&my instance_type);
    lazy_type_drop(&my class_type);
    declaration_drop(&my typeof_declaration);
    arc_declaration_drop(&my declaration);
    type_drop(&my instance_identifier_type);
    vec_type_drop(&my miscellaneous_owned_types);
}

void class_item_lookup_names(ClassItem* me, ScopeStack* names, ErrorCtx* errs) {
    for_each (i, vec_expr, &my decorators) {
        expr_lookup_names(i, names, errs);
    }
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_lookup_names(&my constructor.func, false, names, errs);
            break;

        case CLASS_ITEM_METHOD:
            my method.accessor_type.tag = LAZY_TYPE_CLASS_ITEM_ACCESSOR;
            function_impl_lookup_names(&my method.func, false, names, errs);
            break;

        case CLASS_ITEM_FIELD:
            my field.lazy_type.tag = LAZY_TYPE_CLASS_ITEM_FIELD;
            type_lookup_names(&my field.explicit_type, names, errs);
            expr_lookup_names(&my field.value, names, errs);
            break;

        case CLASS_ITEM_INTERFACE_MEMBER:
            interface_member_lookup_names(&my interface_memeber, names, errs);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            block_resolve_names(&my static_block, names, errs);
            break;
    }
}

void class_decl_add_names(ClassDecl* me, Scope* scope, bool exported) {
    declaration_init(&my typeof_declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .tag = DECLARATION_CLASS,
        .value_type = type_lazy(&my class_type),
        .declared_type = type_lazy(&my class_type),
        .is_typeof = true,
    });

    scope_init_declaration(scope, &my declaration, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported,
        .tag = DECLARATION_CLASS,
        .generic = &my generic,
        .value_type = type_identifier(c_new(IdentifierType, {
            .name = my name,
            .decl_ref = &my typeof_declaration,
        })),
        .declared_type = type_lazy(&my instance_type),
    });
}

static void class_members_lookup_names(ClassDecl* me, ScopeStack* names, ErrorCtx* errs, bool static_members) {
    Scope this_scope = scope_init(errs);
    this_scope.this_declaration = static_members ? &my typeof_declaration : my declaration.get;

    scope_push_to_stack(&this_scope, names);
    for_each (i, vec_class_item, &my items) {
        if (class_item_is_static(i) == static_members) {
            class_item_lookup_names(i, names, errs);
        }
    }

    scope_drop(&this_scope);
}

void class_decl_lookup_names(ClassDecl* me, ScopeStack* names, ErrorCtx* errs) {
    my instance_type.tag = LAZY_TYPE_CLASS_INSTANCE;
    my class_type.tag = LAZY_TYPE_CLASS_CLASS;

    for_each (i, vec_expr, &my decorators) {
        expr_lookup_names(i, names, errs);
    }

    Scope generic_scope = scope_init(errs);
    scope_push_generic_to_stack(&generic_scope, &my generic, names);

    VecSpannedType generic_params = vec_spanned_type_init();
    for_each (i, vec_generic_item, &my generic.items) {
        // Is it the fallback type, always any or something else? unclear.
        vec_spanned_type_push(&generic_params, spanned_type(i->name, type_identifier(c_new(IdentifierType, {
            .name = i->name,
            .decl_ref = i->declaration_ref,
        }))));
    }
    my instance_identifier_type = type_identifier(c_new(IdentifierType, {
        .name = my name,
        .decl_ref = my declaration.get,
        .generic_params = generic_params,
    }));

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
            i->constructor.func.ret_explicit = true;
            i->constructor.func.ret = type_ref_always(&my instance_identifier_type);
        }
    }

    for_each (i, vec_class_item, &my items) {
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

    scope_drop(&generic_scope);
}

Type class_decl_evaluate_type(ClassDecl* me, bool resolve_instance) {
    Interface interface = {0};

    if (!resolve_instance) {
        Type prototype_type = type_clone(my instance_identifier_type);
        VecSpannedType params = {0};
        prototype_type = generic_degenericize_type(&my generic, prototype_type, &params);
        vec_interface_member_push(&interface.members, interface_member_make(
            span_null,
            my declaration.get,
            true,
            false,
            cstr_lit("prototype"),
            span_null,
            false,
            prototype_type
        ));

        // TODO: Use the declaration in Object of lib.es5.d.ts instead
        // Or at least use a global instance of the type
        VecFunctionArg has_own_property_args = vec_function_arg_init();
        vec_function_arg_push(&has_own_property_args, (FunctionArg){
            .initializer.binding.has_explicit_type = true,
            .initializer.binding.explicit_type = type_string,
        });
        Type has_own_property_type = function_impl_into_type((FunctionImpl) {
            .args = has_own_property_args,
            .ret_explicit = true,
            .ret = type_boolean,
        });
        vec_interface_member_push(&interface.members, interface_member_make(
            span_null,
            nullptr,
            true,
            false,
            cstr_lit("hasOwnProperty"),
            span_null,
            false,
            type_clone(has_own_property_type)
        ));
        vec_type_push(&my miscellaneous_owned_types, has_own_property_type);
    }

    bool has_constructor = false;
    for_each (i, vec_class_item, &my items) {
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                if (!resolve_instance && !has_constructor) {
                    has_constructor = true;
                    Type type = type_clone(*lazy_type_get(&i->constructor.func.lazy_type));
                    check(type.tag == TYPE_CALLABLE);
                    generic_drop(&type.callable->generic);
                    type.callable->generic = generic_clone(my generic);
                    InterfaceMember member = {
                        .declarations = vec_span_single(i->constructor.func.full_span),
                        .origin_interface = my declaration.get,
                        .key.span = i->constructor.name_span,
                        .key.tag = INTERFACE_KEY_CALLABLE,
                        .type = type,
                    };
                    vec_interface_member_push(&interface.members, member);
                }
                break;

            case CLASS_ITEM_METHOD:
                if (i->method.is_static != resolve_instance && i->method.accessor && i->method.accessor_first != i) {
                    // The second half of an accessor pair, the first half already added the property
                } else if (i->method.is_static != resolve_instance) {
                    InterfaceMember member = interface_member_make(
                        i->span,
                        my declaration.get,
                        false,
                        i->method.is_optional,
                        cstr_clone(i->method.name_string),
                        i->method.name_span,
                        i->method.name_is_value,
                        class_item_get_type(i)
                    );
                    if (i->method.accessor) {
                        bool has_setter = false;
                        for_each (other, vec_class_item, &my items) {
                            if (other != i && other->tag == CLASS_ITEM_METHOD && other->method.accessor_first == i) {
                                vec_span_push(&member.declarations, other->span);
                            }
                            if (other->tag == CLASS_ITEM_METHOD &&
                                other->method.accessor_first == i &&
                                other->method.accessor == CLASS_ACCESSOR_SET
                            ) {
                                has_setter = true;
                            }
                        }
                        member.readonly = !has_setter;
                    }
                    vec_interface_member_push(&interface.members, member);
                }
                break;

            case CLASS_ITEM_FIELD:
                if (i->field.is_static != resolve_instance) {
                    InterfaceMember field = interface_member_make(
                        i->span,
                        my declaration.get,
                        i->field.is_readonly,
                        i->field.is_optional,
                        cstr_clone(i->field.name_string),
                        i->field.name_span,
                        i->field.name_is_value,
                        class_item_get_type(i)
                    );
                    vec_interface_member_push(&interface.members, field);
                }
                break;

            case CLASS_ITEM_INTERFACE_MEMBER:
                if (resolve_instance) {
                    vec_interface_member_push(&interface.members, interface_member_clone(i->interface_memeber));
                }
                break;

            case CLASS_ITEM_STATIC_BLOCK: break;
        }
    }

    if (!resolve_instance && !has_constructor) {
        Type constructor = function_impl_into_type((FunctionImpl){
            .ret = type_ref_always(&my instance_identifier_type),
            .is_constructor = true,
            .is_abstract_constructor = my is_abstract,
            .generic = generic_clone(my generic),
        });
        InterfaceMember member = {
            .origin_interface = my declaration.get,
            .key.tag = INTERFACE_KEY_CALLABLE,
            .type = type_clone(constructor),
        };
        vec_type_push(&my miscellaneous_owned_types, constructor);
        vec_interface_member_push(&interface.members, member);
    }

    return type_interface(interface);
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
        Type const* extends_type = expr_get_type(&my extends);
        if (extends_type->tag == TYPE_VALUE && extends_type->value.tag == VALUE_NULL) {
        } else {
            CallExpr call_expr = {
                .uses_new = true,
                .is_extends = true,
                .callable = expr_implicit_any(my extends.outer_span),
                .args = vec_expr_init(),
            };
            type_typecheck_call(extends_type, &call_expr, my extends.outer_span, errs);
        }
    }

    int constructor_count = 0;
    for_each (i, vec_class_item, &my items) {
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                function_impl_typecheck(&i->constructor.func, errs);
                constructor_count++;
                break;

            case CLASS_ITEM_METHOD:
                function_impl_typecheck(&i->method.func, errs);
                break;

            case CLASS_ITEM_FIELD: {
                expr_typecheck(&i->field.value, errs);
                if (i->field.has_explicit_type) {
                    type_error_if_not_assignable(
                        expr_get_type(&i->field.value),
                        &i->field.explicit_type,
                        i->field.name_span,
                        errs
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
            if (i->tag == CLASS_ITEM_CONSTRUCTOR) {
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

    interface_typecheck_members_against(&my instance_type.evaluated.interface, my declaration.get, errs);
}
