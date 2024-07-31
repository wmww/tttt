#include "class_decl.h"
#include "name_stack.h"
#include "statement.h"
#include "interface_member.h"
#include "callable_type.h"
#include "identifier_type.h"
#include "error.h"

void class_item_drop(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_drop(&my constructor.func);
            break;

        case CLASS_ITEM_MEMBER:
            cstr_drop(&my member.name_string);
            function_impl_drop(&my member.func);
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
        case CLASS_ITEM_MEMBER: return my member.is_static;
        case CLASS_ITEM_FIELD: return my field.is_static;
        case CLASS_ITEM_INTERFACE_MEMBER: return false;
        case CLASS_ITEM_STATIC_BLOCK: return true;
    }
    UNREACHABLE;
}

void class_decl_drop(ClassDecl* me) {
    generic_drop(&my generic);
    expr_drop(&my extends);
    vec_type_drop(&my implements);
    vec_class_item_drop(&my items);
    type_drop(&my instance_type);
    type_drop(&my class_type);
    named_type_destroy(my instance_named_type);
    named_type_destroy(my class_named_type);
    named_variable_destroy(my instance_named_var);
    named_variable_destroy(my class_named_var);
    type_drop(&my instance_identifier_type);
    type_drop(&my class_identifier_type);
}

void cstr_append_class_decl_debug(cstr* target, ClassDecl const* me, int indent) {
    if (my is_abstract) {
        cstr_append_sv(target, c_sv("abstract "));
    }
    cstr_append_sv(target, c_sv("class "));
    if (span_valid(&my name)) {
        cstr_append_sv(target, c_sv(CLR_NAME));
        cstr_append_span(target, &my name);
        cstr_append_sv(target, c_sv(CLR0));
    }
    cstr_append_generic_debug(target, &my generic, indent);
    if (span_valid(&my name) || !generic_empty(&my generic)) {
        cstr_append_sv(target, c_sv(CLR0 " "));
    }
    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        cstr_append_sv(target, c_sv("extends "));
        cstr_append_expr_debug(target, &my extends, indent);
        cstr_append_sv(target, c_sv(" "));
    }
    if (!vec_type_empty(&my implements)) {
        cstr_append_sv(target, c_sv("implements "));
        c_foreach(i, vec_type, my implements) {
            cstr_append_type_debug(target, i.ref, indent);
            cstr_append_sv(target, i.ref == i.end - 1 ? c_sv(" ") : c_sv(", "));
        }
    }
    cstr_append_sv(target, c_sv("{\n"));
    c_foreach(i, vec_class_item, my items) {
        APPEND_INDENT(target, indent + 1);
        switch (i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                cstr_append_sv(target, c_sv(CLR_NAME "constructor" CLR0));
                cstr_append_function_impl_debug(target, &i.ref->constructor.func, false, indent + 1);
                break;

            case CLASS_ITEM_MEMBER:
                if (i.ref->member.is_static) {
                    cstr_append_sv(target, c_sv("static "));
                }
                cstr_append_sv(target, c_sv(CLR_NAME));
                cstr_append_s(target, i.ref->member.name_string);
                cstr_append_sv(target, c_sv(CLR0));
                if (i.ref->member.is_optional) {
                    cstr_append_sv(target, c_sv("?"));
                }
                cstr_append_function_impl_debug(target, &i.ref->member.func, false, indent + 1);
                break;

            case CLASS_ITEM_FIELD:
                if (i.ref->field.is_static) {
                    cstr_append_sv(target, c_sv("static "));
                }
                if (i.ref->field.is_readonly) {
                    cstr_append_sv(target, c_sv("readonly "));
                }
                cstr_append_sv(target, c_sv(CLR_NAME));
                cstr_append_s(target, i.ref->field.name_string);
                cstr_append_sv(target, c_sv(CLR0));
                if (i.ref->field.is_optional) {
                    cstr_append_sv(target, c_sv("?"));
                }
                if (i.ref->field.has_explicit_type) {
                    cstr_append_sv(target, c_sv(": "));
                    cstr_append_type_debug(target, &i.ref->field.explicit_type, indent);
                }
                cstr_append_sv(target, c_sv(" = "));
                cstr_append_expr_debug(target, &i.ref->field.value, indent);
                break;

            case CLASS_ITEM_INTERFACE_MEMBER:
                cstr_append_interface_member_debug(target, &i.ref->interface_memeber, indent + 1);
                break;

            case CLASS_ITEM_STATIC_BLOCK:
                cstr_append_sv(target, c_sv("static "));
                cstr_append_block_debug(target, &i.ref->static_block, indent + 1);
                break;
        }
        cstr_append_sv(target, c_sv("\n"));
    }
    APPEND_INDENT(target, indent);
    cstr_append_sv(target, c_sv("}"));
}

