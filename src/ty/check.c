#include "ty/ty_internal.h"
#include "scope.h"
#include "display.h"
#include "error.h"

// Checks the statement/expression walk asks of the new core, and how their errors are worded.

static bool report_instantiation_limit(Checker* me, Span span, ErrorCtx* errs) {
    if (!my instantiation_limit_hit) return false;
    my instantiation_limit_hit = false;
    error_ctx_error_d(errs, span, 2589, "Type instantiation is excessively deep and possibly infinite.");
    return true;
}

static bool check_assignable(
    Checker* me, const char* source_label, Ty const* source, const char* target_label, Ty const* target,
    unsigned ts_code, Span span, ErrorCtx* errs
) {
    // HACK: `any` stands for whatever couldn't be typed (no initializer, failed inference...), and is let through even
    // to `never`
    if (source->flags & TYF_ANY) return true;
    bool related = ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE);
    if (report_instantiation_limit(me, span, errs)) return false;
    if (related) return true;

    // Why not, outermost line first
    VecTyReportLine lines = {0};
    ty_explain_unrelated(me, source, target, TY_RELATION_ASSIGNABLE, &lines);
    my instantiation_limit_hit = false;

    // The head line is rendered like the elaboration under it: a literal source shows widened unless the target has
    // room for literals
    cstr source_str = ty_report_source_cstr(me, source, target);
    cstr target_str = ty_to_cstr(me, target);

    isize_t first_nested = 0;
    Error* error;
    bool default_head = ts_code == 2322;
    TyReportLine const* natural_head = vec_ty_report_line_size(&lines) ? vec_ty_report_line_at(&lines, 0) : nullptr;
    if (default_head && natural_head && natural_head->ts_code != 2322) {
        // Something more specific than "not assignable" (a missing property, say) takes the place of the head
        error = error_ctx_error_s(errs, span, natural_head->ts_code, cstr_clone(natural_head->message));
        first_nested = 1;
    } else {
        error = error_ctx_error_d(
            errs, span, ts_code,
            source_label, " '", source_str, "' is not assignable to ", target_label, " '", target_str, "'."
        );
        if (natural_head && natural_head->ts_code == 2322) first_nested = 1;
    }
    for (isize_t i = first_nested; error && i < vec_ty_report_line_size(&lines); i++) {
        error_sub_error_s(error, (Span){0}, TS_CODE_NONE, cstr_clone(vec_ty_report_line_at(&lines, i)->message));
        error = vec_error_back_mut(&error->sub_errors);
    }
    vec_ty_report_line_drop(&lines);
    c_drop(cstr, &source_str, &target_str);
    return false;
}

void ty_check_assignable(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    check_assignable(me, "Type", source, "type", target, 2322, span, errs);
}

void ty_report_unfit_argument(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    check_assignable(me, "Argument of type", source, "parameter of type", target, 2345, span, errs);
}

bool ty_is_comparable_either_way(Checker* me, Ty const* a, Ty const* b) {
    return ty_is_related(me, a, b, TY_RELATION_COMPARABLE) || ty_is_related(me, b, a, TY_RELATION_COMPARABLE);
}

Ty const* ty_of_node(TypeNode const* node) {
    return ty_from_node(current_checker, node, (LowerCtx){ .generic = true });
}

// Property and element access

// A string literal index reads as a property name, anything else as its type
static cstr index_name(Checker* me, Ty const* index) {
    if ((index->flags & TYF_STRING_LITERAL) && !(index->flags & TYF_ENUM_LITERAL)) {
        return cstr_from_sv(atom_sv(index->literal.string));
    }
    return ty_to_cstr(me, index);
}

static void report_no_property(Checker* me, Ty const* object, Ty const* index, unsigned ts_code, Span span, ErrorCtx* errs) {
    cstr name = index_name(me, index);
    cstr object_str = ty_to_cstr(me, object);
    error_ctx_error_d(errs, span, ts_code, "Property '", name, "' does not exist on type '", object_str, "'.");
    c_drop(cstr, &name, &object_str);
}

