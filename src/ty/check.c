#include "ty/ty_internal.h"
#include "scope.h"
#include "display.h"
#include "error.h"
#include "source_file.h"
#include "program.h"
#include "class_decl.h"
#include "port_statement.h"
#include "unused.h"

// Checks the statement/expression walk asks of the new core, and how their errors are worded.

static bool report_instantiation_limit(Checker* me, Span span, ErrorCtx* errs) {
    if (!my instantiation_limit_hit) return false;
    my instantiation_limit_hit = false;
    error_ctx_error_d(errs, span, 2589, "Type instantiation is excessively deep and possibly infinite.");
    return true;
}

// The lines of an explanation from `first` on as the message chain under `error`, then its related information
static void attach_explanation(Error* error, VecTyReportLine const* lines, isize_t first) {
    if (!error) return;
    Error* chain = error;
    for (isize_t i = first; i < vec_ty_report_line_size(lines); i++) {
        TyReportLine const* line = vec_ty_report_line_at(lines, i);
        if (line->related) continue;
        error_sub_error_s(chain, (Span){0}, TS_CODE_NONE, cstr_clone(line->message));
        chain = vec_error_back_mut(&chain->sub_errors);
    }
    for_each (line, vec_ty_report_line, lines) {
        if (line->related) error_add_related_s(error, line->span, line->ts_code, cstr_clone(line->message));
    }
}

// How the head of an assignability error reads: "<source_label> 'S' is not assignable to <target_label> 'T'."
typedef struct AssignHead {
    char const* source_label;
    char const* target_label;
    unsigned ts_code;
} AssignHead;

static AssignHead const HEAD_TYPE = { "Type", "type", 2322 };
static AssignHead const HEAD_ARGUMENT = { "Argument of type", "parameter of type", 2345 };
static AssignHead const HEAD_THIS = { "The 'this' context of type", "method's 'this' of type", 2684 };
static AssignHead const HEAD_COMPUTED = { nullptr, nullptr, 2418 }; // "Type of computed property's value is..."
static AssignHead const HEAD_SATISFIES = { nullptr, nullptr, 1360 };

static Error* report_unassignable_with_head(
    Checker* me, Ty const* source, Ty const* target, unsigned ts_code, cstr head, bool keep_natural_head, Span span,
    ErrorCtx* errs
);

// Reports that source isn't assignable to target (it isn't), explained. Returns the error, to add related information
// to right away
static Error* report_unassignable(
    Checker* me, AssignHead head, Ty const* source, Ty const* target, Span span, ErrorCtx* errs
) {
    // The head line is rendered like the elaboration under it: a literal source shows widened unless the target has
    // room for literals
    cstr source_str = ty_report_source_cstr(me, source, target);
    cstr target_str = ty_to_cstr(me, target);
    if (head.ts_code == HEAD_SATISFIES.ts_code) {
        Error* error = report_unassignable_with_head(me, source, target, 1360, cstr_d(
            "Type '", source_str, "' does not satisfy the expected type '", target_str, "'."
        ), false, span, errs);
        c_drop(cstr, &source_str, &target_str);
        return error;
    }

    // Why not, outermost line first
    VecTyReportLine lines = {0};
    ty_explain_unrelated(me, source, target, TY_RELATION_ASSIGNABLE, &lines);
    my instantiation_limit_hit = false;

    isize_t first_nested = 0;
    Error* error;
    bool default_head = head.ts_code == 2322;
    TyReportLine const* natural_head = vec_ty_report_line_size(&lines) ? vec_ty_report_line_at(&lines, 0) : nullptr;
    if (natural_head && natural_head->related) natural_head = nullptr;
    if (natural_head && span_valid(natural_head->span)) {
        // An error that knows exactly where it is (an excess property) replaces the head, whatever it was
        error = error_ctx_error_s(errs, natural_head->span, natural_head->ts_code, cstr_clone(natural_head->message));
        first_nested = 1;
    } else if (default_head && natural_head && natural_head->ts_code != 2322) {
        // Something more specific than "not assignable" (a missing property, say) takes the place of the head
        error = error_ctx_error_s(errs, span, natural_head->ts_code, cstr_clone(natural_head->message));
        first_nested = 1;
    } else {
        if (head.ts_code == HEAD_COMPUTED.ts_code) {
            error = error_ctx_error_d(errs, span, head.ts_code, "Type of computed property's value is '", source_str,
                "', which is not assignable to type '", target_str, "'.");
        } else {
            error = error_ctx_error_d(errs, span, head.ts_code, head.source_label, " '", source_str,
                "' is not assignable to ", head.target_label, " '", target_str, "'.");
        }
        if (natural_head && natural_head->ts_code == 2322) first_nested = 1;
    }
    attach_explanation(error, &lines, first_nested);
    vec_ty_report_line_drop(&lines);
    c_drop(cstr, &source_str, &target_str);
    return error;
}

// Elaboration (tsc's elaborateError): a failed assignment of an object or array literal, or of an arrow function with
// an expression body, is reported at the innermost property, element or returned expression that doesn't fit

static bool elaborate_error(
    Checker* me, AssignHead head, Expr const* expr, Ty const* source, Ty const* target, ErrorCtx* errs
);

static bool is_or_has_generic_conditional(Ty const* ty) {
    if (ty->kind == TY_CONDITIONAL) return true;
    if (ty->kind != TY_INTERSECTION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (is_or_has_generic_conditional(ty->composite.types.data[i])) return true;
    }
    return false;
}

