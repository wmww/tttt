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
    vec_span_drop(&my alias_path);
    vec_symbol_cptr_drop(&my alias_path_symbols);
}

void import_attribute_drop(ImportAttribute* me) {
    expr_drop(&my value);
}

void port_statement_drop(PortStatement* me) {
    port_item_drop(&my main);
    vec_port_item_drop(&my items);
    cstr_drop(&my target_path);
    import_attributes_free(my attributes);
    vec_expr_drop(&my decorators);
    if (my bad_specifier) {
        expr_drop(my bad_specifier);
        free(my bad_specifier);
    }
}

void import_attributes_free(ImportAttributes* me) {
    if (!me) return;
    vec_import_attribute_drop(&my elements);
    free(me);
}

static bool is_string_literal(Expr const* expr) {
    return expr->tag == EXPR_LITERAL && expr->literal.tag == VALUE_STRING;
}

ImportResolutionOverride import_attributes_resolution_mode(ImportAttributes const* me, ErrorCtx* errs) {
    if (!me) return IMPORT_RESOLUTION_DEFAULT;
    if (vec_import_attribute_size(&my elements) != 1) {
        if (!errs) return IMPORT_RESOLUTION_DEFAULT;
        if (my is_assert) {
            error_ctx_grammar_error_d(errs, my span, 1456,
                "Type import assertions should have exactly one key - `resolution-mode` - with value `import` or `require`.");
        } else {
            error_ctx_grammar_error_d(errs, my span, 1464,
                "Type import attributes should have exactly one key - 'resolution-mode' - with value 'import' or 'require'.");
        }
        return IMPORT_RESOLUTION_DEFAULT;
    }
    ImportAttribute const* element = my elements.data;
    if (*element->name.sv.buf != '"' && *element->name.sv.buf != '\'') return IMPORT_RESOLUTION_DEFAULT;
    csview name = csview_slice(element->name.sv, 1, element->name.sv.size - 1);
    if (!csview_equals(name, "resolution-mode")) {
        if (!errs) return IMPORT_RESOLUTION_DEFAULT;
        if (my is_assert) {
            error_ctx_grammar_error_d(errs, element->name, 1455, "`resolution-mode` is the only valid key for type import assertions.");
        } else {
            error_ctx_grammar_error_d(errs, element->name, 1463, "'resolution-mode' is the only valid key for type import attributes.");
        }
        return IMPORT_RESOLUTION_DEFAULT;
    }
    if (!is_string_literal(&element->value)) return IMPORT_RESOLUTION_DEFAULT;
    csview value = cstr_sv(&element->value.literal.string);
    if (csview_equals(value, "import")) return IMPORT_RESOLUTION_IMPORT;
    if (csview_equals(value, "require")) return IMPORT_RESOLUTION_REQUIRE;
    if (errs) {
        error_ctx_grammar_error_d(errs, expr_outer_span(&element->value), 1453, "`resolution-mode` should be either `require` or `import`.");
    }
    return IMPORT_RESOLUTION_DEFAULT;
}

// An export declaration (`export { a }`, `export * from "m"`), not an exported declaration or `export =`/`export import`
static bool port_statement_is_export_declaration(PortStatement const* me) {
    if (!csview_equals(my keyword_span.sv, "export")) return false;
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
    bool is_import_alias = my main.tag == PORT_IMPORT_ALIAS && csview_equals(my keyword_span.sv, "import");
    if (!my jsdoc && !top && !my in_namespace && (my module_ref || is_import_alias || port_statement_is_export_declaration(me))) {
        my wrong_context = true;
        bool is_export = csview_equals(my keyword_span.sv, "export");
        // (A JavaScript file has no namespaces to mention)
        if (my keyword_span.source && my keyword_span.source->is_js) {
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
    } else if (span_valid(my non_string_specifier)) {
        error_ctx_error_d(binder_errs(scope->binder), my non_string_specifier, 1141, "String literal expected.");
    }

    my main.keyword_span = my keyword_span;
    scope_add_port(scope, &my main);
    for_each (i, vec_port_item, &my items) {
        i->keyword_span = my keyword_span;
        scope_add_port(scope, i);
    }
}


void port_statement_lookup_names(PortStatement* me, Scope* scope, ErrorCtx* errs) {
    for_each (i, vec_expr, &my decorators) expr_lookup_names(i, scope, nullptr);
    if (my bad_specifier) expr_lookup_names(my bad_specifier, scope, nullptr);
    // (the values of `with { type: "json" }` are expressions like any other: checkImportAttribute checks them)
    if (my attributes) {
        for_each (i, vec_import_attribute, &my attributes->elements) expr_lookup_names(&i->value, scope, errs);
    }
    if (my wrong_context) return;
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
        // (nor about what a side effect import resolved to: tsc resolves it only for noUncheckedSideEffectImports)
        module_ref_report_resolved(my module_ref, my main.type_only, errs);
    }
}

