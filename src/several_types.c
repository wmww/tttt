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
    vec_span_drop(&rare->namespaces);
    vec_symbol_cptr_drop(&rare->namespace_symbols);
    import_attributes_free(rare->import_attributes);
    if (rare->import_argument) {
        type_drop(rare->import_argument);
        free(rare->import_argument);
    }
    free(rare);
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
    Symbol const* member = symbol_get_export(namespace, item_name.sv);
    Symbol const* decl = symbol_for_meaning(member, meaning);
    if (!decl && meaning == SYMBOL_TYPE) decl = symbol_for_meaning(member, SYMBOL_ANY_MEANING); // TS2749 below
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

static cstr identifier_type_make_name_str(IdentifierType const* me) {
    cstr ret = cstr_null;
    for_each (i, vec_span, identifier_type_namespaces(me)) {
        cstr_append_span(&ret, *i);
        cstr_append_lit(&ret, ".");
    }
    cstr_append_span(&ret, my name);
    return ret;
}

// `import("./a").A.B`, `typeof import("./a")`: the names are members of the module
static void import_type_lookup_names(IdentifierType* me, Scope* names, ErrorCtx* errs) {
    IdentifierTypeRare* rare = identifier_type_rare(me);
    rare->import_prop_start = -1;
    if (rare->import_argument) {
        // (tsc's checkImportType)
        type_lookup_names(rare->import_argument, names, errs);
        error_ctx_error_d(errs, rare->import_argument_span, 1141, "String literal expected.");
    }
    if (!rare->import_ref) return; // The parser complained
    Symbol* module = binder_module_of_ref(names->binder, rare->import_ref);
    if (!module) {
        if (rare->import_ref->file) {
            cstr shown = module_ref_file_message_path(rare->import_ref);
            error_ctx_error_d(errs, rare->import_ref->span, 2306, "File '", shown, "' is not a module.");
            cstr_drop(&shown);
        } else {
            module_ref_report_unresolved(rare->import_ref, errs);
        }
        return;
    }
    module_ref_report_require_of_esm(rare->import_ref, rare->import_ref->span, errs); // (TS1542)
    if (module->is_shorthand_module) return;
    Symbol const* container = module_export_equals(module);
    if (!span_valid(my name)) {
        if (rare->import_typeof) {
            my target = container;
            my symbol = container;
        } else if (container != module && symbol_for_meaning(container, SYMBOL_TYPE)) {
            // `export = C`
            my target = symbol_for_meaning(container, SYMBOL_TYPE);
            my symbol = container;
        } else if (my jsdoc && (container->flags & SYMBOL_VALUE || container == module)) {
            // In JSDoc a module is a value or a type (tsc's getTypeFromImportTypeNode): a value means its type
            my target = container;
            my symbol = container;
        } else {
            error_ctx_error_d(errs, rare->import_span, 1340,
                "Module '", rare->import_ref->path, "' does not refer to a type, but is used as a type here. Did you mean 'typeof import('",
                rare->import_ref->path, "')'?");
        }
        return;
    }
    if (rare->import_typeof) {
        // A value: exports as far as they go, properties from there on
        Symbol const* current = container;
        isize count = vec_span_size(&rare->namespaces) + 1;
        for (isize i = 0; i < count; i++) {
            bool last = i == count - 1;
            Span name = last ? my name : *vec_span_at(&rare->namespaces, i);
            Symbol const* members = symbol_for_meaning(current, SYMBOL_NAMESPACE);
            if (members && members->is_placeholder) return;
            Symbol const* member = members ? symbol_get_export(members, name.sv) : nullptr;
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
                    return;
                }
                rare->import_prop_start = (int)i;
                break;
            }
            if (last) my symbol = member;
            else vec_symbol_cptr_push(&rare->namespace_symbols, member);
            current = value;
        }
        my target = symbol_for_meaning(current, SYMBOL_VALUE);
        return;
    }

    if (container != module) {
        // A CommonJS module's other exports (its `@typedef`s) merge into what `module.exports =` assigns (tsc's
        // getCommonJsExportEquals)
        bool qualified = vec_span_size(&rare->namespaces);
        Symbol const* own = symbol_get_export(module, (qualified ? *rare->namespaces.data : my name).sv);
        if (own && symbol_for_meaning(own, qualified ? SYMBOL_NAMESPACE : SYMBOL_TYPE)) {
            container = module;
        } else if (my jsdoc && !qualified && !own && (container->flags & SYMBOL_VALUE)) {
            // In JavaScript a name that isn't an export is a property of the value (tsc's getTypeFromImportTypeNode:
            // symbolFromVariable), whose type the name means; the checker reports a missing one
            Symbol const* members = symbol_for_meaning(container, SYMBOL_NAMESPACE);
            Symbol const* member = members ? symbol_get_export(members, my name.sv) : nullptr;
            if (!symbol_for_meaning(member, SYMBOL_TYPE)) {
                rare->import_prop_start = 0;
                my target = (Symbol*)container;
                my symbol = container;
                return;
            }
        }
    }
    Symbol const* shown = nullptr;
    for_each (i, vec_span, &rare->namespaces) {
        Symbol const* found;
        container = get_symbol_from_namespace(container, shown, *i, SYMBOL_NAMESPACE, false, &found, errs);
        if (!container || container->is_placeholder) return;
        vec_symbol_cptr_push(&rare->namespace_symbols, found);
        shown = found;
    }
    my target = get_symbol_from_namespace(container, shown, my name, rare->import_typeof ? SYMBOL_VALUE : SYMBOL_TYPE, false,
        &my symbol, errs);
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
        if (csview_equals(my name.sv, intrinsics[i].name)) {
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
        // (tsc's isJSDocIndexSignature: a string or number key)
        TypeNode const* key = vec_spanned_type_size(&my generic_params) == 2 ? &vec_spanned_type_at(&my generic_params, 0)->type : nullptr;
        bool index = key && (key->tag == TYPE_STRING || key->tag == TYPE_NUMBER);
        my jsdoc_intrinsic = index ? JSDOC_INTRINSIC_INDEX_SIGNATURE : JSDOC_INTRINSIC_ANY;
        return true;
    }
    return false;
}

