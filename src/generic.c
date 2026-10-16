#include "generic.h"
#include "ty/ty.h"
#include "scope.h"
#include "several_types.h"
#include "callable_type.h"
#include "interface.h"
#include "interface_member.h"
#include "expr.h"
#include "error.h"

void generic_item_drop(GenericItem* me) {
    type_drop(&my extends);
    if (my fallback) {
        type_drop(my fallback);
        ast_delete(my fallback);
    }
}

void generic_drop(Generic* me) {
    vec_generic_item_drop(&my items);
}

void generic_add_names(Generic const* me, Scope* scope) {
    for_each (i, vec_generic_item, &my items) {
        scope_init_declaration(scope, &i->owned_symbol, (DeclProps){
            .name = i->name,
            .full = i->full_span, // (tsc's declaration position starts at the modifiers)
            .kind = SYMBOL_DECL_TYPE_PARAMETER,
            .node = i,
        });
        i->symbol_ref = i->owned_symbol;
    }
}

void generic_lookup_names(Generic const* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_generic_item, &my items) {
        type_lookup_names(&i->extends, names, errs);
        TypeParamDefaultScope saved = identifier_type_default_scope;
        identifier_type_default_scope = (TypeParamDefaultScope){ me, i_index };
        if (i->fallback) type_lookup_names(i->fallback, names, errs);
        identifier_type_default_scope = saved;
    }
}

// tsc's checkTypeParameter: a default that reaches itself (TS2716), and the default against the constraint with the
// parameter replaced by the default (TS2344)
static void generic_check_default(GenericItem const* item, Ty const* param, ErrorCtx* errs) {
    Checker* checker = current_checker;
    if (!param) param = ty_type_param(checker, item->symbol_ref);
    if (param->kind != TY_TYPE_PARAM) return;
    Ty const* fallback = ty_param_default(checker, param);
    if (param->param.state->fallback_circular) {
        error_ctx_error_d(errs, item->fallback_span, 2716, "Type parameter '", item->name, "' has a circular default.");
    }
    Ty const* constraint = fallback ? ty_param_constraint(checker, param) : nullptr;
    if (!constraint || (fallback->flags & TYF_ERROR) || fallback == constraint) return;
    TyMapper const* mapper = ty_mapper_new(checker, (TyList){ .data = &param, .len = 1 }, (TyList){ .data = &fallback, .len = 1 });
    Ty const* target = ty_with_this_arg(checker, ty_instantiate(checker, constraint, mapper), fallback);
    VecTyReportLine lines = {0};
    if (ty_relate_explaining(checker, fallback, target, 2344, &lines, errs)) return;
    cstr source_str, target_str;
    ty_report_pair_cstr(checker, fallback, ty_report_target(checker, fallback, target), &source_str, &target_str);
    ty_report_explained_with_head(errs, &lines, 2344, cstr_d(
        "Type '", source_str, "' does not satisfy the constraint '", target_str, "'."), item->fallback_span);
    c_drop(cstr, &source_str, &target_str);
}

void generic_typecheck(Generic const* me, ErrorCtx* errs) {
    bool found_optional = false;
    for_each (i, vec_generic_item, &my items) {
        type_typecheck(&i->extends, errs);
        if (i->fallback) type_typecheck(i->fallback, errs);
        // tsc's checkTypeParameter: hasNonCircularBaseConstraint
        bool circularity = i->extends_explicit && span_valid(i->extends_span);
        Ty const* param = i->symbol_ref && circularity ? ty_type_param(current_checker, i->symbol_ref) : nullptr;
        if (circularity && param && ty_param_constraint_is_circular(current_checker, param)) {
            error_ctx_error_d(errs, i->extends_span, 2313, "Type parameter '", i->name, "' has a circular constraint.");
        }
        if (i->fallback_explicit && span_valid(i->fallback_span) && i->symbol_ref) generic_check_default(i, param, errs);
        if (found_optional && !i->fallback_explicit) {
            error_ctx_error_d(
                errs, i->full_span, 2706,
                "Required type parameters may not follow optional type parameters."
            );
        }
        if (i->fallback_explicit) {
            found_optional = true;
        }
    }
}
