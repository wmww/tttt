#include "display.h"
#include "type.h"
#include "identifier_type.h"
#include "callable_type.h"
#include "interface.h"
#include "interface_member.h"
#include "indexed_type.h"
#include "ternary_type.h"
#include "name_stack.h"

#define has_underline(d) (d.flags & DISPLAY_FLAGS_UNDERLINE_TYPES)

static inline void extend_underline(Display d) {
    if (has_underline(d)) {
        intptr_t extend_to = cstr_size(&d.target_with_underline->main_str) + d.target_with_underline->underline_offset;
        if (extend_to > cstr_size(&d.target_with_underline->underline_str)) {
            cstr_resize(
                &d.target_with_underline->underline_str,
                extend_to,
                d.target_with_underline->inhibit_underline_count ? ' ' : '^');
            if (!d.target_with_underline->inhibit_underline_count) {
                d.target_with_underline->underlines_have_been_added = true;
            }
        }
    }
}

static inline void underline_on(Display d) {
    if (has_underline(d)) {
        extend_underline(d);
        d.target_with_underline->inhibit_underline_count--;
        ASSERT(d.target_with_underline->inhibit_underline_count >= 0);
    }
}

static inline void underline_off(Display d) {
    if (has_underline(d)) {
        extend_underline(d);
        d.target_with_underline->inhibit_underline_count++;
    }
}

void display_value(Display d, Value const* me) {
    switch (my tag) {
        case VALUE_STRING:
            display_lit(d, "\"");
            display_s(d, my string);
            display_lit(d, "\"");
            break;

        default:
            cstr_append_value_js(d.target, me);
            break;
    }
}

void display_generic(Display d, Generic const* me) {
    if (generic_empty(me)) {
        return;
    }
    display_lit(d, "<");
    c_foreach(i, vec_generic_item, my items) {
        underline_off(d);
        display_span(d, i.ref->name);
        underline_on(d);
        if (i.ref->extends.tag != TYPE_ANY) {
            display_lit(d, " extends ");
            underline_off(d);
            display_type(d, &i.ref->extends);
            underline_on(d);
        }
        if (i.ref->fallback_explicit) {
            display_lit(d, " = ");
            display_type(d, &i.ref->fallback);
        }
        if (i.ref < i.end - 1) {
            display_lit(d, ", ");
        }
    }
    display_lit(d, ">");
}

void display_binding(Display d, Binding const* me, bool optional) {
    underline_off(d);

    switch (my tag) {
        case BINDING_NOOP: break;

        case BINDING_IDENTIFIER:
            display_span(d, my content_span);
            break;

        case BINDING_ARRAY:
            display_lit(d, "[");
            c_foreach(i, vec_binding, my array) {
                display_binding(d, i.ref, false);
                if (i.ref != i.end - 1) {
                    display_lit(d, ", ");
                }
            }
            display_lit(d, "]");
            break;

        case BINDING_OBJECT:
            display_lit(d, "{ ");
            c_foreach(i, vec_object_binding_item, my object) {
                display_value(d, &i.ref->source);
                display_lit(d, ": ");
                display_binding(d, &i.ref->target, false);
                if (i.ref != i.end - 1) {
                    display_lit(d, ", ");
                }
            }
            display_lit(d, " }");
            break;
    }

    if (optional) {
        display_lit(d, "?");
    }
    underline_on(d);

    display_lit(d, ": ");

    underline_off(d);
    display_type(d, binding_get_type((Binding*)me));
    underline_on(d);
}

void display_lazy_type(Display d, LazyType const* me) {
    Type const* t = lazy_type_try_get(me);
    if (t) {
        display_type(d, t);
    } else {
        display_lit(d, "/* unevaluated */");
    }
}

void display_identifier_type(Display d, IdentifierType* me) {
    if (my inferred) {
        display_lit(d, "infer ");
    }
    if (my type && my type->is_typeof_class) {
        display_lit(d, "typeof ");
    }
    display_span(d, my name);
    if (!vec_spanned_type_empty(&my generic_params)) {
        display_lit(d, "<");
        c_foreach(i, vec_spanned_type, my generic_params) {
            display_type(d, &i.ref->type);
            if (i.ref != i.end - 1) {
                display_lit(d, ", ");
            }
        }
        display_lit(d, ">");
    }
}

void display_callable(Display d, CallableType* me) {
    if (my is_constructor) {
        display_lit(d, "new ");
    }
    display_generic(d, &my generic);
    display_lit(d, "(");
    c_foreach(i, vec_callable_arg, my args) {
        if (i.ref->rest) {
            display_lit(d, "...");
        }
        display_binding(d, &i.ref->binding, i.ref->optional);
        if (i.ref != i.end - 1) {
            display_lit(d, ", ");
        }
    }
    display_lit(d, ")");
    display_lit(d, " => ");

    if (my ret_explicit) {
        underline_off(d);
        display_type(d, &my ret);
        underline_on(d);
    } else if (my ret.tag != TYPE_ANY) {
        display_type(d, &my ret);
    } else {
        display_lit(d, "any");
    }
}

