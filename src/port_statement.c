#include "port_statement.h"
#include "module_resolution.h"
#include "error.h"
#include "program.h"
#include "source_file.h"
#include "scope.h"
#include "ty/ty.h"

bool port_item_check_namespace_import_use(Symbol const* alias, Scope const* names) {
    if (!alias || !(alias->flags & SYMBOL_ALIAS) || !alias->alias || !alias->alias->item) return false;
    PortItem* item = alias->alias->item;
    Scope const* scope = alias->alias->scope;
    if (!item->in_namespace || !item->module_ref) return false;
    if (item->value_use_checked || !scope || scope->file != names->file) return true;
    item->value_use_checked = true;
    if (!item->module_ref->file && !binder_module_of_ref(names->binder, item->module_ref)) {
        module_ref_report_unresolved(item->module_ref, binder_errs(scope->binder));
    }
    return true;
}

void port_item_drop(PortItem* me) {
    if (my alias_) {
        vec_span_drop(&my alias_->path);
        vec_symbol_cptr_drop(&my alias_->path_symbols);
        ast_delete(my alias_);
    }
}

void import_attribute_drop(ImportAttribute* me) {
    expr_drop(&my value);
}

const PortStatementRare port_statement_no_rare = {0};

PortStatementRare* port_statement_rare_mut(PortStatement* me) {
    if (!my rare) my rare = ast_new(PortStatementRare, {0});
    return my rare;
}

static void port_statement_rare_drop(PortStatementRare* me) {
    cstr_drop(&my target_path);
    if (my bad_specifier) {
        expr_drop(my bad_specifier);
        ast_delete(my bad_specifier);
    }
    vec_expr_drop(&my decorators);
}

void port_statement_rare_trim(PortStatement* me) {
    PortStatementRare const* r = my rare;
    if (!r || span_valid(r->phase_clause) || cstr_size(&r->target_path) || span_valid(r->missing_specifier) ||
        r->bad_specifier || span_valid(r->non_string_specifier) || vec_expr_size(&r->decorators)
    ) {
        return;
    }
    port_statement_rare_drop(my rare);
    ast_delete(my rare);
    my rare = nullptr;
}

csview port_statement_target_path(PortStatement const* me) {
    return my module_ref ? cstr_sv(&my module_ref->path) : cstr_sv(&port_statement_rare(me)->target_path);
}

void port_statement_drop(PortStatement* me) {
    port_item_drop(&my main);
    vec_port_item_drop(&my items);
    import_attributes_free(my attributes);
    if (my rare) port_statement_rare_drop(my rare);
    ast_delete(my rare);
}

void import_attributes_free(ImportAttributes* me) {
    if (!me) return;
    vec_import_attribute_drop(&my elements);
    ast_delete(me);
}

static bool is_string_literal(Expr const* expr) {
    return expr->tag == EXPR_LITERAL && expr->literal.tag == VALUE_STRING;
}

// An attribute value tsc's isStringLiteral takes: not a template, which is a string EXPR_LITERAL too
bool import_attribute_value_is_string_literal(Expr const* value) {
    return is_string_literal(value) && expr_outer_span(value).size && expr_outer_span(value).buf[0] != '`';
}

ImportResolutionOverride import_attributes_resolution_mode(ImportAttributes const* me, ErrorCtx* errs) {
    if (!me) return IMPORT_RESOLUTION_DEFAULT;
    // (tsgo's GetResolutionModeOverride: the first attribute of that name, whatever else there is)
    for_each (element, vec_import_attribute, &my elements) {
        if (*element->name.buf != '"' && *element->name.buf != '\'') continue;
        csview name = csview_slice(span_sv(element->name), 1, element->name.size - 1);
        if (!csview_equals(name, "resolution-mode")) continue;
        if (!is_string_literal(&element->value)) return IMPORT_RESOLUTION_DEFAULT;
        csview value = cstr_sv(&element->value.literal.string);
        if (csview_equals(value, "import")) return IMPORT_RESOLUTION_IMPORT;
        if (csview_equals(value, "require")) return IMPORT_RESOLUTION_REQUIRE;
        if (errs) {
            error_ctx_grammar_error_d(errs, expr_outer_span(&element->value), 1453, "`resolution-mode` should be either `require` or `import`.");
        }
        return IMPORT_RESOLUTION_DEFAULT;
    }
    return IMPORT_RESOLUTION_DEFAULT;
}

// An export declaration (`export { a }`, `export * from "m"`), not an exported declaration or `export =`/`export import`
static bool port_statement_is_export_declaration(PortStatement const* me) {
    if (!csview_equals(span_sv(my keyword_span), "export")) return false;
    switch (my main.tag) {
        case PORT_IMPORT_DEFAULT: case PORT_IMPORT_GATHER: case PORT_IMPORT_SPECIFIC: case PORT_IMPORT_ALIAS:
        case PORT_EXPORT_AS_NAMESPACE:
            return false; // (`export import x = ...`, `export as namespace N`)
        default:
            return true;
    }
}

