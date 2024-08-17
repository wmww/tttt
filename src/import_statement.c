#include "import_statement.h"
#include "name_stack.h"

void import_item_drop(ImportItem* me) {
    named_variable_destroy(my named_var);
    named_type_destroy(my named_type);
}

void import_statement_drop(ImportStatement* me) {
    vec_import_item_drop(&my items);
    cstr_drop(&my path);
    named_variable_destroy(my default_named_var);
    named_type_destroy(my default_named_type);
    named_variable_destroy(my module_named_var);
    named_type_destroy(my module_named_type);
}

void import_statement_add_names(ImportStatement* me, NameStack* names, ErrorCtx* errs) {
    __ errs;

    ASSERT(!my default_named_var);
    ASSERT(!my default_named_type);
    ASSERT(!my module_named_var);
    ASSERT(!my module_named_type);

    if (span_valid(&my default_name)) {
        my default_named_type = make_named_type(my default_name, my default_name, lazy_type_ref(&type_any), nullptr);
        name_stack_add_type(names, my default_named_type);
        if (!my type_only) {
            my default_named_var = make_named_variable(my default_name, my default_name, lazy_type_ref(&type_any), nullptr);
            name_stack_add_variable(names, my default_named_var);
        }
    }
    if (span_valid(&my module_name)) {
        my module_named_type = make_named_type(my module_name, my module_name, lazy_type_ref(&type_any), nullptr);
        name_stack_add_type(names, my module_named_type);
        if (!my type_only) {
            my module_named_var = make_named_variable(my module_name, my module_name, lazy_type_ref(&type_any), nullptr);
            name_stack_add_variable(names, my module_named_var);
        }
    }
    c_foreach(i, vec_import_item, my items) {
        ASSERT(!i.ref->named_var);
        ASSERT(!i.ref->named_type);
        i.ref->named_type = make_named_type(i.ref->name, i.ref->name, lazy_type_ref(&type_any), nullptr);
        name_stack_add_type(names, i.ref->named_type);
        if (!my type_only && !i.ref->type_only) {
            i.ref->named_var = make_named_variable(i.ref->name, i.ref->name, lazy_type_ref(&type_any), nullptr);
            name_stack_add_variable(names, i.ref->named_var);
        }
    }
}