// A JSDoc name in `X.prototype.m = function` that isn't in scope is looked up in X's declaration: its `@template`s
// (tsc's getAssignmentDeclarationLocation). TODO: tsc only does this for the comments of the method and its statement
static Symbol const* jsdoc_prototype_type_param(Scope const* names, Span name) {
    Scope const* scope = names;
    while (scope && scope->kind != SCOPE_FUNCTION) scope = scope->parent;
    FunctionImpl const* function = scope ? scope->function : nullptr;
    Expr const* constructor = function ? function_impl_rare(function)->prototype_of : nullptr;
    if (!constructor) return nullptr;
    Symbol const* symbol = expr_entity_name_symbol(constructor);
    Generic const* generic = nullptr;
    for (u32 i = 0; symbol && i < symbol->decl_count && !generic; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind == SYMBOL_DECL_CLASS) generic = &((ClassDecl const*)decl->node)->generic;
        FunctionImpl const* constructor_function = symbol_decl_js_function(decl);
        if (constructor_function) generic = &constructor_function->generic;
    }
    if (!generic) return nullptr;
    for_each (i, vec_generic_item, &generic->items) {
        if (name_equals(i->name.sv, name.sv)) return i->owned_symbol ? i->owned_symbol : i->symbol_ref;
    }
    return nullptr;
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
        import_type_lookup_names(me, names, errs);
        for_each (i, vec_spanned_type, &my generic_params) {
            type_lookup_names(&i->type, names, errs);
        }
        return;
    }
    if (!my keyword) scope_check_strict_identifier(names, identifier_type_namespace_count(me) ? *my rare->namespaces.data : my name, true);

    if (identifier_type_namespace_count(me)) {
        IdentifierTypeRare* rare = my rare;
        Symbol const* namespace = nullptr;
        for_each (i, vec_span, &rare->namespaces) {
            Symbol const* decl;
            Symbol const* found;
            if (i == rare->namespaces.data /* First namespaces */) {
                decl = scope_get_namespace(names, i->sv, &found);
                unused_mark_referenced(names, (Symbol*)found, SYMBOL_NAMESPACE);
                if (decl && decl->is_placeholder) port_item_check_namespace_import_use(found, names);
                if (!decl && my jsdoc) {
                    // (In JSDoc the left of a qualified name may be a value, whose members are looked in: tsc's
                    // resolveEntityName of a JSDoc type reference falls back to the value meaning)
                    Symbol const* value_found = nullptr;
                    Symbol const* value = scope_get_value(names, i->sv, &value_found);
                    if (value && !value->is_placeholder) {
                        decl = value;
                        found = value_found;
                    }
                }
                if (!decl) {
                    Symbol const* type_symbol = nullptr;
                    Symbol const* type = my jsdoc ? nullptr : scope_get_type(names, i->sv, &type_symbol);
                    if (type && !(type->flags & SYMBOL_NAMESPACE) && !type->is_placeholder) {
                        rare->type_as_namespace = type;
                    } else {
                        scope_report_namespace_not_found(names, *i, errs);
                    }
                }
            } else /* Subsequent namespaces */ {
                Symbol const* member = namespace ? symbol_get_export(namespace, i->sv) : nullptr;
                // (A member that is a value too is TS2749 for the whole name, tsc's canSuggestTypeof)
                Symbol const* type = member && !symbol_for_meaning(member, SYMBOL_NAMESPACE | SYMBOL_VALUE) ?
                    symbol_for_meaning(member, SYMBOL_TYPE) : nullptr;
                if (member && !type && !my jsdoc && !symbol_for_meaning(member, SYMBOL_NAMESPACE) && symbol_for_meaning(member, SYMBOL_VALUE)) {
                    rare->value_member = member;
                    rare->value_member_of = namespace;
                    rare->value_member_at = (u32)(i - rare->namespaces.data);
                    Symbol const* first_found = nullptr;
                    rare->first_value = scope_get_value(names, rare->namespaces.data[0].sv, &first_found);
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
                } else {
                    decl = get_symbol_from_namespace(namespace, nullptr, *i, SYMBOL_NAMESPACE, true, &found, errs);
                }
            }
            // Everything looked up in a placeholder is any
            namespace = decl && !decl->is_placeholder ? decl : nullptr;
            if (!decl) {
                break;
            }
            vec_symbol_cptr_push(&rare->namespace_symbols, found);
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
                        // (A JSDoc `@return` is looked up in the function's scope itself, see function_impl.c)
                        identifier_type_rare(me)->this_in_signature = in_signature || s->jsdoc_parameter_type;
                        break;
                    }
                }
            }
        }
    } else {
        my target = scope_get_type(names, my name.sv, &my symbol);
        // A namespace isn't a type (tsc's checkAndReportErrorForUsingNamespaceAsTypeOrValue, before TS2749)
        if (!my target && !my jsdoc && scope_resolve(names, my name.sv, SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE)) {
            error_ctx_error_d(errs, my name, 2709, "Cannot use namespace '", my name, "' as a type.");
            return;
        }
        if (!my target) my target = scope_get_value(names, my name.sv, &my symbol); // TS2749 below
        if (!my target && my jsdoc) {
            // A value only the function its `@param` or `@return` is on declares (a parameter, a `var`) is still found:
            // tsc's resolveEntityNameFromAssignmentDeclaration looks again from the function itself
            Scope* flagged = nullptr;
            for (Scope* s = names; s && !flagged; s = s->kind == SCOPE_FUNCTION ? nullptr : s->parent) {
                if (s->jsdoc_parameter_type) flagged = s;
            }
            if (flagged) {
                flagged->jsdoc_parameter_type = false;
                my target = scope_get_value(names, my name.sv, &my symbol);
                flagged->jsdoc_parameter_type = true;
            }
        }
        if (!my target && my jsdoc) my target = my symbol = jsdoc_prototype_type_param(names, my name);
        // `exports` of a CommonJS file is the file's symbol, a value (tsc's resolveNameHelper)
        if (!my target && my jsdoc && names->file && names->file->is_commonjs && names->file->scope &&
            span_equals(my name, "exports")
        ) {
            my target = my symbol = names->file->scope->container;
        }
        unused_mark_referenced(names, (Symbol*)my symbol, my target && my target->flags & SYMBOL_TYPE ? SYMBOL_TYPE : SYMBOL_VALUE);
        // In a function's JSDoc a name is looked up from the function too, which makes what it finds there referenced
        // (tsc's resolveEntityNameFromAssignmentDeclaration through getAssignmentDeclarationLocation: the signature's
        // own location): a `@template T` shadowed by a signature's `<T>` in a `@returns` type counts as read
        if (my jsdoc && jsdoc_host_scope && my symbol && !identifier_type_namespace_count(me) && unused_enabled()) {
            Symbol const* host_found = nullptr;
            scope_get_type(jsdoc_host_scope, my name.sv, &host_found);
            if (host_found && host_found != my symbol) unused_mark_referenced(jsdoc_host_scope, (Symbol*)host_found, SYMBOL_TYPE);
        }
        // (In JSDoc a value is looked up too, and suggested: tsc resolves again with meaning Type | Value. But not the
        // function's parameters and variables from its own `@param` types: resolveNameHelper's lastLocation is the
        // JSDoc, not the body)
        if (!my target) {
            Scope const* from = names;
            for (Scope const* s = names; my jsdoc && s && s->kind != SCOPE_FUNCTION; s = s->parent) {
                if (s->parameters && s->parent && s->parent->parent) {
                    from = s->parent->parent;
                    break;
                }
            }
            scope_report_not_found(from, my name, my jsdoc ? SYMBOL_TYPE | SYMBOL_VALUE : SYMBOL_TYPE, false, errs);
        }
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

    // (A value in JSDoc is its type: tsc's getTypeFromJSDocValueReference)
    // (A property of what `export =` names may be a type: the checker decides, lower_identifier())
    bool export_equals_property = my target && my target->decl_count &&
        my target->decls[0].kind == SYMBOL_DECL_EXPORT_EQUALS_PROPERTY && !identifier_type_namespace_count(me);
    if (my target && !my this_kind && !my target->is_placeholder && !(my target->flags & SYMBOL_TYPE) && !my jsdoc &&
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
        default: {
            // A JavaScript constructor function's (symbol_type_generic())
            FunctionImpl const* function = symbol->flags & SYMBOL_CLASS ? symbol_decl_js_function(&symbol->decls[d]) : nullptr;
            return function ? &function->generic : nullptr;
        }
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
                csview a = item->name.sv, b = vec_generic_item_at(&first->items, i)->name.sv;
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
                for (u32 k = 0; k < out->count && !known; k++) known = name_equals(out->items[k]->name.sv, i->name.sv);
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
                    if (name_equals(i->name.sv, out->items[k]->name.sv)) has_default[k] |= i->fallback_explicit;
                }
            }
        }
    }
    for (u32 k = 0; k < out->count; k++) if (!has_default[k]) *min = k + 1;
    return true;
}