void port_statement_add_names(PortStatement* me, Scope* scope) {
    // (tsc's checkGrammarModuleElementContext: a file, a namespace body or an ambient module's)
    bool top = scope->kind == SCOPE_MODULE || scope->kind == SCOPE_SCRIPT || scope->kind == SCOPE_GLOBAL_AUGMENTATION ||
        (scope->container && (scope->container->is_module || scope->container->is_augmentation));
    my in_namespace = !top && scope->kind == SCOPE_NAMESPACE;
    my in_ambient_namespace = my in_namespace && scope->ambient;
    my main.in_namespace = my in_namespace;
    for_each (i, vec_port_item, &my items) i->in_namespace = my in_namespace;
    my in_ambient_module = scope->kind == SCOPE_NAMESPACE && scope->container && scope->container->is_module &&
        scope->parent && scope->parent->kind != SCOPE_MODULE;
    bool is_import_alias = my main.tag == PORT_IMPORT_ALIAS && csview_equals(span_sv(my keyword_span), "import");
    if (!my jsdoc && !top && !my in_namespace &&
        (my module_ref || span_valid(port_statement_rare(me)->non_string_specifier) || is_import_alias || port_statement_is_export_declaration(me))
    ) {
        my wrong_context = true;
        bool is_export = csview_equals(span_sv(my keyword_span), "export");
        // (A JavaScript file has no namespaces to mention)
        if (span_source(my keyword_span) && span_source(my keyword_span)->is_js) {
            error_ctx_error_d(binder_errs(scope->binder), my keyword_span, is_export ? 1474 : 1473, is_export ?
                "An export declaration can only be used at the top level of a module." :
                "An import declaration can only be used at the top level of a module.");
        } else {
            error_ctx_error_d(binder_errs(scope->binder), my keyword_span, is_export ? 1233 : 1232, is_export ?
                "An export declaration can only be used at the top level of a namespace or module." :
                "An import declaration can only be used at the top level of a namespace or module.");
        }
        // (Declared all the same, in the block: tsc's binder doesn't mind, its checker stops at the grammar error)
        my main.wrong_context = true;
        for_each (i, vec_port_item, &my items) i->wrong_context = true;
    } else if (span_valid(port_statement_rare(me)->non_string_specifier)) {
        error_ctx_error_d(binder_errs(scope->binder), port_statement_rare(me)->non_string_specifier, 1141, "String literal expected.");
    }

    if (my attributes && my module_ref && !my jsdoc && !my in_namespace && !my is_import_require) {
        bool invalid = false;
        for_each (i, vec_import_attribute, &my attributes->elements) invalid |= !import_attribute_value_is_string_literal(&i->value);
        my main.clause_unchecked = invalid;
        for_each (i, vec_port_item, &my items) i->clause_unchecked = invalid;
    }
    my main.keyword_span = my keyword_span;
    scope_add_port(scope, &my main);
    for_each (i, vec_port_item, &my items) {
        i->keyword_span = my keyword_span;
        scope_add_port(scope, i);
    }
}


// What tsc's checkGrammarImportClause says about a phase modifier (`import defer`, `import source`), 0 if nothing
static unsigned phase_clause_error(PortStatement const* me) {
    if (!span_valid(port_statement_rare(me)->phase_clause)) return 0;
    ModuleKind module = compiler_options()->module;
    bool name = my main.tag == PORT_IMPORT_DEFAULT;
    // (The clause is the modifier alone when there are no bindings: `import source "m"`)
    bool bindings = my main.tag == PORT_IMPORT_GATHER || vec_port_item_size(&my items) ||
        port_statement_rare(me)->phase_clause.size > (my source_phase ? 6 : 5);
    bindings &= !(name && !vec_port_item_size(&my items));
    if (my source_phase) {
        if (bindings) return 18112;
        if (!name) return 18111;
        if (module == MODULE_KIND_ESNEXT || module == MODULE_KIND_NODENEXT || module == MODULE_KIND_PRESERVE) {
            // (tsc's getEmitSyntaxForModuleSpecifierExpression, of a string literal specifier)
            bool string_specifier = span_valid(my target_path_span);
            return string_specifier && source_file_emit_module_kind(span_source(port_statement_rare(me)->phase_clause)) == MODULE_KIND_COMMONJS ? 18115 : 0;
        }
        return 18113;
    }
    if (name) return 18058;
    if (!bindings) return 18117;
    bool named = my main.tag == PORT_NOOP;
    for_each (i, vec_port_item, &my items) named |= i->tag != PORT_IMPORT_GATHER;
    if (named) return 18059;
    return module == MODULE_KIND_ESNEXT || module == MODULE_KIND_PRESERVE ? 0 : 18060;
}

