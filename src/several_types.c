#include "several_types.h"
#include "ty/ty.h"
#include "lazy_type.h"
#include "scope.h"
#include "generic.h"
#include "error.h"

// Identifier

void identifier_type_drop(IdentifierType* me) {
    vec_span_drop(&my namespaces);
    vec_symbol_cptr_drop(&my namespace_symbols);
    vec_spanned_type_drop(&my generic_params);
}

IdentifierType identifier_type_clone(IdentifierType me) {
    me.namespaces = vec_span_clone(me.namespaces);
    me.namespace_symbols = vec_symbol_cptr_clone(me.namespace_symbols);
    me.generic_params = vec_spanned_type_clone(me.generic_params);
    return me;
}

// Member of a namespace by meaning, TS2694 if there is none. *found is the member as declared (maybe an alias)
static Symbol const* get_symbol_from_namespace(
    Symbol const* namespace,
    Span namespace_name,
    Span item_name,
    u32 meaning,
    Symbol const** found,
    ErrorCtx* errs
) {
    *found = nullptr;
    if (!namespace) return nullptr;
    Symbol const* member = symbol_get_export(namespace, item_name.sv);
    Symbol const* decl = symbol_for_meaning(member, meaning);
    if (!decl && meaning == SYMBOL_TYPE) decl = symbol_for_meaning(member, SYMBOL_ANY_MEANING); // TS2749 below
    if (!decl) {
        error_ctx_error_d(errs, item_name, 2694,
            "Namespace '", namespace_name, "' has no exported member '", item_name, "'.");
        return nullptr;
    }
    *found = member;
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

void identifier_type_lookup_names(IdentifierType* me, Scope* names, ErrorCtx* errs) {
    check(!my target);

    if (vec_span_size(&my namespaces)) {
        Symbol const* namespace = nullptr;
        Span prev_name = span_null;
        for_each (i, vec_span, &my namespaces) {
            Symbol const* decl;
            Symbol const* found;
            if (i == my namespaces.data /* First namespaces */) {
                decl = scope_get_namespace(names, i->sv, &found);
                if (!decl) {
                    error_ctx_error_d(errs, *i, 2503, "Cannot find namespace '", *i, "'.");
                }
            } else /* Subsequent namespaces */ {
                decl = get_symbol_from_namespace(namespace, prev_name, *i, SYMBOL_NAMESPACE, &found, errs);
            }
            // Everything looked up in a placeholder is any
            namespace = decl && !decl->is_placeholder ? decl : nullptr;
            prev_name = *i;
            if (!decl) {
                break;
            }
            vec_symbol_cptr_push(&my namespace_symbols, found);
        }
        if (namespace) {
            my target = get_symbol_from_namespace(namespace, prev_name, my name, SYMBOL_TYPE, &my symbol, errs);
        }
    } else if (my keyword) {
        if (my keyword == KEYWORD_IDENTIFIER_THIS) {
            ThisRef this_ref = scope_get_this(names, true);
            my this_kind = this_ref.kind;
            my target = this_ref.symbol;
        }
    } else {
        my target = scope_get_type(names, my name.sv, &my symbol);
        if (!my target) my target = scope_get_value(names, my name.sv, &my symbol); // TS2749 below
        if (!my target) {
            error_ctx_error_d(errs, my name, 2304, "Cannot find name '", my name, "'.");
        }
    }

    if (my target && !my this_kind && !my target->is_placeholder && !(my target->flags & SYMBOL_TYPE)) {
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

void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs) {
    for_each (i, vec_spanned_type, &my generic_params) {
        type_typecheck(&i->type, errs);
    }
    if (my target) {
        /*
        //This can be enabled once we're parsing the standard library
        if (errs && vec_spanned_type_size(&my generic_params) && generic_empty(my named_type->generic)) {
            error_ctx_error_d(errs, my full_span, 2315, "Type '", my name, "' is not generic.");
        }
        TypeNode const* t = type_resolve(lazy_type_resolve(&my named_type->type));
        if (t->tag == TYPE_ANY && t->any.excessively_deep_error) {
            error_ctx_error_d(errs, my full_span, 2589, "Type instantiation is excessively deep and possibly infinite.");
        }
        */
    }
}

// Inferred identifier

void inferred_type_drop(InferredType* me) {
    type_drop(&my extends);
}

InferredType inferred_type_clone(InferredType me) {
    me.extends = type_clone(me.extends);
    me.owned_symbol = nullptr;
    return me;
}

// Indexed

void indexed_type_drop(IndexedType* me) {
    type_drop(&my interface);
    type_drop(&my index);
}

IndexedType indexed_type_clone(IndexedType me) {
    return (IndexedType){
        .interface = type_clone(me.interface),
        .index = type_clone(me.index),
        .is_from_binding = me.is_from_binding,
        .array_only = me.array_only,
        .rest = me.rest,
        .default_value = me.default_value,
    };
}

void indexed_type_lookup_names(IndexedType* me, Scope* names, ErrorCtx* errs) {
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
}

KeyofType keyof_type_clone(KeyofType me) {
    return (KeyofType){
        .interface = type_clone(me.interface),
    };
}

void keyof_type_lookup_names(KeyofType* me, Scope* names, ErrorCtx* errs) {
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

void typeof_type_lookup_names(TypeofType* me, Scope* names, ErrorCtx* errs) {
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
}

TernaryType ternary_type_clone(TernaryType me) {
    return (TernaryType) {
        .maybe_sub = type_clone(me.maybe_sub),
        .maybe_super = type_clone(me.maybe_super),
        .if_true = type_clone(me.if_true),
        .if_false = type_clone(me.if_false),
    };
}
