#include "interface_decl.h"
#include "error.h"
#include "several_types.h"
#include "interface_member.h"
#include "interface.h"
#include "type.h"
#include "name_stack.h"

void interface_decl_drop(InterfaceDecl* me) {
    generic_drop(&my generic);
    vec_spanned_type_drop(&my extends);
    type_drop(&my type);
    lazy_type_drop(&my lazy_type);
    named_type_destroy(my named_type);
}

void interface_decl_add_names(InterfaceDecl* me, NameStack* names, ErrorCtx* errs) {
    ignore errs;
    ASSERT(!my named_type);
    my named_type = name_stack_add_mergeable_type(
        names,
        my name,
        my full_span,
        type_lazy(&my lazy_type),
        &my generic,
        false
    );
}

void interface_decl_resolve_names(InterfaceDecl* me, NameStack* names, ErrorCtx* errs) {
    my lazy_type.tag = LAZY_TYPE_INTERFACE_DECL;
    Namespace generic_ns = namespace_init();
    namespace_generic_push_to_stack(&generic_ns, &my generic, names);
    c_foreach(i, vec_spanned_type, my extends) {
        type_resolve_names(&i.ref->type, names, errs);
    }
    Namespace block_ns = namespace_init();
    namespace_push_to_stack(&block_ns, names);
    name_stack_add_type_s(names, cstr_lit("this"), my named_type);
    type_resolve_names(&my type, names, errs);
    namespace_drop(&block_ns);
    namespace_drop(&generic_ns);
}

Type interface_decl_evaluate_type(InterfaceDecl* me) {
    ASSERT(my type.tag == TYPE_INTERFACE);

    VecInterfaceMember members = {0};

    c_foreach(extends, vec_spanned_type, my extends) {
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

    if (vec_interface_member_empty(&members)) {
        return type_ref(&my type);
    } else {
        return type_interface((Interface){ .members = members });
    }
}

void interface_decl_typecheck(InterfaceDecl* me, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);

    ASSERT(my type.tag == TYPE_INTERFACE);
    c_foreach(extends, vec_spanned_type, my extends) {
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

    type_typecheck(&my type, errs);
    interface_typecheck_members_against(&my type.interface, my named_type, errs);
}
