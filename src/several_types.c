#include "several_types.h"
#include "port_statement.h"
#include "interface_decl.h"
#include "flow.h"
#include "source_file.h"
#include "ty/ty.h"
#include "lazy_type.h"
#include "scope.h"
#include "generic.h"
#include "error.h"
#include "unused.h"
#include "class_decl.h"
#include "statement.h"

// Identifier

void identifier_type_drop(IdentifierType* me) {
    vec_spanned_type_drop(&my generic_params);
    IdentifierTypeRare* rare = my rare;
    if (!rare) return;
    ast_spans_drop(&rare->namespaces);
    ast_symbols_drop(&rare->namespace_symbols);
    import_attributes_free(rare->import_attributes);
    if (rare->import_argument) {
        type_drop(rare->import_argument);
        ast_delete(rare->import_argument);
    }
    ast_delete(rare);
}

// Member of a namespace by meaning, TS2694 if there is none (TS2724 with a close enough name, when `suggest`: not in
// an import type). *found is the member as declared (maybe an alias)
// `shown`: the namespace as the error names it, if not `namespace` (an import type's names are the exports as
// declared, aliases included: tsc's getTypeFromImportTypeNode keeps them unresolved)
static Symbol const* get_symbol_from_namespace(
    Symbol const* namespace,
    Symbol const* shown,
    Span item_name,
    u32 meaning,
    bool suggest,
    Symbol const** found,
    ErrorCtx* errs
) {
    *found = nullptr;
    if (!namespace) return nullptr;
    Symbol const* member = symbol_namespace_member(namespace, span_sv(item_name));
    Symbol const* decl = symbol_for_meaning(member, meaning);
    // (A value is TS2749 below in a qualified name, tsc's canSuggestTypeof; an import type's is TS2694 here)
    if (!decl && meaning == SYMBOL_TYPE && suggest) decl = symbol_for_meaning(member, SYMBOL_ANY_MEANING);
    if (!decl) {
        cstr qualified = symbol_fully_qualified_name(shown ? shown : namespace);
        if (!suggest || !scope_report_misspelled_export(namespace, item_name, cstr_sv(&qualified), false, errs)) {
            error_ctx_error_d(errs, item_name, 2694,
                "Namespace '", qualified, "' has no exported member '", item_name, "'.");
        }
        cstr_drop(&qualified);
        return nullptr;
    }
    *found = member;
    return decl;
}

// A name of `A.b.c` (b's index `at` in the namespaces, theirs count for c) that isn't a member of the namespace before
// it at all: when A means a value too, whether `A.b.c` reads as properties of it decides between TS2749 and TS2694
// when checking (tsc's resolveEntityName: canSuggestTypeof, tryGetQualifiedNameAsValue). A namespace of the same
// name merged inside one hides the outer one's members from the qualified name, not from A's value
static bool note_value_missing(IdentifierTypeRare* rare, Scope* names, Symbol const* namespace, u32 at) {
    Symbol const* first_found = nullptr;
    rare->first_value = scope_get_value(names, span_sv(rare->namespaces.data[0]), &first_found);
    if (!rare->first_value) return false;
    rare->value_missing = true;
    rare->value_member_of = namespace;
    rare->value_member_at = at;
    return true;
}

static cstr identifier_type_make_name_str(IdentifierType const* me) {
    cstr ret = cstr_null;
    for_each (i, ast_spans, identifier_type_namespaces(me)) {
        cstr_append_span(&ret, *i);
        cstr_append_lit(&ret, ".");
    }
    cstr_append_span(&ret, my name);
    return ret;
}

// Whether a CommonJS module's `export =` has other exports that are types or namespaces next to it (its `@typedef`s):
// tsc's bindCommonJSTypeExports makes `export=` a namespace module then
static bool commonjs_type_exports(Symbol const* module) {
    Symbol const* export_equals = symbol_get_export(module, c_sv("export="));
    return export_equals && export_equals->decl_count && span_source(export_equals->decls[0].full) &&
        span_source(export_equals->decls[0].full)->is_js && (export_equals->flags & SYMBOL_NAMESPACE_MODULE);
}

