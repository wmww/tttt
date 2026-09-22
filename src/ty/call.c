#include "ty/ty_internal.h"
#include "scope.h"
#include "source_file.h"
#include "program.h"
#include "error.h"

// Call resolution on Signatures, the way tsc observably does it (resolveCall): collect the call or construct
// signatures of the callee, and take the first one the arguments fit: right number of them, type arguments inferred
// from them, each one assignable to its parameter. With several candidates a first round wants the arguments to be
// subtypes. If nothing fits, the errors are about the candidates that came closest.
//
// Arguments are checked with their parameter's type as the contextual type (ty_check_expr), per candidate. For a
// generic candidate that is two passes, like tsc: everything that isn't context sensitive is checked against the
// uninstantiated parameter types and inferred from, then the context-sensitive arguments (function expressions with
// unannotated parameters and literals that contain them) are checked one by one against their parameter type
// instantiated with what is known so far, and what they return is inferred from too.

typedef struct Arg {
    Expr const* expr; // For a spread tuple, the spread expression every element came from
    Ty const* type; // As last checked, see check_args()
    bool part_of_spread; // An element of a spread tuple: `type` is all there is, `expr` is the whole spread
    bool spread; // `...xs` of something that isn't a fixed tuple: stands for any number of arguments of this type
    bool context_sensitive;
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

static Args collect_args(Checker* me, CallExpr const* call) {
    Args args = { .spread_index = -1 };
    for_each (i, vec_expr, &call->args) {
        if (i->tag == EXPR_SPREAD) {
            Ty const* operand = ty_expr_type(me, i->spread);
            if (ty_is_tuple(operand) || is_empty_tuple(operand)) {
                // Each element is an argument of its own, a rest element stands for any number of them (tsc's
                // getEffectiveCallArguments)
                TupleShape const* shape = ty_is_tuple(operand) ? operand->reference.target->declared.tuple : operand->declared.tuple;
                TyList elements = ty_is_tuple(operand) ? ty_reference_args(me, operand) : (TyList){0};
                for (u32 j = 0; j < shape->arity; j++) {
                    if (shape->flags[j] & TUPLE_ELEMENT_REST) {
                        if (args.spread_index < 0) args.spread_index = (i32)args.len;
                        args_push(&args, (Arg){ .expr = i, .type = ty_rest_element_type(me, elements.data[j]), .spread = true });
                    } else {
                        args_push(&args, (Arg){ .expr = i, .type = elements.data[j], .part_of_spread = true });
                    }
                }
                continue;
            }
            Ty const* element = ty_is_array(me, operand) || ty_is_tuple(operand) ?
                ty_rest_element_type(me, operand) : ty_iterated_type(me, ITERATION_SPREAD, operand, span_null, nullptr);
            if (args.spread_index < 0) args.spread_index = (i32)args.len;
            args_push(&args, (Arg){ .expr = i, .type = element, .spread = true });
        } else {
            args_push(&args, (Arg){ .expr = i, .context_sensitive = ty_expr_is_context_sensitive(i) });
        }
    }
    return args;
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
            shape.types[shape.count++] = param->optional ? ty_optional_param_type(me, type) : type;
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
                shape.types[shape.count++] = optional ? ty_optional_param_type(me, elements.data[j]) : elements.data[j];
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

static Ty const* shape_type_at(ParamShape const* shape, u32 i) {
    return i < shape->count ? shape->types[i] : shape->rest_element;
}

// Checks argument i with its parameter's type as the contextual type
static void check_arg(Checker* me, ParamShape const* shape, Args* args, u32 i) {
    Arg* arg = &args->data[i];
    if (arg->spread || arg->part_of_spread) return;
    arg->type = ty_check_expr(me, arg->expr, shape_type_at(shape, i));
}

// `skip_sensitive`: leave out the context-sensitive arguments, which a contextual type would type for good (tsc's
// CheckMode.SkipContextSensitive, while it's not known yet which overload they'll be checked against)
static void check_args(Checker* me, ParamShape const* shape, Args* args, bool skip_sensitive) {
    for (u32 i = 0; i < args->len; i++) {
        if (!skip_sensitive || !args->data[i].context_sensitive) check_arg(me, shape, args, i);
    }
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
            Ty const* type = arg->type ? arg->type : ty_expr_type(me, arg->expr);
            elements[i] = widen ? ty_widened_literal(me, type) : type;
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
            if ((part->flags & (TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL_LIKE | TYF_LITERAL | TYF_BOOLEAN | TYF_ENUM)) ||
                part->kind == TY_KEYOF
            ) {
                return true;
            }
        }
        return (constraint->flags & TYF_BOOLEAN) != 0;
    }
    return (constraint->flags & (TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL_LIKE | TYF_LITERAL | TYF_BOOLEAN | TYF_ENUM)) ||
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

// A generic call inferring from its arguments, as the calls nested in them see it (tsc's getInferenceContext)
typedef struct OuterInference {
    Signature const* sig;
    Inference* inf;
} OuterInference;

// What the outer call has inferred so far, the rest being silent never (which nothing is inferred from): tsc's
// cloneInferenceContext(outerContext, InferenceFlags.NoDefault)
static TyMapper const* outer_inference_mapper(Checker* me, OuterInference const* outer) {
    TyList params = outer->sig->type_params;
    Ty const** results = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    for (u32 i = 0; i < params.len; i++) {
        results[i] = inference_call_result(me, outer->inf, i, false);
        if (!results[i]) results[i] = ty_intrinsic(me, TY_SILENT_NEVER);
    }
    return ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
}

// What the contextual return type alone says about the type parameters (tsc's InferenceContext.returnMapper, from
// cloneInferredPartOfContext): the ones it says nothing about stay themselves. Null if it says nothing
static TyMapper const* return_inference_mapper(Checker* me, Signature const* sig, Inference* inf) {
    TyList params = sig->type_params;
    Ty const** results = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    bool any = false;
    for (u32 i = 0; i < params.len; i++) {
        results[i] = inf->params[i].co_len || inf->params[i].contra_len ? inference_call_result(me, inf, i, true) : nullptr;
        any |= results[i] != nullptr;
        if (!results[i]) results[i] = params.data[i];
    }
    return any ? ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len }) : nullptr;
}

