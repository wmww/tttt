#include "binding.h"
#include "name_stack.h"
#include "several_types.h"
#include "display.h"
#include "error.h"

void binding_drop(Binding* me) {
    switch (my tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER: arc_declaration_drop(&my declaration); break;
        case BINDING_ARRAY: vec_binding_drop(&my array); break;
        case BINDING_OBJECT: vec_object_binding_item_drop(&my object); break;
    }
    type_drop(&my explicit_type);
    type_drop(&my assigned_type);
}

void binding_add_names(Binding* me, Type assigned, Scope* scope) {
    my assigned_type = assigned;

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            scope_init_declaration(scope, &my declaration, (DeclProps){
                .name = my content_span,
                .full = my content_span,
                .tag = DECLARATION_VARIABLE,
                .value_type = type_refcopy(binding_get_effective_type(me)),
            });
            break;

        case BINDING_ARRAY: {
            c_foreach(i, vec_binding, my array) {
                intptr_t index = i.ref - my array.data;
                binding_add_names(
                    i.ref,
                    type_indexed(c_new(IndexedType, {
                        .interface = type_refcopy(binding_get_effective_type(me)),
                        .index = type_value(value_number(index)),
                        .is_from_binding = true,
                        .array_only = true,
                    })),
                    scope
                );
            }
        }   break;

        case BINDING_OBJECT: {
            c_foreach(i, vec_object_binding_item, my object) {
                binding_add_names(
                    &i.ref->target,
                    type_indexed(c_new(IndexedType, {
                        .interface = type_refcopy(binding_get_effective_type(me)),
                        .index = type_value(value_clone(i.ref->source)),
                        .is_from_binding = true,
                    })),
                    scope
                );
            }
        }   break;
    }
}

void binding_lookup_names(Binding* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case BINDING_NOOP:
        case BINDING_IDENTIFIER: break;

        case BINDING_ARRAY:
            c_foreach(i, vec_binding, my array) {
                binding_lookup_names(i.ref, names, errs);
            }
            break;

        case BINDING_OBJECT:
            c_foreach(i, vec_object_binding_item, my object) {
                binding_lookup_names(&i.ref->target, names, errs);
            }
            break;
    }

    if (my has_explicit_type) {
        type_lookup_names(&my explicit_type, names, errs);
    }
    type_lookup_names(&my assigned_type, names, errs);
}

Type const* binding_get_effective_type(Binding const* me) {
    return my has_explicit_type ? &my explicit_type : &my assigned_type;
}

void binding_add_implicit_any_type_errors(Binding const* me, bool toplevel, ErrorCtx* errs) {
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            error_ctx_error(
                errs,
                my content_span,
                toplevel ? 7006 : 7031,
                "%s '%.*s' implicitly has an 'any' type.",
                toplevel ? "Parameter" : "Binding element",
                c_SPAN(my content_span)
            );
            break;

        case BINDING_ARRAY:
            c_foreach(i, vec_binding, my array) {
                binding_add_implicit_any_type_errors(i.ref, false, errs);
            }
            break;

        case BINDING_OBJECT:
            c_foreach(i, vec_object_binding_item, my object) {
                binding_add_implicit_any_type_errors(&i.ref->target, false, errs);
            }
            break;
    }
}

void binding_typecheck(Binding* me, ErrorCtx* errs) {
    if (my has_explicit_type) {
        type_typecheck(&my explicit_type, errs);
        type_error_if_not_assignable(
            &my assigned_type,
            &my explicit_type,
            my content_span,
            errs
        );
    }

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            if (my declaration.get) {
                Type const* type = type_resolve(binding_get_effective_type(me));
                if (type_is_self_referential(type)) {
                    cstr msg = cstr_lit("'");
                    cstr_append_span(&msg, my content_span);
                    cstr_append_lit(&msg, "' implicitly has type '");
                    cstr_append_type_error_msg(&msg, type);
                    cstr_append_lit(&msg, "' because it does not have a type annotation and is referenced directly or indirectly in its own initializer.");
                    error_ctx_error_s(errs, my content_span, 7022, msg);
                }
            }
            break;

        case BINDING_ARRAY:
            if (!type_extends_array(binding_get_effective_type(me))) {
                cstr msg = cstr_lit("Type '");
                cstr_append_type_error_msg(&msg, binding_get_effective_type(me));
                cstr_append_lit(&msg, "' is not an array type.");
                error_ctx_error_s(errs, my content_span, 2461, msg);
            }
            c_foreach(i, vec_binding, my array) {
                binding_typecheck(i.ref, errs);
            }
            break;

        case BINDING_OBJECT:
            c_foreach(i, vec_object_binding_item, my object) {
                binding_typecheck(&i.ref->target, errs);
            }
            break;
    }
}

void object_binding_item_drop(ObjectBindingItem* me) {
    value_drop(&my source);
    binding_drop(&my target);
}

void initialized_binding_drop(InitializedBinding* me) {
    binding_drop(&my binding);
    expr_drop(&my value);
}

void initialized_binding_add_names(InitializedBinding* me, bool abstract_literals, Scope* scope) {
    binding_add_names(
        &my binding,
        make_type_abstracted(
            type_lazy(&my value.lazy_type),
            !my binding.has_explicit_type && my binding.tag == BINDING_IDENTIFIER && abstract_literals
        ),
        scope
    );
}

void initialized_binding_lookup_names(InitializedBinding* me, NameStack* names, ErrorCtx* errs) {
    binding_lookup_names(&my binding, names, errs);
    expr_lookup_names(&my value, names, errs);
}

void initialized_binding_typecheck(InitializedBinding* me, ErrorCtx* errs) {
    expr_typecheck(&my value, errs);
    binding_typecheck(&my binding, errs);
}
