#include "interface.h"
#include "interface_member.h"
#include "display.h"
#include "error.h"
#include "declaration.h"
#include "callable_type.h"
#include "expr.h"

void interface_drop(Interface* me) {
    vec_interface_member_drop(&my members);
}

Interface interface_clone(Interface me) {
    return (Interface) {
        .members = vec_interface_member_clone(me.members),
    };
}

static intptr_t index_of_matching(VecInterfaceMember const* haystack, InterfaceMember const* needle) {
    // PERF: do not merge interfaces this way, for obvious reasons
    c_foreach(i, vec_interface_member, *haystack) {
        if (i.ref->key.tag != needle->key.tag) {
            continue;
        }
        switch (needle->key.tag) {
            case INTERFACE_KEY_NAME:
                if (cstr_equals_s(&needle->key.name_string, i.ref->key.name_string)) {
                    return i.ref - haystack->data;
                } else {
                    break;
                }

            case INTERFACE_KEY_INDEX:
            case INTERFACE_KEY_EXPR:
            case INTERFACE_KEY_MAPPED:
                return i.ref - haystack->data;

            case INTERFACE_KEY_CALLABLE:
                break;
        }
    }
    return -1;
}

static void push_interface_members_to(VecInterfaceMember* target, VecInterfaceMember const* source) {
    c_foreach(new_member_i, vec_interface_member, *source) {
        InterfaceMember const* new_member = new_member_i.ref;
        intptr_t matching_index = index_of_matching(target, new_member);
        InterfaceMember* matching = matching_index < 0 ?
            nullptr :
            vec_interface_member_at_mut(target, matching_index);
        if (matching) {
            // Somewhere we want to compare types and show errors if they're different, but that can be done in the
            // typecheck function of the interface declarations. For now just use the type of the first member we
            // encounter with each name.

            // Only safe because spans are POD (clone is a no-op)
            vec_span_insert_n(
                &matching->declarations,
                vec_span_size(&matching->declarations),
                new_member->declarations.data,
                vec_span_size(&new_member->declarations)
            );
        } else {
            vec_interface_member_push(target, interface_member_clone(*new_member));
        }
    }
}

Type interface_from_merged(VecType types) {
    switch (vec_type_size(&types)) {
        case 0:
            vec_type_drop(&types);
            return type_any;

        case 1:
            Type ret = vec_type_pull(&types);
            vec_type_drop(&types);
            return ret;

        default: break;
    }
    VecInterfaceMember members = vec_interface_member_init();
    c_foreach(type_i, vec_type, types) {
        Type const* type = type_resolve(type_i.ref);
        switch (type->tag) {
            case TYPE_ANY:
                vec_interface_member_drop(&members);
                vec_type_drop(&types);
                return type_any;

            case TYPE_INTERFACE:
                push_interface_members_to(&members, &type->interface.members);
                break;

            case TYPE_CALLABLE:
                InterfaceMember callable = {
                    .declarations = vec_span_init(), // TODO
                    .key.span = span_null, // TODO
                    .key.tag = INTERFACE_KEY_CALLABLE,
                    .type = type_clone(*type),
                };
                VecInterfaceMember const single_member = {._len = 1, .data = &callable};
                push_interface_members_to(&members, &single_member);
                interface_member_drop(&callable);
                break;

            default: {
                cstr type_s = cstr_null;
                display_type(debug_display(&type_s), type);
                NONFATAL(
                    "Only interfaces and callables can be merged, but one of the types in the merge list was %s",
                    cstr_str(&type_s)
                );
                cstr_drop(&type_s);
                vec_interface_member_drop(&members);
                vec_type_drop(&types);
                return type_any;
            }
        }
    }
    vec_type_drop(&types);
    return type_interface((Interface){
        .members = members,
    });
}

void interface_typecheck(Interface const* me, ErrorCtx* errs) {
    c_foreach(i, vec_interface_member, my members) {
        interface_member_typecheck(i.ref, false, errs);
    }
}

