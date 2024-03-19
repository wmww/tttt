#include "class_decl.h"

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