void class_item_resolve_names(ClassItem* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_resolve_names(&my constructor.func, names, errs);
            break;

        case CLASS_ITEM_MEMBER:
            function_impl_resolve_names(&my member.func, names, errs);
            break;

        case CLASS_ITEM_FIELD:
            type_resolve_names(&my field.explicit_type, names, errs);
            expr_resolve_names(&my field.value, names, errs);
            break;

        case CLASS_ITEM_INTERFACE_MEMBER:
            interface_member_resolve_names(&my interface_memeber, names, errs);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            block_resolve_names(&my static_block, names, errs);
            break;
    }
}

void class_decl_add_names(ClassDecl* me, NameStack* names, ErrorCtx* errs) {
    ASSERT(!my instance_named_type);
    my instance_named_type = make_named_type(my full_span, lazy_type_class_instance(me), &my generic);
    my instance_named_type->is_class = true;
    if (!name_stack_add_type_s(names, cstr_from_span(&my name), my instance_named_type)) {
        // name is cstr_str(&result.ref->first)
        error_ctx_type_error(errs, "multiple values declared with same name");
    }

    ASSERT(!my instance_named_var);
    VecSpannedType generic_params = {0};
    c_foreach(i, vec_generic_item, my generic.items) {
        // Is it the fallback type, always any or something else? unclear.
        vec_spanned_type_push(&generic_params, spanned_type(i.ref->name, type_clone(i.ref->fallback)));
    }
    my instance_named_var = make_named_variable(
        my full_span,
        lazy_type_value(type_identifier(c_new(IdentifierType, {
            .name = my name,
            .type = my instance_named_type,
            .generic_params = generic_params,
        }))),
        nullptr
    );

    ASSERT(my instance_identifier_type.tag == TYPE_ANY); // Should only be set here, and only once
    generic_params = (VecSpannedType){0};
    c_foreach(i, vec_generic_item, my generic.items) {
        // Is it the fallback type, always any or something else? unclear.
        vec_spanned_type_push(&generic_params, spanned_type(i.ref->name, type_clone(i.ref->fallback)));
    }
    my instance_identifier_type = type_identifier(c_new(IdentifierType, {
        .name = my name,
        .type = my instance_named_type,
        .generic_params = generic_params,
    }));

    ASSERT(!my class_named_type);
    my class_named_type = make_named_type(my name, lazy_type_class_class(me), nullptr);
    my class_named_type->is_typeof_class = true;

    ASSERT(!my class_named_var);
    my class_named_var = make_named_variable(
        my full_span,
        lazy_type_value(type_identifier(c_new(IdentifierType, {
            .name = my name,
            .type = my class_named_type,
        }))),
        &my generic
    );
    if (!name_stack_add_variable_s(names, cstr_from_span(&my name), my class_named_var)) {
        // name is cstr_str(&result.ref->first)
        error_ctx_type_error(errs, "multiple values declared with same name");
    }

    ASSERT(my class_identifier_type.tag == TYPE_ANY); // Should only be set here, and only once
    my class_identifier_type = type_identifier(c_new(IdentifierType, {
        .name = my name,
        .type = my class_named_type,
    }));

    c_foreach(i, vec_class_item, my items) {
        if (i.ref->tag == CLASS_ITEM_CONSTRUCTOR) {
            if (i.ref->constructor.func.callable.ret_explicit) {
                error_ctx_error(
                    errs,
                    i.ref->constructor.func.callable.ret_span,
                    1093,
                    "Type annotation cannot appear on a constructor declaration."
                );
                type_drop(&i.ref->constructor.func.callable.ret);
            }
            ASSERT(i.ref->constructor.func.callable.ret.tag == TYPE_ANY);
            i.ref->constructor.func.ret_resolve_state = RESOLVE_STATE_DONE;
            i.ref->constructor.func.callable.ret = type_clone(my instance_identifier_type);
        }
    }
}