// tsc's isGenericObjectType, roughly: what an indexed access stays deferred on
static bool is_generic_object(Checker* me, Ty const* ty) {
    if (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) return true;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_generic_object(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    return (ty->flags & TYF_OBJECT) && ty_members(me, ty)->mapped;
}

// Where a type is declared (tsc's target.symbol.declarations[0], as an error span), invalid if nowhere
static Span type_decl_span(Checker* me, Ty const* ty) {
    if (ty->kind == TY_REFERENCE) ty = ty->reference.target;
    if (ty->kind == TY_DECLARED) {
        Symbol const* symbol = ty->declared.symbol;
        return symbol && symbol->decl_count ? symbol->decls[0].name : (Span){0};
    }
    while (ty->kind == TY_ANONYMOUS && ty->anonymous.target) ty = ty->anonymous.target;
    if (ty->kind != TY_ANONYMOUS) return (Span){0};
    if (ty->anonymous.node && ty->anonymous.node->tag == TYPE_CALLABLE) return ty->anonymous.node->callable->function->full_span;
    if (ty->anonymous.node && ty->anonymous.node->tag == TYPE_INTERFACE) return ty->anonymous.node->interface.span;
    FunctionImpl const* function = ty->anonymous.function;
    if (function) return span_valid(function->name) ? function->name : function->full_span;
    (void)me;
    return (Span){0};
}

static bool in_lib(Span span) { return !span_valid(span) || !span.source || span.source->is_stdlib; }

// The property a target has for a name (tsc's getPropertyOfType): of a union, only if every member has it
static Prop const* target_prop_of(Checker* me, Ty const* target, Atom const* name) {
    if (target->kind != TY_UNION && target->kind != TY_INTERSECTION) return ty_get_prop(me, ty_apparent_type(me, target), name);
    Prop const* first = nullptr;
    for (u32 i = 0; i < target->composite.types.len; i++) {
        Prop const* prop = target_prop_of(me, target->composite.types.data[i], name);
        if (!prop && target->kind == TY_UNION) return nullptr;
        if (!first) first = prop;
    }
    return first;
}

// tsc's getBestMatchIndexedAccessTypeOrUndefined
static Ty const* best_match_indexed_access(Checker* me, Ty const* source, Ty const* target, Ty const* name_type) {
    if (is_generic_object(me, target)) return nullptr;
    Ty const* ret = ty_access(me, target, name_type);
    if (ret || target->kind != TY_UNION) return ret;
    Ty const* best = ty_best_matching_type(me, source, target);
    return best && !is_generic_object(me, best) ? ty_access(me, best, name_type) : nullptr;
}

// A property or element of a literal to elaborate on (tsc's ElaborationIterator item)
typedef struct ElaborationItem {
    Span error_span; // the property name, the element
    Expr const* inner; // the value, null for methods, accessors and shorthands
    Ty const* name_type;
    Atom const* name;
    bool computed_non_literal; // `[k]: v`: TS2418's message
} ElaborationItem;

// tsc's elaborateElementwise for one item. Returns whether it reported
static bool elaborate_element(
    Checker* me, ElaborationItem const* item, Ty const* source, Ty const* target, ErrorCtx* errs
) {
    Ty const* target_prop = best_match_indexed_access(me, source, target, item->name_type);
    if (!target_prop || target_prop->kind == TY_INDEXED) return false;
    Ty const* source_prop = ty_access(me, source, item->name_type);
    if (!source_prop) return false;
    if (ty_is_related(me, source_prop, target_prop, TY_RELATION_ASSIGNABLE)) return false;
    if (item->inner && elaborate_error(me, HEAD_TYPE, item->inner, source_prop, target_prop, errs)) return true;
    // The expression's own type, if it's the one that doesn't fit
    Ty const* specific = item->inner ? ty_mutable_location_type(me, item->inner, source_prop) : source_prop;
    if (specific != source_prop && ((specific->flags & TYF_ANY) ||
        ty_is_related(me, specific, target_prop, TY_RELATION_ASSIGNABLE))
    ) {
        specific = source_prop;
    }
    AssignHead head = item->computed_non_literal ? HEAD_COMPUTED : HEAD_TYPE;
    Error* error = report_unassignable(me, head, specific, target_prop, item->error_span, errs);
    if (!error) return true;
    // Where the expected type comes from
    Prop const* declared = item->name ? target_prop_of(me, target, item->name) : nullptr;
    if (!declared && !is_generic_object(me, target)) {
        IndexInfo const* info = ty_applicable_index_info(me, ty_apparent_type(me, target), item->name_type);
        if (info && !in_lib(info->decl_span)) {
            error_add_related_d(error, info->decl_span, 6501, "The expected type comes from this index signature.");
            return true;
        }
    }
    Span decl = declared ? ty_prop_decl_span(declared) : (Span){0};
    if (!span_valid(decl)) decl = type_decl_span(me, target);
    if (in_lib(decl)) return true;
    cstr name = item->name && !(item->name_type->flags & TYF_UNIQUE_ES_SYMBOL) ?
        cstr_from_sv(atom_sv(item->name)) : ty_to_cstr(me, item->name_type);
    cstr target_str = ty_to_cstr(me, target);
    error_add_related_d(error, decl, 6500, "The expected type comes from property '", name,
        "' which is declared here on type '", target_str, "'");
    c_drop(cstr, &name, &target_str);
    return true;
}

static bool elaborate_object_literal(Checker* me, Expr const* expr, Ty const* source, Ty const* target, ErrorCtx* errs) {
    if (!(target->flags & (TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_UNKNOWN |
        TYF_ANY | TYF_NON_PRIMITIVE))) {
        return false; // primitives and never
    }
    bool reported = false;
    for_each (i, vec_object_expr_item, &expr->object) {
        if (i->tag == OBJECT_ITEM_SPREAD) continue;
        ElaborationItem item = { .error_span = i->name_span, .inner = i->tag == OBJECT_ITEM_PROPERTY ? &i->value : nullptr };
        if (i->computed_key) {
            bool is_number = false;
            item.name = ty_property_name_of_key(me, ty_expr_type(me, i->computed_key), &is_number);
            Expr const* key = i->computed_key;
            item.computed_non_literal = !(key->tag == EXPR_LITERAL &&
                (key->literal.tag == VALUE_STRING || key->literal.tag == VALUE_NUMBER));
        } else {
            item.name = checker_atom(me, cstr_sv(&i->name_string));
        }
        if (!item.name) continue;
        item.name_type = ty_prop_name_literal(me, item.name);
        reported |= elaborate_element(me, &item, source, target, errs);
    }
    return reported;
}

// tsc's isTupleLikeType
static bool is_tuple_like(Checker* me, Ty const* ty) {
    return ty_is_tuple(ty) || target_prop_of(me, ty, checker_atom(me, c_sv("0")));
}

static bool elaborate_array_literal(Checker* me, Expr const* expr, Ty const* source, Ty const* target, ErrorCtx* errs) {
    if (!(target->flags & (TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_UNKNOWN |
        TYF_ANY | TYF_NON_PRIMITIVE))) {
        return false;
    }
    if (!is_tuple_like(me, source)) {
        // Re-read as a tuple (tsc's forceTuple)
        source = ty_array_literal_as_tuple(me, expr, target);
        if (!source || !is_tuple_like(me, source)) return false;
    }
    bool target_tuple_like = is_tuple_like(me, target);
    bool reported = false;
    for_each (i, vec_expr, &expr->array) {
        // Elements the target doesn't have: a length error on the whole is likely better
        char digits[16];
        snprintf(digits, sizeof(digits), "%u", (unsigned)i_index);
        Atom const* name = checker_atom(me, csview_from(digits));
        if (target_tuple_like && !target_prop_of(me, target, name) && !(ty_is_tuple(target) && i_index <
            (isize_t)target->reference.target->declared.tuple->arity)) {
            continue;
        }
        if (!i->outer_span.sv.size) continue; // omitted
        Expr const* check_node = expr_effective_check_node(i);
        Value index = value_number((double)i_index);
        ElaborationItem item = {
            .error_span = check_node->inner_span, .inner = check_node, .name_type = ty_literal(me, &index), .name = name,
        };
        reported |= elaborate_element(me, &item, source, target, errs);
    }
    return reported;
}

static bool elaborate_arrow_function(Checker* me, Expr const* expr, Ty const* source, Ty const* target, ErrorCtx* errs) {
    FunctionImpl const* function = expr->function;
    if (!span_valid(function->arrow_span) || !function->expression_body) return false;
    for_each (i, vec_function_arg, &function->args) {
        if (i->initializer.binding.has_explicit_type) return false;
    }
    if (!(source->flags & TYF_OBJECT)) return false;
    Members const* source_members = ty_members(me, source);
    if (source_members->call_count != 1 || source_members->construct_count || source_members->prop_count ||
        source_members->index_count
    ) {
        return false;
    }
    Signature* const* target_sigs;
    u32 target_count = 0;
    if (target->kind == TY_UNION) {
        target_sigs = ty_union_signatures(me, target->composite.types, false, &target_count);
    } else {
        Members const* target_members = ty_members(me, ty_apparent_type(me, target));
        target_sigs = target_members->call;
        target_count = target_members->call_count;
    }
    if (!target_count) return false;
    Ty const* source_ret = signature_return_type(me, source_members->call[0]);
    TyBuf rets = {0};
    for (u32 i = 0; i < target_count; i++) ty_buf_push(&rets, signature_return_type(me, target_sigs[i]));
    Ty const* target_ret = ty_union(me, rets.data, rets.len, nullptr, (TyList){0});
    ty_buf_drop(&rets);
    if (ty_is_related(me, source_ret, target_ret, TY_RELATION_ASSIGNABLE)) return false;
    Expr const* body = &vec_statement_at(&function->body.statements, 0)->expr;
    if (elaborate_error(me, HEAD_TYPE, body, source_ret, target_ret, errs)) return true;
    Error* error = report_unassignable(me, HEAD_TYPE, source_ret, target_ret, body->outer_span, errs);
    if (!error) return true;
    if (target->kind != TY_UNION) {
        Span decl = type_decl_span(me, target);
        if (!in_lib(decl)) {
            error_add_related_d(error, decl, 6502, "The expected type comes from the return type of this signature.");
        }
    }
    // Would it fit as an async function?
    Symbol const* promise_symbol = function->is_async ? nullptr :
        program_get_global(my program, c_sv("Promise"), SYMBOL_TYPE);
    Ty const* promise = promise_symbol ? ty_declared_type_of(me, promise_symbol) : nullptr;
    if (promise && promise->kind == TY_DECLARED && promise->declared.type_params.len == 1 &&
        !ty_access(me, source_ret, ty_prop_name_literal(me, checker_atom(me, c_sv("then")))) &&
        ty_is_related(me, ty_reference(me, promise, (TyList){ .data = &source_ret, .len = 1 }), target_ret,
            TY_RELATION_ASSIGNABLE)
    ) {
        error_add_related_d(error, expr->inner_span, 1356, "Did you mean to mark this function as 'async'?");
    }
    return true;
}

// A function (or class) whose call (construction) would fit: the whole error, plus a hint
static bool elaborate_did_you_mean_to_call(
    Checker* me, AssignHead head, Expr const* expr, Ty const* source, Ty const* target, ErrorCtx* errs
) {
    if (!(source->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, ty_apparent_type(me, source));
    for (u32 pass = 0; pass < 2; pass++) {
        bool construct = pass == 0;
        Signature* const* sigs = construct ? members->construct : members->call;
        u32 count = construct ? members->construct_count : members->call_count;
        bool fits = false;
        for (u32 i = 0; i < count && !fits; i++) {
            Ty const* ret = signature_return_type(me, sigs[i]);
            fits = !(ret->flags & (TYF_ANY | TYF_NEVER)) && ty_is_related(me, ret, target, TY_RELATION_ASSIGNABLE);
        }
        if (!fits) continue;
        Error* error = report_unassignable(me, head, source, target, expr->outer_span, errs);
        error_add_related_s(error, expr->outer_span, construct ? 6213 : 6212, construct ?
            cstr_lit("Did you mean to use 'new' with this expression?") : cstr_lit("Did you mean to call this expression?"));
        return true;
    }
    return false;
}

static bool elaborate_error(
    Checker* me, AssignHead head, Expr const* expr, Ty const* source, Ty const* target, ErrorCtx* errs
) {
    if (!expr || is_or_has_generic_conditional(target)) return false;
    if (elaborate_did_you_mean_to_call(me, head, expr, source, target, errs)) return true;
    switch (expr->tag) {
        case EXPR_AS_TYPE:
            if (expr->as_type->angle_bracket_syntax || expr->as_type->is_satisfies || !as_type_is_const(expr->as_type)) {
                return false;
            }
            return elaborate_error(me, head, &expr->as_type->expr, source, target, errs);
        case EXPR_BINARY_OP:
            if (expr->binary_op->op != BINARY_OP_EQUAL && expr->binary_op->op != BINARY_OP_COMMA) return false;
            return elaborate_error(me, head, &expr->binary_op->right, source, target, errs);
        case EXPR_OBJECT: return elaborate_object_literal(me, expr, source, target, errs);
        case EXPR_ARRAY: return elaborate_array_literal(me, expr, source, target, errs);
        case EXPR_FUNCTION: return elaborate_arrow_function(me, expr, source, target, errs);
        default: return false;
    }
}

// tsc's checkTypeRelatedToAndOptionallyElaborate: `expr` (may be null) is what the source came from
static bool check_assignable(
    Checker* me, AssignHead head, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs
) {
    // HACK: `any` stands for whatever couldn't be typed (no initializer, failed inference...), and is let through even
    // to `never`
    if (source->flags & TYF_ANY) return true;
    bool related = ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE);
    if (report_instantiation_limit(me, span, errs)) return false;
    if (related) return true;
    if (errs && elaborate_error(me, head, expr, source, target, errs)) {
        my instantiation_limit_hit = false;
        return false;
    }
    report_unassignable(me, head, source, target, span, errs);
    return false;
}

// An error whose head says something of its own (`head`, taken), followed by why source isn't assignable to target.
// `keep_natural_head`: the elaboration starts with "Type 'S' is not assignable to type 'T'." (tsc's containing message
// chain) rather than having it replaced (tsc's head message)
static Error* report_unassignable_with_head(
    Checker* me, Ty const* source, Ty const* target, unsigned ts_code, cstr head, bool keep_natural_head, Span span,
    ErrorCtx* errs
) {
    VecTyReportLine lines = {0};
    ty_explain_unrelated(me, source, target, TY_RELATION_ASSIGNABLE, &lines);
    my instantiation_limit_hit = false;
    isize_t first = 0;
    TyReportLine const* natural = vec_ty_report_line_size(&lines) ? vec_ty_report_line_at(&lines, 0) : nullptr;
    Error* error;
    if (natural && span_valid(natural->span)) {
        // An error that knows exactly where it is (an excess property) replaces the head
        cstr_drop(&head);
        error = error_ctx_error_s(errs, natural->span, natural->ts_code, cstr_clone(natural->message));
        first = 1;
    } else {
        error = error_ctx_error_s(errs, span, ts_code, head);
    }
    if (natural && natural->ts_code == 2322 && !keep_natural_head) first = 1;
    attach_explanation(error, &lines, first);
    vec_ty_report_line_drop(&lines);
    return error;
}

static Ty const* heritage_prop_type(Checker* me, Prop const* prop) {
    Ty const* type = prop_type(me, prop);
    if ((prop->flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks) {
        Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
        type = ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    return type;
}

void ty_check_heritage(
    Checker* me, Ty const* derived, Ty const* base, HeritageMember const* members, u32 count, unsigned ts_code,
    char const* what, char const* relation, Span span, ErrorCtx* errs
) {
    if ((derived->flags | base->flags) & TYF_ANY) return;
    if (ty_is_related(me, derived, base, TY_RELATION_ASSIGNABLE)) return;
    if (report_instantiation_limit(me, span, errs)) return;
    cstr derived_str = ty_to_cstr(me, derived);
    cstr base_str = ty_to_cstr(me, base);
    // Each of the type's own members that doesn't fit the base's (tsc's issueMemberSpecificError), else the whole type
    bool issued = false;
    Ty const* derived_apparent = ty_apparent_type(me, derived);
    Ty const* base_apparent = ty_apparent_type(me, base);
    for (u32 i = 0; members && i < count; i++) {
        if (!members[i].name || !(derived_apparent->flags & TYF_OBJECT) || !(base_apparent->flags & TYF_OBJECT)) continue;
        Prop const* prop = ty_get_prop(me, derived_apparent, members[i].name);
        Prop const* base_prop = ty_get_prop(me, base_apparent, members[i].name);
        if (!prop || !base_prop) continue;
        Ty const* source = heritage_prop_type(me, prop);
        Ty const* target = heritage_prop_type(me, base_prop);
        if (ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE)) continue;
        cstr name = ty_prop_name_cstr(me, members[i].name);
        report_unassignable_with_head(me, source, target, 2416, cstr_d(
            "Property '", name, "' in type '", derived_str, "' is not assignable to the same property in base type '",
            base_str, "'."
        ), true, members[i].span, errs);
        cstr_drop(&name);
        issued = true;
    }
    if (!issued) {
        report_unassignable_with_head(me, derived, base, ts_code, cstr_d(what, " '", derived_str, "' ", relation, " '",
            base_str, "'."), false, span, errs);
    }
    c_drop(cstr, &derived_str, &base_str);
}

void ty_check_satisfies(Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs) {
    check_assignable(me, HEAD_SATISFIES, source, target, span, expr, errs);
}

void ty_check_assignable(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    check_assignable(me, HEAD_TYPE, source, target, span, nullptr, errs);
}

bool ty_check_assignable_expr(Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs) {
    return check_assignable(me, HEAD_TYPE, source, target, span, expr, errs);
}

// tsc's getGlobalDisposableType(/*reportErrors*/ true) & co: TS2318 once when the lib doesn't have it
static Ty const* global_disposable_type(Checker* me, char const* name, ErrorCtx* errs) {
    Symbol const* symbol = program_get_global(my program, csview_from(name), SYMBOL_TYPE);
    if (symbol && symbol->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) return ty_declared_type_of(me, symbol);
    error_ctx_global_error_d(errs, 2318, "Cannot find global type '", name, "'.");
    return nullptr;
}

void ty_check_using_initializer(Checker* me, Ty const* source, bool is_await, Span span, ErrorCtx* errs) {
    if (!my disposable_looked_up[is_await]) {
        my disposable_looked_up[is_await] = true;
        Ty const* async_disposable = is_await ? global_disposable_type(me, "AsyncDisposable", errs) : nullptr;
        Ty const* disposable = global_disposable_type(me, "Disposable", errs);
        if (disposable && (!is_await || async_disposable)) {
            Ty const* parts[4];
            u32 count = 0;
            if (is_await) parts[count++] = async_disposable;
            parts[count++] = disposable;
            parts[count++] = ty_intrinsic(me, TY_NULL);
            parts[count++] = ty_intrinsic(me, TY_UNDEFINED);
            my disposable_targets[is_await] = ty_union(me, parts, count, nullptr, (TyList){0});
        }
    }
    Ty const* target = my disposable_targets[is_await];
    if (!target || source->flags & TYF_ANY) return;
    if (ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE)) return;
    if (report_instantiation_limit(me, span, errs)) return;
    report_unassignable_with_head(me, source, target, is_await ? 2851 : 2850, is_await ?
        cstr_lit("The initializer of an 'await using' declaration must be either an object with a "
            "'[Symbol.asyncDispose]()' or '[Symbol.dispose]()' method, or be 'null' or 'undefined'.") :
        cstr_lit("The initializer of a 'using' declaration must be either an object with a '[Symbol.dispose]()' "
            "method, or be 'null' or 'undefined'."), false, span, errs);
}

void ty_report_unfit_argument(
    Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs
) {
    check_assignable(me, HEAD_ARGUMENT, source, target, span, expr, errs);
}

void ty_report_unfit_this(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    check_assignable(me, HEAD_THIS, source, target, span, nullptr, errs);
}

Ty const* ty_of_node(TypeNode const* node) {
    return ty_from_node(current_checker, node, (LowerCtx){ .generic = true });
}

// Property and element access

// A string literal index reads as a property name, anything else as its type
static cstr index_name(Checker* me, Ty const* index) {
    if (index->flags & TYF_UNIQUE_ES_SYMBOL) return ty_prop_name_cstr(me, index->literal.string);
    if ((index->flags & TYF_STRING_LITERAL) && !(index->flags & TYF_ENUM_LITERAL)) {
        return cstr_from_sv(atom_sv(index->literal.string));
    }
    return ty_to_cstr(me, index);
}

static void report_no_property(Checker* me, Ty const* object, Ty const* index, unsigned ts_code, Span span, ErrorCtx* errs) {
    cstr name = index_name(me, index);
    cstr object_str = ty_to_cstr(me, object);
    Error* error = error_ctx_error_d(errs, span, ts_code, "Property '", name, "' does not exist on type '", object_str, "'.");
    if (error && object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) {
        // Which member it is that lacks it
        for (u32 i = 0; i < object->composite.types.len; i++) {
            Ty const* member = object->composite.types.data[i];
            if ((member->flags & TYF_NULLABLE) || ty_access(me, member, index)) continue;
            cstr member_str = ty_to_cstr(me, member);
            cstr message = cstr_null;
            cstr_append_d(&message, DEFAULT_DISPLAY, "Property '", name, "' does not exist on type '", member_str, "'.");
            error_sub_error_s(error, (Span){0}, TS_CODE_NONE, message);
            cstr_drop(&member_str);
            break;
        }
    }
    c_drop(cstr, &name, &object_str);
}

static void check_access(
    Checker* me, Ty const* object, Ty const* index, Span object_span, Span index_span, bool object_is_named,
    bool error_if_not_found, u32 depth, ErrorCtx* errs
) {
    if (depth > 8) return;
    // `string & Tag` is a string as far as indexing goes (tsc's isTypeAssignableToKind)
    u32 key_like = TYF_ANY | TYF_STRING_LIKE | TYF_NUMBER | TYF_ES_SYMBOL_LIKE | TYF_NUMBER_LITERAL;
    if (index->kind == TY_UNION || (index->kind == TY_INTERSECTION && !(ty_key_flags(index) & key_like))) {
        for (u32 i = 0; i < index->composite.types.len; i++) {
            check_access(
                me, object, index->composite.types.data[i], object_span, index_span, object_is_named,
                error_if_not_found, depth + 1, errs
            );
        }
        return;
    }
    if (index->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) return; // TODO: TS2536 against `keyof` of the object
    if (!(ty_key_flags(index) & key_like)) {
        cstr index_str = ty_to_cstr(me, index);
        error_ctx_error_d(errs, index_span, 2538, "Type '", index_str, "' cannot be used as an index type.");
        cstr_drop(&index_str);
        return;
    }

    if (object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) {
        // Possibly null/undefined is said once about the whole object, a missing property about the whole union
        bool has_null = false, has_undefined = false, opaque = false;
        for (u32 i = 0; i < object->composite.types.len; i++) {
            Ty const* member = object->composite.types.data[i];
            has_null |= (member->flags & TYF_NULL) != 0;
            has_undefined |= (member->flags & TYF_UNDEFINED) != 0;
            opaque |= (member->flags & (TYF_ANY | TYF_TYPE_PARAM | TYF_DEFERRED)) != 0;
        }
        if ((has_null || has_undefined) && compiler_options()->strict_null_checks) {
            const char* what = has_null && has_undefined ? "null' or 'undefined" : has_null ? "null" : "undefined";
            if (object_is_named) {
                error_ctx_error_d(
                    errs, object_span, has_null && has_undefined ? 18049 : has_null ? 18047 : 18048,
                    "'", object_span, "' is possibly '", what, "'."
                );
            } else {
                error_ctx_error_d(
                    errs, object_span, has_null && has_undefined ? 2533 : has_null ? 2531 : 2532,
                    "Object is possibly '", what, "'."
                );
            }
            return;
        }
        if (opaque || !error_if_not_found) return;
        if (!ty_access(me, object, index)) report_no_property(me, object, index, 2339, index_span, errs);
        return;
    }
    // `this` is checked as its apparent type, the class with this-argument `this` (tsc's isThisTypeParameter case of
    // reportNonexistentProperty). TODO: other type parameters
    if (object->kind == TY_TYPE_PARAM && object->param.state->is_this) object = ty_apparent_type(me, object);
    if (object->flags & (TYF_ANY | TYF_TYPE_PARAM | TYF_DEFERRED)) return;
    if (object->flags & TYF_UNKNOWN) {
        if (object_is_named) {
            error_ctx_error_d(errs, object_span, 18046, "'", object_span, "' is of type 'unknown'.");
        } else {
            error_ctx_error_d(errs, object_span, 2571, "Object is of type 'unknown'.");
        }
        return;
    }
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
        if (!(index->flags & TYF_STRING_LITERAL)) return;
    } else if (ty_is_array(me, object) && !(index->flags & TYF_STRING_LITERAL)) {
        return;
    }

    if (!error_if_not_found) return;
    // `never` has no properties (tsc: only the silent never of inference passes is any-like)
    if (object->flags & TYF_NEVER) {
        report_no_property(me, object, index, 2339, index_span, errs);
        return;
    }
    // HACK: no expando members yet (`function f() {} f.x = 1`, and everything JS files declare by assignment:
    // `this.x = 1`, `module.exports.x`, prototype assignments), so callables and JS files are let through
    if (index_span.source && index_span.source->is_js) return;
    if (object->flags & TYF_OBJECT) {
        Members const* members = ty_members(me, object);
        if (members->call_count && !members->construct_count) return;
        // HACK: a mapped type over something generic (`Pick<T, K>`) only knows its members through its constraint
        if (members->mapped && (object->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return;
        if (ty_has_unresolved_base(me, object)) return;
    }
    if (object->flags & TYF_NULLABLE) return; // TODO: TS18050 (the value 'undefined' cannot be used here)
    if (ty_access(me, object, index)) return;
    // What `typeof globalThis` doesn't have is any (TS7017 under noImplicitAny, TODO)
    if (object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_NAMESPACE && object->declared.namespace_symbol &&
        object->declared.namespace_symbol->is_global_this
    ) {
        return;
    }
    report_no_property(me, object, index, 2339, index_span, errs);
}

void ty_check_property_access(
    Checker* me, Ty const* object, Ty const* index, Span object_span, Span index_span, bool object_is_named,
    bool error_if_not_found, ErrorCtx* errs
) {
    check_access(me, object, index, object_span, index_span, object_is_named, error_if_not_found, 0, errs);
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
    // Arena-allocated: instantiation can keep the mapper (deferred conditional and object types)
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = params.data, .len = len }, (TyList){ .data = lowered, .len = len });
    return ty_is_related(me, lowered[index], ty_instantiate(me, constraint, mapper), TY_RELATION_ASSIGNABLE);
}

