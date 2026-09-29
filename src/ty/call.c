#include "ty/ty_internal.h"
#include "scope.h"
#include "source_file.h"
#include "program.h"
#include "error.h"
#include "class_decl.h"

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
    // `type` is all there is: an element of a spread tuple (`expr` is the whole spread), or the TemplateStringsArray of a
    // tagged template (`expr` is the template)
    bool part_of_spread;
    bool spread; // `...xs` of something that isn't a fixed tuple: stands for any number of arguments of this type
    Ty const* variadic; // `...args` of a generic array type (`T extends unknown[]`): the type spread, kept as `[...T]`
    bool context_sensitive;
    // A generic function where the parameter wants a non-generic one: inferred from once instantiated in the context
    // of that signature, in the second pass (tsc's instantiateTypeWithSingleGenericCallSignature). Set per candidate
    bool generic_function;
    // A call of a generic function that returns a function (`pipe(box)`): checked in the second pass, when the outer
    // call knows more (tsc's isGenericFunctionReturningFunction under SkipGenericFunctions)
    bool generic_call;
    // An object or array literal with a generic function in it that the first pass skipped (GenericFunctionMode)
    bool nested_generic;
} Arg;

typedef struct Args {
    Arg* data;
    u32 len;
    i32 spread_index; // first `spread` argument, -1 if none
    bool incomplete; // An incomplete tagged template: too few arguments are fine (tsc's callIsIncomplete)
    uint8_t decorator_arity; // DecoratorArity
} Args;

// How many arguments the runtime passes a decorator (tsc's getDecoratorArgumentCount)
typedef enum DecoratorArity : uint8_t {
    DECORATOR_ARITY_NONE, // Not a decorator
    DECORATOR_ARITY_FIXED, // All its synthetic arguments
    DECORATOR_ARITY_ES, // ES decorators: 1 or 2, as many as the signature takes
    DECORATOR_ARITY_LEGACY_METHOD, // Legacy method decorators: 2, or 3 (the descriptor) if the signature takes that many
} DecoratorArity;

typedef struct DecoratorCall {
    Ty const* params[3];
    u32 count;
    Ty const* ret; // What it may return, null: not checked
    unsigned head_code; // 1238-1241
    DecoratorArity arity;
    bool void_only; // TS1271 rather than TS1270
} DecoratorCall;

// The parameters of a signature as a call sees them: a rest parameter of tuple type is spread out
typedef struct ParamShape {
    Ty const** types;
    u32 count;
    u32 min;
    Ty const* rest_element; // `...rest: X[]`: X
    Ty const* rest_variadic; // a rest parameter that's neither array nor tuple (a type parameter, mostly)
} ParamShape;

typedef struct Candidate {
    Signature const* sig;
    TyMapper const* mapper; // inferred type arguments, null if the signature isn't generic
    TyList inferred_type_params; // what the returned function is generic in, see infer_type_arguments()
} Candidate;

typedef struct Resolution {
    Candidate chosen; // sig is null if nothing fit
    Ty const* ret;
} Resolution;

// What a call brings besides its arguments
typedef struct CallInfo {
    CallExpr const* call; // Null for a JSX element
    // A tagged template (tsc's TaggedTemplateExpression): `call` is a stand-in with the tag as callee and the
    // substitutions as arguments, which come after the TemplateStringsArray (see tagged_template_call())
    Expr const* tagged_template;
    // A JSX element whose tag is resolved like a call (tsc's JsxCallLike): its one argument is its attributes, checked
    // against what each signature takes as props (ty_jsx_signature_props())
    JsxElement const* jsx;
    bool jsx_class; // The tag has construct signatures (tsc's JsxReferenceKind.Component)
    Ty const* contextual; // contextual type of the call expression, may be null
    VecSpannedType const* type_arg_nodes; // explicit type arguments, null if none
    TyList type_args;
    Candidate* chosen_out; // Where to leave the signature the call resolved to, may be null
    Ty const* this_arg; // What `this` is in the call (the receiver of a method call, else void), see this_argument()
    Span this_span; // Where a `this` that doesn't fit is reported
    Span call_span; // The whole call expression
    // Set when the resolution would report something given an ErrorCtx (see ty_call_typecheck()), may be null
    bool* would_report;
    // `new f()` on something with call signatures only, resolved as a call (tsc's resolveNewExpression)
    bool new_as_call;
    DecoratorCall const* decorator; // A decorator (tsc's resolveDecorator): its synthetic arguments
} CallInfo;

static void mark_would_report(CallInfo const* info) {
    if (info->would_report) *info->would_report = true;
}

static void args_push(Args* args, Arg arg) {
    args->data = realloc(args->data, sizeof(Arg) * (args->len + 1));
    panic_if(!args->data, "out of memory");
    args->data[args->len++] = arg;
}

static bool is_empty_tuple(Ty const* ty) {
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_TUPLE_TARGET;
}

// tsc's isGenericFunctionReturningFunction for some call signature of the callee of `expr`, a call without type arguments
bool ty_calls_generic_function_returning_function(Checker* me, Expr const* expr) {
    if (expr->tag != EXPR_CALL || expr->call->uses_new || expr->call->is_extends) return false;
    if (expr->call->callable.tag == EXPR_GENERIC_PARAMS) return false;
    Ty const* callee = ty_apparent_type(me, ty_expr_type(me, &expr->call->callable));
    if (!(callee->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false; // (getSignaturesOfType)
    Members const* members = ty_members(me, callee);
    for (u32 i = 0; i < members->call_count; i++) {
        signature_resolve(me, members->call[i]);
        if (!members->call[i]->type_params.len) continue;
        Ty const* ret = signature_return_type(me, members->call[i]);
        if ((ret->flags & TYF_OBJECT) && ty_members(me, ret)->call_count) return true;
    }
    return false;
}

static Args collect_args(Checker* me, CallInfo const* info) {
    CallExpr const* call = info->call;
    Args args = { .spread_index = -1 };
    if (info->decorator) {
        for (u32 i = 0; i < info->decorator->count; i++) {
            args_push(&args, (Arg){ .expr = &call->callable, .type = info->decorator->params[i], .part_of_spread = true });
        }
        args.decorator_arity = info->decorator->arity;
        return args;
    }
    if (info->tagged_template) {
        Symbol const* strings = program_get_global(my program, c_sv("TemplateStringsArray"), SYMBOL_TYPE);
        Ty const* type = strings ? ty_declared_type_of(me, strings) : ty_empty_object_type(me);
        args_push(&args, (Arg){ .expr = info->tagged_template, .type = type, .part_of_spread = true });
        args.incomplete = info->tagged_template->template_->incomplete;
    }
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
            // A generic array-like stays itself, a variadic element (tsc's getSpreadArgumentType, isArrayLikeType: its
            // constraint is assignable to `readonly any[]`, `AsArray<T> = T extends any[] ? T : [T]` included)
            Ty const* variadic = nullptr;
            if (operand->kind == TY_TYPE_PARAM || (operand->flags & TYF_DEFERRED)) {
                Ty const* constraint = ty_base_constraint(me, operand);
                if (constraint && (ty_is_array(me, constraint) || ty_is_tuple(constraint) ||
                    (!(constraint->flags & TYF_NULLABLE) &&
                        ty_is_related(me, constraint, ty_array(me, ty_intrinsic(me, TY_ANY), true), TY_RELATION_ASSIGNABLE)))
                ) {
                    variadic = operand;
                }
            } else if (operand->kind == TY_ANONYMOUS && (operand->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
                ty_is_related(me, operand, ty_array(me, ty_intrinsic(me, TY_ANY), true), TY_RELATION_ASSIGNABLE)
            ) {
                // (A homomorphic mapped type over an array type parameter too: `Foo<T>` with `T extends any[]`)
                variadic = operand;
            }
            if (args.spread_index < 0) args.spread_index = (i32)args.len;
            args_push(&args, (Arg){ .expr = i, .type = element, .spread = true, .variadic = variadic });
        } else {
            bool context_sensitive = ty_expr_is_context_sensitive(i);
            args_push(&args, (Arg){
                .expr = i,
                .context_sensitive = context_sensitive,
                .generic_call = !context_sensitive && ty_calls_generic_function_returning_function(me, i),
            });
        }
    }
    return args;
}

static bool accepts_void(Ty const* ty) {
    if (ty->flags & TYF_VOID) return true;
    if (ty->kind != TY_UNION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (ty->composite.types.data[i]->flags & TYF_VOID) return true;
    }
    return false;
}

static bool accepts_void_undefined_unknown_any(Ty const* ty) {
    u32 const flags = TYF_VOID | TYF_UNDEFINED | TYF_UNKNOWN | TYF_ANY;
    if (ty->flags & flags) return true;
    if (ty->kind != TY_UNION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (ty->composite.types.data[i]->flags & flags) return true;
    }
    return false;
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
    // Trailing parameters that accept void can be left out (tsc's getMinArgumentCount): `resolve()` of a Promise<void>
    while (shape.min > 0 && shape.min <= shape.count && accepts_void(shape.types[shape.min - 1])) shape.min--;
    return shape;
}

static ParamShape call_param_shape(Checker* me, CallInfo const* info, Signature const* sig, TyMapper const* mapper);

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

// What a round without the context-sensitive arguments leaves out: them, and the generic calls that should see what
// they infer first
static bool waits_for_sensitive(Arg const* arg) {
    return arg->context_sensitive || arg->generic_call;
}

// `skip_sensitive`: leave out the context-sensitive arguments, which a contextual type would type for good (tsc's
// CheckMode.SkipContextSensitive, while it's not known yet which overload they'll be checked against)
static void check_args(Checker* me, ParamShape const* shape, Args* args, bool skip_sensitive) {
    for (u32 i = 0; i < args->len; i++) {
        if (!skip_sensitive || !waits_for_sensitive(&args->data[i])) check_arg(me, shape, args, i);
    }
}

// tsc's getDecoratorArgumentCount: for a signature of `params` parameters (a rest parameter counts once)
static u32 decorator_arg_count(DecoratorArity arity, u32 synthetic, u32 params) {
    switch (arity) {
        case DECORATOR_ARITY_ES: return params < 1 ? 1 : params > 2 ? 2 : params;
        case DECORATOR_ARITY_LEGACY_METHOD: return params <= 2 ? 2 : 3;
        default: return synthetic;
    }
}

static bool has_correct_arity(ParamShape const* shape, Args const* args) {
    if (args->decorator_arity) {
        u32 len = decorator_arg_count(args->decorator_arity, args->len, shape->count + (shape_has_rest(shape) ? 1 : 0));
        return len >= shape->min && (shape_has_rest(shape) || len <= shape->count);
    }
    if (args->spread_index >= 0) {
        return (u32)args->spread_index >= shape->min && (shape_has_rest(shape) || (u32)args->spread_index < shape->count);
    }
    return (args->incomplete || args->len >= shape->min) && (shape_has_rest(shape) || args->len <= shape->count);
}

// The arguments from `from` on, as the tuple a variadic rest parameter receives
static Ty const* rest_args_tuple(Checker* me, Args const* args, u32 from, bool widen, bool readonly) {
    u32 arity = args->len > from ? args->len - from : 0;
    unsigned char* flags = calloc(arity ? arity : 1, 1);
    Ty const** elements = calloc(arity ? arity : 1, sizeof(Ty const*));
    for (u32 i = 0; i < arity; i++) {
        Arg const* arg = &args->data[from + i];
        if (arg->spread) {
            flags[i] = TUPLE_ELEMENT_REST;
            elements[i] = arg->variadic ? arg->variadic : ty_array(me, arg->type, false);
        } else {
            Ty const* type = arg->type ? arg->type : ty_expr_type(me, arg->expr);
            elements[i] = widen ? ty_widened_literal(me, type) : type;
        }
    }
    TupleShape tuple = { .arity = arity, .flags = flags, .readonly = readonly };
    Ty const* target = ty_tuple_target(me, &tuple);
    Ty const* ret = arity ? ty_reference(me, target, (TyList){ .data = elements, .len = arity }) : target;
    free(flags);
    free(elements);
    return ret;
}

// Type argument inference

static Signature const* last_call_signature(Checker* me, Ty const* ty) {
    Members const* members = ty_members(me, ty);
    return members->call_count ? members->call[members->call_count - 1] : nullptr;
}

static bool has_candidates_or_fixed(InferParam const* param) {
    return param->co_len || param->contra_len || param->fixed;
}

// What checking a context-sensitive argument fixed (KEY_ARG_FIXES), replayed when the call is resolved again
typedef struct ArgFixes {
    u64 mask;
    Ty const** types; // (what each type parameter was fixed to, by the index of mask's bits)
} ArgFixes;

// A generic call inferring from its arguments, as the calls nested in them see it (tsc's getInferenceContext)
typedef struct OuterInference {
    Signature const* sig;
    Inference* inf;
    TyMapper const* return_mapper; // What its own contextual type said, see infer_type_arguments()
    bool fixing; // Contextual signatures assigned now fix the type parameters their parameters mention
    // The context-sensitive literal members checked since the last fixing, and their contextual types (tsc's
    // intraExpressionInferenceSites), pairs
    Ty const** sites;
    u32 site_len, site_cap;
    // (Cache of the non-fixing mapper, valid while the candidate count is `cached_count`)
    TyMapper const* cached_mapper;
    u64 cached_count;
} OuterInference;

static Ty const* instantiate_instantiable(Checker* me, Ty const* type, TyMapper const* mapper);

// A contextual type instantiated with a call's returnMapper: without `boolean` in it (tsc's instantiateContextualType,
// so that `box(false)` against `Box<boolean>` is `Box<boolean>`, not `Box<false>`)
static Ty const* instantiate_with_return_mapper(Checker* me, Ty const* contextual, TyMapper const* return_mapper) {
    Ty const* type = instantiate_instantiable(me, contextual, return_mapper);
    if (type->kind != TY_UNION || !(type->flags & TYF_BOOLEAN)) return type;
    TyBuf kept = {0};
    for (u32 i = 0; i < type->composite.types.len; i++) {
        Ty const* part = type->composite.types.data[i];
        if (part != ty_intrinsic(me, TY_TRUE) && part != ty_intrinsic(me, TY_FALSE)) ty_buf_push(&kept, part);
    }
    Ty const* ret = ty_union_filtered(me, type, kept.data, kept.len);
    ty_buf_drop(&kept);
    return ret;
}

// Changes whenever the non-fixing mapper would: candidates are only added, and fixing is for good
static u64 inference_count(Inference const* inf) {
    u64 candidates = 1, fixed = 0;
    for (u32 j = 0; j < inf->len; j++) {
        candidates += inf->params[j].co_len + inf->params[j].contra_len;
        fixed += inf->params[j].fixed != nullptr;
    }
    return candidates | fixed << 32;
}

static TyMapper const* non_fixing_mapper(Checker* me, OuterInference* outer) {
    u64 count = inference_count(outer->inf);
    if (outer->cached_mapper && outer->cached_count == count) return outer->cached_mapper;
    outer->cached_mapper = inference_mapper(me, outer->sig, outer->inf, true);
    outer->cached_count = count;
    return outer->cached_mapper;
}

// tsc's instantiateContextualType for the call inferring now: the non-fixing mapper if anything has candidates or a
// default, else the return mapper, only at the top level (instantiateInstantiableTypes)
Ty const* ty_instantiate_contextual_type_for(Checker* me, Ty const* contextual, bool signature) {
    OuterInference* outer = my outer_inference;
    if (!contextual || !outer || !(contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return contextual;
    if (!signature) return outer->return_mapper ? instantiate_with_return_mapper(me, contextual, outer->return_mapper) : contextual;
    bool any = false;
    for (u32 j = 0; j < outer->inf->len && !any; j++) {
        any = has_candidates_or_fixed(&outer->inf->params[j]) || ty_param_default(me, outer->sig->type_params.data[j]);
    }
    if (any) return instantiate_instantiable(me, contextual, non_fixing_mapper(me, outer));
    if (outer->return_mapper) return instantiate_with_return_mapper(me, contextual, outer->return_mapper);
    return contextual;
}

Ty const* ty_instantiate_contextual_type(Checker* me, Ty const* contextual) {
    return ty_instantiate_contextual_type_for(me, contextual, false);
}

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
// `fallback`, if set: for the type parameters `inf` has nothing on
static TyMapper const* return_inference_mapper(Checker* me, Signature const* sig, Inference* inf, Inference* fallback) {
    TyList params = sig->type_params;
    Ty const** results = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    bool any = false;
    for (u32 i = 0; i < params.len; i++) {
        results[i] = inf->params[i].co_len || inf->params[i].contra_len ? inference_call_result(me, inf, i, true) :
            fallback && (fallback->params[i].co_len || fallback->params[i].contra_len) ? inference_call_result(me, fallback, i, true) :
            nullptr;
        any |= results[i] != nullptr;
        if (!results[i]) results[i] = params.data[i];
    }
    if (!any) return nullptr;
    TyMapper const* mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    // An inference that doesn't satisfy the constraint is the constraint (tsc's getInferredType on the cloned context):
    // an outer `Options extends readonly S[]` inferred for `Types extends readonly [S, ...S[]]` makes the tuple the
    // contextual type of the argument, which is then typed as a tuple
    bool changed = false;
    for (u32 i = 0; i < params.len; i++) {
        if (results[i] == params.data[i]) continue;
        Ty const* constraint = ty_param_constraint(me, params.data[i]);
        if (!constraint || constraint == params.data[i]) continue;
        Ty const* instantiated = ty_instantiate(me, constraint, mapper);
        if (!ty_is_related(me, results[i], instantiated, TY_RELATION_ASSIGNABLE)) {
            results[i] = instantiated;
            changed = true;
        }
    }
    return changed ? ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len }) : mapper;
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
    // (A union without reduction: tsc's UnionReduction.None, see ty_union_unreduced())
    Ty const* ret = !changed ? type : type->kind == TY_UNION ? ty_union_unreduced(me, mapped, parts.len) : ty_intersection(me, mapped, parts.len, nullptr, (TyList){0});
    free(mapped);
    return ret;
}

static void add_array_literal(Inference* inf, Ty const* type) {
    inf->array_literals = realloc(inf->array_literals, sizeof(Ty const*) * (inf->array_literal_count + 1));
    panic_if(!inf->array_literals, "out of memory");
    inf->array_literals[inf->array_literal_count++] = type;
}

// The tuples of the array literals nested in an argument's object and array literals (tsc flags array literal types
// ObjectFlags.ArrayLiteral; tuples are interned here, so the inference remembers them instead). By the argument's type,
// which a const context made readonly where the nested expressions' own types may not be
static void add_nested_array_literals(Checker* me, Inference* inf, Expr const* expr, Ty const* type, u32 depth) {
    if (depth > 4 || !type) return;
    if (expr->tag == EXPR_ARRAY && !ty_is_tuple(type) && ty_is_array(me, type)) {
        // An array: its element type is (the union of) the elements' types, each nested literal is one of its members
        Ty const* element = ty_reference_args(me, type).data[0];
        TyList members = element->kind == TY_UNION ? element->composite.types : (TyList){ .data = &element, .len = 1 };
        // (Which member is whose isn't known: each member is looked into with the first nested literal of its kind)
        Expr const* first_array = nullptr;
        Expr const* first_object = nullptr;
        for_each (i, vec_expr, &expr->array) {
            if (i->tag == EXPR_ARRAY && !first_array) first_array = i;
            if (i->tag == EXPR_OBJECT && !first_object) first_object = i;
        }
        for (u32 j = 0; j < members.len && (first_array || first_object); j++) {
            Ty const* member = members.data[j];
            bool array_like = ty_is_tuple(member) || ty_is_array(me, member);
            if (array_like && first_array) add_array_literal(inf, member);
            Expr const* nested = array_like ? first_array : first_object;
            if (nested) add_nested_array_literals(me, inf, nested, member, depth + 1);
        }
        return;
    }
    if (expr->tag == EXPR_ARRAY) {
        if (!ty_is_tuple(type)) return;
        TyList elements = ty_reference_args(me, type);
        u32 n = 0;
        for_each (i, vec_expr, &expr->array) {
            if (i->tag == EXPR_SPREAD || n >= elements.len) return;
            Ty const* element = elements.data[n++];
            if (i->tag == EXPR_ARRAY && (ty_is_tuple(element) || ty_is_array(me, element))) add_array_literal(inf, element);
            if (i->tag == EXPR_ARRAY || i->tag == EXPR_OBJECT) add_nested_array_literals(me, inf, i, element, depth + 1);
        }
    } else if (expr->tag == EXPR_OBJECT) {
        for_each (i, vec_object_expr_item, &expr->object) {
            if (i->tag != OBJECT_ITEM_PROPERTY || (i->value.tag != EXPR_ARRAY && i->value.tag != EXPR_OBJECT) || i->computed_key) continue;
            FoundProp prop = ty_find_prop(me, type, checker_atom(me, cstr_sv(&i->name_string)));
            if (!prop.found) continue;
            if (i->value.tag == EXPR_ARRAY && (ty_is_tuple(prop.type) || ty_is_array(me, prop.type))) add_array_literal(inf, prop.type);
            add_nested_array_literals(me, inf, &i->value, prop.type, depth + 1);
        }
    }
}

static void infer_from_arg(Checker* me, Inference* inf, ParamShape const* shape, Args const* args, u32 i) {
    Ty const* param_type = shape_type_at(shape, i);
    Arg const* arg = &args->data[i];
    if (arg->type && !arg->spread && !arg->part_of_spread && arg->expr->tag == EXPR_ARRAY && ty_is_array(me, arg->type)) {
        add_array_literal(inf, arg->type);
    }
    if (arg->type && !arg->spread && !arg->part_of_spread) add_nested_array_literals(me, inf, arg->expr, arg->type, 0);
    if (param_type && args->data[i].type) infer_types(me, inf, args->data[i].type, param_type, INFER_PRIORITY_NORMAL);
}

// Whether instantiating `type` meets a JSDoc object or function type that `host`'s `@template`s are in scope at (see
// Ty.anonymous.jsdoc_references_all): from the first `@template` tag to the end of the host
static bool meets_jsdoc_node_in(Checker* me, Ty const* type, FunctionImpl const* host, u32 depth) {
    // (Not gated by TYF_COULD_CONTAIN_TYPE_VARIABLES: `() => void` mentions nothing, it is only possibly referencing)
    if (depth > 4) return false;
    if (type->kind == TY_UNION || type->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < type->composite.types.len; i++) {
            if (meets_jsdoc_node_in(me, type->composite.types.data[i], host, depth + 1)) return true;
        }
        return false;
    }
    if (type->kind == TY_REFERENCE) {
        TyList args = ty_reference_args(me, type);
        for (u32 i = 0; i < args.len; i++) {
            if (meets_jsdoc_node_in(me, args.data[i], host, depth + 1)) return true;
        }
        return false;
    }
    if (type->kind != TY_ANONYMOUS) return false;
    Ty const* target = type->anonymous.target ? type->anonymous.target : type;
    TypeNode const* node = target->kind == TY_ANONYMOUS ? target->anonymous.node : nullptr;
    if (!node || !target->anonymous.jsdoc_references_all) return false;
    Span span = node->tag == TYPE_CALLABLE ? node->callable->function->args_span : node->interface.span;
    Span first = vec_generic_item_at(&host->generic.items, 0)->full_span;
    return span.source == host->full_span.source && span.sv.buf >= first.sv.buf &&
        span.sv.buf + span.sv.size <= host->full_span.sv.buf + host->full_span.sv.size;
}