// The tslib helpers of CommonJS-like emit (tsc's checkImportDeclaration/checkExportDeclaration/checkImportBinding)
static void port_statement_check_emit_helpers(PortStatement const* me, Span span, ErrorCtx* errs) {
    if (!my module_ref || !ty_emits_commonjs_like(span.source)) return;
    bool interop = compiler_options()->es_module_interop;
    switch (my main.tag) {
        case PORT_IMPORT_GATHER:
            if (!my main.is_require && interop) ty_check_emit_helpers(current_checker, span, EMIT_HELPER_IMPORT_STAR, errs);
            break;
        case PORT_REEXPORT_GATHER:
            if (interop) ty_check_emit_helpers(current_checker, span, EMIT_HELPER_IMPORT_STAR, errs);
            break;
        case PORT_REEXPORT_SPLAT: {
            // (a module with `export =` is TS2498 instead)
            Symbol* module = binder_module_of_ref(current_checker->binder, my module_ref);
            if (!module || !symbol_table_get(module->exports, c_sv("export="))) {
                ty_check_emit_helpers(current_checker, span, EMIT_HELPER_EXPORT_STAR, errs);
            }
        }   break;
        default:
            break;
    }
    if (!interop) return;
    for_each (i, vec_port_item, &my items) {
        if (i->tag != PORT_IMPORT_SPECIFIC && i->tag != PORT_REEXPORT_SPECIFIC) continue;
        if (csview_equals(port_item_name(i->source_name), "default")) {
            Span at = i->full_span; // (which may end in whitespace)
            while (at.sv.size && (at.sv.buf[at.sv.size - 1] == ' ' || at.sv.buf[at.sv.size - 1] == '\n' ||
                at.sv.buf[at.sv.size - 1] == '\t' || at.sv.buf[at.sv.size - 1] == '\r')) at.sv.size--;
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
    if (!item->symbol || !item->symbol->alias || item->jsdoc || item->value_only || item->type_only || my main.type_only) return;
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
            if (decl->full.source != at.source) continue;
            error_add_related_d(error, decl->full, 18044, "'", target->name, "' is automatically exported here.");
            break;
        }
    } else {
        csview name = port_item_name(at);
        csview specifier = my module_ref ? cstr_sv(&my target_path) : c_sv("...");
        error_ctx_error_d(errs, at, 18042, "'", name, "' is a type and cannot be imported in JavaScript files. Use 'import(\"",
            specifier, "\").", name, "' in a JSDoc type annotation.");
    }
}