// Member modifiers

typedef struct MemberRefs {
    Prop const* props[16];
    Ty const* objects[16]; // The apparent type each was found on
    u32 len;
} MemberRefs;

// The properties named `name` of `object` (of every member of a union or intersection that has one), and whether the
// property tsc makes of them is readonly (a union's if any of them is, an intersection's if all are). -1: none
static int member_refs(Checker* me, Ty const* object, Atom const* name, MemberRefs* out, u32 depth) {
    if (depth > 8) return -1;
    // Members first: the apparent type of an intersection with type parameters is a new type
    if (object->kind != TY_UNION && object->kind != TY_INTERSECTION) object = ty_apparent_type(me, object);
    if ((object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) || object->kind == TY_INTERSECTION) {
        bool is_union = object->kind == TY_UNION;
        int ret = -1;
        for (u32 i = 0; i < object->composite.types.len; i++) {
            Ty const* member = object->composite.types.data[i];
            if (member->flags & TYF_NULLABLE) continue;
            int readonly = member_refs(me, member, name, out, depth + 1);
            if (readonly < 0 && is_union) {
                // A member without it: through an index signature, else the union has no such property
                Ty const* apparent = ty_apparent_type(me, member);
                IndexInfo const* info = apparent->flags & (TYF_OBJECT | TYF_INTERSECTION) ?
                    ty_applicable_index_info(me, apparent, ty_intrinsic(me, TY_STRING)) : nullptr;
                if (!info) return -1;
                readonly = info->readonly;
            }
            if (readonly < 0) continue;
            ret = ret < 0 ? readonly : is_union ? (ret || readonly) : (ret && readonly);
        }
        return is_union && ret >= 0 && ty_union_non_public_conflict(me, object, name) ? -1 : ret;
    }
    if (!(object->flags & TYF_OBJECT)) return -1;
    if (ty_is_tuple(object)) {
        // Elements without resolving the tuple's members, which the element access didn't need
        csview sv = atom_sv(name);
        bool numeric = sv.size > 0;
        for (isize_t i = 0; i < sv.size; i++) numeric &= sv.buf[i] >= '0' && sv.buf[i] <= '9';
        if (numeric) {
            u32 index = (u32)atoi(sv.buf);
            TupleShape const* shape = object->reference.target->declared.tuple;
            // Past the fixed elements it's the number index signature
            return index < shape->arity && !(shape->flags[index] & TUPLE_ELEMENT_REST) ? shape->readonly : -1;
        }
    }
    Prop const* prop = ty_get_prop(me, object, name);
    if (!prop) return -1;
    if (out->len < c_arraylen(out->props)) {
        out->props[out->len] = prop;
        out->objects[out->len++] = object;
    }
    return (prop->flags & PROP_READONLY) != 0;
}

