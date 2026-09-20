#include "declaration.h"
#include "generic.h"
#include "ty/ty.h"
#include "error.h"

void merged_type_add(MergedType* me, TypeNode t) {
    switch (my lazy.tag) {
        case LAZY_TYPE_UNINITIALIZED:
            my lazy = lazy_type_preeval(t);
            break;

        case LAZY_TYPE_EVALUATED:
            vec_type_push(&my merge_list, my lazy.preevaluated);
            my lazy = lazy_type_init(LAZY_TYPE_MERGED);
            // fallthrough

        case LAZY_TYPE_MERGED:
            vec_type_push(&my merge_list, t);
            break;

        default: impossible;
    }
}

MergedType merged_type_clone(MergedType me) {
    me.merge_list = vec_type_clone(me.merge_list);
    if (me.lazy.tag == LAZY_TYPE_EVALUATED) {
        me.lazy.preevaluated = type_clone(me.lazy.preevaluated);
    } else if (me.lazy.tag == LAZY_TYPE_MERGED) {
        me.lazy = lazy_type_init(LAZY_TYPE_MERGED); // The clone is a different type and needs its own slot
    }
    return me;
}

Ty const* merged_type_evaluate_type(MergedType* me) {
    if (my declared_owner) return ty_declared_type_of(current_checker, my declared_owner);
    if (my value_owner) return ty_merged_value_type(current_checker, my value_owner);
    log_error("merged type without an owner");
    return ty_intrinsic(current_checker, TY_ERROR);
}

void merged_type_drop(MergedType* me) {
    vec_type_drop(&my merge_list);
    lazy_type_drop(&my lazy);
}

void declaration_drop(Declaration* me) {
    vec_decl_node_drop(&my nodes);
    vec_span_drop(&my declarations);
    vec_span_drop(&my namespaces);
    type_drop(&my value_type);
    type_drop(&my declared_type);
    merged_type_drop(&my merged_value_type);
    merged_type_drop(&my merged_declared_type);
}

void declaration_clone_in_place(Declaration* me) {
    my nodes = vec_decl_node_clone(my nodes);
    my declarations = vec_span_clone(my declarations);
    my namespaces = vec_span_clone(my namespaces);
    my value_type = type_clone(my value_type);
    my merged_value_type = merged_type_clone(my merged_value_type);
    if (my value_type.tag) {
        my value_type = type_lazy(&my merged_value_type.lazy);
        my merged_value_type.value_owner = me;
    }
    my merged_declared_type = merged_type_clone(my merged_declared_type);
    if (my merged_declared_type.declared_owner) my merged_declared_type.declared_owner = me;
    if (my declared_type.tag) {
        my declared_type = type_lazy(&my merged_declared_type.lazy);
    }
}

static inline Generic const* generic_or_null(Generic const* generic) {
    return (generic && !generic_empty(generic)) ? generic : nullptr;
}

void declaration_init(Declaration* me, DeclProps props) {
    panic_if(props.tag == DECLARATION_NONE, "props.tag not set");
    if (!span_valid(my name)) {
        my name = props.name;
    }
    vec_span_push(&my declarations, props.full);
    if (props.node.kind) vec_decl_node_push(&my nodes, props.node);

    if (declaration_tag_has_value(props.tag) && props.value_known) {
        // A further block of a namespace, a redeclared `var`: nothing new about the value
        if (props.tag > my value_tag) my value_tag = props.tag;
    } else if (declaration_tag_has_value(props.tag)) {
        if (!my value_tag) {
            my value_generic = generic_or_null(props.generic);
            my value_type = type_lazy(&my merged_value_type.lazy);
            my merged_value_type.value_owner = me;
        }
        bool add_type = true;
        if ((my value_tag == DECLARATION_FUNCTION_SIGNATURE || my value_tag == DECLARATION_FUNCTION_IMPL) &&
            props.tag == DECLARATION_FUNCTION_IMPL
        ) {
            add_type = false;
        }
        if (props.tag > my value_tag) my value_tag = props.tag;
        if (add_type) {
            merged_type_add(&my merged_value_type, props.value_type);
        } else {
            type_drop(&props.value_type);
        }
    } else if (props.value_type.tag) {
        panic("declaration_init() called with tag ", (i32)props.tag, " and non-empty value type");
    }

    if (declaration_tag_has_type(props.tag)) {
        if (!my type_tag) {
            my type_generic = generic_or_null(props.generic);
            my declared_type = type_lazy(&my merged_declared_type.lazy);
            my is_typeof = props.is_typeof;
        }
        if (props.tag > my type_tag) my type_tag = props.tag;
        merged_type_add(&my merged_declared_type, props.declared_type);
        my merged_declared_type.declared_owner = me;
    } else if (props.declared_type.tag) {
        panic("declaration_init() called with tag ", (i32)props.tag, " and non-empty declared type");
    }

    if (declaration_tag_has_namespace(props.tag)) {
        check(props.namespace);
        if (!my namespace || props.tag > my namespace_tag) my namespace_tag = props.tag;
        my namespace = props.namespace;
    } else if (props.namespace) {
        panic("declaration_init() called with tag ", (i32)props.tag, " and non-null namespace");
    }
}
