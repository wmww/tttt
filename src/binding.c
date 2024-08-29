#include "binding.h"
#include "name_stack.h"
#include "several_types.h"
#include "display.h"
#include "error.h"

void binding_drop(Binding* me) {
    switch (my tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER: named_variable_destroy(my identifier); break;
        case BINDING_ARRAY: vec_binding_drop(&my array); break;
        case BINDING_OBJECT: vec_object_binding_item_drop(&my object); break;
    }
    type_drop(&my explicit_type);
    type_drop(&my assigned_type);
}

void binding_add_names(Binding* me, Type assigned, NameStack* names, ErrorCtx* errs) {
    my assigned_type = assigned;

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            ASSERT(!my identifier);
            my identifier = make_named_variable(
                my content_span,
                my content_span,
                type_ref(binding_get_effective_type(me)),
                nullptr
            );
            name_stack_add_variable(names, my identifier);
            break;

        case BINDING_ARRAY: {
            c_foreach(i, vec_binding, my array) {
                intptr_t index = i.ref - my array.data;
                binding_add_names(
                    i.ref,
                    type_indexed(c_new(IndexedType, {
                        .interface = type_ref(binding_get_effective_type(me)),
                        .index = type_value(value_number(index)),
                        .is_from_binding = true,
                    })),
                    names,
                    errs
                );
            }
        }   break;

        case BINDING_OBJECT: {
            c_foreach(i, vec_object_binding_item, my object) {
                binding_add_names(
                    &i.ref->target,
                    type_indexed(c_new(IndexedType, {
                        .interface = type_ref(binding_get_effective_type(me)),
                        .index = type_value(value_clone(i.ref->source)),
                        .is_from_binding = true,
                    })),
                    names,
                    errs
                );
            }
        }   break;
    }
}

void binding_resolve_names(Binding* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case BINDING_NOOP:
        case BINDING_IDENTIFIER: break;

        case BINDING_ARRAY:
            c_foreach(i, vec_binding, my array) {
                binding_resolve_names(i.ref, names, errs);
            }
            break;

        case BINDING_OBJECT:
            c_foreach(i, vec_object_binding_item, my object) {
                binding_resolve_names(&i.ref->target, names, errs);
            }
            break;
    }

    if (my has_explicit_type) {
        type_resolve_names(&my explicit_type, names, errs);
    }
    type_resolve_names(&my assigned_type, names, errs);
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
                c_SV(span_sv(&my content_span))
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
        check_assignable_or_error(
            &my assigned_type,
            &my explicit_type,
            my content_span,
            errs
        );
    }

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            if (my identifier) {
                Type const* type = type_resolve(binding_get_effective_type(me));
                if (type_is_self_referential(type)) {
                    cstr msg = cstr_lit("'");
                    cstr_append_span(&msg, &my content_span);
                    cstr_append_lit(&msg, "' implicitly has type '");
                    cstr_append_type_error_msg(&msg, type);
                    cstr_append_lit(&msg, "' because it does not have a type annotation and is referenced directly or indirectly in its own initializer.");
                    error_ctx_error_s(errs, my content_span, 7022, msg);
                }
            }
            break;

        case BINDING_ARRAY:
            if (errs && !type_extends_array(binding_get_effective_type(me))) {
                cstr msg = cstr_lit("Type '");
                cstr_append_type_error_msg(&msg, binding_get_effective_type(me));
                cstr_append_sv(&msg, c_sv("' is not an array type."));
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

void initialized_binding_add_names(InitializedBinding* me, bool abstract_literals, NameStack* names, ErrorCtx* errs) {
    binding_add_names(
        &my binding,
        make_type_abstracted(
            type_lazy(&my value.lazy_type),
            !my binding.has_explicit_type && my binding.tag == BINDING_IDENTIFIER && abstract_literals
        ),
        names,
        errs
    );
}

void initialized_binding_resolve_names(InitializedBinding* me, NameStack* names, ErrorCtx* errs) {
    binding_resolve_names(&my binding, names, errs);
    expr_resolve_names(&my value, names, errs);
}

void initialized_binding_typecheck(InitializedBinding* me, ErrorCtx* errs) {
    Type t = expr_get_type_and_typecheck(&my value, errs);
    type_drop(&t);
    binding_typecheck(&my binding, errs);
}
