#include "ty/ty_internal.h"
#include "error.h"

// Call resolution on Signatures, the way tsc observably does it (resolveCall): collect the call or construct
// signatures of the callee, and take the first one the arguments fit: right number of them, type arguments inferred
// from them, each one assignable to its parameter. With several candidates a first round wants the arguments to be
// subtypes. If nothing fits, the errors are about the candidates that came closest.
//
// Arguments are still typed by the legacy checker and lowered here, and the result goes back as TYPE_NEW (stage 4b1).
// Not here yet: the second inference pass for context-sensitive arguments and inference from the contextual return
// type (both need contextual typing, 4b3).

typedef struct Arg {
    Expr const* expr; // For a spread tuple, the spread expression every element came from
    Type const* legacy; // null if the argument is only part of an expression (an element of a spread tuple)
    Ty const* type;
    bool spread; // `...xs` of something that isn't a fixed tuple: stands for any number of arguments of this type
    bool context_sensitive; // A function expression with parameters that would be contextually typed
    bool widened_without_context; // An object or array literal, see first_unfit_argument()
} Arg;

typedef struct Args {
    Arg* data;
    u32 len;
    i32 spread_index; // first `spread` argument, -1 if none
} Args;

// The parameters of a signature as a call sees them: a rest parameter of tuple type is spread out
typedef struct ParamShape {
    Ty const** types;
    u32 count;
    u32 min;
    Ty const* rest_element; // `...rest: X[]`: X
    Ty const* rest_variadic; // a rest parameter that's neither array nor tuple (a type parameter, mostly)
} ParamShape;

static void args_push(Args* args, Arg arg) {
    args->data = realloc(args->data, sizeof(Arg) * (args->len + 1));
    panic_if(!args->data, "out of memory");
    args->data[args->len++] = arg;
}

static bool is_empty_tuple(Ty const* ty) {
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_TUPLE_TARGET;
}

static bool is_fixed_tuple(Ty const* ty) {
    if (is_empty_tuple(ty)) return true;
    if (!ty_is_tuple(ty)) return false;
    TupleShape const* shape = ty->reference.target->declared.tuple;
    for (u32 i = 0; i < shape->arity; i++) {
        if (shape->flags[i]) return false;
    }
    return true;
}

static bool is_context_sensitive(Expr const* expr) {
    if (expr->tag != EXPR_FUNCTION) return false;
    for_each (i, vec_function_arg, &expr->function->args) {
        if (!i->initializer.binding.has_explicit_type) return true;
    }
    return false;
}

static Args collect_args(Checker* me, CallExpr const* call) {
    Args args = { .spread_index = -1 };
    LowerCtx ctx = { .generic = true, .transient = true };
    for_each (i, vec_expr, &call->args) {
        if (i->tag == EXPR_SPREAD) {
            Ty const* operand = ty_from_legacy(me, legacy_expr_type(i->spread), ctx);
            if (is_fixed_tuple(operand)) {
                TyList elements = is_empty_tuple(operand) ? (TyList){0} : ty_reference_args(me, operand);
                for (u32 j = 0; j < elements.len; j++) args_push(&args, (Arg){ .expr = i, .type = elements.data[j] });
                continue;
            }
            Ty const* element = ty_intrinsic(me, TY_ANY);
            if (ty_is_array(me, operand) || ty_is_tuple(operand)) element = ty_rest_element_type(me, operand);
            if (args.spread_index < 0) args.spread_index = (i32)args.len;
            args_push(&args, (Arg){ .expr = i, .type = element, .spread = true });
        } else {
            Type const* legacy = legacy_expr_type(i);
            args_push(&args, (Arg){
                .expr = i, .legacy = legacy, .type = ty_from_legacy(me, legacy, ctx), .context_sensitive = is_context_sensitive(i),
                .widened_without_context = i->tag == EXPR_OBJECT || i->tag == EXPR_ARRAY,
            });
        }
    }
    return args;
}

