#include "class_decl.h"
#include "name_stack.h"
#include "statement.h"
#include "interface_member.h"
#include "callable_type.h"
#include "several_types.h"
#include "error.h"

void class_item_drop(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_drop(&my constructor.func);
            break;

        case CLASS_ITEM_METHOD:
            cstr_drop(&my method.name_string);
            function_impl_drop(&my method.func);
            break;

        case CLASS_ITEM_FIELD:
            cstr_drop(&my field.name_string);
            type_drop(&my field.explicit_type);
            expr_drop(&my field.value);
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
    UNREACHABLE;
}

Type class_item_get_type(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR: return type_lazy(&my constructor.func.lazy_type);
        case CLASS_ITEM_METHOD: return type_lazy(&my method.func.lazy_type);
        case CLASS_ITEM_FIELD:
            return make_type_abstracted(
                my field.has_explicit_type ?
                    type_ref(&my field.explicit_type) :
                    type_lazy(&my field.value.lazy_type),
                true /* maybe should be !my field.is_readonly ? */
            );
        case CLASS_ITEM_INTERFACE_MEMBER: return type_ref(&my interface_memeber.type);
        case CLASS_ITEM_STATIC_BLOCK: return type_never;
    }
    UNREACHABLE;
}

void class_decl_drop(ClassDecl* me) {
    generic_drop(&my generic);
    expr_drop(&my extends);
    vec_type_drop(&my implements);
    vec_class_item_drop(&my items);
    lazy_type_drop(&my instance_type);
    lazy_type_drop(&my class_type);
    named_type_destroy(my instance_named_type);
    named_type_destroy(my class_named_type);
    named_variable_destroy(my instance_named_var);
    named_variable_destroy(my class_named_var);
    type_drop(&my instance_identifier_type);
    type_drop(&my class_identifier_type);
}

void class_item_lookup_names(ClassItem* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_lookup_names(&my constructor.func, names, errs);
            break;

        case CLASS_ITEM_METHOD:
            function_impl_lookup_names(&my method.func, names, errs);
            break;

        case CLASS_ITEM_FIELD:
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

void class_decl_add_names(ClassDecl* me, Scope* scope) {
    ASSERT(!my instance_named_type);
    my instance_named_type = scope_add_mergeable_type(
        scope,
        my name,
        my full_span,
        type_lazy(&my instance_type),
        &my generic,
        true
    );

    ASSERT(!my class_named_var);
    my class_named_var = make_named_variable(my name, my full_span, type_ref(&my class_identifier_type), &my generic);
    scope_add_variable(scope, my class_named_var);
}

void class_decl_lookup_names(ClassDecl* me, NameStack* names, ErrorCtx* errs) {
    my instance_type.tag = LAZY_TYPE_CLASS_INSTANCE;
    my class_type.tag = LAZY_TYPE_CLASS_CLASS;

    Scope generic_scope = scope_init(errs);
    scope_push_generic_to_stack(&generic_scope, &my generic, names);

    VecSpannedType generic_params; // Used multiple times
    ASSERT(my instance_identifier_type.tag == TYPE_ANY); // Should only be set here, and only once
    generic_params = (VecSpannedType){0};
    c_foreach(i, vec_generic_item, my generic.items) {
        // Is it the fallback type, always any or something else? unclear.
        vec_spanned_type_push(&generic_params, spanned_type(i.ref->name, type_identifier(c_new(IdentifierType, {
            .name = i.ref->name,
            .named_type = i.ref->named_type,
        }))));
    }
    my instance_identifier_type = type_identifier(c_new(IdentifierType, {
        .name = my name,
        .named_type = my instance_named_type,
        .generic_params = generic_params,
    }));

    /*
     * TODO: this supposed to be used somewhere?
    generic_params = (VecSpannedType){0};
    c_foreach(i, vec_generic_item, my generic.items) {
        // Is it the fallback type, always any or something else? unclear.
        vec_spanned_type_push(&generic_params, spanned_type(i.ref->name, type_clone(i.ref->fallback)));
    }
    */

    ASSERT(!my instance_named_var);
    my instance_named_var = make_named_variable(my name, my full_span, type_ref(&my instance_identifier_type), nullptr);

    ASSERT(!my class_named_type);
    my class_named_type = make_named_type(my name, my full_span, type_lazy(&my class_type), nullptr);
    my class_named_type->is_typeof_class = true;

    ASSERT(my class_identifier_type.tag == TYPE_ANY); // Should only be set here, and only once
    my class_identifier_type = type_identifier(c_new(IdentifierType, {
        .name = my name,
        .named_type = my class_named_type,
    }));

    c_foreach(i, vec_class_item, my items) {
        if (i.ref->tag == CLASS_ITEM_CONSTRUCTOR) {
            if (i.ref->constructor.func.ret_explicit) {
                error_ctx_error(
                    errs,
                    i.ref->constructor.func.ret_span,
                    1093,
                    "Type annotation cannot appear on a constructor declaration."
                );
                type_drop(&i.ref->constructor.func.ret);
            }
            ASSERT(i.ref->constructor.func.ret.tag == TYPE_ANY);
            i.ref->constructor.func.ret_explicit = true;
            i.ref->constructor.func.ret = type_ref(&my instance_identifier_type);
        }
    }

    expr_lookup_names(&my extends, names, errs);
    c_foreach(i, vec_type, my implements) {
        type_lookup_names(i.ref, names, errs);
    }

    Scope class_this_scope = scope_init(errs);
    scope_add_variable_s(&class_this_scope, cstr_lit("this"), my class_named_var);
    scope_add_type_s(&class_this_scope, cstr_lit("this"), my class_named_type);
    scope_push_to_stack(&class_this_scope, names);
    c_foreach(i, vec_class_item, my items) {
        if (class_item_is_static(i.ref)) {
            class_item_lookup_names(i.ref, names, errs);
        }
    }
    scope_drop(&class_this_scope);

    Scope instance_this_scope = scope_init(errs);
    scope_add_variable_s(&instance_this_scope, cstr_lit("this"), my instance_named_var);
    scope_add_type_s(&instance_this_scope, cstr_lit("this"), my instance_named_type);
    scope_push_to_stack(&instance_this_scope, names);
    c_foreach(i, vec_class_item, my items) {
        if (!class_item_is_static(i.ref)) {
            class_item_lookup_names(i.ref, names, errs);
        }
    }
    scope_drop(&instance_this_scope);

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
            my name,
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
            span_null,
            true,
            false,
            cstr_lit("hasOwnProperty"),
            span_null,
            false,
            has_own_property_type
        ));
    }

    bool has_constructor = false;
    c_foreach(i, vec_class_item, my items) {
        switch (i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                if (!resolve_instance && !has_constructor) {
                    has_constructor = true;
                    Type type = type_clone(*lazy_type_get(&i.ref->constructor.func.lazy_type));
                    ASSERT(type.tag == TYPE_CALLABLE);
                    generic_drop(&type.callable->generic);
                    type.callable->generic = generic_clone(my generic);
                    InterfaceMember member = {
                        .declarations = vec_span_single(i.ref->constructor.func.full_span),
                        .origin_interface_name = my name,
                        .key.span = i.ref->constructor.name_span,
                        .key.tag = INTERFACE_KEY_CALLABLE,
                        .type = type,
                    };
                    vec_interface_member_push(&interface.members, member);
                }
                break;

            case CLASS_ITEM_METHOD:
                if (i.ref->method.is_static != resolve_instance) {
                    InterfaceMember member = interface_member_make(
                        i.ref->span,
                        my name,
                        false,
                        i.ref->method.is_optional,
                        cstr_clone(i.ref->method.name_string),
                        i.ref->method.name_span,
                        i.ref->method.name_is_value,
                        class_item_get_type(i.ref)
                    );
                    vec_interface_member_push(&interface.members, member);
                }
                break;

            case CLASS_ITEM_FIELD:
                if (i.ref->field.is_static != resolve_instance) {
                    InterfaceMember field = interface_member_make(
                        i.ref->span,
                        my name,
                        i.ref->field.is_readonly,
                        i.ref->field.is_optional,
                        cstr_clone(i.ref->field.name_string),
                        i.ref->field.name_span,
                        i.ref->field.name_is_value,
                        class_item_get_type(i.ref)
                    );
                    vec_interface_member_push(&interface.members, field);
                }
                break;

            case CLASS_ITEM_INTERFACE_MEMBER:
                if (resolve_instance) {
                    vec_interface_member_push(&interface.members, interface_member_clone(i.ref->interface_memeber));
                }
                break;

            case CLASS_ITEM_STATIC_BLOCK: break;
        }
    }

    if (!resolve_instance && !has_constructor) {
        FunctionImpl constructor = {
            .ret = type_ref(&my instance_identifier_type),
            .is_constructor = true,
            .is_abstract_constructor = my is_abstract,
            .generic = generic_clone(my generic),
        };
        InterfaceMember member = {
            .origin_interface_name = my name,
            .key.tag = INTERFACE_KEY_CALLABLE,
            .type = function_impl_into_type(constructor),
        };
        vec_interface_member_push(&interface.members, member);
    }

    return type_interface(interface);
}

