#include "type.h"
#include "lazy_type.h"
#include "interface.h"
#include "interface_member.h"
#include "several_types.h"
#include "callable_type.h"
#include "function_impl.h"
#include "generic.h"
#include "value.h"
#include "scope.h"
#include "display.h"
#include "error.h"
#include "math_helpers.h"
#include "ty/ty.h"

const TypeNode type_any = { .tag = TYPE_ANY };
const TypeNode type_self_referential_error = { .tag = TYPE_ANY, .any.self_referential_error = true };
const TypeNode type_unknown = { .tag = TYPE_UNKNOWN };
const TypeNode type_never = { .tag = TYPE_NEVER };
const TypeNode type_void = { .tag = TYPE_VOID };
const TypeNode type_object = { .tag = TYPE_INTERFACE, .interface.members = {0} };
const TypeNode type_boolean = { .tag = TYPE_BOOLEAN };
const TypeNode type_number = { .tag = TYPE_NUMBER };
const TypeNode type_string = { .tag = TYPE_STRING };
const TypeNode type_symbol = { .tag = TYPE_SYMBOL };
const TypeNode type_bigint = { .tag = TYPE_BIGINT };
const TypeNode type_null = { .tag = TYPE_VALUE, .value.tag = VALUE_NULL };
const TypeNode type_undefined = { .tag = TYPE_VALUE, .value.tag = VALUE_UNDEFINED };

TypeNode type_composite_without_simplify(typeof((TypeNode){}.tag) tag, TypeNode a, TypeNode b) {
    if (a.tag == tag && b.tag == tag) {
        // Doesn't seem to be a more elegant way to append one vec to another
        vec_type_insert_n(&a.composite, vec_type_size(&a.composite), b.composite.data, vec_type_size(&b.composite));
        b.composite.size = 0; // fine, becuse the other vec now owns the data
        type_drop(&b);
        return a;
    } else if (a.tag == tag) {
        vec_type_push(&a.composite, b);
        return a;
    } else if (b.tag == tag) {
        vec_type_insert_n(&b.composite, 0, &a, 1);
        return b;
    } else {
        return (TypeNode){
            .tag = tag,
            .composite = c_init(vec_type, {a, b}),
        };
    }
}

void type_drop(TypeNode* me) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
            break;

        case TYPE_VALUE:
            value_drop(&my value);
            break;

        case TYPE_IDENTIFIER:
            identifier_type_drop(my identifier);
            free(my identifier);
            break;

        case TYPE_INFERRED:
            inferred_type_drop(my inferred);
            free(my inferred);
            break;

        case TYPE_CALLABLE:
            callable_type_drop(my callable);
            free(my callable);
            break;

        case TYPE_INTERFACE:
            interface_drop(&my interface);
            break;

        case TYPE_ARRAY:
            type_drop(my array.element);
            free(my array.element);
            break;

        case TYPE_TUPLE:
            vec_tuple_element_drop(&my tuple.elements);
            break;

        case TYPE_INDEXED:
            indexed_type_drop(my indexed);
            free(my indexed);
            break;

        case TYPE_KEYOF:
            keyof_type_drop(my keyof);
            free(my keyof);
            break;

        case TYPE_TYPEOF:
            typeof_type_drop(my typeof_);
            free(my typeof_);
            break;

        case TYPE_TERNARY:
            ternary_type_drop(my ternary);
            free(my ternary);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            vec_type_drop(&my composite);
            break;
    }
    my tag = TYPE_ANY; // Makes repeated calls to drop safe
}