// `import("./a").A.B`, `typeof import("./a")`: the names are members of the module. False if there is no module
static bool import_type_lookup_names(IdentifierType* me, Scope* names, ErrorCtx* errs) {
    IdentifierTypeRare* rare = identifier_type_rare(me);
    rare->import_prop_start = -1;
    if (rare->import_argument) {
        // (tsc's checkImportType)
        type_lookup_names(rare->import_argument, names, errs);
        error_ctx_error_d(errs, rare->import_argument_span, 1141, "String literal expected.");
    }
    if (!rare->import_ref) return true; // The parser complained
    Symbol* module = binder_module_of_ref(names->binder, rare->import_ref);
    if (!module) {
        if (rare->import_ref->file) {
            cstr shown = module_ref_file_message_path(rare->import_ref);
            error_ctx_error_d(errs, rare->import_ref->span, 2306, "File '", shown, "' is not a module.");
            cstr_drop(&shown);
        } else {
            module_ref_report_unresolved(rare->import_ref, errs);
        }
        return false;
    }
    module_ref_report_require_of_esm(rare->import_ref, rare->import_ref->span, errs); // (TS1542)
    if (module->is_shorthand_module) return true;
    Symbol const* container = module_export_equals(module);
    if (!span_valid(my name)) {
        if (rare->import_typeof) {
            my target = container;
            my symbol = container;
        } else if (container != module && symbol_for_meaning(container, SYMBOL_TYPE)) {
            // `export = C`
            my target = symbol_for_meaning(container, SYMBOL_TYPE);
            my symbol = container;
            rare->import_export_equals_alias = commonjs_type_exports(module);
        } else {
            error_ctx_error_d(errs, rare->import_span, 1340,
                "Module '", rare->import_ref->path, "' does not refer to a type, but is used as a type here. Did you mean 'typeof import('",
                rare->import_ref->path, "')'?");
        }
        return true;
    }
    if (rare->import_typeof) {
        // A value: exports as far as they go, properties from there on
        Symbol const* current = container;
        isize count = ast_spans_size(&rare->namespaces) + 1;
        for (isize i = 0; i < count; i++) {
            bool last = i == count - 1;
            Span name = last ? my name : *ast_spans_at(&rare->namespaces, i);
            Symbol const* members = symbol_for_meaning(current, SYMBOL_NAMESPACE);
            if (members && members->is_placeholder) return true;
            Symbol const* member = members ? symbol_namespace_member(members, span_sv(name)) : nullptr;
            Symbol const* value = symbol_for_meaning(member, last ? SYMBOL_VALUE : SYMBOL_VALUE | SYMBOL_NAMESPACE);
            if (!value) {
                // (After `module.exports = v` the names are v's properties, tsc's getTypeFromImportTypeNode:
                // symbolFromVariable; the checker reports a missing one)
                bool export_equals_value = i == 0 && container != module && (current->flags & SYMBOL_VALUE);
                if (!export_equals_value && (i == 0 || !(current->flags & SYMBOL_VALUE))) {
                    cstr container_name = symbol_fully_qualified_name(current);
                    error_ctx_error_d(errs, name, 2694,
                        "Namespace '", container_name, "' has no exported member '", name, "'.");
                    cstr_drop(&container_name);
                    return true;
                }
                rare->import_prop_start = (int)i;
                break;
            }
            if (last) my symbol = member;
            else ast_symbols_push(&rare->namespace_symbols, member);
            current = value;
        }
        my target = symbol_for_meaning(current, SYMBOL_VALUE);
        return true;
    }

    if (container != module) {
        // A CommonJS module's other exports (its `@typedef`s) are found next to what `module.exports =` assigns
        // (tsgo's getExportsOfModuleWorker: the original module's type exports)
        bool qualified = ast_spans_size(&rare->namespaces);
        Symbol const* own = symbol_get_export(module, span_sv((qualified ? *rare->namespaces.data : my name)));
        if (own && symbol_for_meaning(own, qualified ? SYMBOL_NAMESPACE : SYMBOL_TYPE)) container = module;
    }
    Symbol const* shown = nullptr;
    for_each (i, ast_spans, &rare->namespaces) {
        Symbol const* found;
        container = get_symbol_from_namespace(container, shown, *i, SYMBOL_NAMESPACE, false, &found, errs);
        if (!container || container->is_placeholder) return true;
        ast_symbols_push(&rare->namespace_symbols, found);
        shown = found;
    }
    my target = get_symbol_from_namespace(container, shown, my name, rare->import_typeof ? SYMBOL_VALUE : SYMBOL_TYPE, false,
        &my symbol, errs);
    return true;
}

// tsc's resolveNameHelper at a class: a static member can't see the class's type parameters (TS2302)
static void check_static_type_parameter_reference(IdentifierType* me, Scope const* names, ErrorCtx* errs) {
    bool in_computed_name = false;
    for (Scope const* scope = names; scope; scope = scope->parent) {
        if (scope_declares_symbol(scope, my target)) return;
        // A type parameter of a class or interface in the computed name of one of its members: TS2467 (tsc's
        // resolveNameHelper at a ComputedPropertyName), the innermost member name only
        for (u32 i = 0; !in_computed_name && scope_rare(scope)->computed_name_of && i < scope_rare(scope)->computed_name_of->items.size; i++) {
            GenericItem const* item = &scope_rare(scope)->computed_name_of->items.data[i];
            if (item->owned_symbol == my target || item->symbol_ref == my target) {
                error_ctx_error_d(errs, my name, 2467, "A computed property name cannot reference a type parameter from its "
                    "containing type.");
                my target = my symbol = nullptr; // (Resolves to nothing, like in tsc)
                return;
            }
        }
        in_computed_name |= scope_rare(scope)->computed_name_of != nullptr;
        Generic const* base_of = scope_rare(scope)->base_expression_of;
        for (u32 i = 0; base_of && i < base_of->items.size; i++) {
            GenericItem const* item = &base_of->items.data[i];
            if (item->owned_symbol == my target || item->symbol_ref == my target) {
                error_ctx_error_d(errs, my name, 2562, "Base class expressions cannot reference class type parameters.");
                my target = my symbol = nullptr; // (Resolves to nothing, like in tsc)
                return;
            }
        }
        if (scope_rare(scope)->this_ref.kind != THIS_CLASS_STATIC || !scope_rare(scope)->this_ref.class_decl) continue;
        for_each (i, vec_generic_item, &scope_rare(scope)->this_ref.class_decl->generic.items) {
            if (i->owned_symbol == my target || i->symbol_ref == my target) {
                error_ctx_error_d(errs, my name, 2302, "Static members cannot reference class type parameters.");
                my target = my symbol = nullptr; // (Resolves to nothing, like in tsc)
                return;
            }
        }
    }
}

// tsc's getIntendedTypeFromJSDocTypeReference: what some names mean in JSDoc whatever is in scope. False for a name
// that is looked up as usual
static bool jsdoc_lookup_intended(IdentifierType* me, Scope* names) {
    static const struct { const char* name; JsDocIntrinsic intrinsic; } intrinsics[] = {
        {"String", JSDOC_INTRINSIC_STRING}, {"Number", JSDOC_INTRINSIC_NUMBER}, {"BigInt", JSDOC_INTRINSIC_BIGINT},
        {"Boolean", JSDOC_INTRINSIC_BOOLEAN}, {"Void", JSDOC_INTRINSIC_VOID}, {"Undefined", JSDOC_INTRINSIC_UNDEFINED},
        {"Null", JSDOC_INTRINSIC_NULL},
    };
    for (u32 i = 0; i < c_arraylen(intrinsics); i++) {
        if (csview_equals(span_sv(my name), intrinsics[i].name)) {
            my jsdoc_intrinsic = intrinsics[i].intrinsic;
            return true;
        }
    }
    bool no_args = !vec_spanned_type_size(&my generic_params);
    bool implicit_any = !compiler_options()->no_implicit_any;
    // `function` is the global Function type, `array`/`promise` without arguments are `Array`/`Promise` of any
    const char* global = span_equals(my name, "function") ? "Function" :
        span_equals(my name, "array") && no_args && implicit_any ? "Array" :
        span_equals(my name, "promise") && no_args && implicit_any ? "Promise" : nullptr;
    if (global) {
        my target = scope_get_type(names, csview_from(global), &my symbol);
        return true;
    }
    // (`Object<T>` with another number of arguments is TS2315, and any too)
    if (span_equals(my name, "Object") && (vec_spanned_type_size(&my generic_params) == 2 || implicit_any)) {
        // (`Object<K, V>` is `Record<K, V>` when K is a valid key, the checker decides)
        my jsdoc_intrinsic = vec_spanned_type_size(&my generic_params) == 2 ? JSDOC_INTRINSIC_INDEX_SIGNATURE : JSDOC_INTRINSIC_ANY;
        return true;
    }
    return false;
}

