#include "class_decl.h"
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

Ty const* class_item_field_evaluate_type(ClassItem* me) {
    check(my tag == CLASS_ITEM_FIELD);
    Checker* checker = current_checker;
    if (my field.has_explicit_type) return ty_of_node(&my field.explicit_type);
    return ty_widened_for_declaration(checker, ty_expr_type(checker, &my field.value), !my field.is_readonly);
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

TypeNode class_item_get_type(ClassItem* me) {
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
    type_drop(&my instance_identifier_type);
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
            lazy_type_bind(&my method.accessor_type, LAZY_TYPE_CLASS_ITEM_ACCESSOR);
            my method.func.is_method = !my method.accessor;
            function_impl_lookup_names(&my method.func, false, names, errs);
            break;

        case CLASS_ITEM_FIELD:
            lazy_type_bind(&my field.lazy_type, LAZY_TYPE_CLASS_ITEM_FIELD);
            type_lookup_names(&my field.explicit_type, names, errs);
            expr_lookup_names(&my field.value, names, errs);
            break;

        case CLASS_ITEM_INTERFACE_MEMBER:
            interface_member_lookup_names(&my interface_memeber, names, errs);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            Scope* block_scope = scope_new(names, SCOPE_STATIC_BLOCK);
            block_add_names(&my static_block, block_scope);
            block_lookup_names(&my static_block, block_scope, errs);
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
        .node = { DECL_NODE_CLASS, me },
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
        .node = { DECL_NODE_CLASS, me },
    });
}

static void class_members_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs, bool static_members) {
    Scope* this_scope = scope_new(names, SCOPE_BLOCK);
    this_scope->this_declaration = static_members ? &my typeof_declaration : my declaration.get;

    for_each (i, vec_class_item, &my items) {
        if (class_item_is_static(i) == static_members) {
            class_item_lookup_names(i, this_scope, errs);
        }
    }
}

void class_decl_lookup_names(ClassDecl* me, Scope* names, ErrorCtx* errs) {
    lazy_type_bind(&my instance_type, LAZY_TYPE_CLASS_INSTANCE);
    lazy_type_bind(&my class_type, LAZY_TYPE_CLASS_CLASS);

    for_each (i, vec_expr, &my decorators) {
        expr_lookup_names(i, names, errs);
    }

    names = scope_new_generic(names, &my generic, errs);

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
}

// Classes are evaluated by the new type core (ty/members.c)
Ty const* class_decl_evaluate_type(ClassDecl* me, bool resolve_instance) {
    Checker* checker = current_checker;
    if (resolve_instance) {
        return my declaration.get ? ty_declared_type_of(checker, my declaration.get) : ty_intrinsic(checker, TY_ANY);
    }
    return ty_declared_type_of(checker, &my typeof_declaration);
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
}