static void report_string_export_name(Span name, ErrorCtx* errs) {
    if (!span_valid(name) || !name.sv.size || (name.sv.buf[0] != '"' && name.sv.buf[0] != '\'')) return;
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
    SourceFile const* from = my keyword_span.source;
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
            csview name = i->name.sv;
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
            error_ctx_grammar_error_d(errs, span_make(i->full_span.source, i->full_span.sv.buf, i->full_span.sv.buf + 4),
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
    PortItem const* alias_type_only = my main.tag == PORT_IMPORT_ALIAS && !my main.type_only && !my jsdoc && my main.symbol &&
        !vec_span_is_empty(&my main.alias_path) && symbol_resolve_alias(my main.symbol) ?
        symbol_type_only_declaration(my main.symbol) : nullptr;
    if (alias_type_only) {
        bool exported = port_item_is_export(alias_type_only);
        Span first = *vec_span_front(&my main.alias_path), last = *vec_span_back(&my main.alias_path);
        Error* error = error_ctx_error_d(errs, span_make(first.source, span_start_ptr(first), span_end_ptr(last)),
            exported ? 1379 : 1380, "An import alias cannot reference a declaration that was ",
            exported ? "exported using 'export type'." : "imported using 'import type'.");
        port_item_add_type_only_related(error, alias_type_only, port_item_name(alias_type_only->decl_name));
    }
    port_statement_check_json_import(me, errs);
    // (tsc's checkImportEqualsDeclaration)
    if (compiler_options()->erasable_syntax_only && !ambient && !my jsdoc && (my main.tag == PORT_IMPORT_ALIAS || my is_import_require)) {
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
    if (my in_ambient_module && my module_ref && !my jsdoc && module_name_is_relative(cstr_sv(&my target_path))) {
        error_ctx_error_d(errs, span_with_semicolon(span), 2439, "Import or export declaration in an ambient module "
            "declaration cannot reference module through relative module name.");
    } else if (my attributes && my module_ref && !my jsdoc && !my in_namespace && !my is_import_require) {
        // (a string literal: not a template, which isStringLiteral isn't)
        for_each (i, vec_import_attribute, &my attributes->elements) {
            Expr const* value = &i->value;
            if (is_string_literal(value) && expr_outer_span(value).sv.size && expr_outer_span(value).sv.buf[0] != '`') continue;
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
    if (my is_import_require && !ambient && !my main.type_only &&
        module_kind >= MODULE_KIND_ES2015 && module_kind <= MODULE_KIND_ESNEXT
    ) {
        error_ctx_grammar_error_d(errs, span_with_semicolon(span), 1202,
            "Import assignment cannot be used when targeting ECMAScript modules. Consider using 'import * as ns from \"mod\"', "
            "'import {a} from \"mod\"', 'import d from \"mod\"', or another module format instead.");
    }
    // tsc's checkImportEqualsDeclaration: `import x = A.B` whose target is a value, where the first name as a value or
    // namespace is something other than a namespace (the lookup of the alias found a namespace further out)
    PortItem const* alias = &my main;
    if (alias->tag == PORT_IMPORT_ALIAS && alias->symbol && alias->symbol->alias && vec_span_size(&alias->alias_path)) {
        Symbol const* target = symbol_resolve_alias(alias->symbol);
        if (target && !target->is_placeholder && symbol_resolved_flags(target) & SYMBOL_VALUE) {
            Span first = *vec_span_at(&alias->alias_path, 0);
            Symbol const* found = scope_resolve(alias->symbol->alias->scope, first.sv, SYMBOL_VALUE | SYMBOL_NAMESPACE);
            if (found && !(symbol_resolved_flags(found) & SYMBOL_NAMESPACE)) {
                error_ctx_error_d(errs, first, 2437, "Module '", first, "' is hidden by a local declaration with the same name.");
            }
        }
        if (target && !target->is_placeholder && symbol_resolved_flags(target) & SYMBOL_TYPE &&
            type_name_is_reserved(alias->decl_name.sv)
        ) {
            error_ctx_error_d(errs, alias->decl_name, 2438, "Import name cannot be '", alias->decl_name, "'.");
        }
        // tsc's markImportEqualsAliasReferenced: `export import x = A.B` of a value (not a const enum) or of nothing
        // references A as a value (markIdentifierAliasReferenced -> getResolvedSymbol), a namespace only is TS2708
        u32 target_flags = target ? symbol_resolved_flags(target) : 0;
        bool unknown = !target || target->is_placeholder || target->is_unresolved;
        bool value = target_flags & SYMBOL_VALUE && (target_flags & SYMBOL_VALUE & ~SYMBOL_CONST_ENUM);
        if (alias->export_import && !ambient && !alias->declared && (unknown || value)) {
            Span first = *vec_span_at(&alias->alias_path, 0);
            Scope const* scope = alias->symbol->alias->scope;
            if (!scope_resolve(scope, first.sv, SYMBOL_VALUE) && scope_resolve(scope, first.sv, SYMBOL_NAMESPACE)) {
                error_ctx_error_d(errs, first, 2708, "Cannot use namespace '", first, "' as a value.");
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
        error_ctx_grammar_error_d(errs, span_make(my keyword_span.source, start, end), 1363,
            "A type-only import can specify a default import or named bindings, but not both.");
    }
    port_item_check_export_equals_property(&my main, errs);
    for_each (i, vec_port_item, &my items) port_item_check_export_equals_property(i, errs);
    if (span.source && span.source->is_js && !my jsdoc) {
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
    if (type_only && override) return;
    CompilerOptions const* options = compiler_options();
    ModuleKind module = options->module;
    bool supported = module == MODULE_KIND_NODE18 || module == MODULE_KIND_NODE20 || module == MODULE_KIND_NODENEXT || module == MODULE_KIND_PRESERVE ||
        module == MODULE_KIND_ESNEXT;
    if (!supported) {
        if (node->is_assert) {
            error_ctx_grammar_error_d(errs, node->span, 2821,
                "Import assertions are only supported when the '--module' option is set to 'esnext', 'node18', 'node20', 'nodenext', or 'preserve'.");
        } else {
            error_ctx_grammar_error_d(errs, node->span, 2823,
                "Import attributes are only supported when the '--module' option is set to 'esnext', 'node18', 'node20', 'nodenext', or 'preserve'.");
        }
        return;
    }
    if (module == MODULE_KIND_NODENEXT && node->is_assert) {
        error_ctx_grammar_error_d(errs, node->keyword, 2880,
            "Import assertions have been replaced by import attributes. Use 'with' instead of 'assert'.");
        return;
    }
    bool emits_require = (module == MODULE_KIND_NODE18 || module == MODULE_KIND_NODE20 || module == MODULE_KIND_NODENEXT) &&
        !node->span.source->is_esm;
    if (emits_require) {
        if (node->is_assert) {
            error_ctx_grammar_error_d(errs, node->span, 2836,
                "Import assertions are not allowed on statements that compile to CommonJS 'require' calls.");
        } else {
            error_ctx_grammar_error_d(errs, node->span, 2856,
                "Import attributes are not allowed on statements that compile to CommonJS 'require' calls.");
        }
        return;
    }
    if (type_only) {
        if (node->is_assert) {
            error_ctx_grammar_error_d(errs, node->span, 2822, "Import assertions cannot be used with type-only imports or exports.");
        } else {
            error_ctx_grammar_error_d(errs, node->span, 2857, "Import attributes cannot be used with type-only imports or exports.");
        }
        return;
    }
    if (override) error_ctx_grammar_error_d(errs, node->span, 1454, "`resolution-mode` can only be set for type-only imports.");
}