void interface_typecheck_members_against(Interface* me, Declaration const* declaration, ErrorCtx* errs) {
    if (!declaration) {
        return;
    }
    Type const* from_named = type_resolve(&declaration->declared_type);
    if (from_named->tag != TYPE_INTERFACE) {
        NONFATAL(
            "Tried to typecheck against %.*s, which isn't interface. type tag: %d",
            c_SPAN(declaration->name),
            from_named->tag
        );
        return;
    }
    VecInterfaceMember const* reference = &from_named->interface.members;
    c_foreach(i, vec_interface_member, my members) {
        intptr_t index = index_of_matching(reference, i.ref);
        if (index < 0) {
            continue;
        }
        InterfaceMember const* matching = vec_interface_member_at(reference, index);
        // TODO: put this back once it doesn't cause problems with function overloads in the standard library
        ignore errs;
        ignore matching;
        /*
        bool check_type = true;
        if (vec_span_size(&i.ref->declarations) &&
            vec_span_size(&matching->declarations) &&
            span_cmp(*i.ref->declarations.data, *matching->declarations.data) == 0
        ) {
            // If our declaration span is the first span of the matching member it's type will be from us. Don't bother
            // checking if it matches
            check_type = false;
        }
        if (check_type && i.ref->key.tag == INTERFACE_KEY_NAME && (
            !type_extends(&i.ref->type, &matching->type) ||
            !type_extends(&matching->type, &i.ref->type)
        )) {
            cstr msg = cstr_lit("Subsequent property declarations must have the same type.  Property '");
            cstr_append_span(&msg, i.ref->key.span);
            cstr_append_lit(&msg, "' must be of type '");
            cstr_append_type_error_msg(&msg, &matching->type);
            cstr_append_lit(&msg, "', but here has type '");
            cstr_append_type_error_msg(&msg, &i.ref->type);
            cstr_append_lit(&msg, "'.");
            Error* error = error_ctx_error_s(errs, i.ref->key.span, 2717, msg);
            error_related_error(
                error,
                matching->key.span,
                6203,
                "'%.*s' was also declared here.",
                c_SPAN(matching->key.span)
            );
            //type_drop(&i.ref->type);
            //i.ref->type = type_clone(matching->type);
        }
        */

        // what the fuck was this here for? I have no idea
        //vec_span_drop(&i.ref->declarations);
        //i.ref->declarations = vec_span_clone(matching->declarations);
    }
}

InterfaceMember const* interface_select_callable_overload(
    Interface const* me,
    CallExpr const* call,
    bool* is_callable_ret
) {
    InterfaceMember const* selection;
    bool is_callable = false;

    // Look for all callable members
    selection = nullptr;
    c_foreach(i, vec_interface_member, my members) {
        if (i.ref->key.tag == INTERFACE_KEY_CALLABLE) {
            is_callable = true;
            selection = selection ? nullptr : i.ref;
            if (!selection) break;
        }
    }

    if (is_callable_ret) {
        *is_callable_ret = is_callable;
    }

    if (selection || !is_callable) {
        return selection;
    }

    // Look for callable members with the correct number of args
    selection = nullptr;
    intptr_t call_args = vec_expr_size(&call->args);
    c_foreach(i, vec_interface_member, my members) {
        if (i.ref->key.tag == INTERFACE_KEY_CALLABLE) {
            intptr_t min_args = callable_type_min_arg_count(i.ref->type.callable);
            intptr_t max_args = callable_type_max_arg_count(i.ref->type.callable);
            if (min_args <= call_args && max_args >= call_args) {
                selection = selection ? nullptr : i.ref;
                if (!selection) break;
            }
        }
    }

    if (selection) {
        return selection;
    }

    // Look for callable that typechecks without errors
    selection = nullptr;
    c_foreach(i, vec_interface_member, my members) {
        if (i.ref->key.tag == INTERFACE_KEY_CALLABLE) {
            ErrorCtx sub_errs = error_ctx_init();
            sub_errs.phase = ERROR_PHASE_TYPECHECK;
            type_typecheck_call(&i.ref->type, call, span_null, &sub_errs);
            bool no_errors = error_ctx_empty(&sub_errs);
            error_ctx_drop(&sub_errs);
            if (no_errors) {
                selection = i.ref;
                break;
            }
        }
    }

    return selection;
}
