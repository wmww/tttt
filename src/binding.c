#include "binding.h"
#include "name_stack.h"
#include "error.h"

void binding_drop(Binding* me) {
    switch (my tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER: cstr_drop(&my identifier); break;
        case BINDING_ARRAY: vec_binding_drop(&my array); break;
        case BINDING_OBJECT: vec_object_binding_item_drop(&my object); break;
    }
    type_drop(&my type);
}

Binding binding_clone(Binding me) {
    switch (me.tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER: me.identifier = cstr_clone(me.identifier); break;
        case BINDING_ARRAY: me.array = vec_binding_clone(me.array); break;
        case BINDING_OBJECT: me.object = vec_object_binding_item_clone(me.object); break;
    }
    me.type = type_clone(me.type);
    return me;
}

void cstr_append_binding_debug(cstr* target, Binding const* me, int indent) {
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            cstr_append_sv(target, c_sv(CLR_NAME));
            cstr_append_s(target, my identifier);
            cstr_append_sv(target, c_sv(CLR0));
            break;

        case BINDING_ARRAY:
            cstr_append_sv(target, c_sv("["));
            c_foreach(i, vec_binding, my array) {
                cstr_append_binding_debug(target, i.ref, indent);
                if (i.ref != i.end - 1) {
                    cstr_append_sv(target, c_sv(", "));
                }
            }
            cstr_append_sv(target, c_sv("]"));
            break;

        case BINDING_OBJECT:
            cstr_append_sv(target, c_sv("{"));
            c_foreach(i, vec_object_binding_item, my object) {
                cstr_append_value_debug(target, &i.ref->source, indent);
                cstr_append_sv(target, c_sv(": "));
                cstr_append_binding_debug(target, &i.ref->target, indent);
                if (i.ref != i.end - 1) {
                    cstr_append_sv(target, c_sv(", "));
                }
            }
            cstr_append_sv(target, c_sv("}"));
            break;
    }
    if (my type_explicit) {
        cstr_append_sv(target, c_sv(": "));
        cstr_append_type_debug(target, &my type, indent);
    }
}

void cstr_append_binding_error_msg(cstr* target, Binding const* me) {
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            cstr_append_s(target, my identifier);
            break;

        case BINDING_ARRAY:
            cstr_append_sv(target, c_sv("["));
            c_foreach(i, vec_binding, my array) {
                cstr_append_binding_error_msg(target, i.ref);
                if (i.ref != i.end - 1) {
                    cstr_append_sv(target, c_sv(", "));
                }
            }
            cstr_append_sv(target, c_sv("]"));
            break;

        case BINDING_OBJECT:
            cstr_append_sv(target, c_sv("{ "));
            c_foreach(i, vec_object_binding_item, my object) {
                cstr_append_value_error_msg(target, &i.ref->source);
                cstr_append_sv(target, c_sv(": "));
                cstr_append_binding_error_msg(target, &i.ref->target);
                if (i.ref != i.end - 1) {
                    cstr_append_sv(target, c_sv(", "));
                }
            }
            cstr_append_sv(target, c_sv(" }"));
            break;
    }
}

void binding_add_names(Binding* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            if (!name_stack_add_variable(
                names,
                cstr_clone(my identifier),
                my content_span,
                &my type
            )) {
                // name is cstr_str(&result.ref->first)
                error_ctx_error(errs, my content_span, TS_CODE_NONE, "multiple values declared with same name");
            }
            break;

        case BINDING_ARRAY:
            c_foreach(i, vec_binding, my array) {
                binding_add_names(i.ref, names, errs);
            }
            break;

        case BINDING_OBJECT:
            c_foreach(i, vec_object_binding_item, my object) {
                binding_add_names(&i.ref->target, names, errs);
            }
            break;
    }
}

void binding_resolve_names(Binding* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case BINDING_NOOP: break;

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
    type_resolve_names(&my type, names, errs);
}

void binding_add_implicit_any_type_errors(Binding const* me, bool toplevel, ErrorCtx* errs) {
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            error_ctx_error(
                errs,
                my content_span,
                toplevel ? 7006 : 7031,
                "%s '%s' implicitly has an 'any' type.",
                toplevel ? "Parameter" : "Binding element",
                cstr_str(&my identifier)
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

void object_binding_item_drop(ObjectBindingItem* me) {
    value_drop(&my source);
    binding_drop(&my target);
}

ObjectBindingItem object_binding_item_clone(ObjectBindingItem me) {
    me.source = value_clone(me.source);
    me.target = binding_clone(me.target);
    return me;
}

void initialized_binding_drop(InitializedBinding* me) {
    binding_drop(&my binding);
    expr_drop(&my value);
}

void cstr_append_initialized_binding_debug(cstr* target, InitializedBinding const* me, int indent) {
    cstr_append_binding_debug(target, &my binding, indent);
    if (my value_explicit) {
        cstr_append_sv(target, c_sv(" = "));
        cstr_append_expr_debug(target, &my value, indent);
    }
}

void initialized_binding_resolve_names(InitializedBinding* me, NameStack* names, ErrorCtx* errs) {
    binding_resolve_names(&my binding, names, errs);
    expr_resolve_names(&my value, names, errs);
}

void initialized_binding_typecheck(InitializedBinding* me, bool absract_literals, ErrorCtx* errs) {
    if (my value_explicit) {
        Type expr_type = expr_get_type_and_typecheck(&my value, errs);
        if (my binding.type_explicit) {
            check_assignable_or_error(&expr_type, &my binding.type, my binding.content_span, errs);
        } else {
            type_drop(&my binding.type);
            my binding.type = type_clone(expr_type);
            if (absract_literals || my binding.type.tag == TYPE_TUPLE) {
                type_abstract_literals(&my binding.type);
            }
            type_abstract_tuple_to_array(&my binding.type);
            if (my binding.type.tag == TYPE_ARRAY &&
                my binding.type.array->tag == TYPE_VALUE &&
                my binding.type.array->value.tag == VALUE_UNDEFINED
            ) {
                type_drop(my binding.type.array);
                *my binding.type.array = type_any;
            }
        }
        type_drop(&expr_type);
    }
}