void display_interface_member(Display d, InterfaceMember const* me) {
    if (my readonly) {
            display_lit(d, "readonly ");
        }
        switch (my key.tag) {
        case INTERFACE_KEY_NAME:
            display_s(d, my key.name_string);
            display_lit(d, ": ");
            break;

        case INTERFACE_KEY_INDEX:
            display_lit(d, "[");
            display_span(d, my key.span);
            display_lit(d, ": ");
            display_type(d, &my key.index);
            display_lit(d, "]: ");
            break;

        case INTERFACE_KEY_EXPR:
            display_lit(d, "[");
            display_span(d, my key.expr.get->outer_span);
            display_lit(d, "]: ");
            break;

        case INTERFACE_KEY_MAPPED:
            if (my key.mapped.readonly) {
                if (my key.mapped.readonly > 0) {
                    display_lit(d, "+");
                } else {
                    display_lit(d, "-");
                }
                display_lit(d, "readonly ");
            }
            display_lit(d, "[");
            display_span(d, my key.mapped.type_name);
            display_lit(d, " in ");
            display_type(d, &my key.mapped.in_type);
            if (my key.mapped.has_as_type) {
                display_lit(d, " as ");
                display_type(d, &my key.mapped.as_type);
            }
            display_lit(d, "]");
            if (my key.mapped.optional > 0) {
                display_lit(d, "+?");
            } else if (my key.mapped.optional < 0) {
                display_lit(d, "-?");
            }
            display_lit(d, ": ");
            break;

        case INTERFACE_KEY_CALLABLE:
            break;
    }

    if (my underline_in_tests) {
        display_lazy_type(display_remove_flags(d, DISPLAY_FLAGS_UNDERLINE_TYPES), &my type);
    } else {
        underline_off(d);
        display_lazy_type(d, &my type);
        underline_on(d);
    }
}

void display_interface(Display d, Interface const* me) {
    if (vec_interface_member_size(&my members) == 1 &&
        my members.data->key.tag == INTERFACE_KEY_CALLABLE
    ) {
        // This interface has a single callable member, so display it as if it were just a callable
        display_lazy_type(d, &my members.data->type);
        return;
    }

    display_lit(d, "{");
    if (!vec_interface_member_empty(&my members)) {
        display_lit(d, " ");
    }
    c_foreach(i, vec_interface_member, my members) {
        display_interface_member(d, i.ref);
        display_lit(d, "; ");
    }
    display_lit(d, "}");
}

void display_indexed_type(Display d, IndexedType* me) {
    display_type(d, &my interface);
    display_lit(d, "[");
    display_type(d, &my index);
    display_lit(d, "]");
}

void display_keyof_type(Display d, KeyofType* me) {
    display_lit(d, "keyof ");
    display_type(d, &my interface);
}

void display_ternary_type(Display d, TernaryType* me) {
    display_type(d, &my maybe_sub);
    display_lit(d, " extends ");
    display_type(d, &my maybe_super);
    display_lit(d, "\n");
    //APPEND_INDENT(target, indent + 1);
    display_lit(d, "? ");
    display_type(d, &my if_true);
    display_lit(d, "\n");
    //APPEND_INDENT(target, indent + 1);
    display_lit(d, ": ");
    display_type(d, &my if_false);
}

void display_type(Display d, Type const* me) {
    switch (my tag) {
        case TYPE_ANY:
            if (has_underline(d) && !d.target_with_underline->underline_any) {
                //underline_off(d);
                display_lit(d, "any");
                //underline_on(d);
            } else {
                display_lit(d, "any");
            }
            break;

        case TYPE_UNKNOWN: display_lit(d, "unknown"); break;
        case TYPE_NEVER: display_lit(d, "never"); break;
        case TYPE_VOID: display_lit(d, "void"); break;
        case TYPE_BOOLEAN: display_lit(d, "boolean"); break;
        case TYPE_NUMBER: display_lit(d, "number"); break;
        case TYPE_STRING: display_lit(d, "string"); break;
        case TYPE_VALUE: display_value(d, &my value); break;
        case TYPE_IDENTIFIER: display_identifier_type(d, my identifier); break;
        case TYPE_CALLABLE: display_callable(d, my callable); break;
        case TYPE_INTERFACE: display_interface(d, &my interface); break;

        case TYPE_ARRAY: {
            bool needs_peren = my array.element->tag == TYPE_UNION || my array.element->tag == TYPE_INTERSECTION;
            if (needs_peren) {
                display_lit(d, "(");
            }
            if (my array.readonly) {
                display_lit(d, "readonly ");
            }
            display_type(d, my array.element);
            if (needs_peren) {
                display_lit(d, ")");
            }
            display_lit(d, "[]");
        }   break;

        case TYPE_TUPLE:
            if (my tuple.readonly) {
                display_lit(d, "readonly ");
            }
            display_lit(d, "[");
            c_foreach(i, vec_type, my tuple.elements) {
                display_type(d, i.ref);
                if (i.ref != i.end - 1) {
                    display_lit(d, ", ");
                }
            }
            display_lit(d, "]");
            break;

        case TYPE_INDEXED: display_indexed_type(d, my indexed); break;
        case TYPE_KEYOF: display_keyof_type(d, my keyof); break;
        case TYPE_TERNARY: display_ternary_type(d, my ternary); break;

        case TYPE_UNION:
        case TYPE_INTERSECTION: {
            bool first = true;
            c_foreach(i, vec_type, my composite) {
                if (i.ref->tag != (my tag == TYPE_UNION ? TYPE_NEVER : TYPE_ANY)) {
                    if (first) {
                        first = false;
                    } else if (my tag == TYPE_UNION) {
                        display_lit(d, " | ");
                    } else {
                        display_lit(d, " & ");
                    }
                    display_type(d, i.ref);
                }
            }
        }   break;

        case TYPE_REF: display_type(d, my ref); break;
    }

    extend_underline(d);
}