static Ty const* with_undefined(Checker* me, Ty const* type) {
    if (!get_error_config()->strict_null_checks) return type;
    Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

static ParamShape param_shape(Checker* me, Signature const* sig, TyMapper const* mapper) {
    signature_resolve(me, sig);
    ParamShape shape = { .min = sig->min_args };
    u32 cap = sig->param_count + 1;
    shape.types = malloc(sizeof(Ty const*) * cap);
    panic_if(!shape.types, "out of memory");
    for (u32 i = 0; i < sig->param_count; i++) {
        SigParam const* param = &sig->params[i];
        Ty const* type = ty_instantiate(me, param->type, mapper);
        if (!param->rest) {
            shape.types[shape.count++] = param->optional ? with_undefined(me, type) : type;
            continue;
        }
        if (type->kind == TY_TYPE_PARAM) {
            // `...args: T` with `T extends [number, string]` is called like the constraint says
            Ty const* constraint = ty_param_constraint(me, type);
            if (constraint && (ty_is_tuple(constraint) || is_empty_tuple(constraint)) && mapper) type = constraint;
        }
        if (is_empty_tuple(type)) {
            // No more parameters
        } else if (ty_is_tuple(type)) {
            TupleShape const* tuple = type->reference.target->declared.tuple;
            TyList elements = ty_reference_args(me, type);
            cap += elements.len;
            shape.types = realloc(shape.types, sizeof(Ty const*) * cap);
            panic_if(!shape.types, "out of memory");
            bool counting_required = shape.min == shape.count;
            for (u32 j = 0; j < elements.len; j++) {
                if (tuple->flags[j] & TUPLE_ELEMENT_REST) {
                    // Only a trailing one is understood
                    shape.rest_element = ty_rest_element_type(me, elements.data[j]);
                    break;
                }
                bool optional = (tuple->flags[j] & TUPLE_ELEMENT_OPTIONAL) != 0;
                shape.types[shape.count++] = optional ? with_undefined(me, elements.data[j]) : elements.data[j];
                if (optional) counting_required = false;
                if (counting_required) shape.min++;
            }
        } else if (ty_is_array(me, type) || (type->flags & TYF_ANY)) {
            shape.rest_element = ty_rest_element_type(me, type);
        } else {
            shape.rest_variadic = type;
        }
        break;
    }
    return shape;
}

static bool shape_has_rest(ParamShape const* shape) {
    return shape->rest_element || shape->rest_variadic;
}

static bool has_correct_arity(ParamShape const* shape, Args const* args) {
    if (args->spread_index >= 0) {
        return (u32)args->spread_index >= shape->min && (shape_has_rest(shape) || (u32)args->spread_index < shape->count);
    }
    return args->len >= shape->min && (shape_has_rest(shape) || args->len <= shape->count);
}

// The arguments from `from` on, as the tuple a variadic rest parameter receives
static Ty const* rest_args_tuple(Checker* me, Args const* args, u32 from, bool widen) {
    u32 arity = args->len > from ? args->len - from : 0;
    unsigned char* flags = calloc(arity ? arity : 1, 1);
    Ty const** elements = calloc(arity ? arity : 1, sizeof(Ty const*));
    for (u32 i = 0; i < arity; i++) {
        Arg const* arg = &args->data[from + i];
        if (arg->spread) {
            flags[i] = TUPLE_ELEMENT_REST;
            elements[i] = ty_array(me, arg->type, false);
        } else {
            elements[i] = widen ? ty_widened_literal(me, arg->type) : arg->type;
        }
    }
    TupleShape tuple = { .arity = arity, .flags = flags };
    Ty const* target = ty_tuple_target(me, &tuple);
    Ty const* ret = arity ? ty_reference(me, target, (TyList){ .data = elements, .len = arity }) : target;
    free(flags);
    free(elements);
    return ret;
}

// Type argument inference

static bool has_primitive_constraint(Checker* me, Ty const* param) {
    Ty const* constraint = ty_param_constraint(me, param);
    if (!constraint) return false;
    if (constraint->kind == TY_UNION || constraint->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < constraint->composite.types.len; i++) {
            Ty const* part = constraint->composite.types.data[i];
            if ((part->flags & (TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL | TYF_LITERAL | TYF_BOOLEAN | TYF_ENUM)) ||
                part->kind == TY_KEYOF
            ) {
                return true;
            }
        }
        return (constraint->flags & TYF_BOOLEAN) != 0;
    }
    return (constraint->flags & (TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL | TYF_LITERAL | TYF_BOOLEAN | TYF_ENUM)) ||
        constraint->kind == TY_KEYOF;
}