static void check_access(
    Checker* me, Ty const* object, Ty const* index, Span object_span, Span index_span, bool error_if_not_found,
    u32 depth, ErrorCtx* errs
) {
    if (depth > 8) return;
    if (index->kind == TY_UNION || index->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < index->composite.types.len; i++) {
            check_access(me, object, index->composite.types.data[i], object_span, index_span, error_if_not_found, depth + 1, errs);
        }
        return;
    }
    if (index->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) return; // TODO: TS2536 against `keyof` of the object
    if (!(index->flags & (TYF_ANY | TYF_STRING | TYF_NUMBER | TYF_ES_SYMBOL | TYF_STRING_LITERAL | TYF_NUMBER_LITERAL))) {
        cstr index_str = ty_to_cstr(me, index);
        error_ctx_error_d(errs, index_span, 2538, "Type '", index_str, "' cannot be used as an index type.");
        cstr_drop(&index_str);
        return;
    }

    if (object->kind == TY_UNION || object->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < object->composite.types.len; i++) {
            check_access(me, object->composite.types.data[i], index, object_span, index_span, error_if_not_found, depth + 1, errs);
        }
        return;
    }
    if (object->flags & (TYF_ANY | TYF_TYPE_PARAM | TYF_DEFERRED)) return;
    bool is_empty_tuple = object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_TUPLE_TARGET;
    if (ty_is_tuple(object) || is_empty_tuple) {
        TupleShape const* shape = is_empty_tuple ? object->declared.tuple : object->reference.target->declared.tuple;
        bool has_rest = false;
        for (u32 i = 0; i < shape->arity; i++) has_rest |= (shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
        if ((index->flags & TYF_NUMBER_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) && !has_rest &&
            index->literal.value.number >= shape->arity
        ) {
            cstr object_str = ty_to_cstr(me, object);
            error_ctx_error_d(errs, index_span, 2493,
                "Tuple type '", object_str, "' of length '", (i64)shape->arity,
                "' has no element at index '", &index->literal.value, "'.");
            cstr_drop(&object_str);
        }
        return;
    }
    if (ty_is_array(me, object)) return;

    // HACK: a missing property is only an error under noUncheckedIndexedAccess, which is how the checker this was
    // ported from kept the noise of everything it couldn't type yet out of the way. Narrowing (plan 03) comes first
    // Namespaces are the exception: their members are exactly their exports
    bool is_namespace = object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_NAMESPACE &&
        object->declared.namespace_symbol && !symbol_members(object->declared.namespace_symbol)->is_module;
    if (!error_if_not_found || !(compiler_options()->no_unchecked_indexed_access || is_namespace)) return;
    if (object->flags & TYF_NULLABLE) {
        error_ctx_error_d(
            errs, object_span, 18047,
            "'", object_span, "' is possibly '", object->flags & TYF_NULL ? "null" : "undefined", "'."
        );
        return;
    }
    if (ty_access(me, object, index)) return;
    report_no_property(me, object, index, 2339, index_span, errs);
}

void ty_check_property_access(
    Checker* me, Ty const* object, Ty const* index, Span object_span, Span index_span, bool error_if_not_found,
    ErrorCtx* errs
) {
    check_access(me, object, index, object_span, index_span, error_if_not_found, 0, errs);
}

// Whether type argument `index` satisfies the constraint of its type parameter, given all the arguments
bool ty_type_argument_satisfies_constraint(Checker* me, TyList params, VecSpannedType const* args, u32 index) {
    Ty const* lowered[32];
    u32 len = 0;
    for (; len < params.len && len < c_arraylen(lowered) && (isize_t)len < vec_spanned_type_size(args); len++) {
        lowered[len] = ty_of_node(&vec_spanned_type_at(args, len)->type);
    }
    if (index >= len) return true;
    Ty const* constraint = ty_param_constraint(me, params.data[index]);
    if (!constraint) return true;
    // The constraint can mention the other type parameters, which stand for the arguments here
    TyMapper mapper = { .len = len, .sources = params.data, .targets = lowered };
    return ty_is_related(me, lowered[index], ty_instantiate(me, constraint, &mapper), TY_RELATION_ASSIGNABLE);
}