static Atom const* access_name(Checker* me, PropertyExpr const* access) {
    return ty_literal_name(me, ty_regular(me, ty_access_key_type(me, access)));
}

static bool is_keyword(Expr const* expr, KeywordIdentifier keyword) {
    return expr->tag == EXPR_IDENTIFIER && expr->identifier.keyword == keyword;
}

// `ns` of `import * as ns from "./a"`
static bool is_namespace_import(Expr const* expr) {
    if (expr->tag != EXPR_IDENTIFIER || !expr->identifier.symbol) return false;
    PortItem const* item = symbol_find_node(expr->identifier.symbol, SYMBOL_DECL_ALIAS);
    return item && item->tag == PORT_IMPORT_GATHER && !item->is_require;
}

// tsc's isAssignmentToReadonlyEntity for a property
static bool readonly_assignment(Checker* me, PropertyExpr const* access, Ty const* object, MemberRefs* refs) {
    if (!access->assignment_target) return false;
    Atom const* name = access_name(me, access);
    if (!name) return false;
    int readonly = member_refs(me, object, name, refs, 0);
    if (readonly == 0) return is_namespace_import(&access->expr);
    if (readonly < 0) return false;
    // A class's own readonly properties can be assigned through `this` in its constructor (not in functions in there)
    if (refs->len == 1 && is_keyword(&access->expr, KEYWORD_IDENTIFIER_THIS) && access->in_constructor_of &&
        !(refs->props[0]->flags & PROP_ACCESSOR) && refs->props[0]->parent == access->in_constructor_of
    ) {
        return false;
    }
    return true;
}

