#include "class_decl.h"
#include "name_stack.h"
#include "interface_member.h"

void class_item_drop(ClassItem* me) {
    switch (my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_drop(&my constructor);
            break;

        case CLASS_ITEM_MEMBER:
            cstr_drop(&my member.name);
            function_impl_drop(&my member.func);
            break;

        case CLASS_ITEM_FIELD:
            name_init_data_drop(&my field);
            break;
    }
}

void class_decl_drop(ClassDecl* me) {
    cstr_drop(&my name);
    type_drop(&my interface);
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
                cstr_append_function_impl_debug(target, &i.ref->constructor, false, indent + 1);
                break;

            case CLASS_ITEM_MEMBER:
                cstr_append_sv(target, c_sv(CLR_NAME));
                cstr_append_s(target, i.ref->member.name);
                cstr_append_sv(target, c_sv(CLR0));
                cstr_append_function_impl_debug(target, &i.ref->member.func, false, indent + 1);
                break;

            case CLASS_ITEM_FIELD:
                cstr_append_name_init_data_debug(target, &i.ref->field, indent + 1);
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
                function_impl_resolve_names(&i.ref->constructor, names, errs);
                break;

            case CLASS_ITEM_MEMBER:
                name_stack_add_variable(names, cstr_clone(i.ref->member.name), &i.ref->member.func.type);
                function_impl_resolve_names(&i.ref->member.func, names, errs);
                break;

            case CLASS_ITEM_FIELD:
                name_stack_add_variable(names, cstr_clone(i.ref->field.name), &i.ref->field.type);
                type_resolve_names(&i.ref->field.type, names, errs);
                expr_resolve_names(&i.ref->field.value, names, errs);
                break;
        }
    }
    name_stack_pop_frame(names);
}

void class_decl_typecheck(ClassDecl* me, ErrorCtx* errs) {
    Interface interface = {0};
    c_foreach(i, vec_class_item, my items) {
        switch (i.ref->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                function_impl_typecheck(&i.ref->constructor, errs);
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
                if (i.ref->field.value_explicit) {
                    Type expr_type = expr_get_type_and_typecheck(&i.ref->field.value, errs);
                    if (i.ref->field.type_explicit) {
                        check_assignable_or_error(&expr_type, &i.ref->field.type, i.ref->field.name_span, errs);
                    } else {
                        type_drop(&i.ref->field.type);
                        i.ref->field.type = expr_type;
                        expr_type = type_any;
                        type_abstract_literals(&i.ref->field.type); // Perhaps not if read-only?
                    }
                    type_drop(&expr_type);
                }
                vec_interface_member_push(
                    &interface.members,
                    interface_member_make(
                        cstr_clone(i.ref->field.name),
                        type_clone(i.ref->field.type)
                    ));
                break;
        }
    }
    type_drop(&my interface);
    my interface = type_interface(interface);
}