static bool is_param_at_top_level(Ty const* type, Ty const* param) {
    if (type == param) return true;
    if (type->kind == TY_UNION || type->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < type->composite.types.len; i++) {
            if (is_param_at_top_level(type->composite.types.data[i], param)) return true;
        }
    }
    return false;
}

static Signature const* last_call_signature(Checker* me, Ty const* ty) {
    Members const* members = ty_members(me, ty);
    return members->call_count ? members->call[members->call_count - 1] : nullptr;
}

// An instantiated signature shares its type parameters with its target (see signature_resolve()), so what they say
// about themselves (constraint, default) is still in terms of the outer type parameters that were instantiated away
static Ty const* instantiate_outer(Checker* me, Signature const* sig, Ty const* ty) {
    if (!ty || !sig->target) return ty;
    ty = instantiate_outer(me, sig->target, ty);
    return sig->mapper ? ty_instantiate(me, ty, sig->mapper) : ty;
}

// `fixed`: type arguments that are already decided, null for none. The seam for 4b3's context-sensitive second pass
static TyMapper const* infer_type_arguments(Checker* me, Signature const* sig, Args const* args, TyMapper const* fixed) {
    TyList params = sig->type_params;
    Inference inf;
    inference_init(&inf, params);
    for (u32 i = 0; fixed && i < inf.len; i++) {
        for (u32 j = 0; j < fixed->len; j++) {
            if (fixed->sources[j] == inf.params[i].param) inf.params[i].fixed = fixed->targets[j];
        }
    }

    ParamShape shape = param_shape(me, sig, nullptr);
    for (u32 i = 0; i < args->len; i++) {
        Arg const* arg = &args->data[i];
        if (i >= shape.count && shape.rest_variadic) {
            infer_types(me, &inf, rest_args_tuple(me, args, i, true), shape.rest_variadic, INFER_PRIORITY_NORMAL);
            break;
        }
        Ty const* param_type = i < shape.count ? shape.types[i] : shape.rest_element;
        if (!param_type) break;
        if (arg->context_sensitive) {
            // Its parameters are untyped until the contextual type is known (4b3): only what it return_types counts
            Signature const* source = last_call_signature(me, arg->type);
            Signature const* target = param_type->flags & TYF_OBJECT ? last_call_signature(me, param_type) : nullptr;
            if (source && target) {
                infer_types(
                    me, &inf, signature_return_type(me, source), signature_return_type(me, target), INFER_PRIORITY_NORMAL
                );
            }
            continue;
        }
        infer_types(me, &inf, arg->type, param_type, INFER_PRIORITY_NORMAL);
    }
    if (args->len <= shape.count && shape.rest_variadic && args->spread_index < 0) {
        infer_types(me, &inf, rest_args_tuple(me, args, shape.count, true), shape.rest_variadic, INFER_PRIORITY_NORMAL);
    }
    free(shape.types);

    Ty const* return_type = signature_return_type(me, sig);
    Ty const** results = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    bool* inferred = calloc(params.len ? params.len : 1, sizeof(bool));
    for (u32 i = 0; i < params.len; i++) {
        bool keep_literals = has_primitive_constraint(me, params.data[i]) || is_param_at_top_level(return_type, params.data[i]);
        results[i] = inference_call_result(me, &inf, i, keep_literals);
        inferred[i] = results[i] != nullptr;
        if (!results[i]) results[i] = params.data[i]; // stands for itself until the defaults are in, below
    }
    TyMapper const* mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    for (u32 i = 0; i < params.len; i++) {
        if (inferred[i]) continue;
        Ty const* fallback = instantiate_outer(me, sig, ty_param_default(me, params.data[i]));
        results[i] = fallback ? ty_instantiate(me, fallback, mapper) : ty_intrinsic(me, TY_UNKNOWN_);
        if (results[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
            // A default that mentions a type parameter that has none itself
            bool self = false;
            for (u32 j = 0; j < params.len; j++) self |= !inferred[j] && results[i] == params.data[j];
            if (self) results[i] = ty_intrinsic(me, TY_UNKNOWN_);
        }
        mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    }
    // An inference that doesn't satisfy the constraint is replaced by it, and the argument check reports the rest
    bool changed = false;
    for (u32 i = 0; i < params.len; i++) {
        Ty const* constraint = instantiate_outer(me, sig, ty_param_constraint(me, params.data[i]));
        if (!constraint || constraint == params.data[i]) continue;
        Ty const* instantiated = ty_instantiate(me, constraint, mapper);
        if (!ty_is_related(me, results[i], instantiated, TY_RELATION_ASSIGNABLE)) {
            results[i] = instantiated;
            changed = true;
        }
    }
    if (changed) mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    free(inferred);
    inference_drop(&inf);
    return mapper;
}

// Applicability

// The first argument that doesn't fit, -1 if they all do. `*param_type` is what it should have fit, `*arg_type` what
// it is (a tuple of the remaining arguments for a variadic rest parameter)
static i32 first_unfit_argument(
    Checker* me, ParamShape const* shape, Args const* args, TyRelation relation, Ty const** arg_type, Ty const** param_type
) {
    for (u32 i = 0; i < args->len; i++) {
        Arg const* arg = &args->data[i];
        Ty const* source = arg->type;
        Ty const* target;
        if (i < shape->count) {
            target = shape->types[i];
        } else if (shape->rest_element) {
            target = shape->rest_element;
        } else if (shape->rest_variadic) {
            source = rest_args_tuple(me, args, i, false);
            target = shape->rest_variadic;
        } else {
            break;
        }
        // HACK: the legacy checker says `any` for whatever it can't type, see check_assignable_or_error_internal()
        bool fits = (source->flags & TYF_ANY) || ty_is_related(me, source, target, relation);
        if (!fits && relation == TY_RELATION_STRICT_SUBTYPE && arg->context_sensitive) {
            fits = ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE);
        }
        if (!fits && relation == TY_RELATION_ASSIGNABLE && arg->widened_without_context) {
            // HACK(stage 4b3): the legacy checker widens the literals inside object and array literals whatever the
            // contextual type (`{ kind: "a" }` is `{ kind: string }`), so those only have to be comparable
            fits = ty_is_related(me, source, target, TY_RELATION_COMPARABLE);
        }
        if (!fits) {
            *arg_type = source;
            *param_type = target;
            return (i32)i;
        }
        if (i >= shape->count && shape->rest_variadic) break;
    }
    return -1;
}

