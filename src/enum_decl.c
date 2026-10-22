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
    ast_cstr_drop(&my canonical_name);
    if (my computed_key) {
        expr_drop(my computed_key);
        ast_delete(my computed_key);
    }
}

void enum_decl_drop(EnumDecl* me) {
    vec_enum_case_drop(&my cases);
}

void enum_decl_add_names(EnumDecl* me, Scope* scope, DeclStatementProps props) {
    my ambient = props.declared || scope->ambient || (span_source(my name) && span_source(my name)->is_declaration);
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = props.exported == DECL_EXPORTED_YES,
        .default_export = props.exported == DECL_EXPORTED_YES_DEFAULT,
        .declare_keyword = span_valid(props.declare_keyword),
        .is_ambient = props.declared,
        .kind = SYMBOL_DECL_ENUM,
        .node = me,
        .is_const_enum = my is_const,
    });

    // The members are the exports of the enum symbol, shared with the other declarations of the enum
    my namespace = scope_new(scope, SCOPE_ENUM);
    my namespace->container = my symbol;
    my namespace->ambient = my ambient;

    for_each (i, vec_enum_case, &my cases) {
        i->parent = me;
        scope_init_declaration(my namespace, &i->symbol, (DeclProps) {
            .name = i->name,
            .symbol_name = cstr_size(&i->canonical_name) ? cstr_sv(&i->canonical_name) : (csview){0},
            .exported = true,
            .full = i->full_name,
            .kind = SYMBOL_DECL_ENUM_MEMBER,
            .node = i,
        });
    }
}

void enum_decl_lookup_names(EnumDecl* me, Scope* names, ErrorCtx* errs) {
    // Initializers see the members by their bare names
    my namespace->flow_ = scope_flow(names); // The scope was made when the enum was declared
    my namespace->self = my symbol;
    for_each (i, vec_enum_case, &my cases) {
        // (tsgo's checkEnumMember checks a computed name's expression too)
        if (i->computed_key) expr_lookup_names(i->computed_key, my namespace, errs);
        expr_lookup_names(&i->value, my namespace, errs);
    }
    my namespace->flow_ = nullptr;
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

// Whether evaluating an enum initializer reads a declaration of another file (tsc's evaluator: resolvedOtherFiles)
static bool references_other_file(Expr const* expr, SourceFile const* file) {
    switch (expr->tag) {
        case EXPR_IDENTIFIER: {
            // (The evaluator knows Infinity and NaN itself)
            if (span_equals(expr->inner_span, "Infinity") || span_equals(expr->inner_span, "NaN")) return false;
            Symbol const* target = expr->identifier.target;
            return target && target->decl_count && span_source(target->decls[0].full) && span_source(target->decls[0].full) != file;
        }
        case EXPR_PROPERTY: return references_other_file(&expr->property->expr, file);
        case EXPR_BINARY_OP:
            return references_other_file(&expr->binary_op->left, file) || references_other_file(&expr->binary_op->right, file);
        case EXPR_UNARY_OP: return references_other_file(&expr->unary_op->expr, file);
        default: return false;
    }
}

void enum_decl_typecheck(EnumDecl* me, ErrorCtx* errs) {
    if (compiler_options()->erasable_syntax_only && !my ambient) { // (tsc's checkEnumDeclarationWorker)
        error_ctx_error_d(errs, my name, 1294, "This syntax is not allowed when 'erasableSyntaxOnly' is enabled.");
    }
    bool previous_number = true; // (The first member's automatic value is 0)
    bool previous_other_file = false;
    for_each (i, vec_enum_case, &my cases) {
        if (!i->name_is_value && i->name.size && i->name.buf[0] == '#') { // tsc's checkEnumMember
            error_ctx_error_d(errs, i->name, 18024, "An enum member cannot be named with a private identifier.");
        }
        // (tsc's computeEnumMemberValue: a member without initializer after one whose value isn't a number. Members
        // with an initializer set previous_number once their type is known, below)
        bool auto_number = previous_number;
        previous_number = false;
        if (i->value.tag == EXPR_IMPLICIT_ANY && (!my ambient || my is_const)) {
            if (!auto_number) {
                error_ctx_error_d(errs, i->full_name, 1061, "Enum member must have initializer.");
            } else if (previous_other_file && compiler_options()->isolated_modules) {
                // (A number that took another file to compute: tsc's resolvedOtherFiles)
                error_ctx_error_d(errs, i->full_name, 18056, "Enum member following a non-literal numeric member must have "
                    "an initializer when 'isolatedModules' is enabled.");
            }
            previous_number = auto_number;
        }
        previous_other_file = i->value.tag != EXPR_IMPLICIT_ANY && references_other_file(&i->value, span_source(my name));
        if (i->computed_key) expr_typecheck(i->computed_key, errs);
        current_checker->enum_member_initializer = &i->value;
        expr_typecheck(&i->value, errs);
        current_checker->enum_member_initializer = nullptr;
        // (tsc's computeEnumMemberValue)
        if (i->computed_name) {
            error_ctx_error_d(errs, i->full_name, 1164, "Computed property names are not allowed in enums.");
        }
        // (tsc's computeEnumMemberValue: the name's text is what a number prints as, and not Infinity or NaN)
        bool numeric = i->computed_name ? false : i->numeric_name ? i->numeric_name == 1 : value_is_numeric_literal_name(span_sv(i->name)) &&
            !csview_equals(span_sv(i->name), "Infinity") && !csview_equals(span_sv(i->name), "-Infinity") && !csview_equals(span_sv(i->name), "NaN");
        if (numeric) {
            error_ctx_error_d(errs, i->full_name, 2452, "An enum member cannot have a numeric name.");
        }
        if (i->value.tag == EXPR_IMPLICIT_ANY || !i->symbol) continue;
        ty_check_enum_initializer_references(current_checker, i, &i->value, errs);
        // (Evaluated, not typed: the enum's literal types are made when something asks for them, which decides their
        // order in unions)
        double value = 0;
        int constant = ty_enum_member_constant(current_checker, i, &value);
        previous_number = constant == 1;
        Span at = expr_outer_span(&i->value);
        if (constant == 1) {
            if (my is_const && isnan(value)) {
                error_ctx_error_d(errs, at, 2478, "'const' enum member initializer was evaluated to disallowed value 'NaN'.");
            } else if (my is_const && !isfinite(value)) {
                error_ctx_error_d(errs, at, 2477, "'const' enum member initializer was evaluated to a non-finite value.");
            }
            continue;
        }
        if (constant == 2) {
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
