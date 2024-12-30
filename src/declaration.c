#include "declaration.h"
#include "generic.h"
#include "error.h"

void merged_type_add(MergedType* me, Type t) {
    switch (my lazy.tag) {
        case LAZY_TYPE_EVALUATED:
            PANIC("Type added to merged type after evaluated");

        case LAZY_TYPE_UNINITIALIZED:
            my lazy.tag = LAZY_TYPE_MERGED;
            break;

        case LAZY_TYPE_MERGED: break;
        default: UNREACHABLE;
    }
    vec_type_push(&my merge_list, t);
}

MergedType merged_type_clone(MergedType me) {
    me.merge_list = vec_type_clone(me.merge_list);
    if (me.lazy.tag == LAZY_TYPE_EVALUATED) {
        me.lazy.evaluated = type_clone(me.lazy.evaluated);
    }
    return me;
}

Type merged_type_evaluate_type(MergedType* me) {
    Type ret = interface_from_merged(my merge_list);
    my merge_list = vec_type_init();
    return ret;
}

void merged_type_drop(MergedType* me) {
    vec_type_drop(&my merge_list);
    lazy_type_drop(&my lazy);
}

void declaration_drop(Declaration* me) {
    vec_span_drop(&my declarations);
    vec_span_drop(&my namespaces);
    type_drop(&my value_type);
    type_drop(&my declared_type);
    merged_type_drop(&my merged_declared_type);
}

void declaration_clone_in_place(Declaration* me) {
    my declarations = vec_span_clone(my declarations);
    my namespaces = vec_span_clone(my namespaces);
    my value_type = type_clone(my value_type);
    my merged_declared_type = merged_type_clone(my merged_declared_type);
    if (my declared_type.tag) {
        my declared_type = type_lazy(&my merged_declared_type.lazy);
    }
}

static inline Generic const* generic_or_null(Generic const* generic) {
    return (generic && !generic_empty(generic)) ? generic : nullptr;
}

DeclProps decl_props_clone(DeclProps me) {
    me.value_type = type_clone(me.value_type);
    me.declared_type = type_clone(me.declared_type);
    return me;
}

static bool declaration_types_test_mergable(
    Span a_name, DeclarationTag a_tag,
    Span b_name, DeclarationTag b_tag,
    ErrorCtx* errs
) {
    if (!a_tag || !b_tag) return true;

    bool mergable = false;

    switch (a_tag) {
        case DECLARATION_INTERFACE:
            switch (b_tag) {
                case DECLARATION_INTERFACE:
                case DECLARATION_CLASS:
                    mergable = true;
                    break;

                default: break;
            }
            break;

        case DECLARATION_CLASS:
            if (b_tag == DECLARATION_INTERFACE) {
                mergable = true;
            }
            break;

        case DECLARATION_ENUM:
            if (b_tag == DECLARATION_ENUM) {
                mergable = true;
            }
            break;

        default: break;
    }

    if (!mergable) {
        if (a_tag == DECLARATION_ENUM || b_tag == DECLARATION_ENUM) {
            error_ctx_error(errs, a_name, 2567,
                "Enum declarations can only merge with namespace or other enum declarations.");
            error_ctx_error(errs, b_name, 2567,
                "Enum declarations can only merge with namespace or other enum declarations.");
        } else {
            if (a_tag != DECLARATION_GENERIC_TYPE) {
                error_ctx_error(errs, a_name, 2300, "Duplicate identifier '%.*s'.", c_SPAN(a_name));
            }
            error_ctx_error(errs, b_name, 2300, "Duplicate identifier '%.*s'.", c_SPAN(b_name));
        }
    }

    return mergable;
}

static bool declaration_values_test_mergable(
    Span a_name, DeclarationTag a_tag,
    Span b_name, DeclarationTag b_tag,
    ErrorCtx* errs
) {
    if (!a_tag || !b_tag) {
        return true;
    } else if (a_tag == DECLARATION_ENUM || b_tag == DECLARATION_ENUM) {
        error_ctx_error(errs, a_name, 2567,
            "Enum declarations can only merge with namespace or other enum declarations.");
        error_ctx_error(errs, b_name, 2567,
            "Enum declarations can only merge with namespace or other enum declarations.");
        return false;
    } else if (
        (a_tag == DECLARATION_NAMESPACE_WITH_VALUE  && b_tag == DECLARATION_NAMESPACE_WITH_VALUE) ||
        (a_tag == DECLARATION_NAMESPACE_WITH_VALUE  && b_tag == DECLARATION_CLASS) ||
        (a_tag == DECLARATION_CLASS                 && b_tag == DECLARATION_NAMESPACE_WITH_VALUE)
    ) {
        if (a_tag == DECLARATION_NAMESPACE_WITH_VALUE && b_tag == DECLARATION_CLASS) {
            error_ctx_error(errs, a_name, 2434,
                "A namespace declaration cannot be located prior to a class or function with which it is merged.");
        }
        return true;
    } else if (a_tag == DECLARATION_FUNCTION && b_tag == DECLARATION_FUNCTION) {
        // TODO: allow function overloads
        return true;
    } else if (a_tag == DECLARATION_ARGUMENT) {
        error_ctx_error(errs, b_name, 2300, "Duplicate identifier '%.*s'.", c_SPAN(b_name));
        error_ctx_error(errs, a_name, 2300, "Duplicate identifier '%.*s'.", c_SPAN(a_name));
        return false;
    } else {
        error_ctx_error(errs, b_name, 2451,
            "Cannot redeclare block-scoped variable '%.*s'.", c_SPAN(b_name));
        error_ctx_error(errs, a_name, 2451,
            "Cannot redeclare block-scoped variable '%.*s'.", c_SPAN(a_name));
        return false;
    }
}

bool declaration_test_mergable(Declaration const* me, DeclProps const* props, ErrorCtx* errs) {
    bool types_mergable = declaration_types_test_mergable(
        my name, my type_tag,
        props->name, declaration_tag_has_type(props->tag) ? props->tag : DECLARATION_NONE,
        errs
    );

    bool values_mergable = declaration_values_test_mergable(
        my name, my value_tag,
        props->name, declaration_tag_has_value(props->tag) ? props->tag : DECLARATION_NONE,
        errs
    );

    return types_mergable && values_mergable;
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
            my type_generic = generic_or_null(props.generic);
            my declared_type = type_lazy(&my merged_declared_type.lazy);
            my is_typeof = props.is_typeof;
        }
        merged_type_add(&my merged_declared_type, props.declared_type);
    } else if (props.declared_type.tag) {
        PANIC("declaration_init() called with tag %d and non-empty declared type", props.tag);
    }

    if (declaration_tag_has_namespace(props.tag)) {
        ASSERT(props.namespace); // This may not be correct
        if (!my namespace) {
            my namespace_tag = props.tag;
            my namespace = props.namespace;
        } else {
            NONFATAL("Tried to add multiple namespaces to declaration %.*s", c_SPAN(my name));
        }
    } else if (props.namespace) {
        PANIC("declaration_init() called with tag %d and non-null namespace", props.tag);
    }
}