typedef struct Candidate {
    Signature const* sig;
    TyMapper const* mapper; // inferred type arguments, null if the signature isn't generic
} Candidate;

typedef struct Resolution {
    Candidate chosen; // sig is null if nothing fit
    Ty const* ret;
} Resolution;

static Ty const* candidate_return_type(Checker* me, Candidate const* candidate) {
    return ty_instantiate(me, signature_return_type(me, candidate->sig), candidate->mapper);
}

static Span span_of_args(Args const* args, u32 from, u32 to) {
    return span_between(args->data[from].expr->outer_span, args->data[to].expr->outer_span);
}

static void report_arity(
    Checker* me, Signature* const* signatures, u32 count, Args const* args, CallExpr const* call, ErrorCtx* errs
) {
    u32 min = UINT32_MAX;
    u32 max = 0;
    u32 max_below = 0;
    u32 min_above = UINT32_MAX;
    bool has_rest = false;
    bool any_below = false;
    for (u32 i = 0; i < count; i++) {
        ParamShape shape = param_shape(me, signatures[i], nullptr);
        if (shape.min < min) min = shape.min;
        if (shape.count > max) max = shape.count;
        if (shape.count < args->len && (!any_below || shape.count > max_below)) {
            max_below = shape.count;
            any_below = true;
        }
        if (shape.min > args->len && shape.min < min_above) min_above = shape.min;
        has_rest |= shape_has_rest(&shape);
        free(shape.types);
    }
    Span name_span = call->callable.inner_span;
    if (args->spread_index >= 0) {
        error_ctx_error_d(errs, args->data[args->spread_index].expr->outer_span, 2556,
            "A spread argument must either have a tuple type or be passed to a rest parameter.");
    } else if (count > 1 && !has_rest && any_below && min_above != UINT32_MAX) {
        error_ctx_error_d(errs, name_span, 2575,
            "No overload expects ", args->len, " arguments, but overloads do exist that expect either ", max_below,
            " or ", min_above, " arguments.");
    } else {
        Span span = args->len > max && !has_rest ? span_of_args(args, max, args->len - 1) : name_span;
        if (has_rest) {
            error_ctx_error_d(errs, span, 2555, "Expected at least ", min, " arguments, but got ", args->len, ".");
        } else if (min == max) {
            error_ctx_error_d(errs, span, 2554, "Expected ", min, " arguments, but got ", args->len, ".");
        } else {
            error_ctx_error_d(errs, span, 2554, "Expected ", min, "-", max, " arguments, but got ", args->len, ".");
        }
    }
}

