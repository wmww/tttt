#include "import_statement.h"
#include "name_stack.h"
#include "program.h"
#include "source_file.h"
#include "error.h"

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
    ignore errs;

    ASSERT(!my default_named_var);
    ASSERT(!my default_named_type);
    ASSERT(!my module_named_var);
    ASSERT(!my module_named_type);

    if (!my source_file) {
        error_ctx_error(
            errs,
            my import_keyword_span,
            1232,
            "An import declaration can only be used at the top level of a namespace or module."
        );
    }

    if (span_valid(my default_name)) {
        my default_named_type = make_named_type(my default_name, my default_name, type_any, nullptr);
        name_stack_add_type(names, my default_named_type);
        if (!my type_only) {
            my default_named_var = make_named_variable(my default_name, my default_name, type_any, nullptr);
            name_stack_add_variable(names, my default_named_var);
        }
    }

    if (span_valid(my module_name)) {
        if (my source_file) {
            name_stack_add_namespace(
                names,
                my module_name,
                my source_file->name_stack
            );
        }
        if (!my type_only) {
            Type value = type_any;
            if (my source_file) {
                value = name_stack_make_interface_from(my source_file->name_stack);
            }
            my module_named_var = make_named_variable(
                my module_name,
                my module_name,
                value,
                nullptr
            );
            name_stack_add_variable(names, my module_named_var);
        }
    }

    c_foreach(i, vec_import_item, my items) {
        ASSERT(!i.ref->named_var);
        ASSERT(!i.ref->named_type);
        i.ref->named_type = make_named_type(i.ref->name, i.ref->name, type_any, nullptr);
        name_stack_add_type(names, i.ref->named_type);
        if (!my type_only && !i.ref->type_only) {
            i.ref->named_var = make_named_variable(i.ref->name, i.ref->name, type_any, nullptr);
            name_stack_add_variable(names, i.ref->named_var);
        }
    }
}
