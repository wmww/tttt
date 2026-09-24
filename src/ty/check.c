#include "ty/ty_internal.h"
#include "scope.h"
#include "display.h"
#include "error.h"
#include "source_file.h"
#include "program.h"
#include "class_decl.h"
#include "port_statement.h"

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
    if (natural_head && span_valid(natural_head->span)) {
        // An error that knows exactly where it is (an excess property) replaces the head, whatever it was
        error = error_ctx_error_s(errs, natural_head->span, natural_head->ts_code, cstr_clone(natural_head->message));
        first_nested = 1;
    } else if (default_head && natural_head && natural_head->ts_code != 2322) {
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
    attach_explanation(error, &lines, first_nested);
    vec_ty_report_line_drop(&lines);
    c_drop(cstr, &source_str, &target_str);
    return false;
}

// An error whose head says something of its own (`head`, taken), followed by why source isn't assignable to target.
// `keep_natural_head`: the elaboration starts with "Type 'S' is not assignable to type 'T'." (tsc's containing message
// chain) rather than having it replaced (tsc's head message)
static void report_unassignable_with_head(
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

void ty_check_satisfies(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    if (source->flags & TYF_ANY) return;
    if (ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE)) return;
    if (report_instantiation_limit(me, span, errs)) return;
    cstr source_str = ty_report_source_cstr(me, source, target);
    cstr target_str = ty_to_cstr(me, target);
    report_unassignable_with_head(me, source, target, 1360, cstr_d(
        "Type '", source_str, "' does not satisfy the expected type '", target_str, "'."
    ), false, span, errs);
    c_drop(cstr, &source_str, &target_str);
}

void ty_check_assignable(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    check_assignable(me, "Type", source, "type", target, 2322, span, errs);
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

void ty_report_unfit_argument(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    check_assignable(me, "Argument of type", source, "parameter of type", target, 2345, span, errs);
}

void ty_report_unfit_this(Checker* me, Ty const* source, Ty const* target, Span span, ErrorCtx* errs) {
    check_assignable(me, "The 'this' context of type", source, "method's 'this' of type", target, 2684, span, errs);
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
    if (index->kind == TY_UNION || index->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < index->composite.types.len; i++) {
            check_access(
                me, object, index->composite.types.data[i], object_span, index_span, object_is_named,
                error_if_not_found, depth + 1, errs
            );
        }
        return;
    }
    if (index->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) return; // TODO: TS2536 against `keyof` of the object
    if (!(index->flags & (TYF_ANY | TYF_STRING_LIKE | TYF_NUMBER | TYF_ES_SYMBOL_LIKE | TYF_NUMBER_LITERAL))) {
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
    return ty_literal_name(me, ty_regular(me, ty_element_index_type(me, &access->prop)));
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

void ty_check_member_modifiers(Checker* me, PropertyExpr const* access, Ty const* object, Span span, ErrorCtx* errs) {
    // `typeof globalThis` has no properties for block scoped variables (TS2339 there, see check_access())
    if ((object->flags & TYF_ANY) || is_global_this(object)) return;
    Atom const* name = access_name(me, access);
    MemberRefs refs = {0};
    if (!name || member_refs(me, object, name, &refs, 0) < 0) {
        // Assigning to a generic object doesn't go through its index signatures (TS2339 instead)
        bool generic = object->kind == TY_TYPE_PARAM && !object->param.state->is_this;
        if (!access->assignment_target || generic) return;
        // A readonly index signature (`readonly T[]`)
        Ty const* apparent = ty_apparent_type(me, object);
        IndexInfo const* info = apparent->flags & (TYF_OBJECT | TYF_INTERSECTION) ?
            ty_applicable_index_info(me, apparent, ty_regular(me, ty_element_index_type(me, &access->prop))) : nullptr;
        if (info && info->readonly) {
            cstr object_str = ty_to_cstr(me, access->form == PROPERTY_EXPR_DOT ? apparent : object);
            error_ctx_error_d(errs, span, 2542, "Index signature in type '", object_str, "' only permits reading.");
            cstr_drop(&object_str);
        }
        return;
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