void port_statement_lookup_names(PortStatement* me, Scope* scope, ErrorCtx* errs) {
    for_each (i, vec_expr, &port_statement_rare(me)->decorators) expr_lookup_names(i, scope, nullptr);
    if (port_statement_rare(me)->bad_specifier) expr_lookup_names(port_statement_rare(me)->bad_specifier, scope, nullptr);
    // (the values of `with { type: "json" }` are expressions like any other: checkImportAttribute checks them)
    if (my attributes) {
        for_each (i, vec_import_attribute, &my attributes->elements) expr_lookup_names(&i->value, scope, errs);
    }
    if (my wrong_context) {
        // (tsgo's checkExternalModuleNameInGlobalScope: in a block of the file itself, the module is still resolved;
        // not `import "m"`)
        Scope const* container = scope;
        while (container && container->kind == SCOPE_BLOCK) container = container->parent;
        bool side_effect_import = my side_effect_only;
        if (container && (container->kind == SCOPE_MODULE || container->kind == SCOPE_SCRIPT) && !side_effect_import) {
            module_ref_report(my module_ref, scope, errs);
        }
        return;
    }
    // `export { x }` of a local `let`: it may be assigned from anywhere, closures don't see its narrowing (tsc's
    // markNodeAssignments of an ExportSpecifier)
    if (!my module_ref && port_statement_is_export_declaration(me)) {
        for_each (i, vec_port_item, &my items) {
            Symbol* target = (Symbol*)symbol_resolve_alias(i->symbol); // (Local: this file's walk owns it)
            if (target && target->is_local && (target->flags & SYMBOL_VARIABLE) && !target->is_const) {
                target->last_assignment_end = (char const*)UINTPTR_MAX;
            }
        }
    }
    // Ambient modules of every file are known by now. tsc doesn't mind unresolved side effect imports (`import "./a"`)
    // (In a namespace it is TS1147/TS1194 instead, and nothing is resolved: tsc's checkExternalImportOrExportDeclaration)
    bool unchecked = (my side_effect_only && !compiler_options()->no_unchecked_side_effect_imports) || my in_namespace;
    // (An import clause with a grammar error resolves nothing either: tsc's checkImportDeclaration skips its bindings
    // when checkGrammarImportClause reports, TS1363 or a deferred import's TS18058-TS18060)
    if (!my jsdoc && my main.tag == PORT_IMPORT_DEFAULT && my main.type_only && vec_port_item_size(&my items)) unchecked = true;
    if (phase_clause_error(me)) unchecked = true;
    if (my module_ref && !unchecked && !my module_ref->file &&
        !binder_module_of_ref(scope->binder, my module_ref)
    ) {
        // (an implicitly `any` module is no problem for an import that imports nothing)
        if (my side_effect_only) {
            if (my module_ref->failure != MODULE_REF_IS_JS) module_ref_report_unresolved_side_effect(my module_ref, errs);
        } else {
            module_ref_report_unresolved(my module_ref, errs);
        }
    } else if (my module_ref && my module_ref->file && !unchecked) {
        // (A side effect import is resolved only for noUncheckedSideEffectImports, and has no import clause, which is
        // what tsc reports TS2846 for)
        module_ref_report_resolved(my module_ref, my main.type_only, my side_effect_only, errs);
    }
}

// The tslib helpers of CommonJS-like emit (tsc's checkImportDeclaration/checkExportDeclaration/checkImportBinding)
static void port_statement_check_emit_helpers(PortStatement const* me, Span span, ErrorCtx* errs) {
    if (!my module_ref || !ty_emits_commonjs_like(span_source(span))) return;
    switch (my main.tag) {
        case PORT_IMPORT_GATHER:
            if (!my main.is_require) ty_check_emit_helpers(current_checker, span, EMIT_HELPER_IMPORT_STAR, errs);
            break;
        case PORT_REEXPORT_GATHER:
            ty_check_emit_helpers(current_checker, span, EMIT_HELPER_IMPORT_STAR, errs);
            break;
        case PORT_REEXPORT_SPLAT: {
            // (a module with `export =` is TS2498 instead)
            Symbol* module = binder_module_of_ref(current_checker->binder, my module_ref);
            if (!module || !symbol_table_get(module->exports, c_sv("export="))) {
                ty_check_emit_helpers(current_checker, span, EMIT_HELPER_EXPORT_STAR, errs);
            }
        }   break;
        case PORT_IMPORT_DEFAULT: {
            // `import d from "m"` (tsgo's checkImportDeclaration), unless a namespace import needs __importStar
            bool star = false;
            for_each (i, vec_port_item, &my items) star |= i->tag == PORT_IMPORT_GATHER;
            ty_check_emit_helpers(current_checker, span, star ? EMIT_HELPER_IMPORT_STAR : EMIT_HELPER_IMPORT_DEFAULT, errs);
        }   break;
        default:
            break;
    }
    // (tsc's checkImportDeclaration checks named imports' bindings only when the module resolves)
    bool resolved = my module_ref->file || binder_module_of_ref(current_checker->binder, my module_ref);
    for_each (i, vec_port_item, &my items) {
        if (i->tag != PORT_IMPORT_SPECIFIC && i->tag != PORT_REEXPORT_SPECIFIC) continue;
        if (i->tag == PORT_IMPORT_SPECIFIC && !resolved) continue;
        if (csview_equals(port_item_name(i->source_name), "default")) {
            Span at = i->full_span; // (which may end in whitespace)
            while (at.size && (at.buf[at.size - 1] == ' ' || at.buf[at.size - 1] == '\n' ||
                at.buf[at.size - 1] == '\t' || at.buf[at.size - 1] == '\r')) at.size--;
            ty_check_emit_helpers(current_checker, at, EMIT_HELPER_IMPORT_DEFAULT, errs);
        }
    }
}

