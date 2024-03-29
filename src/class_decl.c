#include "class_decl.h"
#include "name_stack.h"
#include "interface_member.h"
#include "callable_type.h"
#include "error.h"

void class_item_drop(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_drop(&my constructor.func);
            break;

        case CLASS_ITEM_MEMBER:
            cstr_drop(&my member.name);
            function_impl_drop(&my member.func);
            break;

        case CLASS_ITEM_FIELD:
            name_init_data_drop(&my field.data);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            block_drop(&my static_block);
            break;
    }
}

void class_decl_drop(ClassDecl* me) {
    cstr_drop(&my name);
    type_drop(&my interface);
    type_drop(&my constructor_type);
    vec_class_item_drop(&my items);
}

void cstr_append_class_decl_debug(cstr* target, ClassDecl const* me, int indent) {
    cstr_append_sv(target, c_sv("class "));
    if (!cstr_empty(&my name)) {
        cstr_append_sv(target, c_sv(CLR_NAME));
        cstr_append_s(target, my name);
        cstr_append_sv(target, c_sv(CLR0 " "));
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
                cstr_append_sv(target, c_sv(CLR_NAME));
                cstr_append_s(target, i.ref->member.name);
                cstr_append_sv(target, c_sv(CLR0));
                cstr_append_function_impl_debug(target, &i.ref->member.func, false, indent + 1);
                break;

            case CLASS_ITEM_FIELD:
                cstr_append_name_init_data_debug(target, &i.ref->field.data, indent + 1);
                break;

            case CLASS_ITEM_STATIC_BLOCK:
                cstr_append_sv(target, c_sv("static "));
                cstr_append_block_debug(target, &i.ref->static_block, indent + 1);
                break;
        }
        cstr_append_sv(target, i.ref == i.end - 1 ? c_sv("\n") : c_sv("\n\n"));
    }
    APPEND_INDENT(target, indent);
    cstr_append_sv(target, c_sv("}"));
}

void class_decl_resolve_names(ClassDecl* me, NameStack* names, ErrorCtx* errs) {
    name_stack_push_frame(names, STACK_FRAME_CLASS);
    c_foreach(i, vec_class_item, my items) {
        switch (i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                break;

            case CLASS_ITEM_MEMBER:
                if (!name_stack_add_variable(
                    names,
                    cstr_clone(i.ref->member.name),
                    i.ref->member.name_span,
                    &i.ref->member.func.type
                )) {
                    /*
                    error_ctx_error(
                        errs,
                        i.ref->member.name_span,
                        TS_CODE_NONE,
                        "multiple class members/fields with same name"
                    );
                    */
                }
                break;

            case CLASS_ITEM_FIELD:
                if (!name_stack_add_variable(
                    names,
                    cstr_clone(i.ref->field.data.name),
                    i.ref->field.data.name_span,
                    &i.ref->field.data.type
                )) {
                    /*
                    error_ctx_error(
                        errs,
                        i.ref->field.name_span,
                        TS_CODE_NONE,
                        "multiple class members/fields with same name"
                    );
                    */
                }
                type_resolve_names(&i.ref->field.data.type, names, errs);
                expr_resolve_names(&i.ref->field.data.value, names, errs);
                break;

            case CLASS_ITEM_STATIC_BLOCK:
                block_resolve_names(&i.ref->static_block, names, errs);
                break;
        }
    }
    name_stack_push_frame(names, STACK_FRAME_CLASS);
    name_stack_add_variable(names, cstr_lit("this"), span_null, &my interface);
    c_foreach(i, vec_class_item, my items) {
        switch (i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                function_impl_resolve_names(&i.ref->constructor.func, names, errs);
                break;

            case CLASS_ITEM_MEMBER:
                function_impl_resolve_names(&i.ref->member.func, names, errs);
                break;

            case CLASS_ITEM_FIELD: break;
            case CLASS_ITEM_STATIC_BLOCK: break;
        }
    }
    name_stack_pop_frame(names);
    name_stack_pop_frame(names);
}

void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs) {
    Interface interface = {0};

    int constructor_count = 0;
    c_foreach(i, vec_class_item, my items) {
        switch (i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                function_impl_typecheck(&i.ref->constructor.func, errs);
                if (constructor_count == 0) {
                    type_drop(&my constructor_type);
                    my constructor_type = type_clone(i.ref->constructor.func.type);
                    CallableType* callable = type_get_callable_type(&my constructor_type);
                    if (callable) {
                        type_drop(&callable->ret);
                        callable->ret = type_clone(my interface);
                        callable->ret_explicit = true;
                    }
                }
                constructor_count++;
                break;

            case CLASS_ITEM_MEMBER:
                function_impl_typecheck(&i.ref->member.func, errs);
                vec_interface_member_push(
                    &interface.members,
                    interface_member_make(
                        cstr_clone(i.ref->member.name),
                        type_clone(i.ref->member.func.type)
                    ));
                break;

            case CLASS_ITEM_FIELD:
                if (i.ref->field.data.value_explicit) {
                    Type expr_type = expr_get_type_and_typecheck(&i.ref->field.data.value, errs);
                    if (i.ref->field.data.type_explicit) {
                        check_assignable_or_error(&expr_type, &i.ref->field.data.type, i.ref->field.data.name_span, errs);
                    } else {
                        type_drop(&i.ref->field.data.type);
                        i.ref->field.data.type = expr_type;
                        expr_type = type_any;
                        type_abstract_literals(&i.ref->field.data.type); // Perhaps not if read-only?
                    }
                    type_drop(&expr_type);
                }
                vec_interface_member_push(
                    &interface.members,
                    interface_member_make(
                        cstr_clone(i.ref->field.data.name),
                        type_clone(i.ref->field.data.type)
                    ));
                break;

            case CLASS_ITEM_STATIC_BLOCK: {
                StatementCtx ctx = statement_ctx_toplevel();
                block_typecheck(&i.ref->static_block, &ctx, errs);
                statement_ctx_drop(&ctx);
            }   break;
        }
    }

    type_drop(&my interface);
    my interface = type_interface(interface);

    if (constructor_count == 0) {
        type_drop(&my constructor_type);
        my constructor_type = type_callable(c_new(CallableType, {
            .args = vec_callable_type_arg_init(),
            .ret_explicit = true,
            .ret = type_clone(my interface),
        }));
    } else if (constructor_count > 1) {
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
}
