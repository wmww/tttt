#include "ty/ty_internal.h"
#include "scope.h"
#include "display.h"
#include "error.h"
#include "source_file.h"
#include "program.h"
#include "class_decl.h"
#include "port_statement.h"
#include "unused.h"
#include "interface_member.h"
#include "name_index.h"
#include "several_types.h"
#include "statement.h"
#include "lib_features.h"

// Checks the statement/expression walk asks of the new core, and how their errors are worded.

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

// Where an explanation says the diagnostic goes instead of `span` (TyReportLine.relocates)
static Span relocated_span(VecTyReportLine const* lines, Span span) {
    for_each (line, vec_ty_report_line, lines) {
        if (line->relocates) return line->span;
    }
    return span;
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
static AssignHead const HEAD_EXACT_PROP = { nullptr, nullptr, 2412 }; // "... with 'exactOptionalPropertyTypes: true'..."


static Error* report_explained_with_head(
    ErrorCtx* errs, VecTyReportLine* lines, unsigned ts_code, cstr head, bool keep_natural_head, Span span
);
static Error* report_unassignable_with_head(
    Checker* me, Ty const* source, Ty const* target, unsigned ts_code, cstr head, bool keep_natural_head, Span span,
    ErrorCtx* errs
);

// tsc's checkTypeRelatedTo with an error node: one run that explains as it goes, with no plain run before it (what it
// caches as failed on the way is then what tsc's run has; a plain run first would leave a pair the explaining run is
// still relating cached as failed). True if related, else `lines` (outermost first) say why. Without `errs`, nothing
// would be reported: a plain run
static bool relate_explaining(
    Checker* me, TyRelation relation, Ty const* source, Ty const* target, unsigned head_code, VecTyReportLine* lines,
    ErrorCtx* errs
) {
    if (!errs) return ty_is_related(me, source, target, relation);
    return ty_explain_unrelated_headed(me, source, target, relation, head_code, lines);
}

// Reports that source isn't assignable to target, explained by `lines` (taken: relate_explaining()'s). Returns the
// error, to add related information to right away
static Error* report_unassignable_explained(
    Checker* me, AssignHead head, Ty const* source, Ty const* target, VecTyReportLine* lines, Span span, ErrorCtx* errs
) {
    // The head line is rendered like the elaboration under it: a literal source shows widened unless the target has
    // room for literals
    cstr source_str, target_str;
    Ty const* head_target = ty_report_target(me, source, target);
    ty_report_pair_cstr(me, ty_report_source(me, source), head_target, &source_str, &target_str);
    if (head.ts_code == HEAD_SATISFIES.ts_code) {
        Error* error = report_explained_with_head(errs, lines, 1360, cstr_d(
            "Type '", source_str, "' does not satisfy the expected type '", target_str, "'."
        ), false, span);
        c_drop(cstr, &source_str, &target_str);
        return error;
    }
    span = relocated_span(lines, span);

    isize_t first_nested = 0;
    Error* error;
    bool default_head = head.ts_code == 2322;
    TyReportLine const* natural_head = vec_ty_report_line_size(lines) ? vec_ty_report_line_at(lines, 0) : nullptr;
    if (natural_head && natural_head->related) natural_head = nullptr;
    if (natural_head && (span_valid(natural_head->span) || natural_head->replaces_head || natural_head->missing_head)) {
        // An error that knows exactly where it is (an excess property) replaces the head, whatever it was
        error = error_ctx_error_s(errs, span_valid(natural_head->span) ? natural_head->span : span, natural_head->ts_code,
            cstr_clone(natural_head->message));
        first_nested = 1;
    } else if (default_head && natural_head && (natural_head->ts_code != 2322 || ty_jsx_skips_head(me, source, target))) {
        // Something more specific than "not assignable" (a missing property, say) takes the place of the head. (JSX
        // attributes against `IntrinsicAttributes & P` have no head of their own: what failed inside is it)
        error = error_ctx_error_s(errs, span, natural_head->ts_code, cstr_clone(natural_head->message));
        first_nested = 1;
    } else {
        // (The argument head says exactOptionalPropertyTypes whenever an optional property of the parameter's type is
        // one the argument has with an undefined, whatever failed: tsc's reportRelationError,
        // getExactOptionalUnassignableProperties)
        if (head.ts_code == HEAD_ARGUMENT.ts_code && ((natural_head && natural_head->ts_code == 2375) ||
            ty_has_exact_optional_unassignable(me, source, target))
        ) {
            error = error_ctx_error_d(errs, span, 2379, "Argument of type '", source_str,
                "' is not assignable to parameter of type '", target_str,
                "' with 'exactOptionalPropertyTypes: true'. Consider adding 'undefined' to the types of the target's properties.");
            if (natural_head && natural_head->ts_code == 2375) first_nested = 1;
        } else if (head.ts_code == HEAD_EXACT_PROP.ts_code) {
            error = error_ctx_error_d(errs, span, 2412, "Type '", source_str, "' is not assignable to type '", target_str,
                "' with 'exactOptionalPropertyTypes: true'. Consider adding 'undefined' to the type of the target.");
            if (natural_head && (natural_head->ts_code == 2322 || natural_head->ts_code == 2375 || natural_head->ts_code == 2719)) first_nested = 1;
        } else if (head.ts_code == HEAD_COMPUTED.ts_code) {
            error = error_ctx_error_d(errs, span, head.ts_code, "Type of computed property's value is '", source_str,
                "', which is not assignable to type '", target_str, "'.");
        } else {
            error = error_ctx_error_d(errs, span, head.ts_code, head.source_label, " '", source_str,
                "' is not assignable to ", head.target_label, " '", target_str, "'.");
        }
        // (2375 is 2322 under exactOptionalPropertyTypes, which a head message stands in for)
        if (natural_head && (natural_head->ts_code == 2322 || natural_head->ts_code == 2375 || natural_head->ts_code == 2719 ||
            natural_head->ts_code == 2820)) first_nested = 1; // (2820: the did-you-mean form, tsc's only without a head)
    }
    attach_explanation(error, lines, first_nested);
    vec_ty_report_line_drop(lines);
    c_drop(cstr, &source_str, &target_str);
    return error;
}

// Reports that source isn't assignable to target (it isn't: tsc's checkTypeRelatedToAndOptionallyElaborate, which
// relates without explaining first), explained. Returns the error, to add related information to right away
static Error* report_unassignable(
    Checker* me, AssignHead head, Ty const* source, Ty const* target, Span span, ErrorCtx* errs
) {
    VecTyReportLine lines = {0};
    unsigned head_code = head.ts_code == HEAD_SATISFIES.ts_code ? HEAD_SATISFIES.ts_code : 0;
    if (ty_explain_unrelated_headed(me, source, target, TY_RELATION_ASSIGNABLE, head_code, &lines)) {
        vec_ty_report_line_drop(&lines);
        return nullptr;
    }
    return report_unassignable_explained(me, head, source, target, &lines, span, errs);
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
bool ty_is_generic_object(Checker* me, Ty const* ty) {
    if (ty->kind == TY_SUBSTITUTION) {
        return ty_is_generic_object(me, ty->substitution.base) || ty_is_generic_object(me, ty->substitution.constraint);
    }
    if (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) return true;
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (ty_is_generic_object(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    // (Only an anonymous type can be mapped: a class or interface isn't resolved, `[Self] extends [never]` with Self
    // the class whose base is being resolved, tsc's isGenericMappedType reads objectFlags)
    return (ty->flags & TYF_OBJECT) &&
        ((ty->kind == TY_ANONYMOUS && ty_generic_mapped(me, ty)) || ty_is_generic_tuple(me, ty));
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
    if (ty->anonymous.node && ty->anonymous.node->tag == TYPE_CALLABLE) {
        // (A method signature is its name, a function type the whole type)
        FunctionImpl const* function = ty->anonymous.node->callable->function;
        if (function->jsdoc_callback && span_valid(function->full_span) && csview_starts_with(span_sv(function->full_span), "@callback")) {
            // (A `@callback`'s reparsed signature starts after its name: the next tag, past the comment's `*`s)
            char const* c = span_end_ptr(function->full_span);
            while (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n' || (*c == '*' && c[1] != '/')) c++;
            return span_make_in(function->full_span, c, c);
        }
        return span_valid(function_impl_member_name(function)) ? function_impl_member_name(function) : function->full_span;
    }
    if (ty->anonymous.node && ty->anonymous.node->tag == TYPE_INTERFACE) return ty->anonymous.node->interface.span;
    FunctionImpl const* function = ty->anonymous.function;
    if (function) {
        // (A method's is its name too: tsc's getErrorSpanForNode)
        if (span_valid(function->name)) return function->name;
        return span_valid(function_impl_member_name(function)) ? function_impl_member_name(function) : function->full_span;
    }
    (void)me;
    return (Span){0};
}

static bool in_lib(Span span) { return !span_valid(span) || !span_source(span) || span_source(span)->is_stdlib; }

// The property a target has for a name (tsc's getPropertyOfType): of a union, only if every member has it
static Prop const* target_prop_of(Checker* me, Ty const* target, Atom const* name) {
    target = ty_reduced(me, target); // (tsc's getPropertyOfType)
    if (target->flags & TYF_NEVER) return nullptr;
    if (target->kind != TY_UNION && target->kind != TY_INTERSECTION) return ty_get_prop(me, ty_apparent_type(me, target), name);
    // (tsc's union or intersection property has the declarations of all of them: the first one declared)
    Prop const* first = nullptr;
    Prop const* first_declared = nullptr;
    for (u32 i = 0; i < target->composite.types.len; i++) {
        Prop const* prop = target_prop_of(me, target->composite.types.data[i], name);
        // (A member with an index signature for the name still has it, partially: tsc's createUnionOrIntersectionProperty)
        if (!prop && target->kind == TY_UNION) {
            Ty const* member = ty_apparent_type(me, target->composite.types.data[i]);
            if (!(member->flags & (TYF_OBJECT | TYF_INTERSECTION)) ||
                !ty_applicable_index_info(me, member, ty_prop_name_literal(me, name))
            ) {
                return nullptr;
            }
        }
        if (!first) first = prop;
        if (prop && !first_declared && span_valid(ty_prop_decl_span(prop))) first_declared = prop;
    }
    return first_declared ? first_declared : first;
}

// tsc's getBestMatchIndexedAccessTypeOrUndefined
static Ty const* best_match_indexed_access(Checker* me, Ty const* source, Ty const* target, Ty const* name_type) {
    if (ty_is_generic_object(me, target)) return nullptr;
    // (ty_access() reads past a union's null and undefined, tsc's getIndexedAccessTypeOrUndefined finds nothing in them:
    // `["x"]` against `readonly T[] | T | undefined` goes to the best matching member)
    bool nullable_member = false;
    for (u32 i = 0; target->kind == TY_UNION && i < target->composite.types.len; i++) {
        nullable_member |= (target->composite.types.data[i]->flags & TYF_NULLABLE) != 0;
    }
    Ty const* ret = nullable_member ? nullptr : ty_access(me, target, name_type);
    if (ret || target->kind != TY_UNION) return ret;
    Ty const* best = ty_best_matching_type(me, source, target);
    return best && !ty_is_generic_object(me, best) ? ty_access(me, best, name_type) : nullptr;
}

static Ty const* access_or_unknown(Checker* me, Ty const* ty, Ty const* name_type) {
    Ty const* ret = ty_access(me, ty, name_type);
    return ret ? ret : ty_intrinsic(me, TY_UNKNOWN_);
}

// A property or element of a literal to elaborate on (tsc's ElaborationIterator item)
typedef struct ElaborationItem {
    Span error_span; // the property name, the element
    Expr const* inner; // the value, null for methods, accessors and shorthands
    Ty const* name_type;
    Atom const* name;
    bool computed_non_literal; // `[k]: v`: TS2418's message
    unsigned message_code; // With `message`: the head of the error instead of "Type 'S' is not assignable..."
    csview message;
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
    // (A literal is checked again with that type as its context, tsc's checkExpressionForMutableLocationWithContext
    // ualType: what a forceTuple re-read made a tuple stays one, `options: [{...}, {...}]`. The literal's own type
    // re-read so, not source_prop, which past a spread of a union of tuples is `E | undefined`)
    bool literal = item->inner && (item->inner->tag == EXPR_OBJECT || item->inner->tag == EXPR_ARRAY) &&
        !expr_is_perenthisized(item->inner);
    Ty const* specific = !item->inner ? source_prop : literal ? ty_literal_rechecked(me, item->inner, source_prop) :
        ty_mutable_location_type(me, item->inner, source_prop);
    if (specific != source_prop && ((specific->flags & TYF_ANY) ||
        ty_is_related(me, specific, target_prop, TY_RELATION_ASSIGNABLE))
    ) {
        specific = source_prop;
    }
    AssignHead head = item->computed_non_literal ? HEAD_COMPUTED : HEAD_TYPE;
    Error* error;
    if (!item->message_code && ty_exact_optional_mismatch(me, specific, target_prop)) {
        // (tsc's elaborateElementwise: the error is on the property, with no elaboration. tsgo collects it with the
        // others, TypeScript 6 logged it directly even for an overload candidate)
        cstr source_str = ty_to_cstr(me, specific), target_str = ty_to_cstr(me, target_prop);
        error = error_ctx_error_d(errs, item->error_span, 2412, "Type '", source_str, "' is not assignable to type '", target_str,
            "' with 'exactOptionalPropertyTypes: true'. Consider adding 'undefined' to the type of the target.");
        c_drop(cstr, &source_str, &target_str);
    } else {
        // (An optional target property's type is shown without the missing type: tsc's removeMissingType)
        if (compiler_options()->exact_optional_property_types && item->name) {
            FoundProp found = ty_find_prop(me, target, item->name);
            if (found.found && (found.flags & PROP_OPTIONAL)) target_prop = ty_remove_missing(me, target_prop, true);
        }
        error = item->message_code ?
            report_unassignable_with_head(me, specific, target_prop, item->message_code, cstr_from_sv(item->message), false,
                item->error_span, errs) :
            report_unassignable(me, head, specific, target_prop, item->error_span, errs);
    }
    if (!error) return true;
    // Where the expected type comes from
    Prop const* declared = item->name ? target_prop_of(me, target, item->name) : nullptr;
    if (!declared && !ty_is_generic_object(me, target)) {
        IndexInfo const* info = ty_applicable_index_info(me, ty_apparent_type(me, target), item->name_type);
        if (info && !in_lib(info->decl_span)) {
            error_add_related_d(error, info->decl_span, 6501, "The expected type comes from this index signature.");
            return true;
        }
    }
    // (tsc's getPropertyOfType also finds the members of the global Object type, declared in the lib)
    Ty const* global_object = !declared && item->name ? ty_global_object_type(me) : nullptr;
    if (global_object && ty_get_prop(me, global_object, item->name)) return true;
    Span decl = declared ? ty_prop_decl_span(declared) : (Span){0};
    // (tsc's targetProp.Declarations[0]: a namespace merged with a class is the namespace's name when it came first)
    Prop const* declared_root = declared;
    while (declared_root && declared_root->target && !declared_root->decl->symbol) declared_root = declared_root->target;
    Symbol const* declared_symbol = declared_root ? declared_root->decl->symbol : nullptr;
    if (declared_symbol && declared_symbol->decl_count > 1 && declared_symbol->decls[0].kind == SYMBOL_DECL_NAMESPACE &&
        span_valid(declared_symbol->decls[0].name)
    ) {
        decl = declared_symbol->decls[0].name;
    }
    if (!span_valid(decl)) decl = type_decl_span(me, target);
    if (in_lib(decl)) return true;
    // (A unique symbol name is typeToString's default, `unique symbol`)
    cstr name = cstr_null;
    if (item->name && !(item->name_type->flags & TYF_UNIQUE_ES_SYMBOL)) name = cstr_from_sv(atom_sv(item->name));
    else name = ty_to_cstr(me, item->name_type);
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
            // (Only a property assignment's: a method or accessor gets the plain message, tsc's
            // generateObjectLiteralElements)
            item.computed_non_literal = i->tag == OBJECT_ITEM_PROPERTY && !(key->tag == EXPR_LITERAL &&
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

// target as written decides which elements are elaborated (tsc's generateLimitedTupleElements: not one a tuple target
// has no property for, which is every one from a generic spread on), each element against the target as relations see it
// (normalized, tsc's isRelatedTo in elaborateElementwise)
static bool elaborate_array_literal(Checker* me, Expr const* expr, Ty const* source, Ty const* declared, ErrorCtx* errs) {
    Ty const* target = ty_normalized_generic_tuple(me, declared, true);
    if (!(target->flags & (TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_UNKNOWN |
        TYF_ANY | TYF_NON_PRIMITIVE))) {
        return false;
    }
    if (!is_tuple_like(me, source)) {
        // Re-read as a tuple (tsc's forceTuple)
        source = ty_array_literal_as_tuple(me, expr, target);
        if (!source || !is_tuple_like(me, source)) return false;
    }
    bool target_tuple_like = is_tuple_like(me, declared);
    // A tuple's properties are its elements before the first rest one (tsc's createTupleTargetType)
    u32 fixed = 0;
    if (ty_is_tuple(declared)) {
        TupleShape const* shape = declared->reference.target->declared.tuple;
        while (fixed < shape->arity && !(shape->flags[fixed] & TUPLE_ELEMENT_REST)) fixed++;
    }
    bool reported = false;
    for_each (i, vec_expr, &expr->array) {
        // Elements the target doesn't have: a length error on the whole is likely better
        Atom const* name = ty_index_atom(me, (u32)i_index);
        if (target_tuple_like && !target_prop_of(me, declared, name) && !(ty_is_tuple(declared) && i_index < (isize_t)fixed)) {
            continue;
        }
        if (!expr_outer_span(i).size) continue; // omitted
        Expr const* check_node = expr_effective_check_node(i);
        Value index = value_number((double)i_index);
        ElaborationItem item = {
            .error_span = expr_error_span(check_node), .inner = check_node, .name_type = ty_literal(me, &index), .name = name,
        };
        reported |= elaborate_element(me, &item, source, target, errs);
    }
    return reported;
}

static Ty const* filter_members(Checker* me, Ty const* ty, bool (*keep)(Checker*, Ty const*), bool want) {
    TyList members = ty->kind == TY_UNION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    TyBuf kept = {0};
    for (u32 i = 0; i < members.len; i++) {
        if (keep(me, members.data[i]) == want) ty_buf_push(&kept, members.data[i]);
    }
    Ty const* ret = kept.len ? ty_union(me, kept.data, kept.len, nullptr, (TyList){0}) : ty_intrinsic(me, TY_NEVER);
    ty_buf_drop(&kept);
    return ret;
}

// tsc's isArrayOrTupleLikeType
static bool is_array_or_tuple_like(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_NULLABLE) return false;
    Ty const* any_array = ty_array(me, ty_intrinsic(me, TY_ANY), true);
    return ty_is_related(me, ty, any_array, TY_RELATION_ASSIGNABLE) || is_tuple_like(me, ty);
}

// Assignable to Iterable<any> (tsc's elaborateJsxComponents), array or tuple like without an Iterable
static bool is_iterable_like(Checker* me, Ty const* ty) {
    Symbol const* symbol = program_get_global(my program, c_sv("Iterable"), SYMBOL_TYPE);
    Ty const* iterable = symbol ? ty_declared_type_of(me, symbol) : nullptr;
    if (!iterable || iterable->kind != TY_DECLARED || !iterable->declared.type_params.len) return is_array_or_tuple_like(me, ty);
    TyList params = iterable->declared.type_params;
    Ty const** args = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 i = 0; i < params.len; i++) args[i] = ty_intrinsic(me, TY_ANY);
    Ty const* any_iterable = ty_reference(me, iterable, (TyList){ .data = args, .len = params.len });
    return ty_is_related(me, ty, any_iterable, TY_RELATION_ASSIGNABLE);
}

// The children of `element` against what the children property of `target` wants (tsc's elaborateJsxComponents): one
// by one, or TS2745/TS2746 when there's one child for a list or several for one thing. Text is TS2747
static bool elaborate_jsx_children(Checker* me, JsxElement const* element, Ty const* source, Ty const* target, ErrorCtx* errs) {
    if (element->self_closing) return false;
    u32 child_count = 0;
    for_each (i, vec_jsx_child, &element->children) child_count += ty_jsx_child_is_semantic(i);
    if (!child_count) return false;
    Atom const* children_name = ty_jsx_children_prop_name(me, element);
    if (!children_name) children_name = checker_atom(me, c_sv("children"));
    Ty const* name_type = ty_prop_name_literal(me, children_name);
    Ty const* children_target = ty_access(me, target, name_type);
    if (!children_target) children_target = ty_intrinsic(me, TY_UNKNOWN_);
    Ty const* array_like = filter_members(me, children_target, is_iterable_like, true);
    Ty const* non_array_like = filter_members(me, children_target, is_iterable_like, false);
    bool array_like_never = (array_like->flags & TYF_NEVER) != 0;
    bool non_array_like_never = (non_array_like->flags & TYF_NEVER) != 0;
    cstr target_str = ty_to_cstr(me, children_target);
    csview tag = span_sv(element->tag_name.inner_span);
    cstr text_message = cstr_d("'", tag, "' components don't accept text as child elements. Text in JSX has the type 'string', "
        "but the expected type of '", atom_sv(children_name), "' is '", target_str, "'.");
    bool reported = false;
    if (child_count > 1 && !array_like_never) {
        // Each child against the list's elements (tsc's elaborateIterableOrArrayLikeTargetElementwise)
        Ty const* tuple_like = filter_members(me, array_like, is_array_or_tuple_like, true);
        Ty const* other = filter_members(me, array_like, is_array_or_tuple_like, false);
        Ty const* iterated = nullptr;
        if (!(other->flags & TYF_NEVER)) {
            TyIterationTypes const* types = ty_iteration_types_of_iterable(me, other, ITERATION_FOR_OF);
            iterated = types ? types->yield_type : nullptr;
        }
        u32 index = 0;
        for_each (child, vec_jsx_child, &element->children) {
            if (!ty_jsx_child_is_semantic(child)) continue;
            Value index_value = value_number((double)index++);
            Ty const* child_name = ty_literal(me, &index_value);
            Ty const* element_target = iterated;
            Ty const* indexed = !(tuple_like->flags & TYF_NEVER) ? ty_access(me, tuple_like, child_name) : nullptr;
            if (indexed && indexed->kind != TY_INDEXED) {
                Ty const* both[2] = { iterated, indexed };
                element_target = iterated ? ty_union(me, both, 2, nullptr, (TyList){0}) : indexed;
            }
            if (!element_target) continue;
            bool text = child->tag == JSX_CHILD_TEXT;
            Ty const* child_type = text ? ty_intrinsic(me, TY_STRING) : ty_expr_type(me, &child->expr);
            // (The child as the source's children have it, widened for a mutable location: tsc's elaborateElementwise
            // takes the source's element type)
            Ty const* source_children = text ? nullptr : ty_access(me, source, name_type);
            Ty const* source_child = source_children && ty_tuple_shape(source_children) ? ty_access(me, source_children, child_name) : nullptr;
            if (source_child) child_type = source_child;
            if (ty_is_related(me, child_type, element_target, TY_RELATION_ASSIGNABLE)) continue;
            reported = true;
            Expr const* inner = text ? nullptr : &child->expr;
            if (inner && elaborate_error(me, HEAD_TYPE, inner, child_type, element_target, errs)) continue;
            Ty const* specific = inner ? ty_mutable_location_type(me, inner, child_type) : child_type;
            if (specific != child_type && ((specific->flags & TYF_ANY) ||
                ty_is_related(me, specific, element_target, TY_RELATION_ASSIGNABLE))) {
                specific = child_type;
            }
            // (An optional element's type is shown without the missing type: tsc's removeMissingType)
            if (compiler_options()->exact_optional_property_types && !(tuple_like->flags & TYF_NEVER)) {
                FoundProp found = ty_find_prop(me, tuple_like, ty_index_atom(me, index - 1));
                if (found.found && (found.flags & PROP_OPTIONAL)) element_target = ty_remove_missing(me, element_target, true);
            }
            if (text) {
                report_unassignable_with_head(me, specific, element_target, 2747, cstr_clone(text_message), false, child->span, errs);
            } else {
                report_unassignable(me, HEAD_TYPE, specific, element_target, child->span, errs);
            }
        }
    } else if (child_count > 1) {
        if (!ty_is_related(me, access_or_unknown(me, source, name_type), children_target, TY_RELATION_ASSIGNABLE)) {
            reported = true;
            error_ctx_error_d(errs, element->tag_name.inner_span, 2746, "This JSX tag's '", atom_sv(children_name),
                "' prop expects a single child of type '", target_str, "', but multiple children were provided.");
        }
    } else if (!non_array_like_never) {
        JsxChild const* child = nullptr;
        for_each (i, vec_jsx_child, &element->children) {
            if (!child && ty_jsx_child_is_semantic(i)) child = i;
        }
        bool text = child->tag == JSX_CHILD_TEXT;
        ElaborationItem item = {
            .error_span = child->span, .inner = text ? nullptr : &child->expr, .name_type = name_type, .name = children_name,
            .message_code = text ? 2747 : 0, .message = cstr_sv(&text_message),
        };
        reported = elaborate_element(me, &item, source, target, errs);
    } else if (!ty_is_related(me, access_or_unknown(me, source, name_type), children_target, TY_RELATION_ASSIGNABLE)) {
        reported = true;
        error_ctx_error_d(errs, element->tag_name.inner_span, 2745, "This JSX tag's '", atom_sv(children_name),
            "' prop expects type '", target_str, "' which requires multiple children, but only a single child was provided.");
    }
    c_drop(cstr, &target_str, &text_message);
    return reported;
}

// tsc's elaborateJsxComponents: the attributes, like the properties of an object literal (hyphenated ones aren't checked
// against anything), then the children
static bool elaborate_jsx_attributes(Checker* me, Expr const* expr, Ty const* source, Ty const* target, ErrorCtx* errs) {
    bool reported = false;
    for_each (i, vec_jsx_attribute, &expr->jsx_attributes) {
        if (i->tag == JSX_ATTRIBUTE_SPREAD || csview_contains(span_sv(i->name), "-")) continue;
        bool has_value = i->tag != JSX_ATTRIBUTE_NO_VALUE && i->tag != JSX_ATTRIBUTE_EMPTY_EXPR;
        ElaborationItem item = { .error_span = i->name, .inner = has_value ? &i->value : nullptr };
        item.name = checker_atom(me, span_sv(i->name));
        item.name_type = ty_prop_name_literal(me, item.name);
        reported |= elaborate_element(me, &item, source, target, errs);
    }
    reported |= elaborate_jsx_children(me, jsx_attributes_element(expr), source, target, errs);
    return reported;
}

static bool elaborate_arrow_function(Checker* me, Expr const* expr, Ty const* source, Ty const* target, ErrorCtx* errs) {
    FunctionImpl const* function = expr->function;
    if (!function_impl_is_arrow(function) || !function->expression_body) return false;
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
    Expr const* body = &vec_statement_at(&function_impl_body(function)->statements, 0)->expr;
    if (elaborate_error(me, HEAD_TYPE, body, source_ret, target_ret, errs)) return true;
    Error* error = report_unassignable(me, HEAD_TYPE, source_ret, target_ret, expr_outer_span(body), errs);
    if (!error) return true;
    if (target->kind != TY_UNION) {
        // (Also in a lib file: tsc's elaborateArrowFunction doesn't check hasNoDefaultLib)
        Span decl = type_decl_span(me, target);
        error_add_related_d(error, decl, 6502, "The expected type comes from the return type of this signature.");
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

// A namespace import whose value would have fit: likely the wrong kind of import (tsc's checkTypeRelatedTo with
// links.originatingImport)
static void add_originating_import(Checker* me, Error* error, Symbol const* alias, Ty const* target) {
    Ty const* original = alias && (alias->flags & SYMBOL_ALIAS) ? ty_namespace_import_original(me, alias) : nullptr;
    if (original && ty_is_related(me, original, target, TY_RELATION_ASSIGNABLE)) {
        PortItem const* item = alias->alias->item;
        Span statement = span_valid(item->keyword_span) ?
            span_between(item->keyword_span, span_with_semicolon(item->full_span)) : item->full_span;
        error_add_related_d(error, statement, 7038, "Type originates at this import. A namespace-style import "
            "cannot be called or constructed, and will cause a failure at runtime. Consider using a default import or "
            "import require here instead.");
    }
}

// A call or `new` of a namespace import whose module's `export =` value has signatures of that kind (tsgo's
// invocationErrorRecovery with the synthetic namespace type's originatingImport)
void ty_add_originating_import_invocation(Checker* me, Error* error, Expr const* callee, bool construct) {
    if (!error || !callee || callee->tag != EXPR_IDENTIFIER) return;
    Symbol const* alias = callee->identifier.symbol;
    Ty const* original = alias && (alias->flags & SYMBOL_ALIAS) ? ty_namespace_import_original(me, alias) : nullptr;
    if (!original) return;
    Members const* members = ty_members(me, ty_apparent_type(me, original));
    if (!(construct ? members->construct_count : members->call_count)) return;
    PortItem const* item = alias->alias->item;
    Span statement = span_valid(item->keyword_span) ?
        span_between(item->keyword_span, span_with_semicolon(item->full_span)) : item->full_span;
    error_add_related_d(error, statement, 7038, "Type originates at this import. A namespace-style import "
        "cannot be called or constructed, and will cause a failure at runtime. Consider using a default import or "
        "import require here instead.");
}

// The annotation of an async function names the global Promise, from ES2015 on (tsc's checkAsyncFunctionReturnType)
void ty_check_async_return_annotation(Checker* me, FunctionImpl const* function, ErrorCtx* errs) {
    // (Only the function's own return type node: tsgo's reparser makes a JavaScript `@type` tag the FullSignature,
    // not node.Type(), so checkFunctionOrConstructorSymbol's checkAsyncFunctionReturnType never sees one)
    if (!function->is_async || function->is_generator || !function->ret_explicit) return;
    Ty const* ret = ty_from_node(me, &function->ret, (LowerCtx){ .generic = true });
    if (!ret || ret->flags & TYF_ERROR) return;
    Symbol const* promise_symbol = program_get_global(my program, c_sv("Promise"), SYMBOL_TYPE);
    Ty const* promise = promise_symbol ? ty_declared_type_of(me, promise_symbol) : nullptr;
    if (!promise || promise->kind != TY_DECLARED || promise->declared.type_params.len != 1) return;
    if (ret == promise || (ret->kind == TY_REFERENCE && ret->reference.target == promise)) return;
    Ty const* awaited = ty_awaited_no_alias(me, ret);
    error_ctx_error_d(errs, function_impl_ret_span(function), 1064, "The return type of an async function or method must be "
        "the global Promise<T> type. Did you mean to write 'Promise<", awaited && !(awaited->flags & TYF_ERROR) ? awaited : ty_intrinsic(me, TY_VOID), ">'?");
}

// A function (or class) whose call (construction) would fit: the whole error, plus a hint
static bool elaborate_did_you_mean_to_call(
    Checker* me, AssignHead head, Expr const* expr, Ty const* source, Ty const* target, ErrorCtx* errs
) {
    // (getSignaturesOfType, of an intersection too)
    Ty const* apparent = ty_apparent_type(me, source);
    if (!(source->flags & (TYF_OBJECT | TYF_INTERSECTION)) || !(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false;
    Members const* members = ty_members(me, apparent);
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
        // (At the expression's error span: a class expression's `class`, tsc's getErrorSpanForNode)
        Error* error = report_unassignable(me, head, source, target, expr_error_span(expr), errs);
        error_add_related_s(error, expr_error_span(expr), construct ? 6213 : 6212, construct ?
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
            // (JavaScript's `/** @satisfies {T} */ (x)` and `/** @type {T} */ (x)` are parentheses around a satisfies or
            // as expression to tsgo's reparser: elaborateError goes through the parentheses and stops at those)
            if (expr->as_type->angle_bracket_syntax || expr->as_type->is_satisfies || !as_type_is_const(expr->as_type)) {
                return false;
            }
            return elaborate_error(me, head, &expr->as_type->expr, source, target, errs);
        case EXPR_BINARY_OP:
            if (expr->binary_op->op != BINARY_OP_EQUAL && expr->binary_op->op != BINARY_OP_COMMA) return false;
            return elaborate_error(me, head, &expr->binary_op->right, source, target, errs);
        case EXPR_OBJECT: return elaborate_object_literal(me, expr, source, target, errs);
        // (Against the target as relations see it: a generic tuple normalized, tsc's isRelatedTo in elaborateElementwise)
        case EXPR_ARRAY: return elaborate_array_literal(me, expr, source, target, errs);
        case EXPR_JSX_ATTRIBUTES: return elaborate_jsx_attributes(me, expr, source, target, errs);
        case EXPR_FUNCTION: return elaborate_arrow_function(me, expr, source, target, errs);
        default: return false;
    }
}

// tsc's checkTypeRelatedToAndOptionallyElaborate: `expr` (may be null) is what the source came from
static bool check_assignable(
    Checker* me, AssignHead head, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs
) {
    u32 limit_hits = my instantiation_limit_hits;
    u32 complexity_hits = my relation_complexity_hits;
    u32 depth_hits = my relation_depth_hits;
    if (ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE)) return true;
    // A relation that only ran out of stack (TS2321, at the current node) is still elaborated, which may run out again
    // deeper down, at the part's node (tsc's isTypeRelatedTo reports it, then checkTypeRelatedToAndOptionallyElaborate
    // goes on). Not reported as unassignable otherwise
    if (my instantiation_limit_hits - limit_hits == my relation_depth_hits - depth_hits && my relation_depth_hits != depth_hits) {
        if (errs) elaborate_error(me, head, expr, source, target, errs);
        return false;
    }
    // (What ran into the instantiation limits has been reported as that, TS2589)
    if (my instantiation_limit_hits != limit_hits) {
        if (errs && my relation_complexity_hits != complexity_hits) {
            // tsc's explaining pass runs out of budget too, which reports TS2859 a second time, and nothing else
            // (tsc's checkTypeRelatedTo with the error node: reported there)
            VecTyReportLine lines = {0};
            CurrentNode saved = ty_enter_node(me, span);
            ty_explain_unrelated(me, source, target, TY_RELATION_ASSIGNABLE, &lines);
            ty_leave_node(me, saved);
            vec_ty_report_line_drop(&lines);
        }
        return false;
    }
    if (errs && elaborate_error(me, head, expr, source, target, errs)) return false;
    // (The head is about the target as relations see it: a substitution type's base, getNormalizedType)
    while (target->kind == TY_SUBSTITUTION) target = target->substitution.base;
    // An arrow function whose block body spans lines is reported on its first line (tsc's getErrorSpanForArrowFunction)
    if (expr && expr->tag == EXPR_FUNCTION && function_impl_is_arrow(expr->function) && span_eq(span, expr_outer_span(expr)) &&
        !expr_is_perenthisized(expr)
    ) {
        span = function_error_span(expr->function);
    }
    Error* error = report_unassignable(me, head, source, target, span, errs);
    // An argument whose promised type would fit (tsc's maybeAddMissingAwaitInfo)
    if (error && head.ts_code == HEAD_ARGUMENT.ts_code && !ty_awaited_of_promise(me, target)) {
        Ty const* awaited = ty_awaited_of_promise(me, source);
        if (awaited && ty_is_related(me, awaited, target, TY_RELATION_ASSIGNABLE)) {
            error_add_related_d(error, span, 2773, "Did you forget to use 'await'?");
        }
    }
    Symbol const* alias = expr && expr->tag == EXPR_IDENTIFIER ? expr->identifier.symbol : nullptr;
    if (error) add_originating_import(me, error, alias, target);
    return false;
}

// An error whose head says something of its own (`head`, taken), followed by why source isn't assignable to target.
// `keep_natural_head`: the elaboration starts with "Type 'S' is not assignable to type 'T'." (tsc's containing message
// chain) rather than having it replaced (tsc's head message)
// (`lines` taken: relate_explaining()'s)
static Error* report_explained_with_head(
    ErrorCtx* errs, VecTyReportLine* lines, unsigned ts_code, cstr head, bool keep_natural_head, Span span
) {
    Error* (*report)(ErrorCtx*, Span, unsigned, cstr) = error_ctx_error_s;
    span = relocated_span(lines, span);
    isize_t first = 0;
    TyReportLine const* natural = vec_ty_report_line_size(lines) ? vec_ty_report_line_at(lines, 0) : nullptr;
    Error* error;
    // (TS2678 is checkTypeComparableTo without a head message: a more specific line, a missing property, stands in
    // for it like for TS2322)
    bool default_head = ts_code == 2678 && natural && !natural->related && natural->ts_code != 2678;
    if (natural && (span_valid(natural->span) || natural->replaces_head || default_head ||
        (natural->missing_head && !keep_natural_head))
    ) {
        // An error that knows exactly where it is (an excess property) replaces the head
        cstr_drop(&head);
        error = error_ctx_error_s(errs, span_valid(natural->span) ? natural->span : span, natural->ts_code,
            cstr_clone(natural->message));
        first = 1;
    } else {
        error = report(errs, span, ts_code, head);
    }
    // (2375 is 2322 under exactOptionalPropertyTypes: tsc only picks it when there's no head message)
    if (natural && (natural->ts_code == 2322 || natural->ts_code == 2375 || natural->ts_code == 2719 || natural->ts_code == 2678 || natural->ts_code == 2820) &&
        !keep_natural_head
    ) {
        first = 1;
    }
    attach_explanation(error, lines, first);
    vec_ty_report_line_drop(lines);
    return error;
}

static Error* report_unrelated_with_head(
    Checker* me, TyRelation relation, Ty const* source, Ty const* target, unsigned ts_code, cstr head, bool keep_natural_head,
    Span span, ErrorCtx* errs
) {
    VecTyReportLine lines = {0};
    ty_explain_unrelated_headed(me, source, target, relation, ts_code, &lines);
    return report_explained_with_head(errs, &lines, ts_code, head, keep_natural_head, span);
}

static Error* report_unassignable_with_head(
    Checker* me, Ty const* source, Ty const* target, unsigned ts_code, cstr head, bool keep_natural_head, Span span,
    ErrorCtx* errs
) {
    return report_unrelated_with_head(me, TY_RELATION_ASSIGNABLE, source, target, ts_code, head, keep_natural_head, span, errs);
}

// tsc's resolveInstanceofExpression and checkInstanceOfExpression for a right operand with a `[Symbol.hasInstance]`
// method: checked like the call `right[Symbol.hasInstance](left)` (TS2860 then why the argument doesn't fit), whose
// return type must be boolean (TS2861). TODO: overloads and generic methods
void ty_check_instanceof_has_instance(Checker* me, Ty const* left, Ty const* right, Span left_span, Span right_span, ErrorCtx* errs) {
    if ((right->flags & TYF_ANY) || (left->flags & TYF_ERROR)) return;
    Atom const* name = ty_well_known_symbol_name(me, "hasInstance");
    Ty const* apparent = name ? ty_apparent_type(me, right) : nullptr;
    if (!apparent || !(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return;
    FoundProp prop = ty_find_prop(me, apparent, name);
    if (!prop.found || !(prop.type->flags & TYF_OBJECT)) return;
    Members const* members = ty_members(me, prop.type);
    if (members->call_count != 1) return;
    Signature const* sig = members->call[0];
    signature_resolve(me, sig);
    if (sig->type_params.len) return;
    Ty const* param = ty_contextual_signature_type_at(me, sig, 0, false);
    if (param && !ty_is_related(me, left, param, TY_RELATION_ASSIGNABLE)) {
        VecTyReportLine lines = {0};
        ty_explain_unrelated(me, left, param, TY_RELATION_ASSIGNABLE, &lines);
        cstr source_str, target_str;
        ty_report_pair_cstr(me, left, param, &source_str, &target_str);
        Error* error = error_ctx_error_d(errs, left_span, 2860, "The left-hand side of an 'instanceof' expression must be "
            "assignable to the first argument of the right-hand side's '[Symbol.hasInstance]' method.");
        if (error) {
            TyReportLine const* natural = vec_ty_report_line_size(&lines) ? vec_ty_report_line_at(&lines, 0) : nullptr;
            // (A missing property of the pair stands in for the argument's head message)
            Error* chain = error;
            if (!natural || !natural->missing_head) {
                error_sub_error_d(error, (Span){0}, TS_CODE_NONE, "Argument of type '", source_str,
                    "' is not assignable to parameter of type '", target_str, "'.");
                chain = vec_error_back_mut(&error->sub_errors);
            }
            isize_t first = natural && !natural->related && (natural->ts_code == 2322 || natural->ts_code == 2719) ? 1 : 0;
            for (isize_t i = first; i < vec_ty_report_line_size(&lines); i++) {
                TyReportLine const* line = vec_ty_report_line_at(&lines, i);
                if (line->related) continue;
                error_sub_error_s(chain, (Span){0}, TS_CODE_NONE, cstr_clone(line->message));
                chain = vec_error_back_mut(&chain->sub_errors);
            }
            for_each (line, vec_ty_report_line, &lines) {
                if (line->related) error_add_related_s(error, line->span, line->ts_code, cstr_clone(line->message));
            }
        }
        vec_ty_report_line_drop(&lines);
        c_drop(cstr, &source_str, &target_str);
        return;
    }
    Ty const* ret = signature_return_type(me, sig);
    Ty const* boolean = ty_intrinsic(me, TY_BOOLEAN);
    VecTyReportLine ret_lines = {0};
    if (relate_explaining(me, TY_RELATION_ASSIGNABLE, ret, boolean, 2861, &ret_lines, errs)) return;
    report_explained_with_head(errs, &ret_lines, 2861, cstr_lit("An object's '[Symbol.hasInstance]' method must return a "
        "boolean value for it to be used on the right-hand side of an 'instanceof' expression."), false, right_span);
}

// tsc's checkTypeComparableTo without a head message: TS2678 and why, or what replaces it (an excess property)
void ty_check_comparable(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    VecTyReportLine lines = {0};
    if (relate_explaining(me, TY_RELATION_COMPARABLE, source, target, 2678, &lines, errs)) return;
    // (A literal source is shown widened unless the target could hold it: tsc's reportRelationError. Of the normalized
    // pair, like TS2322's: a non-nullable source against `string | undefined` names `string`)
    cstr source_str, target_str;
    ty_report_pair_cstr(me, ty_report_source(me, source), ty_report_target(me, source, target), &source_str, &target_str);
    report_explained_with_head(errs, &lines, 2678, cstr_d(
        "Type '", source_str, "' is not comparable to type '", target_str, "'."
    ), false, span);
    c_drop(cstr, &source_str, &target_str);
}

// tsc's checkTypePredicate: `x is T` needs T assignable to x's type (TS2677, the usual explanation under it)
void ty_check_type_predicate(Checker* me, Ty const* predicate, Ty const* param, Span span, ErrorCtx* errs) {
    VecTyReportLine lines = {0};
    if (relate_explaining(me, TY_RELATION_ASSIGNABLE, predicate, param, 2677, &lines, errs)) return;
    report_explained_with_head(errs, &lines, 2677,
        cstr_from("A type predicate's type must be assignable to its parameter's type."), true, span);
}

void ty_check_type_assertion(Checker* me, Ty const* expr_type, Ty const* target, Span span, ErrorCtx* errs) {
    if (target == ty_intrinsic(me, TY_ERROR) || (target->flags & TYF_ANY)) return;
    // tsc's checkAssertionDeferred: the operand's regular base type, widened, has to be comparable to the target
    // either way
    // (Checked as the regular type, whose nested object literals are still fresh: excess properties in them count)
    Ty const* source = ty_regular_object_literal(me, ty_base_type_of_literal(me, expr_type));
    Ty const* widened = ty_widened_nullable(me, ty_widened_objects(me, source));
    if (ty_is_related(me, target, widened, TY_RELATION_COMPARABLE)) return;
    VecTyReportLine lines = {0};
    if (relate_explaining(me, TY_RELATION_COMPARABLE, source, target, 2352, &lines, errs)) return;
    // (reportRelationError's head: two types printed alike are qualified, `import("...").Locator`. Of the normalized
    // pair: `1 as string | undefined` converts `number` to `string`)
    cstr source_str, target_str;
    ty_report_pair_cstr(me, ty_report_source(me, source), ty_report_target(me, source, target), &source_str, &target_str);
    cstr head = cstr_d("Conversion of type '", source_str, "' to type '", target_str, "' may be a mistake because neither "
        "type sufficiently overlaps with the other. If this was intentional, convert the expression to 'unknown' first.");
    report_explained_with_head(errs, &lines, 2352, head, false, span);
    c_drop(cstr, &source_str, &target_str);
}

bool ty_check_heritage(
    Checker* me, Ty const* derived, Ty const* base, HeritageMember const* members, u32 count, unsigned ts_code,
    char const* what, char const* relation, Span span, ErrorCtx* errs
) {
    if ((derived->flags | base->flags) & TYF_ANY) return true;
    u32 limit_hits = my instantiation_limit_hits;
    // An interface's bases (no members to blame) are related once, explaining (tsc's checkTypeAssignableTo with the
    // error node); a class is related plainly first, then member by member (tsc's issueMemberSpecificError)
    VecTyReportLine lines = {0};
    if (!members) {
        if (relate_explaining(me, TY_RELATION_ASSIGNABLE, derived, base, ts_code, &lines, errs)) return true;
    } else if (ty_is_related(me, derived, base, TY_RELATION_ASSIGNABLE)) {
        return true;
    }
    if (my instantiation_limit_hits != limit_hits) {
        vec_ty_report_line_drop(&lines);
        return false;
    }
    cstr derived_str = ty_to_cstr(me, derived);
    cstr base_str = ty_to_cstr(me, base);
    // (The whole type's head names both as reportRelationError does, fully qualified if they'd print the same:
    // `Class 'import("./a").Addon' incorrectly implements class 'import("./b").Addon'`. A member's TS2416 doesn't)
    cstr head_derived, head_base;
    ty_report_pair_cstr(me, derived, base, &head_derived, &head_base);
    if (!members) {
        report_explained_with_head(errs, &lines, ts_code, cstr_d(what, " '", head_derived, "' ", relation, " '", head_base, "'."),
            false, span);
        c_drop(cstr, &derived_str, &base_str, &head_derived, &head_base);
        return false;
    }
    // Each of the type's own members that doesn't fit the base's (tsc's issueMemberSpecificError), else the whole type
    bool issued = false;
    Ty const* derived_apparent = ty_apparent_type(me, derived);
    Ty const* base_apparent = ty_apparent_type(me, base);
    for (u32 i = 0; members && i < count; i++) {
        u32 objects = TYF_OBJECT | TYF_INTERSECTION;
        if (!members[i].name || !(derived_apparent->flags & objects) || !(base_apparent->flags & objects)) continue;
        Prop const* prop = ty_get_prop(me, derived_apparent, members[i].name);
        Prop const* base_prop = ty_property_of_type(me, base_apparent, members[i].name); // (an intersection's too)
        if (!prop || !base_prop) continue;
        // (tsc's getTypeOfSymbol of both, an optional one's undefined or missing included)
        Ty const* source = prop_type(me, prop);
        Ty const* target = prop_type(me, base_prop);
        if (base_apparent->kind == TY_INTERSECTION) {
            // (The intersection's property, of the intersection of its members' types: tsc's getTypeOfSymbol of the
            // synthetic property, `string & number` for a getter and a setter of other types)
            FoundProp found = ty_find_prop(me, base_apparent, members[i].name);
            if (found.found) target = found.type;
        }
        VecTyReportLine member_lines = {0};
        if (relate_explaining(me, TY_RELATION_ASSIGNABLE, source, target, 2416, &member_lines, errs)) continue;
        cstr name = ty_prop_written_name(me, prop);
        report_explained_with_head(errs, &member_lines, 2416, cstr_d(
            "Property '", name, "' in type '", derived_str, "' is not assignable to the same property in base type '",
            base_str, "'."
        ), true, members[i].span);
        cstr_drop(&name);
        issued = true;
    }
    if (!issued) {
        cstr head = cstr_d(what, " '", head_derived, "' ", relation, " '", head_base, "'.");
        if (ts_code == 2720) cstr_append_d(&head, DEFAULT_DISPLAY, " Did you mean to extend '", head_base, "' and inherit its members as a subclass?");
        report_unassignable_with_head(me, derived, base, ts_code, head, false, span, errs);
    }
    c_drop(cstr, &derived_str, &base_str, &head_derived, &head_base);
    return false;
}

// tsc's getTypeWithoutSignatures(): the properties of a type (index signatures go too), of each member of an
// intersection
static Ty const* type_without_signatures(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) {
        TyList types = ty->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * types.len);
        panic_if(!parts, "out of memory");
        for (u32 i = 0; i < types.len; i++) parts[i] = type_without_signatures(me, types.data[i]);
        Ty const* ret = ty_intersection(me, parts, types.len, nullptr, (TyList){0});
        free(parts);
        return ret;
    }
    if (!(ty->flags & TYF_OBJECT)) return ty;
    Members const* members = ty_members(me, ty);
    if (!members->call_count && !members->construct_count) return ty;
    ObjectLiteralProp* props = malloc(sizeof(ObjectLiteralProp) * (members->prop_count ? members->prop_count : 1));
    panic_if(!props, "out of memory");
    for (u32 i = 0; i < members->prop_count; i++) props[i] = (ObjectLiteralProp){ .reuse = members->props[i] };
    Ty const* ret = ty_object_literal(me, props, members->prop_count, nullptr, 0, OBJECT_SYNTHETIC);
    free(props);
    return ret;
}

// How TS2417 names (part of) the base static side, `without` being it without signatures (tsc names the type without
// signatures, which keeps the symbol: a class's static side still says `typeof Base`, as do an enum's and a namespace's
// objects, tsc's createAnonymousTypeNode. An interface's, `ArrayConstructor`, is written out without its signatures)
static Ty const* static_side_for_display(Ty const* part, Ty const* without) {
    Ty const* declared = part->kind == TY_REFERENCE ? part->reference.target : part;
    if (declared->kind != TY_DECLARED) return without;
    switch (declared->declared.kind) {
        case TY_DECLARED_CLASS_STATIC:
        case TY_DECLARED_ENUM_OBJECT:
        case TY_DECLARED_NAMESPACE: return part;
        default: return part == without ? part : without;
    }
}

bool ty_check_static_heritage(Checker* me, Ty const* static_type, Ty const* base_static, Span span, ErrorCtx* errs) {
    if (!(static_type->flags & TYF_OBJECT) || !(base_static->flags & (TYF_OBJECT | TYF_INTERSECTION))) return true;
    TyList parts = base_static->kind == TY_INTERSECTION ? base_static->composite.types : (TyList){ .data = &base_static, .len = 1 };
    // HACK: a class whose base constructor is `X & K` (K a type variable) has the static type `typeof C & K` in tsc
    // (getBaseTypeVariableOfClass in getTypeOfFuncClassEnumModule); tttt's static types don't, so it's added here
    if (parts.len > 1) {
        Ty const** with = malloc(sizeof(Ty const*) * (parts.len + 1));
        panic_if(!with, "out of memory");
        u32 len = 0;
        with[len++] = static_type;
        for (u32 i = 0; i < parts.len; i++) {
            if (parts.data[i]->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) with[len++] = parts.data[i];
        }
        if (len > 1) static_type = ty_intersection(me, with, len, nullptr, (TyList){0});
        free(with);
    }
    Ty const* target = type_without_signatures(me, base_static);
    u32 limit_hits = my instantiation_limit_hits;
    if (ty_is_related(me, static_type, target, TY_RELATION_ASSIGNABLE)) return true;
    if (my instantiation_limit_hits != limit_hits) return false;
    // tsc relates to each member of an intersection on its own (typeRelatedToEachType): the one that fails is named.
    // When none does on its own, the whole intersection, each member named like it (`typeof A & typeof B`)
    Ty const* shown = static_side_for_display(base_static, target);
    if (parts.len > 1) {
        Ty const** named = malloc(sizeof(Ty const*) * parts.len);
        panic_if(!named, "out of memory");
        for (u32 i = 0; i < parts.len; i++) named[i] = static_side_for_display(parts.data[i], type_without_signatures(me, parts.data[i]));
        shown = ty_intersection(me, named, parts.len, nullptr, (TyList){0});
        free(named);
    }
    for (u32 i = 0; i < parts.len && parts.len > 1; i++) {
        Ty const* part_target = type_without_signatures(me, parts.data[i]);
        if (ty_is_related(me, static_type, part_target, TY_RELATION_ASSIGNABLE)) continue;
        target = part_target;
        shown = static_side_for_display(parts.data[i], part_target);
        break;
    }
    cstr derived_str = ty_to_cstr(me, static_type);
    cstr base_str = ty_to_cstr(me, shown);
    if (shown != target) {
        my display_of = target;
        my display_as = shown;
    }
    report_unassignable_with_head(me, static_type, target, 2417, cstr_d("Class static side '", derived_str,
        "' incorrectly extends base class static side '", base_str, "'."), false, span, errs);
    my display_of = my display_as = nullptr;
    c_drop(cstr, &derived_str, &base_str);
    return false;
}

void ty_check_satisfies(Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs) {
    check_assignable(me, HEAD_SATISFIES, source, target, span, expr, errs);
}

void ty_check_variance_annotations(Checker* me, Symbol const* owner, Generic const* generic, bool is_alias, ErrorCtx* errs) {
    if (!owner || generic_empty(generic)) return;
    for (u32 i = 0; i < (u32)vec_generic_item_size(&generic->items); i++) {
        GenericItem const* item = vec_generic_item_at(&generic->items, i);
        if (item->jsdoc) continue; // TODO: JSDoc `@template in T`
        // (Of the type parameter's symbol, tsc's getTypeParameterModifiers: a name the list repeats is one symbol)
        unsigned modifiers = 0;
        for (u32 j = 0; j < (u32)vec_generic_item_size(&generic->items); j++) {
            GenericItem const* other = vec_generic_item_at(&generic->items, j);
            if (j == i || (!other->jsdoc && csview_equals_sv(span_sv(other->name), span_sv(item->name)))) {
                modifiers |= ty_variance_modifiers(owner, j);
            }
        }
        if (!modifiers) continue;
        if (is_alias) {
            // Only object types (literals, function, constructor and mapped types, and instantiations of them)
            Ty const* declared = ty_declared_type_of(me, owner);
            if (declared->kind != TY_ANONYMOUS || (declared->flags & TYF_ERROR)) {
                error_ctx_error_d(errs, item->full_span, 2637, "Variance annotations are only supported in type aliases for "
                    "object, function, constructor, and mapped types.");
                continue;
            }
        }
        if (modifiers == (VARIANCE_MODIFIER_IN | VARIANCE_MODIFIER_OUT)) continue;
        bool out = modifiers == VARIANCE_MODIFIER_OUT;
        Ty const* source = ty_variance_check_instance(me, owner, i, out);
        Ty const* target = ty_variance_check_instance(me, owner, i, !out);
        if (!source || !target) continue;
        VecTyReportLine lines = {0};
        if (relate_explaining(me, TY_RELATION_ASSIGNABLE, source, target, 2636, &lines, errs)) continue;
        cstr source_str, target_str;
        ty_report_pair_cstr(me, source, target, &source_str, &target_str);
        report_explained_with_head(errs, &lines, 2636, cstr_d("Type '", source_str, "' is not assignable to type '",
            target_str, "' as implied by variance annotation."), false, item->full_span);
        c_drop(cstr, &source_str, &target_str);
    }
}

// A computed enum member's value is a number (tsc's computeEnumMemberValue for what it can't evaluate)
void ty_check_enum_member_value(Checker* me, Ty const* source, Span span, ErrorCtx* errs) {
    Ty const* number = ty_intrinsic(me, TY_NUMBER);
    VecTyReportLine lines = {0};
    if (relate_explaining(me, TY_RELATION_ASSIGNABLE, source, number, 18033, &lines, errs)) return;
    cstr source_str, target_str;
    ty_report_pair_cstr(me, source, number, &source_str, &target_str);
    report_explained_with_head(errs, &lines, 18033, cstr_d("Type '", source_str, "' is not assignable to type '",
        target_str, "' as required for computed enum member values."), false, span);
    c_drop(cstr, &source_str, &target_str);
}

// (tsc's checkTypeAssignableTo with an error node: one run, nothing to elaborate)
bool ty_check_assignable(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    u32 limit_hits = my instantiation_limit_hits;
    VecTyReportLine lines = {0};
    if (relate_explaining(me, TY_RELATION_ASSIGNABLE, source, target, 0, &lines, errs)) return true;
    // (What ran into the instantiation limits has been reported as that, TS2589, or TS2859 by the run itself)
    if (my instantiation_limit_hits != limit_hits) {
        vec_ty_report_line_drop(&lines);
        return false;
    }
    // (The head is about the target as relations see it: a substitution type's base, getNormalizedType)
    while (target->kind == TY_SUBSTITUTION) target = target->substitution.base;
    report_unassignable_explained(me, HEAD_TYPE, source, target, &lines, span, errs);
    return false;
}

bool ty_relate_explaining(
    Checker* me, Ty const* source, Ty const* target, unsigned head_code, VecTyReportLine* lines, ErrorCtx* errs
) {
    return relate_explaining(me, TY_RELATION_ASSIGNABLE, source, target, head_code, lines, errs);
}

Error* ty_report_explained_with_head(ErrorCtx* errs, VecTyReportLine* lines, unsigned ts_code, cstr head, Span span) {
    return report_explained_with_head(errs, lines, ts_code, head, false, span);
}

void ty_report_unassignable_with_head(Checker* me, Ty const* source, Ty const* target, unsigned ts_code, cstr head, Span span, ErrorCtx* errs) {
    report_unassignable_with_head(me, source, target, ts_code, head, false, span, errs);
}

bool ty_check_assignable_expr(Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs) {
    return check_assignable(me, HEAD_TYPE, source, target, span, expr, errs);
}

// `x.a = value` (tsc's checkAssignmentOperator: under exactOptionalPropertyTypes an undefined for an optional property
// says so)
bool ty_check_assignable_to_access(
    Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, Expr const* left, ErrorCtx* errs
) {
    AssignHead head = HEAD_TYPE;
    if (compiler_options()->exact_optional_property_types && left->tag == EXPR_PROPERTY &&
        left->property->form == PROPERTY_EXPR_DOT && ty_exact_optional_mismatch(me, source, ty_intrinsic(me, TY_MISSING))
    ) {
        Atom const* name = ty_literal_name(me, ty_access_key_type(me, left->property));
        // (Not a private name: tsc looks `#x` up by its text, which isn't the property's mangled name)
        if (name && atom_sv(name).size && atom_sv(name).buf[0] == '#') name = nullptr;
        // (Of the object's apparent type, a type parameter's constraint or a class's `this`: tsc's getTypeOfPropertyOfType)
        FoundProp prop = name ? ty_find_prop(me, ty_apparent_type(me, ty_expr_type(me, &left->property->expr)), name) :
            (FoundProp){0};
        if (prop.found && ty_exact_optional_mismatch(me, source, prop.type)) {
            head = HEAD_EXACT_PROP;
        }
    }
    return check_assignable(me, head, source, target, span, expr, errs);
}

// The global ImportAttributes / ImportCallOptions, or null (tsc's getGlobalImportAttributesType & co give the empty
// object type, which isn't checked against)
static Ty const* global_import_type(Checker* me, char const* name) {
    Symbol const* symbol = program_get_global(my program, csview_from(name), SYMBOL_TYPE);
    if (!symbol || !(symbol->flags & SYMBOL_INTERFACE)) return nullptr;
    Ty const* type = ty_declared_type_of(me, symbol);
    return type->kind == TY_DECLARED && !type->declared.type_params.len ? type : nullptr;
}

static Ty const* with_undefined(Checker* me, Ty const* type) {
    return ty_union(me, (Ty const*[]){ type, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
}

// `with { type: "json" }`: an object type of the attributes' regular types against the global ImportAttributes (tsc's
// checkImportAttributes with getTypeFromImportAttributes)
void ty_check_import_attributes(Checker* me, ImportAttributes const* attributes, ErrorCtx* errs) {
    Ty const* target = global_import_type(me, "ImportAttributes");
    if (!target) return;
    u32 count = (u32)vec_import_attribute_size(&attributes->elements);
    ObjectLiteralProp* props = calloc(count ? count : 1, sizeof(ObjectLiteralProp));
    panic_if(!props, "out of memory");
    for_each (i, vec_import_attribute, &attributes->elements) {
        csview name = span_sv(i->name);
        if (name.size >= 2 && (name.buf[0] == '"' || name.buf[0] == '\'')) name = csview_slice(name, 1, name.size - 1);
        props[i_index] = (ObjectLiteralProp){
            .name = checker_atom(me, name),
            .type = ty_regular(me, ty_check_expr(me, &i->value, nullptr)),
            // (No declaration: tsgo's members are new symbols, named as their text says)
            .name_form = PROP_NAME_KNOWN | PROP_NAME_STRING | PROP_NAME_RAW,
            .own = true,
        };
    }
    Ty const* source = ty_object_literal(me, props, count, nullptr, 0, OBJECT_SYNTHETIC);
    free(props);
    ty_check_assignable(me, source, with_undefined(me, target), attributes->span, errs);
}

// `declare module "*.css" with { type: "css" } {}`: the type literal against the global ImportAttributes (tsgo's
// checkImportAttributesType)
void ty_check_module_attributes(Checker* me, TypeNode const* attributes, Span span, ErrorCtx* errs) {
    Ty const* target = global_import_type(me, "ImportAttributes");
    if (!target) return;
    ty_check_assignable(me, ty_from_node(me, attributes, (LowerCtx){0}), target, span, errs);
}

// The options of `import("m", options)`, which were checked with ImportCallOptions as their contextual type (tsc's
// checkImportCallExpression)
// tsc's checkImportCallExpression: the specifier is a string (null and undefined aren't, whatever strictNullChecks says)
void ty_check_import_call_specifier(Checker* me, Expr const* specifier, ErrorCtx* errs) {
    // (checked with string as its contextual type, tsc's getContextualTypeForArgumentAtIndex of an import call)
    Ty const* type = ty_check_expr(me, specifier, ty_intrinsic(me, TY_STRING));
    if (type->flags & TYF_ERROR) return;
    if (!(type->flags & (TYF_UNDEFINED | TYF_NULL)) &&
        ty_is_related(me, type, ty_intrinsic(me, TY_STRING), TY_RELATION_ASSIGNABLE)
    ) {
        return;
    }
    cstr shown = ty_to_cstr(me, type);
    error_ctx_error_d(errs, expr_outer_span(specifier), 7036, "Dynamic import's specifier must be of type 'string', but here ",
        "has type '", shown, "'.");
    cstr_drop(&shown);
}

void ty_check_import_call_options(Checker* me, Expr const* options, ErrorCtx* errs) {
    Ty const* target = global_import_type(me, "ImportCallOptions");
    if (target) {
        Ty const* source = ty_check_expr(me, options, target);
        ty_check_assignable(me, source, with_undefined(me, target), expr_outer_span(options), errs);
    }
    // `import(m, { assert: ... })`: deprecated in 6.0
    if (options->tag == EXPR_OBJECT && !expr_is_perenthisized(options)) {
        for_each (i, vec_object_expr_item, &options->object) {
            ObjectExprItem const* prop = i;
            if (prop->tag == OBJECT_ITEM_PROPERTY && !prop->computed_key && prop->name_span.size &&
                *prop->name_span.buf != '"' && *prop->name_span.buf != '\'' && cstr_equals(&prop->name_string, "assert")
            ) {
                // (A checker error in tsgo, not a grammar one: also in a file with parse errors)
                error_ctx_error_d(errs, prop->name_span, 2880,
                    "Import assertions have been replaced by import attributes. Use 'with' instead of 'assert'.");
                break;
            }
        }
    }
}

// tsgo's getGlobalAbstractModuleSourceType: what a source phase import gives, `{}` and TS2318 (once) when the lib
// doesn't have it (esnext.modulesource)
Ty const* ty_abstract_module_source_type(Checker* me) {
    Symbol const* symbol = program_get_global(my program, c_sv("AbstractModuleSource"), SYMBOL_TYPE);
    if (symbol && (symbol->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS))) {
        Ty const* type = ty_declared_type_of(me, symbol);
        if (type->kind == TY_DECLARED && !type->declared.type_params.len) return type;
    }
    if (!symbol) error_ctx_global_error_d(my errs, 2318, "Cannot find global type 'AbstractModuleSource'.");
    return ty_empty_object_type(me);
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
    u32 limit_hits = my instantiation_limit_hits;
    VecTyReportLine lines = {0};
    unsigned code = is_await ? 2851 : 2850;
    if (relate_explaining(me, TY_RELATION_ASSIGNABLE, source, target, code, &lines, errs)) return;
    if (my instantiation_limit_hits != limit_hits) {
        vec_ty_report_line_drop(&lines);
        return;
    }
    Error* error = report_explained_with_head(errs, &lines, code, is_await ?
        cstr_lit("The initializer of an 'await using' declaration must be either an object with a "
            "'[Symbol.asyncDispose]()' or '[Symbol.dispose]()' method, or be 'null' or 'undefined'.") :
        cstr_lit("The initializer of a 'using' declaration must be either an object with a '[Symbol.dispose]()' "
            "method, or be 'null' or 'undefined'."), false, span);
    // A plain `using` of something async disposable: a hint at `await using` (tsgo's checkVariableLikeDeclaration,
    // AddMessageChain: a line of the chain's first level)
    if (!is_await && error) {
        Symbol const* symbol = program_get_global(my program, c_sv("AsyncDisposable"), SYMBOL_TYPE);
        if (symbol && symbol->flags & (SYMBOL_INTERFACE | SYMBOL_CLASS)) {
            Ty const* parts[3] = { ty_declared_type_of(me, symbol), ty_intrinsic(me, TY_NULL), ty_intrinsic(me, TY_UNDEFINED) };
            if (ty_is_related(me, source, ty_union(me, parts, 3, nullptr, (TyList){0}), TY_RELATION_ASSIGNABLE)) {
                error_sub_error_d(error, span_null, 18116,
                    "This initializer has a '[Symbol.asyncDispose]()' method. Did you mean to use 'await using'?");
                // (A line of the chain: before the related information, which follows the whole chain)
                Error* subs = error->sub_errors.data;
                isize n = vec_error_size(&error->sub_errors);
                for (isize i = n - 1; i > 0 && span_valid(subs[i - 1].span); i--) {
                    Error line = subs[i];
                    subs[i] = subs[i - 1];
                    subs[i - 1] = line;
                }
            }
        }
    }
}

void ty_report_unfit_argument(
    Checker* me, Ty const* source, Ty const* target, Span span, Expr const* expr, ErrorCtx* errs
) {
    check_assignable(me, HEAD_ARGUMENT, source, target, span, expr, errs);
}

void ty_report_unfit_this(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    check_assignable(me, HEAD_THIS, source, target, span, nullptr, errs);
}

// TS2786 (tsc's checkJsxReturnAssignableToAppropriateBound and the JSX.ElementType check): "'C' cannot be used as a JSX
// component." over what isn't valid about it ("Its return type 'R' is not a valid JSX element."), over why `source`
// isn't assignable to `target`. False if it isn't (then it was reported)
bool ty_check_jsx_component(Checker* me, Ty const* source, Ty const* target, Span tag_name, JsxComponentCheck kind, ErrorCtx* errs) {
    if (source->flags & TYF_ANY) return true;
    VecTyReportLine lines = {0};
    if (relate_explaining(me, TY_RELATION_ASSIGNABLE, source, target, 0, &lines, errs)) return true;
    Error* error = error_ctx_error_d(errs, tag_name, 2786, "'", tag_name, "' cannot be used as a JSX component.");
    // (The message is reportRelationError's head: the names told apart from the target's if they'd print the same)
    // (Against the target as isRelatedTo has it when it reports: `JSX.Element | null` without the null, tsc's
    // nullStrippedTarget, so a source printing the same as JSX.Element is fully qualified)
    cstr source_str, target_str;
    ty_report_pair_cstr(me, source, target->alias ? target : ty_strip_nullable_target(me, source, target), &source_str, &target_str);
    cstr_drop(&target_str);
    switch (kind) {
        case JSX_COMPONENT_RETURN: error_sub_error_d(error, span_null, TS_CODE_NONE, "Its return type '", source_str, "' is not a valid JSX element."); break;
        case JSX_COMPONENT_INSTANCE: error_sub_error_d(error, span_null, TS_CODE_NONE, "Its instance type '", source_str, "' is not a valid JSX element."); break;
        case JSX_COMPONENT_ELEMENT: error_sub_error_d(error, span_null, TS_CODE_NONE, "Its element type '", source_str, "' is not a valid JSX element."); break;
        case JSX_COMPONENT_TYPE: error_sub_error_d(error, span_null, TS_CODE_NONE, "Its type '", source_str, "' is not a valid JSX element type."); break;
    }
    cstr_drop(&source_str);
    // The explanation goes under that line, less the generic "Type 'S' is not assignable" its message replaces
    Error* chain = vec_error_back_mut(&error->sub_errors);
    TyReportLine const* natural = vec_ty_report_line_size(&lines) ? vec_ty_report_line_at(&lines, 0) : nullptr;
    isize_t first = natural && !natural->related && (natural->ts_code == 2322 || natural->ts_code == 2719) ? 1 : 0;
    for (isize_t i = first; i < vec_ty_report_line_size(&lines); i++) {
        TyReportLine const* line = vec_ty_report_line_at(&lines, i);
        if (line->related) continue;
        error_sub_error_s(chain, (Span){0}, TS_CODE_NONE, cstr_clone(line->message));
        chain = vec_error_back_mut(&chain->sub_errors);
    }
    for_each (line, vec_ty_report_line, &lines) {
        if (line->related) error_add_related_s(error, line->span, line->ts_code, cstr_clone(line->message));
    }
    vec_ty_report_line_drop(&lines);
    return false;
}

// tsc's isValidSpreadType
static Ty const* spread_base_constraint(Checker* me, Ty const* ty) {
    // (An intersection with type variables has one too: `C<E> & {}` is at least `{}` when C<E> is at least unknown,
    // tsc's getBaseConstraintOrType of a UnionOrIntersection)
    bool generic_intersection = ty->kind == TY_INTERSECTION && (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES);
    if (!(ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) && !generic_intersection) return nullptr;
    return ty_base_constraint(me, ty);
}

static bool is_valid_spread_type(Checker* me, Ty const* ty, u32 depth) {
    if (ty->kind == TY_UNION) {
        // (tsc maps the base constraint over it before removing what's definitely falsy)
        TyBuf parts = {0};
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* part = ty->composite.types.data[i];
            Ty const* constraint = spread_base_constraint(me, part);
            ty_buf_push(&parts, constraint ? constraint : part);
        }
        Ty const* mapped = ty_remove_definitely_falsy(me, ty_union(me, parts.data, parts.len, nullptr, (TyList){0}));
        ty_buf_drop(&parts);
        if (mapped->kind != TY_UNION) return is_valid_spread_type(me, mapped, depth + 1);
        for (u32 i = 0; i < mapped->composite.types.len; i++) {
            if (!is_valid_spread_type(me, mapped->composite.types.data[i], depth + 1)) return false;
        }
        return true;
    }
    Ty const* constraint = spread_base_constraint(me, ty);
    if (constraint && constraint != ty) return is_valid_spread_type(me, constraint, depth + 1);
    ty = ty_remove_definitely_falsy(me, ty);
    if (ty->flags & (TYF_ANY | TYF_NON_PRIMITIVE | TYF_OBJECT | TYF_TYPE_PARAM | TYF_DEFERRED)) {
        return !(ty->flags & (TYF_STRING_LIKE)) || (ty->flags & TYF_ANY); // (a deferred string mapping isn't an object)
    }
    if (ty->kind == TY_INTERSECTION || ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!is_valid_spread_type(me, ty->composite.types.data[i], depth + 1)) return false;
        }
        return true;
    }
    return false;
}

bool ty_is_valid_spread_type(Checker* me, Ty const* type) {
    return is_valid_spread_type(me, type, 0);
}

// `...x` in an object literal or JSX attributes: x must be an object (TS2698)
void ty_check_spread_type(Checker* me, Ty const* type, Span span, ErrorCtx* errs) {
    if (!is_valid_spread_type(me, type, 0)) {
        error_ctx_error_d(errs, span, 2698, "Spread types may only be created from object types.");
    }
}

// tsc's checkSpreadPropOverrides (strictNullChecks only): a property written before a spread that certainly has one of
// that name (in every member of a union, and required) is overwritten: TS2783 at the property, the last one written
// of that name
void ty_check_spread_overrides(Checker* me, Ty const* type, TyWrittenProp const* written, u32 count, Span spread_span, ErrorCtx* errs) {
    if (!count || !compiler_options()->strict_null_checks || !is_valid_spread_type(me, type, 0) || (type->flags & TYF_ANY)) return;
    TyList members = type->kind == TY_UNION && !(type->flags & TYF_BOOLEAN) ? type->composite.types : (TyList){ .data = &type, .len = 1 };
    // (Something that spreads into nothing next to an object makes the object's properties optional: tsc's
    // tryMergeUnionOfObjectTypeAndEmptyObject)
    for (u32 i = 0; i < members.len; i++) {
        if (!(ty_apparent_type(me, members.data[i])->flags & (TYF_OBJECT | TYF_INTERSECTION))) return;
    }
    for (u32 w = count; w-- > 0;) {
        // The last one written of each name
        bool later = false;
        for (u32 v = w + 1; v < count && !later; v++) later = written[v].name == written[w].name;
        if (later) continue;
        bool always = true;
        for (u32 i = 0; i < members.len && always; i++) {
            // (Its properties, not Object's: tsc's getPropertiesOfType, `{ toString, ...ctx }` keeps the first)
            FoundProp found = ty_find_own_prop(me, ty_apparent_type(me, members.data[i]), written[w].name);
            always = found.found && !(found.flags & PROP_OPTIONAL);
        }
        if (!always) continue;
        Error* error = error_ctx_error_d(errs, written[w].span, 2783, "'", atom_sv(written[w].name),
            "' is specified more than once, so this usage will be overwritten.");
        error_add_related_d(error, spread_span, 2785, "This spread always overwrites this property.");
    }
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

// A type as a plain typeToString prints it (tsc's default AllowUniqueESSymbolType: `unique symbol`, not `typeof sym`,
// unlike the relation errors' getTypeNamesForErrorDisplay)
// `index_span`: a bigint literal written there is 'bigint' (tsc's getPropertyTypeForIndexType looks at the node kind)
static cstr index_type_str(Checker* me, Ty const* index, Span index_span) {
    csview written = span_valid(index_span) ? span_sv(index_span) : (csview){0};
    if ((index->flags & TYF_BIGINT_LITERAL) && written.size > 1 && written.buf[0] >= '0' && written.buf[0] <= '9' &&
        written.buf[written.size - 1] == 'n'
    ) {
        return cstr_lit("bigint");
    }
    cstr ret = cstr_null;
    ty_display(me, (TyDisplay){ .target = &ret }, index);
    return ret;
}

// The class an instance type is of, if it is
static Symbol const* instance_class(Ty const* object) {
    Ty const* declared = object->kind == TY_REFERENCE ? object->reference.target : object;
    if (declared->kind != TY_DECLARED || declared->declared.kind != TY_DECLARED_CLASS_INSTANCE) return nullptr;
    return declared->declared.symbol;
}

static bool check_accessibility(
    Checker* me, AccessSite const* site, Span span, bool this_object, bool writing, Prop const* prop,
    Ty const* containing, bool is_super, ErrorCtx* errs
);

static bool is_keyword(Expr const* expr, KeywordIdentifier keyword) {
    return expr->tag == EXPR_IDENTIFIER && expr->identifier.keyword == keyword;
}

static ClassDecl const* class_decl_of(Symbol const* symbol);
static bool is_within_class(ClassDecl const* enclosing, ClassDecl const* class_decl);

// tsgo's compareSymbols of two suggested properties: by their first declaration, then by name
static int compare_suggested_props(void* ctx, isize a, isize b) {
    Prop const* const* props = ctx;
    // (An instantiation's declarations are its target's: a class instance type's members are instantiated for `this`)
    Prop const* pa = props[a];
    Prop const* pb = props[b];
    while (!pa->decl->decl_span_count && pa->target) pa = pa->target;
    while (!pb->decl->decl_span_count && pb->target) pb = pb->target;
    PropDecl const* da = pa->decl;
    PropDecl const* db = pb->decl;
    return ty_compare_declarations(da->decl_span_count ? da->decl_spans[0] : span_null, atom_sv(props[a]->name),
        db->decl_span_count ? db->decl_spans[0] : span_null, atom_sv(props[b]->name));
}

// tsc's isValidPropertyAccessForCompletions: whether `access` may read prop (of `containing`). A #private member only
// inside its class, outside an optional chain (tsc's isPropertyAccessible)
static bool suggestion_accessible(Checker* me, PropertyExpr const* access, Prop const* prop, Ty const* containing) {
    PropertyContext const* site = property_context(access);
    csview name = atom_sv(prop->name);
    if (name.size && name.buf[0] == '#') {
        ClassDecl const* declaring = class_decl_of(prop->parent);
        return declaring && !access->optional_chain && is_within_class(site->enclosing_class, declaring);
    }
    AccessSite access_site = {
        .enclosing_class = site->enclosing_class, .containing_function = site->containing_function,
        .this_parameter = site->this_parameter, .this_function = site->this_function,
        .initializer_context = site->initializer_context,
    };
    ErrorCtx discard = error_ctx_init();
    bool ret = check_accessibility(me, &access_site, property_key_span(access), is_keyword(&access->expr, KEYWORD_IDENTIFIER_THIS),
        false, prop, containing, is_keyword(&access->expr, KEYWORD_IDENTIFIER_SUPER), &discard);
    error_ctx_drop(&discard);
    return ret;
}

// tsc's getSuggestedSymbolForNonexistentProperty: the property of `object` whose name is closest to `name` (of a union,
// those all its members have)
Prop const* ty_suggested_property(Checker* me, Ty const* object, csview name) {
    TyList parts = object->kind == TY_UNION ? object->composite.types : (TyList){ .data = &object, .len = 1 };
    Ty const* first = nullptr;
    for (u32 i = 0; i < parts.len && !first; i++) {
        if (!(parts.data[i]->flags & TYF_NULLABLE)) first = ty_apparent_type(me, parts.data[i]);
    }
    if (!first || !(first->flags & (TYF_OBJECT | TYF_INTERSECTION))) return nullptr;
    // (tsc's getPropertiesOfType: an intersection's are those of all of its members, each name once)
    TyList holders = first->kind == TY_INTERSECTION ? first->composite.types : (TyList){ .data = &first, .len = 1 };
    Prop const** props = nullptr;
    u32 count = 0;
    for (u32 h = 0; h < holders.len; h++) {
        Ty const* holder = ty_apparent_type(me, holders.data[h]);
        if (!(holder->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, holder);
        props = realloc(props, sizeof(Prop const*) * (count + members->prop_count + 1));
        panic_if(!props, "out of memory");
        for (u32 i = 0; i < members->prop_count; i++) {
            bool seen = false;
            for (u32 k = 0; k < count && !seen; k++) seen = props[k]->name == members->props[i]->name;
            if (!seen) props[count++] = members->props[i];
        }
    }
    if (!count) {
        free(props);
        return nullptr;
    }
    csview* names = malloc(sizeof(csview) * count);
    panic_if(!names, "out of memory");
    for (u32 i = 0; i < count; i++) {
        Prop const* prop = props[i];
        csview candidate = atom_sv(prop->name);
        bool everywhere = candidate.size;
        for (u32 j = 0; j < parts.len && everywhere; j++) {
            if (parts.data[j]->flags & TYF_NULLABLE) continue;
            // (An object literal without it has it as undefined: createUnionOrIntersectionProperty's WritePartial
            // makes it a property of the union all the same)
            if (ty_is_object_literal_without_spread(me, parts.data[j])) continue;
            // (Object's members count: tsc's union property looks each member up with getPropertyOfType, `valueOf` of
            // `string | Ref<string>`)
            everywhere = ty_find_prop(me, ty_apparent_type(me, parts.data[j]), prop->name).found;
        }
        if (everywhere && me->suggestion_access) everywhere = suggestion_accessible(me, me->suggestion_access, prop, first);
        names[i] = everywhere ? candidate : (csview){0};
    }
    // (Of the candidates at the best distance, the first declared: tsgo's getSpellingSuggestion with compareSymbols)
    isize best = spelling_suggestion_ex(name, names, count, compare_suggested_props, props);
    Prop const* ret = best >= 0 ? props[best] : nullptr;
    free(names);
    free(props);
    return ret;
}

// The name of an empty DOM type a program without the dom lib may declare itself (tsc's
// /^(?:EventTarget|Node|(?:HTML[a-zA-Z]*)?Element)$/)
static bool dom_element_name(csview name) {
    if (csview_equals(name, "EventTarget") || csview_equals(name, "Node") || csview_equals(name, "Element")) return true;
    if (name.size < 11 || !csview_starts_with(name, "HTML") || !csview_ends_with(name, "Element")) return false;
    for (isize i = 4; i < name.size - 7; i++) {
        if (!isalpha((unsigned char)name.buf[i])) return false;
    }
    return true;
}

// tsc's containerSeemsToBeEmptyDomElement: TS2812 suggests the dom lib
static bool seems_empty_dom_element(Checker* me, Ty const* ty) {
    csview lib = cstr_sv(&compiler_options()->lib);
    if (!lib.size) return false;
    for (isize i = 0; i + 3 <= lib.size; i++) {
        if (memcmp(lib.buf + i, "dom", 3) == 0 && (i == 0 || lib.buf[i - 1] == ',') && (i + 3 == lib.size || lib.buf[i + 3] == ',' || lib.buf[i + 3] == '.')) {
            return false;
        }
    }
    TyList parts = ty->kind == TY_INTERSECTION || ty->kind == TY_UNION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = parts.data[i];
        Symbol const* symbol = part->kind == TY_DECLARED ? part->declared.symbol : nullptr;
        if (!symbol || !dom_element_name(symbol->name) || !ty_is_empty_object_type(me, part)) return false;
    }
    return true;
}

// The declaration of the property `name` of ty, following instantiations; of an intersection the one its parts share
// (null if they have different ones: tsc's createUnionOrIntersectionProperty only keeps a single declaration)
static PropDecl const* prop_root_decl(Checker* me, Ty const* ty, Atom const* name) {
    if (ty->kind == TY_INTERSECTION) {
        PropDecl const* ret = nullptr;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            PropDecl const* part = prop_root_decl(me, ty_apparent_type(me, ty->composite.types.data[i]), name);
            if (!part) continue;
            if (ret && ret != part) return nullptr;
            ret = part;
        }
        return ret;
    }
    if (!(ty->flags & TYF_OBJECT)) return nullptr;
    Prop const* prop = ty_get_prop(me, ty, name);
    while (prop && prop->target) prop = prop->target;
    return prop ? prop->decl : nullptr;
}

// A property tsc gives declarations but no valueDeclaration: a mapped or reverse mapped type's, a spread's fresh copy, a
// namespace's export that is an alias (`export { f }`: the export specifier)
static bool no_value_declaration(Prop const* prop) {
    return prop->mapped || (prop->flags & PROP_NO_VALUE_DECLARATION) || prop_decl_rare(prop->decl)->reverse_of;
}

// destructured: a binding element's property (tsc's getIndexedAccessType reports it, without saying which union member
// lacks it)
static void report_no_property(Checker* me, Ty const* object, Ty const* index, unsigned ts_code, Span span, bool destructured,
    ErrorCtx* errs
) {
    // A name the parser found missing (`o.` then something else) has said enough (tsc: `right.escapedText &&`)
    if (span_valid(span) && !span.size) return;
    cstr name = index_name(me, index);
    // (A binding element's is about the apparent type: tsc's getIndexedAccessType reports on getReducedApparentType,
    // `{}` for `object`, the constraint's for a type parameter)
    u32 primitive = TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
        TYF_BOOLEAN_LITERAL | TYF_ES_SYMBOL_LIKE | TYF_NON_PRIMITIVE | TYF_TYPE_PARAM | TYF_DEFERRED;
    cstr object_str = ty_to_cstr(me, destructured && (object->flags & primitive) ? ty_apparent_type(me, object) : object);
    Error* error;
    // (Nor does it suggest anything: tsc's getPropertyTypeForIndexType only does for element access expressions)
    Symbol const* class_symbol = ts_code == 2339 && !destructured ? instance_class(object) : nullptr;
    Atom const* atom = (index->flags & TYF_STRING_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) ? index->literal.string : nullptr;
    Prop const* suggestion = nullptr;
    // The related information, added after the elaboration chain (tsc adds it to the finished diagnostic)
    Span declared_at = {0};
    bool forgot_await = false;
    // (tsc's typeHasStaticProperty: a declared static member, `#x`s being keyed by their class there)
    bool static_member = atom && class_symbol && atom->len && atom->str[0] != '#' && !csview_equals(atom_sv(atom), "prototype") &&
        ty_get_prop(me, ty_static_type_of(me, class_symbol), atom);
    // A member a later lib adds (tsc's getSuggestedLibForNonExistentProperty)
    Ty const* apparent = ty_apparent_type(me, object);
    Ty const* apparent_declared = apparent->kind == TY_REFERENCE ? apparent->reference.target : apparent;
    // (The type's symbol: a declared type's, or a namespace's or function's for its typeof)
    Symbol const* container = apparent_declared->kind == TY_DECLARED ?
        (apparent_declared->declared.symbol ? apparent_declared->declared.symbol : apparent_declared->declared.namespace_symbol) :
        apparent_declared->kind == TY_ANONYMOUS && !apparent_declared->anonymous.is_reverse ? apparent_declared->anonymous.merged_value : nullptr;
    char const* lib = atom && ts_code == 2339 && !destructured && container ? lib_feature_of_member(container->name, atom_sv(atom)) : nullptr;
    // A property of what a promise resolves to (tsc's reportNonexistentProperty with getPromisedTypeOfPromise)
    Ty const* promised = atom && ts_code == 2339 && !destructured && !static_member ? ty_promised_type(me, object) : nullptr;
    if (promised && !ty_has_prop(me, ty_apparent_type(me, promised), atom)) promised = nullptr; // (A union's too)
    if (static_member) {
        error = error_ctx_error_d(errs, span, 2576, "Property '", name, "' does not exist on type '", object_str,
            "'. Did you mean to access the static member '", object_str, ".", name, "' instead?");
    } else if (promised) {
        error = error_ctx_error_d(errs, span, 2339, "Property '", name, "' does not exist on type '", object_str, "'.");
        forgot_await = true;
    } else if (lib) {
        error = error_ctx_error_d(errs, span, 2550, "Property '", name, "' does not exist on type '", object_str,
            "'. Do you need to change your target library? Try changing the 'lib' compiler option to '", lib, "' or later.");
    } else if (atom && ts_code == 2339 && !destructured && !(object->flags & TYF_NEVER) && (suggestion = ty_suggested_property(me, object, atom_sv(atom)))) {
        error = error_ctx_error_d(errs, span, 2551, "Property '", name, "' does not exist on type '", object_str,
            "'. Did you mean '", atom_sv(suggestion->name), "'?");
        // (A mapped or reverse mapped type's property, or a spread's fresh copy, has declarations but no
        // valueDeclaration in tsc: no related information)
        Prop const* declared = suggestion;
        bool synthetic = no_value_declaration(declared);
        while (declared->target && !declared->decl->decl_span_count) {
            declared = declared->target;
            synthetic |= no_value_declaration(declared);
        }
        // (A union's property has a declaration only if every member's is the same one: tsc's
        // createUnionOrIntersectionProperty with hasNonUniformValueDeclaration)
        bool uniform = true;
        if (object->kind == TY_INTERSECTION) {
            // (Likewise an intersection's: two members declaring it make none)
            Prop const* root = suggestion;
            while (root->target) root = root->target;
            for (u32 i = 0; i < object->composite.types.len && uniform; i++) {
                PropDecl const* part = prop_root_decl(me, ty_apparent_type(me, object->composite.types.data[i]), suggestion->name);
                uniform = !part || part == root->decl;
            }
        } else if (object->kind == TY_UNION) {
            Prop const* root = suggestion;
            while (root->target) root = root->target;
            for (u32 i = 0; i < object->composite.types.len && uniform; i++) {
                Ty const* part = object->composite.types.data[i];
                if (part->flags & TYF_NULLABLE) continue;
                PropDecl const* part_decl = prop_root_decl(me, ty_apparent_type(me, part), suggestion->name);
                // (An object literal without it adds no declaration, only undefined: WritePartial)
                if (!part_decl && ty_is_object_literal_without_spread(me, part)) continue;
                uniform = part_decl == root->decl;
            }
        }
        if (error && uniform && !synthetic && declared->decl->decl_span_count) {
            // (tsc's error span of the declaration: its name)
            Span name_span = ty_prop_decl_span(suggestion);
            // (Its valueDeclaration: a class field declared after a JavaScript `this.x = v` that joined it is, tsc's
            // setValueDeclaration prefers a declaration to an assignment)
            Span at = declared->decl->decl_spans[0];
            for (u32 k = 0; k < declared->decl->decl_span_count; k++) {
                if (span_contains_span(declared->decl->decl_spans[k], name_span)) {
                    at = name_span;
                    break;
                }
            }
            declared_at = at;
        }
    } else if (ts_code == 2339 && !destructured && seems_empty_dom_element(me, object)) {
        error = error_ctx_error_d(errs, span, 2812, "Property '", name, "' does not exist on type '", object_str,
            "'. Try changing the 'lib' compiler option to include 'dom'.");
    } else {
        error = error_ctx_error_d(errs, span, ts_code, "Property '", name, "' does not exist on type '", object_str, "'.");
    }
    // Why it is never (tsc's reportNonexistentProperty with elaborateNeverIntersection)
    unsigned never_code;
    cstr never_message;
    if (error && ty_never_intersection_elaboration(me, object, &never_code, &never_message)) {
        error_sub_error_s(error, (Span){0}, never_code, never_message);
    }
    // (Not of an enum: a primitive, tsc's reportNonexistentProperty)
    if (error && !destructured && object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN) && !ty_is_enum_union(object)) {
        // Which member it is that lacks it
        for (u32 i = 0; i < object->composite.types.len; i++) {
            Ty const* member = object->composite.types.data[i];
            if (member->flags & TYF_NULLABLE) continue;
            // (By its properties and index signatures, tsc's getPropertyOfType: not what an access of an object
            // literal would read, undefined)
            if (atom) {
                Ty const* apparent_member = ty_apparent_type(me, member);
                if (ty_find_prop(me, apparent_member, atom).found) continue;
                if ((apparent_member->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_applicable_index_info(me, apparent_member, index)) continue;
            } else if (ty_access(me, member, index)) {
                continue;
            }
            cstr member_str = ty_to_cstr(me, member);
            cstr message = cstr_null;
            cstr_append_d(&message, DEFAULT_DISPLAY, "Property '", name, "' does not exist on type '", member_str, "'.");
            error_sub_error_s(error, (Span){0}, TS_CODE_NONE, message);
            cstr_drop(&member_str);
            break;
        }
    }
    if (error && forgot_await) error_add_related_d(error, span, 2773, "Did you forget to use 'await'?");
    if (error && span_valid(declared_at)) error_add_related_d(error, declared_at, 2728, "'", atom_sv(suggestion->name), "' is declared here.");
    c_drop(cstr, &name, &object_str);
}

// TS2339 about `object` as it was before widening, the member lacking the property named (tsc's reportNonexistentProperty
// with the unwidened left type)
void ty_report_no_property(Checker* me, Ty const* object, Ty const* key, Span span, ErrorCtx* errs) {
    report_no_property(me, object, key, 2339, span, false, errs);
}

static bool is_global_this(Ty const* ty) {
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_NAMESPACE && ty->declared.namespace_symbol &&
        ty->declared.namespace_symbol->is_global_this;
}

// tsc's isJSLiteralType: without noImplicitAny, what a JavaScript object literal made has every property
bool ty_is_js_literal_type(Checker* me, Ty const* type) {
    if (compiler_options()->no_implicit_any) return false;
    switch (type->kind) {
        case TY_ANONYMOUS: return type->anonymous.js_literal;
        case TY_DECLARED: return type->declared.kind == TY_DECLARED_NAMESPACE && type->declared.js_container;
        case TY_UNION:
            for (u32 i = 0; i < type->composite.types.len; i++) {
                if (!ty_is_js_literal_type(me, type->composite.types.data[i])) return false;
            }
            return true;
        case TY_INTERSECTION:
            for (u32 i = 0; i < type->composite.types.len; i++) {
                if (ty_is_js_literal_type(me, type->composite.types.data[i])) return true;
            }
            return false;
        case TY_TYPE_PARAM: {
            Ty const* constraint = ty_param_constraint(me, type);
            return constraint && constraint != type && ty_is_js_literal_type(me, constraint);
        }
        default: return false;
    }
}

// An entity name as tsc's entityNameToString writes it: the names and dots, not the whitespace between them
static cstr entity_name_text(Span span) {
    cstr ret = cstr_null;
    for (isize i = 0; i < span.size; i++) {
        char c = span.buf[i];
        // (`a?.b` is `a.b`: tsc's entityNameToString)
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '?') cstr_append_n(&ret, &span.buf[i], 1);
    }
    return ret;
}

// A union whose members are all tuples without a rest element
static bool is_union_of_fixed_tuples(Ty const* ty) {
    if (ty->kind != TY_UNION || (ty->flags & TYF_BOOLEAN)) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        TupleShape const* shape = ty_tuple_shape(ty->composite.types.data[i]);
        if (!shape) return false;
        for (u32 j = 0; j < shape->arity; j++) {
            if (shape->flags[j] & TUPLE_ELEMENT_REST) return false;
        }
    }
    return true;
}

// What a type parameter's property access looks into: its apparent type, unless that can't say a property is missing
// (null: any, other type variables)
static Ty const* type_param_access_apparent(Checker* me, Ty const* param) {
    Ty const* apparent = ty_apparent_type(me, param);
    // (An apparent type that reduces to never has no properties either: `never[keyof T]`)
    if (apparent->flags & TYF_UNKNOWN) return apparent;
    if (ty_reduced(me, apparent)->flags & TYF_NEVER) return ty_intrinsic(me, TY_NEVER);
    // (Possibly null or undefined is checkNonNullExpression's business)
    if (apparent->flags & (TYF_ANY | TYF_NEVER | TYF_NULLABLE | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_ERROR)) return nullptr;
    if (apparent->kind == TY_UNION) {
        for (u32 i = 0; i < apparent->composite.types.len; i++) {
            if (apparent->composite.types.data[i]->flags & (TYF_ANY | TYF_UNKNOWN | TYF_NULLABLE | TYF_TYPE_PARAM | TYF_DEFERRED)) return nullptr;
        }
    }
    if (ty_is_js_literal_type(me, apparent) || ty_mapped_members_incomplete(me, apparent)) return nullptr;
    return apparent;
}

// A union with type parameter members has a property every member's apparent type has (tsc's getPropertyOfType of a
// union: getUnionOrIntersectionProperty, getReducedApparentType of each member). Null if a member can't say
// Whether a union member's apparent type is the error type: a type parameter constrained to it, a conditional type over
// one, or an intersection with one (tsc's getApparentType of an intersection intersects its members' apparent types,
// and any error type in an intersection makes it the error type)
bool ty_union_member_apparent_is_error(Checker* me, Ty const* member) {
    if (member->kind == TY_TYPE_PARAM) return !member->param.state->is_this && (ty_apparent_type(me, member)->flags & TYF_ERROR);
    // (A conditional type's is its distributive constraint's: `Awaited<T>` with T constrained to an unresolved type)
    if (member->kind == TY_CONDITIONAL) return (ty_apparent_type(me, member)->flags & TYF_ERROR) != 0;
    if (member->kind != TY_INTERSECTION) return false;
    for (u32 i = 0; i < member->composite.types.len; i++) {
        if (ty_union_member_apparent_is_error(me, member->composite.types.data[i])) return true;
    }
    return false;
}

// A union with type parameter members has a property every member's apparent type has (tsc's getPropertyOfType of a
// union: getUnionOrIntersectionProperty, getReducedApparentType of each member). Null if a member can't say. A member
// whose apparent type is the error type says nothing (createUnionOrIntersectionProperty skips it): `*none_left` when
// that's every member, which then has no property at all (`(E & {}) | (E & null)` with E constrained to an unresolved
// type, where either alone is the error type and silent)
static Ty const* union_apparent_members(Checker* me, Ty const* object, bool* none_left) {
    *none_left = false;
    u32 len = object->composite.types.len;
    Ty const* buf[64];
    if (len > c_arraylen(buf)) return nullptr;
    u32 kept = 0;
    for (u32 i = 0; i < len; i++) {
        Ty const* member = object->composite.types.data[i];
        if ((member->kind == TY_TYPE_PARAM || member->kind == TY_INTERSECTION || member->kind == TY_CONDITIONAL) &&
            ty_union_member_apparent_is_error(me, member)
        ) {
            continue;
        }
        if (member->flags & (TYF_ANY | TYF_DEFERRED)) return nullptr;
        if (member->kind == TY_TYPE_PARAM && !member->param.state->is_this) {
            member = type_param_access_apparent(me, member);
            if (!member) return nullptr;
        } else if (member->flags & TYF_TYPE_PARAM) {
            return nullptr;
        }
        buf[kept++] = member;
    }
    if (!kept) {
        *none_left = true;
        return nullptr;
    }
    return ty_union(me, buf, kept, nullptr, (TyList){0});
}

// TS18047/TS18048/TS18049 about a named object, TS2531/TS2532/TS2533 about another
static void report_possibly_nullish(Span object_span, bool object_is_named, bool has_null, bool has_undefined, ErrorCtx* errs) {
    const char* what = has_null && has_undefined ? "null' or 'undefined" : has_null ? "null" : "undefined";
    if (object_is_named) {
        cstr name = entity_name_text(object_span);
        error_ctx_error_d(errs, object_span, has_null && has_undefined ? 18049 : has_null ? 18047 : 18048,
            "'", name, "' is possibly '", what, "'.");
        cstr_drop(&name);
    } else {
        error_ctx_error_d(errs, object_span, has_null && has_undefined ? 2533 : has_null ? 2531 : 2532,
            "Object is possibly '", what, "'.");
    }
}

static void check_access(
    Checker* me, Ty const* object, Ty const* index, Span object_span, Span index_span, bool object_is_named,
    bool error_if_not_found, u32 depth, ErrorCtx* errs
) {
    if (depth > 8) return;
    if (object->flags & TYF_ERROR) return; // (tsc's checkElementAccessExpression: nothing to say about an error type)
    // An unknown object expression is TS18046/TS2571 whatever the index (tsc's checkNonNullExpression comes first)
    if ((object->flags & TYF_UNKNOWN) && compiler_options()->strict_null_checks && span_valid(object_span)) {
        if (object_is_named) {
            cstr name = entity_name_text(object_span);
            error_ctx_error_d(errs, object_span, 18046, "'", name, "' is of type 'unknown'.");
            cstr_drop(&name);
        } else {
            error_ctx_error_d(errs, object_span, 2571, "Object is of type 'unknown'.");
        }
        return;
    }
    // `string & Tag` is a string as far as indexing goes (tsc's isTypeAssignableToKind)
    u32 key_like = TYF_ANY | TYF_STRING_LIKE | TYF_NUMBER | TYF_ES_SYMBOL_LIKE | TYF_NUMBER_LITERAL | TYF_ENUM;
    if (index->kind == TY_UNION || (index->kind == TY_INTERSECTION && !(ty_key_flags(index) & key_like))) {
        // A generic member makes the whole index generic: the access is deferred and only checked against the
        // object's keys (TS2536), not member by member (tsc's getIndexedAccessTypeOrUndefined: isGenericIndexType)
        if (index->kind == TY_UNION) {
            for (u32 i = 0; i < index->composite.types.len; i++) {
                if (ty_is_generic_index(index->composite.types.data[i])) return;
            }
        }
        for (u32 i = 0; i < index->composite.types.len; i++) {
            check_access(
                me, object, index->composite.types.data[i], object_span, index_span, object_is_named,
                error_if_not_found, depth + 1, errs
            );
        }
        return;
    }
    if (index->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) return; // TODO: TS2536 against `keyof` of the object
    // never indexes anything, to never (tsc's getPropertyTypeForIndexType: it is assignable to every key kind)
    if (index->flags & TYF_NEVER) return;
    if (!(ty_key_flags(index) & key_like)) {
        cstr index_str = index_type_str(me, index, index_span);
        error_ctx_error_d(errs, index_span, 2538, "Type '", index_str, "' cannot be used as an index type.");
        cstr_drop(&index_str);
        return;
    }
    // A symbol that only a string index signature takes reads it, and is TS2538 (tsc's getPropertyTypeForIndexType with
    // an access node)
    if (!error_if_not_found && (ty_key_flags(index) & TYF_ES_SYMBOL_LIKE) &&
        ty_symbol_reads_string_index(me, ty_non_nullable(me, object), index)
    ) {
        cstr index_str = index_type_str(me, index, index_span);
        error_ctx_error_d(errs, index_span, 2538, "Type '", index_str, "' cannot be used as an index type.");
        cstr_drop(&index_str);
        return;
    }

    // A union of intersections that all reduce to never is never (tsc's getReducedApparentType): narrowing a union by a
    // predicate it has nothing in common with intersects each member with the candidate
    if (error_if_not_found && object->kind == TY_UNION && (ty_reduced(me, object)->flags & TYF_NEVER)) {
        report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
        return;
    }
    // An intersection that may be null or undefined, `T & ({} | null)` from narrowing an unconstrained T, is said to
    // be so (tsc's checkNonNullType goes by type facts, those of the base constraint `{} | null`), then the rest is
    // looked into
    if (object->kind == TY_INTERSECTION && compiler_options()->strict_null_checks && span_valid(object_span)) {
        bool has_null = ty_can_be_null(me, object), has_undefined = ty_can_be_undefined(me, object);
        if (has_null || has_undefined) {
            report_possibly_nullish(object_span, object_is_named, has_null, has_undefined, errs);
            Ty const* rest = ty_non_nullable(me, object);
            if (rest != object && !(rest->flags & (TYF_NEVER | TYF_NULLABLE))) {
                check_access(me, rest, index, object_span, index_span, object_is_named, error_if_not_found, depth + 1, errs);
            }
            return;
        }
    }
    if (object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) {
        // Possibly null/undefined is said once about the whole object, a missing property about the whole union
        bool has_null = false, has_undefined = false, opaque = false;
        for (u32 i = 0; i < object->composite.types.len; i++) {
            Ty const* member = object->composite.types.data[i];
            has_null |= (member->flags & TYF_NULL) != 0;
            has_undefined |= (member->flags & TYF_UNDEFINED) != 0;
            // (`T & null` from narrowing: by its type facts)
            if (member->kind == TY_INTERSECTION && compiler_options()->strict_null_checks) {
                has_null |= ty_can_be_null(me, member);
                has_undefined |= ty_can_be_undefined(me, member);
            }
            opaque |= (member->flags & (TYF_ANY | TYF_TYPE_PARAM | TYF_DEFERRED)) != 0 ||
                (member->kind == TY_INTERSECTION && ty_union_member_apparent_is_error(me, member));
        }
        // A destructured property (no object expression) is looked up in the union as it is, undefined included
        // (tsc's getBindingElementTypeFromParentType: getIndexedAccessType, no checkNonNullType)
        if ((has_null || has_undefined) && compiler_options()->strict_null_checks && !span_valid(object_span)) {
            // (null and undefined have no properties)
            if (error_if_not_found && !opaque) report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
            return;
        }
        if ((has_null || has_undefined) && compiler_options()->strict_null_checks) {
            report_possibly_nullish(object_span, object_is_named, has_null, has_undefined, errs);
            // (Then the rest is looked into: tsc's checkNonNullExpression gives the non-nullable type, which may lack
            // the property too)
            Ty const* rest = ty_non_nullable(me, object);
            if (rest != object && !(rest->flags & TYF_NEVER)) {
                check_access(me, rest, index, object_span, index_span, object_is_named, error_if_not_found, depth + 1, errs);
            }
            return;
        }
        // Past the end of every member of a union of tuples without rest elements (tsc's getPropertyTypeForIndexType:
        // everyType(objectType, isTupleType), with or without noImplicitAny)
        if (!opaque && is_union_of_fixed_tuples(object) && (index->flags & TYF_NUMBER_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) &&
            !ty_access(me, object, index)
        ) {
            // (No elaboration naming a member)
            cstr object_str = ty_to_cstr(me, object);
            error_ctx_error_d(errs, index_span, 2339, "Property '", &index->literal.value, "' does not exist on type '",
                object_str, "'.");
            cstr_drop(&object_str);
            return;
        }
        if (!error_if_not_found) return;
        Ty const* looked_into = object;
        if (opaque) {
            bool none_left;
            looked_into = union_apparent_members(me, object, &none_left);
            if (none_left) {
                report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
                return;
            }
            if (!looked_into) return;
        }
        if (!ty_access(me, looked_into, index)) report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
        return;
    }
    // `this` is checked as its apparent type, the class with this-argument `this` (tsc's isThisTypeParameter case of
    // reportNonexistentProperty). TODO: other type parameters
    if (object->kind == TY_TYPE_PARAM && object->param.state->is_this) object = ty_apparent_type(me, object);
    if ((object->kind == TY_TYPE_PARAM || object->kind == TY_INDEXED || object->kind == TY_CONDITIONAL ||
         object->kind == TY_SUBSTITUTION) && error_if_not_found) {
        // A type parameter (or `NoInfer<T>`) has what its apparent type has, and is named in the message (tsc's
        // checkPropertyAccessExpressionOrQualifiedName). Without a constraint (or `extends unknown`) that's unknown with
        // strictNullChecks: no properties at all, not even Object's
        Ty const* apparent = type_param_access_apparent(me, object);
        if (!apparent) return;
        if (apparent->flags & (TYF_UNKNOWN | TYF_NEVER)) {
            report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
            return;
        }
        if (!ty_access(me, apparent, index)) report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
        return;
    }
    if (object->flags & (TYF_ANY | TYF_TYPE_PARAM | TYF_DEFERRED)) return;
    if (object->flags & TYF_UNKNOWN) {
        // A destructured property (no object expression), or without strictNullChecks: what unknown lacks
        if (!compiler_options()->strict_null_checks) {
            // (Its apparent type is `{}`, which has Object's members: tsc's getApparentType)
            if (error_if_not_found && !ty_access(me, ty_empty_object_type(me), index)) {
                report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
            }
        } else if (!span_valid(object_span)) {
            if (error_if_not_found) report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
        }
        return;
    }
    bool is_empty_tuple = object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_TUPLE_TARGET;
    if (ty_is_tuple(object) || is_empty_tuple) {
        TupleShape const* shape = is_empty_tuple ? object->declared.tuple : object->reference.target->declared.tuple;
        bool has_rest = false;
        for (u32 i = 0; i < shape->arity; i++) has_rest |= (shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
        if ((index->flags & TYF_NUMBER_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) && !has_rest &&
            index->literal.value.number < 0
        ) {
            error_ctx_error_d(errs, index_span, 2514, "A tuple type cannot be indexed with a negative value.");
        } else if ((index->flags & TYF_NUMBER_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) && !has_rest &&
            index->literal.value.number >= shape->arity
        ) {
            cstr object_str = ty_to_cstr(me, object);
            error_ctx_error_d(errs, index_span, 2493,
                "Tuple type '", object_str, "' of length '", (i64)shape->arity,
                "' has no element at index '", &index->literal.value, "'.");
            cstr_drop(&object_str);
        } else if ((index->flags & TYF_STRING_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) && !has_rest &&
            ty_is_numeric_name(index->literal.string) && !ty_find_prop(me, object, index->literal.string).found
        ) {
            // A numeric name it lacks is an element past its end too (tsc's getPropertyTypeForIndexType:
            // isNumericLiteralName(propName))
            if (atom_sv(index->literal.string).buf[0] == '-') {
                error_ctx_error_d(errs, index_span, 2514, "A tuple type cannot be indexed with a negative value.");
            } else {
                cstr object_str = ty_to_cstr(me, object);
                error_ctx_error_d(errs, index_span, 2493, "Tuple type '", object_str, "' of length '", (i64)shape->arity,
                    "' has no element at index '", atom_sv(index->literal.string), "'.");
                cstr_drop(&object_str);
            }
            return;
        }
        if (!(index->flags & TYF_STRING_LITERAL)) return;
    } else if (ty_is_array(me, object) && !(index->flags & TYF_STRING_LITERAL) &&
        (!compiler_options()->no_lib || ty_applicable_index_info(me, object, ty_intrinsic(me, TY_NUMBER)))
    ) {
        return; // (Unless a noLib Array declares no number index signature, see access())
    }

    if (!error_if_not_found) return;
    // `never` has no properties (tsc: only the silent never of inference passes is any-like)
    if (ty_reduced(me, object)->flags & TYF_NEVER) {
        report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
        return;
    }
    // An object literal of a JavaScript file is open-ended (tsc's isJSLiteralType)
    if (ty_is_js_literal_type(me, object)) return;
    if (ty_mapped_members_incomplete(me, object)) return; // HACK: see MembersRare.index_signatures_incomplete
    if (object->flags & TYF_NULLABLE) {
        // A destructured property of null or undefined doesn't exist (a property access is TS18050 in tsc: TODO)
        if (!span_valid(object_span)) report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
        return;
    }
    if (ty_access(me, object, index)) return;
    // What `typeof globalThis` doesn't have is any, but a block scoped global isn't there (tsc's
    // checkPropertyAccessExpressionOrQualifiedName). Not destructured: tsc's getPropertyTypeForIndexType only makes an
    // element access any, a binding element's missing property is TS2339
    if (is_global_this(object) && span_valid(object_span)) {
        Atom const* name = ty_literal_name(me, index);
        Symbol const* global = name ? symbol_get_export(object->declared.namespace_symbol, atom_sv(name)) : nullptr;
        if (global && (global->flags & GLOBAL_BLOCK_SCOPED)) {
            // (A plain error, no suggestion: tsc's `error(right, Diagnostics.Property_0_does_not_exist_on_type_1)`)
            if (span_valid(object_span)) {
                cstr prop_name = index_name(me, index), object_str = ty_to_cstr(me, object);
                error_ctx_error_d(errs, index_span, 2339, "Property '", prop_name, "' does not exist on type '", object_str, "'.");
                c_drop(cstr, &prop_name, &object_str);
            } else {
                report_no_property(me, object, index, 2339, index_span, true, errs);
            }
        } else if (compiler_options()->no_implicit_any) {
            error_ctx_error_d(errs, index_span, 7017,
                "Element implicitly has an 'any' type because type 'typeof globalThis' has no index signature.");
        }
        return;
    }
    report_no_property(me, object, index, 2339, index_span, !span_valid(object_span), errs);
}

void ty_check_property_access(
    Checker* me, Ty const* object, Ty const* index, Span object_span, Span index_span, bool object_is_named,
    bool error_if_not_found, ErrorCtx* errs
) {
    check_access(me, object, index, object_span, index_span, object_is_named, error_if_not_found, 0, errs);
}

// One member of the index of a non-generic `O[K]` type (tsc's getPropertyTypeForIndexType with an access node)
static void check_indexed_access_part(Checker* me, Ty const* apparent, Ty const* index, Span index_span, ErrorCtx* errs) {
    if (index->flags & TYF_NEVER) return;
    // What can't be a key is an error even on any (tsc's isTypeAssignableToKind(StringLike | NumberLike | ESSymbolLike),
    // any included, before the object is looked at)
    u32 key_like = TYF_ANY | TYF_STRING_LIKE | TYF_NUMBER | TYF_ES_SYMBOL_LIKE | TYF_NUMBER_LITERAL | TYF_ENUM;
    bool is_key = !(index->flags & TYF_NULLABLE) && (ty_key_flags(index) & key_like);
    if (is_key && (apparent->flags & (TYF_ANY | TYF_NEVER))) return;
    if (is_key && (index->flags & TYF_ANY)) {
        // Any indexes what has an index signature (any is assignable to every key type), else it's no key at all
        // (tsc's getPropertyTypeForIndexType falls through to TS2538)
        if (!(apparent->flags & TYF_OBJECT) || ty_applicable_index_info(me, apparent, index)) return;
        is_key = false;
    }
    // (A negative element of a tuple without a rest element isn't its number index signature's: tsc's
    // getPropertyTypeForIndexType)
    if (ty_is_tuple(apparent) && (index->flags & TYF_NUMBER_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) &&
        index->literal.value.number < 0
    ) {
        TupleShape const* shape = apparent->reference.target->declared.tuple;
        bool variable = false;
        for (u32 i = 0; i < shape->arity; i++) variable |= (shape->flags[i] & TUPLE_ELEMENT_REST) != 0;
        if (!variable) {
            error_ctx_error_d(errs, index_span, 2514, "A tuple type cannot be indexed with a negative value.");
            return;
        }
    }
    // (A symbol that only a string index signature takes reads it, TS2538: tsc's getPropertyTypeForIndexType)
    if (is_key && ty_symbol_reads_string_index(me, apparent, index)) is_key = false;
    // (ty_access() reads past a union's null and undefined, which have no properties or index signatures: `X[] |
    // undefined` indexed by number is TS2537, tsc's getPropertyTypeForIndexType on the union)
    bool nullable_member = false;
    for (u32 i = 0; apparent->kind == TY_UNION && i < apparent->composite.types.len; i++) {
        nullable_member |= (apparent->composite.types.data[i]->flags & TYF_NULLABLE) != 0;
    }
    if (is_key && !nullable_member && ty_access(me, apparent, index)) return;
    // A numeric name of tuples is their element (tsc's isNumericLiteralName(propName)): `T["1"]` as `T[1]`
    if ((index->flags & TYF_STRING_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) && index->literal.string &&
        ty_is_numeric_name(index->literal.string)
    ) {
        bool tuples = ty_is_tuple(apparent);
        if (apparent->kind == TY_UNION) {
            tuples = true;
            for (u32 i = 0; i < apparent->composite.types.len && tuples; i++) tuples = ty_is_tuple(apparent->composite.types.data[i]);
        }
        if (tuples) {
            csview digits = atom_sv(index->literal.string);
            char buf[64];
            snprintf(buf, sizeof buf, "%.*s", (int)digits.size, digits.buf);
            Value value = value_number(atof(buf));
            check_indexed_access_part(me, apparent, ty_literal(me, &value), index_span, errs);
            return;
        }
    }
    if (ty_is_tuple(apparent) && (index->flags & TYF_NUMBER_LITERAL)) {
        // (ty_access() only misses an element past the end of a tuple without a rest element)
        double n = index->literal.value.number;
        if (n < 0) {
            error_ctx_error_d(errs, index_span, 2514, "A tuple type cannot be indexed with a negative value.");
        } else {
            cstr object_str = ty_to_cstr(me, apparent);
            error_ctx_error_d(errs, index_span, 2493, "Tuple type '", object_str, "' of length '",
                (i64)apparent->reference.target->declared.tuple->arity, "' has no element at index '", &index->literal.value, "'.");
            cstr_drop(&object_str);
        }
        return;
    }
    if (is_global_this(apparent) && !(index->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL))) return;
    cstr object_str = ty_to_cstr(me, apparent);
    if (!is_key) {
        cstr index_str = index_type_str(me, index, index_span);
        error_ctx_error_d(errs, index_span, 2538, "Type '", index_str, "' cannot be used as an index type.");
        cstr_drop(&index_str);
    } else if (index->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) {
        cstr name = cstr_null;
        cstr_append_value_js(&name, &index->literal.value);
        if (index->literal.string) {
            cstr_drop(&name);
            name = cstr_from_sv(atom_sv(index->literal.string));
        }
        error_ctx_error_d(errs, index_span, 2339, "Property '", name, "' does not exist on type '", object_str, "'.");
        cstr_drop(&name);
    } else if (index->flags & (TYF_STRING | TYF_NUMBER)) {
        cstr index_str = ty_to_cstr(me, index);
        error_ctx_error_d(errs, index_span, 2537, "Type '", object_str, "' has no matching index signature for type '",
            index_str, "'.");
        cstr_drop(&index_str);
    } else {
        cstr index_str = index_type_str(me, index, index_span);
        error_ctx_error_d(errs, index_span, 2538, "Type '", index_str, "' cannot be used as an index type.");
        cstr_drop(&index_str);
    }
    cstr_drop(&object_str);
}

Ty const* ty_instance_type_of_class(Checker* me, Symbol const* class_symbol) {
    Ty const* declared = ty_declared_type_of(me, class_symbol);
    if (declared->kind == TY_DECLARED && declared->declared.type_params.len) {
        return ty_reference(me, declared, declared->declared.type_params);
    }
    return declared;
}

// tsc's checkInferType: an `infer X` declared more than once in an extends type has the same constraint everywhere
// it has one (TS2838 at every declaration)
void ty_check_infer_constraints(Checker* me, InferredType const* inferred, ErrorCtx* errs) {
    Symbol const* symbol = inferred->owned_symbol;
    if (!symbol || symbol->decl_count < 2) return;
    Ty const* constraint = ty_param_constraint(me, ty_type_param(me, symbol));
    if (!constraint) return;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_INFER) continue;
        InferredType const* other = symbol->decls[i].node;
        if (other->extends.tag == TYPE_ANY) continue;
        Ty const* other_constraint = ty_from_node(me, &other->extends, (LowerCtx){ .generic = true });
        if (ty_is_related(me, other_constraint, constraint, TY_RELATION_IDENTITY)) continue;
        error_ctx_error_d(errs, inferred->name, 2838, "All declarations of '", span_sv(inferred->name),
            "' must have identical constraints.");
        return;
    }
}

// tsc's checkIndexedAccessIndexType: every member of the index of a deferred `O[K]` is a key of O (`keyof O`), or a
// number when O has a number index signature
bool ty_indexed_access_index_ok(Checker* me, Ty const* indexed) {
    Ty const* object = indexed->indexed.object;
    Ty const* index = indexed->indexed.index;
    Ty const* keys = ty_keyof(me, object);
    // (A substitution type in a true branch is its base and constraint for this: `RF[N]` with `RF[N] extends A[]`)
    Ty const* apparent = ty_apparent_type(me, object->kind == TY_SUBSTITUTION ? ty_substitution_intersection(me, object) : object);
    bool number_index = false;
    // (A union has the index signatures every member has: tsc's resolveUnionTypeMembers)
    TyList members = apparent->kind == TY_UNION && !(apparent->flags & TYF_BOOLEAN) ? apparent->composite.types :
        (TyList){ .data = &apparent, .len = 1 };
    number_index = members.len > 0;
    for (u32 i = 0; i < members.len && number_index; i++) {
        Ty const* member = members.data[i];
        IndexInfo const* info = member->flags & (TYF_OBJECT | TYF_INTERSECTION) ?
            ty_applicable_index_info(me, member, ty_intrinsic(me, TY_NUMBER)) : nullptr;
        number_index = info && (info->key->flags & TYF_NUMBER);
    }
    Ty const* const* parts = &index;
    u32 count = 1;
    if (index->kind == TY_UNION && !(index->flags & TYF_BOOLEAN)) {
        parts = index->composite.types.data;
        count = index->composite.types.len;
    }
    for (u32 i = 0; i < count; i++) {
        if (ty_is_related(me, parts[i], keys, TY_RELATION_ASSIGNABLE)) continue;
        // (tsc's isApplicableIndexType: numbers, `${number}` and numeric string literals)
        if (number_index && (ty_is_related(me, parts[i], ty_intrinsic(me, TY_NUMBER), TY_RELATION_ASSIGNABLE) ||
            ty_is_numeric_string_type(me, parts[i]) || ((parts[i]->flags & TYF_STRING_LITERAL) &&
            parts[i]->literal.string && ty_is_numeric_name(parts[i]->literal.string)))
        ) {
            continue;
        }
        return false;
    }
    return true;
}

static IndexInfo const* generic_write_index_info(Checker* me, PropertyExpr const* access, Ty const* object, Ty const* key);

// Written to, a generic object's index signatures (its constraint's) don't apply: tsc's checkPropertyAccessExpression-
// OrQualifiedName takes no index info for an assignment to a generic object, checkElementAccess's
// AccessFlags.NoIndexSignatures leaves only a number one. The index signature that would have been used, else null
IndexInfo const* ty_generic_write_index_info(Checker* me, PropertyExpr const* access, Ty const* object, Ty const* key) {
    if (!access->assignment_target || property_private_class(access)) return nullptr;
    if (object->kind == TY_TYPE_PARAM ? object->param.state->is_this : !ty_is_generic_object(me, object)) return nullptr;
    key = ty_regular(me, key);
    // (A generic index, `string | keyof T` included, defers the whole access: tsc's getIndexedAccessTypeOrUndefined)
    if (ty_is_generic_index(key)) return nullptr;
    if (key->kind == TY_UNION && !(key->flags & TYF_BOOLEAN) && access->form == PROPERTY_EXPR_BRACKET) {
        // (tsc's getIndexedAccessType asks every constituent: the first one that would use an index signature)
        for (u32 i = 0; i < key->composite.types.len; i++) {
            IndexInfo const* info = generic_write_index_info(me, access, object, key->composite.types.data[i]);
            if (info) return info;
        }
        return nullptr;
    }
    return generic_write_index_info(me, access, object, key);
}

static IndexInfo const* generic_write_index_info(Checker* me, PropertyExpr const* access, Ty const* object, Ty const* key) {
    if (ty_is_generic_index(key) || (key->flags & (TYF_ANY | TYF_NEVER | TYF_NULLABLE)) || key->kind == TY_UNION) return nullptr;
    Ty const* apparent = ty_apparent_type(me, object);
    if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return nullptr;
    Atom const* name = ty_literal_name(me, key);
    if (access->form == PROPERTY_EXPR_DOT && !name) return nullptr;
    if (name && ty_find_prop(me, apparent, name).found) return nullptr;
    IndexInfo const* info = ty_applicable_index_info(me, apparent, key);
    if (access->form == PROPERTY_EXPR_DOT) return info;
    if (!info) {
        Members const* members = ty_members(me, apparent);
        for (u32 i = 0; i < members->index_count && !info; i++) {
            if (members->index[i].key == ty_intrinsic(me, TY_STRING)) info = &members->index[i];
        }
    }
    return info && info->key != ty_intrinsic(me, TY_NUMBER) ? info : nullptr;
}

void ty_check_generic_index_write(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs) {
    Ty const* key = ty_access_key_type(me, access);
    if (!ty_generic_write_index_info(me, access, object, key)) return;
    if (access->form == PROPERTY_EXPR_DOT) {
        report_no_property(me, object, ty_regular(me, key), 2339, property_key_span(access), false, errs);
        return;
    }
    cstr object_str = ty_to_cstr(me, object);
    error_ctx_error_d(errs, span, 2862, "Type '", object_str, "' is generic and can only be indexed for reading.");
    cstr_drop(&object_str);
}

// tsc's checkIndexedAccessIndexType for an index a generic object rejects: a private or protected member is TS4105
// rather than TS2536 (the first member of the apparent type that has it decides)
static void report_invalid_generic_index(Checker* me, Ty const* object, Ty const* index, Span span, ErrorCtx* errs) {
    if (ty_is_generic_object(me, object) && (index->flags & TYF_STRING_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) && index->literal.string) {
        Ty const* apparent = ty_apparent_type(me, object);
        TyList parts = apparent->kind == TY_UNION ? apparent->composite.types : (TyList){ .data = &apparent, .len = 1 };
        Prop const* prop = nullptr;
        // (Each member by its apparent type: tsc's getPropertyOfType of a type parameter in the union)
        for (u32 i = 0; i < parts.len && !prop; i++) prop = ty_property_of_type(me, ty_apparent_type(me, parts.data[i]), index->literal.string);
        if (prop && (prop->flags & (PROP_PRIVATE | PROP_PROTECTED))) {
            error_ctx_error_d(errs, span, 4105, "Private or protected member '", atom_sv(index->literal.string),
                "' cannot be accessed on a type parameter.");
            return;
        }
    }
    cstr index_str = ty_to_cstr(me, index), object_str = ty_to_cstr(me, object);
    error_ctx_error_d(errs, span, 2536, "Type '", index_str, "' cannot be used to index type '", object_str, "'.");
    c_drop(cstr, &index_str, &object_str);
}

void ty_check_generic_element_access(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs) {
    Ty const* invalid = ty_invalid_generic_element_access(me, access, object);
    if (!invalid) return;
    report_invalid_generic_index(me, invalid->indexed.object, invalid->indexed.index, span, errs);
}

void ty_check_indexed_access_type(Checker* me, Ty const* object, Ty const* index, Span index_span, Span span, ErrorCtx* errs) {
    index = ty_regular(me, index);
    // (An error object is any: indexing it with something that can't be a key is still TS2538, tsc's
    // getPropertyTypeForIndexType, which check_indexed_access_part() follows)
    // (An error index of a generic object is deferred like any other, and any fits every key: nothing to say)
    if ((index->flags & TYF_ERROR) && ty_is_generic_object(me, object)) return;
    // (A union or intersection an instantiation may reduce is generic too: tsc's isGenericObjectType has
    // isGenericReducibleType, `(P & { dataType: K })[42]` with P a union discriminated by dataType)
    if (!(index->flags & TYF_ERROR) && (ty_is_generic_index(index) || ty_is_generic_object(me, object) ||
        ((object->kind == TY_UNION || object->kind == TY_INTERSECTION) && ty_is_generic_reducible(me, object)))
    ) {
        // tsc's checkIndexedAccessIndexType on the deferred type, reported on the whole `O[K]`
        Ty const* indexed = ty_indexed(me, object, index);
        if (indexed->kind != TY_INDEXED || ty_indexed_access_index_ok(me, indexed)) return;
        report_invalid_generic_index(me, object, index, span, errs);
        return;
    }
    Ty const* apparent = (object->flags & (TYF_ANY | TYF_NEVER)) ? object : ty_apparent_type(me, object);
    if (index->kind == TY_UNION && !(index->flags & TYF_BOOLEAN)) {
        for (u32 i = 0; i < index->composite.types.len; i++) {
            check_indexed_access_part(me, apparent, index->composite.types.data[i], index_span, errs);
        }
    } else {
        check_indexed_access_part(me, apparent, index, index_span, errs);
    }
}

// `{ [k]: x } = o` and `const { [k]: x } = o`: getIndexedAccessType with the computed name as the access node, an
// expression rather than an `O[K]` type, so only a generic key defers it; else it's about o's apparent type
void ty_check_destructuring_indexed_access(Checker* me, Ty const* object, Ty const* index, Span index_span, ErrorCtx* errs) {
    index = ty_regular(me, index);
    if ((object->flags & TYF_ERROR) || ty_is_generic_index(index)) return;
    if (!(index->flags & TYF_ERROR) && ty_is_generic_object(me, object)) {
        object = ty_apparent_type(me, object);
        if (ty_is_generic_object(me, object)) return;
    }
    ty_check_indexed_access_type(me, object, index, index_span, index_span, errs);
}

// tsc's tryGetPropertyAccessOrIdentifierToString: `a.b[0]["c"]` as `a.b.0.c` (a bracket access whose key is a property
// name: an identifier key too, `a[k]` as `a.k`), appended to `out`; false (nothing appended) for anything else
static bool access_or_identifier_string(Expr const* expr, cstr* out) {
    if (expr_is_perenthisized(expr)) return false;
    if (expr->tag == EXPR_IDENTIFIER) {
        if (expr->identifier.keyword) return false;
        cstr_append_sv(out, name_cooked(span_sv(identifier_expr_name(&expr->identifier))));
        return true;
    }
    if (expr->tag != EXPR_PROPERTY) return false;
    PropertyExpr const* property = expr->property;
    if (property->form == PROPERTY_EXPR_DOT) {
        if (!access_or_identifier_string(&property->expr, out)) return false;
        cstr_append(out, ".");
        cstr_append_sv(out, span_sv(property_name(property)));
        return true;
    }
    Expr const* key = &property->index;
    bool name = !expr_is_perenthisized(key) && ((key->tag == EXPR_IDENTIFIER && !key->identifier.keyword) ||
        (key->tag == EXPR_LITERAL && (key->literal.tag == VALUE_STRING || key->literal.tag == VALUE_NUMBER)));
    if (!name || !access_or_identifier_string(&property->expr, out)) return false;
    cstr_append(out, ".");
    if (key->tag == EXPR_IDENTIFIER) {
        cstr_append_sv(out, name_cooked(span_sv(identifier_expr_name(&key->identifier))));
    } else {
        cstr_append_value_js(out, &key->literal);
    }
    return true;
}

// tsc's getSuggestionForNonexistentIndexSignature: `o[k]` on something with a `get(k)` (`set(k, v)` when written)
// taking k, as `o.get` (the method's name alone unless o is an entity name)
static bool suggested_index_method(Checker* me, PropertyExpr const* access, Ty const* object, Ty const* index, cstr* out) {
    char const* method = access->assignment_target ? "set" : "get";
    if (object->kind == TY_UNION || object->kind == TY_INTERSECTION) return false; // (getPropertyOfObjectType)
    FoundProp prop = ty_find_prop(me, object, checker_atom(me, csview_from(method)));
    if (!prop.found) return false;
    Signature const* sig = ty_single_signature(me, ty_apparent_type(me, prop.type), false, true);
    if (sig) signature_resolve(me, sig);
    if (!sig || sig->min_args < 1 || !ty_is_related(me, index, ty_signature_param_type_at(me, sig, 0), TY_RELATION_ASSIGNABLE)) {
        return false;
    }
    *out = cstr_null;
    if (access_or_identifier_string(&access->expr, out)) cstr_append(out, ".");
    cstr_append(out, method);
    return true;
}

// The first member of the index `object` has nothing for (tsc reports that one and suppresses the others), null if
// there's none
static Ty const* missing_index_member(Checker* me, Ty const* object, Ty const* index) {
    // `any` fits any index signature (and finds no property)
    if (index->flags & TYF_ANY) {
        // (A union's index signatures are those all of its members have: tsc's resolveUnionTypeMembers)
        if (object->kind == TY_UNION) return ty_union_any_index_type(me, object, index, TYF_OBJECT | TYF_INTERSECTION) ? nullptr : index;
        return ty_applicable_index_info(me, object, index) ? nullptr : index;
    }
    if (index->kind != TY_UNION || (index->flags & TYF_BOOLEAN)) return ty_access_element(me, object, index, false) ? nullptr : index;
    for (u32 i = 0; i < index->composite.types.len; i++) {
        Ty const* member = index->composite.types.data[i];
        if (!ty_access_element(me, object, member, false)) return member;
    }
    return nullptr;
}

// tsc's getFullyQualifiedName with a containing location: a file module by its specifier from `from` (`"./a".N.x`)
static cstr qualified_name_from(Checker* me, Symbol const* symbol, SourceFile const* from) {
    if (symbol->is_module && symbol->module && symbol->module->file && from) {
        cstr specifier = program_module_specifier(my program, from, symbol->module->file, SPECIFIER_MODE_NONE);
        cstr ret = cstr_d("\"", specifier, "\"");
        cstr_drop(&specifier);
        return ret;
    }
    Symbol const* parent = symbol->parent;
    if (!parent || (parent->is_module && (!parent->module || !parent->exports ||
        symbol_table_get(parent->exports, symbol->name) != symbol))
    ) {
        return symbol_fully_qualified_name(symbol);
    }
    cstr ret = qualified_name_from(me, parent, from);
    cstr_append_d(&ret, DEFAULT_DISPLAY, ".", symbol->name);
    return ret;
}

// The noImplicitAny errors of tsc's getPropertyTypeForIndexType for an element access `o[k]` that finds nothing:
// TS7053 (with what is missing), TS7015 (a number index signature and a key that isn't a number), TS7052 (a `get`
// method to call instead), TS2551, TS2576, and TS2339 on an object literal expression
void ty_check_element_implicit_any(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs) {
    if (access->form != PROPERTY_EXPR_BRACKET) return;
    // (unknown was TS18046/TS2571, then the error type: tsc's checkNonNullExpression)
    if ((object->flags & TYF_UNKNOWN) && compiler_options()->strict_null_checks) return;
    if (is_global_this(object)) {
        // `globalThis["x"]` for a block scoped x (tsc's getPropertyTypeForIndexType), with or without noImplicitAny
        Ty const* key = ty_regular(me, ty_access_key_type(me, access));
        Atom const* name = ty_literal_name(me, key);
        Symbol const* global = name ? symbol_get_export(object->declared.namespace_symbol, atom_sv(name)) : nullptr;
        if (global && (global->flags & GLOBAL_BLOCK_SCOPED)) {
            report_no_property(me, object, key, 2339, span, false, errs);
            return;
        }
    }
    // A const enum's object has no index signature to fall back on, with or without noImplicitAny (tsc's
    // getPropertyTypeForIndexType: isConstEnumObjectType)
    if (object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_ENUM_OBJECT && object->declared.symbol &&
        (object->declared.symbol->flags & SYMBOL_CONST_ENUM)
    ) {
        // (tsc's checkElementAccess: isStringLiteralLike, a string literal or a template without substitutions)
        Expr const* index = &access->index;
        if (index->tag != EXPR_LITERAL || index->literal.tag != VALUE_STRING) {
            error_ctx_error_d(errs, index->inner_span, 2476, "A const enum member can only be accessed using a string literal.");
            return;
        }
        Ty const* key = ty_regular(me, ty_access_key_type(me, access));
        if ((key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) && !(key->flags & TYF_ENUM_LITERAL) &&
            !ty_access(me, object, key)
        ) {
            report_no_property(me, object, key, 2339, property_key_span(access), false, errs);
        }
        return;
    }
    if (!compiler_options()->no_implicit_any) return;
    Span index_span = property_key_span(access);
    Ty const* full_index = ty_regular(me, ty_access_key_type(me, access));
    u32 key_like = TYF_STRING_LIKE | TYF_NUMBER | TYF_ES_SYMBOL_LIKE | TYF_NUMBER_LITERAL | TYF_ENUM;
    // (`any` is a key like any other: tsc's isTypeAssignableToKind)
    if ((full_index->flags &
        (TYF_NEVER | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_NULLABLE | TYF_COULD_CONTAIN_TYPE_VARIABLES))) {
        return;
    }
    if (full_index->flags & TYF_ANY) {
        // Found by any index signature
    } else if (full_index->kind == TY_UNION && !(full_index->flags & TYF_BOOLEAN)) {
        for (u32 i = 0; i < full_index->composite.types.len; i++) {
            if (!(ty_key_flags(full_index->composite.types.data[i]) & key_like)) return; // TS2538's business
        }
    } else if (!(ty_key_flags(full_index) & key_like)) {
        return;
    }
    // (Written to, the object is widened: no longer an object literal. tsc's isObjectLiteralType)
    bool object_literal = ty_is_fresh_object(object) && !access->assignment_target;
    // The rest of what check_access() lets through or reports itself
    // (Only what may be null or undefined loses it, tsc's checkNonNullType: a generic whose constraint is any stays itself)
    object = ty_non_nullable_if_needed(me, object);
    // Without strictNullChecks unknown is apparently `{}` (tsc's getApparentType), and so is an unconstrained
    // type parameter
    bool sloppy_unknown = !compiler_options()->strict_null_checks &&
        ((object->flags & TYF_UNKNOWN) || (object->kind == TY_TYPE_PARAM && !ty_param_constraint(me, object)));
    if (sloppy_unknown) object = ty_empty_object_type(me);
    if (object->flags & (TYF_ANY | TYF_NEVER | TYF_UNKNOWN | TYF_DEFERRED)) return;
    if (object->kind == TY_TYPE_PARAM && !ty_param_constraint(me, object)) {
        object = ty_intrinsic(me, TY_UNKNOWN_);
    } else {
        // (An intersection's apparent type is made again from its members, without the alias that named it, when one of
        // them changes: tsc's getApparentTypeOfIntersectionType, `Loaded<User>` printed `User & { ... }`)
        object = ty_intersection_apparent_unaliased(me, ty_apparent_type(me, object));
    }
    Ty const* index = (object->flags & TYF_UNKNOWN) ? full_index : missing_index_member(me, object, full_index);
    if (!index) return;
    if (!(object->flags & TYF_UNKNOWN)) {
        if (object->flags & (TYF_ANY | TYF_NEVER | TYF_UNKNOWN | TYF_DEFERRED | TYF_TYPE_PARAM)) return;
        // (void has no apparent type of its own: `f()["x"]` of a void function, tsc's getPropertyTypeForIndexType)
        if (!(object->flags & (TYF_OBJECT | TYF_INTERSECTION | TYF_UNION | TYF_VOID))) return;
        if (object->kind == TY_UNION) {
            // (A primitive member by its apparent type: `number | C`, tsc's getPropertyOfType of the union)
            u32 primitive = TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
                TYF_BOOLEAN_LITERAL | TYF_ENUM | TYF_ENUM_LITERAL | TYF_ES_SYMBOL_LIKE;
            for (u32 i = 0; i < object->composite.types.len; i++) {
                // (`object` too, apparently `{}`: `([] | object)[k]`)
                if (!(object->composite.types.data[i]->flags & (TYF_OBJECT | TYF_INTERSECTION | TYF_NON_PRIMITIVE | primitive))) return;
            }
        }
        if ((ty_tuple_shape(object) || is_union_of_fixed_tuples(object)) && (index->flags & TYF_NUMBER_LITERAL)) return; // TS2493, TS2339
        if (ty_mapped_members_incomplete(me, object)) return; // HACK: see MembersRare.index_signatures_incomplete
        if (object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_ENUM_OBJECT && object->declared.symbol &&
            (object->declared.symbol->flags & SYMBOL_CONST_ENUM)
        ) {
            return;
        }
    }
    cstr object_str = ty_to_cstr(me, object);
    Atom const* name = (index->flags & TYF_STRING_LITERAL) && !(index->flags & TYF_ENUM_LITERAL) ? index->literal.string : nullptr;
    Prop const* suggestion = nullptr;
    cstr method = cstr_null;
    Symbol const* class_symbol = instance_class(object);
    if (object_literal) {
        if (index->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) {
            cstr index_str = index_name(me, index);
            error_ctx_error_d(errs, span, 2339, "Property '", index_str, "' does not exist on type '", object_str, "'.");
            cstr_drop(&index_str);
            cstr_drop(&object_str);
            return;
        }
        if (index->flags & (TYF_STRING | TYF_NUMBER)) {
            cstr_drop(&object_str);
            return;
        }
    }
    // (tsc's getIndexTypeOfType(objectType, numberType): an intersection has what any of its members has, a union what
    // all of them have)
    bool number_index = object->kind == TY_UNION;
    TyList parts = object->kind == TY_INTERSECTION || object->kind == TY_UNION ? object->composite.types :
        (TyList){ .data = &object, .len = 1 };
    for (u32 j = 0; j < parts.len; j++) {
        Ty const* part = parts.len > 1 ? ty_apparent_type(me, parts.data[j]) : parts.data[j];
        bool has = false;
        if (part->flags & TYF_OBJECT) {
            Members const* members = ty_members(me, part);
            for (u32 i = 0; i < members->index_count; i++) has |= (members->index[i].key->flags & TYF_NUMBER) != 0;
        }
        number_index = object->kind == TY_UNION ? number_index && has : number_index || has;
    }
    if (name && class_symbol && !csview_equals(atom_sv(name), "prototype") &&
        ty_get_prop(me, ty_static_type_of(me, class_symbol), name)
    ) {
        error_ctx_error_d(errs, span, 2576, "Property '", atom_sv(name), "' does not exist on type '", object_str,
            "'. Did you mean to access the static member '", object_str, "[", csview_trim(span_sv(property_key_inner_span(access))),
            "]' instead?");
    } else if (number_index) {
        error_ctx_error_d(errs, index_span, 7015,
            "Element implicitly has an 'any' type because index expression is not of type 'number'.");
    } else if (name && !(object->flags & TYF_UNKNOWN) && (suggestion = ty_suggested_property(me, object, atom_sv(name)))) {
        error_ctx_error_d(errs, index_span, 2551, "Property '", atom_sv(name), "' does not exist on type '", object_str,
            "'. Did you mean '", atom_sv(suggestion->name), "'?");
    } else if (!(object->flags & TYF_UNKNOWN) && suggested_index_method(me, access, object, index, &method)) {
        error_ctx_error_d(errs, span, 7052, "Element implicitly has an 'any' type because type '", object_str,
            "' has no index signature. Did you mean to call '", method, "'?");
    } else {
        // (The key as the expression has it, fresh: a lone enum member prints as `E.x`, not its enum. tsc's
        // getPropertyTypeForIndexType gets checkExpression's type)
        Ty const* fresh_index = ty_access_key_type(me, access);
        if (ty_regular(me, fresh_index) != full_index) fresh_index = full_index;
        cstr full_str = index_type_str(me, fresh_index, span_null);
        Error* error = error_ctx_error_d(errs, span, 7053, "Element implicitly has an 'any' type because expression of type '",
            full_str, "' can't be used to index type '", object_str, "'.");
        cstr index_str = index_name(me, index == full_index ? fresh_index : index);
        if (!error) {
        } else if (index->flags & TYF_UNIQUE_ES_SYMBOL) {
            // (tsc's getFullyQualifiedName of the symbol's symbol: its containers, a module as its quoted name)
            UniqueSymbolDecl const* unique = index->literal.unique;
            if (unique && unique->owner) {
                // (A property's, `readonly isConcatSpreadable: unique symbol`, by its container: SymbolConstructor.x)
                cstr qualified = qualified_name_from(me, unique->owner, span_source(span));
                if (span_valid(unique->member)) cstr_append_d(&qualified, DEFAULT_DISPLAY, ".", span_sv(unique->member));
                cstr_drop(&index_str);
                index_str = cstr_d("[", qualified, "]");
                cstr_drop(&qualified);
            }
            // (index_name() has the brackets)
            error_sub_error_d(error, (Span){0}, TS_CODE_NONE, "Property '", index_str, "' does not exist on type '",
                object_str, "'.");
        } else if (index->flags & TYF_ENUM_LITERAL) {
            error_sub_error_d(error, (Span){0}, TS_CODE_NONE, "Property '[", index_str, "]' does not exist on type '",
                object_str, "'.");
        } else if (index->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) {
            error_sub_error_d(error, (Span){0}, TS_CODE_NONE, "Property '", index_str, "' does not exist on type '",
                object_str, "'.");
        } else if (index->flags & (TYF_STRING | TYF_NUMBER)) {
            error_sub_error_d(error, (Span){0}, TS_CODE_NONE, "No index signature with a parameter of type '", index_str,
                "' was found on type '", object_str, "'.");
        }
        c_drop(cstr, &full_str, &index_str);
    }
    c_drop(cstr, &object_str, &method);
}

// tsc's checkTypeArgumentConstraints for a type reference `T<A, B>` to a generic class, interface or alias: each
// argument against its parameter's constraint, instantiated with the arguments (defaults for the ones not given).
// TS2344, the first one only
void ty_check_type_reference_constraints(Checker* me, Symbol const* symbol, VecSpannedType const* args, ErrorCtx* errs) {
    symbol = binder_redirect(my binder, symbol);
    TyList params = {0};
    if (symbol->flags & SYMBOL_TYPE_ALIAS) {
        params = ty_alias_type_params(me, symbol);
    } else {
        Ty const* declared = ty_declared_type_of(me, symbol);
        // (The written arguments are for its own: a class or interface local to a generic function has the
        // function's first)
        if (declared->kind == TY_DECLARED) params = ty_local_type_params(declared);
    }
    u32 given = (u32)vec_spanned_type_size(args);
    if (!params.len || given > params.len) return;
    // (Nothing is lowered unless there's something to check: merged interfaces' type parameters)
    bool constrained = false;
    for (u32 i = 0; i < given && !constrained; i++) constrained = ty_param_constraint(me, params.data[i]) != nullptr;
    if (!constrained) return;
    Ty const** filled = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 i = 0; i < params.len; i++) {
        if (i < given) {
            filled[i] = ty_of_node(&vec_spanned_type_at(args, i)->type);
            continue;
        }
        Ty const* fallback = ty_param_default(me, params.data[i]);
        TyMapper const* so_far = ty_mapper_new(me, (TyList){ .data = params.data, .len = i }, (TyList){ .data = filled, .len = i });
        filled[i] = fallback ? ty_instantiate(me, fallback, so_far) : ty_intrinsic(me, TY_UNKNOWN_);
    }
    TyMapper const* mapper = ty_mapper_new(me, params, (TyList){ .data = filled, .len = params.len });
    for (u32 i = 0; i < given; i++) {
        Ty const* constraint = ty_param_constraint(me, params.data[i]);
        if (!constraint) continue;
        constraint = ty_instantiate(me, constraint, mapper);
        if (filled[i]->flags & TYF_ERROR) continue;
        VecTyReportLine lines = {0};
        if (relate_explaining(me, TY_RELATION_ASSIGNABLE, filled[i], constraint, 2344, &lines, errs)) continue;
        cstr source_str, target_str;
        ty_report_pair_cstr(me, ty_report_source(me, filled[i]), ty_report_target(me, filled[i], constraint), &source_str, &target_str);
        report_explained_with_head(errs, &lines, 2344, cstr_d(
            "Type '", source_str, "' does not satisfy the constraint '", target_str, "'."
        ), false, vec_spanned_type_at(args, i)->span);
        c_drop(cstr, &source_str, &target_str);
        return;
    }
}

// The constraint type argument `index` must satisfy, given all the arguments: the ones not written are their parameters'
// defaults, unknown without one (tsc's fillMissingTypeArguments: `extends Command<A, B>` whose `Input extends
// ClientInput` has a later `ClientInput extends object = any`, @aws-sdk's commands)
Ty const* ty_type_argument_constraint(Checker* me, TyList params, VecSpannedType const* args, u32 index) {
    u32 given = (isize_t)params.len < vec_spanned_type_size(args) ? params.len : (u32)vec_spanned_type_size(args);
    if (index >= given) return nullptr;
    Ty const* constraint = ty_param_constraint(me, params.data[index]);
    if (!constraint) return nullptr;
    // The constraint can mention the other type parameters, which stand for the arguments here
    // Arena-allocated: instantiation can keep the mapper (deferred conditional and object types)
    Ty const** filled = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 i = 0; i < params.len; i++) {
        if (i < given) {
            filled[i] = ty_of_node(&vec_spanned_type_at(args, i)->type);
            continue;
        }
        Ty const* fallback = ty_param_default(me, params.data[i]);
        TyMapper const* so_far = ty_mapper_new(me, (TyList){ .data = params.data, .len = i }, (TyList){ .data = filled, .len = i });
        filled[i] = fallback ? ty_instantiate(me, fallback, so_far) : ty_intrinsic(me, TY_UNKNOWN_);
    }
    TyMapper const* mapper = ty_mapper_new(me, params, (TyList){ .data = filled, .len = params.len });
    return ty_instantiate(me, constraint, mapper);
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
    object = ty_reduced(me, object); // (tsc's getPropertyOfType: an intersection that is never has none)
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
    Prop const* prop = ty_get_object_prop(me, object, name); // (Function's and Object's too: tsc's getPropertyOfType)
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


// `ns` of `import * as ns from "./a"`
static bool is_namespace_import(Expr const* expr) {
    if (expr->tag != EXPR_IDENTIFIER || !expr->identifier.symbol) return false;
    PortItem const* item = symbol_find_node(expr->identifier.symbol, SYMBOL_DECL_ALIAS);
    return item && item->tag == PORT_IMPORT_GATHER && !item->is_require;
}

// tsc's isAssignmentToReadonlyEntity for a property
// `exports.x = v` in a CommonJS file declares x (whatever else declares it: the target of an alias export is a const)
static bool is_commonjs_export_assignment(PropertyExpr const* access) {
    SourceFile const* file = span_source(property_key_span(access));
    if (!file || !file->is_commonjs) return false;
    Expr left = { .tag = EXPR_PROPERTY, .property = (PropertyExpr*)access };
    return expr_commonjs_target(&left, nullptr) == COMMONJS_TARGET_EXPORTS_PROPERTY;
}

static bool readonly_assignment(Checker* me, PropertyExpr const* access, Ty const* object, MemberRefs* refs) {
    if (!access->assignment_target || access->commonjs_export_alias || is_commonjs_export_assignment(access)) return false;
    Atom const* name = access_name(me, access);
    if (!name) return false;
    // (`globalThis.globalThis`: tsc's globalThisSymbol is readonly, see the TS2540 in check_access)
    if (is_global_this(object) && access->form == PROPERTY_EXPR_DOT && csview_equals(atom_sv(name), "globalThis")) return true;
    int readonly = member_refs(me, object, name, refs, 0);
    if (readonly == 0) return is_namespace_import(&access->expr);
    if (readonly < 0) return false;
    // (One property seen through several members, `this & Derived` after `this instanceof Derived`, is that property:
    // tsc's createUnionOrIntersectionProperty returns the single symbol all constituents have)
    if (refs->len > 1) {
        Prop const* first = refs->props[0];
        while (first->target) first = first->target;
        bool same = true;
        for (u32 i = 1; i < refs->len && same; i++) {
            Prop const* other = refs->props[i];
            while (other->target) other = other->target;
            same = other == first;
        }
        if (same) refs->len = 1;
    }
    // A class's own readonly properties can be assigned through `this` in its constructor (not in functions in there)
    if (refs->len == 1 && is_keyword(&access->expr, KEYWORD_IDENTIFIER_THIS) && property_context(access)->in_constructor_of &&
        !(refs->props[0]->flags & PROP_ACCESSOR) && refs->props[0]->parent == property_context(access)->in_constructor_of
    ) {
        return false;
    }
    return true;
}

void ty_check_delete_operand(Checker* me, Expr const* operand, ErrorCtx* errs) {
    if (operand->tag != EXPR_PROPERTY) {
        error_ctx_error_d(errs, expr_error_span_skip_parens(operand), 2703, "The operand of a 'delete' operator must be a property reference.");
        return;
    }
    PropertyExpr const* access = operand->property;
    Span name_span = property_key_inner_span(access);
    if (access->form == PROPERTY_EXPR_DOT && span_valid(name_span) && name_span.buf[0] == '#') {
        error_ctx_error_d(errs, operand->inner_span, 18011, "The operand of a 'delete' operator cannot be a private identifier.");
    }
    Atom const* name = access_name(me, access);
    Ty const* object = ty_optional_expression_type(me, ty_expr_type(me, &access->expr), access->optional_chain, nullptr);
    MemberRefs refs = {0};
    int readonly = name ? member_refs(me, object, name, &refs, 0) : -1;
    if (readonly < 0 && !(object->flags & TYF_ANY)) {
        // Through a readonly index signature (tsc's getPropertyTypeForIndexType with isDeleteTarget)
        Ty const* apparent = ty_apparent_type(me, object);
        IndexInfo const* info = apparent->flags & (TYF_OBJECT | TYF_INTERSECTION) ?
            ty_applicable_index_info(me, apparent, ty_regular(me, ty_access_key_type(me, access))) : nullptr;
        if (info && info->readonly) {
            cstr object_str = ty_to_cstr(me, apparent); // (tsc shows the apparent type either way)
            error_ctx_error_d(errs, operand->inner_span, 2542, "Index signature in type '", object_str, "' only permits reading.");
            cstr_drop(&object_str);
        }
    }
    if (readonly < 0 || !refs.len) return; // (No property: an index signature, or already an error)
    if (readonly || is_namespace_import(&access->expr)) {
        error_ctx_error_d(errs, operand->inner_span, 2704, "The operand of a 'delete' operator cannot be a read-only property.");
        return;
    }
    if (!compiler_options()->strict_null_checks) return;
    // The property tsc makes of them: optional if any is, of the union of their types
    bool exact = compiler_options()->exact_optional_property_types;
    for (u32 i = 0; i < refs.len; i++) {
        Prop const* prop = refs.props[i];
        if (exact ? (prop->flags & PROP_OPTIONAL) : (prop->flags & PROP_OPTIONAL) && !(prop->flags & PROP_STRIP_UNDEFINED)) return;
        Ty const* type = prop_type(me, prop);
        if (type->flags & (TYF_ANY | TYF_UNKNOWN | TYF_NEVER)) return;
        if (!exact && ty_can_be_undefined(me, type)) return;
    }
    error_ctx_error_d(errs, operand->inner_span, 2790, "The operand of a 'delete' operator must be optional.");
}

// A key of a union written through (`o[k] = v`, k: "a" | "b") that names a readonly property: 1, 0 if it names a
// writable one, -1 if it names none (tsc's getPropertyTypeForIndexType with AccessFlags.Writing, isAssignmentToReadonlyEntity)
static int readonly_key_member(Checker* me, PropertyExpr const* access, Ty const* object, Atom const* name) {
    MemberRefs member = {0};
    int readonly = member_refs(me, object, name, &member, 0);
    if (readonly <= 0) return readonly;
    // (A class's own readonly property through `this` in its constructor)
    PropertyContext const* site = property_context(access);
    if (member.len == 1 && is_keyword(&access->expr, KEYWORD_IDENTIFIER_THIS) && site->in_constructor_of &&
        !(member.props[0]->flags & PROP_ACCESSOR) && member.props[0]->parent == site->in_constructor_of
    ) {
        return 0;
    }
    return 1;
}

// Whether writing to some key of `object` can hit a readonly property or index signature: a cheap gate before trying
// each member of a union key (a union of hundreds of enum members writing into an array is common). Conservative for
// what it doesn't look into: unions, intersections, tuples, and anything with signatures (Function's readonly members)
static bool may_have_readonly_member(Checker* me, Ty const* object) {
    Ty const* apparent = ty_apparent_type(me, object);
    if (!(apparent->flags & TYF_OBJECT) || ty_is_tuple(apparent)) return true;
    Members const* members = ty_members(me, apparent);
    if (members->call_count || members->construct_count) return true;
    for (u32 i = 0; i < members->prop_count; i++) {
        if (members->props[i]->flags & PROP_READONLY) return true;
    }
    for (u32 i = 0; i < members->index_count; i++) {
        if (members->index[i].readonly) return true;
    }
    return false;
}

// The key of an element access written to, when it is a (non-generic) union of keys written one by one
static Ty const* written_union_key(Checker* me, PropertyExpr const* access) {
    // (A union key has no single name, so access_name() needn't be asked: this runs for every element write)
    if (!access->assignment_target || access->form != PROPERTY_EXPR_BRACKET) return nullptr;
    Ty const* key = ty_access_key_type(me, access);
    if (key->kind != TY_UNION) return nullptr;
    key = ty_regular(me, key);
    return key->kind == TY_UNION && !(key->flags & TYF_BOOLEAN) && !ty_is_generic_index(key) ? key : nullptr;
}

bool ty_is_readonly_assignment(Checker* me, PropertyExpr const* access, Ty const* object) {
    MemberRefs refs = {0};
    if (readonly_assignment(me, access, object, &refs)) return true;
    // (Through a union of keys, one readonly property makes the whole access the error type: tsc's
    // getIndexedAccessTypeOrUndefined gives up when a key's getPropertyTypeForIndexType does)
    Ty const* key = written_union_key(me, access);
    if (key && !may_have_readonly_member(me, object)) return false;
    for (u32 i = 0; key && i < key->composite.types.len; i++) {
        Atom const* name = ty_literal_name(me, key->composite.types.data[i]);
        if (name && readonly_key_member(me, access, object, name) > 0) return true;
    }
    return false;
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
static Ty const* this_parameter_class(Checker* me, AccessSite const* access) {
    Ty const* this_type = access->this_parameter ? ty_type_of_symbol(me, access->this_parameter) :
        access->this_function ? ty_contextual_this_type(me, access->this_function) : nullptr;
    if (this_type && this_type->kind == TY_TYPE_PARAM) this_type = ty_param_constraint(me, this_type);
    if (!this_type) return nullptr;
    if (this_type->kind == TY_REFERENCE) this_type = this_type->reference.target;
    if (this_type->kind != TY_DECLARED) return nullptr;
    TyDeclaredKind kind = this_type->declared.kind;
    return kind == TY_DECLARED_CLASS_INSTANCE || kind == TY_DECLARED_INTERFACE ? this_type : nullptr;
}

static bool span_contains(Span outer, char const* at) {
    return at >= span_start_ptr(outer) && at < span_end_ptr(outer);
}

// The class that declares prop abstract, null if it isn't (methods only if `methods`, else properties and accessors:
// tsc's symbolHasNonMethodDeclaration)
static ClassDecl const* abstract_member_class(Prop const* prop, bool methods) {
    while (prop->target) prop = prop->target;
    if (!methods && (prop->flags & PROP_METHOD)) return nullptr;
    if (!prop->parent || !span_valid(prop->decl->name_span)) return nullptr;
    ClassDecl const* class_decl = class_decl_of(prop->parent);
    // (An abstract member of a class that isn't abstract counts too, TS1253 aside: tsc reads the member's modifiers)
    if (!class_decl) return nullptr;
    char const* name_at = span_start_ptr(prop->decl->name_span);
    for_each (i, vec_class_item, &class_decl->items) {
        if (!span_contains(i->span, name_at)) continue;
        bool is_abstract = i->tag == CLASS_ITEM_FIELD ? i->field.is_abstract :
            i->tag == CLASS_ITEM_METHOD && (methods || i->method.accessor) ? i->method.is_abstract : false;
        return is_abstract ? class_decl : nullptr;
    }
    return nullptr;
}

// tsc's checkPropertyAccessibilityAtLocation for one (constituent) property, false if it reported
// `site`: where it is read, `span` what the error is at, `this_object`: the object is `this` (tsc's isThisProperty,
// isThisInitializedObjectBindingExpression, isThisInitializedDeclaration)
static bool check_accessibility(
    Checker* me, AccessSite const* site, Span span, bool this_object, bool writing, Prop const* prop,
    Ty const* containing, bool is_super, ErrorCtx* errs
) {
    if (is_super) {
        if (abstract_member_class(prop, true)) {
            cstr class_str = ty_to_cstr(me, ty_declared_type_of(me, prop->parent));
            error_ctx_error_d(errs, span, 2513, "Abstract method '", atom_sv(prop->name), "' in class '", class_str,
                "' cannot be accessed via super expression.");
            cstr_drop(&class_str);
            return false;
        }
        if (!(prop->flags & PROP_STATIC) && (prop->flags & PROP_FIELD)) {
            // (Named as written: `this['x']` is `'x'`)
            Prop const* root = prop;
            while (root->target) root = root->target;
            Symbol const* symbol = root->decl ? root->decl->symbol : nullptr;
            csview written = symbol && span_valid(symbol->name_span) ? span_sv(symbol->name_span) : atom_sv(prop->name);
            error_ctx_error_d(errs, span, 2855, "Class field '", written,
                "' defined by the parent class is not accessible in the child class via super.");
            return false;
        }
    }
    u32 modifiers = prop->flags & (PROP_PRIVATE | PROP_PROTECTED);
    if ((prop->flags & PROP_ACCESSOR) && writing) {
        modifiers = (prop->flags & PROP_SET_PRIVATE ? PROP_PRIVATE : 0) |
            (prop->flags & PROP_SET_PROTECTED ? PROP_PROTECTED : 0);
    }
    // An abstract property read through `this` while the class initializes (tsc's isNodeUsedDuringClassInitialization:
    // directly in a constructor, or a field's initializer)
    if (this_object && ((site->containing_function &&
        function_impl_rare(site->containing_function)->constructed_class) || site->initializer_context == INITIALIZER_INSTANCE_FIELD ||
        site->initializer_context == INITIALIZER_STATIC_FIELD)
    ) {
        ClassDecl const* abstract_class = abstract_member_class(prop, false);
        if (abstract_class) {
            // (symbolToString of the class: a class expression by the variable it initializes)
            error_ctx_error_d(errs, span, 2715, "Abstract property '", atom_sv(prop->name), "' in class '",
                class_decl_display_name(abstract_class), "' cannot be accessed in the constructor.");
            return false;
        }
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


// The accessibility of the property tsc makes of a union's or intersection's members' (createUnionOrIntersectionProperty's
// CheckFlags.Contains{Public,Protected,Private} and ContainsWrite*, for reading and writing): each member's property
// counts by its own accessibility, a member's synthetic property by its most permissive
enum { SYNTH_PUBLIC = 1, SYNTH_PROTECTED = 2, SYNTH_PRIVATE = 4 };

typedef struct SyntheticAccess {
    bool found; // There's a property (a union's has one in every member, and no non-public one it can't read)
    bool synthetic; // Made of several properties (else `single` is the one)
    Prop const* single;
    unsigned char read, write; // SYNTH_* that the members contain
    bool is_static;
    MemberRefs leaves; // The declared properties under it (tsc's forEachProperty)
} SyntheticAccess;

static unsigned char synth_kind(u32 modifiers) {
    return modifiers & PROP_PROTECTED ? SYNTH_PROTECTED : modifiers & PROP_PRIVATE ? SYNTH_PRIVATE : SYNTH_PUBLIC;
}

// What a property's accessibility is for reading or writing (tsc's getDeclarationModifierFlagsFromSymbol): an
// accessor's setter's for writing; of a synthetic one public over protected over private
static unsigned char synth_effective(SyntheticAccess const* access, bool writing) {
    if (!access->synthetic) {
        Prop const* prop = access->single;
        u32 modifiers = prop->flags & (PROP_PRIVATE | PROP_PROTECTED);
        if ((prop->flags & PROP_ACCESSOR) && writing) {
            modifiers = (prop->flags & PROP_SET_PRIVATE ? PROP_PRIVATE : 0) | (prop->flags & PROP_SET_PROTECTED ? PROP_PROTECTED : 0);
        }
        return synth_kind(modifiers);
    }
    unsigned char contains = writing ? access->write : access->read;
    return contains & SYNTH_PUBLIC ? SYNTH_PUBLIC : contains & SYNTH_PROTECTED ? SYNTH_PROTECTED : SYNTH_PRIVATE;
}

static Prop const* prop_root(Prop const* prop) {
    while (prop->target) prop = prop->target;
    return prop;
}

static void synthetic_access(Checker* me, Ty const* object, Atom const* name, SyntheticAccess* out, u32 depth) {
    *out = (SyntheticAccess){0};
    if (depth > 8) return;
    object = ty_reduced(me, object);
    if (object->kind != TY_UNION && object->kind != TY_INTERSECTION) object = ty_apparent_type(me, object);
    if ((object->kind == TY_UNION && !(object->flags & TYF_BOOLEAN)) || object->kind == TY_INTERSECTION) {
        bool is_union = object->kind == TY_UNION;
        bool partial = false, distinct = false;
        Prop const* first = nullptr;
        for (u32 i = 0; i < object->composite.types.len; i++) {
            Ty const* member = object->composite.types.data[i];
            if (member->flags & TYF_NULLABLE) continue;
            SyntheticAccess child;
            synthetic_access(me, member, name, &child, depth + 1);
            if (!child.found) {
                if (!is_union) continue;
                // (Through an index signature it's only partial for writing, else the union has no such property)
                Ty const* apparent = ty_apparent_type(me, member);
                IndexInfo const* info = apparent->flags & (TYF_OBJECT | TYF_INTERSECTION) ?
                    ty_applicable_index_info(me, apparent, ty_intrinsic(me, TY_STRING)) : nullptr;
                if (!info) {
                    out->found = false;
                    return;
                }
                partial = true;
                continue;
            }
            out->found = true;
            out->read |= synth_effective(&child, false);
            out->write |= synth_effective(&child, true);
            out->is_static |= child.is_static;
            for (u32 j = 0; j < child.leaves.len && out->leaves.len < c_arraylen(out->leaves.props); j++) {
                out->leaves.props[out->leaves.len] = child.leaves.props[j];
                out->leaves.objects[out->leaves.len++] = child.leaves.objects[j];
            }
            // (Instantiations of one property with the same type are that property: tsc keeps the single one)
            if (child.synthetic) {
                distinct = true;
            } else if (!first) {
                first = child.single;
            } else if (prop_root(child.single) != prop_root(first) ||
                !ty_is_related(me, prop_type(me, child.single), prop_type(me, first), TY_RELATION_IDENTITY)
            ) {
                distinct = true;
            }
        }
        if (!out->found) return;
        out->synthetic = distinct;
        out->single = distinct ? nullptr : first;
        if (!distinct && !partial) return;
        // A union's property with a private or protected member's that isn't the same declaration everywhere (tsc's
        // hasCommonDeclaration): none if that's for reading, else written as the most restricted member's
        bool common = out->leaves.len > 0;
        for (u32 i = 1; i < out->leaves.len && common; i++) common = prop_root(out->leaves.props[i]) == prop_root(out->leaves.props[0]);
        if (is_union && ((out->read | out->write) & (SYNTH_PROTECTED | SYNTH_PRIVATE)) && !common) {
            if (out->read & (SYNTH_PROTECTED | SYNTH_PRIVATE)) {
                out->found = false;
                return;
            }
            if (out->write & SYNTH_PRIVATE) out->write = SYNTH_PRIVATE;
            else if (out->write & SYNTH_PROTECTED) out->write &= (unsigned char)~SYNTH_PUBLIC;
        }
        // (A partial one is a synthetic property to tsc)
        if (partial && !out->synthetic) {
            out->synthetic = true;
            out->read = synth_effective(&(SyntheticAccess){ .single = first }, false);
            out->write = (unsigned char)(out->write & (SYNTH_PROTECTED | SYNTH_PRIVATE) ? out->write : synth_effective(&(SyntheticAccess){ .single = first }, true));
            out->single = nullptr;
        }
        return;
    }
    if (!(object->flags & TYF_OBJECT)) return;
    Prop const* prop = ty_get_object_prop(me, object, name);
    if (!prop) return;
    out->found = true;
    out->single = prop;
    out->is_static = (prop->flags & PROP_STATIC) != 0;
    out->leaves.props[0] = prop;
    out->leaves.objects[0] = object;
    out->leaves.len = 1;
}

// tsc's checkPropertyAccessibilityAtLocation of a synthetic property: public if its accessibility for the access is,
// private nowhere (it has no declaring class), protected where the enclosing class derives from the declaring class of
// every protected property under it (isClassDerivedFromDeclaringClasses). The messages name the containing type
static void check_synthetic_access(
    Checker* me, AccessSite const* site, Span span, SyntheticAccess const* access, bool writing, Ty const* containing,
    Atom const* name, ErrorCtx* errs
) {
    unsigned char kind = synth_effective(access, writing);
    if (kind == SYNTH_PUBLIC) return;
    if (kind == SYNTH_PRIVATE) {
        cstr class_str = ty_to_cstr(me, containing);
        error_ctx_error_d(errs, span, 2341, "Property '", atom_sv(name), "' is private and only accessible within class '", class_str, "'.");
        cstr_drop(&class_str);
        return;
    }
    Ty const* declaring[c_arraylen(access->leaves.props)];
    u32 count = 0;
    for (u32 i = 0; i < access->leaves.len; i++) {
        Prop const* prop = access->leaves.props[i];
        SyntheticAccess leaf = { .single = prop };
        if (synth_effective(&leaf, writing) != SYNTH_PROTECTED) continue;
        if (!prop->parent || !class_decl_of(prop->parent)) continue;
        declaring[count++] = ty_declared_type_of(me, prop->parent);
    }
    bool is_static = access->is_static;
    Ty const* enclosing_type = nullptr;
    for (ClassDecl const* c = site->enclosing_class; c && !enclosing_type; c = c->outer_class) {
        if (!c->symbol) continue;
        Ty const* type = ty_declared_type_of(me, c->symbol);
        bool derived = true;
        for (u32 i = 0; i < count && derived; i++) derived = ty_has_base_type(me, type, declaring[i]);
        if (derived) enclosing_type = type;
    }
    bool enclosing_found = enclosing_type != nullptr;
    if (!enclosing_type) {
        enclosing_type = this_parameter_class(me, site);
        for (u32 i = 0; enclosing_type && i < count; i++) {
            if (!ty_has_base_type(me, enclosing_type, declaring[i])) enclosing_type = nullptr;
        }
    }
    if (!enclosing_type || (is_static && !enclosing_found)) {
        cstr class_str = ty_to_cstr(me, containing);
        error_ctx_error_d(errs, span, 2445, "Property '", atom_sv(name),
            "' is protected and only accessible within class '", class_str, "' and its subclasses.");
        cstr_drop(&class_str);
        return;
    }
    if (is_static) return;
    if (containing->kind == TY_TYPE_PARAM) containing = ty_apparent_type(me, containing);
    if (!containing_has_base(me, containing, enclosing_type)) {
        cstr enclosing_str = ty_to_cstr(me, enclosing_type);
        cstr containing_str = ty_to_cstr(me, containing);
        error_ctx_error_d(errs, span, 2446, "Property '", atom_sv(name),
            "' is protected and only accessible through an instance of class '", enclosing_str,
            "'. This is an instance of class '", containing_str, "'.");
        c_drop(cstr, &enclosing_str, &containing_str);
    }
}

static Generic const* class_or_interface_generic(SymbolDecl const* decl) {
    switch (decl->kind) {
        case SYMBOL_DECL_CLASS: return &((ClassDecl const*)decl->node)->generic;
        case SYMBOL_DECL_INTERFACE: return &((InterfaceDecl const*)decl->node)->generic;
        default: return nullptr;
    }
}

void ty_check_type_parameter_lists_identical(Checker* me, Symbol const* symbol, ErrorCtx* errs) {
    // tsc's localTypeParameters: the distinct names, in the order the declarations introduce them
    GenericItem const* target[32];
    u32 target_count = 0, declaration_count = 0;
    for (u32 d = 0; d < symbol->decl_count; d++) {
        Generic const* generic = class_or_interface_generic(&symbol->decls[d]);
        if (!generic) continue;
        declaration_count++;
        for_each (i, vec_generic_item, &generic->items) {
            bool seen = false;
            for (u32 t = 0; t < target_count && !seen; t++) seen = name_equals(span_sv(target[t]->name), span_sv(i->name));
            if (!seen && target_count < c_arraylen(target)) target[target_count++] = i;
        }
    }
    if (declaration_count < 2) return;
    // The declared type first: it makes the later declarations' type parameters the first one's (unify_type_params())
    ty_declared_type_of(me, symbol);
    // Defaults and constraints come from the first declaration that has one
    TypeNode const* defaults[32] = {0};
    TypeNode const* constraints[32] = {0};
    for (u32 d = 0; d < symbol->decl_count; d++) {
        Generic const* generic = class_or_interface_generic(&symbol->decls[d]);
        for (u32 t = 0; generic && t < target_count; t++) {
            for_each (i, vec_generic_item, &generic->items) {
                if (!name_equals(span_sv(target[t]->name), span_sv(i->name))) continue;
                if (!defaults[t] && i->fallback_explicit) defaults[t] = i->fallback;
                if (!constraints[t] && i->extends_explicit) constraints[t] = &i->extends;
            }
        }
    }
    u32 min_count = 0;
    for (u32 t = 0; t < target_count; t++) if (!defaults[t]) min_count = t + 1;
    Ty const* target_params[32];
    for (u32 t = 0; t < target_count; t++) target_params[t] = ty_type_param(me, target[t]->owned_symbol);
    bool identical = true;
    for (u32 d = 0; d < symbol->decl_count && identical; d++) {
        Generic const* generic = class_or_interface_generic(&symbol->decls[d]);
        if (!generic) continue;
        u32 count = (u32)vec_generic_item_size(&generic->items);
        if (count < min_count || count > target_count) {
            identical = false;
            break;
        }
        // The declaration's own type parameters stand for the merged ones in what it writes
        Ty const* source_params[32];
        for (u32 i = 0; i < count; i++) source_params[i] = ty_type_param(me, vec_generic_item_at(&generic->items, i)->owned_symbol);
        TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = source_params, .len = count }, (TyList){ .data = target_params, .len = count });
        for (u32 i = 0; i < count && identical; i++) {
            GenericItem const* source = vec_generic_item_at(&generic->items, i);
            if (!name_equals(span_sv(source->name), span_sv(target[i]->name))) {
                identical = false;
            } else if (source->extends_explicit && constraints[i] && &source->extends != constraints[i]) {
                Ty const* a = ty_instantiate(me, ty_of_node(&source->extends), mapper);
                identical = ty_is_related(me, a, ty_of_node(constraints[i]), TY_RELATION_IDENTITY);
            }
            if (identical && source->fallback_explicit && defaults[i] && source->fallback != defaults[i]) {
                Ty const* a = ty_instantiate(me, ty_of_node(source->fallback), mapper);
                identical = ty_is_related(me, a, ty_of_node(defaults[i]), TY_RELATION_IDENTITY);
            }
        }
    }
    if (identical) return;
    for (u32 d = 0; d < symbol->decl_count; d++) {
        if (!class_or_interface_generic(&symbol->decls[d])) continue;
        // (A lib file's declarations too: what an unchecked one is told is dropped, error_ctx_error_s())
        Span name = symbol->decls[d].name;
        if (!span_source(name)) continue;
        error_ctx_error_d(errs, name, 2428, "All declarations of '", symbol->name, "' must have identical type parameters.");
    }
}

void ty_check_duplicate_index_signatures(Checker* me, InterfaceMember const* const* members, u32 count, ErrorCtx* errs) {
    typedef struct { Ty const* key; InterfaceMember const* member; } Entry;
    Entry entries[64];
    u32 entry_count = 0;
    for (u32 i = 0; i < count; i++) {
        InterfaceMember const* member = members[i];
        if (member->key.tag != INTERFACE_KEY_INDEX || !member->key.index->valid) continue;
        Ty const* key = ty_of_node(&member->key.index->type);
        TyList keys = key->kind == TY_UNION ? key->composite.types : (TyList){ .data = &key, .len = 1 };
        for (u32 k = 0; k < keys.len && entry_count < c_arraylen(entries); k++) {
            entries[entry_count++] = (Entry){ keys.data[k], member };
        }
    }
    for (u32 i = 0; i < entry_count; i++) {
        u32 same = 0;
        bool first = true;
        for (u32 j = 0; j < entry_count; j++) {
            if (entries[j].key != entries[i].key) continue;
            same++;
            if (j < i) first = false;
        }
        if (same < 2 || !first) continue;
        cstr key_str = ty_to_cstr(me, entries[i].key);
        for (u32 j = 0; j < entry_count; j++) {
            if (entries[j].key != entries[i].key) continue;
            Span at = span_valid(entries[j].member->declaration) ? entries[j].member->declaration : entries[j].member->key.span;
            if (!span_source(at)) continue;
            error_ctx_error_d(errs, span_with_member_separator(at), 2374, "Duplicate index signature for type '", key_str, "'.");
        }
        cstr_drop(&key_str);
    }
}

// Duplicate members

enum {
    MEMBER_PROPERTY = SYMBOL_PROPERTY, MEMBER_METHOD = SYMBOL_METHOD, MEMBER_GET = SYMBOL_GET_ACCESSOR,
    MEMBER_SET = SYMBOL_SET_ACCESSOR,
};

typedef struct MemberEntry {
    Atom const* name;
    uint8_t table; // Which symbol table: instance members, static members (exports)
    bool has_member; // Not only exports of a namespace merged with the class, which the binder checked already
    u32 flags; // SymbolFlags
    Span decls[8];
    u32 decl_count;
    u32 total; // Declarations, those past `decls` too
    // Before merge_late_members() merged late-bound declarations into it: what tsc's check of a member before the first
    // late-bound one sees (getMergedSymbol before late binding ran), see check_duplicate_declarations()
    u32 early_total;
    bool merged;
    bool shadowed; // A late-bound one that didn't merge into the early one of its name
} MemberEntry;

typedef struct MemberTables {
    MemberEntry* entries;
    u32 count;
    u32 cap;
    // Past 16 entries, where each (name, table) is among them (find_member(): scanning them for each member declared is
    // quadratic on large classes and interfaces), built lazily: the first `indexed` entries are in it
    NameIndex by_name;
    u32 indexed;
} MemberTables;

static void member_tables_unindex(MemberTables* me) {
    name_index_drop(&my by_name);
    my indexed = 0;
}

static MemberEntry* add_member_entry(MemberTables* me) {
    if (my count == my cap) {
        my cap = my cap ? my cap * 2 : 32;
        my entries = realloc(my entries, sizeof(MemberEntry) * my cap);
        panic_if(!my entries, "out of memory");
    }
    return &my entries[my count++];
}

// TS2300 at `at` naming `name` (once: the binder's and the checker's reports of a declaration are one)
static void report_duplicate_named(Span at, csview name, ErrorCtx* errs) {
    // (A lib's declaration only when lib files are checked: the lib side of the conflict, tsc's check of the lib)
    if (!errs || !span_valid(at) || !span_source(at) || (span_source(at)->is_stdlib && !lib_files_checked())) return;
    Error* existing = error_ctx_find(errs, at, 2300);
    if (!existing) error_ctx_error_s(errs, at, 2300, cstr_d("Duplicate identifier '", name, "'."));
}

static void report_duplicate(Span name, ErrorCtx* errs) {
    report_duplicate_named(name, span_sv(name), errs);
}

// Where (name, table) is in the index, or the empty slot it would go to
static u64 member_tables_probe(MemberTables const* me, Atom const* name, uint8_t table) {
    u64 slot = name_index_slot(&my by_name, (u64)(uintptr_t)name + table);
    for (isize at; (at = my by_name.slots[slot]) >= 0; slot = name_index_next(&my by_name, slot)) {
        if (my entries[at].name == name && my entries[at].table == table) break;
    }
    return slot;
}

static MemberEntry* find_member(MemberTables* me, Atom const* name, uint8_t table) {
    if (my count <= 16) {
        for (u32 i = 0; i < my count; i++) {
            if (my entries[i].name == name && my entries[i].table == table) return &my entries[i];
        }
        return nullptr;
    }
    if (my by_name.slots && (u64)my count * 2 > my by_name.mask + 1) member_tables_unindex(me);
    if (!my by_name.slots) my by_name = name_index_make((isize)my count * 2);
    for (; my indexed < my count; my indexed++) {
        u64 slot = member_tables_probe(me, my entries[my indexed].name, my entries[my indexed].table);
        if (my by_name.slots[slot] < 0) my by_name.slots[slot] = my indexed;
    }
    isize at = my by_name.slots[member_tables_probe(me, name, table)];
    return at >= 0 ? &my entries[at] : nullptr;
}

// tsc's declareSymbol for a member (or an export of a namespace merged with a class, is_member false): a conflict with
// what the table has (by the excludes of the new declaration) is reported on every declaration, and the new one doesn't
// join. Returns the entry (the symbol) it joined, -1 for one of its own
static i32 declare_symbol_flags(
    MemberTables* me, Atom const* name, uint8_t table, u32 includes, Span span, bool is_member, ErrorCtx* errs
) {
    MemberEntry* entry = find_member(me, name, table);
    if (!entry) {
        entry = add_member_entry(me);
        *entry = (MemberEntry){ .name = name, .table = table };
    } else if (entry->flags & symbol_excludes(includes)) {
        if (!entry->has_member && !is_member) return -1;
        for (u32 i = 0; i < entry->decl_count; i++) report_duplicate(entry->decls[i], errs);
        report_duplicate(span, errs);
        // Declarations merged from other files point at each other (tsc's mergeSymbol across files: its
        // amalgamatedDuplicates with fewer than 8 names, TODO: one TS6200 per file from 8 on)
        Error* error = errs ? error_ctx_find(errs, span, 2300) : nullptr;
        for (u32 i = 0; i < entry->decl_count && errs; i++) {
            if (!span_valid(entry->decls[i]) || span_source(entry->decls[i]) == span_source(span)) continue;
            add_duplicate_related(error_ctx_find(errs, entry->decls[i], 2300), entry->decls[i], span);
            add_duplicate_related(error, span, entry->decls[i]);
        }
        // An accessor conflicting with another kind makes the symbol a full accessor: what comes later conflicts
        // too (tsgo's declareSymbol)
        u32 accessor = SYMBOL_GET_ACCESSOR | SYMBOL_SET_ACCESSOR;
        if (entry->flags & accessor && (entry->flags & accessor) != (includes & accessor)) entry->flags |= accessor;
        return -1;
    }
    entry->flags |= includes;
    entry->has_member |= is_member;
    entry->total++;
    if (entry->decl_count < c_arraylen(entry->decls)) entry->decls[entry->decl_count++] = span;
    return (i32)(entry - me->entries);
}

static i32 declare_member(MemberTables* me, Atom const* name, uint8_t table, u32 kind, Span span, ErrorCtx* errs) {
    return declare_symbol_flags(me, name, table, kind, span, true, errs);
}

// The late-bound names (a computed name of a unique symbol or literal type that isn't written as a literal) have
// tables of their own
enum { LATE_TABLE = 4 };
// The private names have tables of their own too
enum { PRIVATE_TABLE = 2 };

// tsc's lateBindMember: a conflict with the late-bound declarations of the name so far is reported at those and the
// early-bound ones of the name, all named alike: a unique symbol by the declaration's name, else the property name
static i32 declare_late_member(MemberTables* me, Atom const* name, uint8_t table, u32 kind, Span span, bool unique_symbol, ErrorCtx* errs) {
    MemberEntry* entry = find_member(me, name, table + LATE_TABLE);
    if (entry && (entry->flags & symbol_excludes(kind))) {
        csview display = unique_symbol ? span_sv(span) : atom_sv(name);
        MemberEntry const* early = find_member(me, name, table);
        for (u32 i = 0; early && i < early->decl_count; i++) report_duplicate_named(early->decls[i], display, errs);
        for (u32 i = 0; i < entry->decl_count; i++) report_duplicate_named(entry->decls[i], display, errs);
        report_duplicate_named(span, display, errs);
        return -1;
    }
    if (!entry) {
        entry = add_member_entry(me);
        *entry = (MemberEntry){ .name = name, .table = table + LATE_TABLE, .has_member = true };
    }
    entry->flags |= kind;
    entry->total++;
    if (entry->decl_count < c_arraylen(entry->decls)) entry->decls[entry->decl_count++] = span;
    return (i32)(entry - me->entries);
}

// A member declaration of a class, interface or type literal, as tsc's checkObjectTypeForDuplicateDeclarations sees it
typedef struct DupRecord {
    Atom const* name; // Its symbol's name
    Span span; // Its name
    void const* member; // The ClassItem or InterfaceMember, null for a parameter property
    FunctionArg const* arg; // The parameter property
    i32 entry; // Its symbol: the MemberTables entry it joined, -1 a symbol of its own
    uint8_t table; // 0 instance, 1 static
    uint8_t check_kind; // 1 a property, 2 an accessor, 0 neither
    bool is_private;
    bool late; // Late bound: after the early declarations of its symbol once merge_late_members() merged it
    u32 list; // The declaration it is in (of an interface's)
    u32 position; // The index of the class item (a parameter property's constructor)
} DupRecord;

#define g_key DupRecord, dup_record
#include <cleat/generic/vec.h>

// tsc's combineSymbolTables of the early and late members: a late-bound symbol merges with the early one of its name
// (mergeSymbol, unless one excludes the other), the early declarations first
static void merge_late_members(MemberTables* tables, VecDupRecord* records, ErrorCtx* errs) {
    for (u32 l = 0; l < tables->count; l++) {
        MemberEntry* late = &tables->entries[l];
        if (late->table < LATE_TABLE) continue;
        MemberEntry* early = find_member(tables, late->name, late->table - LATE_TABLE);
        if (!early) continue;
        late->shadowed = (early->flags & symbol_excludes(late->flags)) || (late->flags & symbol_excludes(early->flags));
        if (early->flags & symbol_excludes(late->flags)) {
            // tsc's mergeSymbol of the two: reportMergeSymbolError, named as the late symbol prints (`[foo]`), each
            // side's declarations pointing at the other's
            if (!early->has_member || !late->decl_count || !errs) continue;
            csview display = span_sv(late->decls[0]);
            for (u32 i = 0; i < late->decl_count; i++) {
                report_duplicate_named(late->decls[i], display, errs);
                for (u32 j = 0; j < early->decl_count; j++) add_duplicate_related_named(error_ctx_find(errs, late->decls[i], 2300), late->decls[i], early->decls[j], display);
            }
            for (u32 j = 0; j < early->decl_count; j++) {
                report_duplicate_named(early->decls[j], display, errs);
                for (u32 i = 0; i < late->decl_count; i++) add_duplicate_related_named(error_ctx_find(errs, early->decls[j], 2300), early->decls[j], late->decls[i], display);
            }
            continue;
        }
        if (late->flags & symbol_excludes(early->flags)) continue;
        i32 early_index = (i32)(early - tables->entries);
        for_each (r, vec_dup_record, records) {
            if (r->entry == (i32)l) r->entry = early_index;
        }
        if (!early->merged) early->early_total = early->total;
        early->merged = true;
        early->total += late->total;
        late->total = 0;
    }
}

// Per entry (symbol), its first record: an early one before the late-bound ones merged into it (callers free it)
static DupRecord const** dup_first_records(MemberTables const* tables, VecDupRecord const* records) {
    DupRecord const** ret = calloc(tables->count ? tables->count : 1, sizeof(DupRecord const*));
    panic_if(!ret, "out of memory");
    for (int late = 0; late < 2; late++) {
        for_each (r, vec_dup_record, records) {
            if (r->entry >= 0 && r->late == (bool)late && !ret[r->entry]) ret[r->entry] = r;
        }
    }
    return ret;
}

// How tsc prints a member's symbol (symbolToString): its first declaration's name as written
static csview dup_record_display(MemberTables const* tables, DupRecord const* record) {
    return record->entry >= 0 && tables->entries[record->entry].decl_count ? span_sv(tables->entries[record->entry].decls[0]) : span_sv(record->span);
}

static void report_duplicate_members(
    MemberTables const* tables, VecDupRecord const* records, u32 list, Atom const* name, bool check_static, uint8_t table,
    bool private_static, ErrorCtx* errs
) {
    for_each (r, vec_dup_record, records) {
        if (r->list != list || r->name != name || (check_static && r->table != table) || (private_static && !r->is_private)) continue;
        if (!private_static) {
            report_duplicate_named(r->span, dup_record_display(tables, r), errs);
        } else if (span_valid(r->span) && span_source(r->span) && !span_source(r->span)->is_stdlib && !error_ctx_find(errs, r->span, 2804)) {
            error_ctx_error_d(errs, r->span, 2804, "Duplicate identifier '", dup_record_display(tables, r),
                "'. Static and instance elements cannot share the same private name.");
        }
    }
}

// tsc's checkObjectTypeForDuplicateDeclarations of one declaration: a property declared again (a symbol of several
// declarations) is reported at every member of the name, and so is an accessor declared with a property (with
// `private_names`, a private name both static and instance is TS2804)
static void check_duplicate_declarations(MemberTables const* tables, VecDupRecord const* records, u32 list, bool private_names, ErrorCtx* errs) {
    typedef struct { Atom const* name; uint8_t table; uint8_t state; } NameState;
    NameState* states = malloc(sizeof(NameState) * (vec_dup_record_size(records) + 1));
    panic_if(!states, "out of memory");
    u32 state_count = 0;
    // (In a class, tsc's getSymbolOfDeclaration of a member before the first late-bound one: the early symbol, which
    // late binding hasn't merged anything into yet. `[k]: T; kk: T` is TS2300, `kk: T; [k]: T` isn't. An interface's
    // or type literal's check resolved the members first, checkIndexConstraints)
    bool late_seen = !private_names;
    for_each (r, vec_dup_record, records) {
        late_seen |= r->late;
        if (r->list != list || r->entry < 0) continue;
        MemberEntry const* entry = &tables->entries[r->entry];
        u32 total = entry->merged && !late_seen ? entry->early_total : entry->total;
        if (r->check_kind && total > 1) {
            NameState* state = nullptr;
            for (u32 i = 0; i < state_count && !state; i++) {
                if (states[i].name == r->name && states[i].table == r->table) state = &states[i];
            }
            if (!state) {
                states[state_count++] = (NameState){ r->name, r->table, r->check_kind };
            } else if (state->state == 1 || (state->state == 2 && r->check_kind != 2)) {
                report_duplicate_members(tables, records, list, r->name, true, r->table, false, errs);
                state->state = 3;
            }
        }
    }
    if (private_names) {
        state_count = 0;
        for_each (r, vec_dup_record, records) {
            if (r->list != list || !r->is_private) continue;
            NameState* state = nullptr;
            for (u32 i = 0; i < state_count && !state; i++) {
                if (states[i].name == r->name) state = &states[i];
            }
            if (!state) {
                state = &states[state_count++];
                *state = (NameState){ r->name, 0, 0 };
            }
            if (state->state == 3) continue;
            state->state |= r->table & 1 ? 2 : 1;
            if (state->state == 3) report_duplicate_members(tables, records, list, r->name, false, 0, true, errs);
        }
    }
    free(states);
}

// The exports a namespace declaration merged with a class adds to the table of static members
static void declare_namespace_exports(Checker* me, MemberTables* tables, Symbol const* symbol, SymbolDecl const* ns, ErrorCtx* errs) {
    SymbolTable const* exports = symbol->exports;
    for (u32 i = 0; exports && i < exports->count; i++) {
        Symbol const* exported = exports->entries[i].symbol;
        for (u32 d = 0; d < exported->decl_count; d++) {
            SymbolDecl const* decl = &exported->decls[d];
            if (span_source(decl->name) != span_source(ns->full) || span_start_ptr(decl->full) < span_start_ptr(ns->full) ||
                span_end_ptr(decl->full) > span_end_ptr(ns->full)
            ) {
                continue;
            }
            declare_symbol_flags(tables, checker_atom(me, symbol_entry_name(&exports->entries[i])), 1, decl->flags, decl->name, false, errs);
        }
    }
}

// tsc's bindClassLikeDeclaration: the class's static side has a `prototype`, which replaces what the table had there
// (a namespace's export of that name is a duplicate)
static void declare_prototype(Checker* me, MemberTables* tables, ErrorCtx* errs) {
    Atom const* name = checker_atom(me, c_sv("prototype"));
    MemberEntry* entry = find_member(tables, name, 1);
    if (entry && entry->decl_count) report_duplicate(entry->decls[0], errs);
    if (!entry) {
        entry = add_member_entry(tables);
    }
    *entry = (MemberEntry){ .name = name, .table = 1, .flags = SYMBOL_PROPERTY, .has_member = true };
}

static void check_subsequent_property(Checker* me, Span name, Ty const* first_type, Ty const* type, Span first_name, ErrorCtx* errs);
static Ty const* with_optionality(Checker* me, Ty const* type, bool optional);

// A class merged with interfaces: tsc binds the instance members of all of them into one members table, in declaration
// order, so a class getter and an interface property of one name conflict (TS2300 on both, reported once)
static void check_class_interface_members(Checker* me, Symbol const* symbol, MemberTables* tables, ErrorCtx* errs) {
    tables->count = 0;
    member_tables_unindex(tables);
    for (u32 d = 0; d < symbol->decl_count; d++) {
        SymbolDecl const* decl = &symbol->decls[d];
        if (decl->kind == SYMBOL_DECL_CLASS) {
            ClassDecl const* class_decl = decl->node;
            for_each (i, vec_class_item, &class_decl->items) {
                if (i->tag == CLASS_ITEM_CONSTRUCTOR) {
                    for_each (arg, vec_function_arg, &i->constructor.func.args) {
                        Binding const* binding = &arg->initializer.binding;
                        if (!arg->is_parameter_property || binding->tag != BINDING_IDENTIFIER) continue;
                        declare_member(tables, checker_atom(me, span_sv(binding->content_span)), 0, MEMBER_PROPERTY, binding->content_span, errs);
                    }
                    continue;
                }
                if ((i->tag != CLASS_ITEM_FIELD && i->tag != CLASS_ITEM_METHOD) || class_item_is_static(i)) continue;
                if (span_valid(class_item_private_name(i))) continue;
                bool is_field = i->tag == CLASS_ITEM_FIELD;
                if (is_field ? i->field.computed_key != nullptr : i->method.computed_key != nullptr) continue;
                Atom const* name = checker_atom(me, cstr_sv(is_field ? &i->field.name_string : &i->method.name_string));
                u32 kind = is_field ? (i->field.is_auto_accessor ? MEMBER_GET | MEMBER_SET : MEMBER_PROPERTY) :
                    i->method.accessor == CLASS_ACCESSOR_GET ? MEMBER_GET :
                    i->method.accessor == CLASS_ACCESSOR_SET ? MEMBER_SET : MEMBER_METHOD;
                declare_member(tables, name, 0, kind, is_field ? i->field.name_span : i->method.name_span, errs);
            }
        } else if (decl->kind == SYMBOL_DECL_INTERFACE) {
            InterfaceDecl const* interface_decl = decl->node;
            for_each (i, vec_interface_member, &interface_decl->type.interface.members) {
                if (i->key.tag != INTERFACE_KEY_NAME) continue;
                Atom const* name = checker_atom(me, cstr_sv(&i->key.name_string));
                if ((i->is_method || i->key.name_is_member_function) && !i->accessor_kind) {
                    declare_member(tables, name, 0, MEMBER_METHOD, i->key.span, errs);
                    continue;
                }
                u32 kind = i->accessor_kind == 1 ? MEMBER_GET : i->accessor_kind == 2 ? MEMBER_SET : MEMBER_PROPERTY;
                declare_member(tables, name, 0, kind, i->key.span, errs);
            }
        }
    }
}

// tsc's binder and lateBindMember for a class declaration's members: its tables (instance, static with the merged
// namespaces' exports and `prototype`, private names, late-bound ones merged into the early ones) and a record per
// member declaration (in member order) with the symbol it is a declaration of. With `errs`, the conflicts are reported
// as they're found (TS2300)
static void declare_class_members(
    Checker* me, ClassDecl const* class_decl, MemberTables* tables, VecDupRecord* records_out, Symbol const** symbol_out,
    ErrorCtx* errs
) {
    // The static members share a table with the exports of the namespaces the class merges with, declared in binding
    // order: those before the class, the class's `prototype`, its members, those after
    Symbol const* symbol = class_decl->is_expression ? nullptr : class_decl->symbol;
    u32 class_index = 0;
    while (symbol && class_index < symbol->decl_count && symbol->decls[class_index].node != class_decl) class_index++;
    if (symbol && class_index == symbol->decl_count) symbol = nullptr;
    for (u32 d = 0; symbol && d < class_index; d++) {
        if (symbol->decls[d].kind == SYMBOL_DECL_NAMESPACE) declare_namespace_exports(me, tables, symbol, &symbol->decls[d], errs);
    }
    declare_prototype(me, tables, errs);
    VecDupRecord records = {0};
    for_each (i, vec_class_item, (VecClassItem*)&class_decl->items) {
        if (i->tag == CLASS_ITEM_CONSTRUCTOR) {
            for_each (arg, vec_function_arg, &i->constructor.func.args) {
                Binding const* binding = &arg->initializer.binding;
                if (!arg->is_parameter_property || binding->tag != BINDING_IDENTIFIER) continue;
                Atom const* name = checker_atom(me, span_sv(binding->content_span));
                i32 entry = declare_member(tables, name, 0, MEMBER_PROPERTY, binding->content_span, errs);
                vec_dup_record_push(&records, (DupRecord){ .name = name, .span = binding->content_span, .arg = arg, .entry = entry,
                    .check_kind = 1, .position = (u32)i_index });
            }
            continue;
        }
        if (i->tag != CLASS_ITEM_FIELD && i->tag != CLASS_ITEM_METHOD) continue;
        bool is_field = i->tag == CLASS_ITEM_FIELD;
        Span name_span = is_field ? i->field.name_span : i->method.name_span;
        Span private_name = class_item_private_name(i);
        bool is_static = class_item_is_static(i);
        Expr const* computed_key = is_field ? i->field.computed_key : i->method.computed_key;
        // (A bigint name is a symbol of its own outside the table, tsc's __missing: nothing to conflict with)
        if (!computed_key && span_is_bigint_name(name_span, is_field ? i->field.name_is_value : i->method.name_is_value)) continue;
        Atom const* name = span_valid(private_name) ? checker_atom(me, span_sv(private_name)) :
            ty_member_name(me, is_field ? &i->field.name_string : &i->method.name_string, computed_key);
        if (!name) continue;
        // (An auto-accessor binds as a get/set accessor pair: tsc's bindPropertyWorker, SymbolFlags.Accessor)
        bool auto_accessor = is_field && i->field.is_auto_accessor;
        u32 kind = is_field ? (auto_accessor ? MEMBER_GET | MEMBER_SET : MEMBER_PROPERTY) :
            i->method.accessor == CLASS_ACCESSOR_GET ? MEMBER_GET :
            i->method.accessor == CLASS_ACCESSOR_SET ? MEMBER_SET : MEMBER_METHOD;
        bool late_bound = computed_key && computed_key->tag != EXPR_LITERAL && !(computed_key->tag == EXPR_UNARY_OP &&
            computed_key->unary_op->expr.tag == EXPR_LITERAL);
        // (A name that isn't an entity name, `[f()]`, isn't late bindable: a symbol of its own, tsc's hasLateBindableName)
        if (late_bound && !expr_is_entity_name(computed_key)) continue;
        i32 entry;
        if (!late_bound) {
            // (A private name is its class's own, `__#1@#a` to tsc's binder: `#a` and `["#a"]` don't meet)
            entry = declare_member(tables, name, is_static + (span_valid(private_name) ? PRIVATE_TABLE : 0), kind, name_span, errs);
        } else {
            bool unique_symbol = ty_regular(me, ty_expr_type(me, computed_key))->flags & TYF_UNIQUE_ES_SYMBOL;
            entry = declare_late_member(tables, name, is_static, kind, name_span, unique_symbol, errs);
        }
        uint8_t check_kind = is_field ? (auto_accessor ? 2 : 1) : kind == MEMBER_METHOD ? 0 : 2;
        vec_dup_record_push(&records, (DupRecord){
            .name = name, .span = name_span, .member = i, .entry = entry, .check_kind = check_kind,
            .table = is_static + (span_valid(private_name) ? PRIVATE_TABLE : 0),
            .is_private = span_valid(private_name), .late = late_bound, .position = (u32)i_index,
        });
    }
    for (u32 d = class_index + 1; symbol && d < symbol->decl_count; d++) {
        if (symbol->decls[d].kind == SYMBOL_DECL_NAMESPACE) declare_namespace_exports(me, tables, symbol, &symbol->decls[d], errs);
    }
    // (A late-bound symbol merged into the early one of its name is its later declarations)
    merge_late_members(tables, &records, errs);
    *records_out = records;
    *symbol_out = symbol;
}

// An accessor with a computed name that isn't a literal: paired by the checker (ty_class_accessor_first()), its class
// recorded by the resolve walk
static ClassDecl const* accessor_late_class(ClassItem const* item) {
    return function_impl_rare(&item->method.func)->late_accessor_class;
}

static bool accessor_late(ClassItem const* item) {
    return item->method.computed_key && !computed_key_early_name(item->method.computed_key, nullptr);
}

ClassMemberSymbols const* ty_class_member_symbols(Checker* me, ClassDecl const* class_decl) {
    ClassMemberSymbols const* cached = ty_link_get(me, KEY_CLASS_MEMBER_SYMBOLS, class_decl);
    if (cached) return cached;
    MemberTables* tables = &(MemberTables){0};
    VecDupRecord records = {0};
    Symbol const* symbol;
    declare_class_members(me, class_decl, tables, &records, &symbol, nullptr);
    ClassMemberSymbols* ret = arena_new(&my arena, ClassMemberSymbols);
    u32 item_count = (u32)vec_class_item_size(&class_decl->items);
    i32* entries = arena_new_n(&my arena, i32, item_count ? item_count : 1);
    for (u32 i = 0; i < item_count; i++) entries[i] = CLASS_MEMBER_NONE;
    u32 param_count = 0;
    for_each (r, vec_dup_record, &records) param_count += !r->member;
    ClassMemberParam* params = arena_new_n(&my arena, ClassMemberParam, param_count ? param_count : 1);
    param_count = 0;
    for_each (r, vec_dup_record, &records) {
        if (r->member) entries[(ClassItem const*)r->member - class_decl->items.data] = r->entry;
        else params[param_count++] = (ClassMemberParam){ .arg = r->arg, .entry = r->entry };
    }
    u32* first_early = arena_new_n(&my arena, u32, tables->count ? tables->count : 1);
    u32* first_late = arena_new_n(&my arena, u32, tables->count ? tables->count : 1);
    bool* shadowed = arena_new_n(&my arena, bool, tables->count ? tables->count : 1);
    for (u32 e = 0; e < tables->count; e++) {
        first_early[e] = first_late[e] = UINT32_MAX;
        shadowed[e] = tables->entries[e].shadowed;
    }
    for_each (r, vec_dup_record, &records) {
        if (r->entry < 0) continue;
        u32* first = r->late ? &first_late[r->entry] : &first_early[r->entry];
        if (r->position < *first) *first = r->position;
    }
    // The accessors with computed names, paired by symbol (the written names' pairs are the binder's)
    i32* accessor_first = arena_new_n(&my arena, i32, item_count ? item_count : 1);
    i32* accessor_getter = arena_new_n(&my arena, i32, item_count ? item_count : 1);
    i32* accessor_setter = arena_new_n(&my arena, i32, item_count ? item_count : 1);
    i32* entry_accessor[3];
    for (u32 k = 0; k < 3; k++) {
        entry_accessor[k] = malloc(sizeof(i32) * (tables->count ? tables->count : 1));
        panic_if(!entry_accessor[k], "out of memory");
        for (u32 e = 0; e < tables->count; e++) entry_accessor[k][e] = -1;
    }
    for (u32 pass = 0; pass < 2; pass++) {
        for (u32 i = 0; i < item_count; i++) {
            ClassItem const* item = &class_decl->items.data[i];
            accessor_first[i] = accessor_getter[i] = accessor_setter[i] = -1;
            if (item->tag != CLASS_ITEM_METHOD || !item->method.accessor || !accessor_late(item)) continue;
            bool get = item->method.accessor == CLASS_ACCESSOR_GET;
            i32 e = entries[i];
            if (e < 0) {
                accessor_first[i] = (i32)i;
                (get ? accessor_getter : accessor_setter)[i] = (i32)i;
            } else if (pass == 0) {
                if (entry_accessor[0][e] < 0) entry_accessor[0][e] = (i32)i;
                if (entry_accessor[get ? 1 : 2][e] < 0) entry_accessor[get ? 1 : 2][e] = (i32)i;
            } else {
                accessor_first[i] = entry_accessor[0][e];
                accessor_getter[i] = entry_accessor[1][e];
                accessor_setter[i] = entry_accessor[2][e];
            }
        }
    }
    for (u32 k = 0; k < 3; k++) free(entry_accessor[k]);
    *ret = (ClassMemberSymbols){ .item_entry = entries, .item_count = item_count, .params = params,
        .param_count = param_count, .entry_count = tables->count, .first_early = first_early, .first_late = first_late,
        .shadowed = shadowed, .accessor_first = accessor_first, .accessor_getter = accessor_getter,
        .accessor_setter = accessor_setter };
    vec_dup_record_drop(&records);
    free(tables->entries);
    member_tables_unindex(tables);
    ty_link_set(me, KEY_CLASS_MEMBER_SYMBOLS, class_decl, ret);
    return ret;
}

ClassItem const* ty_class_accessor_first(Checker* me, ClassItem const* item) {
    ClassDecl const* class_decl = accessor_late_class(item);
    if (!class_decl) return item->method.accessor_first;
    i32 at = ty_class_member_symbols(me, class_decl)->accessor_first[item - class_decl->items.data];
    return at >= 0 ? &class_decl->items.data[at] : item;
}

ClassItem const* ty_class_accessor_getter(Checker* me, ClassItem const* item) {
    ClassDecl const* class_decl = accessor_late_class(item);
    if (!class_decl) return item->method.accessor_getter;
    i32 at = ty_class_member_symbols(me, class_decl)->accessor_getter[item - class_decl->items.data];
    return at >= 0 ? &class_decl->items.data[at] : nullptr;
}

ClassItem const* ty_class_accessor_setter(Checker* me, ClassDecl const* class_decl, ClassItem const* item) {
    if (item->method.accessor == CLASS_ACCESSOR_SET) return item;
    if (!accessor_late_class(item)) {
        ClassItem const* getter = item->method.accessor_getter;
        for_each (i, vec_class_item, (VecClassItem*)&class_decl->items) {
            if (i->tag == CLASS_ITEM_METHOD && i->method.accessor == CLASS_ACCESSOR_SET && i->method.accessor_getter == getter) return i;
        }
        return nullptr;
    }
    class_decl = accessor_late_class(item);
    i32 at = ty_class_member_symbols(me, class_decl)->accessor_setter[item - class_decl->items.data];
    return at >= 0 ? &class_decl->items.data[at] : nullptr;
}

void ty_check_class_duplicate_members(Checker* me, ClassDecl* class_decl, ErrorCtx* errs) {
    MemberTables* tables = &(MemberTables){0};
    VecDupRecord records;
    Symbol const* symbol;
    declare_class_members(me, class_decl, tables, &records, &symbol, errs);
    // A field declared again (its symbol's later declaration) has the type of its symbol's first one (per entry, its
    // first record)
    DupRecord const** first_records = dup_first_records(tables, &records);
    for_each (r, vec_dup_record, &records) {
        ClassItem* item = (ClassItem*)r->member;
        if (r->entry < 0 || !item || item->tag != CLASS_ITEM_FIELD) continue;
        DupRecord const* first = first_records[r->entry];
        if (first == r || !first->member) continue;
        ClassItem* first_item = (ClassItem*)first->member;
        // (A field's type has undefined when it's optional: addOptionality of its declaration)
        Ty const* first_type = class_item_get_type(first_item);
        if (first_item->tag == CLASS_ITEM_FIELD) first_type = with_optionality(me, first_type, first_item->field.is_optional);
        check_subsequent_property(me, item->field.name_span, first_type,
            with_optionality(me, class_item_get_type(item), item->field.is_optional), first->span, errs);
    }
    free(first_records);
    check_duplicate_declarations(tables, &records, 0, true, errs);
    vec_dup_record_drop(&records);
    if (symbol && symbol->flags & SYMBOL_INTERFACE) check_class_interface_members(me, symbol, tables, errs);
    free(tables->entries);
    member_tables_unindex(tables);
}

// TS2717: a property declared again (a duplicate class field, a property of a merged interface) has the type of the
// first declaration of its name (tsc's checkVariableLikeDeclaration for a property's secondary declarations)
static void check_subsequent_property(Checker* me, Span name, Ty const* first_type, Ty const* type, Span first_name, ErrorCtx* errs) {
    Ty const* circular = ty_intrinsic(me, TY_CIRCULAR);
    if ((first_type->flags & TYF_ERROR) || (type->flags & TYF_ERROR) || first_type == circular || type == circular ||
        ty_is_related(me, first_type, type, TY_RELATION_IDENTITY)
    ) {
        return;
    }
    // HACK: a `unique symbol` is one per declaration in tttt, one per (merged) property symbol in tsc
    if ((first_type->flags & TYF_UNIQUE_ES_SYMBOL) && (type->flags & TYF_UNIQUE_ES_SYMBOL)) return;
    cstr first_str = ty_to_cstr(me, first_type);
    cstr type_str = ty_to_cstr(me, type);
    Error* e = error_ctx_error_d(errs, name, 2717, "Subsequent property declarations must have the same type.  Property '",
        name, "' must be of type '", first_str, "', but here has type '", type_str, "'.");
    error_add_related_d(e, first_name, 6203, "'", name, "' was also declared here.");
    c_drop(cstr, &first_str, &type_str);
}

// The type an interface member declares its property with
static Ty const* member_declared_type(Checker* me, InterfaceMember const* member) {
    Ty const* type = ty_of_node(&member->type);
    if (member->optional && compiler_options()->strict_null_checks && !(type->flags & TYF_ANY)) {
        Ty const* types[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
        type = ty_union(me, types, 2, nullptr, (TyList){0});
    }
    return type;
}

// A declaration of a property, as far as tsc's areDeclarationFlagsIdentical goes
typedef struct PropertyDeclEntry {
    csview name;
    Span span;
    bool optional;
    // A method of the name (`m(): T` then `m?: T`): it may be the property symbol's first declaration, which the others
    // are compared with, but isn't variable-like itself (tsc's checkVariableLikeDeclaration: isVariableLike)
    bool method;
    // An accessor (get, set, not an auto accessor): it joins the property symbol and may be its first declaration,
    // compared with the others, but isn't variable-like itself either
    bool accessor;
    uint8_t modifiers; // PROPERTY_MOD_*
} PropertyDeclEntry;

enum { PROPERTY_MOD_PRIVATE = 1, PROPERTY_MOD_PROTECTED = 2, PROPERTY_MOD_ABSTRACT = 4, PROPERTY_MOD_READONLY = 8 };

#define g_key PropertyDeclEntry, property_decl_entry
#include <cleat/generic/vec.h>

static uint8_t access_modifier_bits(AccessModifier access) {
    return access == ACCESS_MODIFIER_PRIVATE ? PROPERTY_MOD_PRIVATE : access == ACCESS_MODIFIER_PROTECTED ? PROPERTY_MOD_PROTECTED : 0;
}

// The instance properties a class declares: its (non-static) fields and parameter properties, in binding order. With
// `late`, those named by an entity name instead (late bound: after the others of every declaration, see
// resolve_declared_members())
static void collect_class_property_decls(Checker* me, ClassDecl const* class_decl, bool late, VecPropertyDeclEntry* out) {
    for_each (i, vec_class_item, &class_decl->items) {
        if (late) {
            Expr const* key = i->tag == CLASS_ITEM_FIELD && !i->field.is_static ? i->field.computed_key : nullptr;
            if (!key || computed_key_early_name(key, nullptr) || expr_is_perenthisized(key) || !expr_is_entity_name(key)) continue;
            Atom const* name = ty_member_name(me, &i->field.name_string, key);
            if (!name) continue;
            vec_property_decl_entry_push(out, (PropertyDeclEntry){
                .name = atom_sv(name), .span = i->field.name_span, .optional = i->field.is_optional,
                .modifiers = access_modifier_bits(i->access) | (i->field.is_readonly ? PROPERTY_MOD_READONLY : 0) |
                    (i->field.is_abstract ? PROPERTY_MOD_ABSTRACT : 0),
            });
            continue;
        }
        if (i->tag == CLASS_ITEM_CONSTRUCTOR) {
            for_each (arg, vec_function_arg, &i->constructor.func.args) {
                Binding const* binding = &arg->initializer.binding;
                if (!arg->is_parameter_property || binding->tag != BINDING_IDENTIFIER) continue;
                vec_property_decl_entry_push(out, (PropertyDeclEntry){
                    .name = span_sv(binding->content_span), .span = binding->content_span, .optional = arg->explicit_optional,
                    .modifiers = access_modifier_bits(arg->access) | (arg->is_readonly_property ? PROPERTY_MOD_READONLY : 0),
                });
            }
        }
        if (i->tag == CLASS_ITEM_METHOD && i->method.accessor && !i->method.is_static && !i->method.computed_key) {
            vec_property_decl_entry_push(out, (PropertyDeclEntry){
                .name = cstr_sv(&i->method.name_string), .span = i->method.name_span, .accessor = true,
                .modifiers = access_modifier_bits(i->access) | (i->method.is_abstract ? PROPERTY_MOD_ABSTRACT : 0),
            });
            continue;
        }
        if (i->tag != CLASS_ITEM_FIELD || i->field.is_static || (i->field.computed_key && !computed_key_early_name(i->field.computed_key, nullptr))) continue;
        vec_property_decl_entry_push(out, (PropertyDeclEntry){
            .name = cstr_sv(&i->field.name_string), .span = i->field.name_span, .optional = i->field.is_optional,
            .modifiers = access_modifier_bits(i->access) | (i->field.is_readonly ? PROPERTY_MOD_READONLY : 0) |
                (i->field.is_abstract ? PROPERTY_MOD_ABSTRACT : 0),
        });
    }
}

static void collect_interface_property_decls(Checker* me, VecInterfaceMember const* members, bool late, VecPropertyDeclEntry* out) {
    for_each (i, vec_interface_member, members) {
        if (late) {
            if (i->key.tag != INTERFACE_KEY_EXPR || !i->key.expr || !expr_is_entity_name(i->key.expr) || i->accessor_kind) continue;
            Atom const* name = ty_literal_name(me, ty_expr_type(me, i->key.expr));
            if (!name) continue;
            bool method = i->is_method && i->type.tag == TYPE_CALLABLE;
            vec_property_decl_entry_push(out, (PropertyDeclEntry){
                .name = atom_sv(name), .span = i->key.span, .optional = i->optional, .method = method,
                .modifiers = !method && i->readonly ? PROPERTY_MOD_READONLY : 0,
            });
            continue;
        }
        if (i->key.tag != INTERFACE_KEY_NAME) continue;
        if ((i->is_method || i->key.name_is_member_function) && !i->accessor_kind) {
            vec_property_decl_entry_push(out, (PropertyDeclEntry){
                .name = cstr_sv(&i->key.name_string), .span = i->key.span, .optional = i->optional, .method = true,
            });
            continue;
        }
        // (Accessors aren't variable-like: tsc's checkVariableLikeDeclaration compares property declarations only, with
        // the symbol's valueDeclaration, which can be an accessor)
        vec_property_decl_entry_push(out, (PropertyDeclEntry){
            .name = cstr_sv(&i->key.name_string), .span = i->key.span, .optional = i->optional, .accessor = i->accessor != nullptr,
            .modifiers = i->readonly && !i->accessor ? PROPERTY_MOD_READONLY : 0,
        });
    }
}

// TS2687: tsc's checkVariableLikeDeclaration, for the property declarations of one member symbol: the first one
// (the valueDeclaration) against every other one, every other one against the first. An accessor of the name is only
// compared with, as the first one
static void check_property_modifiers(VecPropertyDeclEntry const* decls, ErrorCtx* errs) {
    isize count = vec_property_decl_entry_size(decls);
    if (!count) return;
    PropertyDeclEntry const* entries = vec_property_decl_entry_at(decls, 0);
    // The declarations merged into the symbol: tsc's declareSymbol gives a declaration that conflicts with the symbol in
    // the table (TS2300) a symbol of its own. A method first makes every property alone, a method later is only alone
    // itself. Per declaration the first of its name, and per first whether a later property differs from it
    isize* first = malloc(sizeof(isize) * (size_t)count);
    bool* later_differs = calloc((size_t)count, 1);
    panic_if(!first || !later_differs, "out of memory");
    NameIndex index = name_index_make(count);
    for (isize i = 0; i < count; i++) {
        u64 slot = name_index_slot(&index, symbol_name_hash(entries[i].name));
        while (index.slots[slot] >= 0 && !csview_equals_sv(entries[index.slots[slot]].name, entries[i].name)) {
            slot = name_index_next(&index, slot);
        }
        if (index.slots[slot] < 0) index.slots[slot] = i;
        first[i] = index.slots[slot];
    }
    name_index_drop(&index);
    for (isize i = 0; i < count; i++) {
        PropertyDeclEntry const* decl = &entries[i];
        PropertyDeclEntry const* head = &entries[first[i]];
        if (first[i] != i && !decl->method && !decl->accessor && !(decl->optional == head->optional && decl->modifiers == head->modifiers)) {
            later_differs[first[i]] = true;
        }
    }
    for (isize i = 0; i < count; i++) {
        PropertyDeclEntry const* decl = &entries[i];
        // (A lib's declaration too when lib files are checked: tsc checks it there, the lib side of the conflict)
        if (!span_valid(decl->span) || (span_source(decl->span)->is_stdlib && !lib_files_checked()) || decl->method || decl->accessor) continue;
        PropertyDeclEntry const* head = &entries[first[i]];
        if (head->method) continue;
        // The first one against every other one, every other one against the first
        bool differs = first[i] == i ? later_differs[i] :
            !(head->optional == decl->optional && head->modifiers == decl->modifiers);
        if (differs) {
            error_ctx_error_s(errs, decl->span, 2687,
                cstr_d("All declarations of '", decl->span, "' must have identical modifiers."));
        }
    }
    free(first);
    free(later_differs);
}

void ty_check_property_modifiers(Checker* me, Symbol const* symbol, ClassDecl const* class_decl,
    VecInterfaceMember const* type_literal, ErrorCtx* errs
) {
    VecPropertyDeclEntry decls = {0};
    for (int late = 0; late < 2; late++) {
        if (symbol) {
            for (u32 d = 0; d < symbol->decl_count; d++) {
                SymbolDecl const* decl = &symbol->decls[d];
                if (decl->kind == SYMBOL_DECL_CLASS) collect_class_property_decls(me, decl->node, late, &decls);
                if (decl->kind == SYMBOL_DECL_INTERFACE) {
                    collect_interface_property_decls(me, &((InterfaceDecl const*)decl->node)->type.interface.members, late, &decls);
                }
            }
        } else if (class_decl) {
            collect_class_property_decls(me, class_decl, late, &decls);
        } else if (type_literal) {
            collect_interface_property_decls(me, type_literal, late, &decls);
        }
    }
    check_property_modifiers(&decls, errs);
    vec_property_decl_entry_drop(&decls);
}

// The name a type member binds: written, or what a computed key's type names (late bound, an entity name). Null if none
static Atom const* type_member_name(Checker* me, InterfaceMember const* member) {
    if (member->key.tag == INTERFACE_KEY_NAME) return checker_atom(me, cstr_sv(&member->key.name_string));
    if (member->key.tag != INTERFACE_KEY_EXPR || !member->key.expr) return nullptr;
    Expr const* key = member->key.expr;
    if (key->tag != EXPR_LITERAL && !expr_is_entity_name(key)) return nullptr;
    return ty_literal_name(me, ty_expr_type(me, key));
}

// The declared member of a class's (or interface's) declared type, of its declarations' property symbol
static Prop const* declared_member_root(Checker* me, Ty const* declared, Atom const* name) {
    Prop const* prop = ty_get_prop(me, declared, name);
    while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target;
    return prop && prop->decl->decl_span_count ? prop : nullptr;
}

static bool prop_declared_by(Prop const* prop, Span span) {
    for (u32 i = 0; i < prop->decl->decl_span_count; i++) {
        if (span_eq(prop->decl->decl_spans[i], span)) return true;
    }
    return false;
}

static bool is_class_item_span(ClassDecl const* class_decl, Span span) {
    for_each (item, vec_class_item, &class_decl->items) {
        if (span_eq(item->span, span)) return true;
    }
    return false;
}

// A type with undefined when its declaration is optional (tsc's addOptionality, under strictNullChecks)
static Ty const* with_optionality(Checker* me, Ty const* type, bool optional) {
    if (!optional || !compiler_options()->strict_null_checks || (type->flags & TYF_ANY)) return type;
    Ty const* types[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
    return ty_union(me, types, 2, nullptr, (TyList){0});
}

// The type of a property symbol whose first declaration is a class item: its declaration's, optionality included
// (tsc's getTypeOfVariableOrParameterOrProperty of the valueDeclaration)
static Ty const* class_first_declaration_type(Checker* me, ClassDecl const* class_decl, Prop const* root) {
    for_each (item, vec_class_item, &class_decl->items) {
        if (item->tag == CLASS_ITEM_FIELD && span_eq(item->span, root->decl->decl_spans[0])) {
            return with_optionality(me, prop_type(me, root), item->field.is_optional);
        }
    }
    return prop_type(me, root);
}

// The same for a first declaration in one of the interfaces merged with the class
static Ty const* interface_first_declaration_type(Checker* me, Symbol const* symbol, Prop const* root) {
    for (u32 d = 0; d < symbol->decl_count; d++) {
        if (symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface_decl = symbol->decls[d].node;
        for_each (i, vec_interface_member, &interface_decl->type.interface.members) {
            if (span_valid(i->declaration) && span_eq(i->declaration, root->decl->decl_spans[0]) && !i->is_method &&
                i->key.tag == INTERFACE_KEY_NAME && !i->key.name_is_member_function && !i->accessor
            ) {
                return member_declared_type(me, i);
            }
        }
    }
    return prop_type(me, root);
}

// The type of the property symbol an instance field declares (tsc's getTypeOfSymbol(getSymbolOfDeclaration(field))):
// its first declaration's, which may be an interface's merged with the class or another field of its name
Ty const* ty_class_field_symbol_type(Checker* me, ClassDecl const* class_decl, ClassItem const* field) {
    Ty const* own = with_optionality(me, class_item_get_type((ClassItem*)field), field->field.is_optional);
    Symbol const* symbol = class_decl->symbol;
    // (An auto accessor's symbol is an accessor, typed by the accessor: see ty_check_class_interface_property_types())
    if (!symbol || field->field.computed_key || field->field.is_static || span_valid(class_item_private_name(field)) ||
        field->field.is_auto_accessor) {
        return own;
    }
    Ty const* declared = ty_declared_type_of(me, symbol);
    if (!(declared->flags & TYF_OBJECT)) return own;
    Prop const* root = declared_member_root(me, declared, checker_atom(me, cstr_sv(&field->field.name_string)));
    if (!root || !prop_declared_by(root, field->span)) return own;
    // (A symbol with accessors is typed by them, whatever came first: getTypeOfAccessors)
    if (ty_prop_is_accessor(root)) return prop_type(me, root);
    if (span_eq(root->decl->decl_spans[0], field->span)) return own;
    return is_class_item_span(class_decl, root->decl->decl_spans[0]) ? class_first_declaration_type(me, class_decl, root) :
        interface_first_declaration_type(me, symbol, root);
}

// TS2717 across a class and the interfaces it merges with: a property signature after a class member of its name (a
// field, method or accessor), or a field after an interface member, is a secondary declaration of the one property
// symbol, whose type is its first declaration's (tsc's checkVariableLikeDeclaration). Declarations of one kind are
// checked by the class and the interface themselves
void ty_check_class_interface_property_types(Checker* me, Symbol const* symbol, ErrorCtx* errs) {
    ClassDecl const* class_decl = nullptr;
    for (u32 d = 0; d < symbol->decl_count && !class_decl; d++) {
        if (symbol->decls[d].kind == SYMBOL_DECL_CLASS) class_decl = symbol->decls[d].node;
    }
    if (!class_decl) return;
    Ty const* declared = ty_declared_type_of(me, symbol);
    if (!(declared->flags & TYF_OBJECT)) return;
    for (u32 d = 0; d < symbol->decl_count; d++) {
        if (symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface_decl = symbol->decls[d].node;
        if (span_source(interface_decl->name) && span_source(interface_decl->name)->is_stdlib) continue;
        for_each (i, vec_interface_member, &interface_decl->type.interface.members) {
            if (i->key.tag != INTERFACE_KEY_NAME || i->is_method || i->key.name_is_member_function || i->accessor) continue;
            if (!span_valid(i->declaration)) continue;
            Prop const* root = declared_member_root(me, declared, checker_atom(me, cstr_sv(&i->key.name_string)));
            if (!root || !is_class_item_span(class_decl, root->decl->decl_spans[0]) || !prop_declared_by(root, i->declaration)) {
                continue;
            }
            check_subsequent_property(me, i->key.span, class_first_declaration_type(me, class_decl, root), member_declared_type(me, i),
                root->decl->name_span, errs);
        }
    }
    for_each (field, vec_class_item, &class_decl->items) {
        if (field->tag != CLASS_ITEM_FIELD || field->field.is_static || field->field.computed_key) continue;
        // An auto accessor makes the symbol an accessor, whose type is the accessor's own (tsc's getTypeOfSymbol takes
        // getTypeOfAccessors before the valueDeclaration's type): never different from the field's
        if (span_valid(class_item_private_name(field)) || field->field.is_auto_accessor) continue;
        Prop const* root = declared_member_root(me, declared, checker_atom(me, cstr_sv(&field->field.name_string)));
        if (!root || is_class_item_span(class_decl, root->decl->decl_spans[0]) || !prop_declared_by(root, field->span)) continue;
        check_subsequent_property(me, field->field.name_span, interface_first_declaration_type(me, symbol, root),
            with_optionality(me, class_item_get_type(field), field->field.is_optional), root->decl->name_span, errs);
    }
}

// tsc's checkVariableLikeDeclaration for a parameter property that isn't its property's first declaration (the same
// parameter of another constructor declaration, an overload's, or a field before it): node.symbol is the class
// property, whose type is its first declaration's (TS2403, "variable": a parameter isn't a property declaration)
void ty_check_parameter_property_redeclaration(Checker* me, Symbol const* class_symbol, FunctionArg const* arg, ErrorCtx* errs) {
    Binding const* binding = &arg->initializer.binding;
    if (!class_symbol || binding->tag != BINDING_IDENTIFIER || !binding->content_span.size) return;
    Ty const* declared = ty_declared_type_of(me, class_symbol);
    if (!(declared->flags & TYF_OBJECT)) return;
    Prop const* root = declared_member_root(me, declared, checker_atom(me, span_sv(binding->content_span)));
    if (!root || span_eq(root->decl->decl_spans[0], arg->span) || !prop_declared_by(root, arg->span)) return;
    Ty const* type = prop_type(me, root);
    Ty const* own = ty_binding_type(me, binding, (LowerCtx){ .generic = true });
    Ty const* circular = ty_intrinsic(me, TY_CIRCULAR);
    if ((type->flags & TYF_ERROR) || (own->flags & TYF_ERROR) || own == circular || type == circular ||
        ty_is_related(me, type, own, TY_RELATION_IDENTITY)
    ) {
        return;
    }
    cstr type_str = ty_to_cstr(me, type);
    cstr own_str = ty_to_cstr(me, own);
    Error* e = error_ctx_error_d(errs, binding->content_span, 2403,
        "Subsequent variable declarations must have the same type.  Variable '", binding->content_span, "' must be of type '",
        type_str, "', but here has type '", own_str, "'.");
    // (A parameter's declaration starts at its modifiers)
    Span first = root->decl->parameter_property ? root->decl->decl_spans[0] : root->decl->name_span;
    error_add_related_d(e, first, 6203, "'", binding->content_span, "' was also declared here.");
    c_drop(cstr, &type_str, &own_str);
}

void ty_check_object_type_duplicate_members(Checker* me, VecInterfaceMember const* const* lists, u32 list_count, ErrorCtx* errs) {
    MemberTables* tables = &(MemberTables){0};
    // The members of every declaration in one table (the merged symbol's), in declaration order
    VecDupRecord records = {0};
    for (u32 l = 0; l < list_count; l++) {
        for_each (i, vec_interface_member, lists[l]) {
            if (i->key.tag != INTERFACE_KEY_NAME && i->key.tag != INTERFACE_KEY_EXPR) continue;
            if (i->key.tag == INTERFACE_KEY_NAME && span_is_bigint_name(i->key.span, i->key.name_is_value)) continue;
            Atom const* name = type_member_name(me, i);
            if (!name) continue;
            Expr const* key = i->key.tag == INTERFACE_KEY_EXPR ? i->key.expr : nullptr;
            bool late_bound = key && key->tag != EXPR_LITERAL && !(key->tag == EXPR_UNARY_OP && key->unary_op->expr.tag == EXPR_LITERAL);
            bool unique_symbol = late_bound && (ty_regular(me, ty_expr_type(me, key))->flags & TYF_UNIQUE_ES_SYMBOL);
            bool is_method = i->is_method || (i->key.tag == INTERFACE_KEY_NAME && i->key.name_is_member_function);
            {
                u32 kind = i->accessor_kind == 1 ? MEMBER_GET : i->accessor_kind == 2 ? MEMBER_SET : is_method ? MEMBER_METHOD :
                    MEMBER_PROPERTY;
                Span at = i->key.span;
                i32 entry = late_bound ? declare_late_member(tables, name, 0, kind, at, unique_symbol, errs) :
                    declare_member(tables, name, 0, kind, at, errs);
                uint8_t check_kind = kind == MEMBER_METHOD ? 0 : kind == MEMBER_PROPERTY ? 1 : 2;
                vec_dup_record_push(&records, (DupRecord){
                    .name = name, .span = at, .member = i, .entry = entry, .check_kind = check_kind, .list = l, .late = late_bound,
                });
            }
        }
    }
    // (A late-bound symbol merged into the early one of its name is its later declarations)
    merge_late_members(tables, &records, errs);
    // A property signature declared again (its symbol's later declaration, in any declaration) has the type of its
    // symbol's first one. Per entry (symbol): its first record, and whether any of its declarations is optional
    DupRecord const** first_records = dup_first_records(tables, &records);
    bool* optional_entries = calloc(tables->count ? tables->count : 1, 1);
    panic_if(!optional_entries, "out of memory");
    for_each (r, vec_dup_record, &records) {
        if (r->entry >= 0) optional_entries[r->entry] |= ((InterfaceMember const*)r->member)->optional;
    }
    for_each (r, vec_dup_record, &records) {
        InterfaceMember const* i = r->member;
        if (r->entry < 0 || r->check_kind != 1 || i->accessor_kind) continue;
        InterfaceMember const* first = first_records[r->entry]->member;
        if (first == i || !span_valid(first->key.span)) continue;
        Ty const* first_type = member_declared_type(me, first);
        // (The property symbol is optional if any declaration is: its type then has undefined, tsc's
        // getTypeOfSymbol of the merged symbol, `(() => string) | undefined` for `bold(): string; bold?: string`)
        bool any_optional = optional_entries[r->entry];
        // (Only a method's: a property's type is its own declaration's, `x: string` then `x?: string` is TS2717)
        bool first_method = first->is_method || first->key.name_is_member_function;
        if (any_optional && first_method && !first->optional && compiler_options()->strict_null_checks && !(first_type->flags & TYF_ANY)) {
            Ty const* types[2] = { first_type, ty_intrinsic(me, TY_UNDEFINED) };
            first_type = ty_union(me, types, 2, nullptr, (TyList){0});
        }
        check_subsequent_property(me, i->key.span, first_type, member_declared_type(me, i), first->key.span, errs);
    }
    free(first_records);
    free(optional_entries);
    for (u32 l = 0; l < list_count; l++) check_duplicate_declarations(tables, &records, l, false, errs);
    vec_dup_record_drop(&records);
    free(tables->entries);
    member_tables_unindex(tables);
}

void ty_check_computed_name(Checker* me, Expr const* key, Span span, bool in_type, ErrorCtx* errs) {
    // `[k in T]` where a mapped type was meant is reported as that (tsc gives it the error type)
    if (in_type && key->tag == EXPR_BINARY_OP && key->binary_op->op == BINARY_OP_IN) return;
    Ty const* type = ty_expr_type(me, key);
    bool nullable = type->kind != TY_UNION && (type->flags & TYF_NULLABLE);
    u32 key_like = TYF_ANY | TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM | TYF_ES_SYMBOL_LIKE;
    if (!nullable && type->kind != TY_UNION && (type->flags & key_like)) return;
    if (!nullable) {
        Ty const* keys[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
        if (ty_is_related(me, type, ty_union(me, keys, 3, nullptr, (TyList){0}), TY_RELATION_ASSIGNABLE)) return;
    }
    error_ctx_error_d(errs, span, 2464, "A computed property name must be of type 'string', 'number', 'symbol', or 'any'.");
}

// Whether the type has a call signature a function with that many required parameters fits (tsc's
// getContextualCallSignature: not isAritySmaller)
bool ty_has_call_signature_for_arity(Checker* me, Ty const* type, u32 required) {
    // (any has no signatures either: tsgo's getContextualCallSignature, TS8030 for `@type {any}`)
    if (!(type->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, type);
    // (Several that fit: only if they have the same parameters, getIntersectedSignatures, which overloads don't)
    Signature const* applicable = nullptr;
    for (u32 i = 0; i < members->call_count; i++) {
        Signature const* sig = members->call[i];
        signature_resolve(me, sig);
        bool rest = sig->param_count && sig->params[sig->param_count - 1].rest;
        if (!rest && sig->param_count < required) continue;
        if (!applicable) {
            applicable = sig;
            continue;
        }
        if (applicable->param_count != sig->param_count) return false;
        for (u32 p = 0; p < sig->param_count; p++) {
            if (applicable->params[p].type != sig->params[p].type) return false;
        }
    }
    return applicable != nullptr;
}

Signature const* ty_function_signature(Checker* me, FunctionImpl const* function) {
    Members const* members = ty_members(me, ty_function_type(me, function));
    if (members->call_count) return members->call[0];
    return members->construct_count ? members->construct[0] : nullptr;
}

void ty_check_overloads_compatible(
    Checker* me, FunctionImpl const* implementation, Span implementation_name, FunctionImpl const* const* overloads,
    Span const* names, u32 count, ErrorCtx* errs
) {
    Signature const* impl = function_impl_rare(implementation)->jsdoc_overload_count ? ty_implementation_signature(me, implementation) :
        ty_function_signature(me, implementation);
    if (!impl) return;
    for (u32 i = 0; i < count; i++) {
        Signature const* overload = ty_function_signature(me, overloads[i]);
        if (!overload || ty_implementation_compatible_with_overload(me, impl, overload)) continue;
        Error* e = error_ctx_error_d(errs, names[i], 2394, "This overload signature is not compatible with its implementation signature.");
        error_add_related_d(e, implementation_name, 2750, "The implementation signature is declared here.");
        break;
    }
}

// Index constraints

// tsc's isApplicableIndexType
static bool index_applies(Checker* me, Ty const* key, Ty const* index_key) {
    if (ty_is_related(me, key, index_key, TY_RELATION_ASSIGNABLE)) return true;
    if (index_key == ty_intrinsic(me, TY_STRING)) return ty_is_related(me, key, ty_intrinsic(me, TY_NUMBER), TY_RELATION_ASSIGNABLE);
    return index_key == ty_intrinsic(me, TY_NUMBER) && (key->flags & TYF_STRING_LITERAL) && key->literal.string &&
        ty_is_numeric_name(key->literal.string);
}

// Whether the span is part of one of the declarations of `owner` (all of a type literal's members are its own)
static bool is_local_span(Symbol const* owner, Span span) {
    if (!span_valid(span)) return false;
    if (!owner) return true;
    for (u32 i = 0; i < owner->decl_count; i++) {
        Span full = owner->decls[i].full;
        if (span_source(span) == span_source(full) && span_start_ptr(span) >= span_start_ptr(full) && span_end_ptr(span) <= span_end_ptr(full)) {
            return true;
        }
    }
    return false;
}

// Whether a base of an interface has both (then the base reported it)
static bool base_has_both(Checker* me, Ty const* const* bases, u32 base_count, Atom const* prop, Ty const* key, Ty const* index_key) {
    for (u32 b = 0; b < base_count; b++) {
        Members const* members = ty_members(me, ty_apparent_type(me, bases[b]));
        bool has_first = false, has_index = false;
        if (prop) {
            has_first = ty_get_prop(me, bases[b], prop) != nullptr;
        } else {
            for (u32 i = 0; i < members->index_count; i++) has_first |= members->index[i].key == key;
        }
        for (u32 i = 0; i < members->index_count; i++) has_index |= members->index[i].key == index_key;
        if (has_first && has_index) return true;
    }
    return false;
}

// Where tsc reports a property: the name of a property or method declaration, a whole method signature
static Span prop_error_span(Prop const* prop) {
    while (prop->target) prop = prop->target;
    if ((prop->flags & PROP_METHOD) && !prop->decl->function && prop->decl->decl_span_count) {
        return span_with_member_separator(prop->decl->decl_spans[0]);
    }
    return prop->decl->name_span;
}

// tsc's symbolToString of a property: as written if that was a string literal
void ty_check_index_constraints(
    Checker* me, Ty const* type, Symbol const* owner, bool is_static, Ty const* const* bases, u32 base_count, Span name,
    ErrorCtx* errs
) {
    Members const* members = ty_members(me, type);
    if (!members->index_count) return;
    bool interface = owner && !(owner->flags & SYMBOL_CLASS);
    for (u32 p = 0; p < members->prop_count; p++) {
        Prop const* prop = members->props[p];
        Prop const* root = prop;
        while (root->target) root = root->target;
        if (span_valid(root->decl->name_span) && root->decl->name_span.buf[0] == '#') continue;
        if (is_static && csview_equals(atom_sv(prop->name), "prototype")) continue;
        Ty const* key = ty_prop_name_literal(me, prop->name);
        bool local_prop = is_local_span(owner, root->decl->name_span);
        Ty const* prop_ty = nullptr;
        for (u32 i = 0; i < members->index_count; i++) {
            IndexInfo const* info = &members->index[i];
            if (!index_applies(me, key, info->key)) continue;
            Span at = local_prop ? prop_error_span(prop) :
                is_local_span(owner, info->decl_span) ? span_with_member_separator(info->decl_span) :
                interface && !base_has_both(me, bases, base_count, prop->name, nullptr, info->key) ? name : (Span){0};
            if (!span_valid(at)) continue;
            // (An optional property's type has undefined, but not exactOptionalPropertyTypes' missing type, which tsc's
            // checkIndexConstraintForProperty doesn't see: getNonMissingTypeOfSymbol)
            if (!prop_ty) prop_ty = ty_remove_missing(me, prop_type(me, prop), prop->flags & PROP_OPTIONAL);
            if (ty_is_related(me, prop_ty, info->value, TY_RELATION_ASSIGNABLE)) continue;
            cstr prop_str = ty_prop_written_name(me, prop);
            cstr type_str = ty_to_cstr(me, prop_ty);
            cstr key_str = ty_to_cstr(me, info->key);
            cstr value_str = ty_to_cstr(me, info->value);
            // (A lib's declarations: dropped unless lib files are checked)
            Error* e = error_ctx_error_s(errs, at, 2411, cstr_d("Property '",
                prop_str, "' of type '", type_str, "' is not assignable to '", key_str, "' index type '", value_str, "'."));
            // (Reported elsewhere than at a property with a computed name: where it is declared. tsc also does it for
            // JavaScript assignment declarations, TODO)
            bool computed = span_valid(root->decl->name_span) && root->decl->name_span.buf[0] == '[';
            if (!local_prop && computed) error_add_related_d(e, prop_error_span(prop), 2728, "'", prop_str, "' is declared here.");
            c_drop(cstr, &prop_str, &type_str, &key_str, &value_str);
        }
    }
    // A class's members whose computed names aren't known, by the type of the name
    ClassDecl* class_decl = owner ? (ClassDecl*)symbol_find_node(owner, SYMBOL_DECL_CLASS) : nullptr;
    VecClassItem empty = {0};
    for_each (item, vec_class_item, class_decl ? &class_decl->items : &empty) {
        bool is_method = item->tag == CLASS_ITEM_METHOD;
        if ((!is_method && item->tag != CLASS_ITEM_FIELD) || class_item_is_static(item) != is_static) continue;
        Expr const* computed = is_method ? item->method.computed_key : item->field.computed_key;
        if (!computed || ty_member_name(me, is_method ? &item->method.name_string : &item->field.name_string, computed)) continue;
        if (is_method && item->method.accessor == CLASS_ACCESSOR_SET && ty_class_accessor_first(me, item) != item) continue;
        Span item_name = is_method ? item->method.name_span : item->field.name_span;
        Ty const* key = ty_expr_type(me, computed);
        Ty const* item_ty = nullptr;
        for (u32 i = 0; i < members->index_count; i++) {
            IndexInfo const* info = &members->index[i];
            if (!index_applies(me, key, info->key)) continue;
            if (!item_ty) item_ty = class_item_get_type(item);
            if (ty_is_related(me, item_ty, info->value, TY_RELATION_ASSIGNABLE)) continue;
            cstr type_str = ty_to_cstr(me, item_ty);
            cstr key_str = ty_to_cstr(me, info->key);
            cstr value_str = ty_to_cstr(me, info->value);
            error_ctx_error_d(errs, item_name, 2411, "Property '", item_name, "' of type '", type_str, "' is not assignable to '",
                key_str, "' index type '", value_str, "'.");
            c_drop(cstr, &type_str, &key_str, &value_str);
        }
    }
    if (members->index_count < 2) return;
    for (u32 c = 0; c < members->index_count; c++) {
        IndexInfo const* check_info = &members->index[c];
        bool local_check = is_local_span(owner, check_info->decl_span);
        for (u32 i = 0; i < members->index_count; i++) {
            IndexInfo const* info = &members->index[i];
            if (i == c || !index_applies(me, check_info->key, info->key)) continue;
            Span at = local_check ? span_with_member_separator(check_info->decl_span) :
                is_local_span(owner, info->decl_span) ? span_with_member_separator(info->decl_span) :
                interface && !base_has_both(me, bases, base_count, nullptr, check_info->key, info->key) ? name : (Span){0};
            if (!span_valid(at) || ty_is_related(me, check_info->value, info->value, TY_RELATION_ASSIGNABLE)) continue;
            cstr check_key = ty_to_cstr(me, check_info->key);
            cstr check_value = ty_to_cstr(me, check_info->value);
            cstr key_str = ty_to_cstr(me, info->key);
            cstr value_str = ty_to_cstr(me, info->value);
            error_ctx_error_d(errs, at, 2413, "'", check_key, "' index type '", check_value, "' is not assignable to '",
                key_str, "' index type '", value_str, "'.");
            c_drop(cstr, &check_key, &check_value, &key_str, &value_str);
        }
    }
}

// tsc's markPropertyAsReferenced: a private member is read, unless the access only writes it or is its own body
// reading it through `this` (`this.m()` in m). `access` is null for a destructuring, which always reads
static void mark_member_referenced(Checker* me, Prop const* prop, PropertyExpr const* access) {
    while (prop->target) prop = prop->target;
    bool private_name = span_valid(prop->decl->name_span) && prop->decl->name_span.buf[0] == '#';
    if (!(prop->flags & PROP_PRIVATE) && !private_name) return;
    if (access && access->assignment_target == FLOW_TARGET_DEFINITE && !access->read_target && !(prop->flags & PROP_ACCESSOR)) return;
    Expr const* object = access ? &access->expr : nullptr;
    bool self_type = object && (is_keyword(object, KEYWORD_IDENTIFIER_THIS) ||
        (object->tag == EXPR_IDENTIFIER && prop->parent && object->identifier.target == prop->parent));
    FunctionImpl const* function = access ? property_context(access)->containing_function : nullptr;
    char const* at = function ? span_start_ptr(function_impl_member_name(function)) : nullptr;
    for (u32 i = 0; self_type && at && i < prop->decl->decl_span_count; i++) {
        if (at >= span_start_ptr(prop->decl->decl_spans[i]) && at < span_end_ptr(prop->decl->decl_spans[i])) return;
    }
    for (u32 i = 0; i < prop->decl->decl_span_count; i++) ty_mark_member_referenced(me, prop->decl->decl_spans[i]);
}

void ty_check_destructured_member(
    Checker* me, Ty const* object, Ty const* key, Span span, bool from_this, AccessSite const* site, ErrorCtx* errs
) {
    if (object->flags & TYF_ANY) return;
    Atom const* name = ty_literal_name(me, key);
    MemberRefs refs = {0};
    if (!name || member_refs(me, object, name, &refs, 0) < 0) return;
    for (u32 i = 0; unused_enabled() && i < refs.len; i++) {
        // `({ m } = this)` in m itself isn't a read of m (tsc's markPropertyAsReferenced with rightIsThis). HACK: by
        // position, a function nested in m counts as m
        Prop const* prop = refs.props[i];
        while (prop->target) prop = prop->target;
        bool self = false;
        for (u32 d = 0; from_this && span_valid(span) && (prop->flags & PROP_METHOD) && d < prop->decl->decl_span_count; d++) {
            Span decl = prop->decl->decl_spans[d];
            self |= span_source(decl) == span_source(span) && span_contains_span(decl, span);
        }
        if (!self) mark_member_referenced(me, refs.props[i], nullptr);
    }
    // tsc's checkPropertyAccessibility of a binding element or destructuring assignment property, where the pattern is
    // (no site: at the top level)
    if (!span_valid(span)) return;
    AccessSite const top_level = {0};
    for (u32 i = 0; i < refs.len; i++) {
        if (!check_accessibility(me, site ? site : &top_level, span, from_this, false, refs.props[i], refs.objects[i],
            false, errs)
        ) {
            return;
        }
    }
}

// tsc's isPropertyDeclaredInAncestorClass: a base class declares it too, and gives it its value first
static bool declared_in_base_class(Checker* me, ClassDecl const* class_decl, Atom const* name) {
    for (u32 depth = 0; class_decl && depth < 32; depth++) {
        Ty const* base = ty_class_base_instance(me, class_decl);
        if (!base || !(base->flags & TYF_OBJECT)) return false;
        Prop const* prop = ty_get_prop(me, base, name);
        if (prop) return true;
        Ty const* target = base->kind == TY_REFERENCE ? base->reference.target : base;
        Symbol const* symbol = target->kind == TY_DECLARED ? target->declared.symbol : nullptr;
        class_decl = symbol ? class_decl_of(symbol) : nullptr;
    }
    return false;
}

// tsc's isPropertyInitializedInStaticBlocks, syntactically (HACK: tsc asks control flow at the end of each block): a
// `static {}` before `before` assigns `this.name` (or `C.name`) in one of its statements
// `any_type`: the property's type is any or unknown, whose flow type never has undefined, so any static block counts
static bool initialized_in_static_blocks(ClassDecl const* class_decl, csview name, char const* before, bool any_type) {
    for_each (i, vec_class_item, &class_decl->items) {
        if (i->tag != CLASS_ITEM_STATIC_BLOCK || span_start_ptr(i->span) >= before) continue;
        if (any_type) return true;
        for_each (s, vec_statement, &i->static_block.statements) {
            if (s->tag != STATEMENT_EXPR || s->expr.tag != EXPR_BINARY_OP || s->expr.binary_op->op != BINARY_OP_EQUAL) continue;
            Expr const* target = &s->expr.binary_op->left;
            if (target->tag == EXPR_PROPERTY && target->property->form == PROPERTY_EXPR_DOT &&
                csview_equals_sv(span_sv(property_name(target->property)), name)
            ) {
                return true;
            }
        }
    }
    return false;
}


// tsc's checkPropertyNotUsedBeforeDeclaration with isBlockScopedNameDeclaredBeforeUse: `this.x` (or `o.x`) in a field
// initializer or a static block, where x isn't initialized yet (TS2729). Any property declared later in the file
// counts, class members have more rules
static void check_used_before_initialization(Checker* me, PropertyExpr const* access, Prop const* prop, ErrorCtx* errs) {
    PropertyContext const* site = property_context(access);
    while (prop->target) prop = prop->target;
    if (!span_valid(prop->decl->name_span) || span_source(prop->decl->name_span) != span_source(property_key_span(access))) return;
    // A JavaScript assignment declaration (`this.x = v` declares x from where it starts)
    if (prop->decl->symbol && prop->decl->symbol->decl_count && prop->decl->symbol->decls[0].kind == SYMBOL_DECL_ASSIGNMENT) return;
    ClassDecl* class_decl = prop->parent && (prop->parent->flags & SYMBOL_CLASS) ? (ClassDecl*)class_decl_of(prop->parent) : nullptr;
    // A class member's declaration: a field, method or accessor, or a parameter property
    ClassItem* item = nullptr;
    FunctionArg const* parameter = nullptr;
    char const* name_at = span_start_ptr(prop->decl->name_span);
    for_each (i, vec_class_item, class_decl ? &class_decl->items : &(VecClassItem){0}) {
        if ((i->tag == CLASS_ITEM_FIELD || i->tag == CLASS_ITEM_METHOD) && span_contains(i->span, name_at)) item = i;
        if (i->tag != CLASS_ITEM_CONSTRUCTOR) continue;
        for_each (arg, vec_function_arg, &i->constructor.func.args) {
            if (arg->is_parameter_property && span_contains(arg->span, name_at)) parameter = arg;
        }
    }
    if (!item && !parameter) class_decl = nullptr;
    bool is_field = item && item->tag == CLASS_ITEM_FIELD;
    if (is_field && item->field.is_optional) return;
    if (item && item->tag == CLASS_ITEM_METHOD && item->method.is_static) return;
    CompilerOptions const* options = compiler_options();
    bool emit_standard = options->use_define_for_class_fields && options->target >= SCRIPT_TARGET_ES2022;
    if (class_decl && !options->use_define_for_class_fields && declared_in_base_class(me, class_decl, prop->name)) return;

    Span declaration = item ? item->span : parameter ? parameter->span : prop->decl->name_span;
    char const* declared_at = span_start_ptr(declaration);
    char const* used_at = span_start_ptr(expr_outer_span(&access->expr));
    bool same_class = class_decl && site->enclosing_class == class_decl;
    bool this_access = access->expr.tag == EXPR_IDENTIFIER && access->expr.identifier.keyword == KEYWORD_IDENTIFIER_THIS;
    bool initialized = is_field && (item->field.value.tag != EXPR_IMPLICIT_ANY || item->field.is_definite);
    bool within_declaration = span_contains(declaration, used_at);
    // isUsedInFunctionOrInstanceProperty, from where the reads can be
    bool is_instance_field = is_field && !item->field.is_static;
    bool deferred = false;
    switch (site->initializer_context) {
        case INITIALIZER_INSTANCE_FIELD: deferred = !is_instance_field || !same_class; break;
        case INITIALIZER_STATIC_FIELD:
            deferred = (item && item->tag == CLASS_ITEM_METHOD) || (is_field && same_class && site->initializer_field &&
                initialized_in_static_blocks(class_decl, span_sv(property_key_inner_span(access)), span_start_ptr(site->initializer_field->span),
                    (prop_type(me, prop)->flags & (TYF_ANY | TYF_UNKNOWN)) != 0));
            break;
        case INITIALIZER_STATIC_BLOCK: deferred = declared_at < used_at; break;
    }
    bool fine;
    if (site->initializer_context == INITIALIZER_MEMBER_NAME) {
        // A method's or accessor's name is deferred (a function), unless it is in the field it reads
        // (isPropertyImmediatelyReferencedWithinDeclaration: no Block of a function in between)
        fine = !(is_field && within_declaration);
    } else if (declared_at <= used_at && !(is_field && this_access && !initialized)) {
        fine = is_field ? !within_declaration : parameter ? !(emit_standard && same_class && deferred) : true;
    } else if (deferred) {
        // With standard class fields the fields of the class are defined in order, the parameter properties after them
        fine = !(emit_standard && (is_field || parameter) &&
            (within_declaration || (site->initializer_field && same_class) ||
                site->initializer_context == INITIALIZER_STATIC_BLOCK) &&
            span_end_ptr(property_key_span(access)) <= span_end_ptr(declaration));
    } else {
        fine = false;
    }
    if (fine) return;
    Span name = property_key_inner_span(access);
    Error* error = error_ctx_error_d(errs, name, 2729, "Property '", name, "' is used before its initialization.");
    // (tsc's error span of the declaration: the name, but all of a parameter)
    error_add_related_d(error, parameter ? parameter->span : prop->decl->name_span, 2728, "'", name, "' is declared here.");
}

// The class half of tsc's checkPropertyNotUsedBeforeDeclaration: `N.C` of a class a namespace exports, read (not in
// a type) before the class is declared, unless the read is deferred (in a function, an instance field initializer)
static void check_class_used_before_declaration(PropertyExpr const* access, Prop const* prop, ErrorCtx* errs) {
    PropertyContext const* site = property_context(access);
    while (prop->target) prop = prop->target;
    Symbol const* symbol = prop->decl->symbol;
    ClassDecl const* class_decl = symbol ? symbol_find_node(symbol, SYMBOL_DECL_CLASS) : nullptr;
    // (A class declaration: `exports.C = class {}` aliases a class expression)
    if (!class_decl || class_decl->is_ambient || class_decl->is_expression || access->qualified_name) return;
    Span used = property_key_inner_span(access);
    if (!span_valid(used) || span_source(class_decl->full_span) != span_source(used) || !span_source(used) || span_source(used)->is_declaration) return;
    char const* declared_at = span_start_ptr(class_decl->full_span);
    if (declared_at <= span_start_ptr(used)) return;
    if (site->containing_function && !span_contains(site->containing_function->full_span, declared_at)) return;
    if (site->initializer_context == INITIALIZER_INSTANCE_FIELD) return;
    Error* error = error_ctx_error_d(errs, used, 2449, "Class '", used, "' used before its declaration.");
    error_add_related_d(error, class_decl->name, 2728, "'", used, "' is declared here.");
}

// Whether tsc's getPropertyOfType finds a property of that name in an apparent type (Object's members too)
static bool has_real_prop(Checker* me, Ty const* apparent, Atom const* name) {
    if ((apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_property_of_type(me, apparent, name)) return true;
    Ty const* global_object = ty_global_object_type(me);
    return global_object && ty_get_prop(me, global_object, name);
}

void ty_check_index_signature_dot_access(Checker* me, PropertyExpr const* access, Ty const* object, ErrorCtx* errs) {
    if (access->form != PROPERTY_EXPR_DOT || access->qualified_name) return;
    Ty const* key = ty_access_key_type(me, access);
    Atom const* name = ty_literal_name(me, key);
    if (!name || !name->len || name->str[0] == '#') return;
    Ty const* apparent = ty_apparent_type(me, ty_non_nullable(me, object));
    if (apparent->flags & (TYF_ANY | TYF_UNKNOWN | TYF_NEVER | TYF_ERROR)) return;
    // (A union's property exists if any member has one, its index signatures are those all members have)
    TyList members = apparent->kind == TY_UNION ? apparent->composite.types : (TyList){ .data = &apparent, .len = 1 };
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = ty_apparent_type(me, members.data[i]);
        if (!(member->flags & (TYF_OBJECT | TYF_INTERSECTION)) || has_real_prop(me, member, name)) return;
        if (!ty_applicable_index_info(me, member, key)) return;
    }
    // (tsc's checkPropertyAccessExpressionOrQualifiedName, at the name)
    error_ctx_error_d(errs, property_key_span(access), 4111, "Property '", atom_sv(name),
        "' comes from an index signature, so it must be accessed with ['", atom_sv(name), "'].");
}

void ty_check_member_modifiers(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs) {
    PropertyContext const* site = property_context(access);
    // `typeof globalThis` has no properties for block scoped variables (TS2339 there, see check_access()). Its own
    // `globalThis` is readonly (tsc's globalThisSymbol has CheckFlags.Readonly)
    if (is_global_this(object) && access->assignment_target && access->form == PROPERTY_EXPR_DOT &&
        span_equals(property_name(access), "globalThis")
    ) {
        error_ctx_error_d(errs, property_key_span(access), 2540, "Cannot assign to 'globalThis' because it is a read-only property.");
    }
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
        // `y[k] = v` with y a generic `+readonly` mapped type and k one of its keys (tsc's checkIndexedAccessIndexType)
        if (access->assignment_target && access->form == PROPERTY_EXPR_BRACKET && object->kind == TY_ANONYMOUS &&
            (object->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)
        ) {
            MappedInfo const* mapped = ty_members(me, object)->mapped;
            Ty const* key = ty_regular(me, ty_access_key_type(me, access));
            if (mapped && mapped->readonly > 0 && ty_is_generic_index(key) &&
                ty_is_related(me, key, ty_keyof(me, object), TY_RELATION_ASSIGNABLE)
            ) {
                cstr object_str = ty_to_cstr(me, object);
                error_ctx_error_d(errs, span, 2542, "Index signature in type '", object_str, "' only permits reading.");
                cstr_drop(&object_str);
                return;
            }
        }
        // Assigning to a generic object doesn't go through its index signatures (TS2339 instead)
        bool generic = object->kind == TY_TYPE_PARAM && !object->param.state->is_this;
        if (!access->assignment_target || generic) return;
        // Through a union of keys, each one is written (tsc's getIndexedAccessType goes key by key with
        // AccessFlags.Writing): a readonly property is TS2540 at the key, a readonly index signature TS2542 at the access
        Ty const* union_key = written_union_key(me, access);
        if (union_key && !may_have_readonly_member(me, object)) return;
        if (union_key) {
            Ty const* apparent = ty_apparent_type(me, object);
            bool index_reported = false;
            for (u32 i = 0; i < union_key->composite.types.len; i++) {
                Ty const* key = union_key->composite.types.data[i];
                Atom const* member_name = ty_literal_name(me, key);
                int readonly = member_name ? readonly_key_member(me, access, object, member_name) : -1;
                if (readonly > 0) {
                    cstr name_str = ty_prop_name_cstr(me, member_name);
                    error_ctx_error_d(errs, property_key_span(access), 2540,
                        "Cannot assign to '", name_str, "' because it is a read-only property.");
                    cstr_drop(&name_str);
                    continue;
                }
                if (readonly == 0 || index_reported || !(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) continue;
                IndexInfo const* info = ty_applicable_index_info(me, apparent, key);
                if (info && info->readonly) {
                    index_reported = true;
                    cstr object_str = ty_to_cstr(me, apparent); // (tsc shows the apparent type either way)
                    error_ctx_error_d(errs, span, 2542, "Index signature in type '", object_str, "' only permits reading.");
                    cstr_drop(&object_str);
                }
            }
            return;
        }
        // A readonly index signature (`readonly T[]`)
        Ty const* apparent = ty_apparent_type(me, object);
        Ty const* key = ty_regular(me, ty_access_key_type(me, access));
        IndexInfo const* info = apparent->flags & (TYF_OBJECT | TYF_INTERSECTION) ? ty_applicable_index_info(me, apparent, key) : nullptr;
        // (Of a union, any member's: tsc's getIndexedAccessType goes member by member, `string | T` has String's
        // readonly number index signature, and names the union)
        // (A union has an index signature only if every member has one: tsc's getIndexInfosOfType of a union. A member
        // without one makes it TS7053, nothing about reading)
        if (!info && apparent->kind == TY_UNION && !(apparent->flags & TYF_BOOLEAN)) {
            IndexInfo const* readonly_info = nullptr;
            bool all = true;
            for (u32 i = 0; all && i < apparent->composite.types.len; i++) {
                Ty const* member = ty_apparent_type(me, apparent->composite.types.data[i]);
                IndexInfo const* member_info = member->flags & (TYF_OBJECT | TYF_INTERSECTION) ? ty_applicable_index_info(me, member, key) : nullptr;
                all = member_info != nullptr;
                if (member_info && member_info->readonly) readonly_info = member_info;
            }
            if (all) info = readonly_info;
        }
        if (info && info->readonly) {
            cstr object_str = ty_to_cstr(me, apparent); // (tsc shows the apparent type either way)
            error_ctx_error_d(errs, span, 2542, "Index signature in type '", object_str, "' only permits reading.");
            cstr_drop(&object_str);
        }
        return;
    }
    if (unused_enabled()) {
        for (u32 i = 0; i < refs.len; i++) mark_member_referenced(me, refs.props[i], access);
    }
    if (site->initializer_context && refs.len == 1 && !(ty_apparent_type(me, object)->flags & TYF_ANY)) {
        check_used_before_initialization(me, access, refs.props[0], errs);
    }
    if (refs.len == 1 && access->form == PROPERTY_EXPR_DOT) check_class_used_before_declaration(access, refs.props[0], errs);
    // Element access is the way around accessibility
    bool synthetic_checked = false;
    AccessSite synthetic_site = {
        .enclosing_class = site->enclosing_class, .containing_function = site->containing_function,
        .this_parameter = site->this_parameter, .this_function = site->this_function,
        .initializer_context = site->initializer_context,
    };
    // (Only when a member's property isn't public: otherwise every reading and writing is public, nothing to check)
    bool non_public = false;
    for (u32 i = 0; i < refs.len && !non_public; i++) {
        non_public = (refs.props[i]->flags & (PROP_PRIVATE | PROP_PROTECTED | PROP_SET_PRIVATE | PROP_SET_PROTECTED)) != 0;
    }
    if (non_public && access->form == PROPERTY_EXPR_DOT && refs.len > 1 && !is_keyword(&access->expr, KEYWORD_IDENTIFIER_SUPER)) {
        SyntheticAccess synthetic;
        synthetic_access(me, object, name, &synthetic, 0);
        if (synthetic.found && synthetic.synthetic) {
            check_synthetic_access(me, &synthetic_site, property_key_span(access), &synthetic, access->assignment_target,
                object, name, errs);
            synthetic_checked = true;
        }
    }
    Prop const* const* parts = nullptr;
    u32 part_count = access->form == PROPERTY_EXPR_DOT && refs.len == 1 && !is_keyword(&access->expr, KEYWORD_IDENTIFIER_SUPER) ?
        ty_synthetic_prop_parts(me, refs.props[0], &parts) : 0;
    if (part_count > 1) {
        // A property a class inherits from an intersection (a mixin's base): the intersection's synthetic property
        SyntheticAccess synthetic = { .found = true, .synthetic = true };
        for (u32 i = 0; i < part_count && i < c_arraylen(synthetic.leaves.props); i++) {
            SyntheticAccess part = { .single = parts[i] };
            synthetic.read |= synth_effective(&part, false);
            synthetic.write |= synth_effective(&part, true);
            synthetic.is_static |= (parts[i]->flags & PROP_STATIC) != 0;
            synthetic.leaves.props[synthetic.leaves.len] = parts[i];
            synthetic.leaves.objects[synthetic.leaves.len++] = refs.objects[0];
        }
        check_synthetic_access(me, &synthetic_site, property_key_span(access), &synthetic, access->assignment_target,
            object, name, errs);
        synthetic_checked = true;
    }
    if (access->form == PROPERTY_EXPR_DOT && !synthetic_checked) {
        bool is_super = is_keyword(&access->expr, KEYWORD_IDENTIFIER_SUPER);
        for (u32 i = 0; i < refs.len; i++) {
            AccessSite access_site = {
                .enclosing_class = site->enclosing_class, .containing_function = site->containing_function,
                .this_parameter = site->this_parameter, .this_function = site->this_function,
                .initializer_context = site->initializer_context,
            };
            if (!check_accessibility(me, &access_site, property_key_span(access), is_keyword(&access->expr, KEYWORD_IDENTIFIER_THIS),
                access->assignment_target, refs.props[i], refs.objects[i], is_super, errs)
            ) {
                break;
            }
        }
    }
    refs.len = 0;
    if (readonly_assignment(me, access, object, &refs)) {
        cstr name_str = ty_prop_name_cstr(me, name);
        error_ctx_error_d(errs, property_key_span(access), 2540,
            "Cannot assign to '", name_str, "' because it is a read-only property.");
        cstr_drop(&name_str);
    }
}

// tsc's checkNonNullType: unknown (under strictNullChecks) and what may be null or undefined are errors (without
// strictNullChecks only the null and undefined types themselves may be). Null if it reported and nothing is left
Ty const* ty_check_non_null_type(Checker* me, Ty const* type, Expr const* expr, ErrorCtx* errs) {
    bool named = expr_is_entity_name(expr);
    Span span = expr_error_span(expr);
    if (compiler_options()->strict_null_checks && (type->flags & TYF_UNKNOWN)) {
        if (named) {
            cstr name = entity_name_text(span);
            error_ctx_error_d(errs, span, 18046, "'", name, "' is of type 'unknown'.");
            cstr_drop(&name);
        } else {
            error_ctx_error_d(errs, span, 2571, "Object is of type 'unknown'.");
        }
        return nullptr;
    }
    bool has_null = ty_can_be_null(me, type), has_undefined = ty_can_be_undefined(me, type);
    if (!has_null && !has_undefined) return type;
    bool bare = !expr_is_perenthisized(expr) && expr->tag == EXPR_LITERAL;
    char const* what = has_null && has_undefined ? "null' or 'undefined" : has_null ? "null" : "undefined";
    if (bare && expr->literal.tag == VALUE_NULL) {
        error_ctx_error_d(errs, span, 18050, "The value 'null' cannot be used here.");
    } else if (bare && expr->literal.tag == VALUE_UNDEFINED && csview_equals(span_sv(expr->inner_span), "undefined")) {
        error_ctx_error_d(errs, span, 18050, "The value 'undefined' cannot be used here.");
    } else if (named) {
        cstr name = entity_name_text(span);
        error_ctx_error_d(errs, span, has_null && has_undefined ? 18049 : has_null ? 18047 : 18048,
            "'", name, "' is possibly '", what, "'.");
        cstr_drop(&name);
    } else {
        error_ctx_error_d(errs, span, has_null && has_undefined ? 2533 : has_null ? 2531 : 2532,
            "Object is possibly '", what, "'.");
    }
    Ty const* non_null = ty_non_nullable(me, type);
    return non_null->flags & (TYF_NULLABLE | TYF_NEVER) ? nullptr : non_null;
}

u32 ty_check_non_null_non_void_at(Checker* me, Ty const* type, Span span, bool check_void, ErrorCtx* errs) {
    if (!compiler_options()->strict_null_checks) return 0;
    if (type->flags & TYF_UNKNOWN) {
        error_ctx_error_d(errs, span, 2571, "Object is of type 'unknown'.");
        return 2571;
    }
    bool has_null = ty_can_be_null(me, type), has_undefined = ty_can_be_undefined(me, type);
    if (has_null || has_undefined) {
        char const* what = has_null && has_undefined ? "null' or 'undefined" : has_null ? "null" : "undefined";
        u32 code = has_null && has_undefined ? 2533 : has_null ? 2531 : 2532;
        error_ctx_error_d(errs, span, code, "Object is possibly '", what, "'.");
        return code;
    }
    if (check_void && (type->flags & TYF_VOID)) {
        error_ctx_error_d(errs, span, 2532, "Object is possibly 'undefined'.");
        return 2532;
    }
    return 0;
}

Ty const* ty_private_name_owner(Checker* me, Expr const* private_name, Atom const** name) {
    ClassDecl const* class_decl = private_name->private_name->private_class;
    if (!class_decl || !class_decl->symbol) return nullptr;
    csview text = span_sv(private_name->inner_span);
    if (name) *name = ty_private_name(me, class_decl->symbol, text);
    ClassItem* item = (ClassItem*)class_decl_find_private_name(class_decl, text);
    return class_item_is_static(item) ? ty_static_type_of(me, class_decl->symbol) : ty_declared_type_of(me, class_decl->symbol);
}

// tsc's isEmptyAnonymousObjectType
static bool is_empty_anonymous_object(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || ty->anonymous.function) return false;
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->call_count && !members->construct_count && !members->index_count &&
        !members->mapped;
}

// tsc's hasEmptyObjectIntersection: `{}` from narrowing `unknown`, or an intersection whose constraint is `{}` (`T & {}`),
// which are assignable to `object` but may be primitives
static bool has_empty_object_intersection(Checker* me, Ty const* ty) {
    u32 len = ty->kind == TY_UNION ? ty->composite.types.len : 1;
    for (u32 i = 0; i < len; i++) {
        Ty const* part = ty->kind == TY_UNION ? ty->composite.types.data[i] : ty;
        if (part == ty_unknown_empty_object_type(me)) return true;
        if (part->kind == TY_INTERSECTION) {
            Ty const* constraint = ty_base_constraint(me, part);
            if (is_empty_anonymous_object(me, constraint ? constraint : part)) return true;
        }
    }
    return false;
}

// tsc's checkInExpression
void ty_check_in_expression(Checker* me, Expr const* left_expr, Expr const* right_expr, ErrorCtx* errs) {
    Ty const* right = ty_expr_type(me, right_expr);
    if (left_expr->tag == EXPR_PRIVATE_NAME) {
        PrivateNameExpr const* name = left_expr->private_name;
        if (name->private_class && unused_enabled()) {
            ClassItem const* item = class_decl_find_private_name(name->private_class, span_sv(left_expr->inner_span));
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
        Ty const* left = ty_check_non_null_type(me, ty_expr_type(me, left_expr), left_expr, errs);
        if (left) {
            Ty const* keys[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
            ty_check_assignable(me, left, ty_union(me, keys, 3, nullptr, (TyList){0}), left_expr->inner_span, errs);
        }
    }
    Ty const* non_null = ty_check_non_null_type(me, right, right_expr, errs);
    if (!non_null) return;
    if (ty_check_assignable(me, non_null, ty_intrinsic(me, TY_NON_PRIMITIVE), right_expr->inner_span, errs) &&
        has_empty_object_intersection(me, right)
    ) {
        cstr right_str = ty_to_cstr(me, right);
        error_ctx_error_d(errs, right_expr->inner_span, 2638, "Type '", right_str,
            "' may represent a primitive value, which is not permitted as the right operand of the 'in' operator.");
        cstr_drop(&right_str);
    }
}

// tsc's checkPropertyAccessExpressionOrQualifiedName for a PrivateIdentifier, and checkPrivateIdentifierPropertyAccess
bool ty_check_private_name_access(Checker* me, PropertyExpr const* access, Ty const* object, ErrorCtx* errs) {
    PropertyContext const* site = property_context(access);
    if (access->form != PROPERTY_EXPR_DOT) return false;
    Span name = property_name(access);
    if (!span_valid(name) || name.buf[0] != '#') return false;
    ClassDecl const* lexical = property_private_class(access);
    ClassItem const* lexical_item = lexical ? class_decl_find_private_name(lexical, span_sv(name)) : nullptr;
    if (access->assignment_target && lexical_item && lexical_item->tag == CLASS_ITEM_METHOD &&
        lexical_item->method.accessor == CLASS_ACCESSOR_NONE
    ) {
        error_ctx_grammar_error_d(errs, name, 2803, "Cannot assign to private method '", name,
            "'. Private methods are not writable.");
    }
    if (object->flags & TYF_ANY) {
        if (lexical) return true;
        // (Grammar errors: tsgo's checkPrivateIdentifierPropertyAccess)
        if (!site->enclosing_class) {
            error_ctx_grammar_error_d(errs, name, 18016, "Private identifiers are not allowed outside class bodies.");
        } else {
            error_ctx_error_d(errs, name, 2339, "Property '", name, "' does not exist on type 'any'.");
        }
        return true;
    }
    // unknown is TS18046 (check_access()), after which the object is the error type: a name no class around declares
    // doesn't exist on 'any' (tsc's checkPropertyAccessExpressionOrQualifiedName)
    if ((object->flags & TYF_UNKNOWN) && compiler_options()->strict_null_checks && !lexical && site->enclosing_class) {
        error_ctx_error_d(errs, name, 2339, "Property '", name, "' does not exist on type 'any'.");
        return false;
    }
    Ty const* apparent = ty_apparent_type(me, object);
    if (!(apparent->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, apparent);
    Prop const* own = lexical && lexical->symbol ? ty_get_prop(me, apparent, ty_private_name(me, lexical->symbol, span_sv(name))) : nullptr;
    if (own) {
        bool setter = false, getter = false;
        for_each (i, vec_class_item, &lexical->items) {
            if (i->tag != CLASS_ITEM_METHOD || !name_equals(span_sv(i->method.name_span), span_sv(name))) continue;
            setter |= i->method.accessor == CLASS_ACCESSOR_SET;
            getter |= i->method.accessor == CLASS_ACCESSOR_GET;
        }
        if (setter && !getter && access->assignment_target != FLOW_TARGET_DEFINITE) {
            // The whole access (tsc's error(node))
            Span at = span_make_in(name, span_start_ptr(expr_outer_span(&access->expr)), span_end_ptr(name));
            error_ctx_error_d(errs, at, 2806, "Private accessor was defined without a getter.");
        }
        return false;
    }
    // A private name of the same spelling on the type is another class's
    Prop const* other = nullptr;
    for (u32 i = 0; i < members->prop_count && !other; i++) {
        Prop const* prop = members->props[i];
        if (ty_private_name_class(me, prop->name) && csview_equals_sv(atom_sv(prop->name), span_sv(name))) other = prop;
    }
    if (!other) {
        // (In a plain JavaScript file the TS2339 that follows isn't reported, this is)
        if (site->enclosing_class && span_source(name) && span_source(name)->plain_js) {
            error_ctx_grammar_error_d(errs, name, 1111, "Private field '", name, "' must be declared in an enclosing class.");
        }
        return false;
    }
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
    // (tsgo's SymbolToString of the class: a class expression's symbol is called by its assigned name)
    csview class_name = other_class && span_valid(other_class->name) ? span_sv(other_class->name) :
        other_class && span_valid(other_class->assigned_name) ? span_sv(other_class->assigned_name) : c_sv("(anonymous)");
    error_ctx_error_d(errs, name, 18013, "Property '", name, "' is not accessible outside class '", class_name,
        "' because it has a private identifier.");
    return true;
}

bool ty_constructor_accessible(Checker* me, CallExpr const* call, Signature const* sig, Span span, ErrorCtx* errs) {
    while (sig && !sig->function && sig->target) sig = sig->target;
    FunctionImpl const* function = sig ? sig->function : nullptr;
    if (!function || !function_impl_rare(function)->constructed_class || function_impl_rare(function)->constructor_access == ACCESS_MODIFIER_PUBLIC) {
        return true;
    }
    ClassDecl const* declaring = class_decl_of(function_impl_rare(function)->constructed_class);
    if (!declaring || is_within_class(call_rare(call)->enclosing_class, declaring)) return true;
    Ty const* declaring_type = ty_declared_type_of(me, function_impl_rare(function)->constructed_class);
    bool is_private = function_impl_rare(function)->constructor_access == ACCESS_MODIFIER_PRIVATE;
    // A protected constructor is accessible from a derived class (tsc's typeHasProtectedAccessibleBase)
    if (!is_private && call_rare(call)->enclosing_class && call_rare(call)->enclosing_class->symbol &&
        ty_has_base_type(me, ty_declared_type_of(me, call_rare(call)->enclosing_class->symbol), declaring_type)
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