void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs) {
    if (my has_typechecked) {
        return;
    }
    my has_typechecked = true;

    generic_typecheck(&my generic, errs);
    type_typecheck(lazy_type_get(&my instance_type), errs);
    type_typecheck(lazy_type_get(&my class_type), errs);

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
    c_foreach(i, vec_class_item, my items) {
        switch (i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                function_impl_typecheck(&i.ref->constructor.func, errs);
                constructor_count++;
                break;

            case CLASS_ITEM_METHOD:
                function_impl_typecheck(&i.ref->method.func, errs);
                break;

            case CLASS_ITEM_FIELD: {
                expr_typecheck(&i.ref->field.value, errs);
                if (i.ref->field.has_explicit_type) {
                    type_error_if_not_assignable(
                        expr_get_type(&i.ref->field.value),
                        &i.ref->field.explicit_type,
                        i.ref->field.name_span,
                        errs
                    );
                }
            }   break;

            case CLASS_ITEM_INTERFACE_MEMBER: break;

            case CLASS_ITEM_STATIC_BLOCK:
                block_typecheck(&i.ref->static_block, (BlockTypecheckCtx){0}, errs);
                break;
        }
    }

    if (constructor_count > 1) {
        c_foreach(i, vec_class_item, my items) {
            if (i.ref->tag == CLASS_ITEM_CONSTRUCTOR) {
                ASSERT(span_valid(i.ref->constructor.name_span));
                ASSERT(span_valid(i.ref->constructor.func.full_span));
                error_ctx_error(
                    errs,
                    span_between(i.ref->constructor.name_span, i.ref->constructor.func.full_span),
                    2392,
                    "Multiple constructor implementations are not allowed."
                );
            }
        }
    }

    c_foreach(i, vec_type, my implements) {
        // TODO: check that we do, in fact, implement each type
    }

    ASSERT(my instance_type.tag == LAZY_TYPE_EVALUATED && my instance_type.evaluated.tag == TYPE_INTERFACE);
    interface_typecheck_members_against(&my instance_type.evaluated.interface, my instance_named_type, errs);
}