// Reports why the arguments don't fit the candidate. Returns the span it reported at
static Span report_unfit_argument(Checker* me, Candidate const* candidate, Args const* args, Span call_span, ErrorCtx* errs) {
    ParamShape shape = param_shape(me, candidate->sig, candidate->mapper);
    Ty const* arg_type = nullptr;
    Ty const* param_type = nullptr;
    i32 index = first_unfit_argument(me, &shape, args, TY_RELATION_ASSIGNABLE, &arg_type, &param_type);
    Span span = call_span;
    if (index >= 0) {
        Arg const* arg = &args->data[index];
        bool is_rest_tuple = arg_type != arg->type;
        span = is_rest_tuple ? span_of_args(args, (u32)index, args->len - 1) : arg->expr->outer_span;
        type_report_unfit_argument(is_rest_tuple ? nullptr : arg->legacy, arg_type, param_type, span, errs);
    }
    free(shape.types);
    return span;
}

static void report_no_match(
    Checker* me, Candidate const* unfit, u32 unfit_count, u32 signature_count, Args const* args, CallExpr const* call,
    Span call_span, ErrorCtx* errs
) {
    if (unfit_count == 1) {
        report_unfit_argument(me, &unfit[0], args, call_span, errs);
        return;
    }
    ErrorCtx collected = error_ctx_init();
    collected.phase = errs->phase;
    Span common_span = span_null;
    bool same_span = true;
    Error head = { .ts_code = 2769, .phase = errs->phase, .message = cstr_lit("No overload matches this call.") };
    u32 first = unfit_count > 3 ? unfit_count - 1 : 0;
    Error* parent = &head;
    if (unfit_count > 3) {
        error_sub_error_s(&head, span_null, TS_CODE_NONE, cstr_lit("The last overload gave the following error."));
        parent = vec_error_back_mut(&head.sub_errors);
    }
    for (u32 i = first; i < unfit_count; i++) {
        error_ctx_clear(&collected);
        Span span = report_unfit_argument(me, &unfit[i], args, call_span, &collected);
        if (i == first) common_span = span;
        same_span &= span.sv.buf == common_span.sv.buf && span.sv.size == common_span.sv.size;
        Error* under = parent;
        if (unfit_count <= 3) {
            cstr rendered = cstr_null;
            ty_display_signature(me, (TyDisplay){ .target = &rendered }, unfit[i].sig);
            error_sub_error_d(&head, span_null, TS_CODE_NONE,
                "Overload ", i + 1, " of ", signature_count, ", '", rendered, "', gave the following error.");
            cstr_drop(&rendered);
            under = vec_error_back_mut(&head.sub_errors);
        }
        for_each (e, vec_error, &collected.errors) {
            Error copy = error_clone(*e);
            copy.span = span_null;
            copy.ts_code = TS_CODE_NONE;
            vec_error_push(&under->sub_errors, copy);
        }
    }
    error_ctx_drop(&collected);
    head.span = same_span && span_valid(common_span) ? common_span : call->callable.inner_span;
    error_ctx_append_error(errs, head);
}