bool ty_is_readonly_assignment(Checker* me, PropertyExpr const* access, Ty const* object) {
    MemberRefs refs = {0};
    return readonly_assignment(me, access, object, &refs);
}

static ClassDecl const* class_decl_of(Symbol const* symbol) {
    return symbol ? symbol_find_node(symbol, SYMBOL_DECL_CLASS) : nullptr;
}

static bool is_within_class(ClassDecl const* enclosing, ClassDecl const* class_decl) {
    for (ClassDecl const* c = enclosing; c; c = c->outer_class) {
        if (c == class_decl) return true;
    }
    return false;
}

// tsc's hasBaseType on the containing type of a protected member access
static bool containing_has_base(Checker* me, Ty const* containing, Ty const* base) {
    if (containing->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < containing->composite.types.len; i++) {
            if (containing_has_base(me, containing->composite.types.data[i], base)) return true;
        }
        return false;
    }
    return ty_has_base_type(me, containing, base);
}

// tsc's getEnclosingClassFromThisParameter: the class or interface `this` is where a function says what it is
static Ty const* this_parameter_class(Checker* me, PropertyExpr const* access) {
    Ty const* this_type = access->this_parameter ? ty_type_of_symbol(me, access->this_parameter) :
        access->this_function ? ty_contextual_this_type(me, access->this_function) : nullptr;
    if (this_type && this_type->kind == TY_TYPE_PARAM) this_type = ty_param_constraint(me, this_type);
    if (!this_type) return nullptr;
    if (this_type->kind == TY_REFERENCE) this_type = this_type->reference.target;
    if (this_type->kind != TY_DECLARED) return nullptr;
    TyDeclaredKind kind = this_type->declared.kind;
    return kind == TY_DECLARED_CLASS_INSTANCE || kind == TY_DECLARED_INTERFACE ? this_type : nullptr;
}