static void type_add_inferred_names(TypeNode const* me, Scope* scope) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER:
            for_each (i, vec_spanned_type, &my identifier->generic_params) {
                type_add_inferred_names(&i->type, scope);
            }
            break;

        case TYPE_INFERRED:
            scope_init_declaration(scope, &my inferred->owned_symbol, (DeclProps){
                .name = my inferred->name,
                .full = my inferred->name,
                .kind = SYMBOL_DECL_INFER,
                .node = my inferred,
            });
            my inferred->symbol_ref = my inferred->owned_symbol;
            break;

        case TYPE_CALLABLE:
            for_each (i, vec_function_arg, &my callable->function->args) {
                if (i->initializer.binding.has_explicit_type) {
                    type_add_inferred_names(&i->initializer.binding.explicit_type, scope);
                }
            }
            type_add_inferred_names(&my callable->function->ret, scope);
            if (my callable->function->predicate && my callable->function->predicate->has_type) {
                type_add_inferred_names(&my callable->function->predicate->type, scope);
            }
            break;

        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &my interface.members) {
                type_add_inferred_names(&i->type, scope);
            }
            break;

        case TYPE_ARRAY:
            type_add_inferred_names(my array.element, scope);
            break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                type_add_inferred_names(&i->type, scope);
            }
            break;

        case TYPE_INDEXED:
            type_add_inferred_names(&my indexed->interface, scope);
            type_add_inferred_names(&my indexed->index, scope);
            break;

        case TYPE_KEYOF:
            type_add_inferred_names(&my keyof->interface, scope);
            break;

        case TYPE_TYPEOF: break;

        case TYPE_TERNARY:
            type_add_inferred_names(&my ternary->maybe_sub, scope);
            type_add_inferred_names(&my ternary->maybe_super, scope);
            type_add_inferred_names(&my ternary->if_true, scope);
            type_add_inferred_names(&my ternary->if_false, scope);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                type_add_inferred_names(i, scope);
            }
            break;

    }
}

void type_lookup_names(TypeNode* me, Scope* names, ErrorCtx* errs) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
        case TYPE_INFERRED:
            break;

        case TYPE_IDENTIFIER: identifier_type_lookup_names(my identifier, names, errs); break;
        case TYPE_CALLABLE: callable_type_resolve_names(my callable, names, errs); break;

        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &my interface.members) {
                interface_member_lookup_names(i, names, errs);
            }
            break;

        case TYPE_ARRAY:
            type_lookup_names(my array.element, names, errs);
            break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                type_lookup_names(&i->type, names, errs);
            }
            break;

        case TYPE_INDEXED: indexed_type_lookup_names(my indexed, names, errs); break;
        case TYPE_KEYOF: keyof_type_lookup_names(my keyof, names, errs); break;
        case TYPE_TYPEOF: typeof_type_lookup_names(my typeof_, names, errs); break;

        case TYPE_TERNARY: {
            type_lookup_names(&my ternary->maybe_sub, names, errs);
            type_lookup_names(&my ternary->maybe_super, names, errs);

            Scope* inferred_types_scope = scope_new(names, SCOPE_BLOCK);
            type_add_inferred_names(&my ternary->maybe_super, inferred_types_scope);
            type_lookup_names(&my ternary->if_true, inferred_types_scope, errs);

            type_lookup_names(&my ternary->if_false, names, errs);
        }   break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                type_lookup_names(i, names, errs);
            }
            break;
    }
}

void type_typecheck(TypeNode const* me, ErrorCtx* errs) {
    switch (my tag) {
        case TYPE_ANY:
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_BIGINT:
        case TYPE_VALUE:
        case TYPE_INFERRED:
            break;

        case TYPE_IDENTIFIER: identifier_type_typecheck(my identifier, errs); break;
        case TYPE_CALLABLE: callable_type_typecheck(my callable, errs); break;
        case TYPE_INTERFACE: interface_typecheck(&my interface, errs); break;
        case TYPE_ARRAY: type_typecheck(my array.element, errs); break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                type_typecheck(&i->type, errs);
            }
            break;

        case TYPE_INDEXED: indexed_type_typecheck(my indexed, errs); break;
        case TYPE_KEYOF: keyof_type_typecheck(my keyof, errs); break;
        case TYPE_TYPEOF: typeof_type_typecheck(my typeof_, errs); break;

        case TYPE_TERNARY:
            type_typecheck(&my ternary->maybe_sub, errs);
            type_typecheck(&my ternary->maybe_super, errs);
            type_typecheck(&my ternary->if_true, errs);
            type_typecheck(&my ternary->if_false, errs);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                type_typecheck(i, errs);
            }
            break;
    }
}
