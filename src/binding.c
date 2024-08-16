#include "binding.h"
#include "name_stack.h"
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
    lazy_type_drop(&my assigned_type);
}

Binding binding_clone(Binding me) {
    switch (me.tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER: me.identifier = nullptr; break;
        case BINDING_ARRAY: me.array = vec_binding_clone(me.array); break;
        case BINDING_OBJECT: me.object = vec_object_binding_item_clone(me.object); break;
    }
    me.explicit_type = type_clone(me.explicit_type);
    me.assigned_type = lazy_type_clone(me.assigned_type);
    return me;
}

void binding_add_names(Binding* me, LazyType assigned, NameStack* names, ErrorCtx* errs) {
    ASSERT(my assigned_type.tag == 0 && my assigned_type.value.tag == 0);
    my assigned_type = assigned;

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            ASSERT(!my identifier);
            my identifier = make_named_variable(my content_span, binding_get_lazy_type(me), nullptr);
            name_stack_add_variable(names, my identifier);
            break;

        case BINDING_ARRAY: {
            ArcLazyType source = arc_lazy_type_make(binding_get_lazy_type(me));
            c_foreach(i, vec_binding, my array) {
                intptr_t index = i.ref - my array.data;
                binding_add_names(
                    i.ref,
                    lazy_type_indexed(arc_lazy_type_clone(source), value_number(index), true),
                    names,
                    errs
                );
            }
            arc_lazy_type_drop(&source);
        }   break;

        case BINDING_OBJECT: {
            ArcLazyType source = arc_lazy_type_make(binding_get_lazy_type(me));
            c_foreach(i, vec_object_binding_item, my object) {
                binding_add_names(
                    &i.ref->target,
                    lazy_type_indexed(arc_lazy_type_clone(source), value_clone(i.ref->source), true),
                    names,
                    errs
                );
            }
            arc_lazy_type_drop(&source);
        }   break;
    }
}

LazyType binding_get_lazy_type(Binding const* me) {
    if (my has_explicit_type) {
        return lazy_type_value(type_clone(my explicit_type));
    } else {
        return lazy_type_clone(my assigned_type);
    }
}

Type const* binding_get_type(Binding* me) {
    if (my has_explicit_type) {
        return &my explicit_type;
    } else {
        return lazy_type_resolve(&my assigned_type);
    }
}

void binding_resolve_names(Binding* me, NameStack* names, ErrorCtx* errs) {
    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            if (my identifier) {
                lazy_type_resolve_names(&my identifier->type, names, errs);
            }
            break;

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
    type_resolve_names(&my explicit_type, names, errs);
    lazy_type_resolve_names(&my assigned_type, names, errs);
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
            lazy_type_resolve(&my assigned_type),
            &my explicit_type,
            my content_span,
            errs
        );
    }

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            if (my identifier) {
                Type const* type = type_resolve(lazy_type_resolve(&my identifier->type));
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
            if (!type_extends_array(binding_get_type(me))) {
                cstr msg = cstr_lit("Type '");
                cstr_append_type_error_msg(&msg, binding_get_type(me));
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

ObjectBindingItem object_binding_item_clone(ObjectBindingItem me) {
    me.source = value_clone(me.source);
    me.target = binding_clone(me.target);
    return me;
}

void initialized_binding_drop(InitializedBinding* me) {
    binding_drop(&my binding);
    expr_drop(&my value);
}

void initialized_binding_add_names(InitializedBinding* me, bool abstract_literals, NameStack* names, ErrorCtx* errs) {
    bool type_abstract_tuple_to_array = true;
    if (my binding.has_explicit_type || my binding.tag != BINDING_IDENTIFIER) {
        abstract_literals = false;
        type_abstract_tuple_to_array = false;
    }
    binding_add_names(
        &my binding,
        lazy_type_expr(&my value, abstract_literals, type_abstract_tuple_to_array),
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