void class_decl_resolve_names(ClassDecl* me, NameStack* names, ErrorCtx* errs) {
    name_stack_push_generic_frame(names, &my generic, errs);

    expr_resolve_names(&my extends, names, errs);
    c_foreach(i, vec_type, my implements) {
        type_resolve_names(i.ref, names, errs);
    }

    name_stack_push_block_frame(names);
    name_stack_add_variable_s(names, cstr_lit("this"), my class_named_var);
    c_foreach(i, vec_class_item, my items) {
        if (class_item_is_static(i.ref)) {
            class_item_resolve_names(i.ref, names, errs);
        }
    }
    name_stack_pop_frame(names); // frame containing "this"

    name_stack_push_block_frame(names);
    name_stack_add_variable_s(names, cstr_lit("this"), my instance_named_var);
    c_foreach(i, vec_class_item, my items) {
        if (!class_item_is_static(i.ref)) {
            class_item_resolve_names(i.ref, names, errs);
        }
    }
    name_stack_pop_frame(names); // frame containing "this"

    name_stack_pop_frame(names); // generics
}

Type const* class_decl_resolve_type(ClassDecl* me, bool resolve_instance) {
    Interface interface = {0};

    ResolveState* const resolve_state = resolve_instance ? &my instance_resolve_state : &my class_resolve_state;
    Type* const type = resolve_instance ? &my instance_type : &my class_type;

    switch (*resolve_state) {
        case RESOLVE_STATE_UNINITIALIZED:
            break;

        case RESOLVE_STATE_IN_FLIGHT:
            *type = type_self_referential_error;
            *resolve_state = RESOLVE_STATE_DONE;
            break;

        case RESOLVE_STATE_DONE:
            return type;
    }

    *resolve_state = RESOLVE_STATE_IN_FLIGHT;

    if (!resolve_instance) {
        vec_interface_member_push(&interface.members, interface_member_make(
            span_null,
            true,
            false,
            cstr_lit("prototype"),
            span_null,
            false,
            lazy_type_ref(&my instance_identifier_type)
        ));

        // TODO: Use the declaration in Object of lib.es5.d.ts instead
        // Or at least use a global instance of the type
        vec_interface_member_push(&interface.members, interface_member_make(
            span_null,
            true,
            false,
            cstr_lit("hasOwnProperty"),
            span_null,
            false,
            lazy_type_value(type_callable(c_new(CallableType, {
                .args = c_init(vec_callable_arg, {{.binding = {
                    .tag = BINDING_IDENTIFIER,
                    .has_explicit_type = true,
                    .explicit_type = type_string,
                }}}),
                .ret_explicit = true,
                .ret = type_boolean,
            })))
        ));
    }

    bool has_constructor = false;
    c_foreach(i, vec_class_item, my items) {
        switch (i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                if (!resolve_instance && !has_constructor) {
                    has_constructor = true;
                    InterfaceMember member = {
                        .span = i.ref->constructor.func.full_span,
                        .key.span = i.ref->constructor.name_span,
                        .key.tag = INTERFACE_KEY_CALLABLE,
                        .type = lazy_type_function(&i.ref->constructor.func),
                    };
                    vec_interface_member_push(&interface.members, member);
                }
                break;

            case CLASS_ITEM_MEMBER:
                if (i.ref->member.is_static != resolve_instance) {
                    InterfaceMember member = interface_member_make(
                        i.ref->span,
                        false,
                        i.ref->member.is_optional,
                        cstr_clone(i.ref->member.name_string),
                        i.ref->member.name_span,
                        i.ref->member.name_is_value,
                        lazy_type_function(&i.ref->member.func)
                    );
                    vec_interface_member_push(&interface.members, member);
                }
                break;

            case CLASS_ITEM_FIELD:
                if (i.ref->field.is_static != resolve_instance) {
                    LazyType type = i.ref->field.has_explicit_type ?
                        lazy_type_ref(&i.ref->field.explicit_type) :
                        lazy_type_expr(&i.ref->field.value, true, true);
                    InterfaceMember field = interface_member_make(
                        i.ref->span,
                        i.ref->field.is_readonly,
                        i.ref->field.is_optional,
                        cstr_clone(i.ref->field.name_string),
                        i.ref->field.name_span,
                        i.ref->field.name_is_value,
                        type
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
        CallableType constructor = {
            .ret = type_clone(my instance_identifier_type),
            .is_constructor = true,
            .is_abstract_constructor = my is_abstract,
        };
        InterfaceMember member = {
            .key.tag = INTERFACE_KEY_CALLABLE,
            .type = lazy_type_value(type_callable(c_new(CallableType, constructor))),
        };
        vec_interface_member_push(&interface.members, member);
    }

    if (*resolve_state == RESOLVE_STATE_IN_FLIGHT) {
        *resolve_state = RESOLVE_STATE_DONE;
        *type = type_interface(interface);
    } else {
        interface_drop(&interface);
    }
    return type;
}

void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs) {
    if (my has_typechecked) {
        return;
    }
    my has_typechecked = true;

    generic_typecheck(&my generic, errs);
    type_typecheck(class_decl_resolve_type(me, true), errs);
    type_typecheck(class_decl_resolve_type(me, false), errs);

    if (my extends.tag != EXPR_IMPLICIT_ANY) {
        Type extends_type = expr_get_type_and_typecheck(&my extends, errs);
        if (extends_type.tag == TYPE_VALUE && extends_type.value.tag == VALUE_NULL) {
        } else {
            CallExpr call_expr = {
                .uses_new = true,
                .is_extends = true,
                .callable = expr_implicit_any(my extends.outer_span),
                .args = vec_expr_init(),
            };
            Type extends_result = type_typecheck_call(&extends_type, &call_expr, my extends.outer_span, errs, 0);
            type_drop(&extends_result);
        }
        type_drop(&extends_type);
    }

    int constructor_count = 0;
    c_foreach(i, vec_class_item, my items) {
        switch (i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                function_impl_typecheck(&i.ref->constructor.func, errs);
                constructor_count++;
                break;

            case CLASS_ITEM_MEMBER:
                function_impl_typecheck(&i.ref->member.func, errs);
                break;

            case CLASS_ITEM_FIELD: {
                Type value = expr_get_type_and_typecheck(&i.ref->field.value, errs);
                if (i.ref->field.has_explicit_type) {
                    check_assignable_or_error(&value, &i.ref->field.explicit_type, i.ref->field.name_span, errs);
                }
                type_drop(&value);
            }   break;

            case CLASS_ITEM_INTERFACE_MEMBER: break;

            case CLASS_ITEM_STATIC_BLOCK: {
                StatementCtx ctx = statement_ctx_toplevel();
                block_typecheck(&i.ref->static_block, &ctx, errs);
                statement_ctx_drop(&ctx);
            }   break;
        }
    }

    if (constructor_count > 1) {
        c_foreach(i, vec_class_item, my items) {
            if (i.ref->tag == CLASS_ITEM_CONSTRUCTOR) {
                ASSERT(span_valid(&i.ref->constructor.name_span));
                ASSERT(span_valid(&i.ref->constructor.func.full_span));
                error_ctx_error(
                    errs,
                    span_between(&i.ref->constructor.name_span, &i.ref->constructor.func.full_span),
                    2392,
                    "Multiple constructor implementations are not allowed."
                );
            }
        }
    }

    c_foreach(i, vec_type, my implements) {
        // TODO: check that we do, in fact, implement each type
    }
}
