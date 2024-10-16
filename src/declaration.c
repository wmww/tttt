#include "declaration.h"
#include "generic.h"

void declaration_drop(Declaration* me) {
    vec_span_drop(&my declarations);
    type_drop(&my value_type);
    type_drop(&my declared_type);
    vec_type_drop(&my declared_type_list);
    lazy_type_drop(&my declared_type_lazy);
}

Type declaration_evaluate_declared_type(Declaration const* me) {
    return interface_from_merged(&my declared_type_list);
}

static inline Generic const* generic_or_null(Generic const* generic) {
    return (generic && !generic_empty(generic)) ? generic : nullptr;
}

void declaration_init(Declaration* me, DeclProps props) {
    PANIC_IF(props.tag == DECLARATION_NONE, "props.tag not set");
    if (!span_valid(my name)) {
        my name = props.name;
    }
    vec_span_push(&my declarations, props.full);

    if (declaration_tag_has_value(props.tag)) {
        if (!my value_tag) {
            my value_tag = props.tag;
            my value_type = props.value_type;
            my value_generic = generic_or_null(props.generic);
        } else {
            NONFATAL("Tried to add multiple values to declaration %.*s", c_SPAN(my name));
            type_drop(&props.value_type);
        }
    } else if (props.value_type.tag) {
        PANIC("declaration_init() called with tag %d and non-empty value type", props.tag);
    }

    if (declaration_tag_has_type(props.tag)) {
        if (!my type_tag) {
            my type_tag = props.tag;
            my type_generic = generic_or_null(props.generic);;
            my declared_type_lazy.tag = LAZY_TYPE_DECLARATION;
            my declared_type = type_lazy(&my declared_type_lazy);
            my is_typeof = props.is_typeof;
        }
        PANIC_IF(my declared_type_lazy.tag != LAZY_TYPE_DECLARATION,
            "Type added to declaration after LazyType already evaluated");
        vec_type_push(&my declared_type_list, props.declared_type);
    } else if (props.declared_type.tag) {
        PANIC("declaration_init() called with tag %d and non-empty declared type", props.tag);
    }

    if (declaration_tag_has_namespace(props.tag)) {
        ASSERT(props.namespace);
        if (!my namespace) {
            my namespace = props.namespace;
        } else {
            NONFATAL("Tried to add multiple namespaces to declaration %.*s", c_SPAN(my name));
        }
    } else if (props.namespace) {
        PANIC("declaration_init() called with tag %d and non-null namespace", props.tag);
    }
}
