#include "several_types.h"
#include "interface_decl.h"
#include "flow.h"
#include "source_file.h"
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

// Member of a namespace by meaning, TS2694 if there is none. *found is the member as declared (maybe an alias)
static Symbol const* get_symbol_from_namespace(
    Symbol const* namespace,
    csview namespace_name,
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

// `import("./a").A.B`, `typeof import("./a")`: the names are members of the module
static void import_type_lookup_names(IdentifierType* me, Scope* names, ErrorCtx* errs) {
    my import_prop_start = -1;
    if (!my import_ref) return; // The parser complained
    Symbol* module = binder_module_of_ref(names->binder, my import_ref);
    if (!module) {
        if (my import_ref->file) {
            error_ctx_error_d(errs, my import_ref->span, 2306,
                "File '", source_file_display_path(my import_ref->file), "' is not a module.");
        } else {
            module_ref_report_unresolved(my import_ref, errs);
        }
        return;
    }
    if (module->is_shorthand_module) return;
    Symbol const* container = module_export_equals(module);
    if (!span_valid(my name)) {
        if (my import_typeof) {
            my target = container;
            my symbol = container;
        } else if (container != module && symbol_for_meaning(container, SYMBOL_TYPE)) {
            // `export = C`
            my target = symbol_for_meaning(container, SYMBOL_TYPE);
            my symbol = container;
        } else {
            error_ctx_error_d(errs, my import_span, 1340,
                "Module '", my import_ref->path, "' does not refer to a type, but is used as a type here. Did you mean 'typeof import('",
                my import_ref->path, "')'?");
        }
        return;
    }
    if (my import_typeof) {
        // A value: exports as far as they go, properties from there on
        Symbol const* current = container;
        isize count = vec_span_size(&my namespaces) + 1;
        for (isize i = 0; i < count; i++) {
            bool last = i == count - 1;
            Span name = last ? my name : *vec_span_at(&my namespaces, i);
            Symbol const* members = symbol_for_meaning(current, SYMBOL_NAMESPACE);
            if (members && members->is_placeholder) return;
            Symbol const* member = members ? symbol_get_export(members, name.sv) : nullptr;
            Symbol const* value = symbol_for_meaning(member, last ? SYMBOL_VALUE : SYMBOL_VALUE | SYMBOL_NAMESPACE);
            if (!value) {
                if (i == 0 || !(current->flags & SYMBOL_VALUE)) {
                    cstr container_name = current == module ?
                        module_name_for_error(module, nullptr, false) : cstr_from_sv(current->name);
                    error_ctx_error_d(errs, name, 2694,
                        "Namespace '", container_name, "' has no exported member '", name, "'.");
                    cstr_drop(&container_name);
                    return;
                }
                my import_prop_start = (int)i;
                break;
            }
            if (last) my symbol = member;
            else vec_symbol_cptr_push(&my namespace_symbols, member);
            current = value;
        }
        my target = symbol_for_meaning(current, SYMBOL_VALUE);
        return;
    }

    cstr module_name = container == module ? module_name_for_error(module, nullptr, false) : cstr_from_sv(container->name);
    csview prev_name = cstr_sv(&module_name);
    for_each (i, vec_span, &my namespaces) {
        Symbol const* found;
        container = get_symbol_from_namespace(container, prev_name, *i, SYMBOL_NAMESPACE, &found, errs);
        if (!container || container->is_placeholder) {
            cstr_drop(&module_name);
            return;
        }
        vec_symbol_cptr_push(&my namespace_symbols, found);
        prev_name = i->sv;
    }
    my target = get_symbol_from_namespace(
        container, prev_name, my name, my import_typeof ? SYMBOL_VALUE : SYMBOL_TYPE, &my symbol, errs
    );
    cstr_drop(&module_name);
}

void identifier_type_lookup_names(IdentifierType* me, Scope* names, ErrorCtx* errs) {
    check(!my target);

    if (my import_ref || span_valid(my import_span)) {
        import_type_lookup_names(me, names, errs);
        for_each (i, vec_spanned_type, &my generic_params) {
            type_lookup_names(&i->type, names, errs);
        }
        return;
    }

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
                decl = get_symbol_from_namespace(namespace, prev_name.sv, *i, SYMBOL_NAMESPACE, &found, errs);
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
            my target = get_symbol_from_namespace(namespace, prev_name.sv, my name, SYMBOL_TYPE, &my symbol, errs);
        }
    } else if (my keyword) {
        if (my keyword == KEYWORD_IDENTIFIER_THIS) {
            ThisRef this_ref = scope_get_this(names, true);
            my this_kind = this_ref.kind;
            my target = this_ref.symbol;
            if (this_ref.interface) this_ref.interface->contains_this = true;
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
    if (my keyword == KEYWORD_IDENTIFIER_THIS && my this_kind != THIS_CLASS_INSTANCE && my this_kind != THIS_INTERFACE) {
        error_ctx_error_d(errs, my name, 2526, "A 'this' type is available only in a non-static member of a class or interface.");
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

// Indexed

void indexed_type_drop(IndexedType* me) {
    type_drop(&my interface);
    type_drop(&my index);
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

void typeof_type_lookup_names(TypeofType* me, Scope* names, ErrorCtx* errs) {
    bool saved = names->flow->in_type_query;
    names->flow->in_type_query = true;
    expr_lookup_names(my expr, names, errs);
    names->flow->in_type_query = saved;
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