// tsc's instantiateInstantiableTypes: only type variables, at the top level or in unions and intersections
static Ty const* instantiate_instantiable(Checker* me, Ty const* type, TyMapper const* mapper) {
    if (type->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) return ty_instantiate(me, type, mapper);
    if (type->kind != TY_UNION && type->kind != TY_INTERSECTION) return type;
    TyList parts = type->composite.types;
    Ty const** mapped = malloc(sizeof(Ty const*) * (parts.len ? parts.len : 1));
    panic_if(!mapped, "out of memory");
    bool changed = false;
    for (u32 i = 0; i < parts.len; i++) {
        mapped[i] = instantiate_instantiable(me, parts.data[i], mapper);
        changed |= mapped[i] != parts.data[i];
    }
    Ty const* ret = !changed ? type : type->kind == TY_UNION ?
        ty_union(me, mapped, parts.len, nullptr, (TyList){0}) : ty_intersection(me, mapped, parts.len, nullptr, (TyList){0});
    free(mapped);
    return ret;
}

// The mapper for what has been inferred so far: common supertypes of the candidates, then defaults, then constraints
// `fixing`: for the contextual type of a context-sensitive argument, which decides the type parameters it mentions
static TyMapper const* inference_mapper(Checker* me, Signature const* sig, Inference* inf, bool fixing) {
    TyList params = sig->type_params;
    Ty const* return_type = signature_return_type(me, sig);
    Ty const** results = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    bool* inferred = calloc(params.len ? params.len : 1, sizeof(bool));
    for (u32 i = 0; i < params.len; i++) {
        // tsc: literals widen unless the constraint is primitive, or the type parameter is at the top level of the
        // return type and hasn't been fixed
        bool keep_literals = has_primitive_constraint(me, params.data[i]) ||
            (!fixing && !inf->params[i].fixed && is_param_at_top_level(return_type, params.data[i]));
        results[i] = inference_call_result(me, inf, i, keep_literals);
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
    return mapper;
}

static void infer_from_arg(Checker* me, Inference* inf, ParamShape const* shape, Args const* args, u32 i) {
    Ty const* param_type = shape_type_at(shape, i);
    Arg const* arg = &args->data[i];
    if (arg->type && !arg->spread && !arg->part_of_spread && arg->expr->tag == EXPR_ARRAY && ty_is_array(me, arg->type)) {
        inf->array_literals = realloc(inf->array_literals, sizeof(Ty const*) * (inf->array_literal_count + 1));
        panic_if(!inf->array_literals, "out of memory");
        inf->array_literals[inf->array_literal_count++] = arg->type;
    }
    if (param_type && args->data[i].type) infer_types(me, inf, args->data[i].type, param_type, INFER_PRIORITY_NORMAL);
}

// Whether checking a context-sensitive argument against `param_type` fixes type parameter `param`: its parameters are
// typed for good, so what they mention is decided. The contextual return type isn't (tsc instantiates it with the
// non-fixing mapper). Anything other than a single signature is taken to decide everything it mentions
static bool fixed_by_argument(Checker* me, Ty const* param_type, Ty const* param) {
    if (!param_type) return false;
    Ty const* unknown = ty_intrinsic(me, TY_UNKNOWN_);
    TyMapper const* probe = ty_mapper_new(me, (TyList){ .data = &param, .len = 1 }, (TyList){ .data = &unknown, .len = 1 });
    Members const* members = param_type->kind == TY_ANONYMOUS ? ty_members(me, param_type) : nullptr;
    if (!members || members->call_count != 1 || members->prop_count || members->construct_count) {
        return ty_instantiate(me, param_type, probe) != param_type;
    }
    Signature const* contextual = members->call[0];
    signature_resolve(me, contextual);
    for (u32 i = 0; i < contextual->param_count; i++) {
        if (ty_instantiate(me, contextual->params[i].type, probe) != contextual->params[i].type) return true;
    }
    return false;
}

// `contextual`: the contextual type of the call, which the return type is inferred from at a low priority
static TyMapper const* infer_type_arguments(
    Checker* me, Signature const* sig, Args* args, Ty const* contextual, Ty const* this_arg, bool skip_sensitive
) {
    Inference inf;
    inference_init(&inf, sig->type_params);
    ParamShape shape = param_shape(me, sig, nullptr);

    // Nested in an argument of another generic call, the contextual type mentions its type parameters: what it has
    // inferred so far stands in for them
    OuterInference const* outer = my outer_inference;
    my outer_inference = nullptr;
    if (contextual && outer && (contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        contextual = ty_instantiate(me, contextual, outer_inference_mapper(me, outer));
    }
    OuterInference self = { .sig = sig, .inf = &inf };

    TyMapper const* return_mapper = nullptr;
    if (contextual && !(contextual->flags & (TYF_ANY | TYF_COULD_CONTAIN_TYPE_VARIABLES)) && true) {
        Ty const* return_type = signature_return_type(me, sig);
        if (return_type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
            infer_types(me, &inf, contextual, return_type, INFER_PRIORITY_RETURN_TYPE);
            return_mapper = return_inference_mapper(me, sig, &inf);
        }
    }

    if (sig->this_type && this_arg) infer_types(me, &inf, this_arg, sig->this_type, INFER_PRIORITY_NORMAL);

    // First pass: everything that isn't context sensitive
    bool any_sensitive = false;
    for (u32 i = 0; i < args->len; i++) {
        Arg* arg = &args->data[i];
        if (i >= shape.count && shape.rest_variadic) break;
        if (arg->context_sensitive) {
            any_sensitive = true;
            continue;
        }
        my outer_inference = &self;
        if (return_mapper && !arg->spread && !arg->part_of_spread) {
            // What the return type says about the type parameters helps type the argument (a tuple, a literal)
            Ty const* param_type = shape_type_at(&shape, i);
            arg->type = ty_check_expr(me, arg->expr, param_type ? instantiate_instantiable(me, param_type, return_mapper) : nullptr);
        } else {
            check_arg(me, &shape, args, i);
        }
        my outer_inference = nullptr;
        infer_from_arg(me, &inf, &shape, args, i);
    }
    // Second pass: each context-sensitive argument sees what is known by then, which is thereby decided
    for (u32 i = 0; any_sensitive && !skip_sensitive && i < args->len; i++) {
        Arg* arg = &args->data[i];
        if (i >= shape.count && shape.rest_variadic) break;
        if (!arg->context_sensitive) continue;
        TyMapper const* so_far = inference_mapper(me, sig, &inf, true);
        Ty const* param_type = shape_type_at(&shape, i);
        for (u32 j = 0; j < inf.len; j++) {
            if ((inf.params[j].co_len || inf.params[j].contra_len) && fixed_by_argument(me, param_type, sig->type_params.data[j])) {
                inf.params[j].fixed = so_far->targets[j];
            }
        }
        arg->type = ty_check_expr(me, arg->expr, param_type ? ty_instantiate(me, param_type, so_far) : nullptr);
        infer_from_arg(me, &inf, &shape, args, i);
    }
    // A variadic rest parameter takes the remaining arguments as a tuple
    if (shape.rest_variadic) {
        u32 from = shape.count < args->len ? shape.count : args->len;
        for (u32 i = from; i < args->len; i++) {
            if (!args->data[i].type) args->data[i].type = ty_check_expr(me, args->data[i].expr, nullptr);
        }
        if (args->len > shape.count || args->spread_index < 0) {
            infer_types(me, &inf, rest_args_tuple(me, args, from, true), shape.rest_variadic, INFER_PRIORITY_NORMAL);
        }
    }
    free(shape.types);

    TyMapper const* mapper = inference_mapper(me, sig, &inf, false);
    inference_drop(&inf);
    my outer_inference = outer;
    return mapper;
}

// Applicability

// The first argument that doesn't fit, -1 if they all do. `*param_type` is what it should have fit, `*arg_type` what
// it is (a tuple of the remaining arguments for a variadic rest parameter)
static i32 first_unfit_argument(
    Checker* me, ParamShape const* shape, Args const* args, TyRelation relation, bool skip_sensitive,
    Ty const** arg_type, Ty const** param_type
) {
    for (u32 i = 0; i < args->len; i++) {
        Arg const* arg = &args->data[i];
        if (skip_sensitive && arg->context_sensitive) {
            if (i >= shape->count && shape->rest_variadic) break;
            continue;
        }
        Ty const* source = arg->type ? arg->type : ty_expr_type(me, arg->expr);
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
        bool fits = (source->flags & TYF_ANY) || ty_is_related(me, source, target, relation);
        if (!fits && relation == TY_RELATION_STRICT_SUBTYPE && arg->context_sensitive) {
            fits = ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE);
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

// What a call brings besides its arguments
typedef struct CallInfo {
    CallExpr const* call;
    Ty const* contextual; // contextual type of the call expression, may be null
    VecSpannedType const* type_arg_nodes; // explicit type arguments, null if none
    TyList type_args;
    Candidate* chosen_out; // Where to leave the signature the call resolved to, may be null
    Ty const* this_arg; // What `this` is in the call (the receiver of a method call, else void), see this_argument()
    Span this_span; // Where a `this` that doesn't fit is reported
} CallInfo;

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

// Whether what `this` is in the call fits the `this` parameter of the candidate
static bool this_fits(Checker* me, Candidate const* candidate, CallInfo const* info, TyRelation relation) {
    Ty const* this_type = candidate->sig->this_type;
    if (!this_type || !info->this_arg || (info->this_arg->flags & TYF_ANY)) return true;
    return ty_is_related(me, info->this_arg, ty_instantiate(me, this_type, candidate->mapper), relation);
}

// Reports why the arguments don't fit the candidate. Returns the span it reported at
static Span report_unfit_argument(
    Checker* me, Candidate const* candidate, Args* args, CallInfo const* info, Span call_span, ErrorCtx* errs
) {
    if (!this_fits(me, candidate, info, TY_RELATION_ASSIGNABLE)) {
        Ty const* target = ty_instantiate(me, candidate->sig->this_type, candidate->mapper);
        ty_report_unfit_this(me, info->this_arg, target, info->this_span, errs);
        return info->this_span;
    }
    ParamShape shape = param_shape(me, candidate->sig, candidate->mapper);
    check_args(me, &shape, args, false);
    Ty const* arg_type = nullptr;
    Ty const* param_type = nullptr;
    i32 index = first_unfit_argument(me, &shape, args, TY_RELATION_ASSIGNABLE, false, &arg_type, &param_type);
    Span span = call_span;
    if (index >= 0) {
        Arg const* arg = &args->data[index];
        bool is_rest_tuple = arg_type != arg->type;
        span = is_rest_tuple ? span_of_args(args, (u32)index, args->len - 1) : arg->expr->outer_span;
        ty_report_unfit_argument(me, arg_type, param_type, span, errs);
    }
    free(shape.types);
    return span;
}

static void report_no_match(
    Checker* me, Candidate const* unfit, u32 unfit_count, u32 signature_count, Args* args, CallInfo const* info,
    Span call_span, ErrorCtx* errs
) {
    CallExpr const* call = info->call;
    if (unfit_count == 1) {
        report_unfit_argument(me, &unfit[0], args, info, call_span, errs);
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
        Span span = report_unfit_argument(me, &unfit[i], args, info, call_span, &collected);
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

static u32 min_type_arguments(Checker* me, Signature const* sig) {
    u32 min = sig->type_params.len;
    while (min && ty_param_default(me, sig->type_params.data[min - 1])) min--;
    return min;
}

static bool has_correct_type_argument_arity(Checker* me, Signature const* sig, CallInfo const* info) {
    if (!info->type_arg_nodes) return true;
    return info->type_args.len >= min_type_arguments(me, sig) && info->type_args.len <= sig->type_params.len;
}

// The mapper explicit type arguments make, defaults filled in
static TyMapper const* explicit_type_arguments(Checker* me, Signature const* sig, CallInfo const* info) {
    TyList params = sig->type_params;
    if (!params.len) return nullptr;
    Ty const** results = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 i = 0; i < params.len; i++) results[i] = i < info->type_args.len ? info->type_args.data[i] : params.data[i];
    TyMapper const* mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    for (u32 i = info->type_args.len; i < params.len; i++) {
        Ty const* fallback = instantiate_outer(me, sig, ty_param_default(me, params.data[i]));
        results[i] = fallback ? ty_instantiate(me, fallback, mapper) : ty_intrinsic(me, TY_UNKNOWN_);
        mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    }
    return mapper;
}

static void report_type_argument_arity(
    Checker* me, Signature* const* signatures, u32 count, CallInfo const* info, ErrorCtx* errs
) {
    u32 min = UINT32_MAX;
    u32 max = 0;
    for (u32 i = 0; i < count; i++) {
        signature_resolve(me, signatures[i]);
        u32 sig_min = min_type_arguments(me, signatures[i]);
        if (sig_min < min) min = sig_min;
        if (signatures[i]->type_params.len > max) max = signatures[i]->type_params.len;
    }
    Span span = span_between(
        vec_spanned_type_at(info->type_arg_nodes, 0)->span, vec_spanned_type_back(info->type_arg_nodes)->span
    );
    if (min == max) {
        error_ctx_error_d(errs, span, 2558, "Expected ", min, " type arguments, but got ", info->type_args.len, ".");
    } else {
        error_ctx_error_d(errs, span, 2558, "Expected ", min, "-", max, " type arguments, but got ", info->type_args.len, ".");
    }
}

// TS2344 for explicit type arguments. Returns false if one was reported
static bool check_type_argument_constraints(
    Checker* me, Signature const* sig, TyMapper const* mapper, CallInfo const* info, ErrorCtx* errs
) {
    bool satisfied = true;
    for (u32 i = 0; i < info->type_args.len && i < sig->type_params.len; i++) {
        Ty const* constraint = instantiate_outer(me, sig, ty_param_constraint(me, sig->type_params.data[i]));
        if (!constraint) continue;
        constraint = ty_instantiate(me, constraint, mapper);
        if (ty_is_related(me, info->type_args.data[i], constraint, TY_RELATION_ASSIGNABLE)) continue;
        satisfied = false;
        if (!errs) continue;
        cstr arg_str = ty_to_cstr(me, info->type_args.data[i]);
        cstr constraint_str = ty_to_cstr(me, constraint);
        error_ctx_error_d(errs, vec_spanned_type_at(info->type_arg_nodes, i)->span, 2344,
            "Type '", arg_str, "' does not satisfy the constraint '", constraint_str, "'.");
        c_drop(cstr, &arg_str, &constraint_str);
    }
    return satisfied;
}

static TyMapper const* candidate_mapper(
    Checker* me, Signature const* sig, Args* args, CallInfo const* info, bool skip_sensitive
) {
    if (!sig->type_params.len) return nullptr;
    return info->type_arg_nodes ? explicit_type_arguments(me, sig, info) :
        infer_type_arguments(me, sig, args, info->contextual, info->this_arg, skip_sensitive);
}

static Resolution resolve_with_signatures(
    Checker* me, Signature* const* signatures, u32 count, Args* args, CallInfo const* info, Span call_span,
    ErrorCtx* errs
) {
    CallExpr const* call = info->call;
    Resolution ret = {0};
    Candidate* unfit = calloc(count ? count : 1, sizeof(Candidate));
    u32 unfit_count = 0;
    bool type_argument_arity_ok = false;
    TyRelation rounds[2] = { TY_RELATION_STRICT_SUBTYPE, TY_RELATION_ASSIGNABLE };
    // With a choice to make, candidates are first tried without their context-sensitive arguments, which are only
    // checked (and typed for good) against the first candidate the rest fit (tsc's argCheckMode)
    bool skip_sensitive = false;
    if (count > 1 || (count == 1 && signatures[0]->type_params.len)) {
        for (u32 i = 0; i < args->len; i++) skip_sensitive |= args->data[i].context_sensitive;
    }
    for (u32 round = count > 1 ? 0 : 1; round < 2 && !ret.chosen.sig; round++) {
        unfit_count = 0;
        for (u32 i = 0; i < count && !ret.chosen.sig; i++) {
            Signature const* sig = signatures[i];
            signature_resolve(me, sig);
            if (!has_correct_type_argument_arity(me, sig, info)) continue;
            type_argument_arity_ok = true;
            ParamShape uninstantiated = param_shape(me, sig, nullptr);
            bool arity_ok = has_correct_arity(&uninstantiated, args);
            free(uninstantiated.types);
            if (!arity_ok) continue;
            Candidate candidate = { .sig = sig, .mapper = candidate_mapper(me, sig, args, info, skip_sensitive) };
            ParamShape shape = param_shape(me, sig, candidate.mapper);
            check_args(me, &shape, args, skip_sensitive);
            Ty const* arg_type;
            Ty const* param_type;
            bool fits = first_unfit_argument(me, &shape, args, rounds[round], skip_sensitive, &arg_type, &param_type) < 0;
            if (fits && skip_sensitive) {
                // Again with everything, for this candidate and the ones after it
                skip_sensitive = false;
                free(shape.types);
                candidate.mapper = candidate_mapper(me, sig, args, info, false);
                shape = param_shape(me, sig, candidate.mapper);
                check_args(me, &shape, args, false);
                fits = first_unfit_argument(me, &shape, args, rounds[round], false, &arg_type, &param_type) < 0;
            }
            fits = fits && this_fits(me, &candidate, info, rounds[round]);
            if (fits) {
                ret.chosen = candidate;
            } else {
                unfit[unfit_count++] = candidate;
            }
            free(shape.types);
        }
    }

    if (ret.chosen.sig) {
        if (info->type_arg_nodes && ret.chosen.mapper) check_type_argument_constraints(me, ret.chosen.sig, ret.chosen.mapper, info, errs);
        ret.ret = candidate_return_type(me, &ret.chosen);
    } else {
        if (errs) {
            if (!type_argument_arity_ok) {
                report_type_argument_arity(me, signatures, count, info, errs);
            } else if (unfit_count) {
                report_no_match(me, unfit, unfit_count, count, args, info, call_span, errs);
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
                if (signatures[i]->type_params.len && has_correct_type_argument_arity(me, signatures[i], info)) {
                    Candidate candidate = { .sig = signatures[i], .mapper = candidate_mapper(me, signatures[i], args, info, false) };
                    return_types[len++] = candidate_return_type(me, &candidate);
                } else if (!signatures[i]->type_params.len) {
                    return_types[len++] = signature_return_type(me, signatures[i]);
                }
            }
            ret.ret = len ? ty_union(me, return_types, len, nullptr, (TyList){0}) : ty_intrinsic(me, TY_ANY);
            free(return_types);
        }
    }
    free(unfit);
    return ret;
}

// What `this` is in a call (tsc's getThisArgumentType): the receiver of a method call, else void
static Ty const* this_argument(Checker* me, CallExpr const* call, Span call_span, Span* span_out) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    while (callee->tag == EXPR_UNARY_OP && callee->unary_op->op == UNARY_OP_RIGHT_BANG) callee = &callee->unary_op->expr;
    if (callee->tag != EXPR_PROPERTY) {
        *span_out = call_span;
        return ty_intrinsic(me, TY_VOID);
    }
    PropertyExpr const* property = callee->property;
    *span_out = property->expr.outer_span;
    Ty const* receiver = ty_expr_type(me, &property->expr);
    return property->optional_chain ? ty_non_nullable(me, receiver) : receiver;
}

static Ty const* resolve_call(Checker* me, Ty const* callee, CallInfo const* info, Span call_span, ErrorCtx* errs) {
    CallExpr const* call = info->call;
    Ty const* any = ty_intrinsic(me, TY_ANY);
    if (callee->flags & TYF_ANY) return callee->flags & TYF_ERROR ? callee : any;
    Ty const* apparent = ty_apparent_type(me, callee);
    if (apparent->flags & TYF_ANY) return any;

    bool construct = call->uses_new || call->is_extends;
    // A union is called through the signatures its members have in common (tsc's getUnionSignatures)
    Signature* const* union_sigs = nullptr;
    u32 union_count = 0;
    if (apparent->kind == TY_UNION) union_sigs = ty_union_signatures(me, apparent->composite.types, construct, &union_count);
    if (apparent->kind == TY_UNION && !union_sigs && !call->is_extends) {
        bool all_have = true;
        for (u32 i = 0; i < apparent->composite.types.len && all_have; i++) {
            Members const* members = ty_members(me, ty_apparent_type(me, apparent->composite.types.data[i]));
            all_have = construct ? members->construct_count > 0 : members->call_count > 0;
        }
        if (all_have) {
            if (errs) {
                cstr rendered = ty_to_cstr(me, callee);
                Error* e = error_ctx_error_d(errs, call->callable.outer_span, construct ? 2351 : 2349,
                    construct ? "This expression is not constructable." : "This expression is not callable.");
                error_sub_error_d(e, span_null, TS_CODE_NONE, "Each member of the union type '", rendered, "' has ",
                    construct ? "construct " : "", "signatures, but none of those signatures are compatible with each other.");
                cstr_drop(&rendered);
            }
            return any;
        }
    }
    if (apparent->kind == TY_UNION && !union_sigs) {
        // TODO: tsc's errors for unions with members that can't be called ("Not all constituents of type ...")
        Ty const** return_types = malloc(sizeof(Ty const*) * apparent->composite.types.len);
        for (u32 i = 0; i < apparent->composite.types.len; i++) {
            return_types[i] = resolve_call(me, apparent->composite.types.data[i], info, call_span, errs);
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
    if (union_sigs) {
        parts.len = 0;
        signatures = malloc(sizeof(Signature*) * union_count);
        panic_if(!signatures, "out of memory");
        for (u32 i = 0; i < union_count; i++) signatures[count++] = union_sigs[i];
    }
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
            if (compiler_options()->no_implicit_any) {
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

    CallInfo with_this = *info;
    if (!call->uses_new) {
        bool any_this = false;
        for (u32 i = 0; i < count && !any_this; i++) {
            signature_resolve(me, signatures[i]);
            any_this = signatures[i]->this_type != nullptr;
        }
        if (any_this) with_this.this_arg = this_argument(me, call, call_span, &with_this.this_span);
    }
    Args args = collect_args(me, call);
    Resolution resolution = resolve_with_signatures(me, signatures, count, &args, &with_this, call_span, errs);
    ret = resolution.ret;
    if (info->chosen_out) *info->chosen_out = resolution.chosen;
    free(args.data);
    free(signatures);
    return ret;
}

static Ty const* resolve_into(
    Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual, Span call_span, ErrorCtx* errs, Candidate* chosen_out
) {
    CallInfo info = { .call = call, .contextual = contextual, .chosen_out = chosen_out };
    Ty const* type_args[32];
    if (call->callable.tag == EXPR_GENERIC_PARAMS) {
        // `f<A, B>(x)`: the callee type is that of `f`
        info.type_arg_nodes = &call->callable.generic_params->params;
        u32 len = 0;
        for_each (i, vec_spanned_type, info.type_arg_nodes) {
            if (len < c_arraylen(type_args)) type_args[len++] = ty_from_node(me, &i->type, (LowerCtx){ .generic = true });
        }
        info.type_args = (TyList){ .data = type_args, .len = len };
    }
    return resolve_call(me, callee, &info, call_span, errs);
}

static Ty const* resolve(Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual, Span call_span, ErrorCtx* errs) {
    return resolve_into(me, callee, call, contextual, call_span, errs, nullptr);
}

static bool has_predicate_or_declared_never(Checker* me, Signature const* sig) {
    return signature_predicate(me, sig) || signature_declared_never(sig);
}

// What control flow analysis wants to know about a call (tsc's getEffectsSignature()): the type predicate of the
// signature it resolves to, and whether that is declared to return `never`. A callee with one signature that isn't
// generic is that signature whatever the arguments are; anything else is resolved, which checks the arguments: the
// caller (flow.c) sees to it that this happens in uncached mode
CallEffects ty_call_effects(Checker* me, Ty const* callee, CallExpr const* call) {
    CallEffects none = {0};
    if (!callee || call->uses_new || call->is_extends) return none;
    Ty const* apparent = ty_apparent_type(me, callee);
    if ((apparent->flags & TYF_ANY) || !(apparent->flags & TYF_OBJECT)) return none;
    Members const* members = ty_members(me, apparent);
    if (!members->call_count) return none;
    Candidate chosen = {0};
    signature_resolve(me, members->call[0]);
    if (members->call_count == 1 && !members->call[0]->type_params.len) {
        chosen.sig = members->call[0];
    } else {
        bool some = false;
        for (u32 i = 0; i < members->call_count && !some; i++) some = has_predicate_or_declared_never(me, members->call[i]);
        if (!some) return none;
        resolve_into(me, callee, call, nullptr, span_null, nullptr, &chosen);
    }
    if (!chosen.sig || !has_predicate_or_declared_never(me, chosen.sig)) return none;
    CallEffects ret = { .returns_never = signature_declared_never(chosen.sig) };
    TyPredicate const* predicate = signature_predicate(me, chosen.sig);
    if (predicate) {
        ret.predicate = *predicate;
        ret.has_predicate = true;
        if (predicate->type && chosen.mapper) ret.predicate.type = ty_instantiate(me, predicate->type, chosen.mapper);
    }
    return ret;
}

// `import("./a")`: a promise of the module (of what its `export =` names)
static Ty const* dynamic_import_type(Checker* me, CallExpr const* call) {
    if (call->callable.tag != EXPR_IDENTIFIER || call->callable.identifier.keyword != KEYWORD_IDENTIFIER_IMPORT) {
        return nullptr;
    }
    ModuleRef const* ref = call->callable.identifier.module_ref;
    Ty const* module = ty_intrinsic(me, TY_ANY);
    if (ref && ref->module && !ref->module->is_shorthand_module) {
        // TODO: esModuleInterop's synthetic `{ default: ... }` for an `export =` module
        module = ty_type_of_symbol(me, module_export_equals(ref->module));
    }
    Symbol const* promise_symbol = program_get_global(my program, c_sv("Promise"), SYMBOL_TYPE);
    if (!promise_symbol) return ty_intrinsic(me, TY_ANY); // TODO: TS2712
    Ty const* promise = ty_declared_type_of(me, promise_symbol);
    if (promise->kind != TY_DECLARED || promise->declared.type_params.len != 1) return ty_intrinsic(me, TY_ANY);
    return ty_reference(me, promise, (TyList){ .data = &module, .len = 1 });
}

Ty const* ty_call_return_type(Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual) {
    Ty const* imported = dynamic_import_type(me, call);
    if (imported) return imported;
    return resolve(me, callee, call, contextual, span_null, nullptr);
}

void ty_call_typecheck(Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual, Span call_span, ErrorCtx* errs) {
    resolve(me, callee, call, contextual, call_span, errs);
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
    if (ty->kind == TY_REFERENCE && ty->reference.target->declared.symbol &&
        !ty->reference.target->declared.symbol->parent
    ) {
        csview name = ty->reference.target->declared.symbol->name;
        TyList args = ty_reference_args(me, ty);
        if (args.len == 1 && (csview_equals(name, "Promise") || csview_equals(name, "PromiseLike"))) {
            return ty_awaited(me, args.data[0]);
        }
    }
    return ty;
}
