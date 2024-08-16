#include "type_decl.h"
#include "error.h"
#include "identifier_type.h"
#include "interface_member.h"
#include "interface.h"
#include "type.h"
#include "name_stack.h"

void type_decl_drop(TypeDecl* me) {
    generic_drop(&my generic);
    vec_spanned_type_drop(&my interface_extends);
    type_drop(&my type);
    type_drop(&my resolved_type);
    named_type_destroy(my named_type);
}

void type_decl_add_names(TypeDecl* me, NameStack* names, ErrorCtx* errs) {
    __( errs );
    ASSERT(!my named_type);
    LazyType lazy_type = (my is_interface && ! vec_spanned_type_empty(&my interface_extends)) ?
        lazy_type_interface(me) :
        lazy_type_ref(&my type);
    my named_type = make_named_type(my full_span, lazy_type, &my generic);
    name_stack_add_type_s(names, cstr_from_span(&my name), my named_type);
}

Type const* type_decl_resolve_interface_type(TypeDecl* me) {
    ASSERT(my is_interface);
    ASSERT(my type.tag == TYPE_INTERFACE);

    switch (my type_resolve_state) {
        case RESOLVE_STATE_UNINITIALIZED:
            my type_resolve_state = RESOLVE_STATE_IN_FLIGHT;
            VecInterfaceMember members = {0};

            c_foreach(extends, vec_spanned_type, my interface_extends) {
                Type const* extends_resolved = type_resolve(&extends.ref->type);
                if (extends_resolved->tag == TYPE_INTERFACE) {
                    c_foreach(extends_member, vec_interface_member, extends_resolved->interface.members) {
                        bool found_match = false;
                        c_foreach(previous_member, vec_interface_member, members) {
                            if (interface_key_extends(extends_member.ref, previous_member.ref)) {
                                found_match = true;
                                break;
                            }
                        }
                        if (!found_match) {
                            c_foreach(type_member, vec_interface_member, my type.interface.members) {
                                if (interface_key_extends(extends_member.ref, type_member.ref)) {
                                    found_match = true;
                                    break;
                                }
                            }
                        }
                        if (!found_match) {
                            vec_interface_member_push(&members, interface_member_clone(*extends_member.ref));
                        }
                    }
                }
            }

            if (!vec_interface_member_empty(&members)) {
                c_foreach(i, vec_interface_member, my type.interface.members) {
                    vec_interface_member_push(&members, interface_member_clone(*i.ref));
                }
            }

            if (my type_resolve_state == RESOLVE_STATE_IN_FLIGHT) {
                if (vec_interface_member_empty(&members)) {
                    my resolved_type = type_ref(&my type);
                } else {
                    my resolved_type = type_interface((Interface){ .members = members });
                }
                my type_resolve_state = RESOLVE_STATE_DONE;
            }
            break;

        case RESOLVE_STATE_IN_FLIGHT:
            my resolved_type = type_self_referential_error;
            my type_resolve_state = RESOLVE_STATE_DONE;
            break;

        case RESOLVE_STATE_DONE: break;
    }

    return &my resolved_type;
}

void type_decl_typecheck(TypeDecl* me, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);

    if (my is_interface) {
        ASSERT(my type.tag == TYPE_INTERFACE);
        c_foreach(extends, vec_spanned_type, my interface_extends) {
            type_typecheck(&extends.ref->type, errs);
            Type const* resolved = type_resolve(&extends.ref->type);
            if (extends.ref->type.tag != TYPE_IDENTIFIER) {
                error_ctx_error(
                    errs,
                    extends.ref->span,
                    2499,
                    "An interface can only extend an identifier/qualified-name with optional type arguments."
                );
            } else if (resolved->tag != TYPE_INTERFACE) {
                /*
                error_ctx_error(
                    errs,
                    extends.ref->span,
                    2312,
                    "An interface can only extend an object type or intersection of object types with statically known members."
                );
                */
            } else {
                /*
                if (!type_extends(&i.ref->type, &my type)) {
                    error_ctx_error(
                        errs,
                        my name,
                        2430,
                        "Interface '%.*s' incorrectly extends interface '%.*s'.",
                        c_SV(span_sv(&my name)),
                        c_SV(span_sv(&i.ref->type.identifier->name))
                    );
                }
                */
            }
            //my effective_type = type_composite(my effective_type, type_clone(extends.ref->type), TYPE_INTERSECTION);
        }
    } else {
        type_typecheck(&my type, errs);
    }
}