// tsc's checkPropertyAccessibilityAtLocation for one (constituent) property, false if it reported
static bool check_accessibility(
    Checker* me, PropertyExpr const* site, Prop const* prop, Ty const* containing, bool is_super, ErrorCtx* errs
) {
    Span span = site->prop.outer_span;
    if (is_super) {
        if (compiler_options()->target < SCRIPT_TARGET_ES2015) {
            if (!(prop->flags & PROP_METHOD)) {
                error_ctx_error_d(errs, span, 2340,
                    "Only public and protected methods of the base class are accessible via the 'super' keyword.");
                return false;
            }
        }
        if (!(prop->flags & PROP_STATIC) && (prop->flags & PROP_FIELD)) {
            error_ctx_error_d(errs, span, 2855, "Class field '", atom_sv(prop->name),
                "' defined by the parent class is not accessible in the child class via super.");
            return false;
        }
    }
    u32 modifiers = prop->flags & (PROP_PRIVATE | PROP_PROTECTED);
    if ((prop->flags & PROP_ACCESSOR) && site->assignment_target) {
        modifiers = (prop->flags & PROP_SET_PRIVATE ? PROP_PRIVATE : 0) |
            (prop->flags & PROP_SET_PROTECTED ? PROP_PROTECTED : 0);
    }
    if (!modifiers || !prop->parent) return true;
    ClassDecl const* declaring = class_decl_of(prop->parent);
    if (!declaring) return true;
    Ty const* declaring_type = ty_declared_type_of(me, prop->parent);
    if (modifiers & PROP_PRIVATE) {
        if (is_within_class(site->enclosing_class, declaring)) return true;
        cstr class_str = ty_to_cstr(me, declaring_type);
        error_ctx_error_d(errs, span, 2341,
            "Property '", atom_sv(prop->name), "' is private and only accessible within class '", class_str, "'.");
        cstr_drop(&class_str);
        return false;
    }
    if (is_super) return true;
    // The first enclosing class that derives from the declaring class
    Ty const* enclosing_type = nullptr;
    for (ClassDecl const* c = site->enclosing_class; c && !enclosing_type; c = c->outer_class) {
        if (!c->symbol) continue;
        Ty const* type = ty_declared_type_of(me, c->symbol);
        if (ty_has_base_type(me, type, declaring_type)) enclosing_type = type;
    }
    bool enclosing_found = enclosing_type != nullptr;
    if (!enclosing_type) {
        enclosing_type = this_parameter_class(me, site);
        if (enclosing_type && !ty_has_base_type(me, enclosing_type, declaring_type)) enclosing_type = nullptr;
    }
    if (!enclosing_type || (prop->flags & PROP_STATIC && !enclosing_found)) {
        cstr class_str = ty_to_cstr(me, declaring_type);
        error_ctx_error_d(errs, span, 2445, "Property '", atom_sv(prop->name),
            "' is protected and only accessible within class '", class_str, "' and its subclasses.");
        cstr_drop(&class_str);
        return false;
    }
    if (prop->flags & PROP_STATIC) return true;
    if (containing->kind == TY_TYPE_PARAM) containing = ty_apparent_type(me, containing);
    if (!containing_has_base(me, containing, enclosing_type)) {
        cstr enclosing_str = ty_to_cstr(me, enclosing_type);
        cstr containing_str = ty_to_cstr(me, containing);
        error_ctx_error_d(errs, span, 2446, "Property '", atom_sv(prop->name),
            "' is protected and only accessible through an instance of class '", enclosing_str,
            "'. This is an instance of class '", containing_str, "'.");
        c_drop(cstr, &enclosing_str, &containing_str);
        return false;
    }
    return true;
}

