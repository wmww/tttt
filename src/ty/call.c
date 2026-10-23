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
    // An element of a spread tuple: its label (tsc's SyntheticExpression.tupleNameSource), which a rest parameter's
    // tuple keeps. Null if none
    Atom const* tuple_name;
} Arg;

typedef struct Args {
    Arg* data; // `small` until there are more
    u32 len, cap;
    i32 spread_index; // first `spread` argument, -1 if none
    bool incomplete; // An incomplete tagged template: too few arguments are fine (tsc's callIsIncomplete)
    uint8_t decorator_arity; // DecoratorArity
    Arg small[6]; // (Most calls have few arguments: no allocation for them)
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
// (A ParamShape lives where it was made: `types` may point into it)
typedef struct ParamShape {
    Ty const** types; // `small` while they fit
    u32 count;
    u32 min;
    Ty const* rest_element; // `...rest: X[]`: X
    Ty const* rest_variadic; // a rest parameter that's neither array nor tuple (a type parameter, mostly)
    Ty const* small[8];
} ParamShape;

static void shape_drop(ParamShape* shape) {
    if (shape->types != shape->small) free(shape->types);
}

// Room for `cap` types, the `count` there kept
static void shape_reserve(ParamShape* shape, u32 cap) {
    if (cap <= c_arraylen(shape->small)) return;
    if (shape->types == shape->small) {
        shape->types = malloc(sizeof(Ty const*) * cap);
        panic_if(!shape->types, "out of memory");
        memcpy(shape->types, shape->small, sizeof(Ty const*) * shape->count);
    } else {
        shape->types = realloc(shape->types, sizeof(Ty const*) * cap);
        panic_if(!shape->types, "out of memory");
    }
}

static void shape_init(ParamShape* shape, u32 min) {
    shape->types = shape->small;
    shape->count = 0;
    shape->min = min;
    shape->rest_element = nullptr;
    shape->rest_variadic = nullptr;
}

typedef struct Candidate {
    Signature const* sig;
    TyMapper const* mapper; // inferred type arguments, null if the signature isn't generic
    TyList inferred_type_params; // what the returned function is generic in, see infer_type_arguments()
} Candidate;

// What call_return_type() keeps besides the chosen candidate
typedef struct CallOutcome {
    Candidate failure; // If nothing fit, the overload failure candidate (tsc's resolvedSignature then)
    u32 signatures; // How many signatures the call had to choose from (0: none, it didn't get that far)
    // The chosen signature's parameter types its arguments were checked with last (arena), param_count of them
    Ty const* const* param_types;
    u32 param_count;
} CallOutcome;

typedef struct Resolution {
    Candidate chosen; // sig is null if nothing fit
    Ty const* const* chosen_types; // Its parameter types as checked (arena, only with CallInfo.outcome_out)
    u32 chosen_count;
    Candidate failure; // If nothing fit, the candidate the call is taken to be anyway (tsc's getCandidateForOverloadFailure)
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
    // `super(...)` of a class whose base clause has no type arguments: the constructors are instantiated as tsc's
    // getInstantiatedConstructorsForTypeArguments does (super_filled_type_arguments()), not inferred
    bool super_fill;
    Candidate* chosen_out; // Where to leave the signature the call resolved to, may be null
    bool failure_out; // chosen_out gets the overload failure candidate if nothing fit
    struct CallOutcome* outcome_out; // May be null
    // The signatures before reorder_candidates(): tsc's getArgumentArityError reads the original list (whose first
    // signature needing the fewest arguments names the missing parameter). Null: the same order
    Signature* const* declared_order;
    Ty const* this_arg; // What `this` is in the call (the receiver of a method call, else void), see this_argument()
    Span this_span; // Where a `this` that doesn't fit is reported
    // `super.m()`: the receiver is inferred from but not checked against the `this` parameter (tsc's
    // getSignatureApplicabilityError skips isSuperProperty callees)
    bool this_unchecked;
    Span call_span; // The whole call expression
    // `new f()` on something with call signatures only, resolved as a call (tsc's resolveNewExpression)
    bool new_as_call;
    DecoratorCall const* decorator; // A decorator (tsc's resolveDecorator): its synthetic arguments
} CallInfo;

// (An Args lives where it was initialized: `data` may point into it)
static void args_init(Args* args) {
    // (Field by field: `small` isn't zeroed)
    args->data = args->small;
    args->len = 0;
    args->cap = c_arraylen(args->small);
    args->spread_index = -1;
    args->incomplete = false;
    args->decorator_arity = 0;
}

static void args_push(Args* args, Arg arg) {
    if (args->len == args->cap) {
        args->cap *= 2;
        if (args->data == args->small) {
            args->data = malloc(sizeof(Arg) * args->cap);
            panic_if(!args->data, "out of memory");
            memcpy(args->data, args->small, sizeof(args->small));
        } else {
            args->data = realloc(args->data, sizeof(Arg) * args->cap);
            panic_if(!args->data, "out of memory");
        }
    }
    args->data[args->len++] = arg;
}

static void args_drop(Args* args) {
    if (args->data != args->small) free(args->data);
}

static bool is_empty_tuple(Ty const* ty) {
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_TUPLE_TARGET;
}

// tsc's isGenericFunctionReturningFunction for some call signature of the callee of `expr`, a call without type arguments
bool ty_calls_generic_function_returning_function(Checker* me, Expr const* expr) {
    if (expr->tag != EXPR_CALL || expr->call->uses_new || expr->call->is_extends) return false;
    if (expr->call->callable.tag == EXPR_GENERIC_PARAMS) return false;
    // (An immediately invoked function's parameters are its arguments' first: asked here, before the call is checked,
    // its type would otherwise decide them unannotated for good, TS7006, ember's `assert(..., (function (a, b) {...})(...))`)
    ty_assign_iife_parameter_types(me, expr->call);
    Ty const* callee = ty_apparent_type(me, ty_expr_type(me, &expr->call->callable));
    if (!(callee->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false; // (getSignaturesOfType)
    Members const* members = ty_members(me, callee);
    for (u32 i = 0; i < members->call_count; i++) {
        signature_resolve(me, members->call[i]);
        if (!sig_res(me, members->call[i])->type_params.len) continue;
        Ty const* ret = signature_return_type(me, members->call[i]);
        if ((ret->flags & TYF_OBJECT) && ty_members(me, ret)->call_count) return true;
    }
    return false;
}

static void collect_args(Checker* me, CallInfo const* info, Args* out) {
    CallExpr const* call = info->call;
    args_init(out);
    if (info->decorator) {
        for (u32 i = 0; i < info->decorator->count; i++) {
            args_push(out, (Arg){ .expr = &call->callable, .type = info->decorator->params[i], .part_of_spread = true });
        }
        out->decorator_arity = info->decorator->arity;
        return;
    }
    if (info->tagged_template) {
        Symbol const* strings = program_get_global(my program, c_sv("TemplateStringsArray"), SYMBOL_TYPE);
        if (!strings) ty_report_missing_global_type(me, c_sv("TemplateStringsArray")); // (TS2318 without a lib)
        Ty const* type = strings ? ty_declared_type_of(me, strings) : ty_empty_object_type(me);
        args_push(out, (Arg){ .expr = info->tagged_template, .type = type, .part_of_spread = true });
        out->incomplete = info->tagged_template->template_->incomplete;
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
                    Atom const* name = shape->names ? shape->names[j] : nullptr;
                    if (shape->flags[j] & TUPLE_ELEMENT_REST) {
                        if (out->spread_index < 0) out->spread_index = (i32)out->len;
                        // A variadic element (`...T`, T generic) stays itself (tsc's getSpreadArgumentType)
                        Ty const* rest = elements.data[j];
                        bool variadic = !ty_is_array(me, rest) && !ty_is_tuple(rest) && !(rest->flags & TYF_ANY);
                        Ty const* element = variadic ? ty_iterated_type(me, ITERATION_SPREAD, rest, span_null, nullptr) :
                            ty_rest_element_type(me, rest);
                        args_push(out, (Arg){ .expr = i, .type = element, .spread = true, .tuple_name = name,
                            .variadic = variadic ? rest : nullptr });
                    } else {
                        args_push(out, (Arg){ .expr = i, .type = elements.data[j], .part_of_spread = true, .tuple_name = name });
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
                if (ty_is_related(me, operand, ty_array(me, ty_intrinsic(me, TY_ANY), true), TY_RELATION_ASSIGNABLE)) variadic = operand;
            } else if (operand->kind == TY_ANONYMOUS && (operand->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
                ty_is_related(me, operand, ty_array(me, ty_intrinsic(me, TY_ANY), true), TY_RELATION_ASSIGNABLE)
            ) {
                // (A homomorphic mapped type over an array type parameter too: `Foo<T>` with `T extends any[]`)
                variadic = operand;
            }
            if (out->spread_index < 0) out->spread_index = (i32)out->len;
            args_push(out, (Arg){ .expr = i, .type = element, .spread = true, .variadic = variadic });
        } else {
            bool context_sensitive = ty_expr_is_context_sensitive(i);
            args_push(out, (Arg){
                .expr = i,
                .context_sensitive = context_sensitive,
                .generic_call = !context_sensitive && ty_calls_generic_function_returning_function(me, i),
            });
        }
    }
}

static bool accepts_void(Ty const* ty) {
    if (ty->flags & TYF_VOID) return true;
    if (ty->kind != TY_UNION) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (ty->composite.types.data[i]->flags & TYF_VOID) return true;
    }
    return false;
}

static void param_shape(Checker* me, Signature const* sig, TyMapper const* mapper, ParamShape* out) {
    SigResolved const* sig_rs = signature_resolve(me, sig);
    shape_init(out, sig_rs->min_args);
#define shape (*out)
    u32 cap = sig_rs->param_count + 1;
    shape_reserve(out, cap);
    for (u32 i = 0; i < sig_rs->param_count; i++) {
        SigParam const* param = &sig_rs->params[i];
        Ty const* type = ty_instantiate(me, param->type, mapper);
        if (!param->rest) {
            shape.types[shape.count++] = type;
            continue;
        }
        if (is_empty_tuple(type)) {
            // No more parameters
        } else if (ty_is_tuple(type)) {
            TupleShape const* tuple = type->reference.target->declared.tuple;
            TyList elements = ty_reference_args(me, type);
            cap += elements.len;
            shape_reserve(out, cap);
            for (u32 j = 0; j < elements.len; j++) {
                if (tuple->flags[j] & TUPLE_ELEMENT_REST) {
                    // A trailing array is the element type of the arguments left; a variadic element (`[...M, P]`) or
                    // elements after the rest one make the rest of the tuple a rest type of its own (tsc's
                    // getEffectiveRestType slices the tuple after its fixed elements)
                    if (j + 1 == elements.len && (ty_is_array(me, elements.data[j]) || ty_is_tuple(elements.data[j]) ||
                        (elements.data[j]->flags & TYF_ANY))) {
                        shape.rest_element = ty_rest_element_type(me, elements.data[j]);
                    } else {
                        shape.rest_variadic = ty_slice_tuple(me, tuple, elements, j, 0);
                    }
                    break;
                }
                bool optional = (tuple->flags[j] & TUPLE_ELEMENT_OPTIONAL) != 0;
                shape.types[shape.count++] = optional ? ty_optional_param_type(me, elements.data[j]) : elements.data[j];
                // A required element makes the optional parameters before it required (tsc's getMinArgumentCount:
                // the parameters before the rest one, plus the tuple's minLength)
                if (!optional) shape.min = shape.count;
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
#undef shape
}

static void call_param_shape(Checker* me, CallInfo const* info, Signature const* sig, TyMapper const* mapper, ParamShape* out);

static bool shape_has_rest(ParamShape const* shape) {
    return shape->rest_element || shape->rest_variadic;
}

static Ty const* shape_type_at(ParamShape const* shape, u32 i) {
    return i < shape->count ? shape->types[i] : shape->rest_element;
}

static Ty const* return_mapped_context(Checker* me, Ty const* contextual);

// Checks argument i with its parameter's type as the contextual type
static void check_arg(Checker* me, ParamShape const* shape, Args* args, u32 i) {
    Arg* arg = &args->data[i];
    if (arg->spread || arg->part_of_spread) return;
    Ty const* contextual = shape_type_at(shape, i);
    // An argument for a rest parameter that is a union of tuples: the element types at its position (tsc's
    // getContextualTypeForArgumentAtIndex, an indexed access with AccessFlags.Contextual), or of arrays. Also when it is
    // generic in an outer type parameter, a signature that isn't generic itself: `(...h: S<[E]>[] | S<[E]>[][])`
    // (a signature's own type parameters are inferred by infer_rest_variadic())
    if (!contextual && i >= shape->count && shape->rest_variadic) {
        // (Elements after a variadic one count from the end: the arguments are its elements. After a spread argument
        // the position is just the index, tsc's argIndex - restIndex)
        u32 length = args->spread_index < 0 ? args->len - shape->count : UINT32_MAX;
        Ty const* rest = shape->rest_variadic;
        if (rest->kind != TY_KEYOF && (rest->flags & TYF_DEFERRED)) {
            // (A generic conditional or indexed access rest type: its element there stays deferred, `R[0]`, tsc's
            // getIndexedAccessType of a generic object type)
            Value index = value_number((double)(i - shape->count));
            contextual = ty_indexed(me, rest, ty_literal(me, &index));
        } else {
            contextual = ty_contextual_element_type_of(me, rest, i - shape->count, length);
        }
    }
    arg->type = ty_check_expr(me, arg->expr, contextual);
    // A literal its contextual type has room for is regular, so it isn't widened as a candidate (tsc's
    // checkExpressionWithContextualType, which asks instantiateContextualType: what the return type says helps type it,
    // `Promise.resolve('notReady')` returning into `Promise<Result>`): "event1" against `K | keyof T` infers K = "event1"
    if (contextual && (arg->type->flags & TYF_FRESH_LITERAL) &&
        ty_is_literal_of_contextual_type(me, arg->type, return_mapped_context(me, contextual))
    ) {
        arg->type = ty_regular(me, arg->type);
    }
}

// What a round without the context-sensitive arguments leaves out: them, and the generic calls that should see what
// they infer first
static bool waits_for_sensitive(Arg const* arg) {
    return arg->context_sensitive || arg->generic_call;
}

// `skip_sensitive`: leave out the context-sensitive arguments, which a contextual type would type for good (tsc's
// CheckMode.SkipContextSensitive, while it's not known yet which overload they'll be checked against)
static bool skipped_arg(Arg const* arg, bool skip_sensitive, bool skip_generic_functions) {
    return (skip_sensitive && waits_for_sensitive(arg)) || (skip_generic_functions && arg->generic_function);
}

// A literal with a generic function call in it, in a round that skips generic functions: related as the inference's
// first pass typed it, the calls silentNever (tsc's SkipGenericFunctions checks the literal, the calls returning
// resolvingSignature), not checked again. Its non-inferrable type left a naked type parameter at its constraint, which
// a literal with an excess property then doesn't fit
static bool first_pass_typed_arg(Arg const* arg, bool skip_sensitive, bool skip_generic_functions) {
    return skip_generic_functions && arg->nested_generic && !(skip_sensitive && waits_for_sensitive(arg));
}

// A context-sensitive function left out is still tsc's anyFunctionType, which fits whatever has signatures, but not a
// parameter whose type the skipping inference made `never` or a primitive (letrec's `T extends Record<string, unknown>
// ? LetrecTypedBuilder<T> : never` with T unknown so far isn't the overload), nor one with required properties
// (d3's `cross(a, b, (x, y) => ...)` isn't `cross(...iterables: Iterable<T>[])`). The type it doesn't fit, else null
static Ty const* skipped_function_unfit(Checker* me, ParamShape const* shape, Arg const* arg, u32 i, TyRelation relation) {
    if (!arg->context_sensitive || arg->expr->tag != EXPR_FUNCTION || arg->spread || arg->part_of_spread) return nullptr;
    Ty const* target = i < shape->count ? shape->types[i] : shape->rest_element;
    if (!target) return nullptr;
    return ty_is_related(me, ty_any_function_type(me), target, relation) ? nullptr : target;
}

static void check_args_from(
    Checker* me, ParamShape const* shape, Args* args, u32 from, bool skip_sensitive, bool skip_generic_functions
) {
    for (u32 i = from; i < args->len; i++) {
        if (skipped_arg(&args->data[i], skip_sensitive, skip_generic_functions)) continue;
        if (first_pass_typed_arg(&args->data[i], skip_sensitive, skip_generic_functions)) continue;
        check_arg(me, shape, args, i);
    }
}

static void check_args_skipping(Checker* me, ParamShape const* shape, Args* args, bool skip_sensitive, bool skip_generic_functions) {
    check_args_from(me, shape, args, 0, skip_sensitive, skip_generic_functions);
}

static void check_args(Checker* me, ParamShape const* shape, Args* args, bool skip_sensitive) {
    check_args_skipping(me, shape, args, skip_sensitive, false);
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

// tsc's getMutableArrayOrTupleType of a union of arrays and tuples: each member mutable. Null if it isn't one
// `single`: a lone member is one too
static Ty const* mutable_array_or_tuple_union(Checker* me, Ty const* ty, bool single) {
    if (ty->kind != TY_UNION && !single) return nullptr;
    TyList members = ty->kind == TY_UNION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    TyBuf parts = {0};
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        if (ty_is_tuple(member) && member->kind == TY_REFERENCE) {
            TupleShape shape = *member->reference.target->declared.tuple;
            shape.readonly = false;
            member = ty_reference(me, ty_tuple_target(me, &shape), ty_reference_args(me, member));
        } else if (member->kind == TY_DECLARED && member->declared.kind == TY_DECLARED_TUPLE_TARGET) {
            // (The empty tuple, `[] | [A]`, is its tuple target)
            TupleShape shape = *member->declared.tuple;
            shape.readonly = false;
            member = ty_tuple_target(me, &shape);
        } else if (ty_is_array(me, member)) {
            member = ty_array(me, ty_rest_element_type(me, member), false);
        } else {
            ty_buf_drop(&parts);
            return nullptr;
        }
        ty_buf_push(&parts, member);
    }
    Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

// The arguments from `from` on, as the tuple a variadic rest parameter receives
// `skip_sensitive`: a context-sensitive function not typed yet is anyFunctionType (tsc's SkipContextSensitive)
static Ty const* rest_arg_contextual(Checker* me, ParamShape const* shape, Args const* args, u32 i, u32 from);
static bool maybe_primitive_context(Ty const* ty);

// `context_shape`: an element is widened unless its contextual type in the rest parameter may be primitive (tsc's
// getSpreadArgumentType, as the applicability check sees the arguments)
// `rest_context`: the rest type a lone spread argument's operand is checked against (tsc's getSpreadArgumentType checks
// the last argument's spread expression with the rest type as its contextual type: `g(...(c ? [1] : []))` under
// `...args: [number] | []` spreads `[number] | []`, not `number[]`), with `self` the inference it's typed in
typedef struct OuterInference OuterInference;
static Ty const* rest_args_tuple_ctx(
    Checker* me, Args const* args, u32 from, bool widen, bool readonly, bool skip_sensitive, ParamShape const* context_shape,
    Ty const* rest_context, OuterInference* self
) {
    u32 arity = args->len > from ? args->len - from : 0;
    if (arity == 1 && rest_context && args->data[from].spread && !args->data[from].variadic &&
        args->data[from].expr->tag == EXPR_SPREAD
    ) {
        // (Not remembered: tsc's checkExpressionWithContextualType, the expression stays what getEffectiveCallArguments'
        // checkExpressionCached made it, `number[]` above)
        OuterInference* saved = my outer_inference;
        if (self) my outer_inference = self;
        TAINT_FLOW();
        my flow_uncached++;
        Ty const* operand = ty_check_expr(me, args->data[from].expr->spread, rest_context);
        TAINT_FLOW();
        my flow_uncached--;
        my outer_inference = saved;
        if (operand->flags & TYF_ANY) return operand;
        // (A tuple alone too: one a contextual type made, `...(c ? [1] : [2])` spreads `[number]`)
        Ty const* mutable = mutable_array_or_tuple_union(me, operand, ty_is_tuple(operand) || is_empty_tuple(operand));
        if (mutable) return mutable;
    }
    // Just a spread of something of type any: any, not `any[]` (tsc's getSpreadArgumentType for a last spread argument
    // takes an array-like type as it is), so `f(...x as any)` fits a rest parameter `L extends [A, ...A[]]`
    // (That very any: an unresolved `Iterable<number>` is an error type that prints by its name)
    if (arity == 1 && args->data[from].spread && !args->data[from].variadic && args->data[from].expr->tag == EXPR_SPREAD &&
        (ty_expr_type(me, args->data[from].expr->spread)->flags & TYF_ANY)
    ) {
        return ty_expr_type(me, args->data[from].expr->spread);
    }
    // Just a spread of a union of tuples (or arrays): that union, its members mutable (tsc's getSpreadArgumentType)
    if (arity == 1 && args->data[from].spread && !args->data[from].variadic && args->data[from].expr->tag == EXPR_SPREAD) {
        Ty const* mutable = mutable_array_or_tuple_union(me, ty_expr_type(me, args->data[from].expr->spread), false);
        if (mutable) return mutable;
    }
    // A lone spread of a generic array-like whose constraint is a mutable array or tuple: that type itself (tsc's
    // getSpreadArgumentType, getMutableArrayOrTupleType), `f10(...u)` with U extends string[] infers U
    if (arity == 1 && args->data[from].spread && args->data[from].variadic && !readonly && !args->data[from].tuple_name) {
        Ty const* variadic = args->data[from].variadic;
        Ty const* constraint = variadic->kind == TY_TYPE_PARAM || (variadic->flags & TYF_DEFERRED) ? ty_base_constraint(me, variadic) : variadic;
        // (Not a tuple constraint yet: the arity check then reads the instantiated rest parameter as V's fixed
        // elements and reports TS2556 for `f10(...v)`, V extends [number, number])
        if (constraint && ty_is_mutable_array_or_tuple(me, constraint)) return variadic;
    }
    unsigned char* flags = calloc(arity ? arity : 1, 1);
    Ty const** elements = calloc(arity ? arity : 1, sizeof(Ty const*));
    Atom const** names = nullptr;
    for (u32 i = 0; i < arity; i++) {
        Arg const* arg = &args->data[from + i];
        if (arg->tuple_name) {
            if (!names) names = calloc(arity, sizeof(Atom const*));
            names[i] = arg->tuple_name;
        }
        if (arg->spread) {
            flags[i] = TUPLE_ELEMENT_REST;
            elements[i] = arg->variadic ? arg->variadic : ty_array(me, arg->type, false);
        } else {
            // (A context-sensitive literal with its functions left out: typed without them, they would be typed for
            // good without a contextual type)
            Ty const* type = arg->type ? arg->type :
                skip_sensitive && arg->context_sensitive && arg->expr->tag == EXPR_FUNCTION ? ty_any_function_type(me) :
                skip_sensitive && arg->context_sensitive ? ty_check_expr_skipping_sensitive(me, arg->expr, nullptr, false) :
                // (A generic call left out of the inference is typed against its element of the instantiated rest type,
                // tsc's getSpreadArgumentType in the applicability check: `produce(v => v)` under `...rest: [K, S<X>] |
                // [K, "a", S<Y>]`)
                context_shape && arg->generic_call ? ty_check_expr(me, arg->expr, rest_arg_contextual(me, context_shape, args, from + i, from)) :
                ty_expr_type(me, arg->expr);
            bool widen_this = widen;
            if (!widen && context_shape && (type->flags & TYF_LITERAL)) {
                Ty const* contextual = rest_arg_contextual(me, context_shape, args, from + i, from);
                widen_this = !contextual || !maybe_primitive_context(contextual);
            }
            elements[i] = widen_this ? ty_widened_literal(me, type) : type;
        }
    }
    TupleShape tuple = { .arity = arity, .flags = flags, .readonly = readonly, .names = names };
    Ty const* target = ty_tuple_target(me, &tuple);
    Ty const* ret = arity ? ty_reference(me, target, (TyList){ .data = elements, .len = arity }) : target;
    free(flags);
    free(elements);
    free(names);
    return ret;
}

static Ty const* rest_args_tuple_skipping(Checker* me, Args const* args, u32 from, bool widen, bool readonly, bool skip_sensitive) {
    return rest_args_tuple_ctx(me, args, from, widen, readonly, skip_sensitive, nullptr, nullptr, nullptr);
}

static Ty const* rest_args_tuple(Checker* me, Args const* args, u32 from, bool widen, bool readonly) {
    return rest_args_tuple_skipping(me, args, from, widen, readonly, false);
}

// Type argument inference

static Signature const* last_call_signature(Checker* me, Ty const* ty) {
    Members const* members = ty_members(me, ty);
    return members->call_count ? members->call[members->call_count - 1] : nullptr;
}

// What a pattern contextual type is when tsc asks for it without binding patterns (every type parameter of the call
// has a default): nothing for a declaration's, an assignment target's as a plain type (an await operand's union of it
// and its PromiseLike too, the PromiseLike's argument left as it is)
static Ty const* assignment_pattern_context(Checker* me, Ty const* contextual) {
    if (contextual->kind != TY_UNION) return ty_is_assignment_pattern(me, contextual) ? ty_pattern_unclone(me, contextual) : nullptr;
    TyList members = contextual->composite.types;
    Ty const** parts = malloc(sizeof(Ty const*) * members.len);
    panic_if(!parts, "out of memory");
    bool assignment = false;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        bool pattern = (member->flags & TYF_BINDING_PATTERN) && ty_is_assignment_pattern(me, member);
        assignment |= pattern;
        parts[i] = pattern ? ty_pattern_unclone(me, member) : member;
    }
    Ty const* ret = assignment ? ty_union(me, parts, members.len, nullptr, (TyList){0}) : nullptr;
    free(parts);
    return ret;
}

static bool has_candidates_or_fixed(InferParam const* param) {
    return param->co_len || param->contra_len || param->fixed;
}

// A generic call inferring from its arguments, as the calls nested in them see it (tsc's getInferenceContext)
typedef struct OuterInference {
    Signature const* sig;
    Inference* inf;
    TyMapper const* return_mapper; // What its own contextual type said, see infer_type_arguments()
    // tsc's createOuterReturnMapper, made once: the first nested call that asks fixes what the later ones see (tsgo
    // caches it in context.outerReturnMapper, inferences made since don't show)
    TyMapper const* outer_return_mapper;
    bool fixing; // Contextual signatures assigned now fix the type parameters their parameters mention
    // The context-sensitive literal members checked since the last fixing, and their contextual types (tsc's
    // intraExpressionInferenceSites), pairs
    Ty const** sites;
    u32 site_len, site_cap;
    // (Cache of the non-fixing mapper, valid while the candidate count is `cached_count`)
    TyMapper const* cached_mapper;
    u64 cached_count;
    // (Likewise of outer_inference_mapper())
    TyMapper const* cached_no_default;
    u64 cached_no_default_count;
    u64 serial; // See Checker.active_inferences
    struct OuterInference* enclosing;
} OuterInference;

static Ty const* instantiate_instantiable(Checker* me, Ty const* type, TyMapper const* mapper);

// A contextual type instantiated with a call's returnMapper: without `boolean` in it (tsc's instantiateContextualType,
// so that `box(false)` against `Box<boolean>` is `Box<boolean>`, not `Box<false>`)
static Ty const* instantiate_with_return_mapper(Checker* me, Ty const* contextual, TyMapper const* return_mapper) {
    Ty const* type = instantiate_instantiable(me, contextual, return_mapper);
    if (type->kind != TY_UNION) return type;
    // (Both of true and false: `boolean | undefined` too, whose flags aren't boolean's)
    bool has_true = false, has_false = false;
    for (u32 i = 0; i < type->composite.types.len; i++) {
        has_true |= type->composite.types.data[i] == ty_intrinsic(me, TY_TRUE);
        has_false |= type->composite.types.data[i] == ty_intrinsic(me, TY_FALSE);
    }
    if (!has_true || !has_false) return type;
    TyBuf kept = {0};
    for (u32 i = 0; i < type->composite.types.len; i++) {
        Ty const* part = type->composite.types.data[i];
        if (part != ty_intrinsic(me, TY_TRUE) && part != ty_intrinsic(me, TY_FALSE)) ty_buf_push(&kept, part);
    }
    Ty const* ret = ty_union_filtered(me, type, kept.data, kept.len);
    ty_buf_drop(&kept);
    return ret;
}

// A contextual type as tsc's instantiateContextualType without ContextFlags.Signature sees it, for the call inferring now
static Ty const* return_mapped_context(Checker* me, Ty const* contextual) {
    OuterInference* outer = my outer_inference;
    return outer && outer->return_mapper ? instantiate_with_return_mapper(me, contextual, outer->return_mapper) : contextual;
}

// Changes whenever the non-fixing mapper would: candidates are only added (or replaced by ones of a better priority,
// which may leave their count as it was: `inf->replaced`), and fixing is for good
static u64 inference_count(Inference const* inf) {
    u64 candidates = 1, fixed = 0;
    for (u32 j = 0; j < inf->len; j++) {
        candidates += inf->params[j].co_len + inf->params[j].contra_len;
        fixed += inf->params[j].fixed != nullptr;
    }
    return candidates | fixed << 32 | (u64)inf->replaced << 48;
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
    // (An instantiation that is any or unknown says nothing: the contextual type stays, tsc 6.0)
    bool any = false;
    for (u32 j = 0; signature && j < outer->inf->len && !any; j++) {
        any = has_candidates_or_fixed(&outer->inf->params[j]) || ty_param_default(me, sig_res(me, outer->sig)->type_params.data[j]);
    }
    if (any) {
        Ty const* ret = instantiate_instantiable(me, contextual, non_fixing_mapper(me, outer));
        if (!(ret->flags & (TYF_ANY | TYF_UNKNOWN))) return ret;
    }
    if (outer->return_mapper) {
        Ty const* ret = instantiate_with_return_mapper(me, contextual, outer->return_mapper);
        if (!(ret->flags & (TYF_ANY | TYF_UNKNOWN))) return ret;
    }
    return contextual;
}

Ty const* ty_instantiate_contextual_type(Checker* me, Ty const* contextual) {
    return ty_instantiate_contextual_type_for(me, contextual, false);
}

// What the outer call has inferred so far, the rest being silent never (which nothing is inferred from), checked
// against the constraints: tsc's getMapperFromContext(cloneInferenceContext(outerContext, InferenceFlags.NoDefault))
static TyMapper const* outer_inference_mapper(Checker* me, OuterInference* outer) {
    u64 count = inference_count(outer->inf);
    if (outer->cached_no_default && outer->cached_no_default_count == count) return outer->cached_no_default;
    outer->cached_no_default = inference_mapper_no_default(me, outer->sig, outer->inf);
    outer->cached_no_default_count = count;
    return outer->cached_no_default;
}

// tsc's createOuterReturnMapper: what a call nested in an argument of this one instantiates its contextual type with
// before inferring its own return mapper: this call's return mapper, then what it had inferred when first asked
// (defaults, else unknown, for the rest: cloneInferenceContext(context, InferenceFlags.None).mapper)
static TyMapper const* outer_return_mapper(Checker* me, OuterInference* outer) {
    if (outer->outer_return_mapper) return outer->outer_return_mapper;
    TyMapper const* inferred = non_fixing_mapper(me, outer); // (Cached: what a fixing mapper of a clone gives)
    if (!outer->return_mapper) return outer->outer_return_mapper = inferred;
    TyList params = sig_res(me, outer->sig)->type_params;
    Ty const** targets = arena_new_n(&my arena, Ty const*, params.len ? params.len : 1);
    for (u32 i = 0; i < params.len; i++) {
        Ty const* returned = ty_instantiate(me, params.data[i], outer->return_mapper);
        targets[i] = returned != params.data[i] ? returned : ty_instantiate(me, params.data[i], inferred);
    }
    return outer->outer_return_mapper = ty_mapper_new(me, params, (TyList){ .data = targets, .len = params.len });
}

// What the contextual return type alone says about the type parameters (tsc's InferenceContext.returnMapper, from
// cloneInferredPartOfContext): the ones it says nothing about stay themselves. Null if it says nothing
// `fallback`, if set: for the type parameters `inf` has nothing on
static TyMapper const* return_inference_mapper(Checker* me, Signature const* sig, Inference* inf, Inference* fallback) {
    TyList params = sig_res(me, sig)->type_params;
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
// `target`: what the parameter has where the literal is, if known (null: anything), whose parts that mention no type
// variable infer nothing, so their literals can't be candidates (`c: [1]` against `c: unknown` next to `a: T`, whose
// number[] candidate isn't that literal)
static void add_nested_array_literals(Checker* me, Inference* inf, Expr const* expr, Ty const* type, Ty const* target, u32 depth) {
    if (depth > 4 || !type) return;
    if (target && !(target->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return;
    if (expr->tag == EXPR_ARRAY && !ty_is_tuple(type) && ty_is_array(me, type)) {
        // An array: its element type is (the union of) the elements' types, each nested literal is one of its members
        Ty const* element = ty_reference_args(me, type).data[0];
        TyList members = element->kind == TY_UNION ? element->composite.types : (TyList){ .data = &element, .len = 1 };
        // (Which member is whose isn't known: each member is looked into with every nested literal of its kind, an
        // element object literal by its property names: `[{ output: 1 }, { options: [...] }]`'s second member has the
        // array literal the first element doesn't)
        bool any_array = false;
        for_each (i, vec_expr, &expr->array) any_array |= i->tag == EXPR_ARRAY;
        for (u32 j = 0; j < members.len; j++) {
            Ty const* member = members.data[j];
            bool array_like = ty_is_tuple(member) || ty_is_array(me, member);
            if (array_like && any_array) add_array_literal(inf, member);
            u32 looked = 0;
            for_each (i, vec_expr, &expr->array) {
                if (i->tag != (array_like ? EXPR_ARRAY : EXPR_OBJECT) || looked >= 8) continue;
                add_nested_array_literals(me, inf, i, member, nullptr, depth + 1);
                looked++;
            }
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
            if (i->tag == EXPR_ARRAY || i->tag == EXPR_OBJECT) add_nested_array_literals(me, inf, i, element, nullptr, depth + 1);
        }
    } else if (expr->tag == EXPR_OBJECT) {
        for_each (i, vec_object_expr_item, &expr->object) {
            if (i->tag != OBJECT_ITEM_PROPERTY || (i->value.tag != EXPR_ARRAY && i->value.tag != EXPR_OBJECT) || i->computed_key) continue;
            Atom const* name = checker_atom(me, cstr_sv(&i->name_string));
            FoundProp prop = ty_find_prop(me, type, name);
            if (!prop.found) continue;
            // (The parameter's property of the name, when the parameter is a plain object type that has it)
            FoundProp target_prop = target && (target->flags & TYF_OBJECT) && !ty_is_generic_object(me, target) ?
                ty_find_prop(me, target, name) : (FoundProp){0};
            Ty const* nested_target = target_prop.found ? target_prop.type : nullptr;
            if (nested_target && !(nested_target->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) continue;
            if (i->value.tag == EXPR_ARRAY && (ty_is_tuple(prop.type) || ty_is_array(me, prop.type))) add_array_literal(inf, prop.type);
            add_nested_array_literals(me, inf, &i->value, prop.type, nested_target, depth + 1);
        }
    }
}

static void infer_from_arg(Checker* me, Inference* inf, ParamShape const* shape, Args const* args, u32 i) {
    Ty const* param_type = shape_type_at(shape, i);
    Arg const* arg = &args->data[i];
    bool literal = arg->type && !arg->spread && !arg->part_of_spread && arg->expr->tag == EXPR_ARRAY;
    if (literal && ty_is_array(me, arg->type)) add_array_literal(inf, arg->type);
    if (arg->type && !arg->spread && !arg->part_of_spread) add_nested_array_literals(me, inf, arg->expr, arg->type, param_type, 0);
    // (A tuple one is a literal candidate where it is inferred from as it is, see co_literals)
    inf->arg_tuple_literal = literal && ty_is_tuple(arg->type) ? arg->type : nullptr;
    if (param_type && args->data[i].type) infer_types(me, inf, args->data[i].type, param_type, INFER_PRIORITY_NORMAL);
    inf->arg_tuple_literal = nullptr;
}

// Whether assigning contextual signature `sig` to `function` asks for a type that mentions type parameter `param`: the
// types of its parameters without annotation and its `this` (tsc's assignContextualParameterTypes). (JSDoc types are
// no different: tsgo's reparsed `@template` is the function's type parameter, isTypeParameterPossiblyReferenced finds
// it like TypeScript's, unlike Strada's that counted every type parameter as referenced by a JSDoc node)
static bool assignment_mentions(Checker* me, FunctionImpl const* function, Signature const* sig, Ty const* param) {
    SigResolved const* sig_rs = signature_resolve(me, sig);
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
        if (type && ty_mentions_any(me, type, &param, 1)) return true;
    }
    return !this_declared && sig_rs->this_type && ty_mentions_any(me, sig_rs->this_type, &param, 1);
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

bool ty_outer_inference_active(Checker* me) {
    return my outer_inference != nullptr;
}

u64 ty_current_inference_serial(Checker* me) {
    return my outer_inference ? my outer_inference->serial : 0;
}

Ty const* ty_instantiate_fixing(Checker* me, Ty const* type, u64 serial) {
    OuterInference* outer = my outer_inference;
    // (The inference the literal's contextual type came from, when that's still going on: `this` in a call nested in
    // the method, `clone(this.x)`, is fixed by the outer call, tsc's getInferenceContext of the literal)
    for (OuterInference* active = serial ? my active_inferences : nullptr; active; active = active->enclosing) {
        if (active->serial == serial) {
            outer = active;
            break;
        }
    }
    if (!type || !outer || !outer->fixing || !(type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return type;
    Inference* inf = outer->inf;
    TyMapper const* so_far = nullptr;
    for (u32 j = 0; j < inf->len; j++) {
        if (!inf->params[j].fixed && ty_mentions_any(me, type, &sig_res(me, outer->sig)->type_params.data[j], 1)) {
            if (!so_far) so_far = fixing_mapper(me, outer);
            inf->params[j].fixed = so_far->targets[j];
        }
    }
    return ty_instantiate(me, type, inference_mapper(me, outer->sig, inf, true));
}

// A function gets its contextual signature while an argument of the call inferring now is checked: the type
// parameters that the types it asks for mention are fixed (tsc's fixing mapper), and it gets the signature instantiated
// with what they are fixed to
static void infer_from_annotated_params_of(Checker* me, Inference* inf, FunctionImpl const* function, Signature const* contextual);
Signature const* ty_fix_contextual_signature(Checker* me, Signature const* sig, FunctionImpl const* function) {
    OuterInference* outer = my outer_inference;
    if (!outer) return sig;
    if (!ty_function_is_context_sensitive(function)) {
        // A function that isn't context sensitive but has fewer parameters than its contextual signature says what its
        // annotations know too, on its first (inferential) context check only (tsc's
        // contextuallyCheckFunctionExpressionOrObjectLiteralMethod under ContextChecked; this runs once per function,
        // see assign_contextual_signature_as()): `testRest((t1: D) => {})` against `(t: T, t1: T, ...ts: T[]) =>
        // void` infers D covariantly; a function nested in a literal argument too, `{ fn: (a: string) => {} }`
        // against `{ fn: (a: A, b: B) => void }` infers A
        SigResolved const* sig_rs = signature_resolve(me, sig);
        if (generic_empty(&function->generic) && sig_rs->param_count > (u32)vec_function_arg_size(&function->args)) {
            // (Nothing to infer unless a parameter or the return type could mention a type parameter)
            bool could = signature_return_type(me, sig)->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
            for (u32 i = 0; i < sig_rs->param_count && !could; i++) could = sig_rs->params[i].type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
            if (could) infer_from_annotated_params_of(me, outer->inf, function, sig);
        }
        return sig;
    }
    if (!outer->fixing) return sig;
    SigResolved const* sig_rs = signature_resolve(me, sig);
    Inference* inf = outer->inf;
    // (A rest parameter of type parameter type fixes nothing: the signature is instantiated with the non-fixing mapper,
    // tsc's contextuallyCheckFunctionExpressionOrObjectLiteralMethod)
    if (sig_rs->param_count && sig_rs->params[sig_rs->param_count - 1].rest && sig_rs->params[sig_rs->param_count - 1].type->kind == TY_TYPE_PARAM) {
        Signature* instantiated = arena_new(&my arena, Signature);
        *instantiated = (Signature){ .flags = sig->flags, .target = sig, .mapper = inference_mapper(me, outer->sig, inf, true),
            .fresh_type_params = true };
        signature_resolve(me, instantiated);
        return instantiated;
    }
    // Its annotated parameters say something first, a function nested in a literal argument's too (tsc's
    // inferFromAnnotatedParameters in contextuallyCheckFunctionExpressionOrObjectLiteralMethod)
    infer_from_annotated_params_of(me, inf, function, sig);
    TyMapper const* so_far = nullptr;
    for (u32 j = 0; j < inf->len; j++) {
        if (!inf->params[j].fixed && assignment_mentions(me, function, sig, sig_res(me, outer->sig)->type_params.data[j])) {
            if (!so_far) so_far = fixing_mapper(me, outer);
            inf->params[j].fixed = so_far->targets[j];
        }
    }
    TyMapper const* mapper = inference_mapper(me, outer->sig, inf, true);
    Signature* fixed = arena_new(&my arena, Signature);
    *fixed = (Signature){ .flags = sig->flags, .target = sig, .mapper = mapper, .fresh_type_params = true };
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

// tsc's getSingleCallSignature(getNonNullableType(ty)) (or construct): only an object type has one, and only a union can
// become an object type without its null and undefined, the rest skips making `NonNullable<T>`
Signature const* ty_single_signature_non_nullable(Checker* me, Ty const* ty, bool construct, bool allow_members) {
    if (ty->kind == TY_UNION) ty = ty_non_nullable(me, ty);
    return ty_single_signature(me, ty, construct, allow_members);
}

// The generic signature of a generic function argument whose parameter wants a single non-generic signature of the
// same kind (the one in `*contextual`), else null
static Signature const* generic_function_argument(Checker* me, Ty const* arg_type, Ty const* param_type, Signature const** contextual) {
    if (!arg_type || !param_type) return nullptr;
    if (param_type->kind == TY_UNION) {
        // (Each member of a union by its apparent type, a mapped type's aside: tsc's getApparentTypeOfContextualType
        // with NoConstraints leaves alone only a bare type variable, so `f?: T` (`T | undefined`) is T's constraint's
        // signature where `f: T` has none)
        TyBuf members = {0};
        bool changed = false;
        for (u32 i = 0; i < param_type->composite.types.len; i++) {
            Ty const* member = param_type->composite.types.data[i];
            Ty const* apparent = member->kind == TY_ANONYMOUS && ty_members(me, member)->mapped ? member : ty_apparent_type(me, member);
            changed |= apparent != member;
            ty_buf_push(&members, apparent);
        }
        if (changed) param_type = ty_union(me, members.data, members.len, nullptr, (TyList){0});
        ty_buf_drop(&members);
    }
    for (u32 construct = 0; construct < 2; construct++) {
        Signature const* sig = ty_single_signature(me, arg_type, construct, true);
        if (!sig) continue;
        // (The contextual signature first: without one the argument's signature isn't resolved, its parameters aren't
        // typed. tsc reads only the declared type parameters of the argument's (signature.typeParameters); typing a
        // parameter here can enter a cycle tsc never sees: kebo's mobx-state-tree action returning `{ updateField }`
        // whose `value: Snapshot[K]` reaches the model's own type through Snapshot's base)
        Signature const* wanted = ty_single_signature_non_nullable(me, param_type, construct, false);
        if (!wanted) return nullptr;
        SigResolved const* sig_rs = signature_resolve(me, sig);
        if (!sig_rs->type_params.len) return nullptr;
        SigResolved const* wanted_res = signature_resolve(me, wanted);
        if (wanted_res->type_params.len) return nullptr;
        *contextual = wanted;
        return sig;
    }
    return nullptr;
}

// Whether the contextual signature of a generic function argument leaves the type parameters its parameters mention
// unfixed: a rest parameter of type parameter type (tsc's nonFixingMapper)
static bool contextual_rest_is_variadic(Checker* me, Signature const* contextual) {
    SigResolved const* contextual_r = sig_res(me, contextual);
    return contextual_r->param_count && contextual_r->params[contextual_r->param_count - 1].rest &&
        contextual_r->params[contextual_r->param_count - 1].type->kind == TY_TYPE_PARAM;
}

// A generic function argument instantiated in the context of signature `contextual` fixes the call's type parameters
// that `contextual`'s parameters (and `this`) mention: tsc's instantiateSignatureInContextOf instantiates it with the
// fixing mapper, and applyToParameterTypes asks for every parameter type. (A rest parameter of type parameter type
// isn't fixed: tsc's nonFixingMapper.) Returns the mapper of what is inferred so far
static TyMapper const* fix_for_generic_function(Checker* me, Signature const* sig, Inference* inf, Signature const* contextual) {
    SigResolved const* contextual_res = signature_resolve(me, contextual);
    // (The literal's members before this one are inferred from first, see fixing_mapper())
    if (my outer_inference && my outer_inference->inf == inf) fixing_mapper(me, my outer_inference);
    if (!contextual_rest_is_variadic(me, contextual)) {
        TyMapper const* so_far = inference_mapper(me, sig, inf, true);
        for (u32 j = 0; j < inf->len; j++) {
            if (inf->params[j].fixed) continue;
            Ty const* param = sig_res(me, sig)->type_params.data[j];
            bool mentioned = contextual_res->this_type && ty_mentions_any(me, contextual_res->this_type, &param, 1);
            for (u32 i = 0; i < contextual_res->param_count && !mentioned; i++) {
                mentioned = ty_mentions_any(me, contextual_res->params[i].type, &param, 1);
            }
            if (mentioned) inf->params[j].fixed = so_far->targets[j];
        }
    }
    return inference_mapper(me, sig, inf, true);
}

static bool returns_single_signature(Checker* me, Signature const* sig);

GenericFunctionMode ty_outer_generic_function_mode(Checker* me) {
    OuterInference* outer = my outer_inference;
    return (GenericFunctionMode){ .sig = outer ? outer->sig : nullptr, .inf = outer ? outer->inf : nullptr };
}

Ty const* ty_generic_function_in_context(Checker* me, Ty const* type, Ty const* contextual, GenericFunctionMode* mode) {
    Signature const* wanted = nullptr;
    Signature const* generic = generic_function_argument(me, type, contextual, &wanted);
    if (!generic) return type;
    if (!mode->sig) {
        mode->skipped = true;
        return ty_any_function_type(me);
    }
    // Higher order: if the call returns a function and not everything is inferred yet, the member's type parameters
    // can become the result's (`pipe4([list, box])` is `<T>(a: T) => { value: T[] }`, tsc's
    // instantiateTypeWithSingleGenericCallSignature)
    if (returns_single_signature(me, mode->sig)) {
        bool all_inferred = true;
        for (u32 j = 0; j < mode->inf->len; j++) all_inferred &= mode->inf->params[j].co_len || mode->inf->params[j].contra_len;
        Signature const* propagated = all_inferred ? nullptr : inference_propagate_generic(me, mode->inf, generic, wanted);
        if (propagated) return ty_signature_type(me, propagated);
    }
    TyMapper const* so_far = fix_for_generic_function(me, mode->sig, mode->inf, wanted);
    return ty_signature_type(me, ty_instantiate_signature_in_context_of(me, generic, wanted, so_far, false));
}

// tsc's couldContainTypeVariables at the top: an object type named by a non-generic type alias declared outside any
// function (isNonGenericTopLevelType) can't, whatever its members mention (`type F = (x: <T>(p: T) => T) => void`)
static bool could_contain_type_variables(Ty const* ty) {
    if (!(ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return false;
    if (!(ty->flags & TYF_OBJECT) || !ty->alias || ty->alias_args.len) return true;
    for (u32 i = 0; i < ty->alias->decl_count; i++) {
        if (ty->alias->decls[i].kind != SYMBOL_DECL_TYPE_ALIAS) continue;
        for (struct Scope const* scope = ty->alias->decls[i].block; scope; scope = scope->parent) {
            if (scope->kind == SCOPE_FUNCTION || scope->kind == SCOPE_STATIC_BLOCK) return true;
        }
        return false;
    }
    return true;
}

static Ty const* literal_arg_context(Checker* me, Ty const* param_type, TyMapper const* return_mapper);

// The second pass's inference from generic function argument `arg` (see Arg.generic_function), with `param_type` its
// parameter's type
static void infer_from_generic_function_arg(Checker* me, Signature const* sig, Inference* inf, Ty const* param_type, Arg const* arg,
    TyMapper const* return_mapper) {
    Signature const* contextual = nullptr;
    Signature const* generic = generic_function_argument(me, arg->type, literal_arg_context(me, param_type, return_mapper), &contextual);
    // Higher order: if the call returns a function and not everything is inferred yet, the argument's type
    // parameters can become the result's (`pipe(list, box)` is `<T>(a: T) => { value: T[] }`)
    if (returns_single_signature(me, sig)) {
        bool all_inferred = true;
        for (u32 j = 0; j < inf->len; j++) all_inferred &= inf->params[j].co_len || inf->params[j].contra_len;
        Signature const* propagated = all_inferred ? nullptr : inference_propagate_generic(me, inf, generic, contextual);
        if (propagated) {
            infer_types(me, inf, ty_signature_type(me, propagated), param_type, INFER_PRIORITY_NORMAL);
            return;
        }
    }
    TyMapper const* so_far = fix_for_generic_function(me, sig, inf, contextual);
    Signature const* instantiated = ty_instantiate_signature_in_context_of(me, generic, contextual, so_far, false);
    infer_types(me, inf, ty_signature_type(me, instantiated), param_type, INFER_PRIORITY_NORMAL);
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
    SigResolved const* ret_res = signature_resolve(me, ret);
    return !ret_res->type_params.len;
}

// A context-sensitive function argument's annotated parameters say something about the type parameters before the
// others are typed by what is inferred (tsc's inferFromAnnotatedParameters): `reduce(xs, (a: T | undefined, b) => ...)`
static void infer_from_annotated_params(Checker* me, Inference* inf, FunctionImpl const* function, Ty const* param_type) {
    Signature const* contextual = ty_function_contextual_signature(me, function, param_type);
    if (!contextual) return;
    infer_from_annotated_params_of(me, inf, function, contextual);
}

static void infer_from_annotated_params_of_inner(Checker* me, Inference* inf, FunctionImpl const* function, Signature const* contextual);
static void infer_from_annotated_params_of(Checker* me, Inference* inf, FunctionImpl const* function, Signature const* contextual) {
    FunctionImpl const* outer_function = my annotated_context_function;
    Signature const* outer_signature = my annotated_context_signature;
    my annotated_context_function = function;
    my annotated_context_signature = contextual;
    infer_from_annotated_params_of_inner(me, inf, function, contextual);
    my annotated_context_function = outer_function;
    my annotated_context_signature = outer_signature;
}

static void infer_from_annotated_params_of_inner(Checker* me, Inference* inf, FunctionImpl const* function, Signature const* contextual) {
    signature_resolve(me, contextual);
    for_each (i, vec_function_arg, &function->args) {
        if (i->rest) break;
        Binding const* binding = &i->initializer.binding;
        if (!binding->has_explicit_type) continue;
        Ty const* target = ty_signature_param_type_at(me, contextual, (u32)i_index);
        if (!target) break;
        Ty const* source = ty_from_node(me, binding_type(binding), (LowerCtx){ .generic = true });
        if (i->explicit_optional) source = ty_optional_param_type(me, source);
        infer_types(me, inf, source, target, INFER_PRIORITY_NORMAL);
    }
    // And its annotated return type (tsc 6's inferFromAnnotatedParametersAndReturn): `test((arg): number => 1)`
    TypeNode const* ret = function->ret_explicit ? &function->ret : nullptr;
    if (ret) infer_types(me, inf, ty_from_node(me, ret, (LowerCtx){ .generic = true }), signature_return_type(me, contextual), INFER_PRIORITY_NORMAL);
}

// A conditional expression with a generic call returning a function as a branch (`c ? share() : shareReplay(..)`),
// which the first pass skips like tsc's SkipGenericFunctions does wherever it is in the argument
static bool ternary_has_generic_call(Checker* me, Expr const* expr) {
    if (expr->tag != EXPR_TERNARY) return ty_calls_generic_function_returning_function(me, expr);
    return ternary_has_generic_call(me, &expr->ternary->yes) || ternary_has_generic_call(me, &expr->ternary->no);
}

// (A conditional only for its generic calls: checked like a literal otherwise, its type would differ, the literal
// branches of `Promise.resolve(c ? "open" : "close")` widened against the first pass's context)
static bool is_literal_arg(Checker* me, Arg const* arg) {
    if (arg->spread || arg->part_of_spread) return false;
    if (arg->expr->tag == EXPR_OBJECT || arg->expr->tag == EXPR_ARRAY || arg->expr->tag == EXPR_JSX_ATTRIBUTES) return true;
    return arg->expr->tag == EXPR_TERNARY && ternary_has_generic_call(me, arg->expr);
}

// The contextual type of a literal argument in the first pass: what the return type says about the type parameters
// helps type it (a tuple, a literal). An instantiation that is any or unknown says nothing, the parameter type stays
// (tsc's instantiateContextualType): `const a: Struct<any, any> = object({ x: defaulted(v, {}) })` with
// `object<S extends Record<string, Struct<any, any>>>(s: S): Struct<S, S>` types `x` by S's constraint
static Ty const* literal_arg_context(Checker* me, Ty const* param_type, TyMapper const* return_mapper) {
    if (!param_type || !return_mapper) return param_type;
    Ty const* ret = instantiate_instantiable(me, param_type, return_mapper);
    return ret->flags & (TYF_ANY | TYF_UNKNOWN) ? param_type : ret;
}

// A variadic rest parameter takes the remaining arguments as a tuple (tsc's getSpreadArgumentType at the end of
// inferTypeArguments)
// Checks argument i of those for a generic rest parameter, from `from` on (tsc's getSpreadArgumentType): its contextual
// type is the rest type's element at its position, and a literal that has room in it isn't widened as a candidate
// (`Literal("a")` under `...literals: Literals` with `Literals extends readonly LiteralValue[]` is `["a"]`)
static Ty const* rest_arg_contextual(Checker* me, ParamShape const* shape, Args const* args, u32 i, u32 from) {
    if (!ty_is_tuple(shape->rest_variadic)) {
        // A rest type that isn't a tuple gives `T[i]` (with a `const T`, each argument is in a const context), by
        // position even with spreads among the arguments (tsc's getSpreadArgumentType): `f11(1, ...u)` keeps 1
        Value index = value_number((double)(i - from));
        return ty_indexed(me, shape->rest_variadic, ty_literal(me, &index));
    }
    // A tuple's elements after a variadic one count from the end, spread arguments counting one each (tsc's
    // getContextualTypeForElementExpression without spread indices): `pipe(1, ...xs, (...v) => {})` against
    // `...args: [...T, (...v: T) => void]` types the function by the last element
    return ty_contextual_element_type_of(me, shape->rest_variadic, i - from, args->len - from);
}

// What the return type says about the type parameters helps type it, as for the other arguments (tsc's
// instantiateContextualType with the returnMapper): `spawn('child', { id: 'ok1' })` returning into a context that
// knows the actors keeps 'ok1' under `...args: SpawnArguments<TActor>`. Not for a call, whose own inference reads the
// contextual type as it is. And checked as the call infers (its outer inference), like the other arguments in
// infer_type_arguments(): a function's return type is computed then, returning into what the return mapper says
// (`M.tag("A", () => "a")` under `...pattern: [first: P, ...values: P[], f: Fn]` keeps "a" for `Ret`)
// tsc's maybeTypeOfKind(contextualType, Primitive | Index | TemplateLiteral | StringMapping) in getSpreadArgumentType:
// a literal argument stays a literal where the contextual type has a primitive member (`T[number] | string`)
static bool maybe_primitive_context(Ty const* ty) {
    TyList parts = ty->kind == TY_UNION || ty->kind == TY_INTERSECTION ? ty->composite.types : (TyList){ .data = &ty, .len = 1 };
    u32 primitive = TYF_STRING_LIKE | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL_LIKE | TYF_LITERAL | TYF_BOOLEAN | TYF_ENUM |
        TYF_ENUM_LITERAL | TYF_NULLABLE | TYF_VOID;
    for (u32 i = 0; i < parts.len; i++) {
        if ((parts.data[i]->flags & primitive) || parts.data[i]->kind == TY_KEYOF) return true;
    }
    return false;
}

static void check_rest_arg(Checker* me, ParamShape const* shape, Args* args, u32 i, u32 from, bool is_const, OuterInference* self) {
    Arg* arg = &args->data[i];
    Ty const* contextual = rest_arg_contextual(me, shape, args, i, from);
    OuterInference* saved = my outer_inference;
    my outer_inference = self;
    arg->type = ty_check_expr_in_const(me, arg->expr, contextual, is_const);
    if (self && arg->expr->tag == EXPR_FUNCTION && ty_function_assigned_contextual_signature(me, arg->expr->function) &&
        !ty_function_return_in_flight(me, arg->expr->function)
    ) {
        ty_argument_return_type(me, arg->expr->function);
    }
    my outer_inference = saved;
    // (Under a const type parameter always: tsc's hasPrimitiveContextualType is inConstContext || ..., so
    // `f('a', 'b')` with `const X extends string[]` infers X from the regular "a" and "b", which an unconstrained
    // inference from `f(...)[number]` later keeps rather than widening)
    if ((arg->type->flags & TYF_FRESH_LITERAL) && (is_const || (contextual &&
        (ty_is_literal_of_contextual_type(me, arg->type, contextual) || maybe_primitive_context(contextual))))
    ) {
        arg->type = ty_regular(me, arg->type);
    }
}

// The generic calls infer_rest_variadic() left out as silentNever are typed against the candidate chosen without them
static void unskip_generic_calls(Checker* me, Args* args, u32 from) {
    for (u32 i = from; i < args->len; i++) {
        if (args->data[i].generic_call && args->data[i].type == ty_intrinsic(me, TY_SILENT_NEVER)) args->data[i].type = nullptr;
    }
}

// `skip_sensitive`: the context-sensitive functions are left out (as anyFunctionType), not typed for good, and with
// `skip_generic_functions` the generic calls returning functions (as silentNever) too, as in infer_type_arguments()
static void infer_rest_variadic(
    Checker* me, Inference* inf, ParamShape const* shape, Args* args, OuterInference* self, bool skip_sensitive,
    bool skip_generic_functions
) {
    if (!shape->rest_variadic) return;
    u32 from = shape->count < args->len ? shape->count : args->len;
    bool is_const = ty_is_const_type_variable(me, shape->rest_variadic, 0);
    // Each inference types the other arguments again against the rest type (tsc's getSpreadArgumentType): a type left
    // by an earlier round's applicability check came from the instantiated rest type, whose `boolean` keeps a `true`
    // that the uninstantiated one widens (chillout's `forEach({ quux: true }, v => ...)` under a conditional rest)
    for (u32 i = from; i < args->len; i++) {
        if (!args->data[i].context_sensitive && !args->data[i].spread && !args->data[i].part_of_spread) args->data[i].type = nullptr;
    }
    // A context-sensitive function waits for what the other arguments infer, as anyFunctionType (tsc's
    // SkipContextSensitive pass), then is checked with its element of the rest type as its contextual type (tsc's
    // getSpreadArgumentType, an indexed access with AccessFlags.Contextual): `gen(this, function*() { this.x })` under
    // `...args: [self: Self, body: (this: Self) => ...] | [body: ...]`
    // A generic call returning a function waits likewise, as silentNever (tsc's resolveCallExpression under
    // SkipGenericFunctions), then sees what the others inferred: `catchTag("A", fn(function*(e) {...}))` under
    // `...args: [...tags: K, f: (e: Extract<E, { _tag: K[number] }>) => ...]` (tsc's inferences carry over from the
    // skipping inference to the full one, tttt's full inference defers it itself)
    bool sensitive = false;
    for (u32 i = from; i < args->len; i++) {
        Arg const* arg = &args->data[i];
        sensitive |= !arg->type && (arg->context_sensitive || arg->generic_call);
    }
    bool skip_calls = skip_sensitive || skip_generic_functions;
    if (sensitive) {
        for (u32 i = from; i < args->len; i++) {
            Arg* arg = &args->data[i];
            if (arg->type) continue;
            if (arg->generic_call) {
                if (skip_calls && ty_call_resolved_return(me, arg->expr->call)) {
                    check_rest_arg(me, shape, args, i, from, is_const, self);
                } else {
                    arg->type = ty_intrinsic(me, TY_SILENT_NEVER);
                }
            } else if (arg->context_sensitive && arg->expr->tag == EXPR_FUNCTION) {
                arg->type = ty_any_function_type(me);
            } else if (arg->context_sensitive) {
                // (A literal with such functions in it: they are left out the same way, the rest seeing what the call
                // has inferred so far, like the other arguments' first pass)
                OuterInference* saved = my outer_inference;
                my outer_inference = self;
                arg->type = ty_check_expr_skipping_sensitive(me, arg->expr, rest_arg_contextual(me, shape, args, i, from), is_const);
                my outer_inference = saved;
            } else {
                check_rest_arg(me, shape, args, i, from, is_const, self);
            }
        }
        infer_types(me, inf, rest_args_tuple_ctx(me, args, from, !is_const, is_const && !ty_some_mutable_array_like(me, shape->rest_variadic),
            false, nullptr, shape->rest_variadic, self),
            shape->rest_variadic, INFER_PRIORITY_NORMAL);
        for (u32 i = from; i < args->len; i++) {
            Arg* arg = &args->data[i];
            if (arg->generic_call && arg->type == ty_intrinsic(me, TY_SILENT_NEVER)) {
                if (!skip_calls) check_rest_arg(me, shape, args, i, from, is_const, self);
                continue;
            }
            if (!arg->context_sensitive) continue;
            if (skip_sensitive) {
                arg->type = nullptr; // (Typed against the candidate chosen without it)
                continue;
            }
            // (Its contextual type is instantiated where it is looked at, as in the second pass)
            my outer_inference = self;
            self->fixing = true;
            arg->type = ty_check_expr_in_const(me, arg->expr, rest_arg_contextual(me, shape, args, i, from), is_const);
            self->fixing = false;
            my outer_inference = nullptr;
        }
        if (skip_sensitive) {
            unskip_generic_calls(me, args, from);
            return;
        }
    }
    for (u32 i = from; i < args->len; i++) {
        if (!args->data[i].type) check_rest_arg(me, shape, args, i, from, is_const, self);
    }
    if (args->len > shape->count || args->spread_index < 0) {
        infer_types(me, inf, rest_args_tuple_ctx(me, args, from, !is_const, is_const && !ty_some_mutable_array_like(me, shape->rest_variadic),
            false, nullptr, shape->rest_variadic, self), shape->rest_variadic, INFER_PRIORITY_NORMAL);
    }
    if (sensitive && skip_calls) unskip_generic_calls(me, args, from);
}

// `*inferred_out`: the type parameters of generic function arguments that the return type is now generic in
// `skip_generic`: leave out generic function arguments and the generic calls among the arguments (an overload failure's
// fallback, tsc's inferSignatureInstantiationForOverloadFailure). `skip_generic_functions`: only the generic functions
// (tsc's CheckMode.SkipGenericFunctions of a candidate's first inference; not yet those nested in literals)
static TyMapper const* infer_type_arguments(
    Checker* me, Signature const* sig, Args* args, CallInfo const* info, bool skip_sensitive, bool skip_generic,
    bool skip_generic_functions, TyList* inferred_out
) {
    Ty const* contextual = info->contextual;
    Ty const* this_arg = info->this_arg;
    Inference inf;
    inference_init(&inf, sig_res(me, sig)->type_params);
    SourceFile const* file = info->call ? span_source(info->call->callable.inner_span) : span_source(info->call_span);
    inf.any_default = file && file->is_js;
    ParamShape shape;
    call_param_shape(me, info, sig, nullptr, &shape);

    // Nested in an argument of another generic call, the contextual type mentions its type parameters: what it has
    // inferred so far stands in for them
    OuterInference* outer = my outer_inference;
    my outer_inference = nullptr;
    Ty const* uninferred_contextual = contextual;
    if (contextual && outer && (contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        contextual = ty_instantiate(me, contextual, outer_inference_mapper(me, outer));
    }
    OuterInference self = { .sig = sig, .inf = &inf, .serial = ++my inference_serial, .enclosing = my active_inferences };
    my active_inferences = &self;
    my inferential_depth++;
    // (This call's own arguments aren't in the return of a function argument: see ExprCtx in expr.c)
    u32 saved_argument_return_depth = my argument_return_depth;
    my argument_return_depth = 0;
    // (tsc's impliedArity: a rest parameter that is a type parameter gets as many elements as there are arguments for
    // it, unless one is a spread)
    if (shape.rest_variadic && shape.rest_variadic->kind == TY_TYPE_PARAM && args->spread_index < 0) {
        for (u32 i = 0; i < inf.len; i++) {
            if (inf.params[i].param == shape.rest_variadic) {
                inf.params[i].implied_arity_plus_one = (args->len > shape.count ? args->len - shape.count : 0) + 1;
            }
        }
    }

    // A contextual type that is what a binding pattern implies (tsc's isFromBindingPattern) isn't one at all when every
    // type parameter has a default, else it only goes into the return mapper: `const [a, b] = f()` infers nothing
    bool from_pattern = contextual && (contextual->flags & TYF_BINDING_PATTERN);
    if (from_pattern) {
        bool all_defaults = true;
        for (u32 i = 0; i < sig_res(me, sig)->type_params.len && all_defaults; i++) all_defaults = ty_param_default(me, sig_res(me, sig)->type_params.data[i]) != nullptr;
        // (tsc's skipBindingPatterns: the contextual type is asked without binding patterns, which leaves an
        // assignment target's pattern a contextual type like any: `({ f } = await importActual("m"))` with
        // `importActual<T = ...>` infers T from `{ f }`)
        if (all_defaults) {
            contextual = assignment_pattern_context(me, contextual);
            from_pattern = false;
        }
    }
    TyMapper const* return_mapper = nullptr;
    if (contextual && !(contextual->flags & TYF_ANY) && from_pattern) {
        Ty const* return_type = signature_return_type(me, sig);
        if (return_type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
            Inference pattern_inf;
            inference_init(&pattern_inf, sig_res(me, sig)->type_params);
            infer_types(me, &pattern_inf, contextual, return_type, INFER_PRIORITY_NORMAL);
            return_mapper = return_inference_mapper(me, sig, &pattern_inf, nullptr);
            inference_drop(&pattern_inf);
            // HACK: the `any` of an element without default is tsc's nonInferrableAnyType, which infers nothing. And a
            // pattern with a rest element isn't a contextual type at all (see ty_rest_binding_initializer_type(): tsc types such an
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
    } else if (contextual) {
        Ty const* return_type = signature_return_type(me, sig);
        if (return_type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
            // A call at the end of an optional chain that may stop short returns undefined too, which the contextual
            // type's undefined then matches (tsc's getOptionalCallSignature: IsOuterCallChain's getOptionalType)
            if (info->call && !info->tagged_template && !info->jsx && !info->call->uses_new && !info->call->continues_chain) {
                bool optional = false;
                CallExpr const* call = info->call;
                if (call->optional_chain || (call->callable.tag == EXPR_PROPERTY && call->callable.property->continues_chain)) {
                    ty_optional_expression_type(me, ty_expr_type(me, &call->callable), call->optional_chain, &optional);
                }
                if (optional) return_type = ty_union(me, (Ty const*[]){ return_type, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
            }
            // A generic function type is instantiated with its own type parameters, which can then be inferred as
            // they are instead of being erased: `const f: <A>(a: A[]) => A[] = arrayMap(x => x)` gives `x: A`
            Signature const* contextual_sig = ty_single_signature(me, contextual, false, false);
            Ty const* source = contextual;
            if (contextual_sig) {
                SigResolved const* contextual_sig_res = signature_resolve(me, contextual_sig);
                if (contextual_sig_res->type_params.len) {
                    source = ty_signature_type(me, ty_signature_instantiation(me, contextual_sig, contextual_sig_res->type_params));
                }
            }
            infer_types(me, &inf, source, return_type, INFER_PRIORITY_RETURN_TYPE);
            // The mapper that instantiates contextual types of the arguments (tsc's InferenceContext.returnMapper): an
            // inference of its own, from the contextual type instantiated with the outer call's createOuterReturnMapper
            // (its return mapper, then everything it has inferred so far, defaults for the rest), so that in
            // `new Map(xs.map(x => [x, 1]))` `[K, V]` still makes the callback return a tuple, and in
            // `layerEffect(Foo, effectGen(() => ({ fn: ... })))` AEff is what Foo said S is
            // (The same source as the main inference just had, which nothing else has gone into yet: that one, a top-level
            // call's in particular)
            Ty const* return_source = !outer ? uninferred_contextual :
                ty_instantiate(me, uninferred_contextual, outer_return_mapper(me, outer));
            if (source == contextual && return_source == contextual) {
                return_mapper = return_inference_mapper(me, sig, &inf, nullptr);
            } else {
                Inference return_inf;
                inference_init(&return_inf, sig_res(me, sig)->type_params);
                infer_types(me, &return_inf, return_source, return_type, INFER_PRIORITY_NORMAL);
                return_mapper = return_inference_mapper(me, sig, &return_inf, nullptr);
                inference_drop(&return_inf);
            }
        }
    }
    self.return_mapper = return_mapper;

    if (sig_res(me, sig)->this_type && this_arg) infer_types(me, &inf, this_arg, sig_res(me, sig)->this_type, INFER_PRIORITY_NORMAL);

    // First pass: everything that isn't context sensitive
    bool any_sensitive = false;
    for (u32 i = 0; i < args->len; i++) {
        Arg* arg = &args->data[i];
        if (i >= shape.count && shape.rest_variadic) break;
        // An argument whose parameter can't mention a type parameter isn't checked to infer (tsc's inferTypeArguments
        // asks couldContainTypeVariables first): the applicability check types it, after the arguments the inference
        // checked, which decides union order (`propIf<E>(a: Falsy, value: any, alternate: E)` makes "alternate" before
        // "value")
        // (Only literals so far: whether any other argument is a generic function, which the applicability check's
        // rounds go by, is decided here)
        bool literal = arg->expr->tag == EXPR_LITERAL || arg->expr->tag == EXPR_TEMPLATE;
        Ty const* plain_param = literal && i < shape.count && !arg->spread && !arg->part_of_spread ? shape_type_at(&shape, i) : nullptr;
        if (plain_param && !(plain_param->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
            arg->type = nullptr;
            arg->nested_generic = false;
            arg->generic_function = false;
            continue;
        }
        if (arg->context_sensitive || arg->generic_call) {
            any_sensitive = true;
            // The rest of an object or array literal with a context-sensitive function in it is inferred from already
            // (tsc's CheckMode.SkipContextSensitive): `{ schema, onSubmit: (values) => {} }`, and so is the return
            // type of a function that is context sensitive only through what it returns (return_only_type() in expr.c)
            Ty const* param_type = shape_type_at(&shape, i);
            if (arg->context_sensitive && !arg->spread && !arg->part_of_spread && param_type) {
                my outer_inference = &self;
                // (The functions it has that aren't skipped get their return types from their bodies now, whose
                // context-sensitive functions fix what their contextual signatures mention: tsc's
                // contextuallyCheckFunctionExpressionOrObjectLiteralMethod with the inference context's mapper)
                self.fixing = true;
                Ty const* context = literal_arg_context(me, param_type, return_mapper);
                GenericFunctionMode mode = {0};
                Ty const* type = ty_check_literal_argument(me, arg->expr, context, ty_is_const_type_variable(me, param_type, 0),
                    true, &mode);
                arg->nested_generic = mode.skipped;
                self.fixing = false;
                my outer_inference = nullptr;
                infer_types(me, &inf, type, param_type, INFER_PRIORITY_NORMAL);
            }
            continue;
        }
        // (A context-sensitive function nested in it, `u && ((c, { w }) => ...)`, fixes what its contextual signature
        // mentions too: tsc assigns contextual parameter types with the fixing mapper under any inferential check)
        my outer_inference = &self;
        self.fixing = true;
        if (is_literal_arg(me, arg)) {
            GenericFunctionMode mode = {0};
            Ty const* param_type = shape_type_at(&shape, i);
            arg->type = ty_check_literal_argument(me, arg->expr, literal_arg_context(me, param_type, return_mapper),
                param_type && ty_is_const_type_variable(me, param_type, 0), false, &mode);
            arg->nested_generic = mode.skipped;
            any_sensitive |= mode.skipped;
        } else {
            check_arg(me, &shape, args, i);
        }
        // A function argument's return type is computed while the calls it returns still see this one (tsc computes it
        // as it checks the argument)
        // (Only with a contextual signature, like tsc's contextuallyCheckFunctionExpressionOrObjectLiteralMethod: without
        // one, it waits for the check against the instantiated parameter, whose context may be the function itself)
        if (arg->expr->tag == EXPR_FUNCTION && !arg->spread && !arg->part_of_spread &&
            ty_function_assigned_contextual_signature(me, arg->expr->function) &&
            // Not again while it's in flight further up (tsc's ContextChecked: once per node)
            !ty_function_return_in_flight(me, arg->expr->function)
        ) {
            ty_argument_return_type(me, arg->expr->function);
        }
        self.fixing = false;
        my outer_inference = nullptr;
        Signature const* contextual = nullptr;
        arg->generic_function = !arg->spread && !arg->part_of_spread &&
            generic_function_argument(me, arg->type, literal_arg_context(me, shape_type_at(&shape, i), return_mapper), &contextual);
        if (arg->generic_function) {
            any_sensitive = true;
            continue;
        }
        infer_from_arg(me, &inf, &shape, args, i);
    }
    // The remaining arguments are inferred from before the second pass too (tsc's inferTypeArguments does it at the end
    // of each pass), unless one of them is context sensitive itself: `evaluate(([t]) => ..., total)`
    bool rest_inferred = false;
    if (shape.rest_variadic && any_sensitive) {
        bool rest_sensitive = false;
        for (u32 i = shape.count; i < args->len; i++) rest_sensitive |= args->data[i].context_sensitive || args->data[i].generic_call;
        if (!rest_sensitive) {
            infer_rest_variadic(me, &inf, &shape, args, &self, skip_sensitive, skip_generic_functions);
            rest_inferred = true;
        }
    }
    // Second pass: each context-sensitive argument sees what is known by then, which is thereby decided. So does each
    // generic function argument, instantiated in the context of its parameter's signature (tsc's
    // instantiateSignatureInContextOf with the fixing mapper) and inferred from like that
    for (u32 i = 0; any_sensitive && i < args->len; i++) {
        Arg* arg = &args->data[i];
        if (i >= shape.count && shape.rest_variadic) break;
        if (arg->generic_function && !skip_generic && !skip_generic_functions) {
            infer_from_generic_function_arg(me, sig, &inf, shape_type_at(&shape, i), arg, return_mapper);
            continue;
        }
        if (arg->nested_generic && !arg->context_sensitive) {
            // (Under skip_generic_functions its first-pass type is what the applicability check relates)
            if (skip_generic || skip_generic_functions) continue;
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
            // (Skipped in a first inference unless it is resolved already, under another overload's full check: tsc's
            // resolveCallExpression skips under SkipGenericFunctions only when getResolvedSignature has nothing cached,
            // so `tap(acquireRelease(a, f), g)` infers from it before typing `g` for good)
            if (skip_generic || ((skip_sensitive || skip_generic_functions) && !ty_call_resolved_return(me, arg->expr->call))) continue;
            // Checked like in the first pass, but now the calls in it see what the other arguments inferred
            my outer_inference = &self;
            check_arg(me, &shape, args, i);
            my outer_inference = nullptr;
            // What it returns is a generic function like any other: instantiated in the context of the parameter's
            // signature (tsc's checkExpression instantiates every expression's single generic call signature),
            // `pipe(effect, provide(tag))`
            Signature const* contextual = nullptr;
            arg->generic_function = !arg->spread && !arg->part_of_spread &&
                generic_function_argument(me, arg->type, literal_arg_context(me, shape_type_at(&shape, i), return_mapper), &contextual);
            if (arg->generic_function) {
                infer_from_generic_function_arg(me, sig, &inf, shape_type_at(&shape, i), arg, return_mapper);
                continue;
            }
            infer_from_arg(me, &inf, &shape, args, i);
            continue;
        }
        if (!arg->context_sensitive || arg->generic_function || skip_sensitive) continue;
        Ty const* param_type = shape_type_at(&shape, i);
        // (Not one whose parameter can't mention a type parameter: tsc's inferTypeArguments asks
        // couldContainTypeVariables first, so a callback typed by a plain alias isn't checked, and its contextual
        // signature not decided, until the applicability check gets to it, if it does)
        if (param_type && !could_contain_type_variables(param_type)) continue;
        if (param_type && arg->expr->tag == EXPR_FUNCTION && !arg->spread && !arg->part_of_spread) {
            infer_from_annotated_params(me, &inf, arg->expr->function, param_type);
        }
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
        infer_from_arg(me, &inf, &shape, args, i);
    }
    if (!rest_inferred) infer_rest_variadic(me, &inf, &shape, args, &self, skip_sensitive, skip_generic_functions);
    shape_drop(&shape);
    free(self.sites);

    TyMapper const* mapper = inference_mapper(me, sig, &inf, false);
    *inferred_out = ty_list_copy(me, inf.inferred_type_params, inf.inferred_type_param_count);
    inference_drop(&inf);
    my outer_inference = outer;
    my active_inferences = self.enclosing;
    my inferential_depth--;
    my argument_return_depth = saved_argument_return_depth;
    return mapper;
}

// Applicability

// No argument for a rest parameter that isn't an array: it still gets `[]` (tsc's checkApplicableSignature with
// getNonArrayRestType), reported at the call (the index past the arguments)
static i32 unfit_missing_rest(
    Checker* me, ParamShape const* shape, Args const* args, TyRelation relation, Ty const** arg_type, Ty const** param_type
) {
    if (shape->rest_variadic && args->len <= shape->count && args->spread_index < 0 && !args->incomplete) {
        Ty const* source = rest_args_tuple(me, args, args->len, false, false);
        if (!ty_is_related(me, source, shape->rest_variadic, relation)) {
            *arg_type = source;
            *param_type = shape->rest_variadic;
            return (i32)args->len;
        }
    }
    return -1;
}

// The first argument that doesn't fit, -1 if they all do. `*param_type` is what it should have fit, `*arg_type` what
// it is (a tuple of the remaining arguments for a variadic rest parameter)
static i32 first_unfit_argument_skipping(
    Checker* me, ParamShape const* shape, Args const* args, TyRelation relation, bool skip_sensitive,
    bool skip_generic_functions, Ty const** arg_type, Ty const** param_type
) {
    for (u32 i = 0; i < args->len; i++) {
        Arg const* arg = &args->data[i];
        // (A generic function argument skipped by the inference is tsc's anyFunctionType, which fits)
        if (skipped_arg(arg, skip_sensitive, skip_generic_functions)) {
            if (i >= shape->count && shape->rest_variadic) break;
            Ty const* unfit = skip_sensitive ? skipped_function_unfit(me, shape, arg, i, relation) : nullptr;
            if (unfit) {
                *arg_type = ty_any_function_type(me);
                *param_type = unfit;
                return (i32)i;
            }
            continue;
        }
        Ty const* source = arg->type ? arg->type : ty_expr_type(me, arg->expr);
        Ty const* target;
        if (i < shape->count) {
            target = shape->types[i];
        } else if (shape->rest_element) {
            target = shape->rest_element;
        } else if (shape->rest_variadic) {
            source = rest_args_tuple_ctx(me, args, i, false, false, skip_sensitive, shape, shape->rest_variadic, nullptr);
            target = shape->rest_variadic;
        } else {
            break;
        }
        bool fits = ty_is_related(me, source, target, relation);
        if (!fits) {
            *arg_type = source;
            *param_type = target;
            return (i32)i;
        }
        if (i >= shape->count && shape->rest_variadic) break;
    }
    return unfit_missing_rest(me, shape, args, relation, arg_type, param_type);
}

// check_args_skipping() and first_unfit_argument_skipping() at once, an argument checked just before it's related as
// tsc's getSignatureApplicabilityError does: the arguments after the first that doesn't fit aren't checked against
// this candidate (the next candidate or the error report checks them again), which saves checking them for every
// overload a call tries (twice with several: the subtype round, then the assignable one)
static i32 check_args_until_unfit(
    Checker* me, ParamShape const* shape, Args* args, TyRelation relation, bool skip_sensitive,
    bool skip_generic_functions, Ty const** arg_type, Ty const** param_type
) {
    u32 checked = 0;
    for (u32 i = 0; i < args->len; i++) {
        Arg const* arg = &args->data[i];
        // A context-sensitive argument for a variadic rest parameter is in the tuple the rest is related to, its
        // functions as anyFunctionType (tsc's getSpreadArgumentType under SkipContextSensitive): `[x => x.a]` where
        // `...args: [...selectors: I, combiner: C]` wants a selector doesn't fit, so that overload doesn't type `x`
        bool rest_tuple = false;
        if (skipped_arg(arg, skip_sensitive, skip_generic_functions) && i >= shape->count && shape->rest_variadic &&
            !shape->rest_element && skip_sensitive
        ) {
            rest_tuple = true;
            for (u32 j = i; j < args->len && rest_tuple; j++) {
                rest_tuple = !(skip_generic_functions && args->data[j].generic_function) && !args->data[j].spread &&
                    !args->data[j].part_of_spread;
            }
        }
        if (!rest_tuple && skipped_arg(arg, skip_sensitive, skip_generic_functions)) {
            if (i >= shape->count && shape->rest_variadic) break;
            Ty const* unfit = skip_sensitive ? skipped_function_unfit(me, shape, arg, i, relation) : nullptr;
            if (unfit) {
                *arg_type = ty_any_function_type(me);
                *param_type = unfit;
                return (i32)i;
            }
            continue;
        }
        bool variadic = i >= shape->count && !shape->rest_element && shape->rest_variadic;
        if (i >= shape->count && !shape->rest_element && !shape->rest_variadic) break;
        if (variadic) {
            check_args_from(me, shape, args, checked, skip_sensitive, skip_generic_functions);
            checked = args->len;
        } else {
            for (; checked <= i; checked++) {
                Arg const* a = &args->data[checked];
                if (skipped_arg(a, skip_sensitive, skip_generic_functions)) continue;
                if (first_pass_typed_arg(a, skip_sensitive, skip_generic_functions)) continue;
                check_arg(me, shape, args, checked);
            }
        }
        Ty const* source;
        Ty const* target;
        if (!variadic) {
            source = arg->type ? arg->type : ty_expr_type(me, arg->expr);
            target = i < shape->count ? shape->types[i] : shape->rest_element;
        } else {
            source = rest_args_tuple_ctx(me, args, i, false, false, skip_sensitive, shape, shape->rest_variadic, nullptr);
            target = shape->rest_variadic;
        }
        if (!ty_is_related(me, source, target, relation)) {
            *arg_type = source;
            *param_type = target;
            return (i32)i;
        }
        if (variadic) break;
    }
    check_args_from(me, shape, args, checked, skip_sensitive, skip_generic_functions);
    return unfit_missing_rest(me, shape, args, relation, arg_type, param_type);
}

// tsc's CheckMode.SkipContextSensitive checks a context-sensitive object or array literal argument (or JSX attributes) too, its
// context-sensitive functions as anyFunctionType: `{ start(c) {} }` doesn't fit a parameter that requires `type`, so
// that overload isn't the one whose contextual types the functions get. Whether they fit (else -1 as in
// first_unfit_argument()). As a regular object literal: what isn't inferred yet makes no excess properties (tsc's
// getSignatureApplicabilityError). Checking it under this candidate's parameter leaves nothing behind that matters:
// a literal's memo goes by its contextual type, the functions left out aren't typed, and neither are the generic calls
static i32 first_unfit_skipped_literal(Checker* me, ParamShape const* shape, Args const* args, TyRelation relation) {
    for (u32 i = 0; i < args->len; i++) {
        Arg const* arg = &args->data[i];
        if (!arg->context_sensitive || arg->spread || arg->part_of_spread) continue;
        if (arg->expr->tag != EXPR_OBJECT && arg->expr->tag != EXPR_ARRAY && arg->expr->tag != EXPR_JSX_ATTRIBUTES) continue;
        Ty const* target = i < shape->count ? shape->types[i] : shape->rest_element;
        if (!target) continue;
        // (the inference left them out, so do we: tsc's SkipGenericFunctions; resolved here they'd be remembered with this
        // contextual type)
        GenericFunctionMode mode = {0};
        Ty const* checked = arg->nested_generic ? ty_check_literal_argument(me, arg->expr, target, false, true, &mode) :
            ty_check_expr_skipping_sensitive(me, arg->expr, target, false);
        Ty const* source = ty_regular_object_literal(me, checked);
        if (!ty_is_related(me, source, target, relation)) return (i32)i;
    }
    return -1;
}

static i32 first_unfit_argument(
    Checker* me, ParamShape const* shape, Args const* args, TyRelation relation, bool skip_sensitive,
    Ty const** arg_type, Ty const** param_type
) {
    return first_unfit_argument_skipping(me, shape, args, relation, skip_sensitive, false, arg_type, param_type);
}

// The parameters a call's arguments are checked against. A JSX element has one argument, its attributes, which any
// signature takes (tsc's hasCorrectArity: a class's argumentless constructor too), as the props of the signature
static void call_param_shape(Checker* me, CallInfo const* info, Signature const* sig, TyMapper const* mapper, ParamShape* out) {
    if (!info->jsx) {
        param_shape(me, sig, mapper, out);
        return;
    }
    SigResolved const* sig_rs = signature_resolve(me, sig);
    shape_init(out, sig_rs->min_args ? 1 : 0);
    out->count = 1;
    out->types[0] = ty_jsx_signature_props(me, info->jsx, sig, mapper, info->jsx_class);
}

// What the errors about a call are reported at (tsc's getDiagnosticForCallNode): the name of a method, else the callee;
// the whole expression of `new` or a tagged template; a JSX element's tag name
static Span callee_span(CallInfo const* info) {
    if (info->jsx) return info->jsx->fragment ? info->jsx->opening_span : info->jsx->tag_name.inner_span;
    if ((info->call->uses_new || info->tagged_template) && span_valid(info->call_span)) return info->call_span;
    Expr const* callee = &info->call->callable;
    while (callee->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(callee)) callee = &callee->generic_params->expr;
    if (callee->tag == EXPR_PROPERTY && callee->property->form == PROPERTY_EXPR_DOT && !expr_is_perenthisized(callee)) {
        return property_name(callee->property);
    }
    return callee->inner_span;
}

// The callee of a `new` or `extends` without its type arguments (tsc's NewExpression.expression,
// ExpressionWithTypeArguments.expression)
static Span constructor_span(Expr const* callee) {
    if (callee->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(callee)) callee = &callee->generic_params->expr;
    return expr_outer_span(callee);
}

// Where tsc's invocationError reports TS2349/TS2351: the callee, but a call's (not `new`'s) is at the method name like
// getDiagnosticSpanForCallNode's
static Span invocation_error_span(CallInfo const* info) {
    Expr const* callee = &info->call->callable;
    if (info->call->uses_new || info->call->is_extends) return constructor_span(callee);
    if (info->jsx || info->tagged_template || info->decorator) return expr_outer_span(callee);
    while (callee->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(callee)) callee = &callee->generic_params->expr;
    if (callee->tag == EXPR_PROPERTY && callee->property->form == PROPERTY_EXPR_DOT && !expr_is_perenthisized(callee)) {
        return property_name(callee->property);
    }
    // (type arguments belong to tsc's call node, not its expression)
    return expr_outer_span(callee);
}

// TS6234 rather than TS2349: `x.y()` without arguments where y is a get accessor (tsc's invocationErrorDetails, through
// the resolvedSymbol of the callee, which a parenthesized one doesn't have)
static bool callee_is_get_accessor(Checker* me, CallInfo const* info) {
    Expr const* callee = &info->call->callable;
    if (info->jsx || info->tagged_template || info->decorator || info->call->uses_new || vec_expr_size(&info->call->args)) {
        return false;
    }
    if (callee->tag != EXPR_PROPERTY || expr_is_perenthisized(callee)) return false;
    PropertyExpr const* access = callee->property;
    Atom const* name = ty_literal_name(me, ty_regular(me, ty_access_key_type(me, access)));
    if (!name) return false;
    Ty const* object = ty_apparent_type(me, ty_expr_type(me, &access->expr));
    Prop const* prop = ty_get_prop(me, object, name);
    while (prop && prop->target) prop = prop->target;
    return prop && (prop->flags & (PROP_CLASS_GETTER | PROP_GETTER));
}

static Error* not_callable_error(Checker* me, ErrorCtx* errs, CallInfo const* info) {
    if (callee_is_get_accessor(me, info)) {
        return error_ctx_error_d(errs, invocation_error_span(info), 6234,
            "This expression is not callable because it is a 'get' accessor. Did you mean to use it without '()'?");
    }
    return error_ctx_error_d(errs, invocation_error_span(info), 2349, "This expression is not callable.");
}


// Whether a line break comes after `span`, before anything but whitespace and comments (tsc's skipTrivia with
// stopAfterLineBreak, then isLineBreak of the character before)
static bool line_break_follows(Span span) {
    char const* c = span_end_ptr(span);
    char const* end = span_source(span) ? cstr_str(&span_source(span)->contents) + cstr_size(&span_source(span)->contents) : nullptr;
    while (c && c < end) {
        if (*c == '\n' || *c == '\r') return true;
        if (*c == ' ' || *c == '\t' || *c == '\v' || *c == '\f') {
            c++;
        } else if (c + 1 < end && c[0] == '/' && c[1] == '/') {
            while (c < end && *c != '\n' && *c != '\r') c++;
        } else if (c + 1 < end && c[0] == '/' && c[1] == '*') {
            c += 2;
            while (c + 1 < end && !(c[0] == '*' && c[1] == '/')) c++;
            c += 2;
        } else {
            return false;
        }
    }
    return false;
}

// tsc's invocationErrorDetails: "Did you forget to use 'await'?" at the callee when what it awaits to has signatures
static void suggest_await_for_invocation(Checker* me, Error* error, Ty const* apparent, bool construct, CallInfo const* info) {
    if (!error) return;
    Ty const* awaited = ty_awaited(me, apparent);
    if (awaited == apparent) return;
    // (A union's signatures: approximated by every member having some)
    TyList parts = awaited->kind == TY_UNION ? awaited->composite.types : (TyList){ .data = &awaited, .len = 1 };
    bool has = true;
    for (u32 i = 0; i < parts.len && has; i++) {
        Members const* members = ty_members(me, ty_apparent_type(me, parts.data[i]));
        has = construct ? members->construct_count : members->call_count;
    }
    if (has) {
        error_add_related_d(error, expr_outer_span(&info->call->callable), 2773, "Did you forget to use 'await'?");
    }
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

// What the call resolved to gives its return type outside the call's barrier (tsc's checkCallExpression asks
// getReturnTypeOfSignature after getResolvedSignature is done): a cycle through it is the declarations' around the call
static Ty const* resolved_return_type(Checker* me, Candidate const* candidate) {
    u32 start = my resolving_start;
    my resolving_start = ty_barrier_outer_call_start(me);
    Ty const* ret = candidate_return_type(me, candidate);
    my resolving_start = start;
    return ret;
}

static Span span_of_args(Args const* args, u32 from, u32 to) {
    return span_between(expr_outer_span(args->data[from].expr), expr_outer_span(args->data[to].expr));
}

// tsc's isPromiseResolveArityError: the callee is a parameter of a function written as an argument of `new Promise(...)`
// (the global Promise)
static bool is_promise_resolve_call(Checker* me, CallInfo const* info) {
    if (!info->call || info->call->uses_new || info->tagged_template || info->decorator) return false;
    Expr const* callee = &info->call->callable;
    if (callee->tag != EXPR_IDENTIFIER || !callee->identifier.target) return false;
    Symbol const* symbol = callee->identifier.target;
    if (!symbol->decl_count || symbol->decls[0].kind != SYMBOL_DECL_VARIABLE || !symbol->function) return false;
    Binding const* binding = symbol->decls[0].node;
    if (!binding->is_parameter || binding->parent) return false;
    Expr const* constructor = function_impl_rare(symbol->function)->new_callee;
    if (!constructor || !constructor->identifier.target) return false;
    Binder const* binder = program_get_binder(my program);
    Symbol const* promise = symbol_table_get(&binder->globals, c_sv("Promise"));
    return promise && binder_redirect(binder, promise) == binder_redirect(binder, constructor->identifier.target);
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
        ParamShape shape;
        call_param_shape(me, info, signatures[i], mapper, &shape);
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
        shape_drop(&shape);
    }
    Span name_span = callee_span(info);
    if (args->spread_index >= 0) {
        error_ctx_error_d(errs, expr_outer_span(args->data[args->spread_index].expr), 2556,
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
            span = args->len > max && !has_rest ? expr_outer_span(&info->call->callable) : info->call_span;
            error = error_ctx_error_d(errs, span, has_rest ? 1279 : 1278, "The runtime will invoke the decorator with ", args->len,
                " arguments, but the decorator expects ", has_rest ? "at least " : "", min);
            if (!has_rest && min != max) cstr_append_d(&error->message, DEFAULT_DISPLAY, "-", max);
            cstr_append_d(&error->message, DEFAULT_DISPLAY, ".");
        } else if (has_rest) {
            error = error_ctx_error_d(errs, span, 2555, "Expected at least ", min, " arguments, but got ", args->len, ".");
        } else if (min == max && min == 1 && args->len == 0 && is_promise_resolve_call(me, info) && span_source(span) && span_source(span)->is_js) {
            // (In JavaScript, where no type argument can be written, a message of its own without related info)
            error_ctx_error_d(errs, span, 2810, "Expected 1 argument, but got 0. 'new Promise()' needs a JSDoc hint to "
                "produce a 'resolve' that can be called without arguments.");
            return;
        } else if (min == max && min == 1 && args->len == 0 && is_promise_resolve_call(me, info)) {
            error = error_ctx_error_d(errs, span, 2794, "Expected ", min, " arguments, but got ", args->len,
                ". Did you forget to include 'void' in your type argument to 'Promise'?");
        } else if (min == max) {
            error = error_ctx_error_d(errs, span, 2554, "Expected ", min, " arguments, but got ", args->len, ".");
        } else {
            error = error_ctx_error_d(errs, span, 2554, "Expected ", min, "-", max, " arguments, but got ", args->len, ".");
        }
        // The first parameter without an argument of the signature that needs the fewest
        SigParam const* missing = args->len < min && closest && args->len < sig_res(me, closest)->param_count ?
            &sig_res(me, closest)->params[args->len] : nullptr;
        // (A combined signature's are synthesized: tsc looks at its declaration's parameters)
        Signature const* declared_by = missing && !missing->decl ? closest->declared_by : nullptr;
        if (declared_by && args->len < sig_res(me, declared_by)->param_count) missing = &sig_res(me, declared_by)->params[args->len];
        if (missing && missing->decl) {
            Binding const* binding = &missing->decl->initializer.binding;
            Span at = missing->decl->jsdoc_span ? *missing->decl->jsdoc_span : missing->decl->span;
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
    Ty const* this_type = sig_res(me, candidate->sig)->this_type;
    // (tsc's checkApplicableSignature: a declared `this: void` isn't checked, `ns.f()` calls it)
    if (!this_type || (this_type->flags & TYF_VOID) || !info->this_arg || (info->this_arg->flags & TYF_ANY) ||
        info->this_unchecked
    ) {
        return true;
    }
    return ty_is_related(me, info->this_arg, ty_instantiate(me, this_type, candidate->mapper), relation);
}

// Reports why the arguments don't fit the candidate. Returns the span it reported at
static Span report_unfit_argument(
    Checker* me, Candidate const* candidate, Args* args, CallInfo const* info, Span call_span, ErrorCtx* errs
) {
    if (!this_fits(me, candidate, info, TY_RELATION_ASSIGNABLE)) {
        Ty const* target = ty_instantiate(me, sig_res(me, candidate->sig)->this_type, candidate->mapper);
        ty_report_unfit_this(me, info->this_arg, target, info->this_span, errs);
        return info->this_span;
    }
    ParamShape shape;
    call_param_shape(me, info, candidate->sig, candidate->mapper, &shape);
    Ty const* arg_type = nullptr;
    Ty const* param_type = nullptr;
    // (While it reports, what the arguments' functions ask for their contextual types goes through this candidate:
    // tsc's checkExpressionWithContextualType pushes the instantiated parameter type, see derive_anchor_context())
    __typeof__(my reporting_candidate) outer_reporting = my reporting_candidate;
    if (info->call && !info->tagged_template) {
        ty_record_call_anchors(me, info->call);
        my reporting_candidate.call = info->call;
        my reporting_candidate.sig = candidate->sig;
        my reporting_candidate.mapper = candidate->mapper;
    }
    // (The arguments after the first that doesn't fit aren't checked against it: tsc's getSignatureApplicabilityError
    // returns there, so a callback after it is typed by what the failed call is taken to be instead)
    i32 index = check_args_until_unfit(me, &shape, args, TY_RELATION_ASSIGNABLE, false, false, &arg_type, &param_type);
    Span span = call_span;
    if (index >= 0 && info->jsx) {
        // The attributes, at the tag name (tsc's checkApplicableSignatureForJsxCallLikeElement)
        span = callee_span(info);
        // (A fragment's children aren't elaborated into: tsc has no attributes node to report them at)
        ty_check_assignable_expr(me, arg_type, param_type, span, info->jsx->fragment ? nullptr : &info->jsx->attributes, errs);
    } else if (index >= 0 && (u32)index == args->len) {
        ty_report_unfit_argument(me, arg_type, param_type, span, nullptr, errs);
    } else if (index >= 0) {
        Arg const* arg = &args->data[index];
        bool is_rest_tuple = arg_type != arg->type;
        Expr const* effective = expr_effective_check_node(arg->expr);
        // (tsc's getSignatureApplicabilityError: the effective check node, or a synthetic expression over the rest
        // arguments when there are several; getErrorSpanForNode then shortens functions to their name or first token)
        span = is_rest_tuple && (u32)index + 1 < args->len ? span_of_args(args, (u32)index, args->len - 1) :
            expr_error_span_skip_parens(effective);
        // (A tagged template's TemplateStringsArray is tsc's synthetic expression over the template, without the tag)
        bool template_strings = info->tagged_template && arg->expr == info->tagged_template;
        if (template_strings) span = info->tagged_template->template_->template_span;
        ty_report_unfit_argument(me, arg_type, param_type, span, is_rest_tuple || template_strings ? nullptr : effective, errs);
    }
    my reporting_candidate = outer_reporting;
    shape_drop(&shape);
    return span;
}

static Resolution resolve_with_signatures(
    Checker* me, Signature* const* signatures, u32 count, Args* args, CallInfo const* info, Span call_span,
    ErrorCtx* errs
);

// The call fits none of the overloads of a function, but would the implementation? (tsc's
// addImplementationSuccessElaboration.) Function declarations, class methods and constructors
static void add_implementation_success(Checker* me, Candidate const* candidate, Args* args, CallInfo const* info,
    Span call_span, Error* error
) {
    Signature const* failed = candidate->sig;
    while (sig_target(me, failed)) failed = sig_target(me, failed);
    if (!error || !failed->function) return;
    Symbol const* symbol = failed->function->symbol;
    FunctionImpl const* impl = function_impl_rare(failed->function)->overload_implementation;
    if (!impl && (!symbol || symbol->decl_count < 2)) return;
    for (u32 i = 0; symbol && i < symbol->decl_count && !impl; i++) {
        if (symbol->decls[i].kind != SYMBOL_DECL_FUNCTION) continue;
        FunctionImpl const* function = symbol->decls[i].node;
        if (function->has_body) impl = function;
    }
    if (!impl || impl == failed->function) return;
    // (A JSDoc `@overload` host's type is its overloads: its own signature)
    Signature const* sig = function_impl_rare(impl)->jsdoc_overload_count ? ty_implementation_signature(me, impl) :
        single_call_or_construct(me, ty_function_type(me, impl));
    if (!sig) return;
    Signature* impl_sig = (Signature*)sig;
    // (Its check may be the first full one of a context-sensitive function argument after the failing one, which then
    // has no contextual signature for good if the implementation gives none: tsc's chooseOverload([implementation])
    // sets ContextChecked, `(c: any, ...rest: unknown[])` makes `(md, opts) => ...` implicitly any, TS7006)
    bool fits = resolve_with_signatures(me, &impl_sig, 1, args, info, call_span, nullptr).chosen.sig;
    // The arguments stay typed as checked against the overload (what the types baseline shows)
    ParamShape shape;
    call_param_shape(me, info, candidate->sig, candidate->mapper, &shape);
    check_args(me, &shape, args, false);
    shape_drop(&shape);
    if (!fits) return;
    Span at = span_valid(impl->name) ? impl->name : span_valid(function_impl_member_name(impl)) ? function_impl_member_name(impl) : impl->full_span;
    error_add_related_s(error, at, 2793, cstr_lit(
        "The call would have succeeded against this implementation, but implementation signatures of overloads are not "
        "externally visible."));
}

// Where a signature is declared, as tsc's createDiagnosticForNode(signature.declaration) points at it
static Span signature_decl_span(Checker* me, Signature const* sig) {
    while (sig_target(me, sig)) sig = sig_target(me, sig);
    // (A union's signature is declared by the one it was made from: tsc's createUnionSignature clones it)
    while (!sig->function && sig->declared_by && sig->declared_by != sig) {
        sig = sig->declared_by;
        while (sig_target(me, sig)) sig = sig_target(me, sig);
    }
    // (A JSDoc `@overload`: its tag name, where tsgo's reparsed declaration is)
    if (sig->function && sig->function->jsdoc_callback && span_valid(sig->function->full_span) &&
        csview_starts_with(span_sv(sig->function->full_span), "@overload")
    ) {
        Span full = sig->function->full_span;
        return span_make_in(full, full.buf + 1, full.buf + full.size);
    }
    if (sig->function && !sig->function->is_type && span_valid(sig->function->name)) return sig->function->name;
    if (sig->function && span_valid(function_impl_member_name(sig->function))) return function_impl_member_name(sig->function); // A method signature
    if (span_valid(sig->name_span)) return sig->name_span;
    if (span_valid(sig->decl_span)) return sig->decl_span;
    // (A function type's signature: the type node, tsc's getErrorSpanForNode of the declaration)
    if (sig->function && sig->function->is_type && span_valid(sig->function->full_span)) return sig->function->full_span;
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

// tsgo's reportCallResolutionErrors: the last candidate's errors, each its own "No overload matches this call." chain
// when there were several candidates (TypeScript 6 listed every candidate's errors for two or three)
static void report_no_match(
    Checker* me, Candidate const* unfit, u32 unfit_count, Args* args, CallInfo const* info, Span call_span, ErrorCtx* errs
) {
    ErrorCtx collected = error_ctx_scratch(errs);
    Candidate const* last = &unfit[unfit_count - 1];
    report_unfit_argument(me, last, args, info, call_span, &collected);
    for_each (e, vec_error, &collected.errors) {
        Error error = *e;
        if (unfit_count > 1) {
            error = (Error){ .ts_code = 2769, .phase = e->phase, .span = e->span,
                .message = cstr_lit("No overload matches this call.") };
            error_sub_error_s(&error, span_null, TS_CODE_NONE, cstr_lit("The last overload gave the following error."));
            VecError related = {0};
            move_error_into_chain(e, vec_error_back_mut(&error.sub_errors), &related);
            for_each (r, vec_error, &related) vec_error_push(&error.sub_errors, *r);
            related.size = 0;
            vec_error_drop(&related);
            error_add_related_s(&error, signature_decl_span(me, last->sig), 2771, cstr_lit("The last overload is declared here."));
        }
        add_implementation_success(me, last, args, info, call_span, &error);
        error_ctx_append_error(errs, error);
    }
    collected.errors.size = 0;
    error_ctx_drop(&collected);
}

static u32 min_type_arguments(Checker* me, Signature const* sig) {
    SigResolved const* sig_r = sig_res(me, sig);
    u32 min = sig_r->type_params.len;
    while (min && ty_param_default(me, sig_r->type_params.data[min - 1])) min--;
    return min;
}

static bool has_correct_type_argument_arity(Checker* me, Signature const* sig, CallInfo const* info) {
    if (!info->type_arg_nodes) return true;
    return info->type_args.len >= min_type_arguments(me, sig) && info->type_args.len <= sig_res(me, sig)->type_params.len;
}

// The mapper explicit type arguments make, defaults filled in
static TyMapper const* explicit_type_arguments(Checker* me, Signature const* sig, CallInfo const* info) {
    TyList params = sig_res(me, sig)->type_params;
    if (!params.len) return nullptr;
    Ty const** results = arena_new_n(&my arena, Ty const*, params.len);
    // (A default that mentions a later type parameter gets the error type for it: tsc's fillMissingTypeArguments)
    for (u32 i = 0; i < params.len; i++) results[i] = i < info->type_args.len ? info->type_args.data[i] : ty_intrinsic(me, TY_ERROR);
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

// The signature a call none of several non-generic overloads fits is taken to have (tsc's
// createUnionOfSignaturesForOverloadFailure): each parameter the union of the overloads' at its position, a rest
// parameter the array of the union of their rest elements, `this` the union of theirs, all of their returns at once
// (`string & number` is never)
static Signature const* overload_failure_union_signature(Checker* me, Signature* const* signatures, u32 count) {
    u32 min_args = UINT32_MAX, max_params = 0;
    bool some_rest = false, some_this = false;
    for (u32 i = 0; i < count; i++) {
        bool rest = sig_res(me, signatures[i])->param_count && sig_res(me, signatures[i])->params[sig_res(me, signatures[i])->param_count - 1].rest;
        some_rest |= rest;
        some_this |= sig_res(me, signatures[i])->this_type != nullptr;
        u32 non_rest = sig_res(me, signatures[i])->param_count - (rest ? 1 : 0);
        if (non_rest > max_params) max_params = non_rest;
        if (sig_res(me, signatures[i])->min_args < min_args) min_args = sig_res(me, signatures[i])->min_args;
    }
    SigParam* params = arena_new_n(&my arena, SigParam, max_params + 1);
    Ty const** types = malloc(sizeof(Ty const*) * (count + 1));
    panic_if(!types, "out of memory");
    for (u32 p = 0; p < max_params; p++) {
        u32 len = 0;
        Atom const* name = nullptr;
        for (u32 i = 0; i < count; i++) {
            Ty const* type = ty_signature_param_type_at(me, signatures[i], p);
            if (!type) continue;
            if (!name) name = sig_res(me, signatures[i])->params[p < sig_res(me, signatures[i])->param_count ? p : sig_res(me, signatures[i])->param_count - 1].name;
            types[len++] = type;
        }
        params[p] = (SigParam){ .name = name, .type = ty_union_subtype_reduced(me, types, len), .optional = p >= min_args };
    }
    u32 param_count = max_params;
    if (some_rest) {
        u32 len = 0;
        Atom const* name = nullptr;
        for (u32 i = 0; i < count; i++) {
            Signature const* sig = signatures[i];
            if (!sig_res(me, sig)->param_count || !sig_res(me, sig)->params[sig_res(me, sig)->param_count - 1].rest) continue;
            if (!name) name = sig_res(me, sig)->params[sig_res(me, sig)->param_count - 1].name;
            types[len++] = ty_rest_element_type(me, sig_res(me, sig)->params[sig_res(me, sig)->param_count - 1].type);
        }
        params[param_count++] = (SigParam){
            .name = name, .type = ty_array(me, ty_union_subtype_reduced(me, types, len), false), .rest = true,
        };
    }
    Ty const* this_type = nullptr;
    if (some_this) {
        u32 len = 0;
        for (u32 i = 0; i < count; i++) {
            if (sig_res(me, signatures[i])->this_type) types[len++] = sig_res(me, signatures[i])->this_type;
        }
        this_type = ty_union(me, types, len, nullptr, (TyList){0});
    }
    Ty const** return_types = malloc(sizeof(Ty const*) * count);
    panic_if(!return_types, "out of memory");
    for (u32 i = 0; i < count; i++) return_types[i] = signature_return_type(me, signatures[i]);
    Ty const* ret = ty_intersection(me, return_types, count, nullptr, (TyList){0});
    free(return_types);
    free(types);
    Signature* sig = arena_new(&my arena, Signature);
    *sig = (Signature){
        .res = { (uintptr_t)sig_resolved_new(me, (SigResolved){
            .params = params,
            .param_count = param_count,
            .min_args = min_args,
            .this_type = this_type,
        }) },
        .ret = { SW_VAL(ret) },
        .predicate = { SW_NONE },
    };
    return sig;
}

// The type arguments a failed call is taken to have (tsc's getTypeArgumentsFromNodes in pickLongestCandidateSignature):
// unlike explicit_type_arguments(), a missing one is its default as written, not instantiated: `f<A>(bad)` of
// `f<T, F = G<T>>` returns its `F` as `G<T>`
static TyMapper const* failure_type_arguments(Checker* me, Signature const* sig, CallInfo const* info) {
    TyList params = sig_res(me, sig)->type_params;
    if (!params.len) return nullptr;
    Ty const** results = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 i = 0; i < params.len; i++) {
        if (i < info->type_args.len) {
            results[i] = info->type_args.data[i];
            continue;
        }
        Ty const* fallback = ty_param_default(me, params.data[i]);
        if (!fallback) fallback = ty_param_constraint(me, params.data[i]);
        results[i] = fallback ? fallback : ty_intrinsic(me, TY_UNKNOWN_);
    }
    return ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
}

static bool is_super_call(CallExpr const* call);

// The mapper of a `super(...)` signature whose base clause has no type arguments (CallInfo.super_fill, the signature
// takes none: resolve_call_inner() drops the others like getConstructorsForTypeArguments): tsc's
// fillMissingTypeArguments, each type parameter its default (any in JavaScript for a default of unknown or `{}`)
static TyMapper const* super_filled_type_arguments(Checker* me, Signature const* sig, CallInfo const* info) {
    TyList params = sig_res(me, sig)->type_params;
    if (!info->super_fill || !params.len) return nullptr;
    SourceFile const* file = span_source(info->call_span);
    bool js = file && file->is_js;
    Ty const** results = arena_new_n(&my arena, Ty const*, params.len);
    for (u32 i = 0; i < params.len; i++) results[i] = ty_intrinsic(me, TY_ERROR);
    TyMapper const* mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    for (u32 i = 0; i < params.len; i++) {
        Ty const* fallback = ty_param_default(me, params.data[i]);
        if (js && fallback && (ty_is_related(me, fallback, ty_intrinsic(me, TY_UNKNOWN_), TY_RELATION_IDENTITY) ||
            ty_is_related(me, fallback, ty_empty_object_type(me), TY_RELATION_IDENTITY))
        ) {
            fallback = ty_intrinsic(me, TY_ANY);
        }
        results[i] = fallback ? ty_instantiate(me, fallback, mapper) : ty_intrinsic(me, js ? TY_ANY : TY_UNKNOWN_);
        mapper = ty_mapper_new(me, params, (TyList){ .data = results, .len = params.len });
    }
    return mapper;
}

// tsc's getTypeArgumentArityError: one signature states its range; overloads state the closest counts around the
// given one (TS2743 when there are some on both sides)
static void report_type_argument_arity(
    Checker* me, Signature* const* signatures, u32 count, CallInfo const* info, ErrorCtx* errs
) {
    u32 arg_count = info->type_args.len;
    Span span = span_between(
        vec_spanned_type_at(info->type_arg_nodes, 0)->span, vec_spanned_type_back(info->type_arg_nodes)->span
    );
    if (count == 1) {
        signature_resolve(me, signatures[0]);
        u32 min = min_type_arguments(me, signatures[0]);
        u32 max = sig_res(me, signatures[0])->type_params.len;
        if (min < max) {
            error_ctx_error_d(errs, span, 2558, "Expected ", min, "-", max, " type arguments, but got ", arg_count, ".");
        } else {
            error_ctx_error_d(errs, span, 2558, "Expected ", min, " type arguments, but got ", arg_count, ".");
        }
        return;
    }
    i64 below = -1;
    i64 above = -1;
    for (u32 i = 0; i < count; i++) {
        signature_resolve(me, signatures[i]);
        u32 min = min_type_arguments(me, signatures[i]);
        u32 max = sig_res(me, signatures[i])->type_params.len;
        if (min > arg_count) {
            if (above < 0 || min < above) above = min;
        } else if (max < arg_count) {
            if (max > below) below = max;
        }
    }
    if (below >= 0 && above >= 0) {
        error_ctx_error_d(errs, span, 2743,
            "No overload expects ", arg_count, " type arguments, but overloads do exist that expect either ", below,
            " or ", above, " type arguments.");
    } else {
        error_ctx_error_d(errs, span, 2558, "Expected ", below < 0 ? above : below, " type arguments, but got ",
            arg_count, ".");
    }
}

// tsc's checkTypeArguments: whether explicit type arguments satisfy their constraints (instantiated with all of the
// arguments). With `errs`, TS2344 for the first one that doesn't
static bool check_type_argument_constraints(
    Checker* me, Signature const* sig, TyMapper const* mapper, CallInfo const* info, ErrorCtx* errs
) {
    SigResolved const* sig_r = sig_res(me, sig);
    for (u32 i = 0; i < info->type_args.len && i < sig_r->type_params.len; i++) {
        Ty const* constraint = ty_param_constraint(me, sig_r->type_params.data[i]);
        if (!constraint) continue;
        Ty const* arg = info->type_args.data[i];
        constraint = ty_with_this_arg(me, ty_instantiate(me, constraint, mapper), arg);
        if (ty_is_related(me, arg, constraint, TY_RELATION_ASSIGNABLE)) continue;
        if (errs) {
            cstr arg_str, constraint_str;
            ty_report_pair_cstr(me, arg, ty_report_target(me, arg, constraint), &arg_str, &constraint_str);
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
static Candidate make_candidate_skipping(
    Checker* me, Signature const* sig, Args* args, CallInfo const* info, bool skip_sensitive, bool skip_generic,
    bool skip_generic_functions
) {
    Candidate ret = { .sig = sig };
    if (!sig_res(me, sig)->type_params.len) return ret;
    ret.mapper = super_filled_type_arguments(me, sig, info);
    if (ret.mapper) return ret;
    ret.mapper = info->type_arg_nodes ? explicit_type_arguments(me, sig, info) :
        infer_type_arguments(me, sig, args, info, skip_sensitive, skip_generic, skip_generic_functions, &ret.inferred_type_params);
    return ret;
}

static Candidate make_candidate(
    Checker* me, Signature const* sig, Args* args, CallInfo const* info, bool skip_sensitive, bool skip_generic
) {
    return make_candidate_skipping(me, sig, args, info, skip_sensitive, skip_generic, false);
}

static Resolution resolve_with_signatures_inner(
    Checker* me, Signature* const* signatures, u32 count, Args* args, CallInfo const* info, Span call_span,
    ErrorCtx* errs
);

// A call's own resolution isn't inferential because an outer call's inference asked for it: only its own inference
// is (tsc's chooseOverload has argCheckMode of its own), so `outer(isSym(v) ? 1 : lit(v))` checks `v` against lit's
// parameter as its narrowed constraint (narrowable_type())
static Resolution resolve_with_signatures(
    Checker* me, Signature* const* signatures, u32 count, Args* args, CallInfo const* info, Span call_span,
    ErrorCtx* errs
) {
    u32 saved = my inferential_depth;
    CallExpr const* saved_call = my resolving_call;
    my inferential_depth = 0;
    my resolving_call = info->call;
    Resolution ret = resolve_with_signatures_inner(me, signatures, count, args, info, call_span, errs);
    my inferential_depth = saved;
    my resolving_call = saved_call;
    return ret;
}


static Resolution resolve_with_signatures_inner(
    Checker* me, Signature* const* signatures, u32 count, Args* args, CallInfo const* info, Span call_span,
    ErrorCtx* errs
) {
    Resolution ret = {0};
    // (Most calls have one or few signatures: no allocation for them)
    Candidate unfit_small[4];
    Candidate* unfit = unfit_small;
    if (count > c_arraylen(unfit_small)) {
        unfit = malloc(sizeof(Candidate) * count);
        panic_if(!unfit, "out of memory");
    }
    u32 unfit_count = 0;
    bool type_argument_arity_ok = false;
    Candidate arity_candidate = {0}; // tsc's candidateForArgumentArityError
    Signature const* type_arg_candidate = nullptr; // tsc's candidateForTypeArgumentError
    TyRelation rounds[2] = { TY_RELATION_SUBTYPE, TY_RELATION_ASSIGNABLE };
    // With a choice to make, candidates are first tried without their context-sensitive arguments, which are only
    // checked (and typed for good) against the first candidate the rest fit (tsc's argCheckMode)
    bool skip_sensitive = false;
    if (count == 1) signature_resolve(me, signatures[0]);
    if (count > 1 || (count == 1 && sig_res(me, signatures[0])->type_params.len)) {
        for (u32 i = 0; i < args->len; i++) skip_sensitive |= args->data[i].context_sensitive;
    }
    for (u32 round = count > 1 ? 0 : 1; round < 2 && !ret.chosen.sig; round++) {
        unfit_count = 0;
        arity_candidate = (Candidate){0};
        type_arg_candidate = nullptr;
        for (u32 i = 0; i < count && !ret.chosen.sig; i++) {
            Signature const* sig = signatures[i];
            SigResolved const* sig_rs = signature_resolve(me, sig);
            if (!has_correct_type_argument_arity(me, sig, info)) continue;
            type_argument_arity_ok = true;
            // (A `super(...)` of a base without type arguments checks the instantiated constructor)
            ParamShape uninstantiated;
            call_param_shape(me, info, sig, super_filled_type_arguments(me, sig, info), &uninstantiated);
            bool arity_ok = has_correct_arity(&uninstantiated, args);
            bool generic_rest = uninstantiated.rest_variadic != nullptr;
            shape_drop(&uninstantiated);
            if (!arity_ok) continue;
            // A candidate's first inference leaves generic function arguments out, and they fit its first check (tsc's
            // CheckMode.SkipGenericFunctions): a candidate whose other arguments don't fit is reported as that
            // inference has it, before a generic function argument's type parameters could flow into it
            Candidate candidate = make_candidate_skipping(me, sig, args, info, skip_sensitive, false, true);
            bool skipped_generic = false;
            if (sig_rs->type_params.len && !info->type_arg_nodes) {
                // (A call of a generic function that returns a function is left out too: tsc's resolveCallExpression
                // under SkipGenericFunctions)
                for (u32 a = 0; a < args->len; a++) {
                    skipped_generic |= args->data[a].generic_function || args->data[a].generic_call || args->data[a].nested_generic;
                }
            }
            // (A `super(...)`'s are the heritage clause's, checked there: tsc's getInstantiatedConstructorsForTypeArguments
            // instantiates without checking constraints)
            if (info->type_arg_nodes && candidate.mapper && !(info->call && is_super_call(info->call)) &&
                !check_type_argument_constraints(me, sig, candidate.mapper, info, nullptr)
            ) {
                type_arg_candidate = sig;
                continue;
            }
            if (generic_rest && candidate.mapper) {
                // A rest parameter of a generic type may have a different arity once instantiated (tsc's second
                // hasCorrectArity for getNonArrayRestType): `...m: T extends true ? [] : [X]`
                ParamShape instantiated;
                call_param_shape(me, info, sig, candidate.mapper, &instantiated);
                arity_ok = has_correct_arity(&instantiated, args);
                shape_drop(&instantiated);
                if (!arity_ok) {
                    arity_candidate = candidate;
                    continue;
                }
            }
            ParamShape shape;
            call_param_shape(me, info, sig, candidate.mapper, &shape);
            // (With a generic function left out, the generic calls among the arguments wait too: checking them now
            // would type their functions for good against what isn't inferred yet)
            Ty const* arg_type;
            Ty const* param_type;
            bool fits = check_args_until_unfit(me, &shape, args, rounds[round], skip_sensitive || skipped_generic,
                skipped_generic, &arg_type, &param_type) < 0;
            if (fits && skip_sensitive) {
                fits = first_unfit_skipped_literal(me, &shape, args, rounds[round]) < 0;
            }
            if (fits && (skip_sensitive || skipped_generic)) {
                // Again with everything, for this candidate and the ones after it
                skip_sensitive = false;
                shape_drop(&shape);
                candidate = make_candidate(me, sig, args, info, false, false);
                call_param_shape(me, info, sig, candidate.mapper, &shape);
                if (generic_rest && candidate.mapper && !has_correct_arity(&shape, args)) {
                    // (tsc's second hasCorrectArity after the full inference too)
                    shape_drop(&shape);
                    arity_candidate = candidate;
                    continue;
                }
                fits = check_args_until_unfit(me, &shape, args, rounds[round], false, false, &arg_type, &param_type) < 0;
            }
            fits = fits && this_fits(me, &candidate, info, rounds[round]);
            if (fits) {
                ret.chosen = candidate;
                if (info->outcome_out && shape.count) {
                    Ty const** types = arena_new_n(&my arena, Ty const*, shape.count);
                    memcpy(types, shape.types, sizeof(Ty const*) * shape.count);
                    ret.chosen_types = types;
                    ret.chosen_count = shape.count;
                }
            } else {
                unfit[unfit_count++] = candidate;
            }
            shape_drop(&shape);
        }
    }

    if (ret.chosen.sig) {
        ret.ret = resolved_return_type(me, &ret.chosen);
    } else {
        if (errs) {
            if (!type_argument_arity_ok) {
                // (A `super(...)`'s are the heritage clause's, which reports them: TS2314/TS2315. The call itself then
                // has no constructors for those type arguments: tsgo's resolveCall with no candidates says nothing)
                if (!(info->call && is_super_call(info->call))) report_type_argument_arity(me, signatures, count, info, errs);
            } else if (unfit_count) {
                report_no_match(me, unfit, unfit_count, args, info, call_span, errs);
            } else if (arity_candidate.sig) {
                Signature* sig = (Signature*)arity_candidate.sig;
                report_arity(me, &sig, 1, args, info, arity_candidate.mapper, errs);
            } else if (type_arg_candidate) {
                check_type_argument_constraints(me, type_arg_candidate,
                    explicit_type_arguments(me, type_arg_candidate, info), info, errs);
            } else {
                // (Of the signatures that take as many type arguments: tsc's signaturesWithCorrectTypeArgumentArity)
                Signature** fitting = malloc(sizeof(Signature*) * count);
                u32 fitting_count = 0;
                Signature* const* in_order = info->declared_order ? info->declared_order : signatures;
                for (u32 i = 0; i < count; i++) {
                    if (has_correct_type_argument_arity(me, in_order[i], info)) fitting[fitting_count++] = in_order[i];
                }
                report_arity(me, fitting, fitting_count, args, info, nullptr, errs);
                free(fitting);
            }
        }
        // What the call is taken to return anyway (tsc's getCandidateForOverloadFailure): of several non-generic
        // overloads, all of their returns at once (createUnionOfSignaturesForOverloadFailure: `string & number` is
        // never), else the candidate that got furthest, or every possibility
        bool some_generic = false;
        for (u32 i = 0; i < count; i++) {
            signature_resolve(me, signatures[i]);
            some_generic |= sig_res(me, signatures[i])->type_params.len > 0;
        }
        if (count > 1 && !some_generic) {
            // (The arguments are checked against it: a callback's parameters get the union of what each overload
            // gives them, which is no contextual signature unless they agree)
            Signature const* failure = overload_failure_union_signature(me, signatures, count);
            ParamShape shape;
            call_param_shape(me, info, failure, nullptr, &shape);
            check_args(me, &shape, args, false);
            shape_drop(&shape);
            ret.ret = signature_return_type(me, failure);
        } else if (unfit_count) {
            // Inferred again from what doesn't depend on the candidate: context-sensitive and generic function
            // arguments left out (tsc's inferSignatureInstantiationForOverloadFailure). Which one: tsc's
            // pickLongestCandidateSignature, the first overload with a rest parameter or enough parameters for the
            // arguments (getLongestCandidateIndex), when that one got as far as being checked
            u32 picked = unfit_count - 1;
            Signature const* longest = nullptr;
            for (u32 i = 0; i < count; i++) {
                if (!ty_signature_has_rest(me, signatures[i]) && ty_signature_param_count(me, signatures[i]) < args->len) continue;
                longest = signatures[i];
                for (u32 u = 0; u < unfit_count; u++) {
                    if (unfit[u].sig == signatures[i]) picked = u;
                }
                break;
            }
            // (A non-generic one that wasn't checked, its type argument count being wrong: `f<T>(...)` of `f(s:
            // string, n: number): number; f<T>(n: number, t: T): T` returns number)
            // (So is a generic one with explicit type arguments, which are cut or filled in for it: getTypeArgumentsFromNodes)
            bool longest_unchecked = longest && unfit[picked].sig != longest && (!sig_res(me, longest)->type_params.len || info->type_arg_nodes);
            Candidate fallback = longest_unchecked ? (Candidate){ .sig = longest } : unfit[picked];
            if (info->type_arg_nodes && sig_res(me, fallback.sig)->type_params.len &&
                (info->type_args.len < sig_res(me, fallback.sig)->type_params.len || longest_unchecked)
            ) {
                fallback.mapper = failure_type_arguments(me, fallback.sig, info);
                // (The arguments are checked against it again: an arrow's literal returns see its contextual type)
                ParamShape shape;
                call_param_shape(me, info, fallback.sig, fallback.mapper, &shape);
                check_args(me, &shape, args, false);
                shape_drop(&shape);
            }
            if (longest && sig_res(me, longest)->type_params.len && unfit[picked].sig != longest && !info->type_arg_nodes) {
                // A generic one that wasn't checked (too many parameters for the arguments: vitest's `spyOn(o, k,
                // accessor)` before `spyOn(o, k)`) is still the one, inferred from the arguments
                fallback = make_candidate(me, longest, args, info, true, true);
            } else if (sig_res(me, fallback.sig)->type_params.len && !info->type_arg_nodes) {
                fallback = make_candidate(me, fallback.sig, args, info, true, true);
                // (The arguments are left as checked against the candidate, which is what the types baseline shows)
                ParamShape shape;
                call_param_shape(me, info, unfit[picked].sig, unfit[picked].mapper, &shape);
                check_args(me, &shape, args, false);
                shape_drop(&shape);
            } else if (!sig_res(me, fallback.sig)->type_params.len && fallback.sig != unfit[unfit_count - 1].sig) {
                // (A non-generic one checked before the last candidate: the arguments are typed by it again, tsc's
                // getContextualTypeForArgument going by the resolved failure signature)
                ParamShape shape;
                call_param_shape(me, info, fallback.sig, nullptr, &shape);
                check_args(me, &shape, args, false);
                shape_drop(&shape);
            }
            // (What the error report left unchecked, after the argument that didn't fit, is typed by it)
            ParamShape shape;
            call_param_shape(me, info, fallback.sig, fallback.mapper, &shape);
            check_args(me, &shape, args, false);
            shape_drop(&shape);
            ret.ret = resolved_return_type(me, &fallback);
            ret.failure = fallback;
        } else if (count == 1 && !sig_res(me, signatures[0])->type_params.len) {
            // (The arguments are checked against it: an arrow's parameters get its types, tsc's
            // getContextualTypeForArgument going by the resolved failure signature)
            ParamShape shape;
            call_param_shape(me, info, signatures[0], nullptr, &shape);
            check_args(me, &shape, args, false);
            shape_drop(&shape);
            ret.ret = resolved_return_type(me, &(Candidate){ .sig = signatures[0] });
            ret.failure = (Candidate){ .sig = signatures[0] };
        } else {
            // No candidate was checked (wrong argument counts): tsc's pickLongestCandidateSignature, the first candidate
            // that takes the arguments (else the one with the most parameters), its type arguments cut or filled in
            // (getTypeArgumentsFromNodes), else inferred from what doesn't depend on the candidate
            // (inferSignatureInstantiationForOverloadFailure)
            u32 best = 0;
            u32 most = 0;
            for (u32 i = 0; i < count; i++) {
                u32 params = ty_signature_param_count(me, signatures[i]);
                if (ty_signature_has_rest(me, signatures[i]) || params >= args->len) {
                    best = i;
                    break;
                }
                if (i == 0 || params > most) most = params, best = i;
            }
            Signature const* sig = signatures[best];
            Candidate candidate = info->type_arg_nodes ? (Candidate){ .sig = sig, .mapper = explicit_type_arguments(me, sig, info) } :
                sig_res(me, sig)->type_params.len ? make_candidate(me, sig, args, info, true, true) : (Candidate){ .sig = sig };
            // (The arguments are checked against it: an arrow's parameters get its types, tsc's
            // getContextualTypeForArgument going by the resolved failure signature)
            ParamShape shape;
            call_param_shape(me, info, candidate.sig, candidate.mapper, &shape);
            check_args(me, &shape, args, false);
            shape_drop(&shape);
            ret.ret = info->type_arg_nodes ? candidate_return_type(me, &candidate) :
                sig_res(me, sig)->type_params.len ? resolved_return_type(me, &candidate) : signature_return_type(me, sig);
            ret.failure = candidate; // (What control flow goes by too: tsc's getEffectsSignature, a type predicate's)
        }
    }
    if (unfit != unfit_small) free(unfit);
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
    *span_out = expr_outer_span(&property->expr);
    return ty_optional_expression_type(me, ty_expr_type(me, &property->expr), property->optional_chain, nullptr);
}

// tsc's getSignaturesOfType arrayFallbackSignatures: a method of a union of arrays and tuples whose members have no
// signatures in common is that method of an array of all their elements (`(A[] | B[]).filter` is `(A | B)[]`'s,
// readonly if one of them is). Null if the callee isn't such a method
// One member of array_fallback_callee's receiver: its element type appended to `elements` (a member whose apparent
// type is a union, `Registry[K]` constrained to `A[] | B[]`, contributes each of its members: tsc goes by the union
// of the method's types, which is flat). False if it isn't an array, a tuple or an interface extending Array
static bool array_fallback_elements(Checker* me, Ty const* member, Atom const* method, TyBuf* elements,
    bool* readonly, u32 depth
) {
    Ty const* array = ty_apparent_type(me, member);
    if (array->kind == TY_UNION) {
        if (depth > 8) return false;
        TyList members = array->composite.types;
        for (u32 i = 0; i < members.len; i++) {
            if (!array_fallback_elements(me, members.data[i], method, elements, readonly, depth + 1)) return false;
        }
        return true;
    }
    if (ty_is_tuple(array)) array = ty_tuple_as_array(me, array);
    if (!ty_is_array(me, array) && (array->flags & TYF_OBJECT)) {
        // An interface extending Array (RegExpMatchArray): the method is Array's, at its element type (tsc goes
        // by the method's declaring symbol and mapper)
        Prop const* prop = ty_get_prop(me, array, method);
        // (Without a global Array (noLib) an array is the empty object type, which has no target)
        Ty const* any_array = ty_array(me, ty_intrinsic(me, TY_ANY), false);
        Symbol const* array_symbol = any_array->kind == TY_REFERENCE ? any_array->reference.target->declared.symbol : nullptr;
        Symbol const* readonly_symbol = ty_readonly_array_target(me)->declared.symbol;
        if (prop && prop->parent && (prop->parent == array_symbol || prop->parent == readonly_symbol)) {
            *readonly |= prop->parent == readonly_symbol;
            ty_buf_push(elements, ty_indexed(me, array, ty_intrinsic(me, TY_NUMBER)));
            return true;
        }
    }
    if (!ty_is_array(me, array)) return false;
    *readonly |= array->reference.target != ty_array(me, ty_intrinsic(me, TY_ANY), false)->reference.target;
    ty_buf_push(elements, ty_reference_args(me, array).data[0]);
    return true;
}

static Ty const* array_fallback_callee(Checker* me, CallExpr const* call) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    if (callee->tag != EXPR_PROPERTY) return nullptr;
    PropertyExpr const* property = callee->property;
    if (property->form != PROPERTY_EXPR_DOT) return nullptr;
    Ty const* receiver = ty_optional_expression_type(me, ty_expr_type(me, &property->expr),
        property->optional_chain, nullptr);
    Ty const* apparent = ty_apparent_type(me, receiver);
    if (apparent->kind != TY_UNION) return nullptr;
    TyBuf elements = {0};
    bool readonly = false;
    Atom const* method = checker_atom(me, name_cooked(span_sv(property_name(property))));
    if (!array_fallback_elements(me, apparent, method, &elements, &readonly, 0)) {
        ty_buf_drop(&elements);
        return nullptr;
    }
    Ty const* element = ty_union(me, elements.data, elements.len, nullptr, (TyList){0});
    ty_buf_drop(&elements);
    Ty const* index = ty_string_literal(me, atom_sv(method));
    return ty_access(me, ty_array(me, element, readonly), index);
}

static bool is_super_call(CallExpr const* call) {
    // (`super<T>()` is TS2754 and otherwise a super call: its type arguments are ignored)
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    return !call->uses_new && callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER;
}

// A parameter of the declaration is written as a literal type (`method: "stop"`, tsc's SignatureFlags.HasLiteralTypes)
static bool signature_has_literal_types(Checker* me, Signature const* sig) {
    while (sig_target(me, sig)) sig = sig_target(me, sig);
    if (!sig->function) return false;
    for_each (arg, vec_function_arg, &sig->function->args) {
        Binding const* binding = &arg->initializer.binding;
        if (binding->has_explicit_type && binding_type(binding)->tag == TYPE_VALUE &&
            binding_type(binding)->value.tag != VALUE_UNDEFINED
        ) {
            return true;
        }
    }
    return false;
}

// The (merged symbol, declaration block) group of a signature, which reorder_candidates() orders by (tsc's
// reorderCandidates)
static TyDeclGroup const* signature_decl_group(Checker* me, Signature const* sig) {
    while (sig_target(me, sig)) sig = sig_target(me, sig);
    return sig->function ? ty_decl_group(me, sig->function) : nullptr;
}

// tsc's reorderCandidates: signatures with literal types first; and of the same interface, each declaration's
// signatures go before those of its declarations before (in their own order): lib.es2015.promise's tuple overload of
// Promise.all is tried before lib.es2015.iterable's Iterable one. Another interface's (a base's) come after
static void reorder_candidates(Checker* me, Signature** signatures, u32 count) {
    static char const sentinel = 0; // Stands for the symbol and declaration of signatures not from an interface
    Signature* small[8];
    Signature** result = count <= c_arraylen(small) ? small : malloc(sizeof(Signature*) * count);
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
        if (signature_has_literal_types(me, signatures[i])) {
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
    if (result != small) free(result);
}

static Ty const* resolve_call_inner(Checker* me, Ty const* callee, CallInfo const* info, Span call_span, ErrorCtx* errs);

// A cycle found while resolving a call doesn't reach what is being resolved around the call (tsc's getResolvedSignature
// setting resolutionStart), only for a call's first resolution: one re-entered while in flight (call_return_type())
// sees the whole stack
// The arguments of a call that resolved to tsc's unknownSignature (resolveErrorCall: an invocation error) have the
// contextual type any (getContextualTypeForArgumentAtIndex: no parameter, `?? anyType`): `this` in their object literal
// methods is any, `x.extend({ m() { this.parent } })` on a union of incompatible signatures
// tsc's resolveUntypedCall: every argument is checked with the call (the contextual type any: an object literal's
// methods' `this`, a generic call's inference). The others' types are computed right there too: b4run's
// `const page = await store.browse(c === null ? {...} : { cursor })` on an unresolved store is TS7022, the argument
// reading `cursor`, which the loop assigns from `page`
static void check_args_untyped(Checker* me, CallExpr const* call) {
    if (!call) return;
    for_each (i, vec_expr, &call->args) {
        if (i->tag == EXPR_OBJECT || i->tag == EXPR_CALL) ty_check_expr(me, i, ty_intrinsic(me, TY_ANY));
        else if (i->tag == EXPR_SPREAD) ty_expr_type(me, i->spread);
        else if (i->tag != EXPR_IMPLICIT_ANY) ty_expr_type(me, i);
    }
}

static Ty const* resolve_call(Checker* me, Ty const* callee, CallInfo const* info, Span call_span, ErrorCtx* errs) {
    bool nested = my resolving_nested_call;
    ty_barrier_push(me, nested ? BARRIER_NESTED_CALL : BARRIER_CALL);
    my resolving_nested_call = false;
    Ty const* ret = resolve_call_inner(me, callee, info, call_span, errs);
    my resolving_nested_call = nested;
    ty_barrier_pop(me);
    return ret;
}

// TS2507 about a class's base (tsc's getBaseConstructorTypeOfClass), which for a type parameter suggests a constraint
// constructing what its constraint's first construct signature does
static void report_not_constructor_type(Checker* me, Ty const* callee, Span span, ErrorCtx* errs) {
    cstr rendered = ty_to_cstr(me, callee);
    Error* error = error_ctx_error_d(errs, span, 2507, "Type '", rendered, "' is not a constructor function type.");
    cstr_drop(&rendered);
    if (!error || callee->kind != TY_TYPE_PARAM || callee->param.state->is_this || !callee->param.state->item) return;
    Ty const* returned = ty_intrinsic(me, TY_UNKNOWN_);
    Ty const* constraint = ty_param_constraint(me, callee);
    if (constraint) {
        Ty const* apparent = ty_apparent_type(me, constraint);
        if (apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) {
            Members const* members = ty_members(me, apparent);
            if (members->construct_count) returned = signature_return_type(me, members->construct[0]);
        }
    }
    cstr name = ty_to_cstr(me, callee), returned_str = ty_to_cstr(me, returned);
    error_add_related_d(error, callee->param.state->item->name, 2735, "Did you mean for '", name,
        "' to be constrained to type 'new (...args: any[]) => ", returned_str, "'?");
    c_drop(cstr, &name, &returned_str);
}

static Ty const* resolve_call_inner(Checker* me, Ty const* callee, CallInfo const* info, Span call_span, ErrorCtx* errs) {
    CallExpr const* call = info->call;
    Ty const* any = ty_intrinsic(me, TY_ANY);
    Ty const* apparent = callee->flags & TYF_ANY ? callee : ty_apparent_type(me, callee);
    // A type variable whose constraint is the error type isn't a constructor type (tsc's isConstructorType: no
    // signatures, nor a mixin constructor constraint)
    if (call->is_extends && !(callee->flags & TYF_ANY) && (apparent->flags & TYF_ANY) && errs) {
        report_not_constructor_type(me, callee, expr_outer_span(&call->callable), errs);
        return ty_intrinsic(me, TY_ERROR);
    }
    // A union is a constructor type only through construct signatures every member has (tsc's isConstructorType of
    // getSignaturesOfType): `C | undefined` is TS2507 as a whole
    if (call->is_extends && apparent->kind == TY_UNION && !(apparent->flags & TYF_BOOLEAN)) {
        u32 count = 0;
        ty_union_signatures(me, apparent->composite.types, true, &count);
        if (!count) {
            if (errs) report_not_constructor_type(me, callee, expr_outer_span(&call->callable), errs);
            return ty_intrinsic(me, TY_ERROR);
        }
    }
    if (apparent->flags & TYF_ANY) {
        // tsc's resolveCallExpression/resolveNewExpression: an untyped call can't take type arguments (an untyped tagged
        // template can: resolveTaggedTemplateExpression doesn't say, `tx.$queryRaw<Row[]>\`...\`` on any)
        if (info->type_arg_nodes && !info->tagged_template && !(callee->flags & TYF_ERROR) && !call->is_extends && !is_super_call(call)) {
            error_ctx_error_d(errs, call_span, 2347, "Untyped function calls may not accept type arguments.");
        }
        // The arguments have the contextual type any (tsc's getContextualTypeForArgumentAtIndex of anySignature, and of
        // unknownSignature's too), which makes `this` in the methods of an object literal any, and a generic call's
        // return type inference any (`untyped(produce(state, ...args))` picks another immer overload)
        check_args_untyped(me, call);
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
            if (errs) {
                cstr rendered = ty_to_cstr(me, callee);
                Error* e = error_ctx_error_d(errs, invocation_error_span(info), construct ? 2351 : 2349,
                    construct ? "This expression is not constructable." : "This expression is not callable.");
                error_sub_error_d(e, span_null, TS_CODE_NONE, "Each member of the union type '", rendered, "' has ",
                    construct ? "construct " : "", "signatures, but none of those signatures are compatible with each other.");
                cstr_drop(&rendered);
            }
            check_args_untyped(me, call);
            return ty_intrinsic(me, TY_ERROR);
        }
        // Some member has none: the union isn't callable, and the first such member is named (tsc's
        // invocationErrorDetails, resolveDecorator's too). Not for `super(...)`, `new` of something callable, or JSX, which
        // the member by member resolution below still covers
        bool some_have = false;
        Ty const* lacking = nullptr;
        for (u32 i = 0; i < apparent->composite.types.len; i++) {
            Members const* members = ty_members(me, ty_apparent_type(me, apparent->composite.types.data[i]));
            bool has = construct ? members->construct_count > 0 : members->call_count > 0;
            some_have |= has;
            if (!has && !lacking) lacking = apparent->composite.types.data[i];
        }
        // (`new` of a union whose members share call signatures but not construct ones calls them: tsc's
        // resolveNewExpression, TS7009)
        if (lacking && call->uses_new && construct && !super_call && !info->tagged_template && !info->jsx) {
            u32 call_count = 0;
            ty_union_signatures(me, apparent->composite.types, false, &call_count);
            if (call_count) {
                if (errs && compiler_options()->no_implicit_any) {
                    error_ctx_error_d(errs, call_span, 7009,
                        "'new' expression, whose target lacks a construct signature, implicitly has an 'any' type.");
                }
                return any;
            }
        }
        // (A call of a union whose members share construct signatures: tsc's resolveCallExpression counts the union's
        // construct signatures, TS2348)
        if (lacking && !construct && !call->uses_new && !super_call && !info->decorator && !info->jsx && !info->tagged_template) {
            u32 construct_count = 0;
            ty_union_signatures(me, apparent->composite.types, true, &construct_count);
            if (construct_count) {
                if (errs) {
                    cstr rendered = ty_to_cstr(me, callee);
                    error_ctx_error_d(errs, call_span, 2348, "Value of type '", rendered, "' is not callable. Did you mean to include 'new'?");
                    cstr_drop(&rendered);
                }
                return ty_intrinsic(me, TY_ERROR);
            }
        }
        if (lacking && !super_call && !info->jsx) { // (A tagged template too: resolveTaggedTemplateExpression's invocationError)
            if (errs) {
                cstr rendered = ty_to_cstr(me, apparent);
                Error* e = error_ctx_error_d(errs, invocation_error_span(info), construct ? 2351 : 2349,
                    construct ? "This expression is not constructable." : "This expression is not callable.");
                if (!some_have) {
                    error_sub_error_d(e, span_null, TS_CODE_NONE, "No constituent of type '", rendered, "' is ",
                        construct ? "constructable." : "callable.");
                } else {
                    cstr member = ty_to_cstr(me, lacking);
                    error_sub_error_d(e, span_null, TS_CODE_NONE, "Not all constituents of type '", rendered, "' are ",
                        construct ? "constructable." : "callable.");
                    error_sub_error_d(vec_error_back_mut(&e->sub_errors), span_null, TS_CODE_NONE, "Type '", member, "' has no ",
                        construct ? "construct" : "call", " signatures.");
                    cstr_drop(&member);
                }
                suggest_await_for_invocation(me, e, apparent, construct, info);
                cstr_drop(&rendered);
            }
            return ty_intrinsic(me, TY_ERROR);
        }
    }
    if (apparent->kind == TY_UNION && !union_sigs) {
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
    Signature* signatures_small[8]; // (Most callees have few: no allocation for them)
    u32 count = 0;
    u32 other_count = 0; // signatures of the kind that wasn't asked for
    if (union_sigs) {
        signatures = union_count <= c_arraylen(signatures_small) ? signatures_small : malloc(sizeof(Signature*) * union_count);
        panic_if(!signatures, "out of memory");
        for (u32 i = 0; i < union_count; i++) signatures[count++] = union_sigs[i];
    } else {
        Members const* members = ty_members(me, apparent);
        Signature* const* list = construct ? members->construct : members->call;
        u32 list_count = construct ? members->construct_count : members->call_count;
        other_count = construct ? members->call_count : members->construct_count;
        if (list_count) {
            signatures = list_count <= c_arraylen(signatures_small) ? signatures_small : malloc(sizeof(Signature*) * list_count);
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
            if (info->type_arg_nodes && !info->tagged_template) {
                error_ctx_error_d(errs, call_span, 2347, "Untyped function calls may not accept type arguments.");
            }
            return any;
        }
    }
    if (!count) {
        if (!errs) {
            // Reported by the typecheck pass
        } else if (call->is_extends) {
            report_not_constructor_type(me, callee, expr_outer_span(&call->callable), errs);
        } else if (call->uses_new && other_count) {
            // Resolved as a call (its arguments checked, tsc's resolveNewExpression), whose result has to be void if
            // it has a declaration (TS2350) unless noImplicitAny says TS7009 instead; the new is any either way
            bool no_implicit_any = compiler_options()->no_implicit_any;
            Candidate chosen = {0};
            if (!info->new_as_call && !info->tagged_template && !info->jsx) {
                CallInfo as_call = *info;
                as_call.new_as_call = true;
                as_call.chosen_out = &chosen;
                resolve_call(me, callee, &as_call, call_span, errs);
            }
            if (no_implicit_any) {
                error_ctx_error_d(errs, call_span, 7009,
                    "'new' expression, whose target lacks a construct signature, implicitly has an 'any' type.");
            } else if (!info->new_as_call && !info->tagged_template && !info->jsx) {
                Signature const* root = chosen.sig;
                while (root && !root->function && root->target) root = root->target;
                if (root && root->function && !(signature_return_type(me, chosen.sig)->flags & TYF_VOID)) {
                    error_ctx_error_d(errs, call_span, 2350, "Only a void function can be called with the 'new' keyword.");
                }
                if (chosen.sig) signature_resolve(me, chosen.sig);
                if (chosen.sig && sig_res(me, chosen.sig)->this_type && sig_res(me, chosen.sig)->this_type == ty_intrinsic(me, TY_VOID)) {
                    error_ctx_error_d(errs, call_span, 2679,
                        "A function that is called with the 'new' keyword cannot have a 'this' type that is 'void'.");
                }
            }
        } else if (call->uses_new) {
            Error* e = error_ctx_error_d(errs, constructor_span(&call->callable), 2351, "This expression is not constructable.");
            // (tsc's invocationErrorDetails: about the apparent type)
            cstr rendered = ty_to_cstr(me, apparent);
            error_sub_error_d(e, span_null, TS_CODE_NONE, "Type '", rendered, "' has no construct signatures.");
            cstr_drop(&rendered);
            suggest_await_for_invocation(me, e, apparent, true, info);
            ty_add_originating_import_invocation(me, e, &call->callable, true);
        } else if (other_count && !info->tagged_template && !info->decorator) { // (tsc's resolveCallExpression only)
            cstr rendered = ty_to_cstr(me, callee);
            error_ctx_error_d(errs, call_span, 2348,
                "Value of type '", rendered, "' is not callable. Did you mean to include 'new'?");
            cstr_drop(&rendered);
        } else if (info->tagged_template && info->tagged_template->template_->array_element) {
            // (tsc's resolveTaggedTemplateExpression: two templates in an array without a comma between them)
            error_ctx_error_d(errs, expr_outer_span(info->tagged_template->template_->tag), 2796, "It is likely that you are "
                "missing a comma to separate these two template expressions. They form a tagged template expression which "
                "cannot be invoked.");
        } else {
            Error* e = not_callable_error(me, errs, info);
            // (tsc's invocationErrorDetails: about the apparent type, `{}` for unknown)
            cstr rendered = ty_to_cstr(me, callee->flags & TYF_UNKNOWN ? ty_empty_object_type(me) : apparent);
            error_sub_error_d(e, span_null, TS_CODE_NONE, "Type '", rendered, "' has no call signatures.");
            cstr_drop(&rendered);
            suggest_await_for_invocation(me, e, apparent, false, info);
            if (!info->jsx) ty_add_originating_import_invocation(me, e, &info->call->callable, false);
            // `f()` then `(x)` on the next line: one call (tsc's resolveCallExpression)
            if (e && !info->tagged_template && !info->decorator && !info->jsx && vec_expr_size(&info->call->args) == 1 &&
                line_break_follows(expr_outer_span(&info->call->callable))
            ) {
                error_add_related_d(e, expr_outer_span(&info->call->callable), 2734, "Are you missing a semicolon?");
            }
        }
        // (An invocation error is tsc's resolveErrorCall, unknownSignature: the error type. `new` of something with
        // only call signatures is resolved as that call, any)
        if (call->uses_new && other_count && !call->is_extends) return ret;
        check_args_untyped(me, call);
        return ty_intrinsic(me, TY_ERROR);
    }

    if (call->is_extends) {
        // `class A extends B`: B only has to be constructable, `super(...)` is the call that gets checked
        if (signatures != signatures_small) free(signatures);
        return any;
    }
    if (call->uses_new) {
        // (Every construct signature's declaration, the first inaccessible one reported: tsgo 7.1's
        // getConstructorAccessibilityError, `typeof A & typeof B` with one private constructor)
        for (u32 i = 0; i < count; i++) {
            if (!ty_constructor_accessible(me, call, signatures[i], call_span, errs)) {
                if (signatures != signatures_small) free(signatures);
                return ty_intrinsic(me, TY_ERROR); // (tsc's resolveErrorCall)
            }
        }
        // (tsc's resolveNewExpression: someSignature, which looks into a union signature's members)
        bool some_abstract = false;
        for (u32 i = 0; i < count; i++) some_abstract |= (signatures[i]->flags & (SIGNATURE_ABSTRACT | SIGNATURE_SOME_ABSTRACT)) != 0;
        if (some_abstract) {
            error_ctx_error_d(errs, call_span, 2511, "Cannot create an instance of an abstract class.");
            if (signatures != signatures_small) free(signatures);
            return ty_intrinsic(me, TY_ERROR);
        }
    }

    // `super(...)`: the base's constructors that take as many type arguments as its base clause has (tsgo's
    // getConstructorsForTypeArguments: none for a clause without them, then filled in by super_filled_type_arguments()),
    // none of them is a call checked against nothing (no error: resolveCall's unknownSignature)
    if (info->super_fill || (info->type_arg_nodes && call && is_super_call(call))) {
        u32 kept = 0;
        for (u32 i = 0; i < count; i++) {
            signature_resolve(me, signatures[i]);
            u32 len = sig_res(me, signatures[i])->type_params.len;
            if (info->type_args.len >= min_type_arguments(me, signatures[i]) && info->type_args.len <= len) {
                signatures[kept++] = signatures[i];
            }
        }
        if (!kept) {
            if (signatures != signatures_small) free(signatures);
            for_each (i, vec_expr, &call->args) ty_expr_type(me, i->tag == EXPR_SPREAD ? i->spread : i);
            return any;
        }
        count = kept;
    }
    CallInfo with_this = *info;
    // (tsc's getSignatureApplicabilityError doesn't check `this` of a new expression)
    if (!construct && !info->new_as_call) {
        bool any_this = false;
        for (u32 i = 0; i < count && !any_this; i++) {
            Signature const* sig = signatures[i];
            any_this = sig_resolved(me, sig) ? sig_res(me, sig)->this_type != nullptr : ty_signature_has_this(me, sig);
        }
        if (any_this) {
            with_this.this_arg = this_argument(me, call, call_span, &with_this.this_span);
            Expr const* callee = &call->callable;
            if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
            with_this.this_unchecked = callee->tag == EXPR_PROPERTY && callee->property->expr.tag == EXPR_IDENTIFIER &&
                callee->property->expr.identifier.keyword == KEYWORD_IDENTIFIER_SUPER &&
                !expr_is_perenthisized(&callee->property->expr) && !expr_is_perenthisized(callee);
        }
    }
    Signature** declared_order = errs && count > 1 ? malloc(sizeof(Signature*) * count) : nullptr;
    if (declared_order) memcpy(declared_order, signatures, sizeof(Signature*) * count);
    with_this.declared_order = declared_order;
    reorder_candidates(me, signatures, count);
    Args args;
    collect_args(me, info, &args);
    Resolution resolution = resolve_with_signatures(me, signatures, count, &args, &with_this, call_span, errs);
    free(declared_order);
    ret = resolution.ret;
    if (info->chosen_out) *info->chosen_out = !resolution.chosen.sig && info->failure_out ? resolution.failure : resolution.chosen;
    if (info->outcome_out) {
        if (!resolution.chosen.sig) info->outcome_out->failure = resolution.failure;
        info->outcome_out->signatures = count;
        if (resolution.chosen.sig) {
            info->outcome_out->param_types = resolution.chosen_types;
            info->outcome_out->param_count = resolution.chosen_count;
        }
    }
    args_drop(&args);
    if (signatures != signatures_small) free(signatures);
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
        .res = { (uintptr_t)sig_resolved_new(me, (SigResolved){ .params = params, .param_count = param ? 1 : 0, .min_args = param ? 1 : 0 }) },
        .ret = { SW_VAL(ret) },
        .predicate = { SW_NONE },
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
        // (Not an intersection's: tsc's getApparentType of one is no union, `NoInfer<T> & string` with T a key of
        // IntrinsicElements has no signatures at all rather than each intrinsic element's)
        if (!*count_out && depth < 8 && type->kind != TY_INTERSECTION) {
            ret = jsx_union_signatures(me, element, apparent->composite.types, depth, count_out);
        }
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
    Ty const* tag_type = element->fragment ? ty_jsx_fragment_type(me, element) : ty_expr_type(me, &element->tag_name);
    if (!tag_type) return ret;
    Ty const* apparent = ty_apparent_type(me, tag_type);
    Span at = element->fragment ? element->opening_span : element->tag_name.inner_span;
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
            error_ctx_error_d(errs, at, 2604, "JSX element type '", at, "' does not have any construct or call signatures.");
        }
        return ret;
    }
    // (A fragment's signatures take their props as a function component's: getEffectiveFirstArgumentForJsxSignature)
    CallInfo info = { .jsx = element, .jsx_class = ret.class_component && !element->fragment };
    Ty const* type_args[32];
    if (!vec_spanned_type_is_empty(&element->type_args)) {
        info.type_arg_nodes = &element->type_args;
        u32 len = 0;
        for_each (i, vec_spanned_type, info.type_arg_nodes) {
            if (len < c_arraylen(type_args)) type_args[len++] = ty_from_node(me, &i->type, (LowerCtx){ .generic = true });
        }
        info.type_args = (TyList){ .data = type_args, .len = len };
    }
    Args args;
    args_init(&args);
    args_push(&args, (Arg){ .expr = &element->attributes, .context_sensitive = ty_expr_is_context_sensitive(&element->attributes) });
    // A component that needs more arguments than the factory gives fits no signature, which is all that's said
    // (checkApplicableSignatureForJsxCallLikeElement checks the arity before the attributes)
    if (!element->fragment && !ty_jsx_tag_arity_fits(me, element, tag_type, errs)) errs = nullptr;
    Resolution resolution = resolve_with_signatures(me, signatures, count, &args, &info, at, errs);
    args_drop(&args);
    ret.sig = resolution.chosen.sig;
    ret.mapper = resolution.chosen.mapper;
    ret.ret = resolution.ret;
    return ret;
}

static Ty const* resolve_into(
    Checker* me, Ty const* callee, CallExpr const* call, Expr const* tagged_template, Ty const* contextual,
    Span call_span, ErrorCtx* errs, Candidate* chosen_out, CallOutcome* outcome_out
) {
    CallInfo info = {
        .call = call, .tagged_template = tagged_template, .contextual = contextual, .chosen_out = chosen_out,
        .call_span = call_span, .failure_out = chosen_out && !errs, .outcome_out = outcome_out,
    };
    Ty const* type_args[32];
    // `super(x)` in `class A extends B<T>`: B's constructors with the type arguments of the heritage clause (tsc's
    // getInstantiatedConstructorsForTypeArguments)
    Expr const* super_callee = &call->callable;
    if (super_callee->tag == EXPR_GENERIC_PARAMS) super_callee = &super_callee->generic_params->expr;
    ClassDecl const* super_class = is_super_call(call) && super_callee->identifier.target ?
        symbol_find_node(super_callee->identifier.target, SYMBOL_DECL_CLASS) : nullptr;
    if (super_class && super_class->extends.tag == EXPR_GENERIC_PARAMS) {
        info.type_arg_nodes = &super_class->extends.generic_params->params;
    } else if (super_class && super_class->extends.tag != EXPR_IMPLICIT_ANY && !span_valid(super_class->jsdoc_extends)) {
        info.super_fill = true;
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
    return signature_predicate(me, sig) || signature_declared_never(me, sig);
}

// The effects of the signature a call resolved to, its predicate instantiated
static CallEffects chosen_effects(Checker* me, Candidate const* chosen) {
    CallEffects none = {0};
    if (!chosen->sig || !has_predicate_or_declared_never(me, chosen->sig)) return none;
    CallEffects ret = { .returns_never = signature_declared_never(me, chosen->sig) };
    TyPredicate const* predicate = signature_predicate(me, chosen->sig);
    if (predicate) {
        ret.predicate = *predicate;
        ret.has_predicate = true;
        if (predicate->type && chosen->mapper) ret.predicate.type = ty_instantiate(me, predicate->type, chosen->mapper);
    }
    return ret;
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
        SigResolved const* sig_rs = signature_resolve(me, sig);
        if (sig_rs->type_params.len) goto fail;
        TyPredicate const* predicate = signature_predicate(me, sig);
        if (!predicate) {
            Ty const* ret = ty_regular(me, signature_return_type(me, sig));
            if (ret != ty_intrinsic(me, TY_FALSE)) goto fail;
            continue;
        }
        if (!predicate->type || predicate->asserts) goto fail; // (No composite assertion, see union_predicate())
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

// A signature that surely has no effects, known from its declaration alone: a TypeScript function written with a
// return type that is neither a type predicate nor `never` (tsc's hasTypePredicateOrNeverReturnType reads the
// declaration's annotations, getSignatureFromDeclaration leaves the parameter types for later). Resolving it would
// type its parameters, which can be what is being inferred: `type R = ReturnType<typeof f>` with f's body calling
// `check(x: R): string` (openclaw's update-command-terminal-record.ts, a spurious TS2456)
static bool surely_no_effects(Checker* me, Signature const* sig) {
    while (sig->target) sig = sig->target;
    FunctionImpl const* function = sig->function;
    if (!function || function->is_type || function->predicate) return false;
    SourceFile const* file = span_source(function->full_span);
    if (!file || file->is_js) return false;
    if (!function->ret_explicit) {
        // Unannotated, its only effect is an inferred type predicate (tsc's getTypePredicateFromBody), which a
        // generator or async function, or a block body without a `return` of a value, never has: `seed(scope)` whose
        // parameter is `ReturnType<typeof createScope>`, called in createScope (openclaw's
        // session-canonical-key.test.ts, a spurious TS7023/TS2502)
        if (function->is_async || function->is_generator) return true;
        return !function->expression_body && !function_impl_has_explicit_return(function);
    }
    return !signature_declared_never(me, sig);
}

CallEffects ty_call_effects(Checker* me, Ty const* callee, CallExpr const* call) {
    CallEffects none = {0};
    if (!callee || call->uses_new || call->is_extends) return none;
    Ty const* apparent = ty_apparent_type(me, callee);
    if (apparent->kind == TY_UNION && !(apparent->flags & TYF_BOOLEAN)) {
        CallEffects effects = union_call_effects(me, apparent);
        if (effects.has_predicate || effects.returns_never) return effects;
        // A method of a union of arrays is that of an array of their elements (getSignaturesOfType's
        // arrayFallbackSignatures): `(A[] | B[]).every(isA)` narrows by `this is S[]`
        u32 count = 0;
        Signature* const* union_sigs = ty_union_signatures(me, apparent->composite.types, false, &count);
        if (!union_sigs) {
            Ty const* fallback = array_fallback_callee(me, call);
            if (fallback && fallback->kind != TY_UNION) return ty_call_effects(me, fallback, call);
            return effects;
        }
        // Generic union signatures (members with one generic method each, `this is Map[K]`), or several (members
        // whose callee is an intersection of a predicate and other signatures): the signature the call resolves to,
        // its predicate instantiated (tsc's getEffectsSignature: getResolvedSignature)
        Candidate chosen = {0};
        signature_resolve(me, union_sigs[0]);
        if (count == 1 && !sig_res(me, union_sigs[0])->type_params.len) {
            chosen.sig = union_sigs[0];
            return chosen_effects(me, &chosen);
        }
        bool resolve = false;
        for (u32 i = 0; i < count && !resolve; i++) {
            signature_resolve(me, union_sigs[i]);
            resolve = has_predicate_or_declared_never(me, union_sigs[i]);
        }
        if (!resolve) return effects;
        resolve_into(me, callee, call, nullptr, nullptr, span_null, nullptr, &chosen, nullptr);
        return chosen_effects(me, &chosen);
    }
    // (getSignaturesOfType: an intersection's are its members')
    if ((apparent->flags & TYF_ANY) || !(apparent->flags & (TYF_OBJECT | TYF_INTERSECTION))) return none;
    Members const* members = ty_members(me, apparent);
    if (!members->call_count) return none;
    bool all_none = true;
    for (u32 i = 0; i < members->call_count && all_none; i++) all_none = surely_no_effects(me, members->call[i]);
    if (all_none) return none;
    Candidate chosen = {0};
    signature_resolve(me, members->call[0]);
    if (members->call_count == 1 && !sig_res(me, members->call[0])->type_params.len) {
        chosen.sig = members->call[0];
    } else {
        bool some = false;
        for (u32 i = 0; i < members->call_count && !some; i++) some = has_predicate_or_declared_never(me, members->call[i]);
        if (!some) return none;
        resolve_into(me, callee, call, nullptr, nullptr, span_null, nullptr, &chosen, nullptr);
    }
    return chosen_effects(me, &chosen);
}

// What `import("m", options)` imports: what the resolve walk found, unless the attributes are in options' type only
// (`import("m", options)` of a variable): then the `with` property's type says (tsgo's checkImportCallExpression)
Symbol* ty_dynamic_import_module(Checker* me, CallExpr const* call, ModuleRef const* ref) {
    if (vec_expr_size(&call->args) < 2 || !binder_has_attributed_patterns(my binder)) return ref->module;
    ModuleAttrs attributes;
    module_ref_syntactic_attributes(ref, &attributes);
    if (!attributes.needs_types) return ref->module;
    Ty const* options = ty_expr_type(me, vec_expr_at(&call->args, 1));
    Ty const* with = ty_prop_type_of(me, options, checker_atom(me, c_sv("with")));
    attributes = (ModuleAttrs){0};
    Members const* members = with ? ty_members(me, with) : nullptr;
    for (u32 i = 0; members && i < members->prop_count && attributes.count < c_arraylen(attributes.items); i++) {
        Ty const* type = ty_prop_type_of(me, with, members->props[i]->name);
        bool known = type && type->kind == TY_LITERAL && type->flags & TYF_STRING_LITERAL;
        attributes.items[attributes.count++] = (ModuleAttr){
            .name = atom_sv(members->props[i]->name), .value = known ? atom_sv(type->literal.string) : c_sv(""), .known = known,
        };
    }
    return binder_find_module_with(my binder, ref, &attributes);
}

// `import("./a")`: a promise of the module (of what its `export =` names)
static Ty const* dynamic_import_type(Checker* me, CallExpr const* call) {
    // (`import<T>("./a")` too: TS1326, typed all the same)
    Expr const* callee = call->callable.tag == EXPR_GENERIC_PARAMS ? &call->callable.generic_params->expr : &call->callable;
    if (callee->tag != EXPR_IDENTIFIER || callee->identifier.keyword != KEYWORD_IDENTIFIER_IMPORT) {
        return nullptr;
    }
    ModuleRef const* ref = identifier_module_ref(&callee->identifier);
    Ty const* module = ty_intrinsic(me, TY_ANY);
    // `import.source("m")`: a promise of the module's source, whatever the module (tsgo's checkImportCallExpression;
    // no arguments: any, like any import call)
    Symbol* resolved = ref && !callee->identifier.import_source ? ty_dynamic_import_module(me, call, ref) : nullptr;
    if (callee->identifier.import_source && vec_expr_size(&call->args)) {
        module = ty_abstract_module_source_type(me);
    } else if (resolved && !resolved->is_shorthand_module) {
        module = ty_dynamic_import_module_type(me, resolved, span_source(ref->span));
    }
    Symbol const* promise_symbol = program_get_global(my program, c_sv("Promise"), SYMBOL_TYPE);
    if (!promise_symbol) {
        // (tsc's createPromiseReturnType for an import call without a global Promise type)
        ty_error_at_current_node(me, 2711, cstr_lit("A dynamic import call returns a 'Promise'. Make sure you have a "
            "declaration for 'Promise' or include 'ES2015' in your '--lib' option."));
        return ty_intrinsic(me, TY_ANY);
    }
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
    ModuleRef const* ref = identifier_module_ref(&call->callable.identifier);
    if (!ref || !ref->module || ref->module->is_shorthand_module) return ty_intrinsic(me, TY_ANY);
    return ty_type_of_symbol(me, module_export_equals(ref->module));
}

// A call is resolved once (tsc's links.resolvedSignature): its node remembers what the first resolution returned and
// reported, wherever that happened (under an outer call's inference, against one of its overloads...), and the
// typecheck walk reports that. Outer inference rounds, overload rounds and the walk don't resolve it again
// What few resolutions have: a failure, diagnostics
typedef struct CallResolutionRare {
    Signature const* failure_sig; // If none fit: the candidate for overload failure (tsc's resolvedSignature then)
    TyMapper const* failure_mapper;
    ErrorCtx errs; // (Registered in Checker.call_errors when not empty)
} CallResolutionRare;

typedef struct CallResolution {
    Ty const* ret;
    Signature const* sig; // The signature it resolved to, null if none fit
    TyMapper const* mapper; // Its inferred type arguments
    Ty const* const* param_types; // See CallOutcome
    u32 param_count;
    // A non-generic signature, the only one: the contextual types its arguments were checked with are the resolved
    // ones already, nothing to derive again (ty_call_resolved_candidate())
    bool exact;
    bool in_flight;
    CallResolutionRare const* rare; // Null if it has neither a failure candidate nor diagnostics (most calls: vscode 1.2M)
} CallResolution;

// `node`: what the resolution is remembered by, the call or the tagged template
static Ty const* call_return_type(
    Checker* me, Ty const* callee, CallExpr const* call, Expr const* tagged_template, void const* node, Ty const* contextual,
    Span call_span
) {
    CallResolution* resolution = (CallResolution*)ty_link_get(me, KEY_CALL_RESOLUTION, node);
    if (resolution && !resolution->in_flight) return resolution->ret;
    // A call asked for again while in flight is resolved again (tsc's getResolvedSignature: resolvingSignature only
    // reaches SkipGenericFunctions and contextual types)
    if (resolution) {
        if (debug_env.debug_reentry) ty_debug_reentry(me, "NESTEDCALL", call_span);
        // Without a barrier of its own: the whole stack is visible to the nested resolution (tsc's resolutionStart is
        // set only for a call's first resolution)
        bool saved = my resolving_nested_call;
        my resolving_nested_call = true;
        Ty const* ret = resolve_into(me, callee, call, tagged_template, contextual, span_null, nullptr, nullptr, nullptr);
        my resolving_nested_call = saved;
        return ret;
    }
    if (my flow_uncached) {
        return resolve_into(me, callee, call, tagged_template, contextual, span_null, nullptr, nullptr, nullptr);
    }
    // (Not remembered while control flow analysis has nothing memoized: tsc's flowLoopStart === flowLoopCount)
    resolution = arena_new(&my arena, CallResolution);
    *resolution = (CallResolution){ .in_flight = true };
    ty_link_set(me, KEY_CALL_RESOLUTION, node, resolution);
    // (Which call its object literal arguments are of: a getter in them is typed by this resolution, see
    // ty_object_literal_accessor_type())
    if (node == call && !tagged_template) {
        for_each (i, vec_expr, &call->args) {
            if (i->tag == EXPR_OBJECT) ty_link_set(me, KEY_LITERAL_ARG_OF, i, call);
        }
    }
    Candidate chosen = {0};
    CallOutcome outcome = {0};
    ErrorCtx errs = { .phase = ERROR_PHASE_TYPECHECK };
    resolution->ret = resolve_into(me, callee, call, tagged_template, contextual, call_span, &errs, &chosen, &outcome);
    resolution->sig = chosen.sig;
    resolution->mapper = chosen.mapper;
    bool failed = !chosen.sig && outcome.failure.sig;
    if (failed || !error_ctx_empty(&errs)) {
        CallResolutionRare* rare = arena_new(&my arena, CallResolutionRare);
        if (failed) {
            rare->failure_sig = outcome.failure.sig;
            rare->failure_mapper = outcome.failure.mapper;
        }
        rare->errs = errs;
        resolution->rare = rare;
        if (!error_ctx_empty(&errs)) {
            if (my call_errors_len == my call_errors_cap) {
                my call_errors_cap = my call_errors_cap ? my call_errors_cap * 2 : 64;
                my call_errors = realloc(my call_errors, my call_errors_cap * sizeof(ErrorCtx*));
                panic_if(!my call_errors, "out of memory");
            }
            my call_errors[my call_errors_len++] = &rare->errs;
        }
    } else {
        error_ctx_drop(&errs);
    }
    resolution->exact = chosen.sig && !chosen.mapper && outcome.signatures == 1;
    resolution->param_types = outcome.param_types;
    resolution->param_count = outcome.param_count;
    // (Only read once it is resolved, and not needed then if exact)
    if (!resolution->exact && !tagged_template && node == call) ty_record_call_anchors(me, call);
    resolution->in_flight = false;
    return resolution->ret;
}

// What a call's resolution returned, if it is remembered and done (tsc's links.resolvedSignature), else null
Ty const* ty_call_resolved_return(Checker* me, CallExpr const* call) {
    CallResolution const* resolution = ty_link_get(me, KEY_CALL_RESOLUTION, call);
    return resolution && !resolution->in_flight ? resolution->ret : nullptr;
}

// The signature a call resolved to, if its resolution is remembered and done and something fit
Signature const* ty_call_resolved_signature(Checker* me, CallExpr const* call) {
    CallResolution const* resolution = ty_link_get(me, KEY_CALL_RESOLUTION, call);
    return resolution && !resolution->in_flight ? resolution->sig : nullptr;
}

bool ty_call_resolution_in_flight(Checker* me, CallExpr const* call) {
    CallResolution const* resolution = ty_link_get(me, KEY_CALL_RESOLUTION, call);
    return resolution && resolution->in_flight;
}

bool ty_call_resolved_candidate(Checker* me, CallExpr const* call, u32 index, Signature const** sig, TyMapper const** mapper, Ty const** param_type) {
    CallResolution const* resolution = ty_link_get(me, KEY_CALL_RESOLUTION, call);
    if (!resolution || resolution->in_flight || resolution->exact) return false;
    *param_type = resolution->sig && index < resolution->param_count ? resolution->param_types[index] : nullptr;
    if (!resolution->sig) {
        if (!resolution->rare || !resolution->rare->failure_sig) return false;
        *sig = resolution->rare->failure_sig;
        *mapper = resolution->rare->failure_mapper;
        return true;
    }
    *sig = resolution->sig;
    *mapper = resolution->mapper;
    return true;
}

// Reports what the call's resolution reported. One never remembered (a `class A extends B` stand-in, a call only
// typed by control flow) is resolved now
static void call_typecheck(
    Checker* me, Ty const* callee, CallExpr const* call, Expr const* tagged_template, void const* node, Ty const* contextual,
    Span call_span, ErrorCtx* errs
) {
    CallResolution const* resolution = node ? ty_link_get(me, KEY_CALL_RESOLUTION, node) : nullptr;
    if (!resolution || resolution->in_flight) {
        resolve_into(me, callee, call, tagged_template, contextual, call_span, errs, nullptr, nullptr);
        return;
    }
    if (!errs || !resolution->rare) return;
    ErrorCtx copy = error_ctx_scratch(errs);
    for_each (i, vec_error, &resolution->rare->errs.errors) {
        error_ctx_append_error(&copy, error_clone(*i));
    }
    error_ctx_merge(errs, &copy);
    error_ctx_drop(&copy);
}

Ty const* ty_call_return_type(Checker* me, Ty const* callee, CallExpr const* call, Span call_span, Ty const* contextual) {
    Ty const* imported = dynamic_import_type(me, call);
    if (imported) return imported;
    Ty const* required = require_type(me, call);
    if (required) return required;
    return call_return_type(me, callee, call, nullptr, call, contextual, call_span);
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
    return call_return_type(me, callee, &call, template, template, contextual, expr_outer_span(template));
}

void ty_tagged_template_typecheck(Checker* me, Ty const* callee, Expr const* template, Ty const* contextual, ErrorCtx* errs) {
    CallExpr call = tagged_template_call(template);
    call_typecheck(me, callee, &call, template, template, contextual, expr_outer_span(template), errs);
}

static Symbol const* awaited_alias(Checker* me) {
    return ty_global_type_alias(me, c_sv("Awaited"), 1);
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

static bool is_thenable(Checker* me, Ty const* ty);

// tsc's isAwaitedTypeNeeded: a generic type that could be (or hold) a promise stays `Awaited<T>`
static bool awaited_type_needed(Checker* me, Ty const* ty, Symbol const* awaited) {
    if ((ty->flags & TYF_ANY) || (awaited && ty->alias == awaited)) return false;
    if (!ty_is_generic_object(me, ty)) return false;
    Ty const* constraint = ty_base_constraint(me, ty);
    // (A mapped type or generic tuple is its own base constraint; without one, a type variable needs it)
    if (!constraint && ty->kind != TY_TYPE_PARAM && !(ty->flags & TYF_DEFERRED) && (ty->flags & TYF_OBJECT)) constraint = ty;
    if (!constraint) return ty_type_variable_in(ty);
    if (constraint->flags & (TYF_ANY | TYF_UNKNOWN)) return true;
    if (ty_is_empty_object_type(me, constraint)) return true; // `{}`, `object`, or a union with one
    TyList parts = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        if (is_thenable(me, parts.data[i])) return true;
    }
    return false;
}

// The types whose awaited type is being computed further up (tsc's awaitedTypeStack)
typedef struct AwaitedFrame {
    Ty const* ty;
    struct AwaitedFrame const* up;
} AwaitedFrame;

// (Null, which a union leaves out, and the error type at the top: tsc's getAwaitedTypeNoAlias returns undefined,
// mapType drops it, checkAwaitedType makes it errorType)
static Ty const* awaited_cycle(Checker* me) {
    ty_error_at_current_node_d(me, 1062, "Type is referenced directly or indirectly in the fulfillment callback of its own 'then' method.");
    return nullptr;
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
    if (ty->kind == TY_INTERSECTION) {
        // (Assignable to a primitive: tsc's allTypesAssignableToKind)
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (primitive_or_never(ty->composite.types.data[i])) return true;
        }
        return false;
    }
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
        Ty const* param = sig_res(me, sigs[i])->param_count ? ty_signature_param_type_at(me, sigs[i], 0) : nullptr;
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
    Ty const* then = ty_prop_type_of(me, ty, my atoms.then);
    if (!then || then->flags & TYF_ANY) return nullptr;
    u32 count = 0;
    Signature* const* sigs = call_signatures_of(me, then, &count);
    Signature** candidates = malloc(sizeof(Signature*) * (count + 1));
    u32 candidate_count = 0;
    for (u32 i = 0; i < count; i++) {
        signature_resolve(me, sigs[i]);
        Ty const* this_type = sig_res(me, sigs[i])->this_type;
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

// tsc's isThenableType: not primitive, with a callable `then`
static bool is_thenable(Checker* me, Ty const* ty) {
    Ty const* constraint = ty_base_constraint(me, ty);
    if (primitive_or_never(constraint ? constraint : ty)) return false;
    Ty const* then = ty_prop_type_of(me, ty, my atoms.then);
    if (!then) return false;
    u32 count = 0;
    call_signatures_of(me, ty_non_nullable(me, then), &count);
    return count > 0;
}

// The `this` type of the last `then` signature that the type doesn't fit, when none fits (tsc's getPromisedTypeOfPromise's
// thisTypeForErrorOut), else null
static Ty const* then_this_type_for_error(Checker* me, Ty const* ty) {
    Ty const* then = ty_prop_type_of(me, ty, my atoms.then);
    if (!then || then->flags & TYF_ANY) return nullptr;
    u32 count = 0;
    Signature* const* sigs = call_signatures_of(me, then, &count);
    Ty const* ret = nullptr;
    for (u32 i = 0; i < count; i++) {
        signature_resolve(me, sigs[i]);
        Ty const* this_type = sig_res(me, sigs[i])->this_type;
        if (this_type && !(this_type->flags & TYF_VOID) && !ty_is_related(me, ty, this_type, TY_RELATION_SUBTYPE)) ret = this_type;
        else return nullptr;
    }
    return ret;
}

// tsc's getAwaitedType with an error node, for `await x`: a thenable that isn't a proper promise is TS1320 (with why,
// when its `then` doesn't take it as `this`)
static void check_await_operand(Checker* me, Ty const* ty, Span at, u32 depth, ErrorCtx* errs) {
    for (; depth < 16; depth++) {
        // (A generic type is awaited as `Awaited<T>` or itself, never an error: tsc's isAwaitedTypeNeeded)
        if (ty->flags & TYF_ANY || ty_is_generic_type(me, ty)) return;
        if (ty->kind == TY_UNION) {
            for (u32 i = 0; i < ty->composite.types.len; i++) check_await_operand(me, ty->composite.types.data[i], at, depth + 1, errs);
            return;
        }
        // (Most operands: a primitive, or the global Promise/PromiseLike of something, like awaited_type_in())
        if (primitive_or_never(ty)) return;
        if (ty->kind == TY_REFERENCE && ty->reference.target->declared.symbol && !ty->reference.target->declared.symbol->parent) {
            csview name = ty->reference.target->declared.symbol->name;
            TyList args = ty_reference_args(me, ty);
            if (args.len == 1 && args.data[0] != ty && (csview_equals(name, "Promise") || csview_equals(name, "PromiseLike"))) {
                ty = args.data[0];
                continue;
            }
        }
        Ty const* promised = promised_type_of(me, ty);
        if (promised) {
            if (promised == ty) return;
            ty = promised;
            continue;
        }
        if (!is_thenable(me, ty)) return;
        Error* error = error_ctx_error_d(errs, at, 1320, "Type of 'await' operand must either be a valid promise or must "
            "not contain a callable 'then' member.");
        Ty const* this_type = error ? then_this_type_for_error(me, ty) : nullptr;
        if (this_type) {
            error_sub_error_d(error, span_null, 2684, "The 'this' context of type '", ty,
                "' is not assignable to method's 'this' of type '", this_type, "'.");
        }
        return;
    }
}

void ty_check_await_operand(Checker* me, Ty const* ty, Span at, ErrorCtx* errs) { check_await_operand(me, ty, at, 0, errs); }

// tsc's getSignaturesOfType(ty, SignatureKind.Call).length
u32 ty_call_signature_count(Checker* me, Ty const* ty) {
    u32 count = 0;
    call_signatures_of(me, ty, &count);
    return count;
}

// tsc's !!getAwaitedTypeOfPromise(ty): a thenable
bool ty_is_promise(Checker* me, Ty const* ty) { return promised_type_of(me, ty) != nullptr; }
Ty const* ty_promised_type(Checker* me, Ty const* ty) { return promised_type_of(me, ty); }

Ty const* ty_awaited_of_promise(Checker* me, Ty const* ty) {
    Ty const* promised = promised_type_of(me, ty);
    return promised ? ty_awaited(me, promised) : nullptr;
}

// The type `await` gives for a value of the given type. `wrap`: a generic one that could be a promise is `Awaited<T>`
// (tsc's getAwaitedType; without it getAwaitedTypeNoAlias)
// A cycle is TS1062 at the current node (tsc reports it at the error node its callers pass, the `await` expression)
static Ty const* awaited_type_in(Checker* me, Ty const* ty, AwaitedFrame const* stack) {
    if (ty->flags & TYF_ANY) return ty;
    Symbol const* awaited = awaited_alias(me);
    if (awaited && ty->alias == awaited) return ty;
    if (ty->kind == TY_UNION) {
        // `type T = string | Promise<T>` awaits T again (a deferred reference makes it an infinite structure)
        if (awaited_in_progress(stack, ty)) return awaited_cycle(me);
        AwaitedFrame frame = { ty, stack };
        Ty const** parts = malloc(sizeof(Ty const*) * ty->composite.types.len);
        bool changed = false;
        u32 len = 0;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* part = awaited_type_in(me, ty->composite.types.data[i], &frame);
            changed |= part != ty->composite.types.data[i];
            if (part) parts[len++] = part;
        }
        // (tsc's mapType: the union itself when no member changed)
        Ty const* ret = changed ? ty_union(me, parts, len, nullptr, (TyList){0}) : ty;
        free(parts);
        return ret;
    }
    // A generic type that could be a promise stays itself (getAwaitedType makes it `Awaited<T>`)
    if (awaited_type_needed(me, ty, awaited)) return ty;
    if (ty->kind == TY_REFERENCE && ty->reference.target->declared.symbol &&
        !ty->reference.target->declared.symbol->parent
    ) {
        csview name = ty->reference.target->declared.symbol->name;
        TyList args = ty_reference_args(me, ty);
        if (args.len == 1 && (csview_equals(name, "Promise") || csview_equals(name, "PromiseLike"))) {
            if (args.data[0] == ty || awaited_in_progress(stack, args.data[0])) return awaited_cycle(me);
            AwaitedFrame frame = { ty, stack };
            return awaited_type_in(me, args.data[0], &frame);
        }
    }
    Ty const* promised = promised_type_of(me, ty);
    if (promised) {
        if (promised == ty || awaited_in_progress(stack, promised)) return awaited_cycle(me);
        AwaitedFrame frame = { ty, stack };
        return awaited_type_in(me, promised, &frame);
    }
    // A `then` that isn't a promise's: no awaited type (the callers say TS1320 and friends, see ty_awaits_thenable())
    if (ty_is_thenable(me, ty)) return ty_intrinsic(me, TY_ERROR);
    return ty;
}

bool ty_is_thenable(Checker* me, Ty const* ty) { return !(ty->flags & TYF_ANY) && is_thenable(me, ty); }

// Whether awaiting ty meets a thenable that isn't a promise (tsc's getAwaitedTypeNoAlias reporting its
// diagnosticMessage): ty, a member of it, or what a promise in there resolves to
static bool awaits_thenable(Checker* me, Ty const* ty, u32 depth) {
    for (; depth < 16 && !(ty->flags & TYF_ANY) && !ty_is_generic_type(me, ty); depth++) {
        if (ty->kind == TY_UNION) {
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (awaits_thenable(me, ty->composite.types.data[i], depth + 1)) return true;
            }
            return false;
        }
        Ty const* promised = promised_type_of(me, ty);
        if (!promised) return ty_is_thenable(me, ty);
        if (promised == ty) return false;
        ty = promised;
    }
    return false;
}

bool ty_awaits_thenable(Checker* me, Ty const* ty) { return awaits_thenable(me, ty, 0); }

static Ty const* awaited_type(Checker* me, Ty const* ty, bool wrap) {
    Ty const* ret = awaited_type_in(me, ty, nullptr);
    if (!ret) return ty_intrinsic(me, TY_ERROR);
    if (wrap) {
        // tsc's createAwaitedTypeIfNeeded
        Symbol const* awaited = awaited_alias(me);
        if (awaited_type_needed(me, ret, awaited)) {
            if (awaited) {
                // (A union's `Awaited<T>` members unwrapped: `Awaited<T | U>`, not `Awaited<Awaited<T> | U>`)
                Ty const* arg = ty_unwrap_awaited(me, ret);
                return ty_alias_instance(me, awaited, (TyList){ .data = &arg, .len = 1 });
            }
            // (Needed but missing: reported once, without a position, tsc's tryCreateAwaitedType)
            if (!my awaited_reported) {
                my awaited_reported = true;
                error_ctx_global_error_d(my errs, 2318, "Cannot find global type 'Awaited'.");
            }
        }
    }
    return ret;
}

Ty const* ty_awaited(Checker* me, Ty const* ty) { return awaited_type(me, ty, true); }


// tsc's createPromiseLikeType: unknown without a global PromiseLike
Ty const* ty_promise_like_of(Checker* me, Ty const* ty) {
    Symbol const* symbol = program_get_global(my program, c_sv("PromiseLike"), SYMBOL_TYPE);
    Ty const* like = symbol ? ty_declared_type_of(me, symbol) : nullptr;
    if (!like || like->kind != TY_DECLARED || like->declared.type_params.len != 1) return ty_intrinsic(me, TY_UNKNOWN_);
    return ty_reference(me, like, (TyList){ .data = &ty, .len = 1 });
}

// tsc's unwrapAwaitedType: `Awaited<T>` (a union's members too) is T
Ty const* ty_unwrap_awaited(Checker* me, Ty const* ty) {
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

// tsc's getAwaitedTypeNoAlias: a generic type that could be a promise stays itself
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
        .res = { (uintptr_t)sig_resolved_new(me, (SigResolved){ .params = sig_params, .param_count = count, .min_args = count }) },
        .ret = { SW_VAL(ret) }, .predicate = { SW_NONE },
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
        // (An optional field's is its symbol's type, with undefined: tsc's getTypeOfNode)
        if (field && item->field.is_optional) value = ty_optional_prop_type(me, value);
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
                    SigResolved with_this = *sig_res(me, mutator);
                    with_this.this_type = this_type;
                    sig_res_init(mutator, sig_resolved_new(me, with_this));
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
            // (tsc's createClassMemberDecoratorContextOverrideType: no symbol, no inferable index)
            Ty const* override = ty_object_literal(me, props, 3, nullptr, 0, OBJECT_SYNTHETIC | OBJECT_NO_INFERABLE_INDEX | OBJECT_NO_SYMBOL);
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
    ErrorCtx lib_errors = error_ctx_scratch(errs);
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
    if ((apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) && span_eq(expr_outer_span(decorator), decorator->inner_span)) {
        Members const* members = ty_members(me, apparent);
        bool uncalled = members->call_count > 0;
        for (u32 i = 0; i < members->call_count && uncalled; i++) {
            Signature const* s = members->call[i];
            SigResolved const* s_res = signature_resolve(me, s);
            // (signatureHasRestParameter: any rest parameter, a tuple's too)
            bool has_rest = s_res->param_count && s_res->params[s_res->param_count - 1].rest;
            uncalled = s_res->min_args == 0 && !has_rest &&
                s_res->param_count < decorator_arg_count(dc.arity, dc.count, s_res->param_count);
        }
        if (uncalled) {
            error_ctx_error_d(errs, site.at, 1329, "'", span_sv(decorator->inner_span),
                "' accepts too few arguments to be used as a decorator here. Did you mean to call it first and write '@",
                span_sv(decorator->inner_span), "()'?");
            return;
        }
    }
    CallExpr call = { .callable = *decorator, .args = vec_expr_init() };
    CallInfo info = { .call = &call, .decorator = &dc, .call_span = site.at };
    ErrorCtx collected = error_ctx_scratch(errs);
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
    if (ret->flags & TYF_ANY) return;
    unsigned code = dc.void_only ? 1271 : 1270;
    VecTyReportLine lines = {0};
    if (ty_relate_explaining(me, ret, dc.ret, code, &lines, errs)) return;
    cstr source_str, target_str;
    ty_report_pair_cstr(me, ret, dc.ret, &source_str, &target_str);
    cstr head = dc.void_only ?
        cstr_d("Decorator function return type is '", source_str, "' but is expected to be 'void' or 'any'.") :
        cstr_d("Decorator function return type '", source_str, "' is not assignable to type '", target_str, "'.");
    c_drop(cstr, &source_str, &target_str);
    ty_report_explained_with_head(errs, &lines, code, head, expr_outer_span(decorator));
}
