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

bool interface_try_from_type(Type const* me, Type* ret) {
    me = type_resolve(me);
    switch (my tag) {
        case TYPE_CALLABLE: {
            VecInterfaceMember members = vec_interface_member_with_capacity(1);
            vec_interface_member_push(&members, (InterfaceMember) {
               .key.tag = INTERFACE_KEY_CALLABLE,
               .type = type_clone(*me),
            });
            *ret = type_interface((Interface) {
                .members = members,
            });
            return true;
        }

        case TYPE_INTERFACE:
            *ret = type_clone(*me);
            return true;

        case TYPE_ARRAY:
        case TYPE_TUPLE:
            // TODO
            return false;

        case TYPE_UNION:
            if (vec_type_size(&my composite) == 1) {
                return interface_try_from_type(my composite.data, ret);
            } else {
                return false;
            }

        case TYPE_INTERSECTION: {
            VecType interfaces = vec_type_init();
            for_each (i, vec_type, &my composite) {
                Type t;
                if (!interface_try_from_type(i, &t)) {
                    vec_type_drop(&interfaces);
                    return false;
                }
                vec_type_push(&interfaces, t);
            }
            *ret = interface_from_merged_interfaces(interfaces);
            return true;
        }

        default:
            return false;
    }
}

static intptr_t index_of_matching(VecInterfaceMember const* haystack, InterfaceMember const* needle) {
    // PERF: do not merge interfaces this way, for obvious reasons
    for_each (i, vec_interface_member, haystack) {
        if (i->key.tag != needle->key.tag) {
            continue;
        }
        switch (needle->key.tag) {
            case INTERFACE_KEY_NAME:
                if (cstr_eq(&needle->key.name_string, &i->key.name_string)) {
                    return i_index;
                } else {
                    break;
                }

            case INTERFACE_KEY_INDEX:
            case INTERFACE_KEY_EXPR:
            case INTERFACE_KEY_MAPPED:
                return i_index;

            case INTERFACE_KEY_CALLABLE:
                break;
        }
    }
    return -1;
}

static void push_interface_members_to(VecInterfaceMember* target, VecInterfaceMember source) {
    for_each (new_member, vec_interface_member, &source) {
        intptr_t matching_index = index_of_matching(target, new_member);
        InterfaceMember* matching = matching_index < 0 ?
            nullptr :
            vec_interface_member_at_mut(target, matching_index);
        if (matching) {
            for_each (i, vec_span, &new_member->declarations) {
                bool add = true;
                for_each (j, vec_span, &matching->declarations) {
                    if (span_cmp(*i, *j) == 0) {
                        add = false;
                        break;
                    }
                }
                if (add) {
                    vec_span_push(&matching->declarations, *i);
                }
            }
            interface_member_drop(new_member);
        } else {
            vec_interface_member_push(target, *new_member);
        }
    }
    source.size = 0;
    vec_interface_member_drop(&source);
}

Type interface_from_merged_interfaces(VecType types) {
    VecInterfaceMember members = vec_interface_member_init();
    for_each (i, vec_type, &types) {
        ASSERT(i->tag == TYPE_INTERFACE);
        push_interface_members_to(&members, i->interface.members);
        i->interface.members = vec_interface_member_init();
    }
    vec_type_drop(&types);
    return type_interface((Interface){
        .members = members,
    });
}

void interface_typecheck(Interface const* me, ErrorCtx* errs) {
    for_each (i, vec_interface_member, &my members) {
        interface_member_typecheck(i, false, errs);
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
    for_each (i, vec_interface_member, &my members) {
        intptr_t index = index_of_matching(reference, i);
        if (index < 0) {
            continue;
        }
        InterfaceMember const* matching = vec_interface_member_at(reference, index);
        // TODO: put this back once it doesn't cause problems with function overloads in the standard library
        ignore errs;
        ignore matching;
        /*
        bool check_type = true;
        if (vec_span_size(&i->declarations) &&
            vec_span_size(&matching->declarations) &&
            span_cmp(*i->declarations.data, *matching->declarations.data) == 0
        ) {
            // If our declaration span is the first span of the matching member it's type will be from us. Don't bother
            // checking if it matches
            check_type = false;
        }
        if (check_type && i->key.tag == INTERFACE_KEY_NAME && (
            !type_extends(&i->type, &matching->type) ||
            !type_extends(&matching->type, &i->type)
        )) {
            cstr msg = cstr_lit("Subsequent property declarations must have the same type.  Property '");
            cstr_append_span(&msg, i->key.span);
            cstr_append_lit(&msg, "' must be of type '");
            cstr_append_type_error_msg(&msg, &matching->type);
            cstr_append_lit(&msg, "', but here has type '");
            cstr_append_type_error_msg(&msg, &i->type);
            cstr_append_lit(&msg, "'.");
            Error* error = error_ctx_error_s(errs, i->key.span, 2717, msg);
            error_related_error(
                error,
                matching->key.span,
                6203,
                "'%.*s' was also declared here.",
                c_SPAN(matching->key.span)
            );
            //type_drop(&i->type);
            //i->type = type_clone(matching->type);
        }
        */

        // what the fuck was this here for? I have no idea
        //vec_span_drop(&i->declarations);
        //i->declarations = vec_span_clone(matching->declarations);
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
    for_each (i, vec_interface_member, &my members) {
        if (i->key.tag == INTERFACE_KEY_CALLABLE) {
            is_callable = true;
            selection = selection ? nullptr : i;
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
    for_each (i, vec_interface_member, &my members) {
        if (i->key.tag == INTERFACE_KEY_CALLABLE) {
            intptr_t min_args = callable_type_min_arg_count(i->type.callable);
            intptr_t max_args = callable_type_max_arg_count(i->type.callable);
            if (min_args <= call_args && max_args >= call_args) {
                selection = selection ? nullptr : i;
                if (!selection) break;
            }
        }
    }

    if (selection) {
        return selection;
    }

    // Look for callable that typechecks without errors
    selection = nullptr;
    for_each (i, vec_interface_member, &my members) {
        if (i->key.tag == INTERFACE_KEY_CALLABLE) {
            ErrorCtx sub_errs = error_ctx_init();
            sub_errs.phase = ERROR_PHASE_TYPECHECK;
            type_typecheck_call(&i->type, call, span_null, &sub_errs);
            bool no_errors = error_ctx_empty(&sub_errs);
            error_ctx_drop(&sub_errs);
            if (no_errors) {
                selection = i;
                break;
            }
        }
    }

    return selection;
}