static bool is_global_this(Ty const* ty) {
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_NAMESPACE && ty->declared.namespace_symbol &&
        ty->declared.namespace_symbol->is_global_this;
}

// tsc's markPropertyAsReferenced: a private member is read, unless the access only writes it or is its own body
// reading it through `this` (`this.m()` in m). `access` is null for a destructuring, which always reads
static void mark_member_referenced(Checker* me, Prop const* prop, PropertyExpr const* access) {
    while (prop->target) prop = prop->target;
    bool private_name = span_valid(prop->name_span) && prop->name_span.sv.buf[0] == '#';
    if (!(prop->flags & PROP_PRIVATE) && !private_name) return;
    if (access && access->assignment_target == FLOW_TARGET_DEFINITE && !(prop->flags & PROP_ACCESSOR)) return;
    Expr const* object = access ? &access->expr : nullptr;
    bool self_type = object && (is_keyword(object, KEYWORD_IDENTIFIER_THIS) ||
        (object->tag == EXPR_IDENTIFIER && prop->parent && object->identifier.target == prop->parent));
    FunctionImpl const* function = access ? access->containing_function : nullptr;
    char const* at = function ? span_start_ptr(function->member_name) : nullptr;
    for (u32 i = 0; self_type && at && i < prop->decl_span_count; i++) {
        if (at >= span_start_ptr(prop->decl_spans[i]) && at < span_end_ptr(prop->decl_spans[i])) return;
    }
    for (u32 i = 0; i < prop->decl_span_count; i++) ty_mark_member_referenced(me, prop->decl_spans[i]);
}

void ty_mark_destructured_member(Checker* me, Ty const* object, csview name) {
    if (!unused_enabled() || (object->flags & TYF_ANY)) return;
    MemberRefs refs = {0};
    if (member_refs(me, object, checker_atom(me, name), &refs, 0) < 0) return;
    for (u32 i = 0; i < refs.len; i++) mark_member_referenced(me, refs.props[i], nullptr);
}

void ty_check_member_modifiers(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs) {
    // `typeof globalThis` has no properties for block scoped variables (TS2339 there, see check_access())
    if ((object->flags & TYF_ANY) || is_global_this(object)) return;
    Atom const* name = access_name(me, access);
    MemberRefs refs = {0};
    if (!name && unused_enabled()) {
        // `this[k]` with `k: "a" | "b"` reads both
        Ty const* key = ty_regular(me, ty_access_key_type(me, access));
        for (u32 i = 0; key->kind == TY_UNION && i < key->composite.types.len; i++) {
            Atom const* member_name = ty_literal_name(me, key->composite.types.data[i]);
            MemberRefs member = {0};
            if (!member_name || member_refs(me, object, member_name, &member, 0) < 0) continue;
            for (u32 j = 0; j < member.len; j++) mark_member_referenced(me, member.props[j], access);
        }
    }
    if (!name || member_refs(me, object, name, &refs, 0) < 0) {
        // Assigning to a generic object doesn't go through its index signatures (TS2339 instead)
        bool generic = object->kind == TY_TYPE_PARAM && !object->param.state->is_this;
        if (!access->assignment_target || generic) return;
        // A readonly index signature (`readonly T[]`)
        Ty const* apparent = ty_apparent_type(me, object);
        IndexInfo const* info = apparent->flags & (TYF_OBJECT | TYF_INTERSECTION) ?
            ty_applicable_index_info(me, apparent, ty_regular(me, ty_access_key_type(me, access))) : nullptr;
        if (info && info->readonly) {
            cstr object_str = ty_to_cstr(me, access->form == PROPERTY_EXPR_DOT ? apparent : object);
            error_ctx_error_d(errs, span, 2542, "Index signature in type '", object_str, "' only permits reading.");
            cstr_drop(&object_str);
        }
        return;
    }
    if (unused_enabled()) {
        for (u32 i = 0; i < refs.len; i++) mark_member_referenced(me, refs.props[i], access);
    }
    // Element access is the way around accessibility
    if (access->form == PROPERTY_EXPR_DOT) {
        bool is_super = is_keyword(&access->expr, KEYWORD_IDENTIFIER_SUPER);
        for (u32 i = 0; i < refs.len; i++) {
            if (!check_accessibility(me, access, refs.props[i], refs.objects[i], is_super, errs)) break;
        }
    }
    refs.len = 0;
    if (readonly_assignment(me, access, object, &refs)) {
        error_ctx_error_d(errs, access->prop.outer_span, 2540,
            "Cannot assign to '", atom_sv(name), "' because it is a read-only property.");
    }
}

// tsc's checkNonNullType: unknown and (under strictNullChecks) null or undefined are errors. Null if it reported
static Ty const* check_non_null_type(Checker* me, Ty const* type, Expr const* expr, ErrorCtx* errs) {
    bool named = expr_is_entity_name(expr);
    Span span = expr->inner_span;
    if (type->flags & TYF_UNKNOWN) {
        if (named) {
            error_ctx_error_d(errs, span, 18046, "'", span, "' is of type 'unknown'.");
        } else {
            error_ctx_error_d(errs, span, 2571, "Object is of type 'unknown'.");
        }
        return nullptr;
    }
    if (!compiler_options()->strict_null_checks || (type->flags & TYF_ANY)) return type;
    TyList parts = type->kind == TY_UNION ? type->composite.types : (TyList){ .data = &type, .len = 1 };
    bool has_null = false, has_undefined = false;
    for (u32 i = 0; i < parts.len; i++) {
        has_null |= (parts.data[i]->flags & TYF_NULL) != 0;
        has_undefined |= (parts.data[i]->flags & TYF_UNDEFINED) != 0;
    }
    if (!has_null && !has_undefined) return type;
    char const* what = has_null && has_undefined ? "null' or 'undefined" : has_null ? "null" : "undefined";
    if (named) {
        error_ctx_error_d(errs, span, has_null && has_undefined ? 18049 : has_null ? 18047 : 18048,
            "'", span, "' is possibly '", what, "'.");
    } else {
        error_ctx_error_d(errs, span, has_null && has_undefined ? 2533 : has_null ? 2531 : 2532,
            "Object is possibly '", what, "'.");
    }
    Ty const* non_null = ty_non_nullable(me, type);
    return non_null->flags & (TYF_NULLABLE | TYF_NEVER) ? nullptr : non_null;
}

