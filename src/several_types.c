#include "several_types.h"
#include "ty/ty.h"
#include "scope_stack.h"
#include "declaration.h"
#include "scope.h"
#include "generic.h"
#include "error.h"

// Identifier

void identifier_type_drop(IdentifierType* me) {
    vec_span_drop(&my namespaces);
    vec_declaration_cptr_drop(&my namespace_decls);
    vec_spanned_type_drop(&my generic_params);
    lazy_type_drop(&my type);
}

IdentifierType identifier_type_clone(IdentifierType me) {
    me.namespaces = vec_span_clone(me.namespaces);
    me.namespace_decls = vec_declaration_cptr_clone(me.namespace_decls);
    me.generic_params = vec_spanned_type_clone(me.generic_params);
    // PERF: perhaps this should clone the type if already evaluated?
    me.type = lazy_type_init_inline(LAZY_TYPE_IDENTIFIER_TYPE);
    return me;
}

static Declaration const* get_declaration_from_namespace(
    Scope const* namespace,
    Span namespace_name,
    Span item_name,
    bool namespace_only,
    ErrorCtx* errs
) {
    if (!namespace) return nullptr;
    Declaration const* decl = scope_get_declaration(namespace, item_name);
    if (!decl || (namespace_only && !decl->namespace)) {
        error_ctx_error_d(errs, item_name, 2694,
            "Namespace '", namespace_name, "' has no exported member '", item_name, "'.");
        decl = nullptr;
    }
    return decl;
}

static cstr identifier_type_make_name_str(IdentifierType const* me) {
    cstr ret = cstr_null;
    for_each (i, vec_span, &my namespaces) {
        cstr_append_span(&ret, *i);
        cstr_append_lit(&ret, ".");
    }
    cstr_append_span(&ret, my name);
    return ret;
}

void identifier_type_lookup_names(IdentifierType* me, ScopeStack* names, ErrorCtx* errs) {
    check(!my decl_ref);

    if (vec_span_size(&my namespaces)) {
        Scope const* namespace = nullptr;
        Span prev_name = span_null;
        for_each (i, vec_span, &my namespaces) {
            Declaration const* decl;
            if (i == my namespaces.data /* First namespaces */) {
                decl = scope_stack_get_namespace(names, i->sv);
                if (!decl) {
                    error_ctx_error_d(errs, *i, 2503, "Cannot find namespace '", *i, "'.");
                }
            } else /* Subsequent namespaces */ {
                decl = get_declaration_from_namespace(namespace, prev_name, *i, true, errs);
            }
            namespace = decl ? decl->namespace :nullptr;
            prev_name = *i;
            if (!decl) {
                break;
            }
            vec_declaration_cptr_push(&my namespace_decls, decl);
        }
        if (namespace) {
            my decl_ref = get_declaration_from_namespace(namespace, prev_name, my name, false, errs);
        }
    } else if (my keyword) {
        my decl_ref = scope_stack_get_keyword(names, my keyword, true);
    } else {
        my decl_ref = scope_stack_get_type(names, my name.sv);
        if (!my decl_ref) {
            error_ctx_error_d(errs, my name, 2304, "Cannot find name '", my name, "'.");
        }
    }

    if (my decl_ref && !my decl_ref->type_tag) {
        cstr name_str = identifier_type_make_name_str(me);
        Span error_span = span_between(vec_span_size(&my namespaces) ? *my namespaces.data : my name, my name);
        error_ctx_error_d(errs, error_span, 2749,
            "'", name_str, "' refers to a value, but is being used as a type here. Did you mean 'typeof ",
            name_str, "'?");
        cstr_drop(&name_str);
    }

    for_each (i, vec_spanned_type, &my generic_params) {
        type_lookup_names(&i->type, names, errs);
    }
}

Type identifier_type_evaluate_type(IdentifierType* me) {
    if (my decl_ref && ty_declaration_is_evaluated_by_new_core(my decl_ref)) {
        return type_new(ty_from_identifier_type(current_checker, me));
    }
    if (my decl_ref) {
        if (my decl_ref->type_generic) {
            Type ret = generic_degenericize_type(
                my decl_ref->type_generic,
                type_clone(my decl_ref->declared_type),
                &my generic_params
            );
            return ret;
        } else {
            return type_refcopy(&my decl_ref->declared_type);
        }
    } else {
        return type_any;
    }
}

void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs) {
    for_each (i, vec_spanned_type, &my generic_params) {
        type_typecheck(&i->type, errs);
    }
    if (my decl_ref) {
        /*
        //This can be enabled once we're parsing the standard library
        if (errs && vec_spanned_type_size(&my generic_params) && generic_empty(my named_type->generic)) {
            error_ctx_error_d(errs, my full_span, 2315, "Type '", my name, "' is not generic.");
        }
        Type const* t = type_resolve(lazy_type_resolve(&my named_type->type));
        if (t->tag == TYPE_ANY && t->any.excessively_deep_error) {
            error_ctx_error_d(errs, my full_span, 2589, "Type instantiation is excessively deep and possibly infinite.");
        }
        */
    }
}

// Inferred identifier

void inferred_type_drop(InferredType* me) {
    type_drop(&my extends);
    arc_declaration_drop(&my owned_declaration);
}

InferredType inferred_type_clone(InferredType me) {
    me.extends = type_clone(me.extends);
    me.owned_declaration = arc_declaration_init();
    return me;
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

void indexed_type_lookup_names(IndexedType* me, ScopeStack* names, ErrorCtx* errs) {
    type_lookup_names(&my interface, names, errs);
    type_lookup_names(&my index, names, errs);
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

void keyof_type_lookup_names(KeyofType* me, ScopeStack* names, ErrorCtx* errs) {
    type_lookup_names(&my interface, names, errs);
}

void keyof_type_typecheck(KeyofType const* me, ErrorCtx* errs) {
    type_typecheck(&my interface, errs);
}

// Typeof

void typeof_type_drop(TypeofType* me) {
    if (my owns_expr) {
        expr_drop(my expr);
        free(my expr);
    }
}

TypeofType typeof_type_clone(TypeofType me) {
    me.owns_expr = false;
    return me;
}

void typeof_type_lookup_names(TypeofType* me, ScopeStack* names, ErrorCtx* errs) {
    expr_lookup_names(my expr, names, errs);
}

void typeof_type_typecheck(TypeofType const* me, ErrorCtx* errs) {
    expr_typecheck(my expr, errs);
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