// A symbol that is a namespace only through its expandos (`f.x = v`): tsc's binder adds the expando to the exports
// of the function's or initializer's symbol without the Namespace meaning, so the name isn't a namespace in types
static bool namespace_from_expandos_only(Symbol const* symbol) {
    if (!symbol) return false;
    bool expando = false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (!(symbol->decls[i].flags & SYMBOL_NAMESPACE)) continue;
        // (One without a node is the namespace a CommonJS `export =` gets for the module's types: a real one, tsc's
        // bindCommonJSTypeExports)
        if (symbol->decls[i].kind != SYMBOL_DECL_EXPANDO || !symbol->decls[i].node) return false;
        expando = true;
    }
    return expando;
}

_Thread_local TypeParamDefaultScope identifier_type_default_scope;


// The scope of the function whose JSDoc types are being looked up (identifier_type_set_jsdoc_host())
static _Thread_local Scope const* jsdoc_host_scope;

Scope const* identifier_type_set_jsdoc_host(Scope const* scope) {
    Scope const* previous = jsdoc_host_scope;
    jsdoc_host_scope = scope;
    return previous;
}

void identifier_type_lookup_names(IdentifierType* me, Scope* names, ErrorCtx* errs) {
    check(!my target);

    if (my rare && (my rare->import_ref || span_valid(my rare->import_span))) {
        // (tsc's checkImportType doesn't check the type arguments: they are only evaluated when the module is there.
        // Their names still resolve, silently, for whoever asks)
        bool found = import_type_lookup_names(me, names, errs);
        // (The attributes' values are expressions: tsgo's checkImportAttributes checks them)
        if (my rare->import_attributes) {
            for_each (i, vec_import_attribute, &my rare->import_attributes->elements) expr_lookup_names(&i->value, names, errs);
        }
        ErrorCtx silent = errs ? error_ctx_scratch(errs) : (ErrorCtx){0};
        // (Type arguments of `import("./m")<T>` whose target takes none aren't resolved at all in tsc, TS2315: no uses)
        // (Nor of one whose `export=` its typedefs made a namespace: TS2315 "Type 'export=' is not generic")
        bool unresolved = !span_valid(my name) && !my rare->import_typeof && my target && !my target->is_placeholder &&
            (my target->flags & SYMBOL_TYPE) && (!several_types_is_generic(my target) || my rare->import_export_equals_alias);
        unused_marking_suppressed += unresolved;
        bool saved_lazy_silent = type_lookup_lazy_silent;
        type_lookup_lazy_silent = true;
        for_each (i, vec_spanned_type, &my generic_params) {
            type_lookup_names(&i->type, names, found ? errs : &silent);
        }
        type_lookup_lazy_silent = saved_lazy_silent;
        unused_marking_suppressed -= unresolved;
        error_ctx_drop(&silent);
        return;
    }
    if (!my keyword) scope_check_strict_identifier(names, identifier_type_namespace_count(me) ? *my rare->namespaces.data : my name, true);

    if (identifier_type_namespace_count(me)) {
        IdentifierTypeRare* rare = my rare;
        Symbol const* namespace = nullptr;
        for_each (i, ast_spans, &rare->namespaces) {
            Symbol const* decl;
            Symbol const* found;
            if (i == rare->namespaces.data /* First namespaces */) {
                found = nullptr;
                decl = i->size ? scope_get_namespace(names, span_sv(*i), &found) : nullptr; // (Missing: nothing)
                if (namespace_from_expandos_only(decl)) decl = nullptr;
                unused_mark_referenced(names, (Symbol*)found, SYMBOL_NAMESPACE);
                if (decl && decl->is_placeholder) port_item_check_namespace_import_use(found, names);
                if (!decl) {
                    Symbol const* type_symbol = nullptr;
                    Symbol const* type = scope_get_type(names, span_sv(*i), &type_symbol);
                    if (type && !(type->flags & SYMBOL_NAMESPACE) && !type->is_placeholder) {
                        rare->type_as_namespace = type;
                    } else if (i->size) { // (A missing name: tsc's resolveEntityName says nothing)
                        scope_report_namespace_not_found(names, *i, errs);
                    }
                }
            } else /* Subsequent namespaces */ {
                Symbol const* member = namespace ? symbol_get_export(namespace, span_sv(*i)) : nullptr;
                // (A member that is a value too is TS2749 for the whole name, tsc's canSuggestTypeof)
                Symbol const* type = member && !symbol_for_meaning(member, SYMBOL_NAMESPACE | SYMBOL_VALUE) ?
                    symbol_for_meaning(member, SYMBOL_TYPE) : nullptr;
                if (member && !type && !symbol_for_meaning(member, SYMBOL_NAMESPACE) && symbol_for_meaning(member, SYMBOL_VALUE)) {
                    rare->value_member = member;
                    rare->value_member_of = namespace;
                    rare->value_member_at = (u32)(i - rare->namespaces.data);
                    Symbol const* first_found = nullptr;
                    rare->first_value = scope_get_value(names, span_sv(rare->namespaces.data[0]), &first_found);
                    decl = nullptr;
                } else if (type) {
                    // An exported type used as a namespace (tsc's resolveEntityName): said at what follows it, unless
                    // another export's name is close enough (TS2724 comes first there)
                    Span right = i + 1 < rare->namespaces.data + rare->namespaces.size ? i[1] : my name;
                    cstr qualified = symbol_fully_qualified_name(namespace);
                    bool misspelled = scope_report_misspelled_export(namespace, *i, cstr_sv(&qualified), false, errs);
                    cstr_drop(&qualified);
                    if (!misspelled) error_ctx_error_d(errs, right, 2713, "Cannot access '", *i, ".", right, "' because '", *i,
                        "' is a type, but not a namespace. Did you mean to retrieve the type of the property '", right,
                        "' in '", *i, "' with '", *i, "[\"", right, "\"]'?");
                    decl = nullptr;
                } else if (!member && namespace && note_value_missing(rare, names, namespace, (u32)(i - rare->namespaces.data))) {
                    decl = nullptr;
                } else {
                    decl = get_symbol_from_namespace(namespace, nullptr, *i, SYMBOL_NAMESPACE, true, &found, errs);
                }
            }
            // Everything looked up in a placeholder is any
            namespace = decl && !decl->is_placeholder ? decl : nullptr;
            if (!decl) {
                break;
            }
            ast_symbols_push(&rare->namespace_symbols, found);
        }
        if (namespace && !symbol_namespace_member(namespace, span_sv(my name)) &&
            note_value_missing(rare, names, namespace, (u32)ast_spans_size(&rare->namespaces))
        ) {
            namespace = nullptr;
        }
        if (namespace) {
            my target = get_symbol_from_namespace(namespace, nullptr, my name, SYMBOL_TYPE, true, &my symbol, errs);
        }
    } else if (my jsdoc && jsdoc_lookup_intended(me, names)) {
    } else if (my keyword) {
        if (my keyword == KEYWORD_IDENTIFIER_THIS) {
            scope_note_this_reference(names);
            ThisRef this_ref = scope_get_this(names, true);
            my this_kind = this_ref.kind;
            // Not in a constructor's parameters or return type, only its body (tsc's getThisType:
            // isNodeDescendantOf(node, container.body)): an error like in a static member (TS2526, the error type)
            if (my this_kind == THIS_CLASS_INSTANCE) {
                bool in_signature = false;
                for (Scope const* s = names; s; s = s->parent) {
                    in_signature |= s->parameters || s->return_type;
                    if (s->kind == SCOPE_FUNCTION) {
                        if (in_signature && s->function && s->function->is_constructor && function_impl_rare(s->function)->constructed_class) {
                            my this_kind = THIS_CLASS_STATIC;
                        }
                        break;
                    }
                }
            }
            my target = this_ref.symbol;
            if (this_ref.interface) this_ref.interface->contains_this = true;
            if (names->file && names->file->is_js && this_ref.kind != THIS_CLASS_INSTANCE && this_ref.kind != THIS_INTERFACE) {
                ThisRef value = scope_get_this(names, false);
                if (value.kind == THIS_ANY) {
                    identifier_type_rare(me)->this_function = value.function;
                    // (Arrow functions between don't count: tsc's getThisContainer skips them)
                    bool in_signature = false;
                    for (Scope const* s = names; s; s = s->parent) {
                        if (s->kind != SCOPE_FUNCTION) {
                            in_signature |= s->parameters || s->return_type;
                            continue;
                        }
                        if (s->function != value.function) {
                            in_signature = false;
                            continue;
                        }
                        identifier_type_rare(me)->this_in_signature = in_signature;
                        break;
                    }
                }
            }
        }
    } else {
        my target = scope_get_type(names, span_sv(my name), &my symbol);
        // A namespace isn't a type (tsc's checkAndReportErrorForUsingNamespaceAsTypeOrValue, before TS2749)
        Symbol const* namespace = my target ? nullptr : scope_resolve(names, span_sv(my name), SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE);
        if (namespace && !namespace_from_expandos_only(namespace)) {
            error_ctx_error_d(errs, my name, 2709, "Cannot use namespace '", my name, "' as a type.");
            return;
        }
        // (A value found instead, TS2749 below, isn't a use: tsc's resolveName for the type failed, the lookup for the
        // message doesn't mark it referenced)
        if (my target) unused_mark_referenced(names, (Symbol*)my symbol, SYMBOL_TYPE);
        if (!my target) {
            my target = scope_get_value(names, span_sv(my name), &my symbol); // TS2749 below
            // (Unless it's a property of what `export =` names that is a type: the checker decides and marks it,
            // export_equals_property_type())
        }
        // In a function's JSDoc a name is looked up from the function too, which makes what it finds there referenced
        // (tsc's resolveEntityNameFromAssignmentDeclaration through getAssignmentDeclarationLocation: the signature's
        // own location): a `@template T` shadowed by a signature's `<T>` in a `@returns` type counts as read
        if (my jsdoc && jsdoc_host_scope && my symbol && !identifier_type_namespace_count(me) && unused_enabled()) {
            Symbol const* host_found = nullptr;
            scope_get_type(jsdoc_host_scope, span_sv(my name), &host_found);
            if (host_found && host_found != my symbol) unused_mark_referenced(jsdoc_host_scope, (Symbol*)host_found, SYMBOL_TYPE);
        }
        if (!my target) scope_report_not_found(names, my name, SYMBOL_TYPE, false, errs);
        if (my target && (my target->flags & SYMBOL_TYPE_PARAMETER)) {
            check_static_type_parameter_reference(me, names, errs);
            TypeParamDefaultScope in_default = identifier_type_default_scope;
            for (isize i = in_default.generic ? in_default.from : 0; in_default.generic && i < in_default.generic->items.size; i++) {
                if (in_default.generic->items.data[i].symbol_ref != my target) continue;
                error_ctx_error_d(errs, identifier_type_span(me), 2744,
                    "Type parameter defaults can only reference previously declared type parameters.");
            }
        }
    }

    // (A property of what `export =` names may be a type: the checker decides, lower_identifier())
    bool export_equals_property = my target && my target->decl_count &&
        my target->decls[0].kind == SYMBOL_DECL_EXPORT_EQUALS_PROPERTY && !identifier_type_namespace_count(me);
    if (my target && !my this_kind && !my target->is_placeholder && !(my target->flags & SYMBOL_TYPE) &&
        !export_equals_property
    ) {
        cstr name_str = identifier_type_make_name_str(me);
        Span error_span = span_between(identifier_type_namespace_count(me) ? *my rare->namespaces.data : my name, my name);
        error_ctx_error_d(errs, error_span, 2749,
            "'", name_str, "' refers to a value, but is being used as a type here. Did you mean 'typeof ",
            name_str, "'?");
        cstr_drop(&name_str);
    }

    for_each (i, vec_spanned_type, &my generic_params) {
        type_lookup_names(&i->type, names, errs);
    }
}

