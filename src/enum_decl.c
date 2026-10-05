#include "enum_decl.h"
#include <math.h>
#include "error.h"
#include "source_file.h"
#include "ty/ty.h"
#include "scope.h"
#include "several_types.h"
#include "interface_member.h"

void enum_case_drop(EnumCase* me) {
    expr_drop(&my value);
    if (my computed_key) {
        expr_drop(my computed_key);
        free(my computed_key);
    }
}

void enum_decl_drop(EnumDecl* me) {
    vec_enum_case_drop(&my cases);
}

void enum_decl_add_names(EnumDecl* me, Scope* scope, DeclExported exported, bool declared) {
    my ambient = declared || scope->ambient || (my name.source && my name.source->is_declaration);
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = exported == DECL_EXPORTED_YES,
        .default_export = exported == DECL_EXPORTED_YES_DEFAULT,
        .is_ambient = declared,
        .kind = SYMBOL_DECL_ENUM,
        .node = me,
        .is_const_enum = my is_const,
    });

    // The members are the exports of the enum symbol, shared with the other declarations of the enum
    my namespace = scope_new(scope, SCOPE_ENUM);
    my namespace->container = my symbol;

    for_each (i, vec_enum_case, &my cases) {
        i->parent = me;
        scope_init_declaration(my namespace, &i->symbol, (DeclProps) {
            .name = i->name,
            .exported = true,
            .full = i->full_name,
            .kind = SYMBOL_DECL_ENUM_MEMBER,
            .node = i,
        });
    }
}

void enum_decl_lookup_names(EnumDecl* me, Scope* names, ErrorCtx* errs) {
    // Initializers see the members by their bare names
    my namespace->flow = names->flow; // The scope was made when the enum was declared
    my namespace->self = my symbol;
    for_each (i, vec_enum_case, &my cases) {
        expr_lookup_names(&i->value, my namespace, errs);
    }
    my namespace->flow = nullptr;
}


// The rules for the declarations of a merged enum, checked once, by its first declaration (tsc's
// checkEnumDeclarationWorker)
static void enum_decl_check_declarations(EnumDecl const* me, ErrorCtx* errs) {
    Symbol const* symbol = my symbol;
    if (!symbol || symbol_find_node(symbol, SYMBOL_DECL_ENUM) != me) return;
    if (symbol->decl_count > 1) {
        for (u32 i = 0; i < symbol->decl_count; i++) {
            if (symbol->decls[i].kind != SYMBOL_DECL_ENUM) continue;
            EnumDecl const* decl = symbol->decls[i].node;
            if (decl->is_const != my is_const) {
                error_ctx_error_d(errs, decl->name, 2473, "Enum declarations must all be const or non-const.");
            }
        }
    }
    // Only one declaration can continue from 0, the others can't continue the numbering of another
    bool seen_missing_initializer = false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_ENUM) continue;
        EnumDecl const* decl = symbol->decls[i].node;
        if (vec_enum_case_is_empty(&decl->cases)) continue;
        EnumCase const* first = decl->cases.data;
        if (first->value.tag != EXPR_IMPLICIT_ANY) continue;
        if (seen_missing_initializer) {
            error_ctx_error_d(errs, first->full_name, 2432, "In an enum with multiple declarations, only one "
                "declaration can omit an initializer for its first enum element.");
        }
        seen_missing_initializer = true;
    }
}

void enum_decl_typecheck(EnumDecl* me, ErrorCtx* errs) {
    for_each (i, vec_enum_case, &my cases) {
        if (!i->name_is_value && i->name.sv.size && i->name.sv.buf[0] == '#') { // tsc's checkEnumMember
            error_ctx_error_d(errs, i->name, 18024, "An enum member cannot be named with a private identifier.");
        }
        current_checker->enum_member_initializer = &i->value;
        expr_typecheck(&i->value, errs);
        current_checker->enum_member_initializer = nullptr;
        // (tsc's computeEnumMemberValue)
        if (i->computed_name) {
            error_ctx_error_d(errs, i->full_name, 1164, "Computed property names are not allowed in enums.");
            continue;
        }
        // (tsc's computeEnumMemberValue: the name's text is what a number prints as, and not Infinity or NaN)
        bool numeric = i->numeric_name ? i->numeric_name == 1 : value_is_numeric_literal_name(i->name.sv) &&
            !csview_equals(i->name.sv, "Infinity") && !csview_equals(i->name.sv, "-Infinity") && !csview_equals(i->name.sv, "NaN");
        if (numeric) {
            error_ctx_error_d(errs, i->full_name, 2452, "An enum member cannot have a numeric name.");
        }
        if (i->value.tag == EXPR_IMPLICIT_ANY || !i->symbol) continue;
        ty_check_enum_initializer_references(current_checker, i, &i->value, errs);
        Ty const* member = ty_declared_type_of(current_checker, i->symbol);
        Span at = expr_outer_span(&i->value);
        if (member->flags & TYF_NUMBER_LITERAL) {
            double value = member->literal.value.number;
            if (my is_const && isnan(value)) {
                error_ctx_error_d(errs, at, 2478, "'const' enum member initializer was evaluated to disallowed value 'NaN'.");
            } else if (my is_const && !isfinite(value)) {
                error_ctx_error_d(errs, at, 2477, "'const' enum member initializer was evaluated to a non-finite value.");
            }
            continue;
        }
        if (member->flags & TYF_STRING_LITERAL) {
            // (tsc's computeConstantEnumMemberValue, getIsolatedModules)
            if ((compiler_options()->isolated_modules || compiler_options()->verbatim_module_syntax) &&
                !ty_enum_initializer_is_syntactic_string(current_checker, &i->value)
            ) {
                error_ctx_error_d(errs, at, 18055, "'", my name, ".", i->name, "' has a string type, but must have "
                    "syntactically recognizable string syntax when 'isolatedModules' is enabled.");
            }
            continue;
        }
        // A computed member: an enum literal type of neither
        if (my is_const) {
            error_ctx_error_d(errs, at, 2474, "const enum member initializers must be constant expressions.");
        } else if (my ambient) {
            error_ctx_error_d(errs, at, 1066, "In ambient enum declarations member initializer must be constant expression.");
        } else {
            ty_check_enum_member_value(current_checker, ty_expr_type(current_checker, &i->value), at, errs);
        }
    }
    enum_decl_check_declarations(me, errs);
}