// tsc's checkAliasSymbol: a JavaScript file can't import or export what is only a type (TS18042, TS18043)
static void port_item_check_js_type(PortStatement const* me, PortItem const* item, ErrorCtx* errs) {
    switch (item->tag) {
        case PORT_IMPORT_DEFAULT:
        case PORT_IMPORT_SPECIFIC:
        case PORT_EXPORT_SPECIFIC:
        case PORT_REEXPORT_SPECIFIC:
            break;
        default:
            return;
    }
    // (A type-only one has its TS8006 already: tsc's isTypeOnlyImportOrExportDeclaration)
    if (!item->symbol || !item->symbol->alias || item->jsdoc || item->type_only || my main.type_only) return;
    Symbol const* target = symbol_resolve_alias(item->symbol);
    if (!target || target->is_placeholder || target->is_unresolved) return;
    // (A declaration merged with an alias that resolves to nothing, `export { T }` of the file's own typedef T:
    // its own meanings)
    u32 flags = symbol_resolved_flags(target);
    if ((target->flags & SYMBOL_ALIAS) && (target->flags & SYMBOL_ANY_MEANING)) {
        Symbol const* aliased = symbol_resolve_alias((Symbol*)target);
        if (!aliased || aliased->is_placeholder) flags = target->flags & ~(u32)SYMBOL_ALIAS;
    }
    if (flags & SYMBOL_VALUE) return;
    Span at = span_valid(item->source_name) ? item->source_name : item->decl_name;
    if (!span_valid(at)) return;
    if (port_item_is_export(item)) {
        Error* error = error_ctx_error_d(errs, at, 18043, "Types cannot appear in export declarations in JavaScript files.");
        // A JSDoc type the file exports by itself
        for (u32 i = 0; i < target->decl_count; i++) {
            SymbolDecl const* decl = &target->decls[i];
            if (decl->kind != SYMBOL_DECL_TYPE_ALIAS || !((TypeDecl const*)decl->node)->jsdoc) continue;
            if (span_source(decl->full) != span_source(at)) continue;
            error_add_related_d(error, span_valid(decl->name) ? decl->name : decl->full, 18044, "'", target->name, "' is automatically exported here.");
            break;
        }
    } else {
        csview name = port_item_name(at);
        csview specifier = my module_ref ? port_statement_target_path(me) : c_sv("...");
        // (`import("m").T` for a named import, `import("m")` for a default one: tsgo's checkAliasSymbol)
        bool named = item->tag == PORT_IMPORT_SPECIFIC;
        error_ctx_error_d(errs, at, 18042, "'", name, "' is a type and cannot be imported in JavaScript files. Use 'import(\"",
            specifier, "\")", named ? "." : "", named ? name : c_sv(""), "' in a JSDoc type annotation.");
    }
}

// The same for JavaScript's `const t = require("m")`, `const { t } = require("m")` (tsc's checkAliasSymbol of the
// variable declaration or binding element: `import("m")`, never `.t`)
void port_item_check_js_require_type(PortItem const* item, ErrorCtx* errs) {
    if (!item->symbol || !item->symbol->alias || !item->module_ref) return;
    Symbol const* target = symbol_resolve_alias(item->symbol);
    if (!target || target->is_placeholder || target->is_unresolved) return;
    // (The alias's meanings: a typedef with the `export=` value's property as its value half is a value too)
    if ((symbol_resolved_flags(target) | symbol_resolved_flags(item->symbol)) & SYMBOL_VALUE) return;
    error_ctx_error_d(errs, item->decl_name, 18042, "'", item->decl_name, "' is a type and cannot be imported in JavaScript "
        "files. Use 'import(\"", cstr_sv(&item->module_ref->path), "\")' in a JSDoc type annotation.");
}