Span identifier_type_span(IdentifierType const* me) {
    if (my keyword || identifier_type_import_ref(me) || !span_valid(my name) || !my name.source) return my name;
    char const* start = span_start_ptr(!identifier_type_namespace_count(me) ? my name : *vec_span_front(&my rare->namespaces));
    char const* end = span_end_ptr(my name);
    u32 arg_count = (u32)vec_spanned_type_size(&my generic_params);
    if (arg_count) {
        char const* limit = cstr_str(&my name.source->contents) + cstr_size(&my name.source->contents);
        end = span_end_ptr(vec_spanned_type_at(&my generic_params, arg_count - 1)->span);
        while (end < limit && *end != '>') end++;
        if (end < limit) end++;
    } else {
        // An empty `<>` (TS1099) is still part of the reference
        char const* limit = cstr_str(&my name.source->contents) + cstr_size(&my name.source->contents);
        char const* q = end;
        while (q < limit && (*q == ' ' || *q == '\t')) q++;
        if (q < limit && *q == '<') {
            q++;
            while (q < limit && (*q == ' ' || *q == '\t')) q++;
            if (q < limit && *q == '>') end = q + 1;
        }
    }
    return span_make(my name.source, start, end);
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
    if (my keyword || !span_valid(my name) || !my name.source) return;
    u32 arg_count = (u32)vec_spanned_type_size(&my generic_params);
    Span span = identifier_type_span(me);
    if (jsdoc_named) {
        if (arg_count) error_ctx_error_d(errs, span, 2315, "Type '", my name, "' is not generic.");
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
    if (several_types_arity_fits(symbol, arg_count, span.source)) return false;
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
            cstr_append_sv(&name, merged_type_param(&params, i)->name.sv);
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
}

static void identifier_type_typecheck_inner(IdentifierType const* me, ErrorCtx* errs) {
    IdentifierTypeRare const* rare = my rare;
    if (rare && rare->import_attributes) { // tsc's checkImportType
        ImportAttributes const* attributes = rare->import_attributes;
        if (attributes->is_assert && !cstr_equals(&compiler_options()->ignore_deprecations, "6.0") && attributes->span.sv.size &&
            *attributes->span.sv.buf == '{'
        ) {
            // (On the first token: the `{` of the attributes)
            error_ctx_grammar_error_d(errs, span_make(attributes->span.source, attributes->span.sv.buf, attributes->span.sv.buf + 1), 2880,
                "Import assertions have been replaced by import attributes. Use 'with' instead of 'assert'.");
        }
        import_attributes_resolution_mode(attributes, errs);
    }
    // (tsc's checkImportType doesn't check an import type's type arguments, only their constraints)
    if (!identifier_type_import_ref(me)) {
        for_each (i, vec_spanned_type, &my generic_params) type_typecheck(&i->type, errs);
    }
    if (my keyword == KEYWORD_IDENTIFIER_THIS && my this_kind != THIS_CLASS_INSTANCE && my this_kind != THIS_INTERFACE &&
        !(identifier_type_this_function(me) && ty_js_this_class(identifier_type_this_function(me)) &&
            (!my rare->this_in_signature || function_impl_rare(identifier_type_this_function(me))->prototype_of))
    ) {
        error_ctx_error_d(errs, my name, 2526, "A 'this' type is available only in a non-static member of a class or interface.");
    }
    identifier_type_check_arity(me, errs);
    if (rare && rare->type_as_namespace && vec_span_size(&rare->namespaces)) {
        Checker* checker = current_checker;
        Span left = rare->namespaces.data[0];
        Span right = vec_span_size(&rare->namespaces) > 1 ? rare->namespaces.data[1] : my name;
        Ty const* declared = ty_apparent_type(checker, ty_declared_type_of(checker, rare->type_as_namespace));
        Symbol const* as_namespace = rare->type_as_namespace;
        if (as_namespace->decl_count && as_namespace->decls[0].kind == SYMBOL_DECL_BUILTIN && is_primitive_type_name(left.sv)) {
            // (A primitive type keyword isn't a symbol to tsc: checkAndReportErrorForUsingTypeAsValue, its namespace
            // meaning includes ValueModule)
            error_ctx_error_d(errs, left, 2693, "'", left, "' only refers to a type, but is being used as a value here.");
        } else if (ty_property_of_type(checker, declared, checker_atom(checker, right.sv))) {
            error_ctx_error_d(errs, span_between(left, right), 2713, "Cannot access '", left, ".", right, "' because '", left,
                "' is a type, but not a namespace. Did you mean to retrieve the type of the property '", right,
                "' in '", left, "' with '", left, "[\"", right, "\"]'?");
        } else {
            error_ctx_error_d(errs, left, 2702, "'", left, "' only refers to a type, but is being used as a namespace here.");
        }
    }
    if (rare && rare->value_member) {
        Checker* checker = current_checker;
        isize count = vec_span_size(&rare->namespaces);
        // (tsc's tryGetQualifiedNameAsValue: the names after the first are properties of the value)
        Ty const* value = rare->first_value && !rare->first_value->is_placeholder ? ty_type_of_symbol(checker, rare->first_value) : nullptr;
        for (isize k = 1; value && k <= count; k++) {
            Span part = k < count ? rare->namespaces.data[k] : my name;
            value = ty_prop_type_of(checker, ty_apparent_type(checker, value), checker_atom(checker, part.sv));
        }
        u32 at = rare->value_member_at;
        Span member = rare->namespaces.data[at];
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
        } else if (symbol_for_meaning(rare->value_member, SYMBOL_TYPE)) {
            error_ctx_error_d(errs, right, 2713, "Cannot access '", member, ".", right, "' because '", member,
                "' is a type, but not a namespace. Did you mean to retrieve the type of the property '", right,
                "' in '", member, "' with '", member, "[\"", right, "\"]'?");
        } else {
            cstr qualified = symbol_fully_qualified_name(rare->value_member_of);
            error_ctx_error_d(errs, member, 2694, "Namespace '", qualified, "' has no exported member '", member, "'.");
            cstr_drop(&qualified);
        }
    }
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
    vec_cstr_drop(&my texts);
    vec_type_drop(&my types);
    vec_span_drop(&my type_spans);
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
        // (The silent, cached relation first: the reporting one only for what fails)
        if (ty_is_related(checker, type, allowed, TY_RELATION_ASSIGNABLE)) continue;
        ty_check_assignable(checker, type, allowed, *vec_span_at(&my type_spans, i_index), errs);
    }
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