// Whether assigning contextual signature `sig` to `function` asks for a type that mentions type parameter `param`: the
// types of its parameters without annotation and its `this` (tsc's assignContextualParameterTypes). In JS, one that
// meets a JSDoc type node in the scope of the call's `@template`s mentions them all (tsc's fixing mapper fixes every
// type parameter isTypeParameterPossiblyReferenced says the node may reference: `R` for `s: () => void` in
// `@param {(i: number, s: () => void) => R} setup`)
static bool assignment_mentions(Checker* me, FunctionImpl const* function, Signature const* sig, Ty const* param) {
    FunctionImpl const* host = my outer_inference->sig->function;
    if (host && (!host->full_span.source || !host->full_span.source->is_js || !vec_generic_item_size(&host->generic.items))) {
        host = nullptr;
    }
    signature_resolve(me, sig);
    u32 index = 0;
    bool this_declared = false;
    for_each (i, vec_function_arg, &function->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (is_this) {
            this_declared = i->initializer.binding.has_explicit_type;
            continue;
        }
        u32 at = index++;
        if (i->initializer.binding.has_explicit_type) continue;
        Ty const* type = ty_contextual_signature_type_at(me, sig, at, i->rest);
        if (type && (ty_mentions_any(me, type, &param, 1) || (host && meets_jsdoc_node_in(me, type, host, 0)))) return true;
    }
    return !this_declared && sig->this_type && ty_mentions_any(me, sig->this_type, &param, 1);
}

void ty_add_intra_expression_site(Checker* me, Ty const* type, Ty const* contextual) {
    OuterInference* outer = my outer_inference;
    if (!outer || !outer->fixing || !type || !contextual) return;
    if (outer->site_len + 2 > outer->site_cap) {
        outer->site_cap = outer->site_cap ? outer->site_cap * 2 : 8;
        outer->sites = realloc(outer->sites, sizeof(Ty const*) * outer->site_cap);
        panic_if(!outer->sites, "out of memory");
    }
    outer->sites[outer->site_len++] = type;
    outer->sites[outer->site_len++] = contextual;
}

// Before a type parameter is fixed, the literal members checked so far are inferred from (tsc's fixing mapper calling
// inferFromIntraExpressionSites): `{ produce: () => 0, consume: (n) => n }` against `{ produce: () => T; consume:
// (n: T) => void }` infers T from produce before consume's `n` fixes it
static void infer_from_intra_expression_sites(Checker* me, OuterInference* outer) {
    u32 len = outer->site_len;
    outer->site_len = 0;
    for (u32 i = 0; i + 1 < len; i += 2) infer_types(me, outer->inf, outer->sites[i], outer->sites[i + 1], INFER_PRIORITY_NORMAL);
}

// What the type parameters about to be fixed are fixed to: the inferences so far, the sites included
static TyMapper const* fixing_mapper(Checker* me, OuterInference* outer) {
    if (outer->site_len) infer_from_intra_expression_sites(me, outer);
    return inference_mapper(me, outer->sig, outer->inf, true);
}