static void report_string_export_name(Span name, ErrorCtx* errs) {
    if (!span_valid(name) || !name.size || (name.buf[0] != '"' && name.buf[0] != '\'')) return;
    error_ctx_grammar_error_d(errs, name, 18057,
        "String literal import and export names are not supported when the '--module' flag is set to 'es2015' or 'es2020'.");
}

static void report_string_export_names(PortItem const* item, ErrorCtx* errs) {
    report_string_export_name(item->source_name, errs);
    if (!span_eq(item->source_name, item->decl_name)) report_string_export_name(item->decl_name, errs);
}

// tsc's isOnlyImportableAsDefault: in Node.js an ES module's import of JSON (a .json file or a .d.json.ts
// declaration) gets only a default export
static bool imports_json_as_esm(PortStatement const* me) {
    ModuleKind module = compiler_options()->module;
    if (module < MODULE_KIND_NODE16 || module > MODULE_KIND_NODENEXT || my is_import_require || !my module_ref) return false;
    SourceFile const* from = span_source(my keyword_span);
    SourceFile const* target = my module_ref->file;
    if (!from || !from->is_esm || !target) return false;
    return target->is_json || csview_ends_with(cstr_sv(&target->canonical_path), ".d.json.ts");
}

static char const* node_module_kind_name(ModuleKind kind) {
    return kind == MODULE_KIND_NODE16 ? "Node16" : kind == MODULE_KIND_NODE18 ? "Node18" : kind == MODULE_KIND_NODE20 ? "Node20" : "NodeNext";
}

// tsc's checkImportDeclaration (TS1543) and getExternalModuleMember (TS1544) for imports of JSON into an ES module
static void port_statement_check_json_import(PortStatement const* me, ErrorCtx* errs) {
    if (my jsdoc || !imports_json_as_esm(me)) return;
    ModuleKind module = compiler_options()->module;
    bool is_import = span_equals(my keyword_span, "import");
    if (is_import && !my main.type_only && module >= MODULE_KIND_NODE18) {
        bool type_json = false;
        for_each (i, vec_import_attribute, my attributes ? &my attributes->elements : &(VecImportAttribute){0}) {
            csview name = span_sv(i->name);
            if (name.size >= 2 && (*name.buf == '"' || *name.buf == '\'')) name = csview_slice(name, 1, name.size - 1);
            Expr const* value = &i->value;
            type_json |= csview_equals(name, "type") && value->tag == EXPR_LITERAL && value->literal.tag == VALUE_STRING &&
                cstr_equals(&value->literal.string, "json");
        }
        if (!type_json) {
            error_ctx_error_d(errs, my target_path_span, 1543, "Importing a JSON file into an ECMAScript module requires a "
                "'type: \"json\"' import attribute when 'module' is set to '", node_module_kind_name(module), "'.");
        }
    }
    for_each (i, vec_port_item, &my items) {
        if (i->tag != PORT_IMPORT_SPECIFIC && i->tag != PORT_REEXPORT_SPECIFIC) continue;
        if (csview_equals(port_item_name(i->source_name), "default")) continue;
        error_ctx_error_d(errs, i->source_name, 1544, "Named imports from a JSON file into an ECMAScript module are not "
            "allowed when 'module' is set to '", node_module_kind_name(module), "'.");
    }
}

// `import { x }` (or JavaScript's `const { x } = require()`) of a property of what `export =` names: tsc's
// getExternalModuleMember finds none if the value has no such property (the binder leaves that to the checker, see
// resolve_alias_target())
void port_item_check_export_equals_property(PortItem const* item, ErrorCtx* errs) {
    if ((item->tag != PORT_IMPORT_SPECIFIC && item->tag != PORT_REEXPORT_SPECIFIC && item->tag != PORT_IMPORT_DEFAULT) ||
        !item->symbol || item->jsdoc
    ) {
        return;
    }
    Symbol const* target = symbol_resolve_alias(item->symbol);
    if (!target || !target->decl_count || target->decls[0].kind != SYMBOL_DECL_EXPORT_EQUALS_PROPERTY) return;
    Symbol const* assigned = target->decls[0].node;
    if (ty_type_of_symbol(current_checker, assigned)->flags & TYF_ERROR) return;
    if (!ty_export_equals_property_missing(current_checker, target)) return;
    Symbol const* module = binder_module_of_ref(item->symbol->binder, item->module_ref);
    if (module && item->tag == PORT_IMPORT_DEFAULT) scope_report_default_import_of_export_equals(item, module, errs);
    else if (module) scope_report_non_exported_member(item, module, errs);
}