Ty const* ty_private_name_owner(Checker* me, Expr const* private_name, Atom const** name) {
    ClassDecl const* class_decl = private_name->private_name->private_class;
    if (!class_decl || !class_decl->symbol) return nullptr;
    csview text = private_name->inner_span.sv;
    if (name) *name = ty_private_name(me, class_decl->symbol, text);
    ClassItem* item = (ClassItem*)class_decl_find_private_name(class_decl, text);
    return class_item_is_static(item) ? ty_static_type_of(me, class_decl->symbol) : ty_declared_type_of(me, class_decl->symbol);
}

// tsc's checkInExpression
// TODO: TS2638 for a right operand that may be a primitive (`{}`, an intersection with `{}`: hasEmptyObjectIntersection)
void ty_check_in_expression(Checker* me, Expr const* left_expr, Expr const* right_expr, ErrorCtx* errs) {
    Ty const* right = ty_expr_type(me, right_expr);
    if (left_expr->tag == EXPR_PRIVATE_NAME) {
        PrivateNameExpr const* name = left_expr->private_name;
        if (name->private_class && unused_enabled()) {
            ClassItem const* item = class_decl_find_private_name(name->private_class, left_expr->inner_span.sv);
            if (item) ty_mark_member_referenced(me, item->span);
        }
        if (!name->enclosing_class) {
            error_ctx_grammar_error_d(errs, left_expr->inner_span, 18016, "Private identifiers are not allowed outside class bodies.");
        } else if (!name->private_class) {
            cstr right_str = ty_to_cstr(me, right);
            error_ctx_error_d(errs, left_expr->inner_span, 2339,
                "Property '", left_expr->inner_span, "' does not exist on type '", right_str, "'.");
            cstr_drop(&right_str);
        }
    } else {
        Ty const* left = check_non_null_type(me, ty_expr_type(me, left_expr), left_expr, errs);
        if (left) {
            Ty const* keys[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
            ty_check_assignable(me, left, ty_union(me, keys, 3, nullptr, (TyList){0}), left_expr->inner_span, errs);
        }
    }
    right = check_non_null_type(me, right, right_expr, errs);
    if (right) ty_check_assignable(me, right, ty_intrinsic(me, TY_NON_PRIMITIVE), right_expr->inner_span, errs);
}

// tsc's checkPropertyAccessExpressionOrQualifiedName for a PrivateIdentifier, and checkPrivateIdentifierPropertyAccess
bool ty_check_private_name_access(Checker* me, PropertyExpr const* access, Ty const* object, ErrorCtx* errs) {
    Span name = access->prop.inner_span;
    if (access->form != PROPERTY_EXPR_DOT || access->prop.tag != EXPR_LITERAL || !span_valid(name) || name.sv.buf[0] != '#') {
        return false;
    }
    ClassDecl const* lexical = access->private_class;
    ClassItem const* lexical_item = lexical ? class_decl_find_private_name(lexical, name.sv) : nullptr;
    if (access->assignment_target && lexical_item && lexical_item->tag == CLASS_ITEM_METHOD &&
        lexical_item->method.accessor == CLASS_ACCESSOR_NONE
    ) {
        error_ctx_error_d(errs, name, 2803, "Cannot assign to private method '", name,
            "'. Private methods are not writable.");
    }
    if (object->flags & TYF_ANY) {
        if (lexical) return true;
        if (!access->enclosing_class) {
            error_ctx_error_d(errs, name, 18016, "Private identifiers are not allowed outside class bodies.");
        } else {
            error_ctx_error_d(errs, name, 2339, "Property '", name, "' does not exist on type 'any'.");
        }
        return true;
    }
    Ty const* apparent = ty_apparent_type(me, object);
    if (!(apparent->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, apparent);
    if (lexical && lexical->symbol && ty_get_prop(me, apparent, ty_private_name(me, lexical->symbol, name.sv))) return false;
    // A private name of the same spelling on the type is another class's
    Prop const* other = nullptr;
    for (u32 i = 0; i < members->prop_count && !other; i++) {
        Prop const* prop = members->props[i];
        if (ty_private_name_class(me, prop->name) && csview_equals_sv(atom_sv(prop->name), name.sv)) other = prop;
    }
    if (!other) return false;
    ClassDecl const* other_class = class_decl_of(ty_private_name_class(me, other->name));
    if (lexical_item && other_class && is_within_class(lexical, other_class)) {
        cstr type_str = ty_to_cstr(me, object);
        Error* e = error_ctx_error_d(errs, name, 18014, "The property '", name, "' cannot be accessed on type '", type_str,
            "' within this class because it is shadowed by another private identifier with the same spelling.");
        cstr_drop(&type_str);
        Span lexical_name = class_item_private_name(lexical_item);
        error_add_related_d(e, lexical_name, 18017, "The shadowing declaration of '", name, "' is defined here");
        error_add_related_d(e, ty_prop_decl_span(other), 18018, "The declaration of '", name,
            "' that you probably intended to use is defined here");
        return true;
    }
    csview class_name = other_class && span_valid(other_class->name) ? other_class->name.sv : c_sv("(anonymous)");
    error_ctx_error_d(errs, name, 18013, "Property '", name, "' is not accessible outside class '", class_name,
        "' because it has a private identifier.");
    return true;
}

bool ty_constructor_accessible(Checker* me, CallExpr const* call, Signature const* sig, Span span, ErrorCtx* errs) {
    while (sig && !sig->function && sig->target) sig = sig->target;
    FunctionImpl const* function = sig ? sig->function : nullptr;
    if (!function || !function->constructed_class || function->constructor_access == ACCESS_MODIFIER_PUBLIC) {
        return true;
    }
    ClassDecl const* declaring = class_decl_of(function->constructed_class);
    if (!declaring || is_within_class(call->enclosing_class, declaring)) return true;
    Ty const* declaring_type = ty_declared_type_of(me, function->constructed_class);
    bool is_private = function->constructor_access == ACCESS_MODIFIER_PRIVATE;
    // A protected constructor is accessible from a derived class (tsc's typeHasProtectedAccessibleBase)
    if (!is_private && call->enclosing_class && call->enclosing_class->symbol &&
        ty_has_base_type(me, ty_declared_type_of(me, call->enclosing_class->symbol), declaring_type)
    ) {
        return true;
    }
    if (errs) {
        cstr class_str = ty_to_cstr(me, declaring_type);
        error_ctx_error_d(errs, span, is_private ? 2673 : 2674, "Constructor of class '", class_str, "' is ",
            is_private ? "private" : "protected", " and only accessible within the class declaration.");
        cstr_drop(&class_str);
    }
    return false;
}