Ty const* ty_instantiate_fixing(Checker* me, Ty const* type) {
    OuterInference* outer = my outer_inference;
    if (!type || !outer || !outer->fixing || !(type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return type;
    Inference* inf = outer->inf;
    TyMapper const* so_far = nullptr;
    for (u32 j = 0; j < inf->len; j++) {
        if (!inf->params[j].fixed && ty_mentions_any(me, type, &outer->sig->type_params.data[j], 1)) {
            if (!so_far) so_far = fixing_mapper(me, outer);
            inf->params[j].fixed = so_far->targets[j];
        }
    }
    return ty_instantiate(me, type, inference_mapper(me, outer->sig, inf, true));
}

// A function gets its contextual signature while an argument of the call inferring now is checked: the type
// parameters that the types it asks for mention are fixed (tsc's fixing mapper), and it gets the signature instantiated
// with what they are fixed to
Signature const* ty_fix_contextual_signature(Checker* me, Signature const* sig, FunctionImpl const* function) {
    OuterInference* outer = my outer_inference;
    if (!outer || !outer->fixing || !ty_function_is_context_sensitive(function)) return sig;
    signature_resolve(me, sig);
    Inference* inf = outer->inf;
    // (A rest parameter of type parameter type fixes nothing: the signature is instantiated with the non-fixing mapper,
    // tsc's contextuallyCheckFunctionExpressionOrObjectLiteralMethod)
    if (sig->param_count && sig->params[sig->param_count - 1].rest && sig->params[sig->param_count - 1].type->kind == TY_TYPE_PARAM) {
        Signature* instantiated = arena_new(&my arena, Signature);
        *instantiated = (Signature){ .flags = sig->flags, .target = sig, .mapper = inference_mapper(me, outer->sig, inf, true) };
        signature_resolve(me, instantiated);
        return instantiated;
    }
    TyMapper const* so_far = nullptr;
    for (u32 j = 0; j < inf->len; j++) {
        if (!inf->params[j].fixed && assignment_mentions(me, function, sig, outer->sig->type_params.data[j])) {
            if (!so_far) so_far = fixing_mapper(me, outer);
            inf->params[j].fixed = so_far->targets[j];
        }
    }
    TyMapper const* mapper = inference_mapper(me, outer->sig, inf, true);
    Signature* fixed = arena_new(&my arena, Signature);
    *fixed = (Signature){ .flags = sig->flags, .target = sig, .mapper = mapper };
    signature_resolve(me, fixed);
    return fixed;
}

Signature const* ty_single_signature(Checker* me, Ty const* ty, bool construct, bool allow_members) {
    if (!(ty->flags & TYF_OBJECT)) return nullptr;
    Members const* members = ty_members(me, ty);
    if (!allow_members && (members->prop_count || members->index_count)) return nullptr;
    if (construct) return members->construct_count == 1 && !members->call_count ? members->construct[0] : nullptr;
    return members->call_count == 1 && !members->construct_count ? members->call[0] : nullptr;
}

// The generic signature of a generic function argument whose parameter wants a single non-generic signature of the
// same kind (the one in `*contextual`), else null
static Signature const* generic_function_argument(Checker* me, Ty const* arg_type, Ty const* param_type, Signature const** contextual) {
    if (!arg_type || !param_type) return nullptr;
    for (u32 construct = 0; construct < 2; construct++) {
        Signature const* sig = ty_single_signature(me, arg_type, construct, true);
        if (!sig) continue;
        signature_resolve(me, sig);
        if (!sig->type_params.len) return nullptr;
        Signature const* wanted = ty_single_signature(me, ty_non_nullable(me, param_type), construct, false);
        if (!wanted) return nullptr;
        signature_resolve(me, wanted);
        if (wanted->type_params.len) return nullptr;
        *contextual = wanted;
        return sig;
    }
    return nullptr;
}

// Whether the contextual signature of a generic function argument leaves the type parameters its parameters mention
// unfixed: a rest parameter of type parameter type (tsc's nonFixingMapper)
static bool contextual_rest_is_variadic(Signature const* contextual) {
    return contextual->param_count && contextual->params[contextual->param_count - 1].rest &&
        contextual->params[contextual->param_count - 1].type->kind == TY_TYPE_PARAM;
}

// A generic function argument instantiated in the context of signature `contextual` fixes the call's type parameters
// that `contextual`'s parameters (and `this`) mention: tsc's instantiateSignatureInContextOf instantiates it with the
// fixing mapper, and applyToParameterTypes asks for every parameter type. (A rest parameter of type parameter type
// isn't fixed: tsc's nonFixingMapper.) Returns the mapper of what is inferred so far
static TyMapper const* fix_for_generic_function(Checker* me, Signature const* sig, Inference* inf, Signature const* contextual) {
    signature_resolve(me, contextual);
    // (The literal's members before this one are inferred from first, see fixing_mapper())
    if (my outer_inference && my outer_inference->inf == inf) fixing_mapper(me, my outer_inference);
    if (!contextual_rest_is_variadic(contextual)) {
        TyMapper const* so_far = inference_mapper(me, sig, inf, true);
        for (u32 j = 0; j < inf->len; j++) {
            if (inf->params[j].fixed) continue;
            Ty const* param = sig->type_params.data[j];
            bool mentioned = contextual->this_type && ty_mentions_any(me, contextual->this_type, &param, 1);
            for (u32 i = 0; i < contextual->param_count && !mentioned; i++) {
                mentioned = ty_mentions_any(me, contextual->params[i].type, &param, 1);
            }
            if (mentioned) inf->params[j].fixed = so_far->targets[j];
        }
    }
    return inference_mapper(me, sig, inf, true);
}

Ty const* ty_generic_function_in_context(Checker* me, Ty const* type, Ty const* contextual, GenericFunctionMode* mode) {
    Signature const* wanted = nullptr;
    Signature const* generic = generic_function_argument(me, type, contextual, &wanted);
    if (!generic) return type;
    if (!mode->sig) {
        mode->skipped = true;
        return ty_any_function_type(me);
    }
    // HACK: not tsc's higher-order branch (a call returning a function would get the member's type parameters)
    TyMapper const* so_far = fix_for_generic_function(me, mode->sig, mode->inf, wanted);
    return ty_signature_type(me, ty_instantiate_signature_in_context_of(me, generic, wanted, so_far));
}

// `contextual`: the contextual type of the call, which the return type is inferred from at a low priority
// tsc's getSingleCallOrConstructSignature, without other members
static Signature const* single_call_or_construct(Checker* me, Ty const* ty) {
    Signature const* ret = ty_single_signature(me, ty, false, false);
    return ret ? ret : ty_single_signature(me, ty, true, false);
}

// Whether `sig` returns a single non-generic call or construct signature: what a generic function argument's type
// parameters can be propagated to
static bool returns_single_signature(Checker* me, Signature const* sig) {
    Signature const* ret = single_call_or_construct(me, signature_return_type(me, sig));
    if (!ret) return false;
    signature_resolve(me, ret);
    return !ret->type_params.len;
}

// A context-sensitive function argument's annotated parameters say something about the type parameters before the
// others are typed by what is inferred (tsc's inferFromAnnotatedParameters): `reduce(xs, (a: T | undefined, b) => ...)`
static void infer_from_annotated_params(Checker* me, Inference* inf, FunctionImpl const* function, Ty const* param_type) {
    Signature const* contextual = ty_function_contextual_signature(me, function, param_type);
    if (!contextual) return;
    signature_resolve(me, contextual);
    for_each (i, vec_function_arg, &function->args) {
        if (i->rest) break;
        Binding const* binding = &i->initializer.binding;
        if (!binding->has_explicit_type) continue;
        Ty const* target = ty_signature_param_type_at(me, contextual, (u32)i_index);
        if (!target) break;
        Ty const* source = ty_from_node(me, &binding->explicit_type, (LowerCtx){ .generic = true });
        if (i->explicit_optional) source = ty_optional_param_type(me, source);
        infer_types(me, inf, source, target, INFER_PRIORITY_NORMAL);
    }
}

static bool is_literal_arg(Arg const* arg) {
    return !arg->spread && !arg->part_of_spread && (arg->expr->tag == EXPR_OBJECT || arg->expr->tag == EXPR_ARRAY);
}

// The contextual type of a literal argument in the first pass: what the return type says about the type parameters
// helps type it (a tuple, a literal)
static Ty const* literal_arg_context(Checker* me, Ty const* param_type, TyMapper const* return_mapper) {
    return param_type && return_mapper ? instantiate_instantiable(me, param_type, return_mapper) : param_type;
}

// `*inferred_out`: the type parameters of generic function arguments that the return type is now generic in
static TyMapper const* infer_type_arguments(
    Checker* me, Signature const* sig, Args* args, CallInfo const* info, bool skip_sensitive, bool skip_generic,
    TyList* inferred_out
) {
    Ty const* contextual = info->contextual;
    Ty const* this_arg = info->this_arg;
    Inference inf;
    inference_init(&inf, sig->type_params);
    SourceFile const* file = info->call ? info->call->callable.inner_span.source : info->call_span.source;
    inf.any_default = file && file->is_js;
    ParamShape shape = call_param_shape(me, info, sig, nullptr);

    // Nested in an argument of another generic call, the contextual type mentions its type parameters: what it has
    // inferred so far stands in for them
    OuterInference* outer = my outer_inference;
    my outer_inference = nullptr;
    Ty const* uninferred_contextual = contextual;
    if (contextual && outer && (contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        contextual = ty_instantiate(me, contextual, outer_inference_mapper(me, outer));
    }
    OuterInference self = { .sig = sig, .inf = &inf };
    my inferential_depth++;

    // A contextual type that is what a binding pattern implies (tsc's isFromBindingPattern) isn't one at all when every
    // type parameter has a default, else it only goes into the return mapper: `const [a, b] = f()` infers nothing
    bool from_pattern = contextual && (contextual->flags & TYF_BINDING_PATTERN);
    if (from_pattern) {
        bool all_defaults = true;
        for (u32 i = 0; i < sig->type_params.len && all_defaults; i++) all_defaults = ty_param_default(me, sig->type_params.data[i]) != nullptr;
        if (all_defaults) contextual = nullptr;
    }
    TyMapper const* return_mapper = nullptr;
    if (contextual && !(contextual->flags & TYF_ANY) && from_pattern) {
        Ty const* return_type = signature_return_type(me, sig);
        if (return_type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
            Inference pattern_inf;
            inference_init(&pattern_inf, sig->type_params);
            infer_types(me, &pattern_inf, contextual, return_type, INFER_PRIORITY_NORMAL);
            return_mapper = return_inference_mapper(me, sig, &pattern_inf, nullptr);
            inference_drop(&pattern_inf);
            // HACK: the `any` of an element without default is tsc's nonInferrableAnyType, which infers nothing. And a
            // pattern with a rest element isn't a contextual type at all (see narrowable_type(): tsc types such an
            // initializer again for the rest, CheckMode.RestBindingElement)
            TupleShape const* shape = ty_is_tuple(contextual) ? contextual->reference.target->declared.tuple : nullptr;
            bool rest = shape ? shape->arity && (shape->flags[shape->arity - 1] & TUPLE_ELEMENT_REST) :
                (contextual->flags & TYF_OBJECT) && ty_members(me, contextual)->index_count;
            if (rest) return_mapper = nullptr;
            if (return_mapper) {
                Ty const** targets = arena_new_n(&my arena, Ty const*, return_mapper->len);
                bool any_left = false;
                for (u32 i = 0; i < return_mapper->len; i++) {
                    Ty const* target = return_mapper->targets[i];
                    targets[i] = target == ty_intrinsic(me, TY_ANY) ? return_mapper->sources[i] : target;
                    // (An inference is widened, which makes a new object type that is no pattern's: an object literal
                    // argument it contextually types isn't one of a pattern, tsc's contextualTypeHasPattern)
                    if (targets[i]->flags & TYF_BINDING_PATTERN) targets[i] = ty_pattern_unclone(me, targets[i]);
                    any_left |= targets[i] != return_mapper->sources[i];
                }
                return_mapper = !any_left ? nullptr : ty_mapper_new(me, (TyList){ .data = return_mapper->sources, .len = return_mapper->len },
                    (TyList){ .data = targets, .len = return_mapper->len });
            }
        }
    } else if (contextual && !(contextual->flags & TYF_ANY)) {
        Ty const* return_type = signature_return_type(me, sig);
        if (return_type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
            // A generic function type is instantiated with its own type parameters, which can then be inferred as
            // they are instead of being erased: `const f: <A>(a: A[]) => A[] = arrayMap(x => x)` gives `x: A`
            Signature const* contextual_sig = ty_single_signature(me, contextual, false, false);
            Ty const* source = contextual;
            if (contextual_sig) {
                signature_resolve(me, contextual_sig);
                if (contextual_sig->type_params.len) {
                    source = ty_signature_type(me, ty_signature_instantiation(me, contextual_sig, contextual_sig->type_params));
                }
            }
            infer_types(me, &inf, source, return_type, INFER_PRIORITY_RETURN_TYPE);
            // The mapper that instantiates contextual types of the arguments. tsc's InferenceContext.returnMapper comes
            // from an inference of its own, from the contextual type as the outer call's contextual type (not its
            // inferences) says: in `new Map(xs.map(x => [x, 1]))` the outer K and V aren't known, but `[K, V]` still
            // makes the callback return a tuple. HACK: that inference only fills in what the outer inferences left
            // unknown; using it for everything costs valibot 9% (the arguments' contextual types turn generic)
            Ty const* return_source = outer && outer->return_mapper ?
                ty_instantiate(me, uninferred_contextual, outer->return_mapper) : uninferred_contextual;
            bool gaps = false;
            for (u32 i = 0; i < inf.len && !gaps; i++) gaps = !inf.params[i].co_len && !inf.params[i].contra_len;
            if (return_source == source || !gaps) {
                return_mapper = return_inference_mapper(me, sig, &inf, nullptr);
            } else {
                Inference return_inf;
                inference_init(&return_inf, sig->type_params);
                infer_types(me, &return_inf, return_source, return_type, INFER_PRIORITY_NORMAL);
                return_mapper = return_inference_mapper(me, sig, &inf, &return_inf);
                inference_drop(&return_inf);
            }
        }
    }
    self.return_mapper = return_mapper;

    if (sig->this_type && this_arg) infer_types(me, &inf, this_arg, sig->this_type, INFER_PRIORITY_NORMAL);

    // First pass: everything that isn't context sensitive
    bool any_sensitive = false;
    for (u32 i = 0; i < args->len; i++) {
        Arg* arg = &args->data[i];
        if (i >= shape.count && shape.rest_variadic) break;
        if (arg->context_sensitive || arg->generic_call) {
            any_sensitive = true;
            // The rest of an object or array literal with a context-sensitive function in it is inferred from already
            // (tsc's CheckMode.SkipContextSensitive): `{ schema, onSubmit: (values) => {} }`, and so is the return
            // type of a function that is context sensitive only through what it returns (return_only_type() in expr.c)
            Ty const* param_type = shape_type_at(&shape, i);
            if (arg->context_sensitive && !arg->spread && !arg->part_of_spread && param_type) {
                my outer_inference = &self;
                Ty const* context = return_mapper ? instantiate_instantiable(me, param_type, return_mapper) : param_type;
                GenericFunctionMode mode = {0};
                Ty const* type = ty_check_literal_argument(me, arg->expr, context, ty_is_const_type_variable(me, param_type, 0),
                    true, &mode);
                arg->nested_generic = mode.skipped;
                my outer_inference = nullptr;
                infer_types(me, &inf, type, param_type, INFER_PRIORITY_NORMAL);
            }
            continue;
        }
        my outer_inference = &self;
        if (is_literal_arg(arg)) {
            GenericFunctionMode mode = {0};
            Ty const* param_type = shape_type_at(&shape, i);
            arg->type = ty_check_literal_argument(me, arg->expr, literal_arg_context(me, param_type, return_mapper),
                param_type && ty_is_const_type_variable(me, param_type, 0), false, &mode);
            arg->nested_generic = mode.skipped;
            any_sensitive |= mode.skipped;
        } else if (return_mapper && !arg->spread && !arg->part_of_spread) {
            // What the return type says about the type parameters helps type the argument (a tuple, a literal). tsc
            // instantiates the contextual type of each expression as it is asked for, so a function argument (whose
            // parameters are annotated if it gets here) returns into the instantiated return type
            Ty const* param_type = shape_type_at(&shape, i);
            Ty const* context = !param_type ? nullptr : arg->expr->tag == EXPR_FUNCTION ?
                ty_instantiate(me, param_type, return_mapper) : instantiate_instantiable(me, param_type, return_mapper);
            arg->type = ty_check_expr_in_const(me, arg->expr, context,
                ty_is_const_type_variable(me, param_type, 0));
            // A literal the instantiated contextual type has room for is regular, so it isn't widened as a candidate
            // (tsc's checkExpressionWithContextualType)
            if (context && (arg->type->flags & TYF_FRESH_LITERAL) &&
                ty_is_literal_of_contextual_type(me, arg->type, instantiate_with_return_mapper(me, param_type, return_mapper))
            ) {
                arg->type = ty_regular(me, arg->type);
            }
        } else {
            check_arg(me, &shape, args, i);
        }
        // A function argument's return type is computed while the calls it returns still see this one (tsc computes it
        // as it checks the argument)
        // (Only with a contextual signature, like tsc's contextuallyCheckFunctionExpressionOrObjectLiteralMethod: without
        // one, it waits for the check against the instantiated parameter, whose context may be the function itself)
        if (arg->expr->tag == EXPR_FUNCTION && !arg->spread && !arg->part_of_spread &&
            ty_function_assigned_contextual_signature(me, arg->expr->function)
        ) {
            ty_argument_return_type(me, arg->expr->function);
        }
        my outer_inference = nullptr;
        Signature const* contextual = nullptr;
        arg->generic_function = !arg->spread && !arg->part_of_spread &&
            generic_function_argument(me, arg->type, shape_type_at(&shape, i), &contextual);
        if (arg->generic_function) {
            any_sensitive = true;
            continue;
        }
        infer_from_arg(me, &inf, &shape, args, i);
    }
    // Second pass: each context-sensitive argument sees what is known by then, which is thereby decided. So does each
    // generic function argument, instantiated in the context of its parameter's signature (tsc's
    // instantiateSignatureInContextOf with the fixing mapper) and inferred from like that
    for (u32 i = 0; any_sensitive && i < args->len; i++) {
        Arg* arg = &args->data[i];
        if (i >= shape.count && shape.rest_variadic) break;
        if (arg->generic_function && !skip_generic) {
            Ty const* param_type = shape_type_at(&shape, i);
            Signature const* contextual = nullptr;
            Signature const* generic = generic_function_argument(me, arg->type, param_type, &contextual);
            // Higher order: if the call returns a function and not everything is inferred yet, the argument's type
            // parameters can become the result's (`pipe(list, box)` is `<T>(a: T) => { value: T[] }`)
            if (returns_single_signature(me, sig)) {
                bool all_inferred = true;
                for (u32 j = 0; j < inf.len; j++) all_inferred &= inf.params[j].co_len || inf.params[j].contra_len;
                Signature const* propagated = all_inferred ? nullptr : inference_propagate_generic(me, &inf, generic, contextual);
                if (propagated) {
                    infer_types(me, &inf, ty_signature_type(me, propagated), param_type, INFER_PRIORITY_NORMAL);
                    continue;
                }
            }
            TyMapper const* so_far = fix_for_generic_function(me, sig, &inf, contextual);
            Signature const* instantiated = ty_instantiate_signature_in_context_of(me, generic, contextual, so_far);
            infer_types(me, &inf, ty_signature_type(me, instantiated), param_type, INFER_PRIORITY_NORMAL);
            continue;
        }
        if (arg->nested_generic && !arg->context_sensitive) {
            if (skip_generic) continue;
            // Again, now with the generic functions in it instantiated in context
            GenericFunctionMode mode = { .sig = sig, .inf = &inf };
            Ty const* param_type = shape_type_at(&shape, i);
            my outer_inference = &self;
            arg->type = ty_check_literal_argument(me, arg->expr, literal_arg_context(me, param_type, return_mapper),
                param_type && ty_is_const_type_variable(me, param_type, 0), false, &mode);
            my outer_inference = nullptr;
            infer_from_arg(me, &inf, &shape, args, i);
            continue;
        }
        if (arg->generic_call) {
            if (skip_generic || skip_sensitive) continue;
            // Checked like in the first pass, but now the calls in it see what the other arguments inferred
            my outer_inference = &self;
            check_arg(me, &shape, args, i);
            my outer_inference = nullptr;
            infer_from_arg(me, &inf, &shape, args, i);
            continue;
        }
        if (!arg->context_sensitive || arg->generic_function || skip_sensitive) continue;
        Ty const* param_type = shape_type_at(&shape, i);
        if (param_type && arg->expr->tag == EXPR_FUNCTION && !arg->spread && !arg->part_of_spread) {
            infer_from_annotated_params(me, &inf, arg->expr->function, param_type);
        }
        // A call resolved again (for its errors, for another outer inference) fixes what it fixed the first time: the
        // functions in the argument have their contextual signatures by now (tsc resolves once)
        u64 fix_key[3] = { KEY_ARG_FIXES, (u64)(uintptr_t)arg->expr, (u64)(uintptr_t)sig };
        ArgFixes const* recorded = intern_get(&my links, fix_key, 3);
        u64 fixed_before = 0;
        if (recorded) {
            // (To what they were fixed to then: the parameters of the functions were assigned from that)
            for (u32 j = 0; j < inf.len && j < 64; j++) {
                if ((recorded->mask >> j & 1) && !inf.params[j].fixed) inf.params[j].fixed = recorded->types[j];
            }
        }
        for (u32 j = 0; j < inf.len && j < 64; j++) fixed_before |= (u64)(inf.params[j].fixed != nullptr) << j;
        // (Its contextual type is instantiated where it is looked at, see ty_instantiate_contextual_type(), and the
        // functions in it fix what their contextual signatures ask for, see ty_fix_contextual_signature())
        my outer_inference = &self;
        self.fixing = true;
        if (arg->nested_generic && !skip_generic) {
            GenericFunctionMode mode = { .sig = sig, .inf = &inf };
            arg->type = ty_check_literal_argument(me, arg->expr, param_type, false, false, &mode);
        } else {
            arg->type = ty_check_expr(me, arg->expr, param_type);
        }
        self.fixing = false;
        my outer_inference = nullptr;
        if (!recorded) {
            u64 fixed_now = 0;
            for (u32 j = 0; j < inf.len && j < 64; j++) fixed_now |= (u64)(inf.params[j].fixed != nullptr) << j;
            ArgFixes* record = arena_new(&my arena, ArgFixes);
            record->mask = fixed_now & ~fixed_before;
            record->types = arena_new_n(&my arena, Ty const*, inf.len);
            for (u32 j = 0; j < inf.len; j++) record->types[j] = inf.params[j].fixed;
            *intern_slot(me, &my links, fix_key, 3) = record;
        }
        infer_from_arg(me, &inf, &shape, args, i);
    }
    // A variadic rest parameter takes the remaining arguments as a tuple
    if (shape.rest_variadic) {
        u32 from = shape.count < args->len ? shape.count : args->len;
        // (Under `...args: T` with a `const T`, each argument is in a const context: its contextual type is `T[i]`)
        bool is_const = ty_is_const_type_variable(me, shape.rest_variadic, 0);
        for (u32 i = from; i < args->len; i++) {
            if (args->data[i].type) continue;
            Ty const* contextual = nullptr;
            if (is_const && args->spread_index < 0) {
                Value index = value_number((double)(i - from));
                contextual = ty_indexed(me, shape.rest_variadic, ty_literal(me, &index));
            }
            args->data[i].type = ty_check_expr_in_const(me, args->data[i].expr, contextual, is_const);
        }
        if (args->len > shape.count || args->spread_index < 0) {
            infer_types(me, &inf, rest_args_tuple(me, args, from, !is_const, is_const && !ty_some_mutable_array_like(me, shape.rest_variadic)), shape.rest_variadic, INFER_PRIORITY_NORMAL);
        }
    }
    free(shape.types);
    free(self.sites);

    TyMapper const* mapper = inference_mapper(me, sig, &inf, false);
    *inferred_out = ty_list_copy(me, inf.inferred_type_params, inf.inferred_type_param_count);
    inference_drop(&inf);
    my outer_inference = outer;
    my inferential_depth--;
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
        if (skip_sensitive && waits_for_sensitive(arg)) {
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
            source = rest_args_tuple(me, args, i, false, false);
            target = shape->rest_variadic;
        } else {
            break;
        }
        bool fits = ty_is_related(me, source, target, relation);
        if (!fits && relation == TY_RELATION_SUBTYPE && arg->context_sensitive) {
            fits = ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE);
        }
        if (!fits) {
            *arg_type = source;
            *param_type = target;
            return (i32)i;
        }
        if (i >= shape->count && shape->rest_variadic) break;
    }
    // No argument for a rest parameter that isn't an array: it still gets `[]` (tsc's checkApplicableSignature with
    // getNonArrayRestType), reported at the call (the index past the arguments). HACK: not for a generic one, whose
    // conditional types need tsc's skipTrue to take `[]` (xstate's `...[options]: ConditionalRequired<...>`), see
    // hacks.md
    if (shape->rest_variadic && args->len <= shape->count && args->spread_index < 0 && !args->incomplete &&
        !(shape->rest_variadic->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)
    ) {
        Ty const* source = rest_args_tuple(me, args, args->len, false, false);
        if (!ty_is_related(me, source, shape->rest_variadic, relation)) {
            *arg_type = source;
            *param_type = shape->rest_variadic;
            return (i32)args->len;
        }
    }
    return -1;
}

// The parameters a call's arguments are checked against. A JSX element has one argument, its attributes, which any
// signature takes (tsc's hasCorrectArity: a class's argumentless constructor too), as the props of the signature
static ParamShape call_param_shape(Checker* me, CallInfo const* info, Signature const* sig, TyMapper const* mapper) {
    if (!info->jsx) {
        ParamShape shape = param_shape(me, sig, mapper);
        // A call in a JavaScript file without strictNullChecks can also leave out trailing parameters of type undefined,
        // unknown or any (tsc's hasCorrectArity: acceptsVoidUndefinedUnknownOrAny)
        SourceFile const* file = info->call_span.source;
        if (file && file->is_js && !compiler_options()->strict_null_checks) {
            while (shape.min > 0 && shape.min <= shape.count && accepts_void_undefined_unknown_any(shape.types[shape.min - 1])) {
                shape.min--;
            }
        }
        return shape;
    }
    signature_resolve(me, sig);
    ParamShape shape = { .count = 1, .min = sig->min_args ? 1 : 0 };
    shape.types = malloc(sizeof(Ty const*));
    panic_if(!shape.types, "out of memory");
    shape.types[0] = ty_jsx_signature_props(me, info->jsx, sig, mapper, info->jsx_class);
    return shape;
}

// What the errors about a call are reported at (tsc's getDiagnosticForCallNode): the name of a method, else the callee;
// the whole expression of `new`; a JSX element's tag name
static Span callee_span(CallInfo const* info) {
    if (info->jsx) return info->jsx->tag_name.inner_span;
    if (info->call->uses_new && span_valid(info->call_span)) return info->call_span;
    Expr const* callee = &info->call->callable;
    while (callee->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(callee)) callee = &callee->generic_params->expr;
    if (callee->tag == EXPR_PROPERTY && callee->property->form == PROPERTY_EXPR_DOT && !expr_is_perenthisized(callee)) {
        return callee->property->prop.inner_span;
    }
    return callee->inner_span;
}

// Where tsc's invocationError reports TS2349/TS2351: the callee, but a call's (not `new`'s) is at the method name like
// getDiagnosticSpanForCallNode's
static Span invocation_error_span(CallInfo const* info) {
    Expr const* callee = &info->call->callable;
    if (info->jsx || info->tagged_template || info->decorator || info->call->uses_new || info->call->is_extends) {
        return callee->outer_span;
    }
    while (callee->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(callee)) callee = &callee->generic_params->expr;
    if (callee->tag == EXPR_PROPERTY && callee->property->form == PROPERTY_EXPR_DOT && !expr_is_perenthisized(callee)) {
        return callee->property->prop.inner_span;
    }
    return info->call->callable.outer_span;
}

static Ty const* candidate_return_type(Checker* me, Candidate const* candidate) {
    Ty const* ret = ty_instantiate(me, signature_return_type(me, candidate->sig), candidate->mapper);
    if (candidate->inferred_type_params.len) {
        // A function generic in the type parameters propagated from generic function arguments (tsc's
        // getSignatureInstantiation with inferredTypeParameters)
        Signature const* returned = single_call_or_construct(me, ret);
        if (returned) ret = ty_signature_type(me, ty_signature_with_type_params(me, returned, candidate->inferred_type_params));
    }
    return ret;
}

static Span span_of_args(Args const* args, u32 from, u32 to) {
    return span_between(args->data[from].expr->outer_span, args->data[to].expr->outer_span);
}

// `mapper`: the signatures instantiated with it (the one candidate whose instantiated rest type didn't fit)
static void report_arity(
    Checker* me, Signature* const* signatures, u32 count, Args const* args, CallInfo const* info, TyMapper const* mapper,
    ErrorCtx* errs
) {
    u32 min = UINT32_MAX;
    u32 max = 0;
    u32 max_below = 0;
    u32 min_above = UINT32_MAX;
    bool has_rest = false;
    bool any_below = false;
    Signature const* closest = nullptr;
    for (u32 i = 0; i < count; i++) {
        ParamShape shape = call_param_shape(me, info, signatures[i], mapper);
        if (shape.min < min) {
            min = shape.min;
            closest = signatures[i];
        }
        if (shape.count > max) max = shape.count;
        if (shape.count < args->len && (!any_below || shape.count > max_below)) {
            max_below = shape.count;
            any_below = true;
        }
        if (shape.min > args->len && shape.min < min_above) min_above = shape.min;
        has_rest |= shape_has_rest(&shape);
        free(shape.types);
    }
    Span name_span = callee_span(info);
    if (args->spread_index >= 0) {
        error_ctx_error_d(errs, args->data[args->spread_index].expr->outer_span, 2556,
            "A spread argument must either have a tuple type or be passed to a rest parameter.");
    } else if (count > 1 && !has_rest && any_below && min_above != UINT32_MAX) {
        error_ctx_error_d(errs, name_span, 2575,
            "No overload expects ", args->len, " arguments, but overloads do exist that expect either ", max_below,
            " or ", min_above, " arguments.");
    } else {
        Span span = args->len > max && !has_rest ? span_of_args(args, max, args->len - 1) : name_span;
        Error* error;
        if (info->decorator) {
            // (tsc's getArgumentArityError for a decorator: too many at the expression, too few at the decorator)
            span = args->len > max && !has_rest ? info->call->callable.outer_span : info->call_span;
            error = error_ctx_error_d(errs, span, has_rest ? 1279 : 1278, "The runtime will invoke the decorator with ", args->len,
                " arguments, but the decorator expects ", has_rest ? "at least " : "", min);
            if (!has_rest && min != max) cstr_append_d(&error->message, DEFAULT_DISPLAY, "-", max);
            cstr_append_d(&error->message, DEFAULT_DISPLAY, ".");
        } else if (has_rest) {
            error = error_ctx_error_d(errs, span, 2555, "Expected at least ", min, " arguments, but got ", args->len, ".");
        } else if (min == max) {
            error = error_ctx_error_d(errs, span, 2554, "Expected ", min, " arguments, but got ", args->len, ".");
        } else {
            error = error_ctx_error_d(errs, span, 2554, "Expected ", min, "-", max, " arguments, but got ", args->len, ".");
        }
        // The first parameter without an argument of the signature that needs the fewest
        SigParam const* missing = args->len < min && closest && args->len < closest->param_count ?
            &closest->params[args->len] : nullptr;
        if (missing && missing->decl) {
            Binding const* binding = &missing->decl->initializer.binding;
            Span at = span_valid(missing->decl->jsdoc_span) ? missing->decl->jsdoc_span : missing->decl->span;
            if (binding->tag != BINDING_IDENTIFIER) {
                error_add_related_d(error, missing->decl->span, 6211, "An argument matching this binding pattern was not provided.");
            } else if (missing->rest) {
                error_add_related_d(error, missing->decl->span, 6236, "Arguments for the rest parameter '",
                    binding->content_span, "' were not provided.");
            } else if (!span_is_written(binding->content_span)) {
                // An unnamed parameter of a JSDoc function type: tsc says its position
                error_add_related_d(error, at, 6210, "An argument for '", (i32)args->len,
                    "' was not provided.");
            } else {
                error_add_related_d(error, at, 6210, "An argument for '", binding->content_span,
                    "' was not provided.");
            }
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
    ParamShape shape = call_param_shape(me, info, candidate->sig, candidate->mapper);
    check_args(me, &shape, args, false);
    Ty const* arg_type = nullptr;
    Ty const* param_type = nullptr;
    i32 index = first_unfit_argument(me, &shape, args, TY_RELATION_ASSIGNABLE, false, &arg_type, &param_type);
    Span span = call_span;
    if (index >= 0 && info->jsx) {
        // The attributes, at the tag name (tsc's checkApplicableSignatureForJsxCallLikeElement)
        span = callee_span(info);
        ty_check_assignable_expr(me, arg_type, param_type, span, &info->jsx->attributes, errs);
    } else if (index >= 0 && (u32)index == args->len) {
        ty_report_unfit_argument(me, arg_type, param_type, span, nullptr, errs);
    } else if (index >= 0) {
        Arg const* arg = &args->data[index];
        bool is_rest_tuple = arg_type != arg->type;
        Expr const* effective = expr_effective_check_node(arg->expr);
        span = is_rest_tuple ? span_of_args(args, (u32)index, args->len - 1) : effective->inner_span;
        ty_report_unfit_argument(me, arg_type, param_type, span, is_rest_tuple ? nullptr : effective, errs);
    }
    free(shape.types);
    return span;
}

static Resolution resolve_with_signatures(
    Checker* me, Signature* const* signatures, u32 count, Args* args, CallInfo const* info, Span call_span,
    ErrorCtx* errs
);

// The call fits none of the overloads of a function, but would the implementation? (tsc's
// addImplementationSuccessElaboration.) Only function declarations: methods and constructors TODO
static void add_implementation_success(Checker* me, Candidate const* candidate, Args* args, CallInfo const* info,
    Span call_span, Error* error
) {
    Signature const* failed = candidate->sig;
    while (failed->target) failed = failed->target;
    Symbol const* symbol = failed->function ? failed->function->symbol : nullptr;
    if (!error || !symbol || symbol->decl_count < 2) return;
    FunctionImpl const* impl = nullptr;
    for (u32 i = 0; i < symbol->decl_count && !impl; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_FUNCTION) continue;
        FunctionImpl const* function = symbol->decls[i].node;
        if (function->has_body) impl = function;
    }
    if (!impl || impl == failed->function) return;
    Signature const* sig = single_call_or_construct(me, ty_function_type(me, impl));
    if (!sig) return;
    Signature* impl_sig = (Signature*)sig;
    bool fits = resolve_with_signatures(me, &impl_sig, 1, args, info, call_span, nullptr).chosen.sig;
    // The arguments stay typed as checked against the overload (what the types baseline shows)
    ParamShape shape = call_param_shape(me, info, candidate->sig, candidate->mapper);
    check_args(me, &shape, args, false);
    free(shape.types);
    if (!fits) return;
    error_add_related_s(error, impl->name, 2793, cstr_lit(
        "The call would have succeeded against this implementation, but implementation signatures of overloads are not "
        "externally visible."));
}

// Where a signature is declared, as tsc's createDiagnosticForNode(signature.declaration) points at it
static Span signature_decl_span(Signature const* sig) {
    while (sig->target) sig = sig->target;
    if (sig->function && !sig->function->is_type && span_valid(sig->function->name)) return sig->function->name;
    if (span_valid(sig->name_span)) return sig->name_span;
    return sig->decl_span;
}

// Moves an error's message chain under `parent` and its related information into `related`
static void move_error_into_chain(Error* error, Error* parent, VecError* related) {
    Error line = { .message = error->message };
    for_each (sub, vec_error, &error->sub_errors) vec_error_push(span_valid(sub->span) ? related : &line.sub_errors, *sub);
    error->sub_errors.size = 0;
    vec_error_drop(&error->sub_errors);
    vec_error_push(&parent->sub_errors, line);
}

// The signature a candidate was checked as (tsc's checkCandidate: the instantiation with what was inferred)
static Signature const* candidate_instantiation(Checker* me, Candidate const* candidate) {
    Signature const* sig = candidate->sig;
    if (!candidate->mapper || !sig->type_params.len) return sig;
    Ty const* stack_args[16];
    u32 len = sig->type_params.len;
    Ty const** args = len <= c_arraylen(stack_args) ? stack_args : malloc(sizeof(Ty const*) * len);
    panic_if(!args, "out of memory");
    for (u32 i = 0; i < len; i++) args[i] = ty_instantiate(me, sig->type_params.data[i], candidate->mapper);
    Signature const* ret = ty_signature_instantiation(me, sig, (TyList){ .data = args, .len = len });
    if (args != stack_args) free(args);
    return ret;
}

static void report_no_match(
    Checker* me, Candidate const* unfit, u32 unfit_count, u32 signature_count, Args* args, CallInfo const* info,
    Span call_span, ErrorCtx* errs
) {
    ErrorCtx collected = error_ctx_init();
    collected.phase = errs->phase;
    if (unfit_count == 1 || unfit_count > 3) {
        // The last candidate's errors (each with a chain of its own when there were many candidates)
        Candidate const* last = &unfit[unfit_count - 1];
        report_unfit_argument(me, last, args, info, call_span, &collected);
        for_each (e, vec_error, &collected.errors) {
            Error error = *e;
            if (unfit_count > 3) {
                error = (Error){ .ts_code = 2769, .phase = e->phase, .span = e->span,
                    .message = cstr_lit("No overload matches this call.") };
                error_sub_error_s(&error, span_null, TS_CODE_NONE, cstr_lit("The last overload gave the following error."));
                VecError related = {0};
                move_error_into_chain(e, vec_error_back_mut(&error.sub_errors), &related);
                for_each (r, vec_error, &related) vec_error_push(&error.sub_errors, *r);
                related.size = 0;
                vec_error_drop(&related);
                error_add_related_s(&error, signature_decl_span(last->sig), 2771,
                    cstr_lit("The last overload is declared here."));
            }
            add_implementation_success(me, last, args, info, call_span, &error);
            error_ctx_append_error(errs, error);
        }
        collected.errors.size = 0;
        error_ctx_drop(&collected);
        return;
    }
    // Two or three: every candidate's errors, unless one of them had several (then only the one with the fewest)
    VecError per[3] = {0};
    isize_t max = 0, min = INTPTR_MAX;
    u32 min_index = 0;
    for (u32 i = 0; i < unfit_count; i++) {
        error_ctx_clear(&collected);
        report_unfit_argument(me, &unfit[i], args, info, call_span, &collected);
        per[i] = collected.errors;
        collected.errors = (VecError){0};
        isize_t count = vec_error_size(&per[i]);
        if (count <= min) {
            min = count;
            min_index = i;
        }
        if (count > max) max = count;
    }
    error_ctx_drop(&collected);
    Error head = { .ts_code = 2769, .phase = errs->phase, .message = cstr_lit("No overload matches this call.") };
    VecError related = {0};
    Span common_span = span_null;
    bool same_span = true, any = false;
    for (u32 i = 0; i < unfit_count; i++) {
        if (max > 1 && i != min_index) continue;
        cstr rendered = cstr_null;
        ty_display_call_signature(me, (TyDisplay){ .target = &rendered }, candidate_instantiation(me, &unfit[i]));
        for_each (e, vec_error, &per[i]) {
            if (!any) common_span = e->span;
            any = true;
            same_span &= span_eq(e->span, common_span);
            error_sub_error_d(&head, span_null, TS_CODE_NONE,
                "Overload ", i + 1, " of ", signature_count, ", '", rendered, "', gave the following error.");
            move_error_into_chain(e, vec_error_back_mut(&head.sub_errors), &related);
        }
        cstr_drop(&rendered);
    }
    for (u32 i = 0; i < unfit_count; i++) {
        if (max > 1 && i != min_index) vec_error_drop(&per[i]);
        else {
            per[i].size = 0; // messages and chains were moved
            vec_error_drop(&per[i]);
        }
    }
    for_each (r, vec_error, &related) vec_error_push(&head.sub_errors, *r);
    related.size = 0;
    vec_error_drop(&related);
    head.span = any && same_span && span_valid(common_span) ? common_span : callee_span(info);
    add_implementation_success(me, &unfit[0], args, info, call_span, &head);
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
        // (tsc's getTypeArgumentsFromNodes: the default, else the constraint, else unknown)
        Ty const* fallback = ty_param_default(me, params.data[i]);
        if (!fallback) fallback = ty_param_constraint(me, params.data[i]);
        results[i] = fallback ? ty_instantiate(me, fallback, mapper) : ty_intrinsic(me, TY_UNKNOWN_);
        mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    }
    return mapper;
}

static bool is_super_call(CallExpr const* call);

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

// tsc's checkTypeArguments: whether explicit type arguments satisfy their constraints (instantiated with all of the
// arguments). With `errs`, TS2344 for the first one that doesn't
static bool check_type_argument_constraints(
    Checker* me, Signature const* sig, TyMapper const* mapper, CallInfo const* info, ErrorCtx* errs
) {
    for (u32 i = 0; i < info->type_args.len && i < sig->type_params.len; i++) {
        Ty const* constraint = ty_param_constraint(me, sig->type_params.data[i]);
        if (!constraint) continue;
        Ty const* arg = info->type_args.data[i];
        constraint = ty_with_this_arg(me, ty_instantiate(me, constraint, mapper), arg);
        if (ty_is_related(me, arg, constraint, TY_RELATION_ASSIGNABLE)) continue;
        if (errs) {
            cstr arg_str, constraint_str;
            ty_report_pair_cstr(me, arg, constraint, &arg_str, &constraint_str);
            ty_report_unassignable_with_head(me, arg, constraint, 2344, cstr_d(
                "Type '", arg_str, "' does not satisfy the constraint '", constraint_str, "'."
            ), vec_spanned_type_at(info->type_arg_nodes, i)->span, errs);
            c_drop(cstr, &arg_str, &constraint_str);
        }
        return false;
    }
    return true;
}

// `skip_generic`: generic function arguments aren't inferred from either (tsc's SkipGenericFunctions)
static Candidate make_candidate(
    Checker* me, Signature const* sig, Args* args, CallInfo const* info, bool skip_sensitive, bool skip_generic
) {
    Candidate ret = { .sig = sig };
    if (!sig->type_params.len) return ret;
    ret.mapper = info->type_arg_nodes ? explicit_type_arguments(me, sig, info) :
        infer_type_arguments(me, sig, args, info, skip_sensitive, skip_generic, &ret.inferred_type_params);
    return ret;
}

static Resolution resolve_with_signatures(
    Checker* me, Signature* const* signatures, u32 count, Args* args, CallInfo const* info, Span call_span,
    ErrorCtx* errs
) {
    Resolution ret = {0};
    Candidate* unfit = calloc(count ? count : 1, sizeof(Candidate));
    u32 unfit_count = 0;
    bool type_argument_arity_ok = false;
    Candidate arity_candidate = {0}; // tsc's candidateForArgumentArityError
    Signature const* type_arg_candidate = nullptr; // tsc's candidateForTypeArgumentError
    TyRelation rounds[2] = { TY_RELATION_SUBTYPE, TY_RELATION_ASSIGNABLE };
    // With a choice to make, candidates are first tried without their context-sensitive arguments, which are only
    // checked (and typed for good) against the first candidate the rest fit (tsc's argCheckMode)
    bool skip_sensitive = false;
    if (count > 1 || (count == 1 && signatures[0]->type_params.len)) {
        for (u32 i = 0; i < args->len; i++) skip_sensitive |= args->data[i].context_sensitive;
    }
    for (u32 round = count > 1 ? 0 : 1; round < 2 && !ret.chosen.sig; round++) {
        unfit_count = 0;
        arity_candidate = (Candidate){0};
        type_arg_candidate = nullptr;
        for (u32 i = 0; i < count && !ret.chosen.sig; i++) {
            Signature const* sig = signatures[i];
            signature_resolve(me, sig);
            if (!has_correct_type_argument_arity(me, sig, info)) continue;
            type_argument_arity_ok = true;
            ParamShape uninstantiated = call_param_shape(me, info, sig, nullptr);
            bool arity_ok = has_correct_arity(&uninstantiated, args);
            bool generic_rest = uninstantiated.rest_variadic != nullptr;
            free(uninstantiated.types);
            if (!arity_ok) continue;
            Candidate candidate = make_candidate(me, sig, args, info, skip_sensitive, false);
            if (info->type_arg_nodes && candidate.mapper &&
                !check_type_argument_constraints(me, sig, candidate.mapper, info, nullptr)
            ) {
                type_arg_candidate = sig;
                continue;
            }
            if (generic_rest && candidate.mapper) {
                // A rest parameter of a generic type may have a different arity once instantiated (tsc's second
                // hasCorrectArity for getNonArrayRestType): `...m: T extends true ? [] : [X]`
                ParamShape instantiated = call_param_shape(me, info, sig, candidate.mapper);
                arity_ok = has_correct_arity(&instantiated, args);
                free(instantiated.types);
                if (!arity_ok) {
                    arity_candidate = candidate;
                    continue;
                }
            }
            ParamShape shape = call_param_shape(me, info, sig, candidate.mapper);
            check_args(me, &shape, args, skip_sensitive);
            Ty const* arg_type;
            Ty const* param_type;
            bool fits = first_unfit_argument(me, &shape, args, rounds[round], skip_sensitive, &arg_type, &param_type) < 0;
            if (fits && skip_sensitive) {
                // Again with everything, for this candidate and the ones after it
                skip_sensitive = false;
                free(shape.types);
                candidate = make_candidate(me, sig, args, info, false, false);
                shape = call_param_shape(me, info, sig, candidate.mapper);
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
        ret.ret = candidate_return_type(me, &ret.chosen);
    } else {
        mark_would_report(info);
        if (errs) {
            if (!type_argument_arity_ok) {
                // (A `super(...)`'s are the heritage clause's, which reports them: TS2314/TS2315)
                if (!(info->call && is_super_call(info->call))) report_type_argument_arity(me, signatures, count, info, errs);
            } else if (unfit_count) {
                report_no_match(me, unfit, unfit_count, count, args, info, call_span, errs);
            } else if (arity_candidate.sig) {
                Signature* sig = (Signature*)arity_candidate.sig;
                report_arity(me, &sig, 1, args, info, arity_candidate.mapper, errs);
            } else if (type_arg_candidate) {
                check_type_argument_constraints(me, type_arg_candidate,
                    explicit_type_arguments(me, type_arg_candidate, info), info, errs);
            } else {
                report_arity(me, signatures, count, args, info, nullptr, errs);
            }
        }
        // What the call is taken to return anyway (tsc's getCandidateForOverloadFailure): of several non-generic
        // overloads, all of their returns at once (createUnionOfSignaturesForOverloadFailure: `string & number` is
        // never), else the candidate that got furthest, or every possibility
        bool some_generic = false;
        for (u32 i = 0; i < count; i++) {
            signature_resolve(me, signatures[i]);
            some_generic |= signatures[i]->type_params.len > 0;
        }
        if (count > 1 && !some_generic) {
            Ty const** return_types = malloc(sizeof(Ty const*) * count);
            for (u32 i = 0; i < count; i++) return_types[i] = signature_return_type(me, signatures[i]);
            ret.ret = ty_intersection(me, return_types, count, nullptr, (TyList){0});
            free(return_types);
        } else if (unfit_count) {
            // Inferred again from what doesn't depend on the candidate: context-sensitive and generic function
            // arguments left out (tsc's inferSignatureInstantiationForOverloadFailure)
            Candidate fallback = unfit[unfit_count - 1];
            if (fallback.sig->type_params.len && !info->type_arg_nodes) {
                fallback = make_candidate(me, fallback.sig, args, info, true, true);
                // (The arguments are left as checked against the candidate, which is what the types baseline shows)
                ParamShape shape = call_param_shape(me, info, unfit[unfit_count - 1].sig, unfit[unfit_count - 1].mapper);
                check_args(me, &shape, args, false);
                free(shape.types);
            }
            ret.ret = candidate_return_type(me, &fallback);
        } else if (count == 1 && !signatures[0]->type_params.len) {
            ret.ret = signature_return_type(me, signatures[0]);
        } else if (info->type_arg_nodes && !type_argument_arity_ok) {
            // tsc's pickLongestCandidateSignature with getTypeArgumentsFromNodes: the first candidate that takes the
            // arguments (else the one with the most parameters), its type arguments cut or filled in
            u32 best = 0;
            u32 most = 0;
            for (u32 i = 0; i < count; i++) {
                u32 params = ty_signature_param_count(signatures[i]);
                if (ty_signature_has_rest(signatures[i]) || params >= args->len) {
                    best = i;
                    break;
                }
                if (i == 0 || params > most) most = params, best = i;
            }
            Candidate candidate = { .sig = signatures[best], .mapper = explicit_type_arguments(me, signatures[best], info) };
            ret.ret = candidate_return_type(me, &candidate);
        } else {
            Ty const** return_types = malloc(sizeof(Ty const*) * count);
            u32 len = 0;
            for (u32 i = 0; i < count; i++) {
                signature_resolve(me, signatures[i]);
                if (signatures[i]->type_params.len && has_correct_type_argument_arity(me, signatures[i], info)) {
                    Candidate candidate = make_candidate(me, signatures[i], args, info, false, false);
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

// tsc's getSignaturesOfType arrayFallbackSignatures: a method of a union of arrays and tuples whose members have no
// signatures in common is that method of an array of all their elements (`(A[] | B[]).filter` is `(A | B)[]`'s,
// readonly if one of them is). Null if the callee isn't such a method
static Ty const* array_fallback_callee(Checker* me, CallExpr const* call) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    if (callee->tag != EXPR_PROPERTY) return nullptr;
    PropertyExpr const* property = callee->property;
    Expr const* name = &property->prop;
    if (property->form != PROPERTY_EXPR_DOT || name->tag != EXPR_LITERAL || name->literal.tag != VALUE_STRING) {
        return nullptr;
    }
    Ty const* receiver = ty_expr_type(me, &property->expr);
    if (property->optional_chain) receiver = ty_non_nullable(me, receiver);
    Ty const* apparent = ty_apparent_type(me, receiver);
    if (apparent->kind != TY_UNION) return nullptr;
    TyList members = apparent->composite.types;
    Ty const** elements = malloc(sizeof(Ty const*) * members.len);
    bool readonly = false;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* array = ty_apparent_type(me, members.data[i]);
        if (ty_is_tuple(array)) array = ty_tuple_as_array(me, array);
        if (!ty_is_array(me, array)) {
            free(elements);
            return nullptr;
        }
        readonly |= array->reference.target != ty_array(me, ty_intrinsic(me, TY_ANY), false)->reference.target;
        elements[i] = ty_reference_args(me, array).data[0];
    }
    Ty const* element = ty_union(me, elements, members.len, nullptr, (TyList){0});
    free(elements);
    Ty const* index = ty_string_literal(me, cstr_sv(&name->literal.string));
    return ty_access(me, ty_array(me, element, readonly), index);
}

static bool is_super_call(CallExpr const* call) {
    // (`super<T>()` is TS2754 and otherwise a super call: its type arguments are ignored)
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    return !call->uses_new && callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER;
}

// A parameter of the declaration is written as a literal type (`method: "stop"`, tsc's SignatureFlags.HasLiteralTypes)
static bool signature_has_literal_types(Signature const* sig) {
    while (sig->target) sig = sig->target;
    if (!sig->function) return false;
    for_each (arg, vec_function_arg, &sig->function->args) {
        Binding const* binding = &arg->initializer.binding;
        if (binding->has_explicit_type && binding->explicit_type.tag == TYPE_VALUE &&
            binding->explicit_type.value.tag != VALUE_UNDEFINED
        ) {
            return true;
        }
    }
    return false;
}

// The (merged symbol, declaration block) group of a signature, which reorder_candidates() orders by (tsc's
// reorderCandidates)
static TyDeclGroup const* signature_decl_group(Checker* me, Signature const* sig) {
    while (sig->target) sig = sig->target;
    return sig->function ? ty_decl_group(me, sig->function) : nullptr;
}

// tsc's reorderCandidates: signatures with literal types first; and of the same interface, each declaration's
// signatures go before those of its declarations before (in their own order): lib.es2015.promise's tuple overload of
// Promise.all is tried before lib.es2015.iterable's Iterable one. Another interface's (a base's) come after
static void reorder_candidates(Checker* me, Signature** signatures, u32 count) {
    static char const sentinel = 0; // Stands for the symbol and declaration of signatures not from an interface
    Signature** result = malloc(sizeof(Signature*) * (count ? count : 1));
    panic_if(!result, "out of memory");
    u32 len = 0, cutoff = 0, index = 0;
    i32 specialized = -1;
    Symbol const* last_symbol = nullptr;
    void const* last_parent = nullptr;
    for (u32 i = 0; i < count; i++) {
        TyDeclGroup const* group = signature_decl_group(me, signatures[i]);
        Symbol const* symbol = group ? group->symbol : (Symbol const*)(void const*)&sentinel;
        void const* parent = group ? group->declaration : &sentinel;
        if (!last_symbol || symbol == last_symbol) {
            if (last_parent && parent == last_parent) {
                index++;
            } else {
                last_parent = parent;
                index = cutoff;
            }
        } else {
            index = cutoff = len;
            last_parent = parent;
        }
        last_symbol = symbol;
        u32 splice;
        if (signature_has_literal_types(signatures[i])) {
            splice = (u32)++specialized;
            cutoff++;
        } else {
            splice = index;
        }
        memmove(&result[splice + 1], &result[splice], sizeof(Signature*) * (len - splice));
        result[splice] = signatures[i];
        len++;
    }
    memcpy(signatures, result, sizeof(Signature*) * count);
    free(result);
}

static Ty const* resolve_call(Checker* me, Ty const* callee, CallInfo const* info, Span call_span, ErrorCtx* errs) {
    CallExpr const* call = info->call;
    Ty const* any = ty_intrinsic(me, TY_ANY);
    Ty const* apparent = callee->flags & TYF_ANY ? callee : ty_apparent_type(me, callee);
    if (apparent->flags & TYF_ANY) {
        // tsc's resolveCallExpression/resolveNewExpression: an untyped call can't take type arguments
        if (info->type_arg_nodes && !(callee->flags & TYF_ERROR) && !call->is_extends && !is_super_call(call)) {
            mark_would_report(info);
            error_ctx_error_d(errs, call_span, 2347, "Untyped function calls may not accept type arguments.");
        }
        return callee->flags & TYF_ERROR ? ty_intrinsic(me, TY_ERROR) : any; // (resolveErrorCall: an unresolved name's any too)
    }

    bool super_call = is_super_call(call);
    bool construct = (call->uses_new && !info->new_as_call) || call->is_extends || super_call;
    // A union is called through the signatures its members have in common (tsc's getUnionSignatures)
    Signature* const* union_sigs = nullptr;
    u32 union_count = 0;
    if (apparent->kind == TY_UNION) union_sigs = ty_union_signatures(me, apparent->composite.types, construct, &union_count);
    if (apparent->kind == TY_UNION && !union_sigs && !construct) {
        Ty const* fallback = array_fallback_callee(me, call);
        if (fallback && fallback->kind != TY_UNION) return resolve_call(me, fallback, info, call_span, errs);
    }
    if (apparent->kind == TY_UNION && !union_sigs && !call->is_extends) {
        bool all_have = true;
        for (u32 i = 0; i < apparent->composite.types.len && all_have; i++) {
            Members const* members = ty_members(me, ty_apparent_type(me, apparent->composite.types.data[i]));
            all_have = construct ? members->construct_count > 0 : members->call_count > 0;
        }
        if (all_have) {
            mark_would_report(info);
            if (errs) {
                cstr rendered = ty_to_cstr(me, callee);
                Error* e = error_ctx_error_d(errs, invocation_error_span(info), construct ? 2351 : 2349,
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

    // (An intersection's signatures are those of its members, see resolve_intersection_members())
    Signature** signatures = nullptr;
    u32 count = 0;
    u32 other_count = 0; // signatures of the kind that wasn't asked for
    if (union_sigs) {
        signatures = malloc(sizeof(Signature*) * union_count);
        panic_if(!signatures, "out of memory");
        for (u32 i = 0; i < union_count; i++) signatures[count++] = union_sigs[i];
    } else {
        Members const* members = ty_members(me, apparent);
        Signature* const* list = construct ? members->construct : members->call;
        u32 list_count = construct ? members->construct_count : members->call_count;
        other_count = construct ? members->call_count : members->construct_count;
        if (list_count) {
            signatures = malloc(sizeof(Signature*) * list_count);
            panic_if(!signatures, "out of memory");
            for (u32 j = 0; j < list_count; j++) signatures[count++] = list[j];
        }
    }

    Ty const* ret = any;
    // tsc's isUntypedFunctionCall: something assignable to Function without signatures (`Function` itself) is called
    // like any
    if (!count && !other_count && !construct && apparent->kind != TY_UNION && !(apparent->flags & TYF_NEVER)) {
        Ty const* function_type = ty_global_function_type(me);
        if (function_type && ty_is_related(me, callee, function_type, TY_RELATION_ASSIGNABLE)) {
            if (info->type_arg_nodes) {
                mark_would_report(info);
                error_ctx_error_d(errs, call_span, 2347, "Untyped function calls may not accept type arguments.");
            }
            return any;
        }
    }
    if (!count) {
        mark_would_report(info); // (Not always, but calling nothing is cheap to resolve again)
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
            } else if (!info->new_as_call && !info->tagged_template && !info->jsx) {
                // Resolved as a call, whose result has to be void if it has a declaration (TS2350); the new is any
                // either way
                CallInfo as_call = *info;
                Candidate chosen = {0};
                as_call.new_as_call = true;
                as_call.chosen_out = &chosen;
                resolve_call(me, callee, &as_call, call_span, errs);
                Signature const* root = chosen.sig;
                while (root && !root->function && root->target) root = root->target;
                if (root && root->function && !(signature_return_type(me, chosen.sig)->flags & TYF_VOID)) {
                    error_ctx_error_d(errs, call_span, 2350, "Only a void function can be called with the 'new' keyword.");
                }
            }
        } else if (call->uses_new) {
            Error* e = error_ctx_error_d(errs, call->callable.outer_span, 2351, "This expression is not constructable.");
            // (tsc's invocationErrorDetails: about the apparent type)
            cstr rendered = ty_to_cstr(me, apparent);
            error_sub_error_d(e, span_null, TS_CODE_NONE, "Type '", rendered, "' has no construct signatures.");
            cstr_drop(&rendered);
        } else if (other_count && !info->tagged_template && !info->decorator) { // (tsc's resolveCallExpression only)
            cstr rendered = ty_to_cstr(me, callee);
            error_ctx_error_d(errs, call->callable.outer_span, 2348,
                "Value of type '", rendered, "' is not callable. Did you mean to include 'new'?");
            cstr_drop(&rendered);
        } else {
            Error* e = error_ctx_error_d(errs, invocation_error_span(info), 2349, "This expression is not callable.");
            // (tsc's invocationErrorDetails: about the apparent type, `{}` for unknown)
            cstr rendered = ty_to_cstr(me, callee->flags & TYF_UNKNOWN ? ty_empty_object_type(me) : apparent);
            error_sub_error_d(e, span_null, TS_CODE_NONE, "Type '", rendered, "' has no call signatures.");
            cstr_drop(&rendered);
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
            mark_would_report(info);
            error_ctx_error_d(errs, call_span, 2511, "Cannot create an instance of an abstract class.");
            free(signatures);
            return any;
        }
        if (!ty_constructor_accessible(me, call, signatures[0], call_span, errs)) {
            mark_would_report(info);
            free(signatures);
            return any;
        }
    }

    // A JavaScript function tagged `@class` isn't called without `new` (tsc's resolveCallExpression: a call signature
    // whose declaration has getJSDocClassTag)
    if (!construct && !call->uses_new && !info->tagged_template && !info->decorator) {
        for (u32 i = 0; i < count; i++) {
            Signature const* root = signatures[i];
            while (root && !root->function && root->target) root = root->target;
            FunctionImpl const* function = root ? root->function : nullptr;
            if (!function || !function->jsdoc_class || !function->full_span.source || !function->full_span.source->is_js) continue;
            mark_would_report(info);
            if (errs) {
                cstr rendered = ty_to_cstr(me, callee);
                error_ctx_error_d(errs, call_span, 2348, "Value of type '", rendered, "' is not callable. Did you mean to include 'new'?");
                cstr_drop(&rendered);
            }
            free(signatures);
            return ty_intrinsic(me, TY_ERROR);
        }
    }

    CallInfo with_this = *info;
    // (tsc's getSignatureApplicabilityError doesn't check `this` of a new expression)
    if (!construct && !info->new_as_call) {
        bool any_this = false;
        for (u32 i = 0; i < count && !any_this; i++) {
            signature_resolve(me, signatures[i]);
            any_this = signatures[i]->this_type != nullptr;
        }
        if (any_this) with_this.this_arg = this_argument(me, call, call_span, &with_this.this_span);
    }
    reorder_candidates(me, signatures, count);
    Args args = collect_args(me, info);
    Resolution resolution = resolve_with_signatures(me, signatures, count, &args, &with_this, call_span, errs);
    ret = resolution.ret;
    if (info->chosen_out) *info->chosen_out = resolution.chosen;
    free(args.data);
    free(signatures);
    return ret;
}

// A signature taking `param` (none if null) and returning `ret`, for the tags that aren't components
static Signature* jsx_synthetic_signature(Checker* me, Ty const* param, Ty const* ret) {
    SigParam* params = nullptr;
    if (param) {
        params = arena_new(&my arena, SigParam);
        *params = (SigParam){ .name = checker_atom(me, c_sv("props")), .type = param };
    }
    Signature* sig = arena_new(&my arena, Signature);
    *sig = (Signature){
        .state = LAZY_STATE_DONE,
        .params = params,
        .param_count = param ? 1 : 0,
        .min_args = param ? 1 : 0,
        .ret_state = LAZY_STATE_DONE,
        .ret = ret,
        .predicate_done = true,
    };
    return sig;
}

static Signature* const* jsx_signatures_of(Checker* me, JsxElement const* element, Ty const* type, u32 depth, u32* count_out);

// tsc's getUnionSignatures of the members' JSX signatures
static Signature* const* jsx_union_signatures(Checker* me, JsxElement const* element, TyList types, u32 depth, u32* count_out) {
    Signature* const** lists = malloc(sizeof(Signature* const*) * types.len);
    u32* lens = malloc(sizeof(u32) * types.len);
    panic_if(!lists || !lens, "out of memory");
    for (u32 i = 0; i < types.len; i++) lists[i] = jsx_signatures_of(me, element, types.data[i], depth + 1, &lens[i]);
    Signature* const* ret = ty_union_signature_lists(me, lists, lens, types.len, count_out);
    free(lists);
    free(lens);
    return ret;
}

// tsc's getUninstantiatedJsxSignaturesOfType: what a tag of this type is called with. `string` takes anything, a
// string literal is an intrinsic element taking its props, anything else its construct signatures, else its call
// signatures, else (a union) the union of its members'
static Signature* const* jsx_signatures_of(Checker* me, JsxElement const* element, Ty const* type, u32 depth, u32* count_out) {
    *count_out = 0;
    if (type->kind == TY_INTRINSIC && (type->flags & TYF_STRING)) {
        Signature** ret = arena_new(&my arena, Signature*);
        *ret = jsx_synthetic_signature(me, nullptr, ty_intrinsic(me, TY_ANY));
        *count_out = 1;
        return ret;
    }
    if (type->kind == TY_LITERAL && (type->flags & TYF_STRING_LITERAL) && type->literal.string) {
        // (tsc's createSignatureForJSXIntrinsic; its TS2339 for a tag that isn't there isn't reported here)
        JsxIntrinsic intrinsic = ty_jsx_intrinsic_named(me, element, atom_sv(type->literal.string));
        Ty const* element_type = ty_jsx_type(me, element, c_sv("Element"));
        Signature** ret = arena_new(&my arena, Signature*);
        *ret = jsx_synthetic_signature(me, intrinsic.props ? intrinsic.props : ty_intrinsic(me, TY_ERROR),
            element_type ? element_type : ty_intrinsic(me, TY_ERROR));
        *count_out = 1;
        return ret;
    }
    Ty const* apparent = ty_apparent_type(me, type);
    if (!(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) && apparent->kind != TY_UNION) return nullptr;
    if (apparent->kind == TY_UNION) {
        Signature* const* ret = ty_union_signatures(me, apparent->composite.types, true, count_out);
        if (!*count_out) ret = ty_union_signatures(me, apparent->composite.types, false, count_out);
        if (!*count_out && depth < 8) ret = jsx_union_signatures(me, element, apparent->composite.types, depth, count_out);
        return ret;
    }
    Members const* members = ty_members(me, apparent);
    if (members->construct_count) {
        *count_out = members->construct_count;
        return members->construct;
    }
    *count_out = members->call_count;
    return members->call;
}

// A JSX element with a value tag (`<Comp />`, `<this.x />`): the construct signatures of the tag's type, else its call
// signatures, called with the attributes as the one argument (tsc's resolveJsxOpeningLikeElement,
// getUninstantiatedJsxSignaturesOfType). `sig` is null if the tag isn't typed or nothing fit
JsxResolution ty_resolve_jsx_element(Checker* me, JsxElement const* element, ErrorCtx* errs) {
    JsxResolution ret = {0};
    Ty const* tag_type = ty_expr_type(me, &element->tag_name);
    Ty const* apparent = ty_apparent_type(me, tag_type);
    if ((tag_type->flags & TYF_ANY) || (apparent->flags & TYF_ANY) || (tag_type->flags & TYF_STRING)) return ret;
    Signature* const* signatures = nullptr;
    u32 count = 0;
    if (apparent->kind == TY_UNION) {
        signatures = ty_union_signatures(me, apparent->composite.types, true, &count);
        ret.class_component = count > 0;
        if (!count) {
            signatures = ty_union_signatures(me, apparent->composite.types, false, &count);
            ret.function_component = count > 0;
        }
        if (!count) signatures = jsx_union_signatures(me, element, apparent->composite.types, 0, &count);
    } else if (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) {
        Members const* members = ty_members(me, apparent);
        if (members->construct_count) {
            signatures = members->construct;
            count = members->construct_count;
            ret.class_component = true;
        } else if (members->call_count) {
            signatures = members->call;
            count = members->call_count;
            ret.function_component = true;
        }
    }
    if (!count) {
        // Something callable that isn't typed (tsc's isUntypedFunctionCall: a Function) is fine
        Ty const* function = ty_global_function_type(me);
        bool untyped = apparent->kind != TY_UNION && !(apparent->flags & TYF_NEVER) && function &&
            ty_is_related(me, tag_type, function, TY_RELATION_ASSIGNABLE);
        if (!untyped && errs) {
            error_ctx_error_d(errs, element->tag_name.inner_span, 2604, "JSX element type '", element->tag_name.inner_span,
                "' does not have any construct or call signatures.");
        }
        return ret;
    }
    CallInfo info = { .jsx = element, .jsx_class = ret.class_component };
    Ty const* type_args[32];
    if (!vec_spanned_type_is_empty(&element->type_args)) {
        info.type_arg_nodes = &element->type_args;
        u32 len = 0;
        for_each (i, vec_spanned_type, info.type_arg_nodes) {
            if (len < c_arraylen(type_args)) type_args[len++] = ty_from_node(me, &i->type, (LowerCtx){ .generic = true });
        }
        info.type_args = (TyList){ .data = type_args, .len = len };
    }
    Args args = { .spread_index = -1 };
    args_push(&args, (Arg){ .expr = &element->attributes, .context_sensitive = ty_expr_is_context_sensitive(&element->attributes) });
    // A component that needs more arguments than the factory gives fits no signature, which is all that's said
    // (checkApplicableSignatureForJsxCallLikeElement checks the arity before the attributes)
    if (!ty_jsx_tag_arity_fits(me, element, tag_type, errs)) errs = nullptr;
    Resolution resolution = resolve_with_signatures(me, signatures, count, &args, &info, element->tag_name.inner_span, errs);
    free(args.data);
    ret.sig = resolution.chosen.sig;
    ret.mapper = resolution.chosen.mapper;
    ret.ret = resolution.ret;
    return ret;
}

static Ty const* resolve_into(
    Checker* me, Ty const* callee, CallExpr const* call, Expr const* tagged_template, Ty const* contextual,
    Span call_span, ErrorCtx* errs, Candidate* chosen_out, bool* would_report
) {
    CallInfo info = {
        .call = call, .tagged_template = tagged_template, .contextual = contextual, .chosen_out = chosen_out,
        .call_span = call_span, .would_report = would_report,
    };
    Ty const* type_args[32];
    // `super(x)` in `class A extends B<T>`: B's constructors with the type arguments of the heritage clause (tsc's
    // getInstantiatedConstructorsForTypeArguments)
    // TODO: without written type arguments tsc instantiates with the defaults instead of inferring
    Expr const* super_callee = &call->callable;
    if (super_callee->tag == EXPR_GENERIC_PARAMS) super_callee = &super_callee->generic_params->expr;
    ClassDecl const* super_class = is_super_call(call) && super_callee->identifier.target ?
        symbol_find_node(super_callee->identifier.target, SYMBOL_DECL_CLASS) : nullptr;
    if (super_class && super_class->extends.tag == EXPR_GENERIC_PARAMS) {
        info.type_arg_nodes = &super_class->extends.generic_params->params;
    } else if (call->callable.tag == EXPR_GENERIC_PARAMS && !is_super_call(call)) {
        // `f<A, B>(x)`: the callee type is that of `f`
        info.type_arg_nodes = &call->callable.generic_params->params;
    }
    if (info.type_arg_nodes) {
        u32 len = 0;
        for_each (i, vec_spanned_type, info.type_arg_nodes) {
            if (len < c_arraylen(type_args)) type_args[len++] = ty_from_node(me, &i->type, (LowerCtx){ .generic = true });
        }
        info.type_args = (TyList){ .data = type_args, .len = len };
    }
    return resolve_call(me, callee, &info, call_span, errs);
}



static bool has_predicate_or_declared_never(Checker* me, Signature const* sig) {
    return signature_predicate(me, sig) || signature_declared_never(sig);
}

// What control flow analysis wants to know about a call (tsc's getEffectsSignature()): the type predicate of the
// signature it resolves to, and whether that is declared to return `never`. A callee with one signature that isn't
// generic is that signature whatever the arguments are; anything else is resolved, which checks the arguments: the
// caller (flow.c) sees to it that this happens in uncached mode
// A union callee whose members have one non-generic signature each: the predicate they have in common, members that
// return `false` included (tsc's getUnionOrIntersectionTypePredicate of the union signature)
static CallEffects union_call_effects(Checker* me, Ty const* callee) {
    CallEffects none = {0};
    TyPredicate first = {0};
    bool has_first = false;
    TyBuf types = {0};
    for (u32 i = 0; i < callee->composite.types.len; i++) {
        Ty const* member = ty_apparent_type(me, callee->composite.types.data[i]);
        if (!(member->flags & (TYF_OBJECT | TYF_INTERSECTION))) goto fail;
        Members const* members = ty_members(me, member);
        if (members->call_count != 1) goto fail;
        Signature const* sig = members->call[0];
        signature_resolve(me, sig);
        if (sig->type_params.len) goto fail;
        TyPredicate const* predicate = signature_predicate(me, sig);
        if (!predicate) {
            Ty const* ret = ty_regular(me, signature_return_type(me, sig));
            if (ret != ty_intrinsic(me, TY_FALSE)) goto fail;
            continue;
        }
        if (!predicate->type) goto fail;
        if (has_first && (first.is_this != predicate->is_this || first.asserts != predicate->asserts ||
            first.param_index != predicate->param_index)
        ) {
            goto fail;
        }
        if (!has_first) first = *predicate;
        has_first = true;
        ty_buf_push(&types, predicate->type);
    }
    if (!has_first) goto fail;
    first.type = ty_union(me, types.data, types.len, nullptr, (TyList){0});
    ty_buf_drop(&types);
    return (CallEffects){ .has_predicate = true, .predicate = first };
fail:
    ty_buf_drop(&types);
    return none;
}

CallEffects ty_call_effects(Checker* me, Ty const* callee, CallExpr const* call) {
    CallEffects none = {0};
    if (!callee || call->uses_new || call->is_extends) return none;
    Ty const* apparent = ty_apparent_type(me, callee);
    if (apparent->kind == TY_UNION && !(apparent->flags & TYF_BOOLEAN)) return union_call_effects(me, apparent);
    // (getSignaturesOfType: an intersection's are its members')
    if ((apparent->flags & TYF_ANY) || !(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return none;
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
        resolve_into(me, callee, call, nullptr, nullptr, span_null, nullptr, &chosen, nullptr);
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
        module = ty_dynamic_import_module_type(me, ref->module, ref->span.source);
    }
    Symbol const* promise_symbol = program_get_global(my program, c_sv("Promise"), SYMBOL_TYPE);
    if (!promise_symbol) return ty_intrinsic(me, TY_ANY); // TODO: TS2711
    // The ES5 lib has the type, not the constructor (tsc's createPromiseReturnType: getGlobalPromiseConstructorSymbol)
    if (!program_get_global(my program, c_sv("Promise"), SYMBOL_VALUE)) {
        ty_error_at(me, span_null, 2468, cstr_lit("Cannot find global value 'Promise'."));
        ty_error_at_current_node(me, 2712, cstr_lit("A dynamic import call in ES5 requires the 'Promise' constructor.  "
            "Make sure you have a declaration for the 'Promise' constructor or include 'ES2015' in your '--lib' option."));
    }
    Ty const* promise = ty_declared_type_of(me, promise_symbol);
    if (promise->kind != TY_DECLARED || promise->declared.type_params.len != 1) return ty_intrinsic(me, TY_ANY);
    return ty_reference(me, promise, (TyList){ .data = &module, .len = 1 });
}

// `require("m")` in a JavaScript file: the module, like `import x = require("m")` gives (tsc's
// resolveExternalModuleTypeByLiteral)
static Ty const* require_type(Checker* me, CallExpr const* call) {
    if (call->callable.tag != EXPR_IDENTIFIER || !call->callable.identifier.require_call) return nullptr;
    if (!expr_symbol_is_commonjs_require(call->callable.identifier.target)) return nullptr;
    ModuleRef const* ref = call->callable.identifier.module_ref;
    if (!ref || !ref->module || ref->module->is_shorthand_module) return ty_intrinsic(me, TY_ANY);
    return ty_type_of_symbol(me, module_export_equals(ref->module));
}

// `node`: what the resolution is remembered by, the call or the tagged template
static Ty const* call_return_type(
    Checker* me, Ty const* callee, CallExpr const* call, Expr const* tagged_template, void const* node, Ty const* contextual
) {
    // Outside of inference and control flow, this is what the call's memo will say it is: the typecheck walk won't
    // resolve it again if that would report nothing (tsc resolves once, getResolvedSignature)
    bool settled = !my inferential_depth && !my outer_inference && !my flow_uncached;
    bool would_report = false;
    Ty const* ret = resolve_into(me, callee, call, tagged_template, contextual, span_null, nullptr, nullptr,
        settled ? &would_report : nullptr);
    if (settled) {
        u64 key[2] = { KEY_CALL_RESOLUTION, (u64)(uintptr_t)node };
        // The contextual type of a resolution that would report nothing (the checker itself for none), else null
        *intern_slot(me, &my links, key, 2) = would_report ? nullptr : contextual ? (void const*)contextual : (void const*)me;
    }
    return ret;
}

// Resolves the call again with `errs`, unless its type came from a resolution under the same contextual type that
// would report nothing (see call_return_type())
static void call_typecheck(
    Checker* me, Ty const* callee, CallExpr const* call, Expr const* tagged_template, void const* node, Ty const* contextual,
    Span call_span, ErrorCtx* errs
) {
    u64 key[2] = { KEY_CALL_RESOLUTION, (u64)(uintptr_t)node };
    void const* clean = node ? intern_get(&my links, key, 2) : nullptr;
    if (clean && clean == (contextual ? (void const*)contextual : (void const*)me)) return;
    resolve_into(me, callee, call, tagged_template, contextual, call_span, errs, nullptr, nullptr);
}

Ty const* ty_call_return_type(Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual) {
    Ty const* imported = dynamic_import_type(me, call);
    if (imported) return imported;
    Ty const* required = require_type(me, call);
    if (required) return required;
    return call_return_type(me, callee, call, nullptr, call, contextual);
}

// `call` may be a temporary (`class A extends B`), never recorded
void ty_call_typecheck(Checker* me, Ty const* callee, CallExpr const* call, Ty const* contextual, Span call_span, ErrorCtx* errs) {
    call_typecheck(me, callee, call, nullptr, call->is_extends ? nullptr : call, contextual, call_span, errs);
}

// The stand-in call of a tagged template (see CallInfo.tagged_template). HACK: the callee is a shallow copy of the
// tag, which is only looked at for its spans and syntax (never typed: its memo is the tag's)
static CallExpr tagged_template_call(Expr const* template) {
    return (CallExpr){ .callable = *template->template_->tag, .args = template->template_->substitutions };
}

Ty const* ty_tagged_template_return_type(Checker* me, Ty const* callee, Expr const* template, Ty const* contextual) {
    CallExpr call = tagged_template_call(template);
    return call_return_type(me, callee, &call, template, template, contextual);
}

void ty_tagged_template_typecheck(Checker* me, Ty const* callee, Expr const* template, Ty const* contextual, ErrorCtx* errs) {
    CallExpr call = tagged_template_call(template);
    call_typecheck(me, callee, &call, template, template, contextual, template->outer_span, errs);
}

static Symbol const* awaited_alias(Checker* me) {
    Symbol const* symbol = program_get_global(my program, c_sv("Awaited"), SYMBOL_TYPE);
    return symbol && (symbol->flags & SYMBOL_TYPE_ALIAS) ? symbol : nullptr;
}

// tsc's maybeTypeOfKind(type, TypeVariable): a type parameter or indexed access, or a union or intersection with one
static bool ty_type_variable_in(Ty const* ty) {
    if (ty->kind == TY_TYPE_PARAM || ty->kind == TY_INDEXED) return true;
    if (ty->kind != TY_UNION && ty->kind != TY_INTERSECTION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (ty_type_variable_in(ty->composite.types.data[i])) return true;
    }
    return false;
}

// tsc's isAwaitedTypeNeeded: a generic type that could be (or hold) a promise stays `Awaited<T>`
static bool awaited_type_needed(Checker* me, Ty const* ty, Symbol const* awaited) {
    if ((ty->flags & TYF_ANY) || (awaited && ty->alias == awaited)) return false;
    if (!ty_is_generic_object(me, ty)) return false;
    Ty const* constraint = ty_base_constraint(me, ty);
    // (A mapped type or generic tuple is its own base constraint; without one, a type variable needs it)
    if (!constraint && ty->kind != TY_TYPE_PARAM && !(ty->flags & TYF_DEFERRED) && (ty->flags & TYF_OBJECT)) constraint = ty;
    if (!constraint) return ty_type_variable_in(ty);
    if (constraint->flags & (TYF_ANY | TYF_UNKNOWN)) return true;
    if (constraint->flags & TYF_NON_PRIMITIVE) return true;
    if ((constraint->flags & TYF_OBJECT) && !ty_members(me, constraint)->mapped && !ty_members(me, constraint)->prop_count && !ty_members(me, constraint)->call_count &&
        !ty_members(me, constraint)->construct_count && !ty_members(me, constraint)->index_count
    ) {
        return true; // `{}`
    }
    TyList parts = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
    Atom const* then = checker_atom(me, c_sv("then"));
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* apparent = ty_apparent_type(me, parts.data[i]);
        if (ty_prop_type_of(me, apparent, then)) return true; // Thenable
    }
    return false;
}

// The types whose awaited type is being computed further up (tsc's awaitedTypeStack)
typedef struct AwaitedFrame {
    Ty const* ty;
    struct AwaitedFrame const* up;
} AwaitedFrame;

static Ty const* awaited_cycle(Checker* me) {
    ty_error_at_current_node_d(me, 1062, "Type is referenced directly or indirectly in the fulfillment callback of its own 'then' method.");
    return ty_intrinsic(me, TY_ERROR);
}

static bool awaited_in_progress(AwaitedFrame const* stack, Ty const* ty) {
    for (; stack; stack = stack->up) {
        if (stack->ty == ty) return true;
    }
    return false;
}

static bool primitive_or_never(Ty const* ty) {
    u32 primitive = TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
        TYF_BOOLEAN_LITERAL | TYF_ENUM | TYF_ES_SYMBOL_LIKE | TYF_VOID | TYF_NULLABLE | TYF_NEVER;
    if (ty->kind != TY_UNION) return ty->flags & primitive;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (!primitive_or_never(ty->composite.types.data[i])) return false;
    }
    return true;
}

static Signature* const* call_signatures_of(Checker* me, Ty const* ty, u32* count) {
    if (ty->kind == TY_UNION) return ty_union_signatures(me, ty->composite.types, false, count);
    Ty const* apparent = ty_apparent_type(me, ty);
    // (An intersection's are those of its members, see ty_members())
    Members const* members = (apparent->flags & TYF_OBJECT) || apparent->kind == TY_INTERSECTION ? ty_members(me, apparent) : nullptr;
    *count = members ? members->call_count : 0;
    return members ? members->call : nullptr;
}

// What the first parameter of each signature takes, never for a signature without parameters (tsc's
// getTypeOfFirstParameterOfSignature), in a union. Null without signatures
static Ty const* first_params_union(Checker* me, Signature* const* sigs, u32 count, bool subtype) {
    if (!count) return nullptr;
    Ty const** types = malloc(sizeof(Ty const*) * count);
    for (u32 i = 0; i < count; i++) {
        signature_resolve(me, sigs[i]);
        Ty const* param = sigs[i]->param_count ? ty_signature_param_type_at(me, sigs[i], 0) : nullptr;
        types[i] = param ? param : ty_intrinsic(me, TY_NEVER);
    }
    Ty const* ret = subtype ? ty_union_subtype_reduced(me, types, count) : ty_union(me, types, count, nullptr, (TyList){0});
    free(types);
    return ret;
}

// What a thenable's `then` passes its fulfillment callback (tsc's getPromisedTypeOfPromise), null if it isn't one
// TODO: the errors (TS1320 and friends) when it's not a proper thenable
static Ty const* promised_type_of(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_ANY) return nullptr;
    Ty const* constraint = ty_base_constraint(me, ty);
    if (primitive_or_never(constraint ? constraint : ty)) return nullptr;
    Ty const* then = ty_prop_type_of(me, ty, checker_atom(me, c_sv("then")));
    if (!then || then->flags & TYF_ANY) return nullptr;
    u32 count = 0;
    Signature* const* sigs = call_signatures_of(me, then, &count);
    Signature** candidates = malloc(sizeof(Signature*) * (count + 1));
    u32 candidate_count = 0;
    for (u32 i = 0; i < count; i++) {
        signature_resolve(me, sigs[i]);
        Ty const* this_type = sigs[i]->this_type;
        if (this_type && !(this_type->flags & TYF_VOID) && !ty_is_related(me, ty, this_type, TY_RELATION_SUBTYPE)) continue;
        candidates[candidate_count++] = sigs[i];
    }
    Ty const* onfulfilled = first_params_union(me, candidates, candidate_count, false);
    free(candidates);
    if (!onfulfilled) return nullptr;
    onfulfilled = ty_non_nullable(me, onfulfilled);
    if (onfulfilled->flags & TYF_ANY) return nullptr;
    Signature* const* callbacks = call_signatures_of(me, onfulfilled, &count);
    return first_params_union(me, callbacks, count, true);
}

// tsc's getSignaturesOfType(ty, SignatureKind.Call).length
u32 ty_call_signature_count(Checker* me, Ty const* ty) {
    u32 count = 0;
    call_signatures_of(me, ty, &count);
    return count;
}

// tsc's !!getAwaitedTypeOfPromise(ty): a thenable
bool ty_is_promise(Checker* me, Ty const* ty) { return promised_type_of(me, ty) != nullptr; }

// The type `await` gives for a value of the given type. `wrap`: a generic one that could be a promise is `Awaited<T>`
// (tsc's getAwaitedType; without it getAwaitedTypeNoAlias)
// A cycle is TS1062 at the current node (tsc reports it at the error node its callers pass, the `await` expression)
static Ty const* awaited_type_in(Checker* me, Ty const* ty, bool wrap, AwaitedFrame const* stack) {
    Symbol const* awaited = awaited_alias(me);
    if (awaited && ty->alias == awaited) return ty;
    if (wrap && awaited && awaited_type_needed(me, ty, awaited)) return ty_alias_instance(me, awaited, (TyList){ .data = &ty, .len = 1 });
    if (ty->kind == TY_UNION) {
        // `type T = string | Promise<T>` awaits T again (a deferred reference makes it an infinite structure)
        if (awaited_in_progress(stack, ty)) return awaited_cycle(me);
        AwaitedFrame frame = { ty, stack };
        Ty const** parts = malloc(sizeof(Ty const*) * ty->composite.types.len);
        for (u32 i = 0; i < ty->composite.types.len; i++) parts[i] = awaited_type_in(me, ty->composite.types.data[i], wrap, &frame);
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
            if (args.data[0] == ty || awaited_in_progress(stack, args.data[0])) return awaited_cycle(me);
            AwaitedFrame frame = { ty, stack };
            return awaited_type_in(me, args.data[0], wrap, &frame);
        }
    }
    Ty const* promised = promised_type_of(me, ty);
    if (promised) {
        if (promised == ty || awaited_in_progress(stack, promised)) return awaited_cycle(me);
        AwaitedFrame frame = { ty, stack };
        return awaited_type_in(me, promised, wrap, &frame);
    }
    return ty;
}

static Ty const* awaited_type(Checker* me, Ty const* ty, bool wrap) { return awaited_type_in(me, ty, wrap, nullptr); }

Ty const* ty_awaited(Checker* me, Ty const* ty) { return awaited_type(me, ty, true); }


// tsc's createPromiseLikeType: unknown without a global PromiseLike
Ty const* ty_promise_like_of(Checker* me, Ty const* ty) {
    Symbol const* symbol = program_get_global(my program, c_sv("PromiseLike"), SYMBOL_TYPE);
    Ty const* like = symbol ? ty_declared_type_of(me, symbol) : nullptr;
    if (!like || like->kind != TY_DECLARED || like->declared.type_params.len != 1) return ty_intrinsic(me, TY_UNKNOWN_);
    return ty_reference(me, like, (TyList){ .data = &ty, .len = 1 });
}

// HACK: tsc's unwrapAwaitedType, applied to what a return of an async function returns. tsc relates `Awaited<X>` to X
// through the default constraint of the conditional type, which tttt can't
Ty const* ty_awaited_returned(Checker* me, Ty const* ty) {
    ty = awaited_type(me, ty, false);
    Symbol const* awaited = awaited_alias(me);
    if (!awaited) return ty;
    if (ty->alias == awaited && ty->alias_args.len == 1) return ty->alias_args.data[0];
    if (ty->kind != TY_UNION) return ty;
    Ty const** parts = malloc(sizeof(Ty const*) * ty->composite.types.len);
    bool changed = false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        Ty const* part = ty->composite.types.data[i];
        parts[i] = part->alias == awaited && part->alias_args.len == 1 ? part->alias_args.data[0] : part;
        changed |= parts[i] != part;
    }
    Ty const* ret = changed ? ty_union(me, parts, ty->composite.types.len, nullptr, (TyList){0}) : ty;
    free(parts);
    return ret;
}
// HACK: iteration and generator types use this where tsc has Awaited<T> too, which tttt can't relate to T yet
Ty const* ty_awaited_no_alias(Checker* me, Ty const* ty) { return awaited_type(me, ty, false); }

// Decorators checked as calls (tsc's checkDecorator)

// A global generic type of the lib applied to `args`, TS2318 if there's no such type (tsc's getGlobalType with
// reportErrors, which tsc reports once per program: the error context deduplicates it)
static Ty const* global_generic(Checker* me, char const* name, Ty const* const* args, u32 len, ErrorCtx* errs) {
    Symbol const* symbol = program_get_global(my program, csview_from(name), SYMBOL_TYPE);
    Ty const* declared = symbol ? ty_declared_type_of(me, symbol) : nullptr;
    if (!declared || declared->kind != TY_DECLARED || declared->declared.type_params.len != len) {
        if (!symbol) error_ctx_global_error_d(errs, 2318, "Cannot find global type '", name, "'.");
        return nullptr;
    }
    return ty_reference(me, declared, (TyList){ .data = (Ty const**)args, .len = len });
}

static Ty const* union2(Checker* me, Ty const* a, Ty const* b) {
    Ty const* both[2] = { a, b };
    return ty_union(me, both, 2, nullptr, (TyList){0});
}

// tsc's getClassElementPropertyKeyType
static Ty const* member_key_type(Checker* me, ClassItem* item) {
    Expr const* computed = item->tag == CLASS_ITEM_FIELD ? item->field.computed_key :
        item->tag == CLASS_ITEM_METHOD ? item->method.computed_key : nullptr;
    if (computed) {
        Ty const* key = ty_expr_type(me, computed);
        return key->flags & TYF_ES_SYMBOL_LIKE ? key : ty_intrinsic(me, TY_STRING);
    }
    cstr const* name = item->tag == CLASS_ITEM_FIELD ? &item->field.name_string : &item->method.name_string;
    return ty_string_literal(me, cstr_sv(name));
}

static Ty const* member_this_type(Checker* me, ClassDecl const* class_decl, ClassItem* item) {
    return class_item_is_static(item) ? ty_type_of_symbol(me, class_decl->symbol) : ty_declared_type_of(me, class_decl->symbol);
}

static Signature* decorator_signature(Checker* me, Ty const* const* params, char const* const* names, u32 count, Ty const* ret) {
    SigParam* sig_params = arena_new_n(&my arena, SigParam, count);
    for (u32 i = 0; i < count; i++) sig_params[i] = (SigParam){ .name = checker_atom(me, csview_from(names[i])), .type = params[i] };
    Signature* sig = arena_new(&my arena, Signature);
    *sig = (Signature){
        .state = LAZY_STATE_DONE, .params = sig_params, .param_count = count, .min_args = count,
        .ret_state = LAZY_STATE_DONE, .ret = ret, .predicate_done = true,
    };
    return sig;
}

// tsc's getLegacyDecoratorCallSignature/getESDecoratorCallSignature: the synthetic arguments into `out`, the
// signature (the decorator's contextual type) into `sig_out`; false if there is none
static bool decorator_call(Checker* me, DecoratorSite site, DecoratorCall* out, Signature** sig_out, ErrorCtx* errs) {
    bool legacy = compiler_options()->experimental_decorators;
    ClassDecl const* class_decl = site.class_decl;
    ClassItem* item = site.item;
    Ty const* void_type = ty_intrinsic(me, TY_VOID);
    *out = (DecoratorCall){0};
    static char const* const legacy_names[] = { "target", "propertyKey", "descriptor" };
    static char const* const param_names[] = { "target", "propertyKey", "parameterIndex" };
    static char const* const es_names[] = { "target", "context" };
    char const* const* names = legacy_names;
    if (!item) {
        Ty const* class_type = ty_type_of_symbol(me, class_decl->symbol);
        out->head_code = 1238;
        if (legacy) {
            out->params[0] = class_type;
            out->count = 1;
            out->arity = DECORATOR_ARITY_FIXED;
            out->ret = union2(me, class_type, void_type);
        } else {
            Ty const* context = global_generic(me, "ClassDecoratorContext", &class_type, 1, errs);
            if (!context) return false;
            out->params[0] = class_type;
            out->params[1] = context;
            out->count = 2;
            out->arity = DECORATOR_ARITY_ES;
            out->ret = union2(me, class_type, void_type);
            names = es_names;
        }
    } else if (site.param_index >= 0) {
        if (!legacy) return false;
        out->head_code = 1239;
        out->void_only = true;
        out->params[0] = item->tag == CLASS_ITEM_CONSTRUCTOR ? ty_type_of_symbol(me, class_decl->symbol) : member_this_type(me, class_decl, item);
        out->params[1] = item->tag == CLASS_ITEM_CONSTRUCTOR ? ty_intrinsic(me, TY_UNDEFINED) : member_key_type(me, item);
        Value index = value_number(site.param_index);
        out->params[2] = ty_literal(me, &index);
        out->count = 3;
        out->arity = DECORATOR_ARITY_FIXED;
        out->ret = void_type;
        names = param_names;
    } else if (item->tag == CLASS_ITEM_METHOD || item->tag == CLASS_ITEM_FIELD) {
        bool field = item->tag == CLASS_ITEM_FIELD;
        bool auto_accessor = field && item->field.is_auto_accessor;
        Ty const* value = class_item_get_type(item);
        Ty const* this_type = member_this_type(me, class_decl, item);
        out->head_code = field ? 1240 : 1241;
        if (legacy) {
            out->params[0] = this_type;
            out->params[1] = member_key_type(me, item);
            Ty const* descriptor = global_generic(me, "TypedPropertyDescriptor", &value, 1, errs);
            if (!descriptor) descriptor = ty_intrinsic(me, TY_ANY);
            bool has_descriptor = !field || auto_accessor;
            if (has_descriptor) out->params[2] = descriptor;
            out->count = has_descriptor ? 3 : 2;
            out->arity = field ? DECORATOR_ARITY_FIXED : DECORATOR_ARITY_LEGACY_METHOD;
            out->ret = field ? void_type : union2(me, descriptor, void_type);
            out->void_only = field;
        } else {
            Ty const* pair[2] = { this_type, value };
            Ty const* target;
            Ty const* ret;
            char const* context_name;
            if (field) {
                context_name = auto_accessor ? "ClassAccessorDecoratorContext" : "ClassFieldDecoratorContext";
                target = auto_accessor ? global_generic(me, "ClassAccessorDecoratorTarget", pair, 2, errs) : ty_intrinsic(me, TY_UNDEFINED);
                if (auto_accessor) {
                    ret = global_generic(me, "ClassAccessorDecoratorResult", pair, 2, errs);
                } else {
                    // (this: This, value: Value) => Value
                    Signature* mutator = decorator_signature(me, &value, (char const*[]){ "value" }, 1, value);
                    mutator->this_type = this_type;
                    ret = ty_signature_type(me, mutator);
                }
            } else if (item->method.accessor == CLASS_ACCESSOR_GET) {
                context_name = "ClassGetterDecoratorContext";
                target = ret = ty_signature_type(me, decorator_signature(me, nullptr, nullptr, 0, value));
            } else if (item->method.accessor == CLASS_ACCESSOR_SET) {
                context_name = "ClassSetterDecoratorContext";
                target = ret = ty_signature_type(me, decorator_signature(me, &value, (char const*[]){ "value" }, 1, void_type));
            } else {
                context_name = "ClassMethodDecoratorContext";
                target = ret = value;
            }
            Ty const* context = global_generic(me, context_name, pair, 2, errs);
            if (!context || !target || !ret) return false;
            // & { name: "x"; private: boolean; static: boolean } (tsc's getClassMemberDecoratorContextOverrideType)
            bool is_private = span_valid(class_item_private_name(item));
            cstr const* name_string = field ? &item->field.name_string : &item->method.name_string;
            Ty const* name_type = is_private ? ty_string_literal(me, cstr_sv(name_string)) : member_key_type(me, item);
            ObjectLiteralProp props[3] = {
                { .name = checker_atom(me, c_sv("name")), .type = name_type },
                { .name = checker_atom(me, c_sv("private")), .type = ty_intrinsic(me, is_private ? TY_TRUE : TY_FALSE) },
                { .name = checker_atom(me, c_sv("static")), .type = ty_intrinsic(me, class_item_is_static(item) ? TY_TRUE : TY_FALSE) },
            };
            Ty const* override = ty_object_literal(me, props, 3, nullptr, 0, OBJECT_SYNTHETIC);
            Ty const* both[2] = { context, override };
            out->params[0] = target;
            out->params[1] = ty_intersection(me, both, 2, nullptr, (TyList){0});
            out->count = 2;
            out->arity = DECORATOR_ARITY_ES;
            out->ret = union2(me, ret, void_type);
            names = es_names;
        }
    } else {
        return false;
    }
    if (sig_out) *sig_out = decorator_signature(me, out->params, names, out->count, out->ret);
    return true;
}

// tsc's checkDecorator: resolveDecorator, then the return type against what the decorator may return
void ty_check_decorator(Checker* me, Expr const* decorator, DecoratorSite site, ErrorCtx* errs) {
    DecoratorCall dc;
    Signature* sig = nullptr;
    // (tsc looks up the lib types the signature needs once the callee's type is known not to be an error)
    ErrorCtx lib_errors = error_ctx_init();
    lib_errors.phase = errs->phase;
    bool has_call = decorator_call(me, site, &dc, &sig, &lib_errors);
    Ty const* callee = ty_check_expr(me, decorator, has_call ? ty_signature_type(me, sig) : nullptr);
    Ty const* apparent = ty_apparent_type(me, callee);
    if (!(apparent->flags & TYF_ANY) && apparent != ty_intrinsic(me, TY_ERROR)) {
        for_each (e, vec_error, &lib_errors.errors) error_ctx_append_error(errs, *e);
        lib_errors.errors.size = 0;
    }
    error_ctx_drop(&lib_errors);
    if (!has_call || (apparent->flags & TYF_ANY) || apparent == ty_intrinsic(me, TY_ERROR)) return;
    // tsc's isPotentiallyUncalledDecorator: `@f` where `@f()` was meant (not when parenthesized)
    if ((apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) && span_eq(decorator->outer_span, decorator->inner_span)) {
        Members const* members = ty_members(me, apparent);
        bool uncalled = members->call_count > 0;
        for (u32 i = 0; i < members->call_count && uncalled; i++) {
            Signature const* s = members->call[i];
            signature_resolve(me, s);
            uncalled = s->min_args == 0 && !ty_signature_has_rest(s) &&
                s->param_count < decorator_arg_count(dc.arity, dc.count, s->param_count);
        }
        if (uncalled) {
            error_ctx_error_d(errs, site.at, 1329, "'", decorator->inner_span.sv,
                "' accepts too few arguments to be used as a decorator here. Did you mean to call it first and write '@",
                decorator->inner_span.sv, "()'?");
            return;
        }
    }
    CallExpr call = { .callable = *decorator, .args = vec_expr_init() };
    CallInfo info = { .call = &call, .decorator = &dc, .call_span = site.at };
    ErrorCtx collected = error_ctx_init();
    collected.phase = errs->phase;
    Ty const* ret = resolve_call(me, callee, &info, site.at, &collected);
    // Whatever resolving the call reports goes under TS1238-1241's head: its elaboration nested one deeper, its related
    // information (the sub-errors with a span) still related
    static char const* const heads[] = {
        "Unable to resolve signature of class decorator when called as an expression.",
        "Unable to resolve signature of parameter decorator when called as an expression.",
        "Unable to resolve signature of property decorator when called as an expression.",
        "Unable to resolve signature of method decorator when called as an expression.",
    };
    for_each (e, vec_error, &collected.errors) {
        Error head = { .ts_code = dc.head_code, .phase = e->phase, .span = e->span, .message = cstr_from(heads[dc.head_code - 1238]) };
        Error line = { .message = e->message };
        VecError related = {0};
        for_each (sub, vec_error, &e->sub_errors) vec_error_push(span_valid(sub->span) ? &related : &line.sub_errors, *sub);
        vec_error_push(&head.sub_errors, line);
        for_each (r, vec_error, &related) vec_error_push(&head.sub_errors, *r);
        // (moved out: only the vectors' buffers are left to free)
        e->sub_errors.size = 0;
        vec_error_drop(&e->sub_errors);
        related.size = 0;
        vec_error_drop(&related);
        error_ctx_append_error(errs, head);
    }
    collected.errors.size = 0;
    error_ctx_drop(&collected);
    if ((ret->flags & TYF_ANY) || ty_is_related(me, ret, dc.ret, TY_RELATION_ASSIGNABLE)) return;
    cstr source_str, target_str;
    ty_report_pair_cstr(me, ret, dc.ret, &source_str, &target_str);
    cstr head = dc.void_only ?
        cstr_d("Decorator function return type is '", source_str, "' but is expected to be 'void' or 'any'.") :
        cstr_d("Decorator function return type '", source_str, "' is not assignable to type '", target_str, "'.");
    c_drop(cstr, &source_str, &target_str);
    ty_report_unassignable_with_head(me, ret, dc.ret, dc.void_only ? 1271 : 1270, head, decorator->outer_span, errs);
}