void port_statement_typecheck(PortStatement const* me, Span span, bool ambient, ErrorCtx* errs) {
    if (my in_namespace && !my jsdoc) {
        if (port_statement_is_export_declaration(me)) {
            // (tsc's checkExportDeclaration: at the module name with one, else at the declaration unless ambient)
            if (my module_ref) {
                error_ctx_error_d(errs, my module_ref->span, 1194, "Export declarations are not permitted in a namespace.");
            } else if (!my in_ambient_namespace && my main.tag != PORT_REEXPORT_SPLAT && my main.tag != PORT_REEXPORT_GATHER) {
                error_ctx_error_d(errs, span_with_semicolon(span), 1194, "Export declarations are not permitted in a namespace.");
            }
        } else if (my module_ref) {
            error_ctx_error_d(errs, my module_ref->span, 1147, "Import declarations in a namespace cannot reference a module.");
        }
    }
    // tsc's checkGrammarNamedImportsOrExports: the first specifier with a redundant `type`
    if (my main.type_only && !my jsdoc) {
        for_each (i, vec_port_item, &my items) {
            if (!i->type_modifier) continue;
            bool is_import = i->tag == PORT_IMPORT_SPECIFIC;
            error_ctx_grammar_error_d(errs, span_make_in(i->full_span, i->full_span.buf, i->full_span.buf + 4),
                is_import ? 2206 : 2207, "The 'type' modifier cannot be used on a named ", is_import ? "import" : "export",
                " when '", is_import ? "import" : "export", " type' is used on its ", is_import ? "import" : "export", " statement.");
            break;
        }
    }
    // tsc's checkExportDeclaration: `export *` (as ns or not) of a module with `export =`
    if ((my main.tag == PORT_REEXPORT_SPLAT || my main.tag == PORT_REEXPORT_GATHER) && my module_ref && !my jsdoc) {
        Symbol* module = binder_module_of_ref(current_checker->binder, my module_ref);
        if (module && module->exports && symbol_table_get(module->exports, c_sv("export="))) {
            cstr name = module_name_for_error(module, my module_ref, false);
            error_ctx_error_d(errs, my target_path_span, 2498, "Module '", name, "' uses 'export =' and cannot be used with 'export *'.");
            cstr_drop(&name);
        }
    }
    // tsc's checkAndReportErrorForResolvingImportAliasToTypeOnlySymbol: `import A = a.B` through a type-only import
    // or export, at the name
    // (Each prefix of the name, longest first: what it names, unresolved, and that alias's type-only declaration)
    PortItem const* alias_type_only = nullptr;
    if (my main.tag == PORT_IMPORT_ALIAS && !my main.type_only && !my jsdoc && my main.symbol &&
        !vec_span_is_empty(&port_item_alias(&my main)->path) && symbol_resolve_alias(my main.symbol)
    ) {
        for (isize i = vec_symbol_cptr_size(&port_item_alias(&my main)->path_symbols); i-- > 0 && !alias_type_only;) {
            Symbol const* part = *vec_symbol_cptr_at(&port_item_alias(&my main)->path_symbols, i);
            if (part && (part->flags & SYMBOL_ALIAS)) alias_type_only = symbol_type_only_declaration(part);
        }
    }
    if (alias_type_only) {
        bool exported = port_item_is_export(alias_type_only);
        Span first = *vec_span_front(&port_item_alias(&my main)->path), last = *vec_span_back(&port_item_alias(&my main)->path);
        Error* error = error_ctx_error_d(errs, span_make_in(first, span_start_ptr(first), span_end_ptr(last)),
            exported ? 1379 : 1380, "An import alias cannot reference a declaration that was ",
            exported ? "exported using 'export type'." : "imported using 'import type'.");
        port_item_add_type_only_related(error, alias_type_only, port_item_name(alias_type_only->decl_name));
    }
    port_statement_check_json_import(me, errs);
    // (tsc's checkImportEqualsDeclaration)
    if (compiler_options()->erasable_syntax_only && !ambient && !my jsdoc && !my wrong_context && (my main.tag == PORT_IMPORT_ALIAS || my is_import_require)) {
        error_ctx_error_d(errs, span_with_semicolon(span), 1294, "This syntax is not allowed when 'erasableSyntaxOnly' is enabled.");
    }
    if (!ambient) port_statement_check_emit_helpers(me, span, errs);
    // `export * from "./script"` (tsc's checkExportDeclaration: resolveExternalModuleName; aliases say it themselves)
    if (my main.tag == PORT_REEXPORT_SPLAT && my module_ref && my module_ref->file && !my jsdoc && !my wrong_context &&
        !binder_module_of_ref(current_checker->binder, my module_ref)
    ) {
        cstr shown = module_ref_file_message_path(my module_ref);
        error_ctx_error_d(errs, my module_ref->span, 2306, "File '", shown, "' is not a module.");
        cstr_drop(&shown);
    }
    // tsc's checkExternalImportOrExportDeclaration
    if (my in_ambient_module && my module_ref && !my jsdoc && module_name_is_relative(port_statement_target_path(me))) {
        error_ctx_error_d(errs, span_with_semicolon(span), 2439, "Import or export declaration in an ambient module "
            "declaration cannot reference module through relative module name.");
    } else if (my attributes && my module_ref && !my jsdoc && !my in_namespace && !my is_import_require) {
        // (a string literal: not a template, which isStringLiteral isn't)
        for_each (i, vec_import_attribute, &my attributes->elements) {
            Expr const* value = &i->value;
            if (import_attribute_value_is_string_literal(value)) continue;
            if (my attributes->is_assert) {
                error_ctx_error_d(errs, expr_outer_span(value), 2837, "Import assertion values must be string literal expressions.");
            } else {
                error_ctx_error_d(errs, expr_outer_span(value), 2858, "Import attribute values must be string literal expressions.");
            }
        }
    }
    // tsc's checkImportEqualsDeclaration: `import x = require()` can't be emitted as an ES module (by the module
    // option, whatever the file's format)
    ModuleKind module_kind = compiler_options()->module;
    if (my is_import_require && !ambient && !my main.type_only && !my wrong_context && !my in_namespace &&
        module_kind >= MODULE_KIND_ES2015 && module_kind <= MODULE_KIND_ESNEXT
    ) {
        error_ctx_grammar_error_d(errs, span_with_semicolon(span), 1202,
            "Import assignment cannot be used when targeting ECMAScript modules. Consider using 'import * as ns from \"mod\"', "
            "'import {a} from \"mod\"', 'import d from \"mod\"', or another module format instead.");
    }
    // tsc's checkImportEqualsDeclaration: `import x = A.B` whose target is a value, where the first name as a value or
    // namespace is something other than a namespace (the lookup of the alias found a namespace further out)
    PortItem const* alias = &my main;
    if (alias->tag == PORT_IMPORT_ALIAS && alias->symbol && alias->symbol->alias && vec_span_size(&port_item_alias(alias)->path)) {
        Symbol const* target = symbol_resolve_alias(alias->symbol);
        if (target && !target->is_placeholder && symbol_resolved_flags(target) & SYMBOL_VALUE) {
            Span first = *vec_span_at(&port_item_alias(alias)->path, 0);
            Symbol const* found = scope_resolve(alias->symbol->alias->scope, span_sv(first), SYMBOL_VALUE | SYMBOL_NAMESPACE);
            if (found && !(symbol_resolved_flags(found) & SYMBOL_NAMESPACE)) {
                error_ctx_error_d(errs, first, 2437, "Module '", first, "' is hidden by a local declaration with the same name.");
            }
        }
        if (target && !target->is_placeholder && symbol_resolved_flags(target) & SYMBOL_TYPE &&
            type_name_is_reserved(span_sv(alias->decl_name))
        ) {
            error_ctx_error_d(errs, alias->decl_name, 2438, "Import name cannot be '", alias->decl_name, "'.");
        }
        // tsc's markImportEqualsAliasReferenced: `export import x = A.B` of a value (not a const enum) or of nothing
        // references A as a value (markIdentifierAliasReferenced -> getResolvedSymbol), a namespace only is TS2708
        u32 target_flags = target ? symbol_resolved_flags(target) : 0;
        bool unknown = !target || target->is_placeholder || target->is_unresolved;
        bool value = target_flags & SYMBOL_VALUE && (target_flags & SYMBOL_VALUE & ~SYMBOL_CONST_ENUM);
        // (Not under verbatimModuleSyntax: tsc's canCollectSymbolAliasAccessibilityData. Nothing found is TS2304, like
        // any value use of the name)
        if (alias->export_import && !ambient && !alias->declared && (unknown || value) &&
            !compiler_options()->verbatim_module_syntax
        ) {
            Span first = *vec_span_at(&port_item_alias(alias)->path, 0);
            Scope const* scope = alias->symbol->alias->scope;
            Symbol const* value_found = scope_resolve(scope, span_sv(first), SYMBOL_VALUE);
            if (!value_found && scope_resolve(scope, span_sv(first), SYMBOL_NAMESPACE)) {
                error_ctx_error_d(errs, first, 2708, "Cannot use namespace '", first, "' as a value.");
            } else if (!value_found) {
                scope_report_value_not_found(scope, first, false, errs);
            }
        }
    }
    // tsc's checkModuleExportName: `import { "a" as b }`, `export { a as "b" }` can't be emitted as ES2015/ES2020 modules
    if ((module_kind == MODULE_KIND_ES2015 || module_kind == MODULE_KIND_ES2020) && !my jsdoc) {
        report_string_export_names(&my main, errs);
        for_each (i, vec_port_item, &my items) report_string_export_names(i, errs);
    }
    // tsc's checkImportEqualsDeclaration: `import type A = N.B`
    if (alias->tag == PORT_IMPORT_ALIAS && alias->type_only) {
        error_ctx_grammar_error_d(errs, span_with_semicolon(span), 1392, "An import alias cannot use 'import type'");
    }
    // tsc's checkGrammarImportClause: `import type A, { B }` (the clause, from `type` to the bindings' end)
    if (!my jsdoc && my main.tag == PORT_IMPORT_DEFAULT && my main.type_only && vec_port_item_size(&my items) &&
        span_valid(my keyword_span)
    ) {
        const char* start = span_end_ptr(my keyword_span);
        while (*start == ' ' || *start == '\t') start++;
        PortItem const* last = vec_port_item_back(&my items);
        const char* end = span_end_ptr(last->full_span);
        if (last->tag != PORT_IMPORT_GATHER) {
            while (*end && *end != '}') end++;
            if (*end) end++;
        }
        error_ctx_grammar_error_d(errs, span_make_in(my keyword_span, start, end), 1363,
            "A type-only import can specify a default import or named bindings, but not both.");
    }
    // tsc's checkGrammarImportClause: a deferred import is of a namespace, where the module format can defer it; a
    // source phase import is of a default binding alone, where the module format has them
    switch (phase_clause_error(me)) {
        case 18058:
            error_ctx_grammar_error_d(errs, port_statement_rare(me)->phase_clause, 18058, "Default imports are not allowed in a deferred import.");
            break;
        case 18117:
            error_ctx_grammar_error_d(errs, port_statement_rare(me)->phase_clause, 18117, "A deferred import must specify a namespace binding.");
            break;
        case 18059:
            error_ctx_grammar_error_d(errs, port_statement_rare(me)->phase_clause, 18059, "Named imports are not allowed in a deferred import.");
            break;
        case 18060:
            error_ctx_grammar_error_d(errs, port_statement_rare(me)->phase_clause, 18060,
                "Deferred imports are only supported when the '--module' flag is set to 'esnext' or 'preserve'.");
            break;
        case 18112:
            error_ctx_grammar_error_d(errs, port_statement_rare(me)->phase_clause, 18112,
                "Named and namespace imports are not allowed in a source phase import.");
            break;
        case 18111:
            error_ctx_grammar_error_d(errs, port_statement_rare(me)->phase_clause, 18111, "A source phase import must specify a local binding.");
            break;
        case 18115:
            error_ctx_grammar_error_d(errs, port_statement_rare(me)->phase_clause, 18115,
                "Source phase imports are not allowed on statements that compile to CommonJS 'require' calls.");
            break;
        case 18113:
            error_ctx_grammar_error_d(errs, port_statement_rare(me)->phase_clause, 18113, "Source phase imports are only supported when the "
                "'--module' option is set to 'esnext', 'nodenext', or 'preserve'.");
            break;
    }
    port_item_check_export_equals_property(&my main, errs);
    for_each (i, vec_port_item, &my items) port_item_check_export_equals_property(i, errs);
    if (span_source(span) && span_source(span)->is_js && !my jsdoc) {
        port_item_check_js_type(me, &my main, errs);
        for_each (i, vec_port_item, &my items) port_item_check_js_type(me, i, errs);
    }
    // tsc's checkImportAttributes()
    ImportAttributes const* node = my attributes;
    if (!node) return;
    ty_check_import_attributes(current_checker, node, errs);
    for_each (i, vec_import_attribute, &node->elements) expr_typecheck(&i->value, errs);
    bool type_only = my main.type_only; // tsc's isExclusivelyTypeOnlyImportOrExport
    ImportResolutionOverride override = import_attributes_resolution_mode(node, type_only ? errs : nullptr);
    // (The other checks don't apply to type-only imports and exports. `assert` is the parser's TS2880 in TypeScript 7,
    // which reports it like `with`)
    if (type_only) return;
    CompilerOptions const* options = compiler_options();
    ModuleKind module = options->module;
    bool supported = module == MODULE_KIND_NODE18 || module == MODULE_KIND_NODE20 || module == MODULE_KIND_NODENEXT || module == MODULE_KIND_PRESERVE ||
        module == MODULE_KIND_ESNEXT;
    if (!supported) {
        error_ctx_grammar_error_d(errs, node->span, 2823,
            "Import attributes are only supported when the '--module' option is set to 'esnext', 'node18', 'node20', 'nodenext', or 'preserve'.");
        return;
    }
    bool emits_require = (module == MODULE_KIND_NODE18 || module == MODULE_KIND_NODE20 || module == MODULE_KIND_NODENEXT) &&
        !span_source(node->span)->is_esm;
    if (emits_require) {
        error_ctx_grammar_error_d(errs, node->span, 2856,
            "Import attributes are not allowed on statements that compile to CommonJS 'require' calls.");
        return;
    }
    if (override) error_ctx_grammar_error_d(errs, node->span, 1454, "`resolution-mode` can only be set for type-only imports.");
}
