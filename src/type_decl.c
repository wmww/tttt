#include "type_decl.h"
#include "error.h"
#include "identifier_type.h"
#include "interface_member.h"
#include "interface.h"
#include "type.h"

void type_decl_drop(TypeDecl* me) {
    generic_drop(&my generic);
    vec_spanned_type_drop(&my interface_extends);
    type_drop(&my type);
}

void cstr_append_type_decl_debug(cstr* target, TypeDecl* me, int indent) {
    if (my is_interface) {
        cstr_append_sv(target, c_sv("interface " CLR_NAME));
        cstr_append_span(target, &my name);
        cstr_append_sv(target, c_sv(CLR0));
        cstr_append_generic_debug(target, &my generic, indent);
        if (!vec_spanned_type_empty(&my interface_extends)) {
            cstr_append_sv(target, c_sv(" extends "));
            c_foreach(i, vec_spanned_type, my interface_extends) {
                cstr_append_type_debug(target, &i.ref->type, indent);
                if (i.ref != i.end - 1) {
                    cstr_append_sv(target, c_sv(", "));
                }
            }
        }
        cstr_append_sv(target, c_sv(" "));
    } else {
        cstr_append_sv(target, c_sv("type " CLR_NAME));
        cstr_append_span(target, &my name);
        cstr_append_sv(target, c_sv(CLR0));
        cstr_append_generic_debug(target, &my generic, indent);
        cstr_append_sv(target, c_sv(" = "));
    }
    cstr_append_type_debug(target, &my type, indent);
}

LazyType type_decl_unresolved_lazy_type(TypeDecl* me) {
    if (!my is_interface || vec_spanned_type_empty(&my interface_extends)) {
        return lazy_type_ref(&my type);
    } else {
        return lazy_type_interface(me);
    }
}

Type type_decl_make_interface_type(TypeDecl* me) {
    ASSERT(my is_interface);
    Type ret = type_composite_base(TYPE_INTERSECTION);
    c_foreach(i, vec_spanned_type, my interface_extends) {
        ret = type_composite(TYPE_INTERSECTION, ret, type_clone(i.ref->type));
    }
    ret = type_composite(TYPE_INTERSECTION, ret, type_clone(my type));
    return ret;
}

void type_decl_typecheck(TypeDecl* me, ErrorCtx* errs) {
    if (my is_interface) {
        ASSERT(my type.tag == TYPE_INTERFACE);
        c_foreach(extends, vec_spanned_type, my interface_extends) {
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
    }
}
