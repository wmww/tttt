#include "declaration.h"
#include "generic.h"

static void named_variable_drop(NamedVariable* me) {
    type_drop(&my type);
}

static void named_type_drop(NamedType* me) {
    type_drop(&my type);
    vec_type_drop(&my merged_types);
    lazy_type_drop(&my lazy_type);
}

Type named_type_evaluate_type(NamedType* me) {
    // This should only be called when evaluating our LazyType, and our
    // LazyType should only be used when there are multiple types to merge
    ASSERT(my is_merged && vec_type_size(&my merged_types) > 1);
    Type ret = interface_from_merged(&my merged_types);
    vec_type_drop(&my merged_types);
    my merged_types = vec_type_init();
    return ret;
}

void declaration_drop(Declaration* me) {
    vec_span_drop(&my declarations);
    named_variable_drop(&my value);
    named_type_drop(&my type);
}

void declaration_init(Declaration* me, DeclProps props) {
    PANIC_IF(props.tag == DECLARATION_NONE, "props.tag not set");
    if (!span_valid(my name)) {
        my name = props.name;
    }
    if (my tag == DECLARATION_NONE) {
        my tag = props.tag;
    }
    vec_span_push(&my declarations, props.full);
    if (declaration_tag_has_value(props.tag)) {
        if (!my has_value) {
            my has_value = true;
            my value.name = props.name;
            my value.type = props.value_type;
            my value.generic = (props.generic && !generic_empty(props.generic)) ? props.generic : nullptr;
        } else {
            NONFATAL("Tried to add multiple values to declaration %.*s", c_SPAN(my name));
            type_drop(&props.value_type);
        }
    }
    if (declaration_tag_has_type(props.tag)) {
        if (!my has_type) {
            my has_type = true;
            my type.name = props.name;
            my type.type = props.declared_type;
            my type.generic = (props.generic && !generic_empty(props.generic)) ? props.generic : nullptr;
            my type.is_typeof_class = props.is_typeof;
        } else {
            // Merge declarations
            // TODO: check generics are equal
            if (!my type.is_merged) {
                // Previously this was not a merged declaration, so we have some setup to do
                // 1. move the previous type into the merged vec
                // 2. set up our lazy type
                // 3. make our type refer to our lazy type
                // Resolving our type later (in the typecheck phase) will then result in named_type_evaluate_type() being
                // called, which will do the actual merging
                vec_type_push(&my type.merged_types, my type.type);
                my type.lazy_type.tag = LAZY_TYPE_NAMED_TYPE;
                my type.type = type_lazy(&my type.lazy_type);
                my type.is_merged = true;
            }
            vec_type_push(&my type.merged_types, props.declared_type);
        }
    }
}
