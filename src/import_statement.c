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

void cstr_append_import_statement_debug(cstr* target, ImportStatement* me, int indent) {
    (void)indent;
    cstr_append_sv(target, c_sv("import "));
    if (my type_only) {
        cstr_append_sv(target, c_sv("type "));
    }
    bool force_brace_list = true;
    if (span_valid(&my default_name)) {
        cstr_append_sv(target, c_sv(CLR_NAME));
        cstr_append_sv(target, span_sv(&my default_name));
        cstr_append_sv(target, c_sv(CLR0));
        force_brace_list = false;
    }
    if (span_valid(&my module_name)) {
        cstr_append_sv(target, c_sv(CLR_VALUE "*" CLR0 " as " CLR_NAME));
        cstr_append_sv(target, span_sv(&my module_name));
        cstr_append_sv(target, c_sv(CLR0));
        force_brace_list = false;
    }
    if (!force_brace_list) {
        cstr_append_sv(target, c_sv(", "));
    }
    if (force_brace_list || !vec_import_item_empty(&my items)) {
        cstr_append_sv(target, c_sv("{"));
        c_foreach(i, vec_import_item, my items) {
            if (i.ref->type_only) {
                cstr_append_sv(target, c_sv("type "));
            }
            cstr_append_sv(target, c_sv(CLR_NAME));
            cstr_append_sv(target, span_sv(&i.ref->source));
            if (span_cmp(&i.ref->name, &i.ref->source) != 0) {
                cstr_append_sv(target, c_sv(CLR0 " as " CLR_NAME));
                cstr_append_sv(target, span_sv(&i.ref->name));
            }
            cstr_append_sv(target, c_sv(CLR0));
            if (i.ref != i.end - 1) {
                cstr_append_sv(target, c_sv(", "));
            }
        }
        cstr_append_sv(target, c_sv("}"));
    }
    cstr_append_sv(target, c_sv(" from " CLR_STR));
    cstr_append_s(target, my path);
    cstr_append_sv(target, c_sv(CLR0 ";"));
}

void import_statement_add_names(ImportStatement* me, NameStack* names, ErrorCtx* errs) {
    (void)errs;
    ASSERT(!my default_named_var);
    ASSERT(!my default_named_type);
    ASSERT(!my module_named_var);
    ASSERT(!my module_named_type);
    if (span_valid(&my default_name)) {
        my default_named_type = make_named_type(my default_name, lazy_type_ref(&type_any), nullptr);
        name_stack_add_type(names, my default_named_type);
        if (!my type_only) {
            my default_named_var = make_named_variable(my default_name, lazy_type_ref(&type_any), nullptr);
            name_stack_add_variable(names, my default_named_var);
        }
    }
    if (span_valid(&my module_name)) {
        my module_named_type = make_named_type(my module_name, lazy_type_ref(&type_any), nullptr);
        name_stack_add_type(names, my module_named_type);
        if (!my type_only) {
            my module_named_var = make_named_variable(my module_name, lazy_type_ref(&type_any), nullptr);
            name_stack_add_variable(names, my module_named_var);
        }
    }
    c_foreach(i, vec_import_item, my items) {
        ASSERT(!i.ref->named_var);
        ASSERT(!i.ref->named_type);
        i.ref->named_type = make_named_type(i.ref->name, lazy_type_ref(&type_any), nullptr);
        name_stack_add_type(names, i.ref->named_type);
        if (!my type_only && !i.ref->type_only) {
            i.ref->named_var = make_named_variable(i.ref->name, lazy_type_ref(&type_any), nullptr);
            name_stack_add_variable(names, i.ref->named_var);
        }
    }
}
