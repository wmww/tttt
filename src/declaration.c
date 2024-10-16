#include "declaration.h"
#include "generic.h"

static void named_type_drop(NamedType* me) {
    type_drop(&my type);
    vec_type_drop(&my merged_types);
    lazy_type_drop(&my lazy_type);
}

Type named_type_evaluate_type(NamedType* me) {
    Type ret = interface_from_merged(&my merged_types);
    return ret;
}

void declaration_drop(Declaration* me) {
    vec_span_drop(&my declarations);
    type_drop(&my value_type);
    named_type_drop(&my type);
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
    }
    if (declaration_tag_has_type(props.tag)) {
        if (!my type_tag) {
            my type_tag = props.tag;
            my type.generic = generic_or_null(props.generic);;
            my type.lazy_type.tag = LAZY_TYPE_NAMED_TYPE;
            my type.type = type_lazy(&my type.lazy_type);
            my type.is_typeof_class = props.is_typeof;
        }
        PANIC_IF(my type.lazy_type.tag != LAZY_TYPE_NAMED_TYPE,
            "Type added to declaration after LazyType already evaluated");
        vec_type_push(&my type.merged_types, props.declared_type);
    }
}