static Resolution resolve_with_signatures(
    Checker* me, Signature* const* signatures, u32 count, Args const* args, CallExpr const* call, Span call_span,
    ErrorCtx* errs
) {
    Resolution ret = {0};
    Candidate* unfit = calloc(count ? count : 1, sizeof(Candidate));
    u32 unfit_count = 0;
    TyRelation rounds[2] = { TY_RELATION_STRICT_SUBTYPE, TY_RELATION_ASSIGNABLE };
    for (u32 round = count > 1 ? 0 : 1; round < 2 && !ret.chosen.sig; round++) {
        unfit_count = 0;
        for (u32 i = 0; i < count && !ret.chosen.sig; i++) {
            Signature const* sig = signatures[i];
            signature_resolve(me, sig);
            ParamShape uninstantiated = param_shape(me, sig, nullptr);
            bool arity_ok = has_correct_arity(&uninstantiated, args);
            free(uninstantiated.types);
            if (!arity_ok) continue;
            Candidate candidate = { .sig = sig };
            if (sig->type_params.len) candidate.mapper = infer_type_arguments(me, sig, args, nullptr);
            ParamShape shape = param_shape(me, sig, candidate.mapper);
            Ty const* arg_type;
            Ty const* param_type;
            if (first_unfit_argument(me, &shape, args, rounds[round], &arg_type, &param_type) < 0) {
                ret.chosen = candidate;
            } else {
                unfit[unfit_count++] = candidate;
            }
            free(shape.types);
        }
    }

    if (ret.chosen.sig) {
        ret.ret = candidate_return_type(me, &ret.chosen);
    } else {
        if (errs) {
            if (unfit_count) {
                report_no_match(me, unfit, unfit_count, count, args, call, call_span, errs);
            } else {
                report_arity(me, signatures, count, args, call, errs);
            }
        }
        // What the call is taken to return anyway: the candidate that got furthest, or every possibility
        if (unfit_count) {
            ret.ret = candidate_return_type(me, &unfit[unfit_count - 1]);
        } else if (count == 1 && !signatures[0]->type_params.len) {
            ret.ret = signature_return_type(me, signatures[0]);
        } else {
            Ty const** return_types = malloc(sizeof(Ty const*) * count);
            u32 len = 0;
            for (u32 i = 0; i < count; i++) {
                signature_resolve(me, signatures[i]);
                if (signatures[i]->type_params.len) {
                    Candidate candidate = { .sig = signatures[i], .mapper = infer_type_arguments(me, signatures[i], args, nullptr) };
                    return_types[len++] = candidate_return_type(me, &candidate);
                } else {
                    return_types[len++] = signature_return_type(me, signatures[i]);
                }
            }
            ret.ret = ty_union(me, return_types, len, nullptr, (TyList){0});
            free(return_types);
        }
    }
    free(unfit);
    return ret;
}

