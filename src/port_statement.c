#include "port_statement.h"
#include "error.h"
#include "program.h"
#include "source_file.h"
#include "scope.h"
#include "ty/ty.h"

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
        error_ctx_grammar_error_d(errs, element->value.outer_span, 1453, "`resolution-mode` should be either `require` or `import`.");
    }
    return IMPORT_RESOLUTION_DEFAULT;
}

void port_statement_add_names(PortStatement* me, Scope* scope) {
    if (my module_ref && !(scope->kind == SCOPE_MODULE || (scope->container && scope->container->is_module))) {
        error_ctx_error_d(
            scope->errs,
            my keyword_span,
            1232,
            "An import declaration can only be used at the top level of a namespace or module."
        );
        return;
    }

    scope_add_port(scope, &my main);
    for_each (i, vec_port_item, &my items) {
        scope_add_port(scope, i);
    }
}


void port_statement_lookup_names(PortStatement* me, Scope* scope, ErrorCtx* errs) {
    // Ambient modules of every file are known by now. tsc doesn't mind unresolved side effect imports (`import "./a"`)
    bool unchecked = my side_effect_only && !compiler_options()->no_unchecked_side_effect_imports;
    if (my module_ref && !unchecked && !my module_ref->file &&
        !binder_module_of_ref(scope->binder, my module_ref)
    ) {
        // (an implicitly `any` module is no problem for an import that imports nothing)
        if (!(my side_effect_only && my module_ref->failure == MODULE_REF_IS_JS)) {
            module_ref_report_unresolved(my module_ref, errs);
        }
    } else if (my module_ref && my module_ref->file) {
        module_ref_report_resolved(my module_ref, errs);
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

void port_statement_typecheck(PortStatement const* me, Span span, bool ambient, ErrorCtx* errs) {
    if (!ambient) port_statement_check_emit_helpers(me, span, errs);
    // tsc's checkImportAttributes()
    ImportAttributes const* node = my attributes;
    if (!node) return;
    // TODO: the attributes' type against the global `ImportAttributes` (TS2322 for a non-string value)
    bool type_only = my main.type_only; // tsc's isExclusivelyTypeOnlyImportOrExport
    ImportResolutionOverride override = import_attributes_resolution_mode(node, type_only ? errs : nullptr);
    if (type_only && override) return;
    CompilerOptions const* options = compiler_options();
    ModuleKind module = options->module;
    bool supported = module == MODULE_KIND_NODE18 || module == MODULE_KIND_NODENEXT || module == MODULE_KIND_PRESERVE ||
        module == MODULE_KIND_ESNEXT;
    if (!supported) {
        if (node->is_assert) {
            error_ctx_grammar_error_d(errs, node->span, 2821,
                "Import assertions are only supported when the '--module' option is set to 'esnext', 'node18', 'nodenext', or 'preserve'.");
        } else {
            error_ctx_grammar_error_d(errs, node->span, 2823,
                "Import attributes are only supported when the '--module' option is set to 'esnext', 'node18', 'nodenext', or 'preserve'.");
        }
        return;
    }
    if (module == MODULE_KIND_NODENEXT && node->is_assert) {
        error_ctx_grammar_error_d(errs, node->keyword, 2880,
            "Import assertions have been replaced by import attributes. Use 'with' instead of 'assert'.");
        return;
    }
    bool emits_require = (module == MODULE_KIND_NODE18 || module == MODULE_KIND_NODENEXT) &&
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
