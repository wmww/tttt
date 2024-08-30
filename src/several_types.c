#include "several_types.h"
#include "name_stack.h"
#include "error.h"

// Identifier

void identifier_type_drop(IdentifierType* me) {
    vec_span_drop(&my namespaces);
    vec_spanned_type_drop(&my generic_params);
    lazy_type_drop(&my type);
}

IdentifierType identifier_type_clone(IdentifierType me) {
    return (IdentifierType){
        .namespaces = vec_span_clone(me.namespaces),
        .name = me.name,
        .full_span = me.full_span,
        .named_type = me.named_type,
        .generic_params = vec_spanned_type_clone(me.generic_params),
        // PERF: perhaps this should clone the type if already evaluated?
        .type = (LazyType){ .tag = LAZY_TYPE_IDENTIFIER_TYPE },
    };
}

void identifier_type_resolve_names(IdentifierType* me, NameStack* names, ErrorCtx* errs) {
    if (!my named_type) {
        NameStack const* namespace = names;
        c_foreach(i, vec_span, my namespaces) {
            namespace = name_stack_get_namespace(namespace, i.ref->sv);
            if (!namespace) {
                error_ctx_error(errs, *i.ref, 2503, "Cannot find namespace '%.*s'.", c_SPAN(*i.ref));
                break;
            }
        }
        if (namespace) {
            my named_type = name_stack_get_type(namespace, my name.sv);
            if (!my named_type) {
                if (vec_span_size(&my namespaces)) {
                    error_ctx_error(
                        errs,
                        my name,
                        2694,
                        "Namespace '%.*s' has no exported member '%.*s'.",
                        c_SPAN(*vec_span_back(&my namespaces)),
                        c_SPAN(my name)
                    );
                } else {
                    error_ctx_error(
                        errs,
                        my name,
                        2304,
                        "Cannot find name '%.*s'.",
                        c_SPAN(my name)
                    );
                }
            }
        }
    }
    c_foreach(i, vec_spanned_type, my generic_params) {
        type_resolve_names(&i.ref->type, names, errs);
    }
}

Type identifier_type_evaluate_type(IdentifierType* me) {
    if (my named_type) {
        if (my named_type->generic) {
            return generic_degenericize_type(
                my named_type->generic,
                type_clone(my named_type->type),
                &my generic_params
            );
        } else {
            return type_ref(&my named_type->type);
        }
    } else {
        return type_any;
    }
}

void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs) {
    c_foreach(i, vec_spanned_type, my generic_params) {
        type_typecheck(&i.ref->type, errs);
    }
    if (my named_type) {
        /*
        //This can be enabled once we're parsing the standard library
        if (errs && vec_spanned_type_size(&my generic_params) && generic_empty(my named_type->generic)) {
            error_ctx_error(errs, my full_span, 2315, "Type '%.*s' is not generic.", c_SPAN(my name));
        }
        Type const* t = type_resolve(lazy_type_resolve(&my named_type->type));
        if (t->tag == TYPE_ANY && t->any.excessively_deep_error) {
            error_ctx_error(errs, my full_span, 2589, "Type instantiation is excessively deep and possibly infinite.");
        }
        */
    }
}

// Inferred identifier

void inferred_type_drop(InferredType* me) {
    type_drop(&my extends);
    named_type_destroy(my named_type);
}

// Indexed

void indexed_type_drop(IndexedType* me) {
    type_drop(&my interface);
    type_drop(&my index);
    type_drop(&my cached);
}

IndexedType indexed_type_clone(IndexedType me) {
    return (IndexedType){
        .interface = type_clone(me.interface),
        .index = type_clone(me.index),
        .cached = {0},
    };
}

void indexed_type_resolve_names(IndexedType* me, NameStack* names, ErrorCtx* errs) {
    type_resolve_names(&my interface, names, errs);
    type_resolve_names(&my index, names, errs);
}

void indexed_type_typecheck(IndexedType const* me, ErrorCtx* errs) {
    type_typecheck(&my interface, errs);
    type_typecheck(&my index, errs);
}

// Keyof

void keyof_type_drop(KeyofType* me) {
    type_drop(&my interface);
    type_drop(&my cached);
}

KeyofType keyof_type_clone(KeyofType me) {
    return (KeyofType){
        .interface = type_clone(me.interface),
        .has_cache = false,
        .cached = {0},
    };
}

void keyof_type_resolve_names(KeyofType* me, NameStack* names, ErrorCtx* errs) {
    type_resolve_names(&my interface, names, errs);
}

void keyof_type_typecheck(KeyofType const* me, ErrorCtx* errs) {
    type_typecheck(&my interface, errs);
}

// Ternary

void ternary_type_drop(TernaryType* me) {
    type_drop(&my maybe_sub);
    type_drop(&my maybe_super);
    type_drop(&my if_true);
    type_drop(&my if_false);
    type_drop(&my cached);
}

TernaryType ternary_type_clone(TernaryType me) {
    return (TernaryType) {
        .maybe_sub = type_clone(me.maybe_sub),
        .maybe_super = type_clone(me.maybe_super),
        .if_true = type_clone(me.if_true),
        .if_false = type_clone(me.if_false),
        .cached = {0},
    };
}

// Abstracted

void abstracted_type_drop(AbstractedType* me) {
    type_drop(&my inner);
    type_drop(&my cached);
}

AbstractedType abstracted_type_clone(AbstractedType me) {
    me.inner = type_clone(me.inner);
    me.has_cache = false;
    me.cached = (Type){0};
    return me;
}

Type const* abstracted_type_resolve(AbstractedType* me) {
    if (!my has_cache) {
        // PERF: figure out how to avoid cloning if no abstracting is needed
        my cached = type_clone(my inner);
        my has_cache = true;
        if (my abstract_literals) {
            type_abstract_literals(&my cached);
        }
    }
    return &my cached;
}