static Ty const* resolve_call(Checker* me, Ty const* callee, CallExpr const* call, Span call_span, ErrorCtx* errs) {
    Ty const* any = ty_intrinsic(me, TY_ANY);
    if (callee->flags & TYF_ANY) return callee->flags & TYF_ERROR ? callee : any;
    Ty const* apparent = ty_apparent_type(me, callee);
    if (apparent->flags & TYF_ANY) return any;

    bool construct = call->uses_new || call->is_extends;
    if (apparent->kind == TY_UNION) {
        // TODO: tsc resolves a union of callables against one combined signature (and TS2349s if there isn't one)
        Ty const** return_types = malloc(sizeof(Ty const*) * apparent->composite.types.len);
        for (u32 i = 0; i < apparent->composite.types.len; i++) {
            return_types[i] = resolve_call(me, apparent->composite.types.data[i], call, call_span, errs);
        }
        Ty const* ret = ty_union(me, return_types, apparent->composite.types.len, nullptr, (TyList){0});
        free(return_types);
        return ret;
    }

    // An intersection is called through the signatures of all of its members
    Signature** signatures = nullptr;
    u32 count = 0;
    u32 other_count = 0; // signatures of the kind that wasn't asked for
    TyList parts = apparent->kind == TY_INTERSECTION ? apparent->composite.types : (TyList){ .data = &apparent, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        Members const* members = ty_members(me, ty_apparent_type(me, parts.data[i]));
        Signature* const* list = construct ? members->construct : members->call;
        u32 list_count = construct ? members->construct_count : members->call_count;
        other_count += construct ? members->call_count : members->construct_count;
        if (!list_count) continue;
        signatures = realloc(signatures, sizeof(Signature*) * (count + list_count));
        panic_if(!signatures, "out of memory");
        for (u32 j = 0; j < list_count; j++) signatures[count++] = list[j];
    }

    Ty const* ret = any;
    if (!count) {
        if (!errs) {
            // Reported by the typecheck pass
        } else if (call->is_extends) {
            cstr rendered = ty_to_cstr(me, callee);
            error_ctx_error_d(errs, call->callable.outer_span, 2507, "Type '", rendered, "' is not a constructor function type.");
            cstr_drop(&rendered);
        } else if (call->uses_new && other_count) {
            if (get_error_config()->no_implicit_any) {
                error_ctx_error_d(errs, call_span, 7009,
                    "'new' expression, whose target lacks a construct signature, implicitly has an 'any' type.");
            }
        } else if (call->uses_new) {
            error_ctx_error_d(errs, call->callable.outer_span, 2351, "This expression is not constructable.");
        } else if (other_count) {
            cstr rendered = ty_to_cstr(me, callee);
            error_ctx_error_d(errs, call->callable.outer_span, 2348,
                "Value of type '", rendered, "' is not callable. Did you mean to include 'new'?");
            cstr_drop(&rendered);
        } else {
            Error* e = error_ctx_error_d(errs, call->callable.outer_span, 2349, "This expression is not callable.");
            if (apparent->flags & TYF_OBJECT) {
                cstr rendered = ty_to_cstr(me, callee);
                error_sub_error_d(e, span_null, TS_CODE_NONE, "Type '", rendered, "' has no call signatures.");
                cstr_drop(&rendered);
            }
        }
        return ret;
    }

    if (call->is_extends) {
        // `class A extends B`: B only has to be constructable, `super(...)` is the call that gets checked
        free(signatures);
        return any;
    }
    if (call->uses_new) {
        bool all_abstract = true;
        for (u32 i = 0; i < count; i++) all_abstract &= (signatures[i]->flags & SIGNATURE_ABSTRACT) != 0;
        if (all_abstract) {
            error_ctx_error_d(errs, call_span, 2511, "Cannot create an instance of an abstract class.");
            free(signatures);
            return any;
        }
    }

    Args args = collect_args(me, call);
    ret = resolve_with_signatures(me, signatures, count, &args, call, call_span, errs).ret;
    free(args.data);
    free(signatures);
    return ret;
}

Ty const* ty_call_return_type(Checker* me, Ty const* callee, CallExpr const* call) {
    return resolve_call(me, callee, call, span_null, nullptr);
}

void ty_call_typecheck(Checker* me, Ty const* callee, CallExpr const* call, Span call_span, ErrorCtx* errs) {
    resolve_call(me, callee, call, call_span, errs);
}

// The type `await` gives for a value of the given type
// TODO: only unwraps Promise<T> and PromiseLike<T> by name, not arbitrary thenables. Should be the lib's Awaited<T>
Ty const* ty_awaited(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION) {
        Ty const** parts = malloc(sizeof(Ty const*) * ty->composite.types.len);
        for (u32 i = 0; i < ty->composite.types.len; i++) parts[i] = ty_awaited(me, ty->composite.types.data[i]);
        Ty const* ret = ty_union(me, parts, ty->composite.types.len, nullptr, (TyList){0});
        free(parts);
        return ret;
    }
    if (ty->kind == TY_REFERENCE && ty->reference.target->declared.decl &&
        vec_span_is_empty(&ty->reference.target->declared.decl->namespaces)
    ) {
        csview name = ty->reference.target->declared.decl->name.sv;
        TyList args = ty_reference_args(me, ty);
        if (args.len == 1 && (csview_equals(name, "Promise") || csview_equals(name, "PromiseLike"))) {
            return ty_awaited(me, args.data[0]);
        }
    }
    return ty;
}