// The type parameters a class, interface or alias declares (the first declaration that has any), and how many of them
// no declaration gives a default (tsc's getMinTypeArgumentCount)
static Generic const* decl_generic(Symbol const* symbol, u32 d) {
    switch (symbol->decls[d].kind) {
        case SYMBOL_DECL_CLASS: return &((ClassDecl const*)symbol->decls[d].node)->generic;
        case SYMBOL_DECL_INTERFACE: return &((InterfaceDecl const*)symbol->decls[d].node)->generic;
        case SYMBOL_DECL_TYPE_ALIAS: return &((TypeDecl const*)symbol->decls[d].node)->generic;
        default: return nullptr;
    }
}

// The merged list: of class and interface declarations every distinct name, in the order they come (a later one's
// parameter of a known name is the same one, tsc's binder merges them by name); else the first declaration's
typedef struct MergedTypeParams {
    Generic const* first;
    GenericItem const* items[32]; // Only filled when `merged`: else the first's
    u32 count;
    bool merged;
} MergedTypeParams;

static GenericItem const* merged_type_param(MergedTypeParams const* me, u32 i) {
    return my merged ? my items[i] : vec_generic_item_at(&my first->items, i);
}

static bool declared_type_params(Symbol const* symbol, u32* min, MergedTypeParams* out) {
    Generic const* first = nullptr;
    u32 first_d = 0;
    *min = 0;
    out->count = 0;
    for (u32 d = 0; d < symbol->decl_count && !first; d++) {
        Generic const* generic = decl_generic(symbol, d);
        if (!generic_empty(generic)) {
            first = generic;
            first_d = d;
        }
    }
    if (!first) return false;
    out->first = first;
    out->merged = false;
    out->count = (u32)vec_generic_item_size(&first->items);
    if (out->count > c_arraylen(out->items)) out->count = c_arraylen(out->items);
    // One pass, by position (PERF: every type reference's arity check asks): the defaults, and whether a later class or
    // interface declaration has type parameters the first doesn't have at the same position, which makes the list a
    // merge by name (rare)
    bool can_merge = symbol->decls[first_d].kind == SYMBOL_DECL_CLASS || symbol->decls[first_d].kind == SYMBOL_DECL_INTERFACE;
    uint8_t memo = __atomic_load_n(&symbol->type_params_merge, __ATOMIC_RELAXED);
    bool merges = memo == 2;
    bool has_default[32] = {0};
    if (!can_merge) memo = 1;
    for (u32 d = 0; d < symbol->decl_count && !merges; d++) {
        Generic const* generic = decl_generic(symbol, d);
        if (!generic) continue;
        u32 count = (u32)vec_generic_item_size(&generic->items);
        bool mergeable = can_merge && d > first_d &&
            (symbol->decls[d].kind == SYMBOL_DECL_CLASS || symbol->decls[d].kind == SYMBOL_DECL_INTERFACE);
        if (mergeable && memo != 1 && count > out->count) merges = true;
        for (u32 i = 0; i < count && i < out->count; i++) {
            GenericItem const* item = vec_generic_item_at(&generic->items, i);
            has_default[i] |= item->fallback_explicit;
            if (mergeable && memo != 1 && !merges) {
                csview a = span_sv(item->name), b = span_sv(vec_generic_item_at(&first->items, i)->name);
                if ((a.size != b.size || memcmp(a.buf, b.buf, (size_t)a.size)) && !name_equals(a, b)) merges = true;
            }
        }
    }
    if (!memo) __atomic_store_n(&((Symbol*)symbol)->type_params_merge, merges ? 2 : 1, __ATOMIC_RELAXED);
    if (merges) {
        out->merged = true;
        for (u32 i = 0; i < out->count; i++) out->items[i] = vec_generic_item_at(&first->items, i);
        for (u32 d = first_d + 1; d < symbol->decl_count; d++) {
            if (symbol->decls[d].kind != SYMBOL_DECL_CLASS && symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
            for_each (i, vec_generic_item, &decl_generic(symbol, d)->items) {
                bool known = false;
                for (u32 k = 0; k < out->count && !known; k++) known = name_equals(span_sv(out->items[k]->name), span_sv(i->name));
                if (!known && out->count < c_arraylen(out->items)) out->items[out->count++] = i;
            }
        }
        // (By name then: a parameter's default may be written at another position)
        for (u32 k = 0; k < out->count; k++) {
            has_default[k] = false;
            for (u32 d = 0; d < symbol->decl_count && !has_default[k]; d++) {
                Generic const* generic = decl_generic(symbol, d);
                if (!generic) continue;
                for_each (i, vec_generic_item, &generic->items) {
                    if (name_equals(span_sv(i->name), span_sv(out->items[k]->name))) has_default[k] |= i->fallback_explicit;
                }
            }
        }
    }
    for (u32 k = 0; k < out->count; k++) if (!has_default[k]) *min = k + 1;
    return true;
}

Span identifier_type_span(IdentifierType const* me) {
    if (my keyword || identifier_type_import_ref(me) || !span_valid(my name) || !span_source(my name)) return my name;
    char const* start = span_start_ptr(!identifier_type_namespace_count(me) ? my name : *ast_spans_front(&my rare->namespaces));
    char const* end = span_end_ptr(my name);
    u32 arg_count = (u32)vec_spanned_type_size(&my generic_params);
    if (arg_count) {
        char const* limit = cstr_str(&span_source(my name)->contents) + cstr_size(&span_source(my name)->contents);
        end = span_end_ptr(vec_spanned_type_at(&my generic_params, arg_count - 1)->span);
        while (end < limit && *end != '>') end++;
        if (end < limit) end++;
    } else {
        // An empty `<>` (TS1099) is still part of the reference
        char const* limit = cstr_str(&span_source(my name)->contents) + cstr_size(&span_source(my name)->contents);
        char const* q = end;
        while (q < limit && (*q == ' ' || *q == '\t')) q++;
        if (q < limit && *q == '<') {
            q++;
            while (q < limit && (*q == ' ' || *q == '\t')) q++;
            if (q < limit && *q == '>') end = q + 1;
        }
    }
    return span_make_in(my name, start, end);
}

// JSDoc's `Function`/`function`: the global Function type, type arguments or not (tsc's
// getIntendedTypeFromJSDocTypeReference)
bool identifier_type_is_jsdoc_function(IdentifierType const* me) {
    return my jsdoc && !identifier_type_namespace_count(me) && (span_equals(my name, "Function") || span_equals(my name, "function"));
}

// tsc's getTypeFromClassOrInterfaceReference/getTypeFromTypeAliasReference/checkNoTypeArguments: the number of type
// arguments has to fit the type parameters (TS2314, TS2707), and a type without any takes none (TS2315)
static void identifier_type_check_arity(IdentifierType const* me, ErrorCtx* errs) {
    Symbol const* symbol = my target;
    // JSDoc's own names take no type arguments (tsc's checkNoTypeArguments in getIntendedTypeFromJSDocTypeReference)
    bool jsdoc_named = my jsdoc_intrinsic && my jsdoc_intrinsic != JSDOC_INTRINSIC_INDEX_SIGNATURE &&
        !(my jsdoc_intrinsic == JSDOC_INTRINSIC_ANY && vec_spanned_type_size(&my generic_params) == 2);
    if (identifier_type_is_jsdoc_function(me)) jsdoc_named = true;
    if (!jsdoc_named && (!symbol || symbol->is_placeholder)) return;
    // (A JSDoc `import("./m").C` of a property of the module's value: the target is the module's value, the type
    // arguments go to the class the property's type is, tsc's getTypeFromJSDocValueReference)
    if (my rare && my rare->import_prop_start >= 0 && !my rare->import_typeof) return;
    // (`typeof import("./m").C` names the value C, not the class type: no type arguments needed, those written make an
    // instantiation expression, tsc's getTypeFromImportTypeNode with isTypeOf)
    if (my rare && my rare->import_typeof && span_valid(my name)) return;
    // (`import("./m")<T>` names what the module's `export=` assigns, by that symbol's name)
    bool import_itself = !span_valid(my name) && my rare && my rare->import_ref && !my rare->import_typeof;
    if (my keyword || (!import_itself && (!span_valid(my name) || !span_source(my name)))) return;
    u32 arg_count = (u32)vec_spanned_type_size(&my generic_params);
    Span span = identifier_type_span(me);
    if (import_itself) {
        // (The whole `import("./m")<T>`: tsgo's error node is the import type)
        Span import_span = identifier_type_import_span(me);
        if (!span_valid(import_span) || !arg_count) return;
        SourceFile const* file = span_source(import_span);
        char const* limit = cstr_str(&file->contents) + cstr_size(&file->contents);
        char const* end = span_end_ptr(vec_spanned_type_at(&my generic_params, arg_count - 1)->span);
        while (end < limit && *end != '>') end++;
        if (end < limit) end++;
        span = span_make(file, span_start_ptr(import_span), end);
    }
    if (jsdoc_named) {
        if (arg_count) error_ctx_error_d(errs, span, 2315, "Type '", my name, "' is not generic.");
        return;
    }
    if (ty_alias_is_circular(current_checker, symbol)) {
        if (arg_count) error_ctx_error_d(errs, span, 2315, "Type '", symbol->name, "' is not generic.");
        return;
    }
    if (!several_types_check_arity(symbol, arg_count, span, false, errs) && arg_count && !my jsdoc_unchecked) {
        ty_check_type_reference_constraints(current_checker, symbol, &my generic_params, errs);
    }
}

// The arity memo of a class, interface or type alias (0 for other symbols): 1 << 12 | count << 6 | min
static uint16_t several_types_arity_memo(Symbol const* symbol) {
    bool generic_kind = symbol->flags & (SYMBOL_CLASS | SYMBOL_INTERFACE | SYMBOL_TYPE_ALIAS);
    u32 min = 0;
    MergedTypeParams params;
    // (Asked for every type reference: remembered, see Symbol.type_args_arity)
    uint16_t memo = generic_kind ? __atomic_load_n(&symbol->type_args_arity, __ATOMIC_RELAXED) : 0;
    if (!memo && generic_kind) {
        // (None at all is none allowed, which a class, interface or alias, all types, says the same way)
        u32 count = declared_type_params(symbol, &min, &params) ? params.count : 0;
        memo = (uint16_t)(1u << 12 | count << 6 | min);
        __atomic_store_n(&((Symbol*)symbol)->type_args_arity, memo, __ATOMIC_RELAXED);
    }
    return memo;
}

// Whether symbol is a class, interface or type alias with type parameters
bool several_types_is_generic(Symbol const* symbol) {
    return (several_types_arity_memo(symbol) >> 6) & 63;
}

// Whether a reference to symbol with arg_count type arguments has the right number of them (in source)
bool several_types_arity_fits(Symbol const* symbol, u32 arg_count, SourceFile const* source) {
    uint16_t memo = several_types_arity_memo(symbol);
    if (!memo) return !arg_count || !(symbol->flags & SYMBOL_TYPE);
    u32 min = memo & 63;
    u32 count = (memo >> 6) & 63;
    // In JavaScript without noImplicitAny any number of type arguments goes to a generic class or interface (tsc's
    // isJsImplicitAny: the missing ones are any)
    // (Not for a type alias: tsc's getTypeFromTypeAliasReference has no such leniency, nor for a type without type
    // parameters: checkNoTypeArguments)
    bool js_implicit_any = source && source->is_js && !compiler_options()->no_implicit_any && !(symbol->flags & SYMBOL_TYPE_ALIAS) && count;
    return (arg_count >= min && arg_count <= count) || js_implicit_any;
}

bool several_types_check_arity(Symbol const* symbol, u32 arg_count, Span span, bool missing_extends, ErrorCtx* errs) {
    if (several_types_arity_fits(symbol, arg_count, span_source(span))) return false;
    bool generic_kind = symbol->flags & (SYMBOL_CLASS | SYMBOL_INTERFACE | SYMBOL_TYPE_ALIAS);
    u32 min = 0;
    MergedTypeParams params;
    if (!generic_kind || !declared_type_params(symbol, &min, &params)) {
        // (tsc's symbolToString of a class expression without a name)
        csview name = symbol->name.size || !(symbol->flags & SYMBOL_CLASS) ? symbol->name : c_sv("(Anonymous class)");
        error_ctx_error_d(errs, span, 2315, "Type '", name, "' is not generic.");
        return true;
    }
    u32 count = params.count;
    cstr name = cstr_from_sv(symbol->name);
    if (!(symbol->flags & SYMBOL_TYPE_ALIAS)) {
        // (tsc's typeToString of the generic type: its parameters)
        cstr_append(&name, "<");
        for (u32 i = 0; i < count; i++) {
            if (i) cstr_append(&name, ", ");
            cstr_append_sv(&name, span_sv(merged_type_param(&params, i)->name));
        }
        cstr_append(&name, ">");
    }
    if (missing_extends && min == count) {
        error_ctx_error_d(errs, span, 8026, "Expected ", name, " type arguments; provide these with an '@extends' tag.");
    } else if (missing_extends) {
        // (tsc fills the message with the type and the minimum)
        error_ctx_error_d(errs, span, 8027, "Expected ", name, "-", min, " type arguments; provide these with an '@extends' tag.");
    } else if (min == count) {
        error_ctx_error_d(errs, span, 2314, "Generic type '", name, "' requires ", count, " type argument(s).");
    } else {
        error_ctx_error_d(errs, span, 2707, "Generic type '", name, "' requires between ", min, " and ", count, " type arguments.");
    }
    cstr_drop(&name);
    return true;
}

static void identifier_type_typecheck_inner(IdentifierType const* me, ErrorCtx* errs);

void identifier_type_typecheck(IdentifierType const* me, ErrorCtx* errs) {
    // tsc's checkSourceElement of a TypeReference: where its instantiation errors go
    CurrentNode saved = ty_enter_node(current_checker, identifier_type_span(me));
    identifier_type_typecheck_inner(me, errs);
    ty_leave_node(current_checker, saved);
    // (tsgo's checkImportType ends with checkImportAttributes: the attributes against the global ImportAttributes,
    // and nothing more for an import type)
    if (my rare && my rare->import_attributes) {
        ty_check_import_attributes(current_checker, my rare->import_attributes, errs);
        for_each (i, vec_import_attribute, &my rare->import_attributes->elements) expr_typecheck(&i->value, errs);
    }
}

static void identifier_type_typecheck_inner(IdentifierType const* me, ErrorCtx* errs) {
    IdentifierTypeRare const* rare = my rare;
    if (rare && rare->import_attributes) { // tsc's checkImportType
        // (tsgo's checkGrammarImportAttributeValues: only a string literal, not a template)
        for_each (i, vec_import_attribute, &rare->import_attributes->elements) {
            if (!import_attribute_value_is_string_literal(&i->value)) {
                error_ctx_error_d(errs, expr_outer_span(&i->value), 2858, "Import attribute values must be string literal expressions.");
            }
        }
        // (`assert` is the parser's TS2880)
        import_attributes_resolution_mode(rare->import_attributes, errs);
    }
    // (tsc's checkImportType doesn't check an import type's type arguments, only their constraints)
    if (!identifier_type_import_ref(me)) {
        for_each (i, vec_spanned_type, &my generic_params) type_typecheck(&i->type, errs);
    }
    if (my keyword == KEYWORD_IDENTIFIER_THIS && my this_kind != THIS_CLASS_INSTANCE && my this_kind != THIS_INTERFACE) {
        error_ctx_error_d(errs, my name, 2526, "A 'this' type is available only in a non-static member of a class or interface.");
    }
    identifier_type_check_arity(me, errs);
    if (rare && rare->type_as_namespace && ast_spans_size(&rare->namespaces)) {
        Checker* checker = current_checker;
        Span left = rare->namespaces.data[0];
        Span right = ast_spans_size(&rare->namespaces) > 1 ? rare->namespaces.data[1] : my name;
        Ty const* declared = ty_apparent_type(checker, ty_declared_type_of(checker, rare->type_as_namespace));
        Symbol const* as_namespace = rare->type_as_namespace;
        if (as_namespace->decl_count && as_namespace->decls[0].kind == SYMBOL_DECL_BUILTIN && is_primitive_type_name(span_sv(left))) {
            // (A primitive type keyword isn't a symbol to tsc: checkAndReportErrorForUsingTypeAsValue, its namespace
            // meaning includes ValueModule)
            error_ctx_error_d(errs, left, 2693, "'", left, "' only refers to a type, but is being used as a value here.");
        } else if (ty_property_of_type(checker, declared, checker_atom(checker, span_sv(right)))) {
            error_ctx_error_d(errs, span_between(left, right), 2713, "Cannot access '", left, ".", right, "' because '", left,
                "' is a type, but not a namespace. Did you mean to retrieve the type of the property '", right,
                "' in '", left, "' with '", left, "[\"", right, "\"]'?");
        } else {
            error_ctx_error_d(errs, left, 2702, "'", left, "' only refers to a type, but is being used as a namespace here.");
        }
    }
    if (rare && (rare->value_member || rare->value_missing)) {
        Checker* checker = current_checker;
        isize count = ast_spans_size(&rare->namespaces);
        // (tsc's tryGetQualifiedNameAsValue: the names after the first are properties of the value)
        Ty const* value = rare->first_value && !rare->first_value->is_placeholder ? ty_type_of_symbol(checker, rare->first_value) : nullptr;
        for (isize k = 1; value && k <= count; k++) {
            Span part = k < count ? rare->namespaces.data[k] : my name;
            value = ty_prop_type_of(checker, ty_apparent_type(checker, value), checker_atom(checker, span_sv(part)));
        }
        u32 at = rare->value_member_at;
        Span member = (isize)at < count ? rare->namespaces.data[at] : my name;
        Span right = (isize)at + 1 < count ? rare->namespaces.data[at + 1] : my name;
        // (tsc's resolveEntityName: a close enough export's name first)
        cstr container_name = symbol_fully_qualified_name(rare->value_member_of);
        bool misspelled = scope_report_misspelled_export(rare->value_member_of, member, cstr_sv(&container_name), false, errs);
        cstr_drop(&container_name);
        if (misspelled) {
        } else if (value) {
            cstr name = identifier_type_make_name_str(me);
            error_ctx_error_d(errs, span_between(rare->namespaces.data[0], my name), 2749, "'", name, "' refers to a value, "
                "but is being used as a type here. Did you mean 'typeof ", name, "'?");
            cstr_drop(&name);
        } else if (rare->value_member && symbol_for_meaning(rare->value_member, SYMBOL_TYPE)) {
            error_ctx_error_d(errs, right, 2713, "Cannot access '", member, ".", right, "' because '", member,
                "' is a type, but not a namespace. Did you mean to retrieve the type of the property '", right,
                "' in '", member, "' with '", member, "[\"", right, "\"]'?");
        } else {
            cstr qualified = symbol_fully_qualified_name(rare->value_member_of);
            error_ctx_error_d(errs, member, 2694, "Namespace '", qualified, "' has no exported member '", member, "'.");
            cstr_drop(&qualified);
        }
    }
    ty_check_export_equals_property_reference(current_checker, me);
    // tsc's getTypeFromTypeReference: a referenced alias is resolved (TS2456 if that is circular)
    if (my target && (my target->flags & (SYMBOL_TYPE_ALIAS | SYMBOL_CLASS | SYMBOL_INTERFACE | SYMBOL_ENUM)) && !my target->is_placeholder) ty_declared_type_of(current_checker, my target);
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
    CurrentNode saved = ty_enter_node(current_checker, my span); // tsc's checkSourceElement
    type_typecheck(&my interface, errs);
    type_typecheck(&my index, errs);
    ty_check_indexed_access_type(current_checker, ty_of_node(&my interface), ty_of_node(&my index), my index_span, my span, errs);
    ty_leave_node(current_checker, saved);
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

// Template literal type

void template_type_drop(TemplateType* me) {
    ast_cstrs_drop(&my texts);
    vec_type_drop(&my types);
    ast_spans_drop(&my type_spans);
}

// A placeholder made of primitives and literals (or any/never) fits without a relation
static bool template_placeholder_trivially_fits(Ty const* type) {
    u32 fits = TYF_ANY | TYF_NEVER | TYF_NULLABLE | TYF_STRING_LIKE | TYF_NUMBER | TYF_BIGINT | TYF_LITERAL | TYF_BOOLEAN |
        TYF_ENUM | TYF_ENUM_LITERAL;
    if (type->kind != TY_UNION) return type->kind != TY_INTERSECTION && (type->flags & fits) && !(type->flags & ~(fits |
        TYF_UNION | TYF_ERROR | TYF_COULD_CONTAIN_TYPE_VARIABLES | TYF_FRESH_LITERAL | TYF_NON_INFERRABLE));
    for (u32 i = 0; i < type->composite.types.len; i++) {
        if (!template_placeholder_trivially_fits(type->composite.types.data[i])) return false;
    }
    return true;
}

// tsc's checkTemplateLiteralType: each placeholder is string | number | bigint | boolean | null | undefined
// (templateConstraintType), as the type node lowers (`${K}` in the true branch of `K extends string ? ...` is a
// substitution type, fine)
void template_type_typecheck(TemplateType const* me, ErrorCtx* errs) {
    Checker* checker = current_checker;
    Ty const* allowed = nullptr;
    for_each (i, vec_type, &my types) {
        type_typecheck(i, errs);
        if (!allowed) {
            Ty const* parts[6] = {
                ty_intrinsic(checker, TY_STRING), ty_intrinsic(checker, TY_NUMBER), ty_intrinsic(checker, TY_BIGINT),
                ty_intrinsic(checker, TY_BOOLEAN), ty_intrinsic(checker, TY_NULL), ty_intrinsic(checker, TY_UNDEFINED),
            };
            allowed = ty_union(checker, parts, 6, nullptr, (TyList){0});
        }
        Ty const* type = ty_of_node(i);
        if (template_placeholder_trivially_fits(type)) continue;
        ty_check_assignable(checker, type, allowed, *ast_spans_at(&my type_spans, i_index), errs);
    }
}

// Typeof

void typeof_type_drop(TypeofType* me) {
    if (my owns_expr) {
        expr_drop(my expr);
        ast_delete(my expr);
    }
}

void typeof_type_lookup_names(TypeofType* me, Scope* names, ErrorCtx* errs) {
    bool saved = names->flow->in_type_query;
    names->flow->in_type_query = true;
    expr_lookup_names(my expr, names, errs);
    names->flow->in_type_query = saved;
    Expr* name = my expr;
    if (name->tag == EXPR_GENERIC_PARAMS) name = &name->generic_params->expr;
    for (; name->tag == EXPR_PROPERTY && name->property->form == PROPERTY_EXPR_DOT; name = &name->property->expr) {
        name->property->qualified_name = true;
    }
}

void typeof_type_typecheck(TypeofType const* me, ErrorCtx* errs) {
    expr_typecheck_type_only_site(my expr, errs);
}

// Ternary

void ternary_type_drop(TernaryType* me) {
    type_drop(&my maybe_sub);
    type_drop(&my maybe_super);
    type_drop(&my if_true);
    type_drop(&my if_false);
}

