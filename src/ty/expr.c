#include "source_file.h"
#include "ty/ty_internal.h"
#include "statement.h"
#include "flow.h"
#include "binding.h"
#include "compiler_options.h"
#include "program.h"
#include "class_decl.h"
#include "scope.h"
#include "error.h"

// The types of expressions. ty_check_expr(expr, contextual) is the one entry point: whoever knows
// the contextual type of an expression (a declaration with a type annotation, a call resolving its arguments, an
// object literal checking its members, a return statement...) checks it with that type *before* anything else asks,
// and the result is the expression's memo. Everything else reads the memo through ty_expr_type(), which checks
// without a contextual type only if nobody got there first. That works because an expression's type is only ever
// demanded by its parent or by the declaration it initializes, and the typecheck pass walks top down.
//
// Like tsc, a function expression takes its parameter types from the first contextual signature it meets and keeps
// them (KEY_CONTEXT_PARAM links, read back by the parameter's own lazy type), while object and array literals are
// simply checked again when the contextual type changes (overload candidates, inference passes).
//
// Literal types that come out of an expression are fresh (TYF_FRESH_LITERAL, ty_fresh_literal()): they widen at a
// mutable location, and ty_regular() gives the regular twin, like tsc's fresh/regular literal type pairs.

static const LowerCtx expr_lower_ctx = { .generic = true };

// What tsc's getContextualType walks up the parents to find, pushed down instead: tsc instantiates at every level too
// (getApparentTypeOfContextualType of the parent before a property lookup), along two chains, without flags and with
// ContextFlags.Signature, which these carry eagerly (plan 51 measured the dynamic `outer_inference` equal to tsc's
// node-captured inference context along every chain)
typedef struct ExprCtx {
    Ty const* contextual; // may be null
    // While a call infers: the contextual type as a contextual signature sees it (tsc's ContextFlags.Signature, which
    // instantiates every level with the non-fixing mapper), when that differs from `contextual`
    Ty const* sig_contextual;
    // While a call infers: that view has no contextual type here though `contextual` has one (a property of a literal
    // whose contextual type is a type parameter with a default: `{}` under the non-fixing mapper, its constraint as
    // `contextual`). A function here has no contextual signature
    bool sig_absent;
    bool is_const; // inside `as const`
    bool template_context; // The index of an element access (a template literal there is a template type)
    // Context-sensitive functions are ty_any_function_type(), whatever contains one isn't remembered (tsc's
    // CheckMode.SkipContextSensitive: the first inference pass sees the rest of an object literal argument)
    bool skip_sensitive;
    GenericFunctionMode* generic_mode; // See GenericFunctionMode: the members of literal arguments
    // While a call infers: `contextual` before its top-level instantiation (what a nested call resolves with: tsc's
    // resolveCall takes getContextualType(), and instantiates it with the outer mappers itself)
    Ty const* raw_contextual;
    // `is_const` was decided by the caller from the contextual type as written (a call's argument against its parameter
    // type, before the return mapper instantiated it): tsc's isConstContext asks getContextualType, `U` in
    // `reduce<U>(..., initial: U)`, not the `const P` that `as P` makes of it. For this expression only
    bool const_given;
} ExprCtx;

static Ty const* compute(Checker* me, Expr const* expr, ExprCtx ctx);
static Ty const* check_template(Checker* me, Expr const* expr, ExprCtx ctx);

static Ty const* any_type(Checker* me) { return ty_intrinsic(me, TY_ANY); }

// A property access that found nothing: tsc's errorType where it reports the missing property (an object type that
// isn't `typeof globalThis`), else any. HACK: approximates the reporting conditions of ty/check.c (JS files and
// whatever else tttt doesn't know the members of stay any)
static Ty const* missing_property_type(Checker* me, Expr const* expr, Ty const* object) {
    if (object->kind == TY_TYPE_PARAM && object->param.state->is_this) object = ty_apparent_type(me, object);
    // Nothing but null and undefined: an error of its own and the error type (tsc's checkNonNullType)
    if ((object->flags & TYF_NULLABLE) && (ty_non_nullable(me, object)->flags & (TYF_NULLABLE | TYF_NEVER))) {
        return ty_intrinsic(me, TY_ERROR);
    }
    // A primitive's members are its apparent type's (`true.e` is reported against Boolean: the error type)
    if (!(object->flags & (TYF_OBJECT | TYF_ANY | TYF_UNKNOWN | TYF_NEVER | TYF_NULLABLE | TYF_TYPE_PARAM)) &&
        (object->kind == TY_LITERAL || (object->kind == TY_INTRINSIC &&
            (object->flags & (TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL | TYF_BOOLEAN))))
    ) {
        object = ty_apparent_type(me, object);
    }
    bool global_this = object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_NAMESPACE &&
        object->declared.namespace_symbol && object->declared.namespace_symbol->is_global_this;
    if ((object->flags & TYF_ANY) || global_this) return any_type(me);
    // What a JavaScript object literal made has every property, except in a JavaScript file that isn't checked (tsc's
    // checkPropertyAccessExpressionOrQualifiedName: isUncheckedJSSuggestion makes it a suggestion and errorType)
    SourceFile const* file = span_source(expr_outer_span(expr));
    bool unchecked_js = file && file->is_js && file->unchecked;
    // (Not when what it's a property of was declared in another global file: approximated by the object's name)
    Expr const* object_expr = &expr->property->expr;
    if (unchecked_js && object_expr->tag == EXPR_IDENTIFIER && object_expr->identifier.target &&
        object_expr->identifier.target->decl_count
    ) {
        SourceFile const* declared = span_source(object_expr->identifier.target->decls[0].full);
        if (declared && declared != file && !declared->is_module) unchecked_js = false;
    }
    if (!unchecked_js && ty_is_js_literal_type(me, object)) return any_type(me);
    return ty_intrinsic(me, TY_ERROR);
}

// Memo

static bool kind_uses_context(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_OBJECT:
        case EXPR_ARRAY:
        case EXPR_FUNCTION:
        case EXPR_TERNARY:
        case EXPR_AS_TYPE:
        case EXPR_TEMPLATE:
        case EXPR_JSX_ATTRIBUTES:
            return true;
        case EXPR_UNARY_OP:
            // (A `yield`'s contextual type decides TS7057: one first evaluated without it is checked again with it)
            return expr->unary_op->op == UNARY_OP_RIGHT_BANG || expr->unary_op->op == UNARY_OP_AWAIT ||
                expr->unary_op->op == UNARY_OP_YIELD;
        case EXPR_BINARY_OP:
            switch (expr->binary_op->op) {
                case BINARY_OP_OR_OR:
                case BINARY_OP_AND_AND:
                case BINARY_OP_EH_EH:
                case BINARY_OP_COMMA:
                case BINARY_OP_EQUAL:
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

Ty const* ty_expr_type(Checker* me, Expr const* expr) {
    LazyType* lazy = (LazyType*)&expr->lazy_type;
    if (lazy->tag != LAZY_TYPE_EXPR || !lazy->slot) return ty_compute_expr_without_context(me, expr); // implicit any and friends
    if (my flow_uncached) {
        // For the flow engine (ty_flow_expr_type()): finished memos only, and nothing is remembered
        LazyMemo* memo = lazy_type_state_peek(lazy);
        return memo && memo->done ? lazy_memo_evaluated(memo) : ty_compute_expr_without_context(me, expr);
    }
    return lazy_type_get(lazy); // Detects cycles
}

static bool has_generic_with_union_constraint(Checker* me, Ty const* ty);

// A reference whose type has a type variable with a union constraint is narrowed as the constraint where the
// contextual type isn't generic (see narrowable_type())
bool ty_reference_uses_context(Checker* me, Expr const* expr) {
    // (A property access says so once it has been computed: access_flow_type())
    if (expr->tag == EXPR_PROPERTY) return intern_get(&my links, (u64[]){ KEY_ACCESS_USES_CONTEXT, (u64)(uintptr_t)expr }, 2) != nullptr;
    if (expr->tag != EXPR_IDENTIFIER || expr->identifier.keyword || !expr->identifier.target) return false;
    return has_generic_with_union_constraint(me, ty_identifier_reference_type(me, &expr->identifier));
}

// tsc's isValidConstAssertionArgument, as far as a const context changes what these check to
bool ty_is_const_context_kind(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_LITERAL:
        case EXPR_TEMPLATE:
        case EXPR_ARRAY:
        case EXPR_OBJECT:
            return true;
        case EXPR_UNARY_OP:
            return expr->unary_op->op == UNARY_OP_MINUS || expr->unary_op->op == UNARY_OP_PLUS;
        case EXPR_PROPERTY: {
            // A member of an enum (`E.A`, `E["A"]`)
            Expr const* object = &expr->property->expr;
            Symbol const* symbol = object->tag == EXPR_IDENTIFIER ? object->identifier.target : nullptr;
            return symbol && (symbol->flags & SYMBOL_ENUM);
        }
        default:
            return false;
    }
}

// tsc's isConstContext: inside `as const`, or the contextual type is a `const` type parameter (an argument of
// `f<const T>(t: T)`)
// (The contextual type as given, before what the call inferring now knows replaces its type parameters: tsc's
// isConstContext asks getContextualType, not the apparent type, so a return type inference doesn't hide `const T`)
static bool in_const_context(Checker* me, Expr const* expr, ExprCtx ctx) {
    Ty const* contextual = ctx.raw_contextual ? ctx.raw_contextual : ctx.contextual;
    return ctx.is_const || (contextual && (contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
        ty_is_const_context_kind(expr) && ty_is_const_type_variable(me, contextual, 0));
}

static Ty const* check_with(Checker* me, Expr const* expr, ExprCtx ctx);
static Ty const* discriminate_by_object_members(Checker* me, Ty const* contextual, ObjectExpr const* object);
static Ty const* apparent_literal_context(Checker* me, Ty const* contextual);
static Ty const* literal_context_now(Checker* me, Expr const* literal, u64* inference);
static bool has_context_sensitive_params(FunctionImpl const* function);

static _Thread_local struct { Expr const* data[64]; u32 len; bool overflow; } collected_returns;
static bool collect_return(Expr const* expr) {
    if (collected_returns.len == c_arraylen(collected_returns.data)) collected_returns.overflow = true;
    else collected_returns.data[collected_returns.len++] = expr;
    return false;
}

// A context-sensitive function checked while skipping context-sensitive arguments (tsc's returnOnlyType in
// checkFunctionExpressionOrObjectLiteralMethod): when its parameters aren't what makes it context sensitive (its
// returned expression is: `(): ... => x => x.length`, `() => ({ f: x => x })`) and the contextual return type could
// mention type parameters, a signature that only has the return type of the body, checked skipping context-sensitive
// parts too, so its other parts infer early. Non-inferrable: its (missing) parameters don't infer anything.
// Generators get tsc's anyFunctionType
static Ty const* return_only_type(Checker* me, FunctionImpl const* function, Ty const* contextual) {
    if (!contextual || function->ret_explicit || !function->has_body || function->is_generator ||
        has_context_sensitive_params(function)) {
        return ty_any_function_type(me);
    }
    Signature const* context_sig = ty_contextual_signature(me, contextual, function);
    if (!context_sig) return ty_any_function_type(me);
    signature_resolve(me, context_sig);
    Ty const* context_ret = signature_return_type(me, context_sig);
    // (tsc's couldContainTypeVariables, which says yes to any function or type literal's type: `() => (x: string) =>
    // boolean`, the applicability check's instantiated parameter, gets the type the inference made, or one now)
    if (!ty_tsc_could_contain_type_variables(context_ret)) return ty_any_function_type(me);
    u64 key[2] = { KEY_CONTEXT_FREE, (u64)(uintptr_t)function };
    void const* known = intern_get(&my links, key, 2);
    if (known) return known;

    // The returned expressions are collected first: checking them can get here for a nested function
    collected_returns.len = 0;
    collected_returns.overflow = false;
    statements_any_return(&function_impl_body(function)->statements, collect_return);
    if (collected_returns.overflow || !collected_returns.len) return ty_any_function_type(me);
    u32 count = collected_returns.len;
    Expr const* returned[c_arraylen(collected_returns.data)];
    memcpy(returned, collected_returns.data, count * sizeof(Expr const*));

    Ty const* context = function->is_async ? ty_async_contextual_awaited(me, context_ret) : context_ret;
    TyBuf types = {0};
    for (u32 i = 0; i < count; i++) {
        Ty const* type = check_with(me, returned[i], (ExprCtx){ .contextual = context, .skip_sensitive = true });
        ty_buf_push(&types, function->is_async ? ty_unwrap_awaited(me, ty_awaited_no_alias(me, type)) : type);
    }
    FlowReachability end = ty_function_end_reachability(me, function);
    if (compiler_options()->strict_null_checks && (function_impl_has_bare_return(function) || end == FLOW_REACH_YES)) {
        ty_buf_push(&types, ty_intrinsic(me, TY_UNDEFINED));
    }
    Ty const* body = ty_widened_return_type(me, ty_union_subtype_reduced(me, types.data, types.len), context);
    ty_buf_drop(&types);
    if (function->is_async) body = ty_promise_of(me, function, body);

    Signature* sig = arena_new(&my arena, Signature);
    *sig = (Signature){
        .flags = SIGNATURE_NON_INFERRABLE, .res = { (uintptr_t)&sig_resolved_none }, .ret = { SW_VAL(body) },
        .predicate = { SW_NONE },
    };
    Ty* type = (Ty*)ty_signature_type(me, sig);
    type->flags |= TYF_NON_INFERRABLE;
    *intern_slot(me, &my links, key, 2) = type;
    return type;
}

// Whether checking the expression may ask for a contextual signature: it is a function or has one where its contextual
// type reaches (what tsc asks getApparentTypeOfContextualType with ContextFlags.Signature for)
static bool may_want_signature(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_FUNCTION: return true;
        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &expr->object) {
                if (may_want_signature(&i->value)) return true;
            }
            return false;
        case EXPR_ARRAY:
            for_each (i, vec_expr, &expr->array) {
                if (may_want_signature(i)) return true;
            }
            return false;
        case EXPR_TERNARY:
            return may_want_signature(&expr->ternary->yes) || may_want_signature(&expr->ternary->no);
        case EXPR_BINARY_OP:
            return may_want_signature(&expr->binary_op->left) || may_want_signature(&expr->binary_op->right);
        case EXPR_JSX_ATTRIBUTES:
            for_each (i, vec_jsx_attribute, &expr->jsx_attributes) {
                if (i->tag == JSX_ATTRIBUTE_EXPR && may_want_signature(&i->value)) return true;
            }
            for_each (i, vec_jsx_child, &jsx_attributes_element(expr)->children) {
                if (i->tag == JSX_CHILD_EXPR && !i->spread && may_want_signature(&i->expr)) return true;
            }
            return false;
        default:
            return false;
    }
}

// The context of an operand whose contextual type is the expression's own (a conditional's branches): before its
// instantiation, which each operand does again (tsc's getContextualType recurses through such parents without
// instantiating, so a call there resolves with the type as written)
static ExprCtx passed_on_context(ExprCtx ctx) {
    if (ctx.raw_contextual && ctx.raw_contextual != ctx.contextual) {
        ctx.contextual = ctx.raw_contextual;
        ctx.sig_contextual = nullptr;
    }
    return ctx;
}

static Ty const* check_with(Checker* me, Expr const* expr, ExprCtx ctx) {
    // (tsc's getApparentTypeOfContextualType: every node's contextual type is instantiated at its top level with what the
    // call inferring now knows. The view a contextual signature sees only where one may be asked for: instantiating
    // creates types, which tsc wouldn't have yet)
    bool inferring = my outer_inference && ((ctx.contextual && (ctx.contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) ||
        ctx.sig_contextual);
    if (inferring) {
        Ty const* raw_sig = ctx.sig_absent ? nullptr : ctx.sig_contextual ? ctx.sig_contextual : ctx.contextual;
        ctx.raw_contextual = ctx.contextual;
        ctx.contextual = ty_instantiate_contextual_type(me, ctx.contextual);
        Ty const* sig_view = !raw_sig ? nullptr :
            may_want_signature(expr) ? ty_instantiate_contextual_type_for(me, raw_sig, true) : ctx.contextual;
        ctx.sig_contextual = sig_view != ctx.contextual ? sig_view : nullptr;
    } else {
        ctx.sig_contextual = nullptr;
        ctx.raw_contextual = ctx.contextual;
    }
    if (!ctx.const_given) ctx.is_const = in_const_context(me, expr, ctx);
    ctx.const_given = false;
    if (ctx.skip_sensitive) {
        if (ty_expr_is_context_sensitive(expr)) {
            if (expr->tag == EXPR_FUNCTION) return return_only_type(me, expr->function, ctx.contextual);
            return compute(me, expr, ctx);
        }
        // (`a && { f: (x) => x }` isn't context sensitive, but tsc's check mode reaches its operand all the same: the
        // function is left out of the first pass, atproto's wizard steps `key != null && { contentRender: (p) => ... }`)
        if (expr->tag == EXPR_BINARY_OP && (expr->binary_op->op == BINARY_OP_AND_AND || expr->binary_op->op == BINARY_OP_COMMA) &&
            ty_expr_is_context_sensitive(&expr->binary_op->right)) {
            return compute(me, expr, ctx);
        }
        ctx.skip_sensitive = false;
    }
    if (ctx.generic_mode) {
        // Literals pass it on, and aren't remembered like this; anything else is a member it may apply to
        // (A conditional expression's branches are what it is: tsc skips a generic call in either under
        // SkipGenericFunctions like at the top, `cond ? share() : shareReplay(...)`)
        if (expr->tag == EXPR_ARRAY || expr->tag == EXPR_TERNARY || expr->tag == EXPR_JSX_ATTRIBUTES) return compute(me, expr, ctx);
        // (And the operands of `??`, `||`, `&&` and `,` like a conditional's branches, `useFn(maybe ?? box(), 2)`)
        if (expr->tag == EXPR_BINARY_OP && (expr->binary_op->op == BINARY_OP_OR_OR || expr->binary_op->op == BINARY_OP_EH_EH ||
            expr->binary_op->op == BINARY_OP_AND_AND || expr->binary_op->op == BINARY_OP_COMMA)
        ) {
            return compute(me, expr, ctx);
        }
        if (expr->tag == EXPR_OBJECT) {
            // (Nor is the contextual type its methods' `this` goes by: the check against the call's chosen candidate,
            // which a remembered type may answer without checking again, is what tsc's resolved signature says)
            Ty const* recorded = ty_link_get(me, KEY_LITERAL_CONTEXT, expr);
            Ty const* ret = compute(me, expr, ctx);
            if (recorded) ty_link_set(me, KEY_LITERAL_CONTEXT, expr, recorded);
            return ret;
        }
        GenericFunctionMode* mode = ctx.generic_mode;
        ctx.generic_mode = nullptr;
        if (!mode->sig && ty_calls_generic_function_returning_function(me, expr)) {
            // (Not a call resolved already: tsc's resolveCallExpression only skips under SkipGenericFunctions when
            // getResolvedSignature has nothing cached)
            Ty const* resolved = ty_call_resolved_return(me, expr->call);
            if (resolved) return resolved;
            mode->skipped = true;
            return ty_intrinsic(me, TY_SILENT_NEVER);
        }
        // (Checked again from the top: with the contextual type before instantiation, which a call resolves with)
        Ty const* instantiated = ctx.contextual;
        ctx.contextual = ctx.raw_contextual;
        return ty_generic_function_in_context(me, check_with(me, expr, ctx), instantiated, mode);
    }
    // A generic function among the return expressions of a function argument whose contextual type wants a single
    // non-generic signature: instantiated in that context while the call infers (tsc's
    // instantiateTypeWithSingleGenericCallSignature under CheckMode.Inferential; getReturnTypeFromBody drops
    // SkipGenericFunctions): `map("", () => ({ x: identity }))` returns `{ x: (y: string) => string }`
    if (my argument_return_depth && my outer_inference && ctx.contextual &&
        (expr->tag == EXPR_IDENTIFIER || expr->tag == EXPR_PROPERTY)
    ) {
        Ty const* type = compute(me, expr, ctx);
        GenericFunctionMode mode = ty_outer_generic_function_mode(me);
        return ty_generic_function_in_context(me, type, ctx.raw_contextual ? ctx.raw_contextual : ctx.contextual, &mode);
    }
    LazyType* lazy = (LazyType*)&expr->lazy_type;
    if (lazy->tag != LAZY_TYPE_EXPR || !lazy->slot) return compute(me, expr, ctx); // implicit any and friends
    LazyMemo* memo = lazy_type_state_peek(lazy);
    if (memo && memo->done && !ctx.is_const && !ctx.template_context) {
        if (!ctx.contextual || memo->context == ctx.contextual->id || !(kind_uses_context(expr) || ty_reference_uses_context(me, expr))) {
            return lazy_memo_evaluated(memo);
        }
    }
    if (!ctx.contextual && !ctx.sig_contextual && !ctx.is_const && !ctx.template_context) return ty_expr_type(me, expr); // through the LazyType, which detects cycles
    Ty const* ret = compute(me, expr, ctx);
    if (my flow_uncached) return ret;
    // (What such a reference is while type arguments are inferred isn't what it is once they are)
    if (my inferential_depth && ty_reference_uses_context(me, expr)) return ret;
    lazy_type_set(lazy, ret, ctx.contextual);
    return ret;
}

Ty const* ty_check_expr(Checker* me, Expr const* expr, Ty const* contextual) {
    return check_with(me, expr, (ExprCtx){ .contextual = contextual });
}

// `is_const`: in a const context whatever the contextual type (it was instantiated from a const type parameter)
Ty const* ty_check_expr_in_const(Checker* me, Expr const* expr, Ty const* contextual, bool is_const) {
    return check_with(me, expr, (ExprCtx){ .contextual = contextual, .is_const = is_const && ty_is_const_context_kind(expr) });
}

static Ty const* check_literal_argument(
    Checker* me, Expr const* expr, Ty const* contextual, bool is_const, bool const_given, bool skip_sensitive, GenericFunctionMode* mode
) {
    return check_with(me, expr, (ExprCtx){
        .contextual = contextual, .is_const = is_const && ty_is_const_context_kind(expr), .const_given = const_given,
        .skip_sensitive = skip_sensitive,
        .generic_mode = expr->tag == EXPR_OBJECT || expr->tag == EXPR_ARRAY || expr->tag == EXPR_TERNARY ||
            expr->tag == EXPR_JSX_ATTRIBUTES || expr->tag == EXPR_BINARY_OP ? mode : nullptr,
    });
}

Ty const* ty_check_literal_argument(
    Checker* me, Expr const* expr, Ty const* contextual, bool is_const, bool skip_sensitive, GenericFunctionMode* mode
) {
    return check_literal_argument(me, expr, contextual, is_const, false, skip_sensitive, mode);
}

// An argument whose contextual type the return mapper instantiated: `is_const` says what the parameter type as written
// makes of it (see ExprCtx.const_given)
Ty const* ty_check_literal_argument_const_given(
    Checker* me, Expr const* expr, Ty const* contextual, bool is_const, bool skip_sensitive, GenericFunctionMode* mode
) {
    return check_literal_argument(me, expr, contextual, is_const, true, skip_sensitive, mode);
}

// Checked with the context-sensitive functions in it left out: see ExprCtx.skip_sensitive
Ty const* ty_check_expr_skipping_sensitive(Checker* me, Expr const* expr, Ty const* contextual, bool is_const) {
    return check_with(me, expr, (ExprCtx){
        .contextual = contextual, .is_const = is_const && ty_is_const_context_kind(expr), .skip_sensitive = true,
    });
}

// Unions and widening

// tsc's isUnitType (computed enum members included)
bool ty_is_unit(Ty const* ty) {
    return ty->kind != TY_UNION && (ty->flags & (TYF_LITERAL | TYF_ENUM | TYF_UNIQUE_ES_SYMBOL | TYF_NULLABLE));
}

// An instance type of a class (tsc's ObjectFlags.Class on getTargetType()), a class merged with an interface too
static bool is_class_instance(Ty const* ty) {
    return ty_declared_is_class(ty);
}

// tsc's emptyObjectType and unknownEmptyObjectType
static bool empty_without_symbol(Checker* me, Ty const* ty) {
    return ty == ty_empty_object_type(me) || ty == ty_unknown_empty_object_type(me);
}

// An anonymous object type with a symbol in tsc: an object literal's, a type literal's, a function's (or an
// instantiation of one)
static bool anonymous_with_symbol(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS) return false;
    if (ty == ty_empty_type_literal(me)) return true;
    while (ty->anonymous.target && ty->anonymous.target->kind == TY_ANONYMOUS && ty->anonymous.target != ty) ty = ty->anonymous.target;
    return ty->anonymous.object_literal || ty->anonymous.node || ty->anonymous.function || ty->anonymous.empty_symbol ||
        ty->anonymous.module_clone;
}

typedef struct ReductionKey {
    Atom const* name; // Null: none
    Ty const* type;
} ReductionKey;

// The first property of a union member's (keyed) type whose type is a unit type, which ty_union_subtype_reduced()
// tells members apart by, and that unit type (regular). Remembered per type: a big union's reduction asked it for each
// member and each reduction again, its intersection members' properties looked up in each other part (5.6M ty_get_prop()
// calls on excalidraw K1)
static Atom const* reduction_key(Checker* me, Ty const* keyed, Ty const** key_type_out) {
    ReductionKey const* known = ty_link_get(me, KEY_REDUCTION_KEY, keyed);
    if (known) {
        *key_type_out = known->type;
        return known->name;
    }
    Atom const* key = nullptr;
    Ty const* key_type = nullptr;
    if (keyed && keyed->kind == TY_INTERSECTION) {
        // An intersection's properties are its members', in order, each of the type the intersection gives it (tsc's
        // getPropertiesOfType): `YieldExpression & { comments: ... }` is keyed by `type` like YieldExpression
        TyList parts = keyed->composite.types;
        for (u32 p = 0; p < parts.len && !key; p++) {
            if (!(parts.data[p]->flags & TYF_OBJECT)) continue;
            Members const* m = ty_members(me, parts.data[p]);
            for (u32 k = 0; k < m->prop_count && !key; k++) {
                // (It is only a unit type if some member's own is: a literal and its primitive are the literal,
                // two literals never)
                Atom const* name = members_prop_name(m, k);
                bool unit_somewhere = false;
                for (u32 q = 0; q < parts.len && !unit_somewhere; q++) {
                    if (!(parts.data[q]->flags & TYF_OBJECT)) {
                        Ty const* t = ty_prop_type_of(me, parts.data[q], name); // (a type variable's apparent type's)
                        unit_somewhere = t && ty_is_unit(t);
                        continue;
                    }
                    Prop const* own = q == p ? members_prop(me, m, k) : ty_get_prop(me, parts.data[q], name);
                    unit_somewhere = own && ty_is_unit(prop_type(me, own));
                }
                Ty const* t = unit_somewhere ? ty_prop_type_of(me, keyed, name) : nullptr;
                if (t && ty_is_unit(t)) {
                    key = members_prop_name(m, k);
                    key_type = ty_regular(me, t);
                }
            }
        }
    } else if (keyed && (keyed->flags & TYF_OBJECT)) {
        Members const* m = ty_members(me, keyed);
        for (u32 k = 0; k < m->prop_count && !key; k++) {
            Ty const* t = prop_type(me, members_prop(me, m, k));
            if (ty_is_unit(t)) {
                key = members_prop_name(m, k);
                key_type = ty_regular(me, t);
            }
        }
    }
    ReductionKey* memo = arena_new(&my arena, ReductionKey);
    *memo = (ReductionKey){ key, key_type };
    ty_link_set(me, KEY_REDUCTION_KEY, keyed, memo);
    *key_type_out = key_type;
    return key;
}

Ty const* ty_union_subtype_reduced(Checker* me, Ty const* const* types, u32 len) {
    if (len < 2) return len ? types[0] : ty_intrinsic(me, TY_NEVER);
    // Flatten first, so members of member unions take part
    Ty const* flat = ty_union(me, types, len, nullptr, (TyList){0});
    if (flat->kind != TY_UNION || (flat->flags & TYF_BOOLEAN)) return flat;
    TyList members = flat->composite.types;
    // Like tsc's removeSubtypes, only structured and instantiable members are candidates for removal, unless there's
    // an empty object type to absorb primitives. Literals next to their primitive are gone already (ty_union()), of
    // tsc's removeRedundantLiteralTypes that leaves `undefined` next to `void`
    bool has_empty_object = false;
    bool has_void = false;
    for (u32 i = 0; i < members.len; i++) {
        Ty const* t = members.data[i];
        has_void |= (t->flags & TYF_VOID) != 0;
        if (has_empty_object || !(t->flags & TYF_OBJECT)) continue;
        Members const* m = ty_members(me, t);
        has_empty_object = !m->mapped && !m->prop_count && !m->call_count && !m->construct_count && !m->index_count;
    }
    u32 structured = TYF_OBJECT | TYF_UNION | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_TEMPLATE_LITERAL |
        TYF_STRING_MAPPING;
    // From the last member on, each is dropped if it's a strict subtype of one still there (so of mutual subtypes the
    // first stays). A member whose first unit-typed property differs from another's can't be its subtype. After 100000
    // checks, an estimate of more than a million is too complex (TS2590, and the error type)
    bool* dropped = calloc(members.len, sizeof(bool));
    panic_if(!dropped, "out of memory");
    u32 count = 0;
    Ty const* ret = nullptr;
    for (u32 i = members.len; i-- > 0 && !ret;) {
        Ty const* source = members.data[i];
        if (!has_empty_object && !(source->flags & structured)) {
            if (has_void && (source->flags & TYF_UNDEFINED)) dropped[i] = true;
            continue;
        }
        if (source->kind == TY_TYPE_PARAM) {
            // A type parameter constrained to a union is a subtype of the rest of the members as a whole, not of one of
            // them (`T extends A | B` next to A and B)
            Ty const* constraint = ty_base_constraint(me, source);
            if (constraint && constraint->kind == TY_UNION) {
                TyBuf rest = {0};
                for (u32 j = 0; j < members.len; j++) if (j != i && !dropped[j]) ty_buf_push(&rest, members.data[j]);
                Ty const* others = ty_union(me, rest.data, rest.len, nullptr, (TyList){0});
                ty_buf_drop(&rest);
                dropped[i] = ty_is_related(me, source, others, TY_RELATION_STRICT_SUBTYPE);
                continue;
            }
        }
        Ty const* key_type = nullptr;
        // (tsc: of objects, intersections and type variables, conditional types and substitutions, whose properties are
        // their apparent type's)
        Ty const* keyed = source->flags & (TYF_OBJECT | TYF_INTERSECTION) ? source :
            (source->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) && source->kind != TY_KEYOF ? ty_apparent_type(me, source) : nullptr;
        Atom const* key = keyed ? reduction_key(me, keyed, &key_type) : nullptr;
        for (u32 j = 0; j < members.len; j++) {
            if (j == i || dropped[j]) continue;
            Ty const* target = members.data[j];
            if (count == 100000) {
                u64 estimate = (u64)count / (members.len - i) * members.len;
                if (estimate > 1000000) {
                    ty_error_at_current_node_d(me, 2590, "Expression produces a union type that is too complex to represent.");
                    ret = ty_intrinsic(me, TY_ERROR);
                    break;
                }
            }
            count++;
            if (key && ((target->flags & (TYF_OBJECT | TYF_INTERSECTION)) ||
                ((target->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) && target->kind != TY_KEYOF))
            ) {
                Ty const* t = ty_prop_type_of(me, target, key);
                if (t && ty_is_unit(t) && ty_regular(me, t) != key_type) continue;
            }
            // The symbolless `{}`s stay next to an empty object literal or type literal, which they would be mutual
            // subtypes of (tsc's removeSubtypes: `u || {}` with u unknown is the narrowed `{}`, without an inferable index)
            if (empty_without_symbol(me, source) && anonymous_with_symbol(me, target) && ty_is_empty_anonymous_object(me, target)) {
                continue;
            }
            // (One class's instances are a subtype of another's only when derived from it)
            if (ty_is_related(me, source, target, TY_RELATION_STRICT_SUBTYPE) &&
                (!is_class_instance(source) || !is_class_instance(target) || ty_is_type_derived_from(me, source, target))
            ) {
                dropped[i] = true;
                break;
            }
        }
    }
    if (!ret) {
        Ty const** kept = malloc(sizeof(Ty const*) * members.len);
        panic_if(!kept, "out of memory");
        u32 kept_len = 0;
        for (u32 i = 0; i < members.len; i++) if (!dropped[i]) kept[kept_len++] = members.data[i];
        if (kept_len == members.len) ret = flat;
        else if (!(ret = ty_named_union_of_reduced(me, types, len, kept, kept_len))) ret = ty_union(me, kept, kept_len, nullptr, (TyList){0});
        free(kept);
    }
    free(dropped);
    return ret;
}

static Ty const* union_of_two(Checker* me, Ty const* a, Ty const* b) {
    Ty const* parts[2] = { a, b };
    return ty_union_subtype_reduced(me, parts, 2);
}

// What an optional chain that may have ended evaluates to (tsc's getOptionalType: no subtype reduction, so a `void`
// result stays `void | undefined`)
static Ty const* optional_type(Checker* me, Ty const* ty) {
    Ty const* parts[2] = { ty, ty_intrinsic(me, TY_UNDEFINED) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

// What an access or call of an optional chain that may have ended evaluates to (tsc's propagateOptionalTypeMarker): at
// the chain's end the undefined of `optional_type()`, before it the marker the rest of the chain takes out again
static Ty const* propagate_optional(Checker* me, Ty const* ty, bool continues_chain, bool optional) {
    if (!optional) return ty;
    if (!continues_chain) return optional_type(me, ty);
    Ty const* parts[2] = { ty, ty_intrinsic(me, TY_OPTIONAL) };
    return ty_union(me, parts, 2, nullptr, (TyList){0});
}

// tsc's getWidenedLiteralLikeTypeForContextualType without the context: fresh literals and unique symbols widen, and what
// is left is regular (a mutable location holds regular literals)
static Ty const* widen_literals(Checker* me, Ty const* ty) {
    return ty_regular(me, ty_widened_unique_symbol(me, ty_widened_literal(me, ty)));
}

// Without strictNullChecks `null` and `undefined` widen to `any`. Unique symbols stay (tsc's getWidenedLiteralType): only
// one made for another declaration widens, and only at the top (binding.c's initialized_binding_type)
Ty const* ty_widened_for_declaration(Checker* me, Ty const* ty, bool literals) {
    // Not regularised: `const e = E.x` holds the fresh `E.x`, `let x = a` with `a: "a"` the regular "a"
    if (literals) ty = ty_widened_literal(me, ty);
    return ty_widened_nullable(me, ty_widened_objects(me, ty));
}

// Contextual types

static bool is_nullable_or_primitive(Ty const* ty) {
    return !(ty->flags & (TYF_OBJECT | TYF_INTERSECTION | TYF_TYPE_PARAM | TYF_DEFERRED | TYF_UNION)) ||
        (ty->flags & TYF_BOOLEAN) != 0;
}

// Maps `get` over the members of a contextual union that have something to say
typedef Ty const* (*ContextGetter)(Checker* me, Ty const* object, void const* arg);

static Ty const* context_map(Checker* me, Ty const* contextual, ContextGetter get, void const* arg) {
    if (!contextual || (contextual->flags & TYF_ANY)) return nullptr;
    // (never says never of each part: tsc's mapType returns it as it is)
    if (contextual->flags & TYF_NEVER) return contextual;
    if (contextual->kind == TY_UNION && !(contextual->flags & TYF_BOOLEAN)) {
        TyList members = contextual->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        u32 len = 0;
        bool unknown = false;
        for (u32 i = 0; i < members.len; i++) {
            Ty const* part = context_map(me, members.data[i], get, arg);
            // tsc maps contextual types without reductions (mapType's noReductions), so an `unknown` from one member
            // (an index signature) doesn't absorb what the others give. It contributes nothing itself: no members,
            // signatures or literals
            if (part && part->kind == TY_INTRINSIC && (part->flags & TYF_UNKNOWN)) unknown = true;
            else if (part) parts[len++] = part;
        }
        if (unknown && !len) parts[len++] = ty_intrinsic(me, TY_UNKNOWN_);
        // (Not literal-reduced either: `number | 1` has room for the literal 2)
        Ty const* ret = len ? ty_union_unreduced(me, parts, len) : nullptr;
        free(parts);
        return ret;
    }
    if (is_nullable_or_primitive(contextual)) return nullptr;
    return get(me, contextual, arg);
}

static bool is_generic_mapped_context(Checker* me, Ty const* ty) {
    return ty->kind == TY_ANONYMOUS && (ty->flags & TYF_OBJECT) && !ty->anonymous.function &&
        (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && ty_members(me, ty)->mapped && !ty_mapped_is_remapping(me, ty_members(me, ty)->mapped);
}

// (tsc's appendContextualPropertyTypeConstituent: `any` says nothing next to the others)
static void push_prop_context_part(Checker* me, TyBuf* parts, Ty const* type) {
    if (type) ty_buf_push(parts, type->flags & TYF_ANY ? ty_intrinsic(me, TY_UNKNOWN_) : type);
}

// Of an intersection, the intersection of what its object members say: a generic mapped member what its template
// says for the key, the others their property, or if none has one, their index signatures (tsc's
// getTypeOfPropertyOfContextualType). Type variables in it say nothing
static bool is_contextual_type_variable(Ty const* ty) {
    return ty->kind == TY_TYPE_PARAM || ty->kind == TY_INDEXED || ty->kind == TY_CONDITIONAL || ty->kind == TY_SUBSTITUTION;
}

static Ty const* get_prop_context(Checker* me, Ty const* object, void const* arg);

// What get_prop_context() looks for: a property by name, its literal type made only when needed (tsc's
// getTypeOfPropertyOfContextualType: getStringLiteralType(name) only for index signatures and generic mapped types)
typedef struct PropContextKey {
    Atom const* name; // Null if the key is a type that names no property
    Ty const* key; // The key type, if known
} PropContextKey;

static Ty const* prop_context_key(Checker* me, PropContextKey const* k) {
    return k->key ? k->key : ty_prop_name_literal(me, k->name);
}

static Ty const* intersection_members_prop_context(Checker* me, Ty const* object, Ty const* const* members, u32 len, Ty const* key, u32 depth);

static Ty const* intersection_prop_context(Checker* me, Ty const* object, Ty const* key) {
    TyList members = object->composite.types;
    bool distributes = false;
    for (u32 i = 0; i < members.len && !distributes; i++) {
        if (!is_contextual_type_variable(members.data[i])) continue;
        Ty const* apparent = ty_apparent_type(me, members.data[i]);
        distributes = apparent->kind == TY_INTERSECTION || (apparent->kind == TY_UNION && apparent->composite.types.len <= 8);
    }
    if (!distributes) return intersection_members_prop_context(me, object, members.data, members.len, key, 0);
    // (Distributed, remembered per intersection and key: tsc makes the apparent type once, resolvedApparentType, and
    // looks properties up in its members' resolved ones; recomputing it per lookup cost tanstack-router ~1.2%)
    static char const none;
    void const** cached = intern_slot(me, &my links, (u64[]){ KEY_INTERSECTION_PROP_CONTEXT, object->id, key->id }, 3);
    if (*cached) return *cached == &none ? nullptr : *cached;
    Ty const* ret = intersection_members_prop_context(me, object, members.data, members.len, key, 0);
    *cached = ret ? (void const*)ret : &none;
    return ret;
}

// (`object` is the intersection, the `this` of its members; `members` what it is made of with the type variables seen
// so far as their constraints)
static Ty const* intersection_members_prop_context(Checker* me, Ty const* object, Ty const* const* members, u32 len, Ty const* key, u32 depth) {
    // A type variable member as its constraint (tsc's getApparentTypeOfIntersectionType: getTypeWithThisArgument with
    // needApparentType, which makes a new intersection): one that is a union distributes the intersection over it,
    // each alternative what its members say (`T & (IsAny<T> extends true ? {} : M)` is `(C & {}) | (C & M)`), one that
    // is an intersection adds its members. An alternative that is an intersection itself is flattened in too. (Bounded:
    // 8 levels, unions of at most 8 members; past that, below, a union says what its members say, as one part)
    for (u32 i = 0; i < len && depth < 8; i++) {
        bool variable = is_contextual_type_variable(members[i]);
        if (!variable && members[i]->kind != TY_INTERSECTION) continue;
        Ty const* apparent = variable ? ty_apparent_type(me, members[i]) : members[i];
        if (apparent->kind != TY_UNION && apparent->kind != TY_INTERSECTION) continue;
        if (apparent->kind == TY_UNION && apparent->composite.types.len > 8) continue;
        TyList inner = apparent->composite.types;
        u32 width = apparent->kind == TY_INTERSECTION ? inner.len : 1;
        Ty const** list = malloc(sizeof(Ty const*) * (len - 1 + width));
        panic_if(!list, "out of memory");
        memcpy(list, members, sizeof(Ty const*) * i);
        memcpy(list + i + width, members + i + 1, sizeof(Ty const*) * (len - i - 1));
        Ty const* ret = nullptr;
        if (apparent->kind == TY_INTERSECTION) {
            memcpy(list + i, inner.data, sizeof(Ty const*) * inner.len);
            ret = intersection_members_prop_context(me, object, list, len - 1 + width, key, depth + 1);
        } else {
            TyBuf alternatives = {0};
            for (u32 j = 0; j < inner.len; j++) {
                list[i] = inner.data[j];
                Ty const* alternative = intersection_members_prop_context(me, object, list, len, key, depth + 1);
                if (alternative) ty_buf_push(&alternatives, alternative);
            }
            ret = !alternatives.len ? nullptr : alternatives.len == 1 ? alternatives.data[0] :
                ty_union(me, alternatives.data, alternatives.len, nullptr, (TyList){0});
            ty_buf_drop(&alternatives);
        }
        free(list);
        return ret;
    }
    TyBuf parts = {0};
    TyBuf index_candidates = {0};
    bool ignore_index = false;
    Atom const* name = ty_literal_name(me, key);
    for (u32 i = 0; i < len; i++) {
        Ty const* member = members[i];
        if (is_contextual_type_variable(member)) member = ty_apparent_type(me, member);
        if (member->kind == TY_UNION || member->kind == TY_INTERSECTION) {
            PropContextKey member_key = { name, key };
            push_prop_context_part(me, &parts, context_map(me, member, get_prop_context, &member_key));
            continue;
        }
        if (!(member->flags & TYF_OBJECT)) continue;
        if (is_generic_mapped_context(me, member)) {
            push_prop_context_part(me, &parts, ty_generic_mapped_prop_context(me, member, key));
            continue;
        }
        FoundProp prop = name ? ty_find_prop(me, member, name) : (FoundProp){0};
        if (!prop.found) {
            if (!ignore_index) ty_buf_push(&index_candidates, member);
            continue;
        }
        ignore_index = true;
        index_candidates.len = 0;
        // (Its members see the whole intersection as their `this`, tsc's getApparentTypeOfIntersectionType)
        push_prop_context_part(me, &parts, ty_access(me, ty_with_this_arg(me, member, object), key));
    }
    for (u32 i = 0; i < index_candidates.len; i++) {
        IndexInfo const* info = ty_applicable_index_info(me, index_candidates.data[i], key);
        push_prop_context_part(me, &parts, info ? info->value : nullptr);
    }
    Ty const* ret = !parts.len ? nullptr : parts.len == 1 ? parts.data[0] :
        ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    ty_buf_drop(&index_candidates);
    return ret;
}

// Whether the property an assignment writes is optional (tsc's prop.flags & SymbolFlags.Optional in
// getFlowTypeOfAccessExpression, which only then removes the missing type): of a union, if some member's is. Only a
// literal key names one; `r.current = cb` with `current: T` and T inferred as `X | missing` keeps the missing type
static bool written_prop_is_optional(Checker* me, Ty const* object, Ty const* key) {
    // (A non-literal key writes the optional properties' write types, tsc's getWriteTypeOfSymbol, which drop the missing
    // type themselves: `cache[key] = undefined` with `cache: Partial<Record<K, X>>` is TS2322; tttt's write types keep it)
    if (!(key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL))) return true;
    Atom const* name = ty_literal_name(me, key);
    Ty const* apparent = ty_apparent_type(me, object);
    TyList members = apparent->kind == TY_UNION ? apparent->composite.types : (TyList){ .data = &apparent, .len = 1 };
    for (u32 i = 0; i < members.len; i++) {
        FoundProp found = ty_find_prop(me, ty_apparent_type(me, members.data[i]), name);
        if (found.found && (found.flags & PROP_OPTIONAL)) return true;
    }
    return false;
}

static Ty const* get_prop_context_with_missing(Checker* me, Ty const* object, PropContextKey const* k);

// (An optional property says its declared type: under exactOptionalPropertyTypes without the missing type, tsc's
// getTypeOfConcretePropertyOfContextualType)
static Ty const* get_prop_context(Checker* me, Ty const* object, void const* arg) {
    Ty const* type = get_prop_context_with_missing(me, object, arg);
    return type ? ty_remove_missing(me, type, true) : nullptr;
}

static Ty const* get_prop_context_with_missing(Checker* me, Ty const* object, PropContextKey const* k) {
    // A type variable says what its constraint says, a generic mapped one included (tsc's
    // getApparentTypeOfContextualType)
    if (is_contextual_type_variable(object)) {
        object = ty_apparent_type(me, object);
        if (is_nullable_or_primitive(object) || object->kind == TY_TYPE_PARAM) return nullptr;
    }
    // (Every intersection, tsc's getTypeOfPropertyOfContextualType: `{ type: "a" | "b" } & { type: any }` keeps "a")
    if (object->kind == TY_INTERSECTION) {
        return intersection_prop_context(me, object, prop_context_key(me, k));
    }
    // A property the object has: no literal type for its name (not generic: a generic mapped type wants the key)
    if (!k->key && !(object->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        Ty const* own = ty_access_own_named_prop(me, object, k->name);
        if (own) return own;
    }
    Ty const* key = prop_context_key(me, k);
    // (Only a key that names a property is looked up in a generic mapped type: a computed one of another type, `[A]`
    // with `A: never`, reads the index signatures, which a generic mapped type has none of, tsc's
    // getContextualTypeForObjectLiteralElement past isTypeUsableAsPropertyName)
    bool names_property = (key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL)) && key->kind != TY_UNION;
    if (!names_property && ty_generic_mapped(me, object)) {
        // (Its index signatures are what its key type's lower bound gives it: `{ [K in keyof C["ops"] & string]: F<K>
        // }` with C's ops a Record<string, ...> has a string one, which `[actionId]` reads, tsc's
        // findApplicableIndexInfo over getIndexInfosOfStructuredType of the resolved mapped type)
        IndexInfo const* index = ty_applicable_index_info(me, object, key);
        return index ? index->value : nullptr;
    }
    Ty const* mapped = ty_generic_mapped_prop_context(me, object, key);
    return mapped ? mapped : ty_access(me, object, key);
}

static Ty const* contextual_prop_type(Checker* me, Ty const* contextual, Ty const* key) {
    PropContextKey k = { nullptr, key };
    return context_map(me, contextual, get_prop_context, &k);
}

// contextual_prop_type() of a property name
static Ty const* contextual_named_prop_type(Checker* me, Ty const* contextual, Atom const* name) {
    PropContextKey k = { name, nullptr };
    return context_map(me, contextual, get_prop_context, &k);
}

typedef struct ElementContextArg {
    u32 index;
    u32 length; // Of the array literal, UINT32_MAX if unknown
    u32 first_spread, last_spread; // UINT32_MAX if none
} ElementContextArg;

// tsc's getContextualTypeForElementExpression for a tuple: the element at the index while the elements before it are
// the tuple's leading fixed ones, the element from the end when the elements after it are its trailing fixed ones,
// otherwise any of the elements in between
static Ty const* tuple_element_context(Checker* me, Ty const* tuple, ElementContextArg const* a) {
    TupleShape const* shape = tuple->reference.target->declared.tuple;
    u32 fixed = ty_tuple_fixed_length(shape);
    if ((a->first_spread == UINT32_MAX || a->index < a->first_spread) && a->index < fixed) {
        // (An optional one's missing type, under exactOptionalPropertyTypes, is left out: tsc's
        // getTypeOfPropertyOfContextualType through removeMissingType)
        Ty const* element = ty_reference_args(me, tuple).data[a->index];
        return ty_remove_missing(me, element, (shape->flags[a->index] & TUPLE_ELEMENT_OPTIONAL) != 0);
    }
    u32 offset = a->length != UINT32_MAX && (a->last_spread == UINT32_MAX || a->index > a->last_spread) ?
        a->length - a->index : 0;
    u32 fixed_end = 0;
    if (offset > 0 && fixed < shape->arity) {
        while (fixed_end < shape->arity && !(shape->flags[shape->arity - 1 - fixed_end] & TUPLE_ELEMENT_REST)) fixed_end++;
    }
    if (offset > 0 && offset <= fixed_end) return ty_reference_args(me, tuple).data[shape->arity - offset];
    u32 from = a->first_spread < fixed ? a->first_spread : fixed;
    u32 end_skip = fixed_end;
    if (a->length != UINT32_MAX && a->last_spread != UINT32_MAX && a->length - a->last_spread < end_skip) {
        end_skip = a->length - a->last_spread;
    }
    return ty_tuple_slice_element_type(me, tuple, from, end_skip, true);
}

// The element of an array literal whose contextual type is any (tsc's getIteratedTypeOrElementType for
// IterationUse.Element: any is iterable)
static Ty const* any_element_context(Checker* me) {
    return ty_intrinsic(me, TY_ANY);
}

static Ty const* get_element_context(Checker* me, Ty const* object, void const* arg) {
    ElementContextArg const* a = arg;
    u32 index = a->index;
    // any has no properties: only iterating it (ES2015 or downlevelIteration) gives an element, any (tsc's
    // getIteratedTypeOrElementType, which in ES5 wants a numeric index signature)
    if ((object->flags & TYF_ANY) ||
        ((object->kind == TY_TYPE_PARAM || (object->flags & TYF_DEFERRED)) && (ty_apparent_type(me, object)->flags & TYF_ANY))
    ) {
        return any_element_context(me);
    }
    // A type parameter as its constraint (tsc's getApparentTypeOfContextualType): the elements of a tuple constraint
    if (object->kind == TY_TYPE_PARAM) {
        Ty const* apparent = ty_apparent_type(me, object);
        if (ty_is_tuple(apparent) && apparent->kind == TY_REFERENCE) return tuple_element_context(me, apparent, a);
        // (A union constraint member by member, unreduced: `R extends "x" | Array<"a">` gives `string | "a"`, which
        // has room for the literal "a")
        if (apparent->kind == TY_UNION) return context_map(me, apparent, get_element_context, arg);
    }
    // A conditional type as its constraint, each member of a union one (`string | [unknown, ...unknown[]] | { [k in
    // keyof D]: N<D[k]> }` gives N<D["0"]> next to unknown)
    if (object->kind == TY_CONDITIONAL) {
        Ty const* apparent = ty_apparent_type(me, object);
        if (apparent != object && apparent->kind != TY_CONDITIONAL) return context_map(me, apparent, get_element_context, arg);
    }
    if (ty_is_tuple(object) && object->kind == TY_REFERENCE) return tuple_element_context(me, object, a);
    // (An intersection by its members' properties, tsc's getTypeOfPropertyOfContextualType: `[any] & [1]` gives 1)
    if (object->kind == TY_INTERSECTION) {
        cstr name = cstr_d(index);
        Ty const* ret = intersection_prop_context(me, object, ty_string_literal(me, cstr_sv(&name)));
        cstr_drop(&name);
        if (ret) return ret;
    }
    if (object->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) {
        cstr name = cstr_d(index);
        Ty const* mapped = ty_generic_mapped_prop_context(me, object, ty_string_literal(me, cstr_sv(&name)));
        cstr_drop(&name);
        if (mapped) return mapped;
    }
    Value value = value_number(index);
    Ty const* ret = ty_access(me, object, ty_literal(me, &value));
    if (!ret) ret = ty_access(me, object, ty_intrinsic(me, TY_NUMBER));
    if (!ret) {
        // Anything iterable: what it yields (tsc's getIteratedTypeOrElementType for IterationUse.Element)
        TyIterationTypes const* types = ty_iteration_types_of_iterable_unreported(me, object, ITERATION_ALLOWS_SYNC);
        if (types) ret = types->yield_type;
    }
    return ret;
}

static Ty const* contextual_element_type(Checker* me, Ty const* contextual, u32 index) {
    ElementContextArg arg = { index, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    return context_map(me, contextual, get_element_context, &arg);
}

// An array literal's length and spread positions, for the contextual types of its elements (computed once per literal:
// once per element is quadratic on large literals)
static ElementContextArg array_element_context_arg(Expr const* array) {
    ElementContextArg arg = { 0, (u32)vec_expr_size(&array->array), UINT32_MAX, UINT32_MAX };
    for_each (i, vec_expr, &array->array) {
        if (i->tag != EXPR_SPREAD) continue;
        if (arg.first_spread == UINT32_MAX) arg.first_spread = (u32)i_index;
        arg.last_spread = (u32)i_index;
    }
    return arg;
}

// The contextual type of the element at `index` of an array literal (`arg` from array_element_context_arg())
static Ty const* contextual_array_element_type_at(Checker* me, Ty const* contextual, ElementContextArg arg, u32 index) {
    if (!contextual) return nullptr;
    if (contextual->flags & TYF_ANY) return any_element_context(me);
    arg.index = index;
    return context_map(me, contextual, get_element_context, &arg);
}

static Ty const* contextual_array_element_type(Checker* me, Ty const* contextual, Expr const* array, u32 index) {
    return contextual ? contextual_array_element_type_at(me, contextual, array_element_context_arg(array), index) : nullptr;
}

Ty const* ty_contextual_prop_type(Checker* me, Ty const* contextual, Ty const* key) {
    return contextual_prop_type(me, contextual, key);
}

Ty const* ty_contextual_element_type(Checker* me, Ty const* contextual, u32 index) {
    return contextual_element_type(me, contextual, index);
}

Ty const* ty_contextual_element_type_of(Checker* me, Ty const* contextual, u32 index, u32 length) {
    ElementContextArg arg = { index, length, UINT32_MAX, UINT32_MAX };
    return context_map(me, contextual, get_element_context, &arg);
}

// tsc's isTupleLikeType past its "0" check: an array-like type (assignable to `readonly any[]`) whose length is number
// literals
bool ty_array_like_with_literal_length(Checker* me, Ty const* ty) {
    Ty const* length = ty_prop_type_of(me, ty, my atoms.length);
    if (!length) return false;
    TyList parts = length->kind == TY_UNION ? length->composite.types : (TyList){ .data = &length, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        if (!(parts.data[i]->flags & TYF_NUMBER_LITERAL)) return false;
    }
    return parts.len && ty_is_related(me, ty, ty_array(me, ty_intrinsic(me, TY_ANY), true), TY_RELATION_ASSIGNABLE);
}

// A spread of a type variable that is array-like stays a variadic element of the tuple it's spread into: `[...self,
// that]` with `self: A extends readonly unknown[]` is `[...A, B]` (tsc's checkArrayLiteral, isArrayLikeType, and
// createNormalizedTupleType keeping InstantiableNonPrimitive variadic elements)
// (A generic mapped type too: createNormalizedTupleType keeps isGenericMappedType variadic elements, `[...x, y]` with
// `x: { [P in keyof E]: X }` is `[...{ [P in keyof E]: X }, Y]`, its elements `{ ... }[number]` in an array)
// An object type that is array-like (assignable to `readonly any[]`) but not an array or tuple, spread into an array
// literal: its number index signature's type, else the error type (tsc's checkArrayLiteral takes it as a variadic
// element, which createNormalizedTupleType makes a rest element of `getIndexTypeOfType(t, number) ?? errorType`,
// not what iterating it gives: `[...p]` with `p: Pick<[D, D], Exclude<keyof [D, D], number>>` is any[]). Null otherwise
static Ty const* array_like_element(Checker* me, Ty const* operand) {
    if (!(operand->flags & (TYF_OBJECT | TYF_INTERSECTION)) || (operand->flags & (TYF_TYPE_PARAM | TYF_DEFERRED | TYF_COULD_CONTAIN_TYPE_VARIABLES))) {
        return nullptr;
    }
    if (ty_is_array(me, operand) || ty_is_tuple(operand) || operand->kind == TY_UNION) return nullptr;
    if (operand->kind == TY_DECLARED && operand->declared.kind == TY_DECLARED_TUPLE_TARGET) return nullptr;
    if (!ty_extends_array(me, operand)) return nullptr;
    Ty const* apparent = ty_apparent_type(me, operand);
    IndexInfo const* index = ty_applicable_index_info(me, apparent, ty_intrinsic(me, TY_NUMBER));
    return index ? index->value : ty_intrinsic(me, TY_ERROR);
}

static bool is_generic_array_like(Checker* me, Ty const* ty) {
    bool generic = (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) != 0;
    if (!generic && ty->kind == TY_ANONYMOUS && (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        MappedInfo const* mapped = ty_members(me, ty)->mapped;
        generic = mapped && ty_is_generic_index(mapped->constraint);
    }
    return generic && ty_extends_array(me, ty);
}

// A union of arrays and tuples is array-like: spread into a tuple it stays a variadic element, which the tuple's
// normalization distributes, `[...op]` with `op: [A] | [B, C]` is `[A] | [B, C]` (tsc's checkArrayLiteral,
// isArrayLikeType, createNormalizedTupleType). (tsc asks assignability to `readonly any[]`: the members by kind here)
static bool is_union_of_arrays(Checker* me, Ty const* ty) {
    if (ty->kind != TY_UNION || (ty->flags & TYF_BOOLEAN)) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        Ty const* member = ty->composite.types.data[i];
        if (!ty_is_array(me, member) && !ty_is_tuple(member) &&
            !(member->kind == TY_DECLARED && member->declared.kind == TY_DECLARED_TUPLE_TARGET)) {
            return false;
        }
    }
    return true;
}

static bool context_is_tuple_like(Checker* me, Ty const* contextual, u32 depth) {
    if (!contextual) return false;
    if (ty_is_tuple(contextual)) return true;
    if (contextual->kind == TY_DECLARED && contextual->declared.kind == TY_DECLARED_TUPLE_TARGET) return true;
    if (contextual->kind == TY_UNION) {
        for (u32 i = 0; i < contextual->composite.types.len; i++) {
            if (context_is_tuple_like(me, contextual->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    if (contextual->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < contextual->composite.types.len; i++) {
            if (context_is_tuple_like(me, contextual->composite.types.data[i], depth + 1)) return true;
        }
    }
    // Anything with a "0" property (tsc's isTupleLikeType: `Pick<[T, ...T[]], ...> & { length: 3 }`), or array-like
    // with a literal length (`number[] & { length: 2 }`, an interface extending `[]`)
    if ((contextual->kind == TY_INTERSECTION || contextual->kind == TY_DECLARED || contextual->kind == TY_REFERENCE) &&
        ty_array_like_with_literal_length(me, contextual)
    ) {
        return true;
    }
    if ((contextual->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_get_prop(me, contextual, my atoms.zero)) return true;
    if (contextual->kind == TY_TYPE_PARAM) {
        Ty const* constraint = ty_param_constraint(me, contextual);
        return constraint && constraint != contextual && context_is_tuple_like(me, constraint, depth + 1);
    }
    // An indexed access or conditional type: what its constraint is (tsc's getApparentTypeOfContextualType, the base
    // constraint: of a conditional both branches, `C extends true ? Required<[a?: A]> : [a?: A]`)
    if (contextual->flags & TYF_DEFERRED) {
        Ty const* constraint = ty_base_constraint(me, contextual);
        return constraint && constraint != contextual && context_is_tuple_like(me, constraint, depth + 1);
    }
    // A generic homomorphic mapped type (tsc's checkArrayLiteral: `{ [K in keyof T]: X }` could map a tuple). Whether
    // it's homomorphic goes by the declaration (tsc's getHomomorphicTypeVariable of the target), whose T must be a type
    // parameter: `{ [K in keyof T["a"]]: X }` isn't
    if (contextual->kind == TY_ANONYMOUS && (contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) &&
        ty_members(me, contextual)->mapped
    ) {
        MappedInfo const* mapped = ty_members(me, contextual)->mapped;
        Ty const* root = contextual->anonymous.target ? contextual->anonymous.target : contextual;
        MappedInfo const* declared = root != contextual && (root->flags & TYF_OBJECT) ? ty_members(me, root)->mapped : mapped;
        if (!declared) declared = mapped;
        Ty const* variable = declared->modifiers_type && declared->keyof_constraint ?
            ty_actual_type_variable(me, declared->modifiers_type) : nullptr;
        if (!mapped->name_type && mapped->modifiers_type && variable && variable->kind == TY_TYPE_PARAM &&
            (mapped->modifiers_type->kind == TY_TYPE_PARAM || (mapped->modifiers_type->flags & TYF_DEFERRED))
        ) {
            return true;
        }
    }
    if (contextual->kind == TY_ANONYMOUS) return ty_prop_exists(me, contextual, my atoms.zero);
    return false;
}

// The flags of a type and, through unions and intersections, of its parts
static u32 ty_flags_in_parts(Ty const* ty) {
    if (ty->kind != TY_UNION && ty->kind != TY_INTERSECTION) return ty->flags;
    u32 ret = ty->flags;
    for (u32 i = 0; i < ty->composite.types.len; i++) ret |= ty_flags_in_parts(ty->composite.types.data[i]);
    return ret;
}

// tsc's isLiteralOfContextualType: a literal type stays as it is where the contextual type has room for literals of
// its kind
static bool is_literal_of_contextual_type(Checker* me, Ty const* literal, Ty const* contextual, u32 depth) {
    if (!contextual || depth > 8) return false;
    if (contextual->kind == TY_UNION || contextual->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < contextual->composite.types.len; i++) {
            if (is_literal_of_contextual_type(me, literal, contextual->composite.types.data[i], depth + 1)) return true;
        }
        return false;
    }
    // (Generic or not: they're primitives to tsc)
    if (contextual->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) return (literal->flags & TYF_STRING_LITERAL) != 0;
    // `keyof T` has room for string literals, whatever T is (tsc: TypeFlags.Index is no InstantiableNonPrimitive)
    if (contextual->kind == TY_KEYOF) return (literal->flags & TYF_STRING_LITERAL) != 0;
    if (contextual->kind == TY_TYPE_PARAM || (contextual->flags & TYF_DEFERRED)) {
        // A type parameter or deferred type: what its constraint has room for. `T extends string` keeps "a"
        Ty const* constraint = ty_base_constraint(me, contextual);
        if (!constraint || constraint == contextual) return false;
        // (tsc's maybeTypeOfKind: `string` anywhere in a union or intersection counts)
        u32 kinds = ty_flags_in_parts(constraint);
        u32 f = literal->flags;
        if (((kinds & TYF_ES_SYMBOL) && (f & TYF_UNIQUE_ES_SYMBOL)) ||
            ((kinds & TYF_STRING) && (f & TYF_STRING_LITERAL)) ||
            ((kinds & TYF_NUMBER) && (f & TYF_NUMBER_LITERAL)) ||
            ((kinds & TYF_BIGINT) && (f & TYF_BIGINT_LITERAL)) ||
            ((kinds & TYF_BOOLEAN) && (f & TYF_BOOLEAN_LITERAL))
        ) {
            return true;
        }
        return is_literal_of_contextual_type(me, literal, constraint, depth + 1);
    }
    if ((contextual->flags & TYF_ES_SYMBOL_LIKE) && (literal->flags & TYF_UNIQUE_ES_SYMBOL)) return true;
    u32 kinds = TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_BIGINT_LITERAL | TYF_BOOLEAN_LITERAL;
    return contextual->kind == TY_LITERAL && (contextual->flags & literal->flags & kinds) != 0;
}

static bool any_literal_of_contextual_type(Checker* me, Ty const* ty, Ty const* contextual) {
    if (ty->kind == TY_LITERAL) return is_literal_of_contextual_type(me, ty, contextual, 0);
    if (ty->kind == TY_UNION && !(ty->flags & TYF_BOOLEAN)) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* part = ty->composite.types.data[i];
            if (part->kind == TY_LITERAL && is_literal_of_contextual_type(me, part, contextual, 0)) return true;
        }
    }
    return (ty->flags & TYF_BOOLEAN) && contextual && is_literal_of_contextual_type(me, ty_intrinsic(me, TY_TRUE), contextual, 0);
}

bool ty_is_literal_of_contextual_type(Checker* me, Ty const* ty, Ty const* contextual) {
    return any_literal_of_contextual_type(me, ty, contextual);
}

// tsc's isTypeAssertion: `x as T` or `<T>x` (parenthesized or not; `satisfies` isn't one)
static bool is_type_assertion(Expr const* expr) {
    return expr->tag == EXPR_AS_TYPE && !expr->as_type->is_satisfies;
}

// tsc's checkExpressionForMutableLocation: the type a property or element gets from its initializer. What a type
// assertion says stays as it is (`{ a: S as typeof S }` keeps the unique symbol)
static Ty const* check_for_mutable_location(Checker* me, Expr const* expr, ExprCtx ctx) {
    Ty const* type = check_with(me, expr, ctx);
    if (in_const_context(me, expr, ctx)) return ty_regular(me, type);
    if (is_type_assertion(expr)) return type;
    if (any_literal_of_contextual_type(me, type, ty_instantiate_contextual_type(me, ctx.contextual))) return ty_regular(me, type);
    return widen_literals(me, type);
}

Ty const* ty_widened_return_type(Checker* me, Ty const* ty, Ty const* contextual) {
    // Only a unit type widens: a function that returns 1 or "a" says so
    bool unit = ty->kind == TY_LITERAL;
    if (unit) ty = contextual && any_literal_of_contextual_type(me, ty, contextual) ? ty_regular(me, ty) : widen_literals(me, ty);
    return ty_widened_nullable(me, ty_widened_objects(me, ty));
}

// The signature a function expression is contextually typed by, null if there's none (or no single one)
// tsc's isAritySmaller: a signature that takes fewer parameters than the function expression requires isn't its context
static bool is_arity_smaller(Checker* me, Signature const* sig, FunctionImpl const* function) {
    signature_resolve(me, sig);
    u32 required = 0;
    u32 index = 0;
    for_each (i, vec_function_arg, &function->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (is_this) continue;
        index++;
        if (i->rest || i->explicit_optional || initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY) break;
        required = index;
    }
    // (A rest tuple of fixed length is that many parameters, not an effective rest: tsc's hasEffectiveRestParameter and
    // getParameterCount, `(...args: Parameters<F>)` with F's two parameters doesn't type `(a, b, c) =>`)
    if (ty_signature_has_rest(me, sig)) return false;
    return ty_signature_param_count(me, sig) < required;
}

// tsc's getContextualCallSignature: the call signatures that don't take fewer parameters than the function has
// required ones. Several are combined into one (getIntersectedSignatures), under noImplicitAny only
static Signature const* contextual_call_signature(Checker* me, Ty const* type, FunctionImpl const* function) {
    Signature const* stack_buf[16];
    Signature const** applicable = stack_buf;
    u32 cap = c_arraylen(stack_buf);
    u32 count = 0;
    if (type->flags & (TYF_OBJECT | TYF_INTERSECTION)) {
        Members const* members = ty_members(me, type);
        for (u32 j = 0; j < members->call_count; j++) {
            if (is_arity_smaller(me, members->call[j], function)) continue;
            if (count == cap) {
                cap *= 2;
                Signature const** grown = malloc(sizeof(Signature const*) * cap);
                panic_if(!grown, "out of memory");
                memcpy(grown, applicable, sizeof(Signature const*) * count);
                if (applicable != stack_buf) free(applicable);
                applicable = grown;
            }
            applicable[count++] = members->call[j];
        }
    }
    Signature const* ret = count == 1 ? applicable[0] : nullptr;
    if (count > 1 && compiler_options()->no_implicit_any) {
        ret = applicable[0];
        signature_resolve(me, ret);
        for (u32 i = 1; i < count && ret; i++) {
            Signature const* right = applicable[i];
            SigResolved const* right_res = signature_resolve(me, right);
            if (right == ret) continue;
            ret = ty_type_params_identical(me, sig_res(me, ret)->type_params, right_res->type_params) ?
                ty_combine_intersection_signatures(me, ret, right) : nullptr;
        }
    }
    if (applicable != stack_buf) free(applicable);
    if (ret) signature_resolve(me, ret);
    return ret;
}

static Ty const* apparent_contextual_type(Checker* me, Ty const* ty);

// tsc's getContextualSignature: of a union, the signature its members have in common (up to return types)
static Signature const* contextual_signature(Checker* me, Ty const* contextual, FunctionImpl const* function, u32 depth) {
    if (!contextual || depth > 8 || (contextual->flags & TYF_ANY)) return nullptr;
    if (contextual->kind == TY_UNION) {
        Signature* stack_buf[16];
        Signature** found = stack_buf;
        u32 count = 0;
        u32 len = contextual->composite.types.len;
        if (len > c_arraylen(stack_buf)) {
            found = malloc(sizeof(Signature*) * len);
            panic_if(!found, "out of memory");
        }
        Signature const* ret = nullptr;
        bool compatible = true;
        for (u32 i = 0; i < len && compatible; i++) {
            Signature const* sig = contextual_signature(me, contextual->composite.types.data[i], function, depth + 1);
            if (!sig) continue;
            if (count && !ty_signatures_identical(me, found[0], sig, false, true)) compatible = false;
            else found[count++] = (Signature*)sig;
        }
        if (compatible && count) ret = count == 1 ? found[0] : ty_union_signature(me, found[0], found, count);
        if (found != stack_buf) free(found);
        return ret;
    }
    // A type parameter as its base constraint (tsc's getApparentType: getBaseConstraintOfType || unknown): nothing for
    // `V extends D | ((eb: EB) => R)` with D unconstrained, whose union's base constraint is none
    if (contextual->kind == TY_TYPE_PARAM) {
        Ty const* constraint = ty_base_constraint(me, contextual);
        return constraint && constraint != contextual ? contextual_signature(me, constraint, function, depth + 1) : nullptr;
    }
    // An indexed access or conditional type: its constraint's (tsc's getApparentTypeOfContextualType)
    if (contextual->kind == TY_INDEXED || contextual->kind == TY_CONDITIONAL || contextual->kind == TY_SUBSTITUTION) {
        Ty const* apparent = ty_apparent_type(me, contextual);
        return apparent != contextual ? contextual_signature(me, apparent, function, depth + 1) : nullptr;
    }
    if (!(contextual->flags & (TYF_OBJECT | TYF_INTERSECTION))) return nullptr;
    // An intersection's signatures are its members', a type variable's those of its constraint (tsc's
    // getSignaturesOfType of each member through getReducedApparentType)
    if (contextual->kind == TY_INTERSECTION && (contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        // (Each member's apparent type on its own: one that is a union without call signatures in common, a conditional
        // type's `"must be sync" | {}`, contributes none, rather than distributing the intersection over it. convex's
        // `withOptimisticUpdate<T extends OptimisticUpdate<A>>(u: T & (ReturnType<T> extends Promise<any> ? "..." : {}))`)
        TyList types = contextual->composite.types;
        Ty const** kept = malloc(sizeof(Ty const*) * types.len);
        panic_if(!kept, "out of memory");
        u32 kept_count = 0;
        bool dropped = false;
        for (u32 i = 0; i < types.len; i++) {
            Ty const* apparent = apparent_contextual_type(me, types.data[i]);
            // (A conditional type's: a union written as a member is distributed over when the intersection is made, and
            // other type variables' apparent unions keep the distribution, `T["children"] & ({} | undefined)`,
            // intersectionOfTypeVariableHasApparentSignatures)
            if (apparent->kind == TY_UNION && types.data[i]->kind == TY_CONDITIONAL && !(apparent->flags & TYF_BOOLEAN)) {
                u32 count = 0;
                ty_union_signatures(me, apparent->composite.types, false, &count);
                if (!count) {
                    dropped = true;
                    continue;
                }
            }
            kept[kept_count++] = apparent;
        }
        if (dropped) {
            contextual = kept_count == 1 ? kept[0] : kept_count ? ty_intersection(me, kept, kept_count, nullptr, (TyList){0}) : nullptr;
            free(kept);
            if (!contextual) return nullptr;
            return contextual_signature(me, contextual, function, depth + 1);
        }
        free(kept);
        contextual = apparent_contextual_type(me, contextual);
        if (!(contextual->flags & (TYF_OBJECT | TYF_INTERSECTION))) return nullptr;
    }
    return contextual_call_signature(me, contextual, function);
}

Signature const* ty_contextual_signature(Checker* me, Ty const* contextual, FunctionImpl const* function) {
    return contextual_signature(me, contextual, function, 0);
}

// The elements of a tuple from `from` on (tsc's getRestTypeAtPosition of a rest tuple parameter): its rest element's
// array when that's all that's left
static Ty const* tuple_slice(Checker* me, Ty const* tuple, u32 from) {
    TupleShape const* shape = ty_tuple_shape(tuple);
    TyList args = ty_reference_args(me, tuple);
    u32 arity = shape->arity;
    if (from >= arity) {
        bool rest_last = arity && (shape->flags[arity - 1] & TUPLE_ELEMENT_REST);
        if (rest_last) return args.data[arity - 1];
        from = arity;
    }
    if (from == arity - 1 && (shape->flags[from] & TUPLE_ELEMENT_REST)) return args.data[from];
    u32 len = arity - from;
    TupleShape sliced = {
        .arity = len, .readonly = false, .flags = shape->flags + from, .names = shape->names ? shape->names + from : nullptr,
        .labels = shape->labels ? shape->labels + from : nullptr,
    };
    Ty const* target = ty_tuple_target(me, &sliced);
    return len ? ty_reference(me, target, (TyList){ .data = args.data + from, .len = len }) : target;
}

static Ty const* signature_type_at(Checker* me, Signature const* sig, u32 index, bool as_rest) {
    SigResolved const* sig_r = sig_res(me, sig);
    for (u32 i = 0; i < sig_r->param_count; i++) {
        SigParam const* param = &sig_r->params[i];
        if (!param->rest) {
            if (i == index && !as_rest) return param->type;
            continue;
        }
        if (index < i) return as_rest ? ty_signature_params_tuple(me, sig, index, nullptr) : nullptr;
        // The rest parameter at the same position: its type as it is, `never` too (tsc's getRestTypeAtPosition with
        // getEffectiveRestType: any is any[]). Past it: an array of the element type
        if (as_rest && i == index) return (param->type->flags & TYF_ANY) ? ty_array(me, param->type, false) : param->type;
        if (as_rest && ty_is_tuple(param->type) && !ty_is_generic_tuple(me, param->type)) {
            return tuple_slice(me, param->type, index - i);
        }
        // A type variable: `T[0]` where it's by position, `T[number][]` for a rest past it (tsc's tryGetTypeAtPosition /
        // getRestTypeAtPosition). A conditional or indexed access type too: getIndexedAccessType defers on a generic
        // object (`...[options]: ConditionalRequired<[options?: X], C>` is `ConditionalRequired<...>[0]`)
        if (param->type->kind == TY_TYPE_PARAM || (param->type->kind != TY_KEYOF && (param->type->flags & TYF_DEFERRED))) {
            if (as_rest) return ty_array(me, ty_indexed(me, param->type, ty_intrinsic(me, TY_NUMBER)), false);
            Value value = value_number(index - i);
            return ty_indexed(me, param->type, ty_literal(me, &value));
        }
        TupleShape const* shape = ty_tuple_shape(param->type);
        if (shape) {
            // Past the end of a tuple without rest elements: nothing (tsc's tryGetTypeAtPosition)
            bool variable = false;
            for (u32 k = 0; k < shape->arity; k++) variable |= (shape->flags[k] & TUPLE_ELEMENT_REST) != 0;
            if (!variable && index - i >= shape->arity) return nullptr;
        }
        Ty const* element;
        if (ty_is_tuple(param->type)) {
            Value value = value_number(index - i);
            element = ty_access(me, param->type, ty_literal(me, &value));
        } else if (ty_is_array(me, param->type) || (param->type->flags & TYF_ANY)) {
            element = ty_rest_element_type(me, param->type);
        } else {
            // By position (tsc's tryGetTypeAtPosition): a union of tuples gives each one's element there
            Value value = value_number(index - i);
            Ty const* at = ty_access(me, param->type, ty_literal(me, &value));
            element = at ? at : ty_access(me, param->type, ty_intrinsic(me, TY_NUMBER));
            // (Nothing there, `(...args: { a: number })`: tsc's getIndexedAccessType without an access node is unknown,
            // so a destructured parameter falls back to its pattern's type without TS7031)
            if (!element && !as_rest) element = ty_intrinsic(me, TY_UNKNOWN_);
        }
        return as_rest && element ? ty_array(me, element, false) : element;
    }
    // A rest parameter where the signature has plain ones: the tuple of those left
    return as_rest ? ty_signature_params_tuple(me, sig, index, nullptr) : nullptr;
}

Ty const* ty_contextual_signature_type_at(Checker* me, Signature const* sig, u32 index, bool as_rest) {
    return signature_type_at(me, sig, index, as_rest);
}

// Whether the parameter whose pattern this is took its contextual type from the pattern, inference having given
// `unknown`: to tsc's getSymbolAtLocation, which asks the contextual signature again, its properties are unknown's
bool ty_contextual_parameter_is_pattern(Checker* me, Binding const* pattern) {
    while (pattern->parent) pattern = pattern->parent;
    u64 key[2] = { KEY_CONTEXT_PARAM_PATTERN, (u64)(uintptr_t)pattern };
    return intern_get(&my links, key, 2) != nullptr;
}

// The contextual signature's own type for a parameter, before assignContextualParameterTypes let a wider initializer
// win (what checkVariableLikeDeclaration checks a binding pattern's initializer against)
Ty const* ty_contextual_parameter_type_unwidened(Checker* me, InitializedBinding const* parameter) {
    Ty const* raw = intern_get(&my links, (u64[]){ KEY_CONTEXT_PARAM_UNWIDENED, (u64)(uintptr_t)parameter }, 2);
    return raw ? raw : ty_contextual_parameter_type(me, parameter);
}

Ty const* ty_contextual_parameter_type(Checker* me, InitializedBinding const* parameter) {
    Ty const* ret = ty_link_get(me, KEY_CONTEXT_PARAM, parameter);
    FunctionImpl const* this_of = initialized_binding_rare(parameter)->this_of;
    if (!ret && this_of) {
        // (A setter's: the getter's `this` parameter, tsc's getTypeForVariableLikeDeclaration)
        FunctionArg const* paired_this = this_of->accessor == 2 ? function_impl_paired_this(this_of) : nullptr;
        ret = paired_this ? ty_type_of_symbol(me, paired_this->initializer.binding.symbol) : ty_contextual_this_type(me, this_of);
    }
    return ret;
}

// tsc's ThisType<T> marker in a contextual type (getThisTypeFromContextualType): T, null if there's none
static Ty const* this_type_marker(Checker* me, Ty const* contextual) {
    if (contextual->kind == TY_UNION || contextual->kind == TY_INTERSECTION) {
        TyBuf found = {0};
        for (u32 i = 0; i < contextual->composite.types.len; i++) {
            Ty const* part = this_type_marker(me, contextual->composite.types.data[i]);
            if (part) {
                ty_buf_push(&found, part);
                if (contextual->kind == TY_INTERSECTION) break;
            }
        }
        Ty const* ret = found.len ? ty_union(me, found.data, found.len, nullptr, (TyList){0}) : nullptr;
        ty_buf_drop(&found);
        return ret;
    }
    if (contextual->kind != TY_REFERENCE) return nullptr;
    Symbol const* symbol = contextual->reference.target->declared.symbol;
    if (!symbol || symbol->parent || !csview_equals(symbol->name, "ThisType")) return nullptr;
    TyList args = ty_reference_args(me, contextual);
    return args.len == 1 ? args.data[0] : nullptr;
}

// A type variable among a contextual type (or its union members) as its apparent type (tsc's
// getApparentTypeOfContextualType, which leaves mapped types alone). The apparent type of an intersection is the
// intersection of its members' (getApparentTypeOfIntersectionType)
static Ty const* apparent_contextual_type(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        TyList types = ty->composite.types;
        Ty const** mapped = malloc(sizeof(Ty const*) * types.len);
        panic_if(!mapped, "out of memory");
        bool changed = false;
        for (u32 i = 0; i < types.len; i++) {
            mapped[i] = apparent_contextual_type(me, types.data[i]);
            changed |= mapped[i] != types.data[i];
        }
        Ty const* ret = !changed ? ty : ty->kind == TY_UNION ? ty_union(me, mapped, types.len, nullptr, (TyList){0}) :
            ty_intersection(me, mapped, types.len, nullptr, (TyList){0});
        free(mapped);
        return ret;
    }
    if (ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED)) return ty_apparent_type(me, ty);
    return ty;
}

// KEY_CONTEXT_CHECKED of a function whose first check had no contextual signature
static Signature const context_checked_none;

Ty const* ty_contextual_this_type(Checker* me, FunctionImpl const* function) {
    // A JavaScript `@type {F}` gives its signature's `this` (tsc's getThisTypeOfDeclaration of a type tag's signature)
    if (function_impl_rare(function)->jsdoc_signature_type) ty_function_apply_jsdoc_type(me, function, nullptr);
    u64 this_key[2] = { KEY_CONTEXT_THIS, (u64)(uintptr_t)function };
    Ty const* from_signature = intern_get(&my links, this_key, 2);
    if (from_signature) return from_signature;
    // (tsc's getContextualThisParameterType: the containing literal or assigned object with noImplicitThis, or in JS)
    bool in_js = span_source(function->full_span) && span_source(function->full_span)->is_js;
    if (!compiler_options()->no_implicit_this && !in_js) return nullptr;
    if (my lazy_getter == function) {
        // (Which `this` the body sees is this worker's state, the call resolution in flight that types the getter
        // first: what is computed from it is that history's, never a clean value. vidstack's player-state.ts: a local
        // of a getter typed under it, published, let another checker skip the evaluation whose expression memos tsc
        // keeps, TS2531 at K16)
        TAINT_EVENT(TAINT_IN_FLIGHT_OTHER);
        if (!my lazy_getter_nested) return ty_intrinsic(me, TY_ANY);
        Expr const* literal = function_impl_rare(function)->this_literal;
        return ty_widened_for_declaration(me, ty_expr_type(me, literal), true);
    }
    if (function_impl_rare(function)->this_literal && !function_impl_rare(function)->this_literal_parenthesized) {
        Expr const* literal = function_impl_rare(function)->this_literal;
        u64 ignored_serial = 0;
        Ty const* contextual = literal_context_now(me, literal, &ignored_serial);
        // A ThisType<T> marker in the contextual type of the literal or of the literals it is a property value of
        // (tsc's getThisTypeOfObjectLiteral)
        Expr const* outer = literal;
        for (u32 depth = 0; outer && depth < 32; depth++) {
            u64 serial = 0;
            Ty const* outer_context = literal_context_now(me, outer, &serial);
            if (!outer_context) break;
            Ty const* marker = this_type_marker(me, ty_apparent_type(me, outer_context));
            // (A ThisType<T> marker, and only that, fixes what it mentions: tsc's getContextualThisParameterType)
            if (marker) return ty_instantiate_fixing(me, marker, serial);
            u64 parent_key[2] = { KEY_LITERAL_PARENT, (u64)(uintptr_t)outer };
            outer = intern_get(&my links, parent_key, 2);
        }
        if (contextual) {
            // The apparent contextual type (tsc's getApparentTypeOfContextualType): a type variable is what it is at
            // least, `this` of a literal returned as a generic conditional type is the conditional's constraint
            contextual = apparent_contextual_type(me, contextual);
            return ty_widened_for_declaration(me, ty_non_nullable(me, contextual), false);
        }
        return ty_widened_for_declaration(me, ty_expr_type(me, literal), true);
    }
    if (function_impl_rare(function)->this_assigned_object) {
        // Not `exports` in `exports.F = function () { this.x = 1 }`, nor `module` in `module.exports = function`
        // (tsgo's getContextualThisParameterType: an identifier whose symbol is ModuleExports)
        Expr const* object = function_impl_rare(function)->this_assigned_object;
        SourceFile const* file = span_source(object->inner_span);
        Symbol const* target = object->tag == EXPR_IDENTIFIER ? object->identifier.target : nullptr;
        if (in_js && file && file->is_commonjs && expr_is_plain_identifier(object, "exports") &&
            (!target || symbol_find_decl(target, SYMBOL_DECL_COMMONJS_EXPORTS))
        ) {
            return nullptr;
        }
        if (in_js && file && file->is_commonjs && expr_is_plain_identifier(object, "module") &&
            (!target || symbol_find_decl(target, SYMBOL_DECL_COMMONJS_MODULE))
        ) {
            return nullptr;
        }
        return ty_widened_for_declaration(me, ty_expr_type(me, function_impl_rare(function)->this_assigned_object), true);
    }
    return nullptr;
}

// The state of one read through anchors
typedef struct AnchorRead {
    bool anchored; // The call is resolved (else the reader goes by what its last check had)
    bool transient; // It depended on a return type being in flight (not cached)
} AnchorRead;

static Signature const* anchored_signature(Checker* me, FunctionImpl const* function, AnchorRead* read, u32 depth, bool* composite_own);
static bool composite_in_flight(Checker* me, Ty const* contextual, FunctionImpl const* function, bool* own);

// `self`: the contextual signature is the function's own (tsc's getReturnTypeFromBody keeps its unit return type)
Ty const* ty_contextual_return_type_self(Checker* me, FunctionImpl const* function, ContextSelf* self) {
    *self = CONTEXT_OTHER;
    if (!function->ret_explicit && function->is_expression) {
        AnchorRead read = {0};
        bool composite_own = false;
        Signature const* sig = anchored_signature(me, function, &read, 0, &composite_own);
        if (read.anchored) {
            // (A composite of signatures with its own while that is inferred: none, tsc's
            // isResolvingReturnTypeOfSignature; but TS7024's circularity, tsc's widening check asks for its return type
            // without that guard)
            if (composite_own) *self = CONTEXT_COMPOSITE_OWN;
            if (!sig) return nullptr;
            // (Its own: none while it is inferred, else what it was inferred as)
            if (sig->function == function && !sig_target(me, sig)) {
                *self = CONTEXT_OWN;
                if (ty_function_return_in_flight(me, function)) {
                    if (debug_env.debug_reentry && ty_resolving_reenters(me, RESOLVING_FUNCTION_RETURN, function)) ty_debug_reentry(me, "RETINFLIGHT call", span_null);
                    return nullptr;
                }
            }
            // (void too: tsc's getContextualReturnType doesn't leave it out, and a call's return type inference
            // infers from it, a candidate that fails its constraint: `onClick={() => setIds(new Set())}` with solid's
            // `<U extends T>(value: Exclude<U, Function> | ...) => U` contextually types `new Set()` by T)
            return signature_return_type(me, sig);
        }
    }
    Signature const* sig = ty_link_get(me, KEY_CONTEXT_RETURN, function);
    if (!sig) return nullptr;
    if (sig->function == function && !sig_target(me, sig)) {
        *self = CONTEXT_OWN;
        if (ty_function_return_in_flight(me, function)) {
            if (debug_env.debug_reentry && ty_resolving_reenters(me, RESOLVING_FUNCTION_RETURN, function)) ty_debug_reentry(me, "RETINFLIGHT call", span_null);
            return nullptr;
        }
    }
    return signature_return_type(me, sig);
}

Ty const* ty_contextual_return_type(Checker* me, FunctionImpl const* function) {
    ContextSelf self;
    return ty_contextual_return_type_self(me, function, &self);
}

Ty const* ty_return_expressions_context(Checker* me, FunctionImpl const* function) {
    Ty const* ret = ty_contextual_return_type(me, function);
    if (ret || !function->is_iife) return ret;
    u64 key[2] = { KEY_CONTEXT_IIFE_RETURN, (u64)(uintptr_t)function };
    return intern_get(&my links, key, 2);
}

// A function expression meets a contextual type. Its unannotated parameters take their types from the contextual
// signature, once: the first contextual signature decides (tsc's ContextChecked)
Signature const* ty_function_contextual_signature(Checker* me, FunctionImpl const* function, Ty const* contextual) {
    return contextual_signature(me, ty_instantiate_contextual_type_for(me, contextual, true), function, 0);
}

// The context of a function expression as a function of the node (FunctionImpl.context_root). Contextual types are
// pushed down (this file's header): the first check of a function with a contextual type assigns its contextual
// signature (KEY_CONTEXT_CHECKED, KEY_CONTEXT_PARAM, KEY_CONTEXT_RETURN...), which its parameters and inferred return
// type read back. Something may ask for those before anything checked the function: a JavaScript `this.x = p` member
// read elsewhere, a type another checker of the shared graph published that holds the function's type (plan 86 stage
// 4f). tsc's getContextualSignature walks up and resolves the enclosing call on demand; here the function's context
// root, the outermost node the walk checks (an expression statement's expression, a declaration's initializer, a
// return expression), is checked first, as the walk would check it, and the links are read then. A call is resolved
// once, wherever that happens first (call.c), so this gives the context the walk gives; checking a root from inside
// (an inner call before the outer one that infers it) wouldn't. Nothing is done for a root in flight on this checker:
// its check assigns the links when it gets to the function (or doesn't: what that check does then is today's)
// The roots being checked on this thread, by a settlement or by the walk where nothing else says so
// (ty_context_root_enter()): not settled again from inside
static _Thread_local struct { void const* data[256]; u32 len, overflow; } settling;

bool ty_context_root_enter(void const* node) {
    if (settling.len == c_arraylen(settling.data)) {
        settling.overflow++;
        return false;
    }
    settling.data[settling.len++] = node;
    return true;
}

void ty_context_root_leave(bool entered) {
    if (entered) settling.len--;
    else settling.overflow--;
}

static bool context_root_in_flight(void const* node) {
    if (settling.overflow) return true;
    for (u32 i = 0; i < settling.len; i++) {
        if (settling.data[i] == node) return true;
    }
    return false;
}

// (An accessor or a method of an object literal is never checked as an expression of its own: no KEY_CONTEXT_CHECKED)
static bool function_unsettled(Checker* me, FunctionImpl const* function) {
    return function->context_root && function->is_expression && !function->accessor &&
        !ty_link_get(me, KEY_CONTEXT_CHECKED, function) && !function_impl_rare(function)->jsdoc_signature_type;
}

static _Thread_local FunctionImpl const* settle_target;
static _Thread_local Expr const* settle_found;
static bool return_contains_target(Expr const* expr) {
    if (!span_contains_span(expr_outer_span(expr), settle_target->full_span)) return false;
    settle_found = expr;
    return true;
}

static void settle_function(Checker* me, FunctionImpl const* function, u32 depth);

static void settle_root(Checker* me, FunctionImpl const* function, uintptr_t root, u32 depth) {
    void const* node = context_root_node(root);
    switch (context_root_kind(root)) {
        case CONTEXT_ROOT_EXPR: {
            Expr const* expr = node;
            LazyMemo* memo = lazy_type_state_peek((LazyType*)&expr->lazy_type);
            if (memo && memo->evaluation_started && !memo->done) return;
            (void)ty_expr_type(me, expr);
            return;
        }
        case CONTEXT_ROOT_BINDING: {
            InitializedBinding* binding = (InitializedBinding*)node;
            FunctionImpl const* outer = initialized_binding_rare(binding)->context_function;
            if (outer) settle_function(me, outer, depth + 1);
            LazyMemo* memo = lazy_type_state_peek(&binding->lazy_type);
            if (memo && memo->evaluation_started && !memo->done) return;
            initialized_binding_settle_value(binding);
            return;
        }
        case CONTEXT_ROOT_RETURN: {
            FunctionImpl const* outer = node;
            if (outer->is_generator || !outer->has_body) return;
            settle_function(me, outer, depth + 1);
            // As the walk checks a return statement: the inferred return type first, then the expression in its return
            // context (statement_typecheck_inner()). The first has checked it, unless it was known already
            if (!outer->ret_explicit && !function_impl_rare(outer)->constructed_class && !function_impl_setter_type(outer)) {
                if (ty_function_return_in_flight(me, outer)) return;
                ty_function_return_type(me, outer);
            }
            if (!function_unsettled(me, function)) return;
            settle_target = function;
            settle_found = nullptr;
            statements_any_return(&function_impl_body(outer)->statements, return_contains_target);
            Expr const* found = settle_found;
            if (!found) return;
            Ty const* outer_context = function_impl_enter_return_context(outer);
            CurrentNode saved_node = ty_enter_node(me, expr_outer_span(found));
            ty_check_expr(me, found, my return_context);
            ty_leave_node(me, saved_node);
            my return_context = outer_context;
            return;
        }
        default: return;
    }
}

static void settle_function(Checker* me, FunctionImpl const* function, u32 depth) {
    if (depth > 8 || !function_unsettled(me, function)) return;
    uintptr_t root = function->context_root;
    void const* node = context_root_node(root);
    if (context_root_in_flight(node)) return;
    bool entered = ty_context_root_enter(node);
    // Self-contained, like a declaration (tsc's checkExpressionCached): not an argument of the call inferring now, no
    // loop in progress, no return context, and a node of its own (what is reported at the current node is at the root,
    // with the instantiation count the walk would start it with) whose count the asker gets back
    FlowIsolation isolation = ty_flow_isolate(me);
    struct OuterInference* outer = my outer_inference;
    u32 inferential_depth = my inferential_depth, argument_return_depth = my argument_return_depth;
    Ty const* return_context = my return_context;
    u32 instantiation_count = my instantiation_count;
    my outer_inference = nullptr;
    my inferential_depth = my argument_return_depth = 0;
    my return_context = nullptr;
    Span at = context_root_kind(root) == CONTEXT_ROOT_EXPR ? expr_outer_span(node) :
        context_root_kind(root) == CONTEXT_ROOT_BINDING ? ((InitializedBinding const*)node)->binding.content_span : ((FunctionImpl const*)node)->full_span;
    CurrentNode saved_node = ty_enter_node(me, at);
    settle_root(me, function, root, depth);
    ty_leave_node(me, saved_node);
    my instantiation_count = instantiation_count;
    my return_context = return_context;
    my inferential_depth = inferential_depth;
    my argument_return_depth = argument_return_depth;
    my outer_inference = outer;
    ty_flow_restore(me, isolation);
    ty_context_root_leave(entered);
}

bool ty_settle_function_context(Checker* me, FunctionImpl const* function) {
    if (!function_unsettled(me, function)) return false;
    settle_function(me, function, 0);
    return !function_unsettled(me, function);
}

Signature const* ty_function_assigned_contextual_signature(Checker* me, FunctionImpl const* function) {
    Signature const* sig = ty_link_get(me, KEY_CONTEXT_CHECKED, function);
    return sig == &context_checked_none ? nullptr : sig;
}

bool ty_function_checked_without_context(Checker* me, FunctionImpl const* function) {
    return ty_link_get(me, KEY_CONTEXT_CHECKED, function) == &context_checked_none;
}

// The contextual type of a function's return expressions: its contextual signature's return type, as the last check of
// the function saw it (tsc goes through the resolved signature of the call it is an argument of), instantiated where it
// is looked at while the call infers (ty_instantiate_contextual_type())
// When that signature is the function's own (the call it is an argument of inferred its parameter's type from it:
// `id(() => "x")`), its unit return type is the literal of its context and stays (tsc's getReturnTypeFromBody,
// `contextualSignature === getSignatureFromDeclaration(func)`)
static void set_contextual_return(Checker* me, FunctionImpl const* function, Signature const* sig) {
    if (function->ret_explicit) return;
    // (The signature: its return type is read when asked, tsc's getContextualReturnType. Resolved now unless it is the
    // function's own, which is what tsc's first contextual check does, reporting what goes wrong there: TS2590)
    if (sig->function != function) signature_return_type(me, sig);
    if (ty_link_get(me, KEY_CONTEXT_RETURN, function)) return;
    ty_link_set(me, KEY_CONTEXT_RETURN, function, sig);
}

// Where a function expression's contextual type comes from when that is a call's argument (tsc's getContextualType
// walking up to getContextualTypeForArgumentAtIndex): the argument, then a path down from it. Read when asked, through
// the call's resolved signature (tsc's links.resolvedSignature), so a return type inferred late sees the context as
// it is then
typedef struct ContextAnchor ContextAnchor;
struct ContextAnchor {
    enum { ANCHOR_ARG, ANCHOR_PROP, ANCHOR_ELEM, ANCHOR_RETURN } kind;
    u32 index; // ARG, ELEM
    CallExpr const* call; // ARG
    Expr const* literal; // PROP: the object literal, ELEM: the array literal
    ObjectExprItem const* item; // PROP: the property
    FunctionImpl const* function; // RETURN: the function whose return expression it is
    struct ContextAnchor const* parent; // PROP, ELEM
    // Once the call is resolved the derivation is for good, unless it went through a return type in flight
    bool cached;
    Ty const* cached_type;
    // The same for the object literal at the anchor, its apparent and discriminated contextual type (ternary branches
    // share an anchor)
    Expr const* literal_cached_for;
    Ty const* literal_cached_type;
};


static void record_anchors_in(Checker* me, Expr const* expr, ContextAnchor const* anchor, u32 depth);
static bool anchors_may_reach(Expr const* expr);

static void record_anchors_in_block(Checker* me, Block const* block, ContextAnchor const* anchor, u32 depth) {
    for_each (s, vec_statement, &block->statements) {
        if (s->tag == STATEMENT_RETURN_EXPR) record_anchors_in(me, &s->expr, anchor, depth + 1);
        else if (s->tag == STATEMENT_BLOCK) record_anchors_in_block(me, &s->block, anchor, depth + 1);
    }
}

static ContextAnchor* new_anchor(Checker* me, ContextAnchor a) {
    ContextAnchor* ret = arena_new(&my arena, ContextAnchor);
    *ret = a;
    return ret;
}

static void record_anchors_in(Checker* me, Expr const* expr, ContextAnchor const* anchor, u32 depth) {
    if (depth > 16) return;
    switch (expr->tag) {
        case EXPR_FUNCTION: {
            FunctionImpl const* function = expr->function;
            if (ty_link_get(me, KEY_FUNCTION_ANCHOR, function)) return;
            ty_link_set(me, KEY_FUNCTION_ANCHOR, function, anchor);
            // (A generator's return expressions are about its TReturn: ty_generator_context())
            if (function->ret_explicit || function->is_generator) return;
            record_anchors_in_block(me, function_impl_body(function), new_anchor(me, (ContextAnchor){ .kind = ANCHOR_RETURN, .function = function }), depth);
            return;
        }
        case EXPR_OBJECT: {
            if (ty_link_get(me, KEY_LITERAL_ANCHOR, expr)) return;
            ty_link_set(me, KEY_LITERAL_ANCHOR, expr, anchor);
            for_each (i, vec_object_expr_item, &expr->object) {
                if (i->tag != OBJECT_ITEM_PROPERTY && i->tag != OBJECT_ITEM_METHOD) continue;
                if (object_item_is_private_name(i)) continue;
                if (!anchors_may_reach(&i->value)) continue;
                record_anchors_in(me, &i->value, new_anchor(me, (ContextAnchor){ .kind = ANCHOR_PROP, .literal = expr, .item = i,
                    .parent = anchor }), depth + 1);
            }
            return;
        }
        case EXPR_ARRAY:
            for_each (i, vec_expr, &expr->array) {
                if (i->tag == EXPR_TERNARY || !anchors_may_reach(i)) continue;
                record_anchors_in(me, i, new_anchor(me, (ContextAnchor){ .kind = ANCHOR_ELEM, .literal = expr,
                    .index = (u32)i_index, .parent = anchor }), depth + 1);
            }
            return;
        case EXPR_TERNARY:
            record_anchors_in(me, &expr->ternary->yes, anchor, depth + 1);
            record_anchors_in(me, &expr->ternary->no, anchor, depth + 1);
            return;
        case EXPR_AS_TYPE:
            // (A const assertion's operand has the assertion's contextual type: tsc's getContextualType of an
            // AsExpression with a `const` type, tryFindWhenConstTypeReference)
            if (as_type_is_const(expr->as_type)) record_anchors_in(me, &expr->as_type->expr, anchor, depth + 1);
            return;
        default: return;
    }
}

// What record_anchors_in() goes into (a const assertion: its operand)
static bool anchors_may_reach(Expr const* expr) {
    return expr->tag == EXPR_FUNCTION || expr->tag == EXPR_OBJECT || expr->tag == EXPR_ARRAY || expr->tag == EXPR_TERNARY ||
        (expr->tag == EXPR_AS_TYPE && as_type_is_const(expr->as_type));
}

void ty_record_call_anchors(Checker* me, CallExpr const* call) {
    for_each (i, vec_expr, &call->args) {
        if (i->tag == EXPR_SPREAD) break;
        if (!anchors_may_reach(i)) continue;
        record_anchors_in(me, i, new_anchor(me, (ContextAnchor){ .kind = ANCHOR_ARG, .call = call, .index = (u32)i_index }), 0);
    }
}

static Ty const* anchor_context(Checker* me, ContextAnchor const* anchor, AnchorRead* read, u32 depth);

// A union contextual type whose signatures make a composite with one whose return type is being inferred (tsc's
// isResolvingReturnTypeOfSignature of compositeSignatures). *own: `function` is among them
static bool composite_in_flight(Checker* me, Ty const* contextual, FunctionImpl const* function, bool* own) {
    *own = false;
    if (!contextual || contextual->kind != TY_UNION) return false;
    u32 functions = 0;
    bool in_flight = false;
    for (u32 i = 0; i < contextual->composite.types.len; i++) {
        Ty const* member = contextual->composite.types.data[i];
        if (!(member->flags & (TYF_OBJECT | TYF_INTERSECTION)) || !ty_members(me, member)->call_count) continue;
        functions++;
        FunctionImpl const* member_function = member->kind == TY_ANONYMOUS ? member->anonymous.function : nullptr;
        if (member_function && member_function->has_body && ty_function_return_in_flight(me, member_function)) {
            in_flight = true;
            *own |= member_function == function;
        }
    }
    if (functions > 1 && in_flight) return true;
    *own = false;
    return false;
}

// Null for a composite with a signature whose return type is being inferred (*composite_own: one is its own)
static Signature const* anchored_signature(Checker* me, FunctionImpl const* function, AnchorRead* read, u32 depth, bool* composite_own) {
    *composite_own = false;
    ContextAnchor const* anchor = ty_link_get(me, KEY_FUNCTION_ANCHOR, function);
    if (!anchor || depth > 16) return nullptr;
    Ty const* contextual = anchor_context(me, anchor, read, depth + 1);
    if (!contextual) return nullptr;
    if (composite_in_flight(me, contextual, function, composite_own)) {
        read->transient = true;
        return nullptr;
    }
    return contextual_signature(me, contextual, function, 0);
}

static Ty const* derive_anchor_context(Checker* me, ContextAnchor const* anchor, AnchorRead* read, u32 depth);

// Null with read->anchored false: not anchored, or the call isn't resolved (the caller goes by what it has); with
// read->anchored true: no contextual type there
static Ty const* anchor_context(Checker* me, ContextAnchor const* anchor, AnchorRead* read, u32 depth) {
    if (anchor->cached) {
        read->anchored = true;
        return anchor->cached_type;
    }
    AnchorRead here = {0};
    Ty const* ret = derive_anchor_context(me, anchor, &here, depth);
    if (here.anchored && !here.transient) {
        ContextAnchor* cache = (ContextAnchor*)anchor;
        cache->cached = true;
        cache->cached_type = ret;
    }
    read->anchored |= here.anchored;
    read->transient |= here.transient;
    return ret;
}

// The contextual type of the object literal at the anchor, as check_object() narrows it
static Ty const* anchor_literal_context(Checker* me, ContextAnchor const* anchor, Expr const* literal, AnchorRead* read, u32 depth) {
    if (anchor->literal_cached_for == literal) {
        read->anchored = true;
        return anchor->literal_cached_type;
    }
    AnchorRead here = {0};
    Ty const* type = anchor_context(me, anchor, &here, depth);
    if (type) {
        type = apparent_literal_context(me, type);
        if (type->kind == TY_UNION) type = discriminate_by_object_members(me, type, &literal->object);
    }
    if (here.anchored && !here.transient) {
        ContextAnchor* cache = (ContextAnchor*)anchor;
        cache->literal_cached_for = literal;
        cache->literal_cached_type = type;
    }
    read->anchored |= here.anchored;
    read->transient |= here.transient;
    return type;
}

static Ty const* derive_anchor_context(Checker* me, ContextAnchor const* anchor, AnchorRead* read, u32 depth) {
    switch (anchor->kind) {
        case ANCHOR_ARG: {
            Signature const* sig = nullptr;
            TyMapper const* mapper = nullptr;
            Ty const* checked = nullptr;
            if (my reporting_candidate.call && my reporting_candidate.call == anchor->call) {
                // (Checked against the candidate a failed resolution reports: for now, see report_unfit_argument())
                read->anchored = true;
                read->transient = true;
                Ty const* type = signature_type_at(me, my reporting_candidate.sig, anchor->index, false);
                return type && my reporting_candidate.mapper ? ty_instantiate(me, type, my reporting_candidate.mapper) : type;
            }
            if (!ty_call_resolved_candidate(me, anchor->call, anchor->index, &sig, &mapper, &checked)) return nullptr;
            read->anchored = true;
            if (checked) return checked;
            Ty const* type = signature_type_at(me, sig, anchor->index, false);
            return type && mapper ? ty_instantiate(me, type, mapper) : type;
        }
        case ANCHOR_PROP: {
            Ty const* parent = anchor_literal_context(me, anchor->parent, anchor->literal, read, depth + 1);
            if (!parent) return nullptr;
            ObjectExprItem const* item = anchor->item;
            if (item->computed_key) return contextual_prop_type(me, parent, ty_expr_type(me, item->computed_key));
            return contextual_named_prop_type(me, parent, checker_atom(me, cstr_sv(&item->name_string)));
        }
        case ANCHOR_ELEM: {
            Ty const* parent = anchor_context(me, anchor->parent, read, depth + 1);
            return parent ? contextual_array_element_type(me, parent, anchor->literal, anchor->index) : nullptr;
        }
        case ANCHOR_RETURN: {
            FunctionImpl const* function = anchor->function;
            bool composite_own;
            Signature const* sig = anchored_signature(me, function, read, depth + 1, &composite_own);
            if (!sig) return nullptr;
            // (Its own: none while that is being inferred, tsc's isResolvingReturnTypeOfSignature, so not for good
            // until it is known)
            if (sig->function == function && !ty_function_return_known(me, function)) {
                read->transient = true;
                if (ty_function_return_in_flight(me, function)) return nullptr;
            }
            Ty const* ret = signature_return_type(me, sig);
            // (What function_impl_enter_return_context() makes of it)
            if (function->is_async) return ret ? ty_async_return_expressions_context(me, ret) : nullptr;
            return ret;
        }
    }
    return nullptr;
}

// The contextual type of an object literal: through its anchor once the call is resolved (tsc's getContextualType then
// goes through the resolved signature), else what its last check had (*inference: the OuterInference serial then)
static Ty const* literal_context_now(Checker* me, Expr const* literal, u64* inference) {
    ContextAnchor const* anchor = ty_link_get(me, KEY_LITERAL_ANCHOR, literal);
    if (anchor) {
        AnchorRead read = {0};
        Ty const* type = anchor_literal_context(me, anchor, literal, &read, 0);
        if (read.anchored) {
            *inference = 0;
            return type;
        }
    }
    *inference = (u64)(uintptr_t)intern_get(&my links, (u64[]){ KEY_LITERAL_INFERENCE, (u64)(uintptr_t)literal }, 2);
    return ty_link_get(me, KEY_LITERAL_CONTEXT, literal);
}

// `evaluating`: the parameter whose type evaluation asked for this, which takes its contextual type like the others
// `raw`: the contextual type before a generic call's inference instantiated it (null: the same)
static void assign_contextual_signature_as(
    Checker* me, FunctionImpl const* function, Ty const* contextual, Signature const* given, InitializedBinding const* evaluating,
    Ty const* raw_contextual
) {
    Signature const* checked = ty_link_get(me, KEY_CONTEXT_CHECKED, function);
    if (checked == &context_checked_none) {
        // Its parameters have no contextual types for good, but its `this` is whatever the contextual signature says when
        // it is looked at (tsc's getContextualThisParameterType asks getContextualSignature each time): the last one, of
        // the overload a call settles on after a candidate that gave none checked it first
        Signature const* late = given ? given : contextual_signature(me, ty_instantiate_contextual_type_for(me, contextual, true), function, 0);
        // (None from this one, nothing: a `this` an earlier candidate's signature gave doesn't outlive it, Vue 2's
        // `extend` overloads, the second's DefaultMethods `(this: V)` tried and then the first settled on)
        Ty const* late_this = late ? sig_res(me, late)->this_type : nullptr;
        u64 this_key[2] = { KEY_CONTEXT_THIS, (u64)(uintptr_t)function };
        if (late_this || intern_get(&my links, this_key, 2)) *intern_slot(me, &my links, this_key, 2) = late_this;
        return;
    }
    if (checked) return;
    Signature const* sig = given ? given : contextual_signature(me, ty_instantiate_contextual_type_for(me, contextual, true), function, 0);
    if (!sig) return;
    // The type parameters of the generic call this is an argument of are fixed now (a contextual type that left them
    // generic because nothing was inferred for them: `unknown`)
    Signature const* unfixed = sig;
    sig = ty_fix_contextual_signature(me, sig, function);
    ty_link_set(me, KEY_CONTEXT_CHECKED, function, sig);
    if (sig_res(me, sig)->this_type) {
        u64 this_key[2] = { KEY_CONTEXT_THIS, (u64)(uintptr_t)function };
        *intern_slot(me, &my links, this_key, 2) = sig_res(me, sig)->this_type;
    }
    // A function with type parameters of its own isn't context sensitive (unless it's a generator yielding a context
    // sensitive expression), its unannotated parameters stay implicitly any (tsc's getContextuallyTypedParameterType
    // asks isContextSensitiveFunctionOrObjectLiteralMethod first); its `this` and its return still take the contextual
    // signature's. A JSDoc @type's signature types them all the same
    bool own_type_params = vec_generic_item_size(&function->generic.items) && !function_impl_rare(function)->jsdoc_signature_type &&
        !ty_function_is_context_sensitive(function);
    u32 index = 0;
    FunctionImpl const* outer_assigning = my assigning_context_function;
    Signature const* outer_assigning_signature = my assigning_context_signature;
    my assigning_context_function = function;
    my assigning_context_signature = unfixed;
    for_each (i, vec_function_arg, &function->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (own_type_params && !is_this) continue;
        if (is_this) {
            // An unannotated `this` parameter takes the contextual signature's (tsc's assignContextualParameterTypes)
            if (!i->initializer.binding.has_explicit_type && sig_res(me, sig)->this_type) {
                ty_link_set(me, KEY_CONTEXT_PARAM, &i->initializer, sig_res(me, sig)->this_type);
            }
            continue;
        }
        u32 at = index++;
        // A parameter with an initializer takes the contextual type too (tsc's getContextuallyTypedParameterType)
        if (i->initializer.binding.has_explicit_type) continue;
        LazyMemo* param_memo = lazy_type_state_peek((LazyType*)&i->initializer.lazy_type);
        if (param_memo && param_memo->evaluation_started && &i->initializer != evaluating) continue;
        Ty const* type = signature_type_at(me, sig, at, i->rest);
        if (!type && !given) {
            // Nothing there once instantiated (`(...args: TArgs)` with TArgs inferred as []): the parameter's own type
            // asks the contextual signature again, uninstantiated (tsc's assignParameterType without a type ->
            // getContextuallyTypedParameterType), `TArgs[0]`
            Signature const* raw = unfixed != sig ? unfixed : contextual_signature(me, raw_contextual ? raw_contextual : contextual, function, 0);
            if (raw && raw != sig) type = signature_type_at(me, raw, at, i->rest);
        }
        if (!type) continue;
        Ty const* unwidened = nullptr;
        if (initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY) {
            // An initializer that doesn't fit the contextual type but is wider than it wins (tsc's
            // assignContextualParameterTypes): `(max = 3) => {}` against `(max: 1 | 2) => void` takes a number
            // (Checked with that contextual type: tsc's checkDeclarationInitializer, getContextuallyTypedParameterType)
            Ty const* initializer = ty_check_expr(me, initialized_binding_value(&i->initializer), type);
            if (!ty_is_related(me, initializer, type, TY_RELATION_ASSIGNABLE)) {
                Ty const* widened = ty_widened_for_declaration(me, initializer, true);
                if (ty_is_related(me, type, widened, TY_RELATION_ASSIGNABLE)) {
                    // (Not under a generic call's inference: tsc's parameter type was fixed by its first assignment,
                    // which the inferred contextual type then repeats)
                    if ((!raw_contextual || raw_contextual == contextual) && sig == unfixed) unwidened = type;
                    type = widened;
                }
            }
        }
        // A destructured parameter inference gave nothing but `unknown` falls back to its pattern's type (tsc's
        // assignParameterType)
        if (type == ty_intrinsic(me, TY_UNKNOWN_) &&
            (i->initializer.binding.tag == BINDING_OBJECT || i->initializer.binding.tag == BINDING_ARRAY)
        ) {
            type = ty_binding_pattern_type(me, &i->initializer.binding, false);
            // (tsc's getTypeFromObjectBindingPattern is an object literal type, isObjectLiteralType, and this one isn't
            // widened: as the target of a relation it has no room for other properties, propertiesRelatedTo. A copy of its
            // own, the same pattern elsewhere is widened first)
            if (type->kind == TY_ANONYMOUS) {
                type = ty_pattern_unclone(me, type);
                *intern_slot(me, &my links, (u64[]){ KEY_PATTERN_OBJECT_LITERAL, type->id }, 2) = type;
            }
            u64 pattern_key[2] = { KEY_CONTEXT_PARAM_PATTERN, (u64)(uintptr_t)&i->initializer.binding };
            *intern_slot(me, &my links, pattern_key, 2) = type;
        }
        ty_link_set(me, KEY_CONTEXT_PARAM, &i->initializer, type);
        // (Per assignment: a later one that didn't widen clears what an earlier pass left)
        if (unwidened || intern_get(&my links, (u64[]){ KEY_CONTEXT_PARAM_UNWIDENED, (u64)(uintptr_t)&i->initializer }, 2)) {
            *intern_slot(me, &my links, (u64[]){ KEY_CONTEXT_PARAM_UNWIDENED, (u64)(uintptr_t)&i->initializer }, 2) = unwidened;
        }
    }
    my assigning_context_function = outer_assigning;
    my assigning_context_signature = outer_assigning_signature;
    set_contextual_return(me, function, unfixed);
}

void ty_function_apply_jsdoc_type(Checker* me, FunctionImpl const* function, InitializedBinding const* evaluating) {
    if (!function_impl_rare(function)->jsdoc_signature_type) return;
    // A declaration's parameters take the types at their positions whatever its arity (tsc's getParameterTypeOfTypeTag
    // through getSignatureOfTypeTag, not a contextual signature)
    Ty const* type = ty_of_node(function_impl_rare(function)->jsdoc_signature_type);
    assign_contextual_signature_as(me, function, type, nullptr, evaluating, nullptr);
    if (ty_function_assigned_contextual_signature(me, function) || function->is_expression || !(type->flags & TYF_OBJECT)) return;
    // (One the arity rules out as a contextual signature: more parameters than it has)
    Members const* members = ty_members(me, type);
    if (members->call_count == 1 && !members->construct_count && !sig_res(me, members->call[0])->type_params.len) {
        assign_contextual_signature_as(me, function, type, members->call[0], evaluating, nullptr);
    }
}

// tsc's hasContextSensitiveParameters
static bool has_context_sensitive_params(FunctionImpl const* function) {
    if (vec_generic_item_size(&function->generic.items)) return false;
    for_each (i, vec_function_arg, &function->args) {
        if (!i->initializer.binding.has_explicit_type) return true;
    }
    // A function expression's `this` is contextually typed too, unless it declares one or doesn't mention `this` (tsc 6's
    // hasContextSensitiveParameters: NodeFlags.ContainsThis)
    if (!function_impl_is_arrow(function)) {
        FunctionArg const* first = vec_function_arg_size(&function->args) ? vec_function_arg_at(&function->args, 0) : nullptr;
        if (!(first && first->initializer.binding.tag == BINDING_IDENTIFIER &&
            span_equals(first->initializer.binding.content_span, "this"))
        ) {
            return function_impl_contains_this(function);
        }
    }
    return false;
}

bool ty_function_is_context_sensitive(FunctionImpl const* function) {
    bool generic = vec_generic_item_size(&function->generic.items);
    if (!generic && has_context_sensitive_params(function)) return true;
    // So is one whose returned expression is (tsc's hasContextSensitiveReturnExpression): `() => x => x.length`
    if (!generic && !function->ret_explicit && function->has_body &&
        statements_any_return(&function_impl_body(function)->statements, ty_expr_is_context_sensitive)
    ) {
        return true;
    }
    // Or a generator one of whose yielded expressions is (tsc 6's hasContextSensitiveYieldExpression, whatever the
    // return type annotation and type parameters): `function* () { yield x => x.length }`
    if (function->is_generator && function->has_body) {
        Expr const* const* yields = function_impl_yields(function);
        for (u32 i = 0; i < function_impl_yield_count(function); i++) {
            if (ty_expr_is_context_sensitive(&yields[i]->unary_op->expr)) return true;
        }
    }
    return false;
}

bool ty_expr_is_context_sensitive(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_FUNCTION: return ty_function_is_context_sensitive(expr->function);
        case EXPR_OBJECT:
            // (Not through its accessors: tsc's isContextSensitive only looks at property assignments, methods and
            // shorthands, so `{ items: [true], get f() { return this.items } }` is inferred from in one pass)
            for_each (i, vec_object_expr_item, &expr->object) {
                if (i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER) continue;
                if (ty_expr_is_context_sensitive(&i->value)) return true;
            }
            return false;
        case EXPR_ARRAY:
            for_each (i, vec_expr, &expr->array) {
                if (ty_expr_is_context_sensitive(i)) return true;
            }
            return false;
        case EXPR_TERNARY:
            return ty_expr_is_context_sensitive(&expr->ternary->yes) || ty_expr_is_context_sensitive(&expr->ternary->no);
        case EXPR_BINARY_OP:
            return (expr->binary_op->op == BINARY_OP_OR_OR || expr->binary_op->op == BINARY_OP_EH_EH) &&
                (ty_expr_is_context_sensitive(&expr->binary_op->left) || ty_expr_is_context_sensitive(&expr->binary_op->right));
        case EXPR_JSX_ATTRIBUTES: {
            // Its attributes, and the children of its element
            for_each (i, vec_jsx_attribute, &expr->jsx_attributes) {
                if (i->tag == JSX_ATTRIBUTE_EXPR && ty_expr_is_context_sensitive(&i->value)) return true;
            }
            for_each (i, vec_jsx_child, &jsx_attributes_element(expr)->children) {
                if (i->tag == JSX_CHILD_EXPR && !i->spread && ty_expr_is_context_sensitive(&i->expr)) return true;
            }
            return false;
        }
        default:
            return false;
    }
}

// Identifiers

// An interface merged into the class that extends something (tsc's getBaseTypes of the class type has its bases)
static bool merged_interface_extends(Symbol const* symbol) {
    for (u32 d = 0; symbol && d < symbol->decl_count; d++) {
        if (symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface = symbol->decls[d].node;
        if (vec_spanned_type_size(&interface->extends)) return true;
    }
    return false;
}

// The first base an interface merged into a class extends that isn't the error type (tsc's
// resolveBaseTypesOfInterface, after a class base that is none). Null if none
static Ty const* merged_interface_base(Checker* me, Symbol const* symbol) {
    Ty const* declared = symbol ? ty_declared_type_of(me, symbol) : nullptr;
    LowerCtx ctx = { .generic = declared && declared->kind == TY_DECLARED &&
        (declared->declared.type_params.len || declared->declared.this_param) };
    for (u32 d = 0; symbol && d < symbol->decl_count; d++) {
        if (symbol->decls[d].kind != SYMBOL_DECL_INTERFACE) continue;
        InterfaceDecl const* interface = symbol->decls[d].node;
        for_each (extends, vec_spanned_type, &interface->extends) {
            Ty const* base = ty_reduced(me, ty_from_node(me, &extends->type, ctx));
            if (base->flags & TYF_ERROR) continue;
            if (base->flags & (TYF_OBJECT | TYF_INTERSECTION | TYF_ANY)) return base;
        }
    }
    return nullptr;
}

// tsc's checkSuperExpression: for a property access the base class instance with `this` as this-argument, in a static
// member the base constructor; the callee of `super(...)` is the base constructor
static Ty const* super_type(Checker* me, IdentifierExpr const* identifier) {
    ClassDecl const* class_decl = identifier->target ? symbol_find_node(identifier->target, SYMBOL_DECL_CLASS) : nullptr;
    // Where it can't be (TS2335 & co.) it's the error type, in an object literal's method any (checkSuperExpression)
    if (identifier->super_invalid) return ty_intrinsic(me, TY_ERROR);
    if (!class_decl || class_decl->extends.tag == EXPR_IMPLICIT_ANY) return any_type(me);
    // `extends null`: `super()` is an error of its own (TS17005), `super.x` reads null (tsc's checkSuperExpression)
    if (class_decl->extends.tag == EXPR_LITERAL && class_decl->extends.literal.tag == VALUE_NULL) {
        return identifier->super_member ? ty_intrinsic(me, TY_NULL_WIDENING) : ty_intrinsic(me, TY_ERROR);
    }
    // (No base type, the base class got the wrong number of type arguments: tsc's checkSuperExpression. Unless an
    // interface merged into the class extends something: getBaseTypes has that, and `super` is the base constructor)
    if (!class_decl_base_arity_fits(class_decl) && !merged_interface_extends(identifier->target)) return ty_intrinsic(me, TY_ERROR);
    if (!identifier->super_member) {
        Expr const* extends = &class_decl->extends;
        if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
        Ty const* base = ty_expr_type(me, extends);
        // Not a constructor type (TS2507): the error type (tsc's getBaseConstructorTypeOfClass)
        if (base->kind == TY_ANONYMOUS && !ty_members(me, base)->construct_count) return ty_intrinsic(me, TY_ERROR);
        Ty const* apparent_base = ty_apparent_type(me, base);
        if (apparent_base->kind == TY_UNION && !(apparent_base->flags & TYF_BOOLEAN)) {
            u32 count = 0;
            ty_union_signatures(me, apparent_base->composite.types, true, &count);
            if (!count) return ty_intrinsic(me, TY_ERROR);
        }
        // (Nor is one whose construct signatures don't take the type arguments, TS2508: no base type. But a merged
        // interface's bases are base types too, and then `super` is the base constructor, getBaseConstructorTypeOfClass)
        if (ty_class_base_constructor_unfit(me, class_decl)) {
            bool constructs = (apparent_base->flags & (TYF_OBJECT | TYF_INTERSECTION)) && ty_members(me, apparent_base)->construct_count;
            if (!constructs || !merged_interface_extends(identifier->target)) return ty_intrinsic(me, TY_ERROR);
        }
        return base;
    }
    if (identifier->this_kind == THIS_CLASS_STATIC) return ty_expr_type(me, &class_decl->extends);
    Ty const* base = ty_class_base_instance(me, class_decl);
    if (!base) {
        // (Extending any or the error type, e.g. an unresolved import, it's the base type as it is: tsc's
        // resolveBaseTypesOfClass. So `super.m<T>()` on the error type isn't TS2347)
        Expr const* extends = &class_decl->extends;
        if (extends->tag == EXPR_GENERIC_PARAMS) extends = &extends->generic_params->expr;
        Ty const* constructor = ty_expr_type(me, extends);
        // (A base constructor of the error type is no base, but an interface merged into the class may extend
        // something: the first valid one of those is getBaseTypes()[0], what tsc's checkSuperExpression reads)
        if (constructor->flags & TYF_ERROR) {
            Ty const* merged = merged_interface_base(me, identifier->target);
            if (merged) {
                Ty const* declared = ty_declared_type_of(me, identifier->target);
                Ty const* this_param = declared->kind == TY_DECLARED ? declared->declared.this_param : nullptr;
                return this_param ? ty_with_this_arg(me, merged, this_param) : merged;
            }
        }
        if (constructor->flags & TYF_ANY) return constructor;
        // (No construct signature takes the type arguments, TS2508, or none at all: no base, the error type)
        return ty_class_base_constructor_unfit(me, class_decl) ? ty_intrinsic(me, TY_ERROR) : any_type(me);
    }
    Ty const* declared = ty_declared_type_of(me, identifier->target);
    Ty const* this_param = declared->kind == TY_DECLARED ? declared->declared.this_param : nullptr;
    return this_param ? ty_with_this_arg(me, base, this_param) : base;
}

Ty const* ty_identifier_reference_type(Checker* me, IdentifierExpr const* identifier) {
    Symbol const* decl = identifier->target;
    // (`import * as ns` of an `export =` module can be more than what it assigns: see ty_type_of_symbol())
    if (identifier->symbol && (identifier->symbol->flags & SYMBOL_ALIAS) && identifier->symbol != decl &&
        !(identifier->symbol->alias && identifier->symbol->alias->export_value == decl)
    ) {
        decl = identifier->symbol;
    }
    if (identifier->keyword == KEYWORD_IDENTIFIER_SUPER) return super_type(me, identifier);
    switch (identifier->this_kind) {
        case THIS_NONE: break;
        case THIS_ANY: {
            if (identifier->this_in_parameter_default) return any_type(me);
            FunctionArg const* paired_this = identifier_this_function(identifier) ? function_impl_paired_this(identifier_this_function(identifier)) : nullptr;
            if (paired_this) return ty_type_of_symbol(me, paired_this->initializer.binding.symbol);
            Ty const* contextual = identifier_this_function(identifier) ? ty_contextual_this_type(me, identifier_this_function(identifier)) : nullptr;
            return contextual ? contextual : any_type(me);
        }
        case THIS_PARAMETER: break;
        case THIS_CLASS_STATIC: return ty_type_of_symbol(me, decl); // (With what a namespace merges in)
        case THIS_INTERFACE: return any_type(me);
        case THIS_GLOBAL: return my binder->global_this ? ty_type_of_symbol(me, my binder->global_this) : any_type(me);
        case THIS_NAMESPACE: case THIS_ENUM: return any_type(me);
        case THIS_CLASS_INSTANCE: {
            // The class's `this` type (else the instance type, for a generic class applied to its own parameters)
            Ty const* declared = ty_declared_type_of(me, decl);
            if (declared->kind == TY_DECLARED && declared->declared.this_param) return declared->declared.this_param;
            if (declared->kind == TY_DECLARED && declared->declared.type_params.len) {
                return ty_reference(me, declared, declared->declared.type_params);
            }
            return declared;
        }
    }
    if (identifier->keyword == KEYWORD_IDENTIFIER_NEW_TARGET) {
        // tsc's checkNewTargetMetaProperty(): the class of a constructor, else the function
        if (decl) return ty_type_of_symbol(me, decl);
        return identifier_this_function(identifier) ? ty_function_type(me, identifier_this_function(identifier)) : ty_intrinsic(me, TY_ERROR);
    }
    if (!decl && identifier->is_arguments && !identifier->invalid_initializer_reference) {
        Symbol const* arguments = program_get_global(my program, c_sv("IArguments"), SYMBOL_TYPE);
        return arguments ? ty_declared_type_of(me, arguments) : ty_empty_object_type(me); // (tsc's getGlobalType fallback)
    }
    if (!decl) {
        // TODO: `this` outside of anything that has one is `typeof globalThis` (or implicitly any)
        // (A name that wasn't found is tsc's errorType)
        return identifier->keyword == KEYWORD_IDENTIFIER_THIS ? ty_intrinsic(me, TY_UNDEFINED) : ty_intrinsic(me, TY_ERROR);
    }
    // tsc's checkIdentifier: assigning to what isn't a variable, or to a constant, is an error of type errorType
    // (A JavaScript file's `exports = ...` is no error)
    if (identifier_is_invalid_assignment_target(identifier) &&
        !(span_source(identifier_expr_name(identifier)) && span_source(identifier_expr_name(identifier))->is_js && (identifier->symbol->flags & SYMBOL_VALUE_MODULE))
    ) {
        return ty_intrinsic(me, TY_ERROR);
    }
    return ty_type_of_symbol(me, decl);
}

static Ty const* base_type_of_literal(Checker* me, Ty const* ty) { return ty_base_type_of_literal(me, ty); }

static bool is_generic_with_union_constraint(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_generic_with_union_constraint(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    if (ty->kind != TY_TYPE_PARAM && !(ty->flags & TYF_DEFERRED)) return false;
    Ty const* constraint = ty_base_constraint(me, ty);
    return constraint && constraint != ty && ((constraint->flags & TYF_NULLABLE) || constraint->kind == TY_UNION);
}

static bool is_generic_type(Checker* me, Ty const* ty);

bool ty_is_generic_type(Checker* me, Ty const* ty) {
    return is_generic_type(me, ty);
}

bool ty_function_context_is_self(Checker* me, FunctionImpl const* function) {
    // (Its return type was inferred with its own signature as the context)
    ty_function_return_type(me, function);
    return intern_get(&my links, (u64[]){ KEY_CONTEXT_RETURN_SELF, (u64)(uintptr_t)function }, 2) != nullptr;
}

// tsc's shouldReportErrorsFromWideningWithContextualSignature for WideningKind.FunctionReturn (GeneratorYield with
// `yield`): a function expression's contextual signature, if it has one, has a generic return type (a generator's: the
// return or yield type it iterates with)
static bool widening_reportable_with(Checker* me, FunctionImpl const* function, Signature const* sig, bool yield) {
    if (!sig) return true;
    Ty const* ret = signature_return_type(me, sig);
    if (function->is_generator) {
        TyIterationTypes const* types = ty_generator_return_iteration_types(me, ret, function->is_async);
        if (yield) return types && types->yield_type && is_generic_type(me, types->yield_type);
        if (types && types->return_type) ret = types->return_type;
    } else if (function->is_async) {
        Ty const* awaited = ty_awaited_no_alias(me, ret);
        if (awaited) ret = awaited;
    }
    return is_generic_type(me, ret);
}

// The contextual signature a function expression has now (tsc's getContextualSignatureForFunctionLikeDeclaration):
// through the resolved call, else the one its first contextual check had (while a call infers, its type parameters
// are still there)
static Signature const* contextual_signature_now(Checker* me, FunctionImpl const* function) {
    AnchorRead read = {0};
    bool composite_own = false;
    Signature const* sig = anchored_signature(me, function, &read, 0, &composite_own);
    if (read.anchored) return sig;
    return ty_link_get(me, KEY_CONTEXT_RETURN, function);
}

// Decided when the return type is inferred, with the contextual signature it has then (tsc's getReturnTypeFromBody
// reports widening there): 1 | reportable return << 1 | reportable yield << 2 (KEY_RETURN_REPORTABLE)
void ty_function_decide_widening_reportable(Checker* me, FunctionImpl const* function) {
    if (!function->is_expression || function->ret_explicit) return;
    if (compiler_options()->strict_null_checks || !compiler_options()->no_implicit_any) return;
    Signature const* sig = contextual_signature_now(me, function);
    // (Its own signature: that is TS7023/TS7024's circularity, ty_function_context_is_self())
    if (sig && sig->function == function && !sig_target(me, sig)) return;
    uintptr_t bits = 1 | (widening_reportable_with(me, function, sig, false) ? 2 : 0) |
        (function->is_generator && widening_reportable_with(me, function, sig, true) ? 4 : 0);
    *intern_slot(me, &my links, (u64[]){ KEY_RETURN_REPORTABLE, (u64)(uintptr_t)function }, 2) = (void const*)bits;
}

bool ty_function_widening_reportable(Checker* me, FunctionImpl const* function, bool yield) {
    uintptr_t bits = (uintptr_t)intern_get(&my links, (u64[]){ KEY_RETURN_REPORTABLE, (u64)(uintptr_t)function }, 2);
    if (bits) return bits & (yield ? 4 : 2);
    return widening_reportable_with(me, function, contextual_signature_now(me, function), yield);
}

static Ty const* base_constraint_or_type(Checker* me, Ty const* ty) {
    if (ty->kind == TY_INTERSECTION && is_generic_type(me, ty)) {
        // The intersection of its members' base constraints, without the members that have none (tsc's
        // computeBaseConstraint)
        TyList members = ty->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        panic_if(!parts, "out of memory");
        u32 len = 0;
        bool different = false;
        for (u32 i = 0; i < members.len; i++) {
            Ty const* member = members.data[i];
            bool instantiable = member->kind == TY_TYPE_PARAM || (member->flags & TYF_DEFERRED);
            Ty const* constraint = instantiable ? ty_base_constraint(me, member) : member;
            if (constraint) parts[len++] = constraint;
            different |= constraint != member;
        }
        // (Nothing changed: the intersection itself, its alias kept)
        Ty const* ret = !different ? ty : len ? ty_intersection(me, parts, len, nullptr, (TyList){0}) : ty;
        free(parts);
        return ret;
    }
    Ty const* constraint = ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED) ? ty_base_constraint(me, ty) : nullptr;
    return constraint ? constraint : ty;
}

static bool has_generic_with_union_constraint(Checker* me, Ty const* ty) {
    if (ty->kind != TY_UNION) return is_generic_with_union_constraint(me, ty);
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (is_generic_with_union_constraint(me, ty->composite.types.data[i])) return true;
    }
    return false;
}

// tsc's isGenericType: a generic object type (a type variable, a mapped type over generic keys, a generic tuple) or a
// generic index type (a type variable, `keyof`, a template or string mapping that isn't a pattern), at the top or in a
// union or intersection
static bool is_generic_type(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_generic_type(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    if (ty->kind == TY_TEMPLATE_LITERAL || ty->kind == TY_STRING_MAPPING) return !ty_is_pattern_literal(ty);
    return ty_is_generic_object(me, ty);
}

// A binding pattern's implied type (TYF_BINDING_PATTERN) of a pattern with a rest element: an object one has an
// index signature, an array one a rest element
static bool pattern_type_has_rest(Checker* me, Ty const* pattern) {
    if (ty_is_tuple(pattern)) {
        TupleShape const* shape = pattern->reference.target->declared.tuple;
        return shape->arity && (shape->flags[shape->arity - 1] & TUPLE_ELEMENT_REST);
    }
    return (pattern->flags & TYF_OBJECT) && ty_members(me, pattern)->index_count;
}

// tsc's getNarrowableTypeForReference: in a constraint position, or where the contextual type isn't generic, a generic
// type with a union constraint is narrowed as its constraint (`c && c.flags` with `c: T extends Ctx | undefined` reads
// `flags` of Ctx; `isA(x) ? takesA(x) : takesB(x)` with `x: T extends A | B`)
// tsc's someType(type, isGenericTypeWithoutNullableConstraint)
static bool has_generic_without_nullable_constraint(Checker* me, Ty const* type) {
    TyList members = type->kind == TY_UNION ? type->composite.types : (TyList){ .data = &type, .len = 1 };
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        // (An intersection if some member is: tsc's isGenericTypeWithoutNullableConstraint)
        if (member->kind == TY_INTERSECTION) {
            for (u32 j = 0; j < member->composite.types.len; j++) {
                Ty const* part = member->composite.types.data[j];
                if (part->kind != TY_UNION && has_generic_without_nullable_constraint(me, part)) return true;
            }
            continue;
        }
        if (!(member->flags & (TYF_TYPE_PARAM | TYF_DEFERRED))) continue;
        Ty const* constraint = base_constraint_or_type(me, member);
        if (!ty_can_be_null(me, constraint) && !ty_can_be_undefined(me, constraint)) return true;
    }
    return false;
}

// A generic type with a union constraint as its constraint, every member of a union (the substitution of tsc's
// getNarrowableTypeForReference)
Ty const* ty_substitute_constraints(Checker* me, Ty const* type) {
    if (!has_generic_with_union_constraint(me, type)) return type;
    if (type->kind != TY_UNION) return base_constraint_or_type(me, type);
    u32 count = type->composite.types.len;
    Ty const** mapped = malloc(sizeof(Ty const*) * count);
    panic_if(!mapped, "out of memory");
    for (u32 i = 0; i < count; i++) mapped[i] = base_constraint_or_type(me, type->composite.types.data[i]);
    Ty const* ret = ty_union(me, mapped, count, nullptr, (TyList){0});
    free(mapped);
    return ret;
}

// `element_index`: the index of `x[i]` that the reference is the object of (null for any other constraint position)
static Ty const* narrowable_type(Checker* me, bool constraint_position, Expr const* element_index, Ty const* type, Ty const* contextual) {
    if (ty_is_no_infer(type)) type = type->substitution.base;
    if (!has_generic_with_union_constraint(me, type)) return type;
    // `x[i]` with a generic i keeps a type variable of x that can't be nullable as it is (tsc's isConstraintPosition)
    if (constraint_position && element_index && has_generic_without_nullable_constraint(me, type) &&
        ty_is_generic_index(ty_expr_type(me, element_index))
    ) {
        constraint_position = false;
    }
    bool substitute = constraint_position ||
        (contextual && !my inferential_depth && !is_generic_type(me, contextual));
    if (!substitute) return type;
    return ty_substitute_constraints(me, type);
}

// A parameter's circularity through its default (tsc's reportCircularityError of its symbol): TS2502 when annotated,
// else TS7022 under noImplicitAny, at the whole parameter (symbol.valueDeclaration)
static void report_parameter_circularity(Checker* me, InitializedBinding const* initialized) {
    Binding const* binding = &initialized->binding;
    if (binding->tag != BINDING_IDENTIFIER) return;
    Span at = span_between(binding->content_span, expr_outer_span(initialized_binding_value(initialized)));
    if (binding->has_explicit_type) {
        ty_error_at(me, at, 2502, cstr_d("'", binding->content_span,
            "' is referenced directly or indirectly in its own type annotation."));
    } else if (compiler_options()->no_implicit_any) {
        ty_error_at(me, at, 7022, cstr_d("'", binding->content_span, "' implicitly has type 'any' "
            "because it does not have a type annotation and is referenced directly or indirectly in its own initializer."));
    }
}

static char const initializer_undefined_yes, initializer_undefined_no;

// Whether a parameter's default can be undefined (tsc's parameterInitializerContainsUndefined): resolved once, under a
// resolution entry of its own, so a default that reaches the parameter again through a function whose return type is
// being inferred is circular there (`(n: number | undefined = [1].map(() => n)[0])`: TS2502 and the arrow's TS7024).
// Circular, it can be undefined
static bool parameter_initializer_contains_undefined(Checker* me, InitializedBinding const* initialized) {
    void const* memo = ty_link_get(me, KEY_INITIALIZER_UNDEFINED, initialized);
    if (memo) return memo == &initializer_undefined_yes;
    if (ty_resolving_find(me, RESOLVING_INITIALIZER_UNDEFINED, initialized)) {
        ty_resolving_cycle(me, RESOLVING_INITIALIZER_UNDEFINED, initialized);
        report_parameter_circularity(me, initialized);
        return true;
    }
    ty_resolving_push(me, RESOLVING_INITIALIZER_UNDEFINED, initialized);
    // (tsc's checkDeclarationInitializer: the quick type first, `= g(x)` of a non-generic g doesn't check x)
    Expr const* initializer = initialized_binding_value(initialized);
    Ty const* value = ty_quick_expr_type(me, initializer, false);
    if (!value) value = ty_expr_type(me, initializer);
    if (ty_resolving_pop(me)) {
        report_parameter_circularity(me, initialized);
        return true;
    }
    // (`any` and `unknown` can't be undefined here, tsc's UnknownFacts)
    // (A type parameter by its constraint: tsc's hasTypeFacts(IsUndefined))
    bool may_be_undefined = ty_contains_undefined_type(value) || (value->flags & TYF_VOID) ||
        (!(value->flags & (TYF_ANY | TYF_UNKNOWN)) && ty_can_be_undefined(me, value));
    if (!ty_link_get(me, KEY_INITIALIZER_UNDEFINED, initialized)) {
        ty_link_set(me, KEY_INITIALIZER_UNDEFINED, initialized, may_be_undefined ? &initializer_undefined_yes : &initializer_undefined_no);
    }
    return may_be_undefined;
}

// `(o: T | undefined = {})` (or an optional parameter typed by the context or a JSDoc `@type` tag) is a T in the body
// unless the default can be undefined (tsc's removeOptionalityFromDeclaredType)
static Ty const* parameter_initial_type(Checker* me, Binding const* binding, Ty const* type) {
    if (!binding || !compiler_options()->strict_null_checks || !ty_contains_undefined_type(type) || !binding->source ||
        binding->source->tag != LAZY_TYPE_INITIALIZED_BINDING
    ) {
        return type;
    }
    InitializedBinding const* initialized = c_container_of(binding->source, InitializedBinding, lazy_type);
    if (initialized_binding_value(initialized)->tag == EXPR_IMPLICIT_ANY) return type;
    return parameter_initializer_contains_undefined(me, initialized) ? type : ty_without_undefined(me, type);
}

// The tail of tsc's checkIdentifier(): a variable or parameter has the type control flow gives it where it is read
// With `issue_only` it only looks for the issue: nullptr when there can't be one, before walking the flow graph (every
// identifier is asked when checked, and its type has been computed with its contextual type already)
// The flow type of a variable, except that `x!` where control flow says x is only null or undefined is the declared type
// (tsc's getFlowTypeOfReference: "the operand of a non-null assertion is never nullish")
static Ty const* non_null_flow_type(Checker* me, Expr const* expr, Ty const* declared, Ty const* initial) {
    Ty const* flow_type = ty_flow_type(me, expr, declared, initial, expr->identifier.flow);
    if (expr->identifier.non_null_operand && !(flow_type->flags & TYF_NEVER) &&
        // (getTypeWithFacts(NEUndefinedOrNull), whatever strictNullChecks says: `undefined` has no non-nullish part)
        (ty_non_nullish_part(me, flow_type)->flags & TYF_NEVER)
    ) {
        return declared;
    }
    return flow_type;
}

// A reference to a destructured element inside the initializer of the declaration it's from, in the same flow container:
// no narrowing by the pattern's other elements (tsgo 7.1's getNarrowedTypeOfSymbol, "avoid declaration circularity")
static bool in_own_root_initializer(Binding const* binding, Expr const* reference) {
    Binding const* root = binding;
    while (root->parent) root = root->parent;
    if (!root->source || root->source->tag != LAZY_TYPE_INITIALIZED_BINDING || reference->identifier.outer_reference) return false;
    Expr const* value = initialized_binding_value(c_container_of(root->source, InitializedBinding, lazy_type));
    if (value->tag == EXPR_IMPLICIT_ANY) return false;
    Span span = expr_outer_span(value), at = reference->inner_span;
    return span_source(at) == span_source(span) && span_start_ptr(at) >= span_start_ptr(span) && span_end_ptr(at) <= span_end_ptr(span);
}

// Whether a method called on the import's target (`ns.isFoo()`) may be a `this` predicate or assertion, which is all
// that narrows it there: anything but a module or namespace may (a class's static method, a function's or an enum's
// members are rarely called on it in a condition); a module's or namespace's exported functions are looked at once
// (Symbol.exports_this_predicate). Missed: a `this` predicate typed on an exported variable
static bool may_have_this_predicate(Symbol* target) {
    if (!(target->flags & (SYMBOL_VALUE_MODULE | SYMBOL_NAMESPACE_MODULE)) || (target->flags & (SYMBOL_CLASS | SYMBOL_FUNCTION | SYMBOL_ENUM))) {
        return true;
    }
    uint8_t memo = atomic_load_explicit(&target->exports_this_predicate, memory_order_relaxed);
    if (memo) return memo == 2;
    bool found = false;
    for (SymbolTable const* table = symbol_exports_table(target); table && !found; table = table->parent) {
        for (u32 i = 0; i < table->count && !found; i++) {
            Symbol const* exported = table->entries[i].symbol;
            for (u32 d = 0; exported && d < exported->decl_count && !found; d++) {
                if (exported->decls[d].kind != SYMBOL_DECL_FUNCTION) continue;
                FunctionImpl const* fn = exported->decls[d].node;
                found = fn && fn->predicate && fn->predicate->is_this;
            }
        }
    }
    atomic_store_explicit(&target->exports_this_predicate, found ? 2 : 1, memory_order_relaxed);
    return found;
}

static Ty const* identifier_flow_type(Checker* me, Expr const* expr, Ty const* contextual, IdentifierFlowIssue* issue, bool issue_only) {
    IdentifierExpr const* identifier = &expr->identifier;
    if (my contextual_pattern_count && identifier->target && (identifier->target->flags & SYMBOL_VARIABLE)) {
        Binding const* binding = symbol_find_node(identifier->target, SYMBOL_DECL_VARIABLE);
        Binding const* pattern = binding ? binding->parent : nullptr;
        for (u32 i = 0; pattern && i < my contextual_pattern_count; i++) {
            if (my contextual_patterns[i] == pattern && span_start_ptr(pattern->content_span) <= span_start_ptr(expr_outer_span(expr)) &&
                span_end_ptr(expr_outer_span(expr)) <= span_end_ptr(pattern->content_span)
            ) {
                return ty_intrinsic(me, TY_ANY);
            }
        }
    }
    Ty const* declared_type = ty_identifier_reference_type(me, identifier);
    Ty const* type = narrowable_type(me, identifier->constraint_position, identifier_element_index(identifier), declared_type, contextual);
    // What is assigned along the way is narrowable too (tsc's getInitialOrAssignedType), whether or not the declared type
    // was: tsc asks getNarrowableTypeForReference there without the check mode, so not in an inference either
    bool substituted = type != declared_type || identifier->constraint_position ||
        (contextual && !is_generic_type(me, contextual));
    Symbol const* symbol = identifier->target;
    if (identifier->keyword == KEYWORD_IDENTIFIER_THIS) {
        if (issue_only) return nullptr;
        // A function's `this` that nothing types is any, not narrowed (tsgo's tryGetThisTypeAt: no type to narrow)
        // (Not narrowable as a reference to a generic with a union constraint: tsc's tryGetThisTypeAt goes to
        // getFlowTypeOfReference with the declared `this` type, no getNarrowableTypeForReference. `this.next(...)` in a
        // method with `this: Opts<V> & VariablesOption<V>` passes that, not the constraint)
        type = declared_type;
        if (identifier->this_kind == THIS_ANY) {
            FunctionImpl const* function = identifier_this_function(identifier);
            if (!function || identifier->this_in_parameter_default || (!function_impl_paired_this(function) && !ty_contextual_this_type(me, function))) return type;
        }
        return identifier->flow ? ty_flow_type(me, expr, type, type, identifier->flow) : type;
    }
    // (An import is narrowed too, assumed initialized: tsc's checkIdentifier with isAlias, `'_pathCache' in Module` of
    // an imported class, a predicate on an imported function. Not a class declared here. Only when a narrowing flow
    // node names the alias (Symbol.flow_narrowed): imported functions and enums are on every line of TypeScript's
    // compiler, a flow walk each cost +1.4% instructions there)
    if (symbol && !identifier->keyword && identifier->symbol && identifier->symbol != symbol &&
        (identifier->symbol->flags & SYMBOL_ALIAS) && !(symbol->flags & SYMBOL_VARIABLE)
    ) {
        if (issue_only || !identifier->flow) return issue_only ? nullptr : type;
        SourceFile const* file = span_source(identifier_expr_name(identifier));
        Symbol* alias = (Symbol*)identifier->symbol;
        bool narrowed = atomic_load_explicit(&alias->flow_narrowed, memory_order_relaxed) ||
            (file && atomic_load_explicit(&file->flow_unresolved_names, memory_order_relaxed)) ||
            (atomic_load_explicit(&alias->flow_narrowed_this, memory_order_relaxed) && may_have_this_predicate((Symbol*)symbol));
        if (!narrowed) return type;
        return ty_flow_type(me, expr, type, type, identifier->flow);
    }
    if (!symbol || identifier->keyword || !(symbol->flags & SYMBOL_VARIABLE)) return type;
    if (identifier->assignment_target == FLOW_TARGET_DEFINITE) {
        return identifier->compound_like_target ? base_type_of_literal(me, type) : type;
    }
    Ty const* auto_any = ty_intrinsic(me, TY_AUTO);
    if (!identifier->flow) {
        // `typeof x` in a signature without a body (an interface member, a function type): a flow container of its own
        // with nothing in it, so the initial type (tsc's checkIdentifier at the container's start). An auto-typed
        // outer variable that may be initialized stays auto: implicitly any
        if ((type == auto_any || (type == my auto_array && type)) && identifier->outer_reference) {
            if (ty_symbol_never_initialized(symbol)) return ty_intrinsic(me, TY_UNDEFINED);
            *issue = type == auto_any ? IDENTIFIER_FLOW_IMPLICIT_ANY : IDENTIFIER_FLOW_IMPLICIT_ANY_ARRAY;
            return type == auto_any ? ty_intrinsic(me, TY_ANY) : ty_array(me, ty_intrinsic(me, TY_ANY), false);
        }
        // (Unreachable code has no flow nodes either: tsc's flow type is the declared type there, then an assignment
        // target's literals are widened like anywhere else, `v++` of `v: 0 | 1` is a number)
        return identifier->assignment_target ? base_type_of_literal(me, type) : type;
    }
    Binding const* binding = symbol_find_node(symbol, SYMBOL_DECL_VARIABLE);
    if (type == auto_any || (type == my auto_array && type)) {
        // `let x;`, `let x = []` (tsc's checkIdentifier). Undefined until something is assigned. From a closure it is
        // assumed to be assigned, to something control flow may not know: any unless every path leads to an
        // assignment in the closure. `x!` is what was assigned, non-nullable
        bool non_null = identifier->non_null_operand;
        bool assume_initialized = non_null || (identifier->outer_reference && (symbol_assignments(symbol)->is_assigned || type != auto_any));
        Ty const* initial = assume_initialized && !non_null ? type : ty_intrinsic(me, TY_UNDEFINED);
        my flow_substitutes_constraints = substituted;
        Ty const* flow_type = non_null_flow_type(me, expr, type, initial);
        // (tsc's getNonNullableType makes an intersection of any, which is the plain any)
        if (non_null && compiler_options()->strict_null_checks) {
            flow_type = flow_type == auto_any ? ty_intrinsic(me, TY_ANY) : ty_non_nullable(me, flow_type);
        }
        bool still_auto = flow_type == auto_any || flow_type == my auto_array;
        if (still_auto || (!assume_initialized && ty_contains_undefined_type(flow_type))) {
            if (!ty_is_evolving_array_operation_target(me, expr)) {
                if (still_auto) {
                    *issue = flow_type == auto_any ? IDENTIFIER_FLOW_IMPLICIT_ANY : IDENTIFIER_FLOW_IMPLICIT_ANY_ARRAY;
                    // tsc reports this whenever it checks the reference, also from inside a loop's control flow
                    // analysis where the type is still incomplete (`x = [...x, 1]` in a loop): remember it for
                    // ty_identifier_flow_issue(), which only sees the final type
                    if (!issue_only) *intern_slot(me, &my links, (u64[]){ KEY_AUTO_FLOW_ISSUE, (u64)(uintptr_t)expr }, 2) = flow_type;
                    return flow_type == auto_any ? ty_intrinsic(me, TY_ANY) : ty_array(me, ty_intrinsic(me, TY_ANY), false);
                }
            } else if (!assume_initialized && ty_contains_undefined_type(flow_type)) {
                *issue = IDENTIFIER_FLOW_UNASSIGNED; // `let x; x.push(1)`
                return type;
            }
        }
        if (issue_only && *issue == IDENTIFIER_FLOW_OK) {
            Ty const* seen = intern_get(&my links, (u64[]){ KEY_AUTO_FLOW_ISSUE, (u64)(uintptr_t)expr }, 2);
            if (seen) *issue = seen == auto_any ? IDENTIFIER_FLOW_IMPLICIT_ANY : IDENTIFIER_FLOW_IMPLICIT_ANY_ARRAY;
        }
        return identifier->assignment_target ? base_type_of_literal(me, flow_type) : flow_type;
    }
    // Where nothing says otherwise, a variable is `undefined` until something is assigned to it
    bool tsc_assumes_initialized = symbol->is_parameter || symbol->is_ambient || !compiler_options()->strict_null_checks ||
        (type->flags & (TYF_ANY | TYF_UNKNOWN | TYF_VOID)) || !binding || binding->definitely_assigned ||
        identifier->assume_initialized ||
        // A variable of another function than the one the reference is in (tsc's isOuterVariable): its initial type is
        // the declared type, which is also what the loop caches go by
        (identifier->outer_reference && !ty_symbol_never_initialized(symbol)) ||
        // A variable merged with an import (`import { X } from "./types-only"; const X = 1`): tsc's symbol is an alias
        // too (checkIdentifier's isAlias)
        symbol_find_node(symbol, SYMBOL_DECL_ALIAS);
    // (A type with undefined in it is its own optional type, tsc's getOptionalType: the same as assumed initialized)
    bool assume_initialized = tsc_assumes_initialized || ty_contains_undefined_type(type);
    if (issue_only && assume_initialized) return nullptr;
    // (The check pass asks again for the issue: what the type's own flow query found, tsc's checkIdentifier reports it
    // as it computes the type. A second flow query isn't free of effects: its loop junctions are cached complete where
    // the first one left them incomplete, and a later reference then misses tsc's incomplete (silent never) result)
    u64 issue_key[2] = { KEY_IDENTIFIER_FLOW_ISSUE, (u64)(uintptr_t)expr };
    if (issue_only) {
        void const* seen = intern_get(&my links, issue_key, 2);
        if (seen) {
            if ((uintptr_t)seen == 2) *issue = IDENTIFIER_FLOW_UNASSIGNED;
            return nullptr;
        }
    }
    // What the pattern's other elements say about the value destructured (tsc's getNarrowedTypeOfSymbol)
    // (Then made narrowable like the declared type: tsc's checkIdentifier asks getNarrowedTypeOfSymbol before
    // getNarrowableTypeForReference, so a generic element the pattern's narrowing leaves, `T & Exclude<T, "c">`, is its
    // constraint in a constraint position or under a contextual type without generics, and narrows to never)
    if (binding && binding->parent && !in_own_root_initializer(binding, expr)) {
        Ty const* narrowed = ty_destructured_narrowed_type(me, binding, identifier->flow);
        if (narrowed) {
            type = narrowable_type(me, identifier->constraint_position, identifier_element_index(identifier), narrowed, contextual);
            substituted |= type != narrowed;
        }
    } else if (binding && symbol->is_parameter) {
        Ty const* narrowed = ty_parameter_narrowed_type(me, binding, identifier->flow);
        if (narrowed) {
            type = narrowable_type(me, identifier->constraint_position, identifier_element_index(identifier), narrowed, contextual);
            substituted |= type != narrowed;
        }
    }
    Ty const* initial = symbol->is_parameter ? parameter_initial_type(me, binding, type) : type;
    // (tsc's getOptionalType of a declared type whose undefined is noUncheckedIndexedAccess's missing type, `const x =
    // a[i]`, is another union, with undefined: not the declared type, so a branch label's antecedent that has the
    // declared type doesn't end the walk early, getTypeAtFlowBranchLabel)
    if (!tsc_assumes_initialized && type->kind == TY_UNION && ty_contains_missing(me, type)) {
        Ty const* types[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
        initial = ty_union(me, types, 2, nullptr, (TyList){0});
    }
    if (!assume_initialized) {
        Ty const* types[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
        initial = ty_union(me, types, 2, nullptr, (TyList){0});
    }
    my flow_substitutes_constraints = substituted;
    Ty const* flow_type = non_null_flow_type(me, expr, type, initial);
    bool unassigned = !assume_initialized && ty_contains_undefined_type(flow_type);
    if (!issue_only && !assume_initialized) *intern_slot(me, &my links, issue_key, 2) = (void const*)(uintptr_t)(unassigned ? 2 : 1);
    if (unassigned) {
        *issue = IDENTIFIER_FLOW_UNASSIGNED;
        return type;
    }
    return identifier->assignment_target ? base_type_of_literal(me, flow_type) : flow_type;
}

IdentifierFlowIssue ty_identifier_flow_issue(Checker* me, IdentifierExpr const* identifier) {
    IdentifierFlowIssue ret = IDENTIFIER_FLOW_OK;
    identifier_flow_type(me, c_container_of(identifier, Expr, identifier), nullptr, &ret, true);
    return ret;
}

bool ty_is_import_meta(Expr const* expr) {
    if (expr->tag != EXPR_PROPERTY || expr->property->form != PROPERTY_EXPR_DOT) return false;
    Expr const* object = &expr->property->expr;
    return object->tag == EXPR_IDENTIFIER && object->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT;
}

// The type of `import.x`: ImportMeta for `import.meta`, else the error type (TS17012, tsc's checkImportMetaProperty)
Ty const* ty_import_meta_expr_type(Checker* me, Expr const* expr) {
    return csview_equals(span_sv(property_key_inner_span(expr->property)), "meta") ? ty_import_meta_type(me) : ty_intrinsic(me, TY_ERROR);
}

// `import.meta`: the global ImportMeta interface (tsc's getGlobalImportMetaType, `{}` without one)
Ty const* ty_import_meta_type(Checker* me) {
    Symbol const* symbol = program_get_global(my program, c_sv("ImportMeta"), SYMBOL_TYPE);
    if (!symbol) ty_report_missing_global_type(me, c_sv("ImportMeta")); // (reportErrors: TS2318 without a lib)
    return symbol ? ty_declared_type_of(me, symbol) : ty_empty_object_type(me);
}

// A reference that could have a key (flow.c): names and literal keys down to an identifier or `this`. Unlike the
// builder's expr_is_narrowable_reference() also through `?.`: `typeof a?.b === "string" ? a?.b : ""`
static bool is_narrowable_access(Expr const* expr) {
    while (true) {
        switch (expr->tag) {
            case EXPR_IDENTIFIER:
                return true; // (`import` too: the object of `import.meta`)
            case EXPR_PROPERTY: {
                Expr const* key = expr->property->form == PROPERTY_EXPR_BRACKET ? &expr->property->index : nullptr;
                // (`m[E.A]`: a dotted key names something too, see flow.c's is_constant_entity_name())
                if (key && key->tag != EXPR_LITERAL && key->tag != EXPR_IDENTIFIER && !expr_is_entity_name(key)) return false;
                expr = &expr->property->expr;
                break;
            }
            case EXPR_UNARY_OP:
                if (expr->unary_op->op != UNARY_OP_RIGHT_BANG) return false;
                expr = &expr->unary_op->expr;
                break;
            // (A JSDoc cast is tsgo's AsExpression: no reference, it prevents narrowing)
            case EXPR_BINARY_OP:
                if (expr->binary_op->op != BINARY_OP_COMMA) return false; // `(a, x).y`
                expr = &expr->binary_op->right;
                break;
            default:
                return false;
        }
    }
}

// tsc's getFlowTypeOfAccessExpression looks at the flow only for variables, properties and accessors, and for a method
// whose declared type is a union (an optional one under strictNullChecks): `!Array.isArray` doesn't make the method
// never in what follows. (An intersection's or a union's property is tsc's synthetic Property when more than one
// member has it: only a plain object's is looked at)
static bool is_unnarrowed_method(Checker* me, Ty const* object, Ty const* key) {
    Atom const* name = ty_literal_name(me, key);
    if (!name) return false;
    Ty const* apparent = ty_apparent_type(me, object);
    if (!(apparent->flags & TYF_OBJECT) || (apparent->flags & (TYF_UNION | TYF_INTERSECTION))) return false;
    Prop const* prop = ty_property_of_type(me, apparent, name);
    if (!prop) return false;
    // (A module's export that isn't a variable: `fs.promises` of `export * as promises`, a namespace, a function)
    Symbol const* symbol = prop->decl ? prop->decl->symbol : nullptr;
    if (!(prop->flags & PROP_METHOD) && symbol && symbol->decl_count &&
        !(symbol->flags & (SYMBOL_VARIABLE | SYMBOL_PROPERTY | SYMBOL_GET_ACCESSOR | SYMBOL_SET_ACCESSOR | SYMBOL_METHOD))
    ) {
        return true;
    }
    if (!(prop->flags & PROP_METHOD)) return false;
    if ((prop->flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks) return false;
    return prop_type(me, prop)->kind != TY_UNION;
}

// tsc's getFlowTypeOfAccessExpression()
static Ty const* access_flow_type(Checker* me, Expr const* expr, Ty const* object, Ty const* key, Ty const* type, Ty const* contextual) {
    PropertyExpr const* access = expr->property;
    if (access->assignment_target == FLOW_TARGET_DEFINITE) return type;
    bool narrowable = access->flow && is_narrowable_access(expr);
    // (A reference that isn't narrowable still gets getNarrowableTypeForReference: `(x as M<S>)[k]` where the context
    // isn't generic is the constraint of `M<S>[keyof S]`)
    if (!narrowable && !(type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return type;
    // Methods don't narrow
    if (type->kind == TY_ANONYMOUS && type->anonymous.function && type->anonymous.function->is_method) return type;
    if (!access->constraint_position && (type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES) && has_generic_with_union_constraint(me, type)) {
        // (What it is depends on the contextual type and on whether an inference is running, see check_with())
        *intern_slot(me, &my links, (u64[]){ KEY_ACCESS_USES_CONTEXT, (u64)(uintptr_t)expr }, 2) = expr;
    }
    Ty const* declared_type = type;
    type = narrowable_type(me, access->constraint_position, nullptr, type, contextual);
    if (!narrowable) return type;
    my flow_substitutes_constraints = type != declared_type;
    Ty const* flow_type = ty_flow_type(me, expr, type, type, access->flow);
    if (flow_type != type && is_unnarrowed_method(me, object, key)) return type;
    // tsc's getFlowTypeOfReference: the operand of a non-null assertion is never nullish (see non_null_flow_type())
    if (access->non_null_operand && !(flow_type->flags & TYF_NEVER) && (ty_non_nullable(me, flow_type)->flags & TYF_NEVER)) {
        return type;
    }
    return access->assignment_target ? base_type_of_literal(me, flow_type) : flow_type;
}

// The second case of tsc's assumeUninitialized: a JavaScript assignment declaration (`d.q = v`) read in the control
// flow container that declares it (tsc's getControlFlowContainer), where it's undefined until assigned
static bool is_read_in_declaring_container(Checker* me, PropertyExpr const* access) {
    // (Atomic: a deferred body's materialization may set it meanwhile, plan 78; only reads in its body, checked after it, care)
    if (!__atomic_load_n(&my binder->has_assignment_flow_containers, __ATOMIC_RELAXED)) return false;
    if (access->form != PROPERTY_EXPR_DOT || !span_valid(property_name(access)) || property_private_class(access)) return false;
    Ty const* object = ty_expr_type(me, &access->expr);
    if (object->kind == TY_UNION || (object->flags & (TYF_ANY | TYF_ERROR))) return false;
    Ty const* apparent = ty_apparent_type(me, object);
    if (!(apparent->flags & TYF_OBJECT)) return false;
    Prop const* prop = ty_property_of_type(me, apparent, ty_literal_name(me, ty_access_key_type(me, access)));
    // (Of a class instance: the declared property, which an instantiation's `target` is)
    while (prop && prop->target) prop = prop->target;
    // (A method, `F.prototype.m = function`, has its declared type unless that's a union: tsc's
    // getFlowTypeOfAccessExpression doesn't look at the flow for anything but variables, properties and accessors)
    if (prop && (prop->flags & PROP_METHOD) && prop_type(me, prop)->kind != TY_UNION) return false;
    Symbol const* symbol = prop && prop->decl ? prop->decl->symbol : nullptr;
    if (!symbol || !symbol->decl_count) return false;
    SymbolDecl const* decl = &symbol->decls[0];
    return decl->kind == SYMBOL_DECL_ASSIGNMENT && decl->js && decl->js->flow_container && decl->js->flow_container == property_context(access)->flow_container;
}

// tsc's getFlowTypeOfAccessExpression with assumeUninitialized: `this.x` in the constructor of the class that declares
// x without an initializer is undefined until assigned (TS2565), and so is a JavaScript assignment declaration in the
// container that assigns it
void ty_check_property_used_before_assigned(Checker* me, Expr const* expr, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    if (!options->strict_null_checks) return;
    PropertyExpr const* access = expr->property;
    if (access->assignment_target == FLOW_TARGET_DEFINITE || !access->flow) return;
    Expr const* object = &access->expr;
    // (`typeof this.x` written as a type is a QualifiedName for tsc, not an access expression: not the `this.x` case,
    // though its flow type is still narrowed)
    bool this_access = !access->qualified_name && object->tag == EXPR_IDENTIFIER &&
        object->identifier.keyword == KEYWORD_IDENTIFIER_THIS;
    // (`this.x` under strictPropertyInitialization is only the first case)
    // (Not through a JSDoc cast, `/** @type {T} */ (o).x`: tsgo's reparsed assertion isn't a narrowable reference)
    bool cast = object->tag == EXPR_AS_TYPE;
    if (!(this_access && options->strict_property_initialization) && !cast && is_read_in_declaring_container(me, access)) {
        Ty const* type = ty_expr_type(me, expr);
        if (ty_contains_undefined_type(type)) return;
        Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
        Ty const* initial = ty_union(me, parts, 2, nullptr, (TyList){0});
        if (!ty_contains_undefined_type(ty_flow_type(me, expr, type, initial, access->flow))) return;
        error_ctx_error_d(errs, property_name(access), 2565, "Property '", property_name(access), "' is used before being assigned.");
        return;
    }
    if (!this_access || !options->strict_property_initialization || !property_context(access)->in_constructor_of) return;
    if (object->tag != EXPR_IDENTIFIER || object->identifier.keyword != KEYWORD_IDENTIFIER_THIS ||
        object->identifier.this_kind != THIS_CLASS_INSTANCE || object->identifier.target != property_context(access)->in_constructor_of
    ) {
        return;
    }
    if (access->form != PROPERTY_EXPR_DOT || !span_valid(property_name(access))) return;
    ClassDecl const* class_decl = symbol_find_node(property_context(access)->in_constructor_of, SYMBOL_DECL_CLASS);
    if (!class_decl || class_decl->is_ambient) return;
    ClassItem const* field = class_decl_find_member(class_decl, span_sv(property_name(access)), false);
    if (!field || field->tag != CLASS_ITEM_FIELD || !expr_absent(&field->field.value) || field->field.is_abstract ||
        field->field.is_declare || field->field.is_auto_accessor || field->field.is_definite
    ) {
        return;
    }
    Ty const* type = ty_expr_type(me, expr);
    if (ty_contains_undefined_type(type)) return;
    Ty const* parts[2] = { type, ty_intrinsic(me, TY_UNDEFINED) };
    Ty const* initial = ty_union(me, parts, 2, nullptr, (TyList){0});
    if (!ty_contains_undefined_type(ty_flow_type(me, expr, type, initial, access->flow))) return;
    error_ctx_error_d(errs, property_name(access), 2565, "Property '", property_name(access), "' is used before being assigned.");
}

// Operators

// tsc's isTypeAssignableToKind with `strict`: any, unknown, void, null and undefined are no number or string
static bool extends_strict_nulls(Checker* me, Ty const* source, TyIntrinsic target) {
    if (source->flags & (TYF_NULLABLE | TYF_VOID | TYF_ANY | TYF_UNKNOWN)) return false;
    return ty_is_related(me, source, ty_intrinsic(me, target), TY_RELATION_ASSIGNABLE);
}

static bool is_definitely_falsy(Ty const* ty) {
    if (ty->flags & (TYF_NULLABLE | TYF_VOID)) return true;
    if (ty->kind != TY_LITERAL || (ty->flags & TYF_ENUM)) return false;
    if (ty->flags & TYF_STRING_LITERAL) return !ty->literal.string || ty->literal.string->len == 0; // (The value's string is borrowed)
    return !value_to_boolean(&ty->literal.value);
}

// The part of a type that survives `x || ...` being taken: x was truthy
static Ty const* remove_definitely_falsy(Checker* me, Ty const* ty) {
    return ty_remove_definitely_falsy(me, ty);
}

// The falsy values a type allows: what `x && ...` is when x was falsy
static Ty const* definitely_falsy_part(Checker* me, Ty const* ty) {
    if (ty->flags & TYF_BOOLEAN) return ty_intrinsic(me, TY_FALSE);
    if (is_definitely_falsy(ty)) return ty;
    if (ty->flags & TYF_STRING) return ty_string_literal(me, c_sv(""));
    if (ty->flags & TYF_NUMBER) {
        Value zero = value_number(0);
        return ty_literal(me, &zero);
    }
    if (ty->kind == TY_UNION) {
        TyList members = ty->composite.types;
        Ty const** parts = malloc(sizeof(Ty const*) * members.len);
        for (u32 i = 0; i < members.len; i++) parts[i] = definitely_falsy_part(me, members.data[i]);
        Ty const* ret = ty_union(me, parts, members.len, nullptr, (TyList){0});
        free(parts);
        return ret;
    }
    // A type parameter contributes nothing, whatever its constraint (tsc's getDefinitelyFalsyPartOfType)
    if (ty->flags & (TYF_ANY | TYF_UNKNOWN)) return ty;
    return ty_intrinsic(me, TY_NEVER);
}

static BinaryOp without_assignment(BinaryOp op) {
    switch (op) {
        case BINARY_OP_PLUS_EQUAL: return BINARY_OP_PLUS;
        case BINARY_OP_STAR_STAR_EQUAL: return BINARY_OP_STAR_STAR;
        case BINARY_OP_STAR_EQUAL: return BINARY_OP_STAR;
        case BINARY_OP_SLASH_EQUAL: return BINARY_OP_SLASH;
        case BINARY_OP_MOD_EQUAL: return BINARY_OP_MOD;
        case BINARY_OP_MINUS_EQUAL: return BINARY_OP_MINUS;
        case BINARY_OP_LESS_LESS_EQUAL: return BINARY_OP_LESS_LESS;
        case BINARY_OP_GREATER_GREATER_EQUAL: return BINARY_OP_GREATER_GREATER;
        case BINARY_OP_GREATER_GREATER_GREATER_EQUAL: return BINARY_OP_GREATER_GREATER_GREATER;
        case BINARY_OP_AND_EQUAL: return BINARY_OP_AND;
        case BINARY_OP_XOR_EQUAL: return BINARY_OP_XOR;
        case BINARY_OP_OR_EQUAL: return BINARY_OP_OR;
        case BINARY_OP_AND_AND_EQUAL: return BINARY_OP_AND_AND;
        case BINARY_OP_OR_OR_EQUAL: return BINARY_OP_OR_OR;
        case BINARY_OP_EH_EH_EQUAL: return BINARY_OP_EH_EH;
        default: return op;
    }
}


static bool is_bigint_like(Checker* me, Ty const* ty) {
    return (ty->flags & (TYF_BIGINT | TYF_BIGINT_LITERAL)) || extends_strict_nulls(me, ty, TY_BIGINT);
}

// tsc's checkBinaryLikeExpression for `*`, `-`, `<<`... : number, unless bigint is involved
static Ty const* arithmetic_result(Checker* me, Ty const* left, Ty const* right) {
    left = ty_non_nullable(me, left);
    right = ty_non_nullable(me, right);
    u32 any_or_unknown = TYF_ANY | TYF_UNKNOWN;
    u32 bigint_like = TYF_BIGINT | TYF_BIGINT_LITERAL;
    if (((left->flags & any_or_unknown) && (right->flags & any_or_unknown)) ||
        !(ty_maybe_type_of_kind(left, bigint_like) || ty_maybe_type_of_kind(right, bigint_like))
    ) {
        return ty_intrinsic(me, TY_NUMBER);
    }
    // (tsc's bothAreBigIntLike is the loose isTypeAssignableToKind: `any` is bigint-like, `bigint / any` is bigint)
    Ty const* bigint = ty_intrinsic(me, TY_BIGINT);
    if (((left->flags & bigint_like) || ty_is_related(me, left, bigint, TY_RELATION_ASSIGNABLE)) &&
        ((right->flags & bigint_like) || ty_is_related(me, right, bigint, TY_RELATION_ASSIGNABLE))
    ) {
        return bigint;
    }
    return any_type(me); // Mixed: an error (tsc's errorType)
}

// What tsc's checkNonNullType leaves of an operand under strictNullChecks: the error type for unknown or if nothing is
// left
static Ty const* non_null_operand(Checker* me, Ty const* type) {
    if (type->flags & TYF_UNKNOWN) return ty_intrinsic(me, TY_ERROR);
    if (!ty_can_be_null(me, type) && !ty_can_be_undefined(me, type)) return type;
    Ty const* non_null = ty_non_nullable(me, type);
    return non_null->flags & (TYF_NULLABLE | TYF_NEVER) ? ty_intrinsic(me, TY_ERROR) : non_null;
}

static Ty const* binary_result(Checker* me, Ty const* left, BinaryOp op, Ty const* right) {
    switch (op) {
        case BINARY_OP_PLUS: {
            // Unless one side is a string, both are checked non-null first (tsc's checkNonNullType of the operands,
            // the error type if nothing is left)
            if (compiler_options()->strict_null_checks) {
                Ty const* string = ty_intrinsic(me, TY_STRING);
                if (!ty_is_related(me, left, string, TY_RELATION_ASSIGNABLE) &&
                    !ty_is_related(me, right, string, TY_RELATION_ASSIGNABLE)
                ) {
                    left = non_null_operand(me, left);
                    right = non_null_operand(me, right);
                }
            }
            // (tsc's strict isTypeAssignableToKind: never is a number first)
            u32 loose = TYF_ANY | TYF_UNKNOWN | TYF_VOID | TYF_NULLABLE;
            if (!((left->flags | right->flags) & loose)) {
                if (extends_strict_nulls(me, left, TY_NUMBER) && extends_strict_nulls(me, right, TY_NUMBER)) {
                    return ty_intrinsic(me, TY_NUMBER);
                }
                if (is_bigint_like(me, left) && is_bigint_like(me, right)) return ty_intrinsic(me, TY_BIGINT);
            }
            bool left_string = extends_strict_nulls(me, left, TY_STRING);
            bool right_string = extends_strict_nulls(me, right, TY_STRING);
            if (left_string || right_string) {
                bool known = (left_string && !(left->flags & TYF_ANY)) || (right_string && !(right->flags & TYF_ANY));
                if (known) return ty_intrinsic(me, TY_STRING);
            } else if (extends_strict_nulls(me, left, TY_NUMBER) && extends_strict_nulls(me, right, TY_NUMBER)) {
                return ty_intrinsic(me, TY_NUMBER);
            }
            // (tsc: the error type if either side is)
            if ((left->flags | right->flags) & TYF_ERROR) return ty_intrinsic(me, TY_ERROR);
            return any_type(me);
        }

        case BINARY_OP_STAR_STAR:
        case BINARY_OP_STAR:
        case BINARY_OP_SLASH:
        case BINARY_OP_MOD:
        case BINARY_OP_MINUS:
        case BINARY_OP_LESS_LESS:
        case BINARY_OP_GREATER_GREATER:
        case BINARY_OP_GREATER_GREATER_GREATER:
        case BINARY_OP_AND:
        case BINARY_OP_XOR:
        case BINARY_OP_OR:
            return arithmetic_result(me, left, right);

        case BINARY_OP_DOUBLE_EQUAL:
        case BINARY_OP_TRIPLE_EQUAL:
        case BINARY_OP_NOT_EQUAL:
        case BINARY_OP_TRIPLE_NOT_EQUAL:
        case BINARY_OP_LESS:
        case BINARY_OP_GREATER:
        case BINARY_OP_LESS_EQUAL:
        case BINARY_OP_GREATER_EQUAL:
        case BINARY_OP_IN:
        case BINARY_OP_INSTANCEOF:
            return ty_intrinsic(me, TY_BOOLEAN);

        case BINARY_OP_AND_AND:
        {
            if (!ty_can_be_truthy(me, left)) return left;
            // (Literal reduction only, `void | undefined` stays. Without strictNullChecks the falsy part is the right
            // operand's base type's: `"a" && "b"` is `"" | "b"`)
            Ty const* falsy = definitely_falsy_part(me, compiler_options()->strict_null_checks ? left : ty_base_type_of_literal(me, right));
            Ty const* parts[2] = { falsy, right };
            return ty_union(me, parts, 2, nullptr, (TyList){0});
        }

        case BINARY_OP_OR_OR: {
            if (!ty_can_be_falsy(me, left)) return left;
            // (tsc's getNonNullableType of it: a T that may be nullish is NonNullable<T>, the identity without strictNullChecks)
            Ty const* types[2] = { ty_non_nullable(me, remove_definitely_falsy(me, left)), right };

            return ty_union_subtype_reduced(me, types, 2);
        }

        case BINARY_OP_EH_EH: {
            if (!ty_can_be_nullish(me, left)) return left;
            Ty const* types[2] = { ty_non_nullable(me, left), right };
            return ty_union_subtype_reduced(me, types, 2);
        }

        case BINARY_OP_EQUAL:
        case BINARY_OP_COMMA:
            return right;

        default:
            // Compound assignment: the type of what gets assigned
            return binary_result(me, left, without_assignment(op), right);
    }
}

// Whether the left operand of `binary` is checked with a contextual type (see check_binary())
static bool left_takes_context(BinaryOpExpr const* binary) {
    return binary->op == BINARY_OP_OR_OR || binary->op == BINARY_OP_EH_EH;
}

// A long `a + b + c + ...` would recurse once per operand (compute -> the left operand's LazyType -> compute): tsc
// checks binary chains iteratively (checkBinaryExpression's state machine). Here the left spine is evaluated bottom
// up first, so each level finds its left operand memoised. Only the part of the spine checked without a contextual
// type, since that's what its memo is; each level's own walk stops at the first finished memo, so a chain is O(n)
static void check_left_spine(Checker* me, BinaryOpExpr const* binary, ExprCtx ctx) {
    if (my flow_uncached) return; // Nothing is memoised
    bool contextual = ctx.contextual && left_takes_context(binary);
    Expr const* first = nullptr;
    u32 depth = 0;
    for (Expr const* e = &binary->left; e->tag == EXPR_BINARY_OP; e = &e->binary_op->left) {
        LazyType* lazy = (LazyType*)&e->lazy_type;
        if (lazy->tag != LAZY_TYPE_EXPR || !lazy->slot) break;
        LazyMemo* memo = lazy_type_state_peek(lazy);
        if (memo && (memo->done || memo->evaluation_started)) break;
        if (!contextual) {
            if (!first) first = e;
            depth++;
        }
        contextual = contextual && left_takes_context(e->binary_op);
    }
    if (depth < 32) return; // Shallow enough to recurse
    Expr const** spine = malloc(sizeof(Expr const*) * depth);
    panic_if(!spine, "out of memory");
    u32 len = 0;
    for (Expr const* e = first; len < depth; e = &e->binary_op->left) spine[len++] = e;
    for (u32 i = len; i-- > 0;) ty_expr_type(me, spine[i]);
    free(spine);
}

// `exports.x = v`, `module.exports = v` in a CommonJS file: an assignment that declares
static bool is_commonjs_declaration(BinaryOpExpr const* binary) {
    if (binary_commonjs_declaration(binary, nullptr) != COMMONJS_TARGET_NONE) return true;
    // `module.exports = v` of any JavaScript file, an ES module's too (tsc's getAssignmentDeclarationKind is syntactic:
    // AssignmentDeclarationKind.ModuleExports)
    SourceFile const* file = span_source(expr_outer_span(&binary->left));
    if (!file || !file->is_js || binary->op != BINARY_OP_EQUAL || expr_is_perenthisized(&binary->left) ||
        !expr_is_module_exports(&binary->left)
    ) {
        return false;
    }
    return true;
}

// JavaScript `a.b = {}` (or `F.prototype = {...}`) declaring a member that has expandos of its own (tsc's
// isAssignmentDeclaration in checkBinaryLikeExpression: getAssignedExpandoInitializer is an object literal and the
// symbol has exports)
static bool is_js_container_declaration(BinaryOpExpr const* binary) {
    Expr const* left = &binary->left;
    if (left->tag != EXPR_PROPERTY) return false;
    SourceFile const* file = span_source(expr_outer_span(left));
    if (!file || !file->is_js) return false;
    Symbol const* symbol = property_rare(left->property)->declares;
    // (A CommonJS export: `module.exports.A = {}; module.exports.A.B = 1`)
    Span name = span_null;
    if (!symbol && file->scope && file->scope->container && file->scope->container->exports &&
        binary_commonjs_declaration(binary, &name) == COMMONJS_TARGET_EXPORTS_PROPERTY
    ) {
        symbol = symbol_table_get(file->scope->container->exports, span_sv(name));
    }
    if (!symbol) return false;
    if (!symbol->exports || !symbol->exports->count) return false;
    Expr const* value = &binary->right;
    while (value->tag == EXPR_BINARY_OP && value->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(value)) {
        value = &value->binary_op->right;
    }
    if (value->tag != EXPR_OBJECT) return false;
    return !vec_object_expr_item_size(&value->object) || span_equals(expr_static_property_name(left), "prototype");
}

// tsc's getContextualTypeForAssignmentDeclaration for `exports.x = v` and `module.exports = v`: the JSDoc `@type` of
// the export's value declaration (the first assignment that declares it), else none
static Ty const* commonjs_declaration_context(BinaryOpExpr const* binary) {
    if (binary->jsdoc_type) return ty_of_node(binary->jsdoc_type);
    SourceFile const* file = span_source(expr_outer_span(&binary->left));
    Symbol const* module = file && file->scope ? file->scope->container : nullptr;
    if (!module || !module->exports) return nullptr;
    Span name = span_null;
    CommonJsTarget target = binary_commonjs_declaration(binary, &name);
    Symbol const* symbol = target == COMMONJS_TARGET_EXPORTS_PROPERTY ? symbol_table_get(module->exports, span_sv(name)) :
        target == COMMONJS_TARGET_MODULE_EXPORTS ? symbol_table_get(module->exports, c_sv("export=")) : nullptr;
    if (!symbol || !symbol->decl_count || symbol->decls[0].kind != SYMBOL_DECL_ASSIGNMENT) return nullptr;
    return symbol_decl_jsdoc_type(&symbol->decls[0]) ? ty_of_node(symbol_decl_jsdoc_type(&symbol->decls[0])) : nullptr;
}

static bool expando_target_is_annotated(PropertyExpr const* access) {
    Symbol const* declared = property_rare(access)->declares;
    // (`this.x`: its value declaration's annotation; `F.x`: only F's, tsgo's getContextualTypeForAssignmentExpression)
    bool this_access = access->expr.tag == EXPR_IDENTIFIER && access->expr.identifier.keyword == KEYWORD_IDENTIFIER_THIS;
    if (this_access && declared->decl_count && symbol_decl_jsdoc_type(&declared->decls[0])) return true;
    if (access->expr.tag != EXPR_IDENTIFIER || access->expr.identifier.keyword) return false;
    Symbol const* parent = access->expr.identifier.target;
    if (!parent || !(parent->flags & SYMBOL_VARIABLE)) return false;
    Binding const* binding = symbol_find_node(parent, SYMBOL_DECL_VARIABLE);
    return binding && !binding->parent && binding->has_explicit_type;
}

// What the target of `a = v` (or `||=`, `&&=`, `??=`) gives v as its contextual type, given the target's type (tsc's
// getContextualTypeForBinaryOperand), past the CommonJS and JavaScript container declarations check_binary() handles
// tsc's isBindableStaticNameExpression(expr, excludeThisKeyword=true): a name, or accesses of names and literals of one
static bool is_bindable_static_name(Expr const* expr) {
    if (expr_is_perenthisized(expr)) return false;
    if (expr->tag == EXPR_IDENTIFIER) return !expr->identifier.keyword;
    if (expr->tag != EXPR_PROPERTY) return false;
    if (expr->property->form == PROPERTY_EXPR_BRACKET) {
        Expr const* key = &expr->property->index;
        if (key->tag != EXPR_LITERAL || (key->literal.tag != VALUE_STRING && key->literal.tag != VALUE_NUMBER)) return false;
    } else {
        // (Not `a.#x`: tsc's isIdentifier(node.name))
        Span name = property_name(expr->property);
        if (span_valid(name) && span_sv(name).size && span_sv(name).buf[0] == '#') return false;
    }
    return is_bindable_static_name(&expr->property->expr);
}

// `this.b = v`, or `x.b = v` where x isn't a dotted name (or another operator than `=`): when b is a property declared
// with a type annotation (in a class or a type literal/interface), that annotation's type (a JavaScript class field's
// JSDoc type, for the expando case of assignment_value_context()). Null otherwise. (tsgo's
// getContextualTypeForAssignmentExpression otherwise gives a TypeScript `this.b = v` the target's type, an optional
// property's undefined included: tsc 6's getContextualTypeForAssignmentDeclaration took the annotation)
static Ty const* annotated_property_context(Checker* me, BinaryOpExpr const* binary) {
    PropertyExpr const* access = binary->left.property;
    // (`a.b = v` is AssignmentDeclarationKind.Property: the type of `a.b`)
    if (binary->op == BINARY_OP_EQUAL && is_bindable_static_name(&access->expr)) return nullptr;
    Ty const* object = ty_expr_type(me, &access->expr);
    if (object->flags & TYF_ANY) return nullptr;
    object = ty_apparent_type(me, object);
    // (A union's or intersection's property is a synthetic one, left to the target's type)
    if (!(object->flags & TYF_OBJECT) || object->kind == TY_UNION || object->kind == TY_INTERSECTION) return nullptr;
    Atom const* name = ty_literal_name(me, ty_access_key_type(me, access));
    if (!name) return nullptr;
    Prop const* prop = ty_find_prop(me, object, name).prop;
    if (!prop || (prop->flags & (PROP_METHOD | PROP_GETTER | PROP_SETTER))) return nullptr;
    Prop const* declared = prop;
    while (declared->target && !declared->mapped) declared = declared->target;
    if (declared->target || !declared->decl->node || declared->decl->parameter_property) return nullptr;
    return prop_type(me, prop);
}

// JavaScript `this.x = v` in an object literal's method or accessor declares x in the literal (tsgo's
// bindThisPropertyAssignment): v has no contextual type (getContextualTypeForAssignmentExpression)
static bool is_literal_this_declaration(BinaryOpExpr const* binary) {
    if (binary->op != BINARY_OP_EQUAL || binary->left.tag != EXPR_PROPERTY || !span_valid(expr_static_property_name(&binary->left))) {
        return false;
    }
    Expr const* object = &binary->left.property->expr;
    IdentifierExpr const* this = expr_this_keyword(object);
    FunctionImpl const* function = this && this->this_kind == THIS_ANY && !this->this_in_parameter_default ? identifier_this_function(this) : nullptr;
    SourceFile const* file = span_source(object->inner_span);
    return function && file && file->is_js && function_impl_rare(function)->this_literal && !function_impl_is_arrow(function) &&
        (span_valid(function_impl_member_name(function)) || function->accessor);
}

// Whether checking an assignment pattern reads a reference, which runs control flow analysis: a property access target
// (`{ a: o.p }`, `this.p`: its object is read), or a default or key that isn't a literal. tsc's getTypeOfExpression
// caches the left side's type then (flowInvocationCount moved), so getContextualType gives the same type twice and a
// call on the right infers from it; a pattern of plain identifiers (definite assignment targets, read without flow)
// gets a fresh object literal type each time, which isFromBindingPattern takes for a binding pattern's
static bool pattern_reads_references(Expr const* target) {
    if (target->tag == EXPR_BINARY_OP && target->binary_op->op == BINARY_OP_EQUAL) {
        Expr const* value = &target->binary_op->right;
        if (value->tag != EXPR_LITERAL) return true;
        target = &target->binary_op->left;
    }
    switch (target->tag) {
        case EXPR_IDENTIFIER:
            return target->identifier.keyword != 0; // (`this`)
        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &target->object) {
                if (i->tag == OBJECT_ITEM_SPREAD || i->computed_key) return true;
                if (object_item_rare(i)->shorthand_default && object_item_rare(i)->shorthand_default->tag != EXPR_LITERAL) return true;
                if (i->tag != OBJECT_ITEM_SHORTHAND && pattern_reads_references(&i->value)) return true;
            }
            return false;
        case EXPR_ARRAY:
            for_each (i, vec_expr, &target->array) {
                if (i->tag == EXPR_IMPLICIT_ANY) continue; // (An omitted element)
                if (pattern_reads_references(i->tag == EXPR_SPREAD ? i->spread : i)) return true;
            }
            return false;
        default:
            return true;
    }
}

// A computed name of a destructuring assignment's object literal that leaves its properties open (tsc's
// patternWithComputedProperties in checkObjectLiteral): a key type that is no literal or unique symbol but a property
// key type. A literal one is a property; one that isn't a key at all (`[undefined]`, TS2464) neither, so the assigned
// literal's other properties are still excess
static bool computed_key_opens_pattern(Checker* me, ObjectExprItem const* item) {
    if (!item->computed_key) return false;
    Ty const* key = ty_expr_type(me, item->computed_key);
    if (key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL)) return false;
    Ty const* all[3] = { ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_ES_SYMBOL) };
    return ty_is_related(me, key, ty_union(me, all, 3, nullptr, (TyList){0}), TY_RELATION_ASSIGNABLE);
}

// An object literal destructuring target's type as a pattern (tsc's checkObjectLiteral: `result.pattern = node`),
// registered as an assignment's (see ty_is_assignment_pattern()); null when a spread or computed name leaves its
// properties open
static Ty const* object_target_pattern(Checker* me, Expr const* target, Ty const* type) {
    if (!(type->flags & TYF_OBJECT) || type->kind != TY_ANONYMOUS) return nullptr;
    for_each (i, vec_object_expr_item, &target->object) {
        if (i->tag == OBJECT_ITEM_SPREAD || computed_key_opens_pattern(me, i)) return nullptr;
    }
    Ty const* ret = ty_pattern_clone(me, type);
    // (An assignment's: tsc's SkipBindingPatterns doesn't skip it, see ty_is_assignment_pattern())
    *intern_slot(me, &my links, (u64[]){ KEY_ASSIGNMENT_PATTERN, ret->id }, 2) = ret;
    return ret;
}

// The type of an object literal target nested in a destructuring target (an array literal's element, an object
// literal's property value: `[{ a: x }] = [{ a: 1, b: 2 }]`) is a pattern like the outermost one's (see
// assignment_value_context()), so the literal assigned to it gets the pattern's excess property check (TS2353)
static Ty const* nested_target_type(Checker* me, Expr const* parent, Expr const* value, Ty const* type) {
    if (!parent->assignment_pattern || value->tag != EXPR_OBJECT || !value->assignment_pattern) return type;
    Ty const* pattern = object_target_pattern(me, value, type);
    return pattern ? pattern : type;
}

static Ty const* assignment_value_context(Checker* me, BinaryOpExpr const* binary, Ty const* left) {
    // A JSDoc `@type` of the assignment (tsgo's reparsed binary.Type, getContextualTypeForBinaryOperand)
    if (binary->jsdoc_type && !binary->jsdoc_type_borrowed) return ty_of_node(binary->jsdoc_type);
    if (is_literal_this_declaration(binary)) return nullptr;
    Ty const* assigned_context = left->flags & TYF_ANY ? nullptr : left;
    // (A JavaScript object literal assigned to something of type any has that contextual type, so it isn't
    // open-ended: tsc's isJSObjectLiteral in checkObjectLiteral)
    if ((left->flags & TYF_ANY) && binary->right.tag == EXPR_OBJECT && span_source(binary->right.inner_span) &&
        span_source(binary->right.inner_span)->is_js && binary->op == BINARY_OP_EQUAL
    ) {
        assigned_context = left;
    }
    // (An any target is a context too, which a `yield` records as what its generator is sent, and in which a reference
    // of a generic type with a union constraint is its constraint: tsc's hasContextualTypeWithNoGenericTypes)
    // (And `this` in an object literal's methods is that any: tsc's getContextualThisParameterType)
    if (!assigned_context && ((binary->right.tag == EXPR_UNARY_OP && binary->right.unary_op->op == UNARY_OP_YIELD) ||
        binary->right.tag == EXPR_IDENTIFIER || binary->right.tag == EXPR_PROPERTY || binary->right.tag == EXPR_OBJECT)
    ) {
        assigned_context = left;
    }
    // `a.b = v` or `this.b = v` that declares b (an expando): v's type is what b is, so it gives v no context
    // unless b is declared with a type, or `a` is (tsc's getContextualTypeForAssignmentDeclaration). A class field
    // the class body declares with a type is that declaration's (the symbol's valueDeclaration in
    // getContextualTypeForThisPropertyAssignment): `/** @type {M} */ map;` then `this.map = {...}`
    if (binary->op == BINARY_OP_EQUAL && binary->left.tag == EXPR_PROPERTY && property_rare(binary->left.property)->declares &&
        !binary->jsdoc_type && !expando_target_is_annotated(binary->left.property)
    ) {
        assigned_context = annotated_property_context(me, binary);
    }
    // (An assignment declaration's JSDoc `@type` is its value's context, whatever it declares: tsgo's reparser makes
    // it the binary expression's type, getContextualTypeForAssignmentDeclaration)
    if (binary->op == BINARY_OP_EQUAL && binary->jsdoc_type) assigned_context = ty_of_node(binary->jsdoc_type);
    if (binary->op == BINARY_OP_EQUAL && binary->left.tag == EXPR_OBJECT && binary->left.assignment_pattern &&
        (left->flags & TYF_OBJECT) && left->kind == TY_ANONYMOUS &&
        (binary->right.tag != EXPR_CALL || !pattern_reads_references(&binary->left))
    ) {
        // An object pattern's type is a pattern (tsc's checkObjectLiteral: `result.pattern = node`), unless a
        // spread or computed name leaves its properties open. For a call, only when no reference is read in it (see
        // pattern_reads_references()): TYF_BINDING_PATTERN also means tsc's isFromBindingPattern, which goes by the
        // identity of two getContextualType answers: `({ a: n } = t0())` infers nothing, `({ promise: this.p } =
        // withResolvers())` infers from it like from any contextual type
        Ty const* pattern = object_target_pattern(me, &binary->left, left);
        if (pattern) assigned_context = pattern;
    }
    return assigned_context;
}

// A pattern type that is an assignment target's object literal, not a declaration's binding pattern: tsc's
// getContextualType with SkipBindingPatterns still gives it (getTypeOfExpression of the left side)
bool ty_is_assignment_pattern(Checker* me, Ty const* pattern) {
    return intern_get(&my links, (u64[]){ KEY_ASSIGNMENT_PATTERN, pattern->id }, 2) != nullptr;
}

Ty const* ty_assigned_value_context(Checker* me, BinaryOpExpr const* assignment) {
    if (is_literal_this_declaration(assignment) && !assignment->jsdoc_type) return nullptr;
    if (assignment->op == BINARY_OP_EQUAL && is_commonjs_declaration(assignment)) return commonjs_declaration_context(assignment);
    if (assignment->op == BINARY_OP_EQUAL && is_js_container_declaration(assignment)) return nullptr;
    return assignment_value_context(me, assignment, ty_expr_type(me, &assignment->left));
}

static bool is_logical_op(BinaryOp op) {
    return op == BINARY_OP_AND_AND || op == BINARY_OP_OR_OR || op == BINARY_OP_EH_EH;
}

static bool truthy_tests_check(Expr const* cond, Expr const* target);

// tsc's checkTestingKnownTruthyType's normal-mode checkExpression: a logical `cond`'s right operand, unless that is
// logical itself (then checkTestingKnownTruthyTypes of it)
static bool truthy_test_checks(Expr const* cond, Expr const* target) {
    if (cond->tag != EXPR_BINARY_OP || !is_logical_op(cond->binary_op->op)) return false;
    Expr const* location = &cond->binary_op->right;
    if (location->tag == EXPR_BINARY_OP && is_logical_op(location->binary_op->op)) return truthy_tests_check(location, target);
    return location->inner_span.file == target->inner_span.file && span_contains_span(expr_outer_span(location), target->inner_span);
}

// Whether tsc's checkTestingKnownTruthyTypes of `cond` (a conditional's condition, a `&&`'s left operand) checks
// `target` in normal mode: the right operand of `cond` and of each `||`/`??` down its left operands (strictNullChecks)
static bool truthy_tests_check(Expr const* cond, Expr const* target) {
    if (!compiler_options()->strict_null_checks) return false;
    for (u32 depth = 0; depth < 64; depth++) {
        if (truthy_test_checks(cond, target)) return true;
        if (cond->tag != EXPR_BINARY_OP || (cond->binary_op->op != BINARY_OP_OR_OR && cond->binary_op->op != BINARY_OP_EH_EH)) return false;
        cond = &cond->binary_op->left;
    }
    return false;
}

// Whether `target` is `root` or below it through nodes that check that operand in the check mode they're checked in
// (tsc's: parentheses, a conditional's three parts, a binary operator's operands (both of a logical operator, the left
// one otherwise) and a comma's right one, an assignment's value, `as` and `void`; `!`, `satisfies`, a call's arguments
// and literals' elements check in normal mode: bun's checks-of-a-value-assigned-on-a-loop-back-edge a3.ts), and not
// in a part a known-truthy test checks again in normal mode (a conditional's condition, a `&&`'s left operand:
// truthy_tests_check(); `f() || x === 1 ? a : b` reports, `f() || (x === 1 && g()) ? a : b` doesn't), see
// Checker.type_only_root
static bool type_only_reaches(Expr const* root, Expr const* target) {
    for (u32 depth = 0; depth < 64; depth++) {
        if (root == target) return true;
        switch (root->tag) {
            case EXPR_TERNARY:
                if (truthy_tests_check(&root->ternary->condition, target)) return false;
                return type_only_reaches(&root->ternary->condition, target) || type_only_reaches(&root->ternary->yes, target) ||
                    type_only_reaches(&root->ternary->no, target);
            case EXPR_BINARY_OP:
                // (An assignment checks its value in the mode it's checked in: `(y = x === 5) ? 1 : 2` reports nothing)
                if (binary_op_is_assignment(root->binary_op->op)) {
                    root = &root->binary_op->right;
                    break;
                }
                if (root->binary_op->op == BINARY_OP_AND_AND && truthy_tests_check(&root->binary_op->left, target)) return false;
                if ((root->binary_op->op == BINARY_OP_COMMA || is_logical_op(root->binary_op->op)) &&
                    type_only_reaches(&root->binary_op->right, target)) {
                    return true;
                }
                root = &root->binary_op->left;
                break;
            case EXPR_AS_TYPE:
                if (root->as_type->is_satisfies) return false;
                root = &root->as_type->expr;
                break;
            case EXPR_UNARY_OP:
                if (root->unary_op->op != UNARY_OP_VOID) return false;
                root = &root->unary_op->expr;
                break;
            default:
                return false;
        }
    }
    return false;
}

// The operators whose operands a loop's unfinished flow analysis checks on the partial types (binary_op_typecheck()):
// equalities, arithmetic, `+` and comparisons
static bool binary_op_checks_operands(BinaryOp op) {
    switch (op) {
        case BINARY_OP_DOUBLE_EQUAL: case BINARY_OP_TRIPLE_EQUAL: case BINARY_OP_NOT_EQUAL: case BINARY_OP_TRIPLE_NOT_EQUAL:
        case BINARY_OP_PLUS: case BINARY_OP_MINUS: case BINARY_OP_STAR: case BINARY_OP_SLASH: case BINARY_OP_MOD:
        case BINARY_OP_STAR_STAR: case BINARY_OP_LESS_LESS: case BINARY_OP_GREATER_GREATER:
        case BINARY_OP_GREATER_GREATER_GREATER: case BINARY_OP_AND: case BINARY_OP_XOR: case BINARY_OP_OR:
        case BINARY_OP_LESS: case BINARY_OP_GREATER: case BINARY_OP_LESS_EQUAL: case BINARY_OP_GREATER_EQUAL:
            return true;
        default:
            return false;
    }
}

static Ty const* check_binary(Checker* me, BinaryOpExpr const* binary, ExprCtx ctx) {
    check_left_spine(me, binary, ctx);
    Ty const* left;
    Ty const* right;
    switch (binary->op) {
        case BINARY_OP_EQUAL:
            if (is_commonjs_declaration(binary)) {
                // `exports.x = v` declares x: it is what v is (no context but a JSDoc type, tsc's
                // getContextualTypeForAssignmentDeclaration). The assignment is v's type (tsgo's
                // checkBinaryLikeExpression), unless v is a container
                right = ty_check_expr(me, &binary->right, commonjs_declaration_context(binary));
                if (is_js_container_declaration(binary)) return ty_expr_type(me, &binary->left);
                ty_expr_type(me, &binary->left);
                return right;
            }
            if (is_js_container_declaration(binary)) {
                // JavaScript `ns.a = {}` whose target has expandos is the container's type (tsc's
                // checkBinaryLikeExpression: isAssignmentDeclaration returns the left type)
                ty_check_expr(me, &binary->right, nullptr);
                return ty_expr_type(me, &binary->left);
            }
            // fallthrough
        case BINARY_OP_OR_OR_EQUAL:
        case BINARY_OP_AND_AND_EQUAL:
        case BINARY_OP_EH_EH_EQUAL:
            // The target contextually types what is assigned to it (tsc's getContextualTypeForBinaryOperand)
            left = ty_expr_type(me, &binary->left);
            Ty const* assigned_context = assignment_value_context(me, binary, left);
            right = ty_check_expr(me, &binary->right, assigned_context);
            // `{} = v` is v without null and undefined, the error type if nothing is left (tsc's
            // checkObjectLiteralAssignment returns checkNonNullType's)
            if (binary->op == BINARY_OP_EQUAL && binary->left.tag == EXPR_OBJECT && !expr_is_perenthisized(&binary->left) &&
                !vec_object_expr_item_size(&binary->left.object) && compiler_options()->strict_null_checks
            ) {
                if (right->flags & TYF_UNKNOWN) return ty_intrinsic(me, TY_ERROR);
                if (ty_can_be_null(me, right) || ty_can_be_undefined(me, right)) {
                    Ty const* non_null = ty_non_nullable(me, right);
                    return non_null->flags & (TYF_NULLABLE | TYF_NEVER) ? ty_intrinsic(me, TY_ERROR) : non_null;
                }
            }
            break;
        case BINARY_OP_OR_OR:
        case BINARY_OP_EH_EH: {
            // (The operands' contextual type is the expression's as written, see passed_on_context())
            // (A context-sensitive function in either is left out of a first inference pass like anywhere else: tsc's
            // check mode reaches both operands, so `response ?? (async ({ request }) => ...)` gets its contextual
            // signature once the call inferred what it mentions)
            // (And the view a contextual signature sees, like a conditional's branches: the second inference pass gives
            // a function there the signature what the call inferred makes, not the one the first pass saw)
            ExprCtx passed = passed_on_context(ctx);
            passed.is_const = false;
            passed.template_context = false;
            left = check_with(me, &binary->left, passed);
            // (The left operand's type when there is no contextual type, or when it is a binding pattern's: tsc's
            // getContextualTypeForBinaryOperand, `let { a, b } = opts ?? { a: new Set(), b: new Set() }`)
            bool from_pattern = ctx.contextual && (ctx.contextual->flags & TYF_BINDING_PATTERN);
            right = check_with(me, &binary->right, ctx.contextual && !from_pattern ? passed :
                (ExprCtx){ .contextual = left, .skip_sensitive = ctx.skip_sensitive, .generic_mode = ctx.generic_mode });
            break;
        }
        case BINARY_OP_AND_AND:
        case BINARY_OP_COMMA: {
            left = ty_expr_type(me, &binary->left);
            ExprCtx passed = passed_on_context(ctx);
            passed.is_const = false;
            passed.template_context = false;
            right = check_with(me, &binary->right, passed);
            break;
        }
        default:
            left = ty_expr_type(me, &binary->left);
            right = ty_expr_type(me, &binary->right);
            break;
    }
    return binary_result(me, left, binary->op, right);
}

// tsc's getUnaryResultType: -, ~, ++ and -- of a bigint are a bigint
static Ty const* unary_result_type(Checker* me, Ty const* operand) {
    if (!ty_maybe_type_of_kind(operand, TYF_BIGINT | TYF_BIGINT_LITERAL)) return ty_intrinsic(me, TY_NUMBER);
    if ((operand->flags & (TYF_ANY | TYF_UNKNOWN)) || ty_maybe_type_of_kind(operand, TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM)) {
        Ty const* parts[2] = { ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_BIGINT) };
        return ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    return ty_intrinsic(me, TY_BIGINT);
}

static Ty const* check_unary(Checker* me, UnaryOpExpr const* unary) {
    Expr const* operand = &unary->expr;
    switch (unary->op) {
        case UNARY_OP_LEFT_BANG: {
            Ty const* type = ty_expr_type(me, operand);
            bool truthy = ty_can_be_truthy(me, type);
            bool falsy = ty_can_be_falsy(me, type);
            if (truthy != falsy) return ty_fresh_literal(me, ty_intrinsic(me, truthy ? TY_FALSE : TY_TRUE));
            return ty_intrinsic(me, TY_BOOLEAN);
        }
        case UNARY_OP_TYPEOF: {
            ty_expr_type(me, operand); // Evaluated like any operand: a circular reference through it is one
            return my typeof_type;
        }
        case UNARY_OP_VOID: return ty_intrinsic(me, TY_UNDEFINED_WIDENING);
        case UNARY_OP_TILDE: return unary_result_type(me, ty_expr_type(me, operand));
        case UNARY_OP_DELETE:
            ty_expr_type(me, operand); // (tsc's checkDeleteExpression checks the operand first)
            return ty_intrinsic(me, TY_BOOLEAN);
        case UNARY_OP_AWAIT: return ty_awaited(me, ty_expr_type(me, operand));

        case UNARY_OP_YIELD:
        case UNARY_OP_YIELD_STAR:
            impossible; // check_yield()

        case UNARY_OP_PLUS:
        case UNARY_OP_MINUS:
            // (tsc's checkPrefixUnaryExpression checks the operand first: `-1` makes the literal 1 before -1)
            if (!expr_is_perenthisized(operand) && operand->tag == EXPR_LITERAL) ty_expr_type(me, operand);
            if (!expr_is_perenthisized(operand) && operand->tag == EXPR_LITERAL && operand->literal.tag == VALUE_NUMBER) {
                Value value = value_number(unary->op == UNARY_OP_PLUS ? operand->literal.number : -operand->literal.number);
                return ty_fresh_literal(me, ty_literal(me, &value));
            }
            if (unary->op == UNARY_OP_MINUS && !expr_is_perenthisized(operand) && operand->tag == EXPR_LITERAL &&
                operand->literal.tag == VALUE_BIGINT
            ) {
                Value value = value_bigint_negated(&operand->literal); // (bigints have no -0: printed and keyed as 0)
                return ty_fresh_literal(me, ty_literal(me, &value));
            }
            // (the operand is checked even when `+` makes any of it a number: `x = +(x === 5)` on a loop's back edge
            // reports the comparison, `var a = +a` is circular)
            if (unary->op == UNARY_OP_PLUS) {
                ty_expr_type(me, operand);
                return ty_intrinsic(me, TY_NUMBER);
            }
            return unary_result_type(me, ty_expr_type(me, operand));

        case UNARY_OP_LEFT_PLUS_PLUS:
        case UNARY_OP_LEFT_MINUS_MINUS:
        case UNARY_OP_RIGHT_PLUS_PLUS:
        case UNARY_OP_RIGHT_MINUS_MINUS:
            return unary_result_type(me, ty_expr_type(me, operand));

        case UNARY_OP_RIGHT_BANG: {
            // (tsc's getNonNullableType: without strictNullChecks nothing is nullable, `null!` is null)
            Ty const* type = ty_expr_type(me, operand);
            if (!compiler_options()->strict_null_checks) return type;
            // Part of an optional chain (`a?.b!.c`): the chain may still have ended, tsc's checkNonNullChain keeps its
            // marker
            bool optional = false;
            type = ty_optional_expression_type(me, type, false, &optional);
            return propagate_optional(me, ty_non_nullable(me, type), unary->continues_chain, optional);
        }
    }
    impossible;
}

// Object literals

Atom const* ty_property_name_of_key(Checker* me, Ty const* key, bool* is_number) {
    if (key->kind != TY_LITERAL || (key->flags & TYF_ENUM)) return nullptr;
    if (key->flags & (TYF_STRING_LITERAL | TYF_UNIQUE_ES_SYMBOL)) return key->literal.string;
    if (!(key->flags & TYF_NUMBER_LITERAL)) return nullptr;
    cstr rendered = cstr_null;
    cstr_append_value_js(&rendered, &key->literal.value);
    Atom const* name = checker_atom(me, cstr_sv(&rendered));
    cstr_drop(&rendered);
    *is_number = true;
    return name;
}

typedef struct LiteralBuilder {
    ObjectLiteralProp* props;
    u32 count, cap;
    IndexInfo index[3];
    u32 index_count;
    bool empty_object; // Spreads: nothing, tsc's emptyObjectType, rather than an object literal without properties
    // Spreads: the names of private and protected properties the spread object has, which remove what came before
    // (tsc's getSpreadType: skippedPrivateMembers)
    Atom const** skipped;
    u32 skipped_count;
    // Past LITERAL_BUILDER_INDEXED properties, where each name is among `props` (literal_builder_find(): scanning them
    // for each property added is quadratic on large literals). Open addressing over prop indices, -1 for an empty slot,
    // built lazily: `indexed` props are in it. Not shared by copies (literal_builder_clone() leaves it out)
    i32* slots;
    u32 slot_mask;
    u32 indexed;
} LiteralBuilder;

enum { LITERAL_BUILDER_INDEXED = 16 };

static void literal_builder_skip(LiteralBuilder* builder, Atom const* name) {
    builder->skipped = realloc(builder->skipped, sizeof(Atom const*) * (builder->skipped_count + 1));
    panic_if(!builder->skipped, "out of memory");
    builder->skipped[builder->skipped_count++] = name;
}

static bool literal_builder_skips(LiteralBuilder const* builder, Atom const* name) {
    for (u32 i = 0; i < builder->skipped_count; i++) {
        if (builder->skipped[i] == name) return true;
    }
    return false;
}

static inline u32 literal_builder_slot(LiteralBuilder const* builder, Atom const* name) {
    u64 hash = (u64)(uintptr_t)name;
    return (u32)((hash ^ (hash >> 29)) * 0x9e3779b97f4a7c15ull >> 32) & builder->slot_mask;
}

// Where `name` is in the index, or the empty slot it would go to
static u32 literal_builder_probe(LiteralBuilder const* builder, Atom const* name) {
    u32 slot = literal_builder_slot(builder, name);
    while (builder->slots[slot] >= 0 && builder->props[builder->slots[slot]].name != name) {
        slot = (slot + 1) & builder->slot_mask;
    }
    return slot;
}

// Brings the index up to `count` (each name is in `props` once: literal_builder_add() replaces in place)
static void literal_builder_index(LiteralBuilder* builder) {
    if (!builder->slots || builder->count * 2 > builder->slot_mask + 1) {
        u32 capacity = 64;
        while (capacity < builder->count * 4) capacity <<= 1;
        free(builder->slots);
        builder->slots = malloc(sizeof(i32) * capacity);
        panic_if(!builder->slots, "out of memory");
        memset(builder->slots, 0xff, sizeof(i32) * capacity);
        builder->slot_mask = capacity - 1;
        builder->indexed = 0;
    }
    for (; builder->indexed < builder->count; builder->indexed++) {
        builder->slots[literal_builder_probe(builder, builder->props[builder->indexed].name)] = (i32)builder->indexed;
    }
}

static ObjectLiteralProp* literal_builder_find(LiteralBuilder* builder, Atom const* name) {
    if (builder->count > LITERAL_BUILDER_INDEXED) {
        literal_builder_index(builder);
        i32 at = builder->slots[literal_builder_probe(builder, name)];
        return at >= 0 ? &builder->props[at] : nullptr;
    }
    for (u32 i = 0; i < builder->count; i++) {
        if (builder->props[i].name == name) return &builder->props[i];
    }
    return nullptr;
}

static ObjectLiteralProp* literal_builder_add(LiteralBuilder* builder, ObjectLiteralProp prop) {
    // A later property of the same name replaces the earlier one in place
    ObjectLiteralProp* existing = literal_builder_find(builder, prop.name);
    if (existing) {
        *existing = prop;
        return existing;
    }
    if (builder->count == builder->cap) {
        builder->cap = builder->cap ? builder->cap * 2 : 8;
        builder->props = realloc(builder->props, sizeof(ObjectLiteralProp) * builder->cap);
        panic_if(!builder->props, "out of memory");
    }
    builder->props[builder->count] = prop;
    return &builder->props[builder->count++];
}

// Room for `count` properties in all (a literal's own: its items up to the next spread), one allocation rather than a
// growing one
static void literal_builder_reserve(LiteralBuilder* builder, u32 count) {
    if (count <= builder->cap) return;
    builder->cap = count;
    builder->props = realloc(builder->props, sizeof(ObjectLiteralProp) * builder->cap);
    panic_if(!builder->props, "out of memory");
}

// How many of an object literal's items from `from` on come before the next spread (or the end)
static u32 object_items_before_spread(ObjectExpr const* object, ObjectExprItem const* from) {
    u32 size = (u32)vec_object_expr_item_size(object);
    ObjectExprItem const* end = size ? vec_object_expr_item_at(object, size - 1) + 1 : from;
    u32 n = 0;
    for (ObjectExprItem const* i = from; i < end && i->tag != OBJECT_ITEM_SPREAD; i++) n++;
    return n;
}

static void literal_builder_drop(LiteralBuilder* builder) {
    free(builder->props);
    free(builder->skipped);
    free(builder->slots);
    *builder = (LiteralBuilder){0};
}

static LiteralBuilder literal_builder_clone(LiteralBuilder const* builder) {
    LiteralBuilder ret = *builder;
    ret.skipped = nullptr;
    ret.skipped_count = 0;
    ret.slots = nullptr;
    ret.slot_mask = 0;
    ret.indexed = 0;
    ret.cap = builder->count;
    ret.props = builder->count ? malloc(sizeof(ObjectLiteralProp) * builder->count) : nullptr;
    panic_if(builder->count && !ret.props, "out of memory");
    if (builder->count) memcpy(ret.props, builder->props, sizeof(ObjectLiteralProp) * builder->count);
    return ret;
}

// Spreads (tsc's getSpreadType): what `{ ...a, x: 1, ...b }` is made of, in the order tsc makes it. Spreading `right`
// into `left` gives right's properties (in its order), then those of left that right doesn't have; an optional
// property of right adds to left's of that name rather than replacing it. The properties written between spreads are
// spread into what came before like that too. A union is spread member by member (the result is a union), a generic
// object stays itself (the result is an intersection with it)

// tsc's isSpreadableProperty (and the modifier check of getSpreadType/getRestType): not private, protected or `#x`, nor
// a method or accessor declared in a class
bool ty_is_spreadable_prop(Checker* me, Prop const* prop) {
    if (prop->flags & (PROP_PRIVATE | PROP_PROTECTED)) return false;
    if (ty_private_name_class(me, prop->name)) return false;
    Prop const* origin = prop;
    while (origin->target) origin = origin->target;
    u32 kinds = PROP_METHOD | PROP_ACCESSOR | PROP_GETTER | PROP_SETTER;
    return !((prop->flags | origin->flags) & kinds) || !origin->parent || !(origin->parent->flags & SYMBOL_CLASS);
}

// The properties of an object type as a spread takes them (tsc's isSpreadableProperty and getSpreadSymbol): not
// private or protected ones, nor methods of classes, and none readonly. `partial`: each one optional (tsc's
// getAnonymousPartialType). `readonly`: in a const context, every one readonly, methods too as properties
static void spread_props_of(Checker* me, Ty const* object, bool partial, bool readonly, LiteralBuilder* out) {
    if (object->kind == TY_INTERSECTION) {
        // The properties of all of its members, each of the type the intersection has for it: every member's
        // properties are spread, then made what the intersection says (optional only if optional everywhere)
        LiteralBuilder all = {0};
        for (u32 i = 0; i < object->composite.types.len; i++) {
            Ty const* member = ty_apparent_type(me, object->composite.types.data[i]);
            if (!(member->flags & (TYF_OBJECT | TYF_INTERSECTION))) continue;
            LiteralBuilder part = {0};
            spread_props_of(me, member, partial, readonly, &part);
            for (u32 j = 0; j < part.count; j++) {
                ObjectLiteralProp* existing = literal_builder_find(&all, part.props[j].name);
                if (!existing) {
                    literal_builder_add(&all, part.props[j]);
                    continue;
                }
                if (!(part.props[j].flags & PROP_OPTIONAL)) {
                    existing->flags &= ~(u32)PROP_OPTIONAL;
                    existing->parent = nullptr;
                }
            }
            for (u32 j = 0; j < part.skipped_count; j++) literal_builder_skip(out, part.skipped[j]);
            literal_builder_drop(&part);
        }
        for (u32 j = 0; j < all.count; j++) {
            all.props[j].parent = nullptr; // (A property of the intersection: a symbol of its own)
            // (Several members' (a class method among them too): tsc's createUnionOrIntersectionProperty makes a Property)
            if ((all.props[j].flags & PROP_METHOD) && object->composite.types.len <= 64) {
                Prop const* having[64];
                u32 count = 0;
                for (u32 i = 0; i < object->composite.types.len; i++) {
                    Ty const* member = ty_apparent_type(me, object->composite.types.data[i]);
                    if (!(member->flags & (TYF_OBJECT | TYF_INTERSECTION))) continue;
                    FoundProp found = ty_find_prop(me, member, all.props[j].name);
                    if (found.found && found.prop) having[count++] = found.prop;
                }
                if (count > 1 && ty_props_distinct(me, having, count)) all.props[j].flags &= ~(u32)PROP_METHOD;
            }
            Ty const* type = ty_access(me, object, ty_prop_name_literal(me, all.props[j].name));
            // (Under exactOptionalPropertyTypes the optional property's type is the read's, missing included)
            bool exact = compiler_options()->exact_optional_property_types && compiler_options()->strict_null_checks;
            if (type && exact && (all.props[j].flags & PROP_OPTIONAL)) {
                all.props[j].type = type;
                literal_builder_add(out, all.props[j]);
                continue;
            }
            if (type && (all.props[j].flags & PROP_OPTIONAL) && !partial) type = ty_without_undefined(me, type);
            if (type) {
                all.props[j].type = (all.props[j].flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks ?
                    optional_type(me, type) : type;
                // (`asserted` stays: the types baseline reuses it where it still says the same, like tsc does the
                // annotation of the intersection property's first declaration)
            }
            literal_builder_add(out, all.props[j]);
        }
        // (The intersection's index signatures: one per key type, of the intersection of the members' value types, tsc's
        // getIndexInfosOfType with resolveIntersectionTypeMembers' appendIndexInfo)
        Members const* whole = ty_members(me, object);
        for (u32 j = 0; j < whole->index_count && out->index_count < c_arraylen(out->index); j++) {
            out->index[out->index_count] = whole->index[j];
            out->index[out->index_count++].readonly = readonly;
        }
        literal_builder_drop(&all);
        return;
    }
    Members const* members = ty_members(me, object);
    for (u32 j = 0; j < members->prop_count; j++) {
        Prop const* prop = members_prop(me, members, j);
        if (prop->flags & (PROP_PRIVATE | PROP_PROTECTED)) literal_builder_skip(out, prop->name);
        if (!ty_is_spreadable_prop(me, prop)) continue;
        if ((prop->flags & PROP_METHOD) && prop->decl->function) continue; // (a class method)
        Prop const* origin = prop;
        while (origin->target) origin = origin->target;
        // (A set-only accessor spreads as undefined: tsc's getSpreadSymbol)
        bool setonly = ((prop->flags | origin->flags) & PROP_SETTER) && !((prop->flags | origin->flags) & PROP_GETTER);
        Ty const* type = setonly ? ty_intrinsic(me, TY_UNDEFINED) : prop_type(me, prop);
        u32 flags = prop->flags & (u32)~(PROP_READONLY | (setonly ? PROP_SETTER : 0));
        Ty const* declared_type = type;
        if (readonly) flags = (flags & (u32)~PROP_METHOD) | PROP_READONLY; // (tsc's getSpreadSymbol makes a property)
        // (Several parts' of an intersection: tsc's createUnionOrIntersectionProperty made a Property, which the spread keeps)
        bool synthetic = false;
        for (Prop const* i = prop; i && !synthetic; i = i->target) synthetic = i->synthetic;
        if (synthetic) flags &= (u32)~PROP_METHOD;
        if (partial || setonly || readonly != !!(prop->flags & PROP_READONLY)) flags |= PROP_NO_VALUE_DECLARATION;
        // (tsc's getSpreadSymbol keeps the symbol: one without a valueDeclaration stays so, a mapped or reverse mapped
        // type's property, and an intersection's of several declarations (createUnionOrIntersectionProperty's
        // hasNonUniformValueDeclaration), whose annotation isn't reused and whose comments aren't copied)
        bool no_value = false;
        for (Prop const* i = prop; i && !no_value; i = i->target) {
            no_value = i->mapped || (i->flags & PROP_NO_VALUE_DECLARATION) || prop_decl_rare(i->decl)->reverse_of;
        }
        if (synthetic && !no_value) {
            Prop const* const* parts = nullptr;
            u32 part_count = ty_synthetic_prop_parts(me, prop, &parts);
            PropDecl const* first = nullptr;
            for (u32 k = 0; k < part_count && !no_value; k++) {
                Prop const* root = parts[k];
                while (root->target) root = root->target;
                if (!first) first = root->decl;
                else if (root->decl != first) no_value = true;
            }
        }
        if (no_value) flags |= PROP_NO_VALUE_DECLARATION;
        if (partial) {
            // (A property, even of a method: tsc's tryMergeUnionOfObjectTypeAndEmptyObject. With addOptionality,
            // missing under exactOptionalPropertyTypes: `{ ...(c ? {} : { d: o.d }) }` is `{ d?: string }`)
            flags = (flags | PROP_OPTIONAL) & ~(u32)PROP_METHOD;
            type = ty_optional_prop_type(me, type);
        }
        // The annotation of the property it comes from still says what it is, unless instantiated (or made optional:
        // an optional one of a partial spread, `{ ...(o as Opts | undefined) }`, still is what it says)
        TypeNode const* annotation = !(no_value && synthetic) && origin->decl->node && origin->decl->node->parsed && !origin->decl->lazy && !origin->decl->function &&
            !prop_decl_rare(origin->decl)->overload_count && !origin->decl->symbol && declared_type == prop_type(me, origin) ? origin->decl->node : nullptr;
        // (A property spread in from an object literal reuses what that one reused: `{ ...{ ...b1, z } }`)
        if (!annotation && prop_decl_asserted(origin->decl) && declared_type == prop_type(me, origin)) annotation = prop_decl_asserted(origin->decl);
        // (A mapped type's property is declared where the property it maps is, `{ ...(x as Readonly<A>) }`: tsc's mapped
        // symbol's declarations, whose annotation is reused where it says the same)
        Prop const* mapped_from = prop_decl_rare(prop->decl)->mapped_from;
        // (Compared with the optionality of each: the mapped property's type and the annotation's, as declared optional
        // or not, tsc's addOptionality. Partial's `f?` of a required `f: string` isn't what `string` says, an optional
        // `s?: string` mapped is)
        bool same_as_mapped = mapped_from && (prop->flags & PROP_OPTIONAL ? optional_type(me, declared_type) : declared_type) ==
            (mapped_from->flags & PROP_OPTIONAL ? optional_type(me, prop_type(me, mapped_from)) : prop_type(me, mapped_from));
        // (Through a mapped type of a mapped type, `Pick<Readonly<A>, K>`, to the annotation it all comes from, while
        // each says the same)
        for (u32 depth = 0; !annotation && same_as_mapped && depth < 8; depth++) {
            Prop const* root = mapped_from;
            while (root->target) root = root->target;
            annotation = ty_prop_annotation(root);
            Prop const* next = prop_decl_rare(mapped_from->decl)->mapped_from;
            same_as_mapped = next && (mapped_from->flags & PROP_OPTIONAL ? optional_type(me, prop_type(me, mapped_from)) : prop_type(me, mapped_from)) ==
                (next->flags & PROP_OPTIONAL ? optional_type(me, prop_type(me, next)) : prop_type(me, next));
            mapped_from = next;
        }
        // (Declared where the property it maps is, which orders it: tsc's mapped symbol's declarations)
        Prop const* declared_at = origin;
        if (!declared_at->decl->decl_span_count) {
            // (The first one along the way that has declarations: a mapped property has its own, which its template,
            // the origin, hasn't)
            Prop const* own = prop;
            while (own->target && !own->decl->decl_span_count) own = own->target;
            if (own->decl->decl_span_count) declared_at = own;
        }
        if (!declared_at->decl->decl_span_count && prop_decl_rare(prop->decl)->mapped_from) {
            declared_at = prop_decl_rare(prop->decl)->mapped_from;
            while (declared_at->target && !declared_at->decl->decl_span_count) declared_at = declared_at->target;
        }
        TypeNode const* annotation_required = nullptr;
        // (Under exactOptionalPropertyTypes a partial spread's missing doesn't count: the printer takes it out)
        if (annotation && type != declared_type && ty_remove_missing(me, type, (flags & PROP_OPTIONAL) != 0) != declared_type) {
            annotation_required = annotation;
            annotation = nullptr;
        }
        literal_builder_add(out, (ObjectLiteralProp){
            .name = prop->name,
            .flags = flags,
            .type = type,
            .asserted = annotation,
            .asserted_required = annotation_required,
            .item = origin->decl->item,
            .name_span = origin->decl->name_span,
            .name_form = ty_prop_name_form_of(prop),
            .name_type = prop_decl_rare(prop->decl)->name_type,
            .decl_spans = declared_at->decl->decl_spans,
            .decl_span_count = declared_at->decl->decl_span_count,
            .name_is_value = origin->decl->name_is_value,
            .parent = !partial && !setonly && !(prop->flags & PROP_READONLY) ? prop->parent : nullptr,
            .spread_variable = origin->decl->symbol ? origin->decl->symbol : prop_decl_rare(origin->decl)->spread_variable,
            .spread_namespace = origin->decl->symbol ?
                (object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_NAMESPACE ? object->declared.namespace_symbol : nullptr) :
                prop_decl_rare(origin->decl)->spread_namespace,
            .spread_fresh = partial || setonly || readonly != !!(prop->flags & PROP_READONLY) ||
                (!origin->decl->symbol && prop_decl_rare(origin->decl)->spread_fresh),
            .spread_alias = origin->decl->symbol ? (origin->flags & PROP_NO_VALUE_DECLARATION) != 0 :
                prop_decl_rare(origin->decl)->spread_alias,
            // (An accessor pair whose setter takes another type stays that symbol, with its write type: tsc's
            // getSpreadSymbol keeps a property whose readonly-ness doesn't change, `sp.d = "x"` writes the setter's)
            .reuse = !partial && !setonly && readonly == !!(prop->flags & PROP_READONLY) && prop_write_type(me, prop) ? prop : nullptr,
        });
    }
    // (Not readonly outside a const context, like the properties: tsc's getIndexInfoWithReadonly in getSpreadType)
    for (u32 j = 0; j < members->index_count && out->index_count < c_arraylen(out->index); j++) {
        out->index[out->index_count] = members->index[j];
        out->index[out->index_count++].readonly = readonly;
    }
}

// tsc's getSpreadType of two object types, `left` (tsc's emptyObjectType if `left_empty`) becoming the result
// The literal's own accessor as a spread takes it (tsc's getSpreadSymbol): a property of the accessor's type unless it's
// already as readonly as the spread makes it, a set-only one undefined
// The new property has no value declaration, so it gets no excess property check: `const o: Options = { ...defaults, get extra() { return 1; } }` is fine
static ObjectLiteralProp spread_accessor(Checker* me, ObjectLiteralProp prop, bool readonly) {
    // (A property merged with a getter, `{ s: 1, get s() {...} }`, is as readonly as the getter: a fresh symbol once
    // the spread changes that)
    if (!prop.accessor && prop.own && !readonly && (prop.flags & PROP_READONLY) && !prop.reuse) {
        prop.own_merged = true;
        prop.own = false;
        prop.flags = (prop.flags & ~(u32)PROP_READONLY) | PROP_NO_VALUE_DECLARATION;
        prop.parent = nullptr;
        return prop;
    }
    if (!prop.accessor || prop.type) return prop;
    bool setonly = (prop.flags & PROP_SETTER) && !(prop.flags & PROP_GETTER);
    if (!setonly && readonly == !!(prop.flags & PROP_READONLY)) return prop;
    prop.own_merged = prop.own || prop.own_merged;
    prop.own = false;
    prop.type = setonly ? ty_intrinsic(me, TY_UNDEFINED) : ty_object_literal_accessor_type(me, prop.accessor, true);
    prop.accessor = prop.setter = nullptr;
    prop.flags = (prop.flags & ~(u32)(PROP_GETTER | PROP_SETTER | PROP_READONLY)) | PROP_NO_VALUE_DECLARATION |
        (readonly ? PROP_READONLY : 0);
    prop.parent = nullptr;
    return prop;
}

static void spread_into(Checker* me, LiteralBuilder* left, LiteralBuilder const* right, bool readonly) {
    LiteralBuilder result = {0};
    for (u32 i = 0; i < right->count; i++) {
        ObjectLiteralProp spread = spread_accessor(me, right->props[i], readonly);
        ObjectLiteralProp const* r = &spread;
        ObjectLiteralProp* l = literal_builder_find(left, r->name);
        if (!l || !(r->flags & PROP_OPTIONAL)) {
            literal_builder_add(&result, *r);
            continue;
        }
        // Optional: what was there stays possible, and so does its being required
        ObjectLiteralProp merged = *l;
        merged.parent = nullptr;
        merged.reuse = nullptr;
        if (!merged.type && merged.accessor) { // (A getter (and setter) pair: getTypeOfSymbol)
            merged.type = ty_object_literal_accessor_type(me, merged.accessor, (merged.flags & PROP_GETTER) != 0);
            merged.accessor = merged.setter = nullptr;
        }
        l = &merged;
        merged.flags = (l->flags & (PROP_OPTIONAL | PROP_METHOD)) | PROP_NO_VALUE_DECLARATION; // (tsc: a fresh symbol)
        merged.own_merged = merged.own || merged.own_merged;
        merged.own = false;
        // (Under exactOptionalPropertyTypes only the missing goes: tsc's removeMissingOrUndefinedType)
        bool exact = compiler_options()->exact_optional_property_types;
        Ty const* right_type = exact ? ty_remove_missing(me, r->type, true) : ty_with_ne_undefined_facts(me, r->type);
        if (l->type && (exact ? ty_remove_missing(me, l->type, true) : ty_with_ne_undefined_facts(me, l->type)) != right_type) {
            Ty const* both[2] = { l->type, right_type };
            merged.type = ty_union_subtype_reduced(me, both, 2);
        }
        // (tsc's merged symbol has both declarations: the types baseline reuses the first annotation that still says
        // the same; a required declaration's annotation only while the merged property is required)
        if (!merged.asserted) merged.asserted = r->asserted ? r->asserted : !(merged.flags & PROP_OPTIONAL) ? r->asserted_required : nullptr;
        merged.asserted_required = nullptr;
        u32 count = l->decl_span_count + r->decl_span_count;
        Span* spans = arena_new_n(&my arena, Span, count ? count : 1);
        for (u32 k = 0; k < l->decl_span_count; k++) spans[k] = l->decl_spans[k];
        for (u32 k = 0; k < r->decl_span_count; k++) spans[l->decl_span_count + k] = r->decl_spans[k];
        merged.decl_spans = spans;
        merged.decl_span_count = count;
        literal_builder_add(&result, merged);
    }
    for (u32 i = 0; i < left->count; i++) {
        if (literal_builder_skips(right, left->props[i].name)) continue;
        if (!literal_builder_find((LiteralBuilder*)right, left->props[i].name)) {
            literal_builder_add(&result, spread_accessor(me, left->props[i], readonly));
        }
    }
    // Index signatures: right's into nothing, else the keys both have (tsc's getUnionIndexInfos)
    if (left->empty_object) {
        result.index_count = right->index_count;
        for (u32 i = 0; i < right->index_count; i++) result.index[i] = right->index[i];
    } else {
        for (u32 i = 0; i < left->index_count; i++) {
            for (u32 j = 0; j < right->index_count; j++) {
                if (left->index[i].key != right->index[j].key) continue;
                // (A new one, without a declaration)
                IndexInfo info = { .key = left->index[i].key, .param_name = my atoms.x };
                // (getUnionType's literal reduction, not subtypes: identical function types stay apart)
                Ty const* values[2] = { left->index[i].value, right->index[j].value };
                info.value = ty_union(me, values, 2, nullptr, (TyList){0});
                info.readonly = left->index[i].readonly && right->index[j].readonly;
                result.index[result.index_count++] = info;
            }
        }
    }
    literal_builder_drop(left);
    *left = result;
}

// What `{ ...a, x: 1, ...b }` is made of, as tsc's getSpreadType makes it: a union (one alternative per combination of
// the members of the unions spread) of intersections, `{ a } & T & { b }` in order, only neighbouring non-generic
// objects merged. A part is a generic object type or the objects spread since the last one
typedef struct SpreadPart {
    Ty const* generic; // Else `object`
    LiteralBuilder object;
} SpreadPart;

typedef struct SpreadAlt {
    SpreadPart* parts;
    u32 part_count;
    // A union member null or undefined was spread into it: without parts it is still a new object with the literal's
    // symbol (tsc's getSpreadType returns its left side, emptyObjectType at first, only for primitives and `object`)
    bool nullable_spread;
} SpreadAlt;

typedef struct SpreadAcc {
    SpreadAlt* alts; // None until something is spread (tsc's emptyObjectType)
    u32 alt_count;
    bool any; // Something `any` was spread: the whole literal is any
    bool readonly; // In a const context: what's spread is readonly
    // The last thing spread was no object (TS2698): the literal is errorType (tsc's checkObjectLiteral; a spread after
    // it makes any of it, getSpreadType)
    bool error;
    // A written member's type contains a widening null or undefined (OBJECT_CONTAINS_WIDENING)
    bool own_widening;
} SpreadAcc;

static void spread_alt_push(SpreadAlt* alt, SpreadPart part) {
    alt->parts = realloc(alt->parts, sizeof(SpreadPart) * (alt->part_count + 1));
    panic_if(!alt->parts, "out of memory");
    alt->parts[alt->part_count++] = part;
}

static void spread_alt_drop(SpreadAlt* alt) {
    for (u32 i = 0; i < alt->part_count; i++) {
        if (!alt->parts[i].generic) literal_builder_drop(&alt->parts[i].object);
    }
    free(alt->parts);
    *alt = (SpreadAlt){0};
}

static SpreadAlt spread_alt_clone(SpreadAlt const* alt) {
    SpreadAlt ret = { .nullable_spread = alt->nullable_spread };
    for (u32 i = 0; i < alt->part_count; i++) {
        SpreadPart const* part = &alt->parts[i];
        spread_alt_push(&ret, part->generic ? *part : (SpreadPart){ .object = literal_builder_clone(&part->object) });
    }
    return ret;
}

static void spread_acc_add_alt(SpreadAcc* acc, SpreadAlt alt) {
    acc->alts = realloc(acc->alts, sizeof(SpreadAlt) * (acc->alt_count + 1));
    panic_if(!acc->alts, "out of memory");
    acc->alts[acc->alt_count++] = alt;
}

// An object spread into an alternative: into its last part if that's objects, else a new part
static void spread_alt_object(Checker* me, SpreadAlt* alt, LiteralBuilder const* right, bool readonly) {
    // After a generic intersection whose last member is a non-generic object, `T & { a }`, that member takes the
    // object spread: `T & { a, b }` (tsc's getSpreadType: lastLeft)
    Ty const* last_generic = alt->part_count ? alt->parts[alt->part_count - 1].generic : nullptr;
    // (Or an object type spread whole after a generic one, see spread_alt_type(): spread into a new object now)
    if (last_generic && (last_generic->flags & TYF_OBJECT) && !ty_is_generic_object(me, last_generic)) {
        alt->part_count--;
        LiteralBuilder left = { .empty_object = false };
        spread_props_of(me, last_generic, false, readonly, &left);
        spread_alt_push(alt, (SpreadPart){ .object = left });
    } else if (last_generic && last_generic->kind == TY_INTERSECTION) {
        TyList members = last_generic->composite.types;
        Ty const* last = members.data[members.len - 1];
        if ((last->flags & TYF_OBJECT) && !ty_is_generic_object(me, last)) {
            alt->parts[alt->part_count - 1].generic = members.len == 2 ? members.data[0] :
                ty_intersection(me, members.data, members.len - 1, nullptr, (TyList){0});
            LiteralBuilder left = { .empty_object = false };
            spread_props_of(me, last, false, readonly, &left);
            spread_alt_push(alt, (SpreadPart){ .object = left });
        }
    }
    if (!alt->part_count || alt->parts[alt->part_count - 1].generic) {
        spread_alt_push(alt, (SpreadPart){ .object = { .empty_object = true } });
    }
    spread_into(me, &alt->parts[alt->part_count - 1].object, right, readonly);
}

// A generic object spread into an alternative: the intersection with it, or itself after nothing (tsc's getSpreadType
// returns the generic right side for an empty left)
static void spread_alt_generic(SpreadAlt* alt, Ty const* generic) {
    bool empty = true;
    for (u32 i = 0; i < alt->part_count && empty; i++) {
        SpreadPart const* part = &alt->parts[i];
        empty = !part->generic && !part->object.count && !part->object.index_count;
    }
    if (empty) {
        spread_alt_drop(alt);
    }
    spread_alt_push(alt, (SpreadPart){ .generic = generic });
}

// Properties spread into what was spread so far (the ones written since the last spread, an object spread)
static void spread_acc_props(Checker* me, SpreadAcc* acc, LiteralBuilder const* right) {
    if (!acc->alt_count) spread_acc_add_alt(acc, (SpreadAlt){0});
    for (u32 i = 0; i < right->count; i++) {
        if (right->props[i].type && (right->props[i].type->xflags & TYX_CONTAINS_WIDENING)) acc->own_widening = true;
    }
    for (u32 i = 0; i < acc->alt_count; i++) spread_alt_object(me, &acc->alts[i], right, acc->readonly);
}

// Empty for tsc's spread purposes (isEmptyObjectTypeOrSpreadsIntoEmptyObject): no properties, or a primitive
static bool spreads_into_nothing(Checker* me, Ty const* ty) {
    // (An intersection only if every member is an empty object, isEmptyObjectType: `T & undefined` is generic)
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* member = ty->composite.types.data[i];
            if (!(member->flags & TYF_OBJECT) || !spreads_into_nothing(me, member)) return false;
        }
        return true;
    }
    if (ty->flags & (TYF_NULLABLE | TYF_BOOLEAN | TYF_BOOLEAN_LITERAL | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT |
        TYF_BIGINT_LITERAL | TYF_STRING_LIKE | TYF_ENUM | TYF_NON_PRIMITIVE | TYF_VOID)) {
        return true;
    }
    if (ty->kind == TY_KEYOF) return true;
    if (!(ty->flags & TYF_OBJECT) || (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED))) return false;
    // (Nor a generic mapped type: tsc's isEmptyObjectType says !isGenericMappedType)
    if (ty->kind == TY_ANONYMOUS && ty_generic_mapped(me, ty)) return false;
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->index_count && !members->call_count && !members->construct_count;
}

// One type (not a union) spread into an alternative. False if it's any
static bool spread_alt_type(Checker* me, SpreadAlt* alt, Ty const* member, bool partial, bool readonly) {
    if (!partial && ty_is_generic_object(me, member)) {
        spread_alt_generic(alt, member);
        return true;
    }
    if (!partial && spreads_into_nothing(me, member) && !(member->flags & TYF_OBJECT)) return true;
    // An object type spread right after a generic one is intersected as it is, `T & Record<string, any>` (tsc's
    // getSpreadType: getIntersectionType([left, right]) once left is generic); what is spread after it merges with it
    // (An intersection too, `T & Data & AllowedAttrs`, getIntersectionType flattening it, after anything generic: it is
    // no object type a last non-generic member of the left side could merge with, isNonGenericObjectType)
    bool left_generic = false;
    for (u32 i = 0; i < alt->part_count && !left_generic; i++) left_generic = alt->parts[i].generic != nullptr;
    if (!partial && left_generic && member->kind == TY_INTERSECTION) {
        spread_alt_push(alt, (SpreadPart){ .generic = member });
        return true;
    }
    if (!partial && alt->part_count && alt->parts[alt->part_count - 1].generic && (member->flags & TYF_OBJECT) &&
        alt->parts[alt->part_count - 1].generic->kind != TY_INTERSECTION &&
        ty_is_generic_object(me, alt->parts[alt->part_count - 1].generic)
    ) {
        spread_alt_push(alt, (SpreadPart){ .generic = member });
        return true;
    }
    Ty const* apparent = ty_apparent_type(me, member);
    if (apparent->flags & TYF_ANY) return false;
    if (!(apparent->flags & TYF_OBJECT) && apparent->kind != TY_INTERSECTION) return true;
    LiteralBuilder right = { .empty_object = false };
    spread_props_of(me, apparent, partial, readonly, &right);
    spread_alt_object(me, alt, &right, readonly);
    literal_builder_drop(&right);
    return true;
}

// Something spread: `...x`
static void spread_acc_type(Checker* me, SpreadAcc* acc, Ty const* type) {
    acc->error = false;
    if (acc->any) return; // (Anything spread into any is any)
    if (type->flags & TYF_ANY) {
        acc->any = true;
        return;
    }
    TyList members = type->kind == TY_UNION && !(type->flags & TYF_BOOLEAN) ? type->composite.types : (TyList){ .data = &type, .len = 1 };
    // A union of one object with things that spread into nothing is that object, every property optional (tsc's
    // tryMergeUnionOfObjectTypeAndEmptyObject)
    Ty const* partial = nullptr;
    Ty const* empty = nullptr;
    if (members.len > 1) {
        u32 objects = 0;
        for (u32 i = 0; i < members.len; i++) {
            if (!spreads_into_nothing(me, members.data[i])) {
                objects++;
                partial = members.data[i];
            }
        }
        // A union of only things that spread into nothing spreads its first empty object type, `{}` of `{} | undefined`,
        // or `object` (tsc's tryMergeUnionOfObjectTypeAndEmptyObject: then getSpreadType keeps only the index
        // signatures both sides have, none for `{}`, and returns the left side for `object`)
        if (objects == 0) {
            for (u32 i = 0; i < members.len && !empty; i++) {
                Ty const* member = members.data[i];
                if (member->flags & (TYF_OBJECT | TYF_NON_PRIMITIVE) || member->kind == TY_INTERSECTION) empty = member;
            }
            if (!empty) return;
        }
        if (objects > 1) partial = nullptr;
        if (empty) members = (TyList){ .data = &empty, .len = 1 };
    }
    if (partial || members.len == 1) {
        Ty const* object = partial ? partial : members.data[0];
        if (partial || !ty_is_generic_object(me, object)) {
            Ty const* apparent = ty_apparent_type(me, object);
            if (apparent->flags & TYF_ANY) {
                acc->any = true;
                return;
            }
            if (!(apparent->flags & TYF_OBJECT) && apparent->kind != TY_INTERSECTION) return;
            // (An empty object still makes what was spread a new object rather than tsc's emptyObjectType: index
            // signatures written after it are the ones both have, none)
            if (!partial && spreads_into_nothing(me, object) && !(object->flags & TYF_OBJECT)) return;
        }
        if (!acc->alt_count) spread_acc_add_alt(acc, (SpreadAlt){0});
        for (u32 i = 0; i < acc->alt_count; i++) {
            if (!spread_alt_type(me, &acc->alts[i], object, partial != nullptr, acc->readonly)) {
                acc->any = true;
                return;
            }
        }
        return;
    }
    // Member by member, each into each of what was spread so far (tsc's getSpreadType maps over a union), unless the
    // cross product is too large (tsc's checkCrossProductUnion: TS2590 at the node being checked, and the error type,
    // which makes the rest of the spreads any)
    if (!acc->alt_count) spread_acc_add_alt(acc, (SpreadAlt){0});
    if ((u64)acc->alt_count * members.len >= 100000) {
        ty_error_at_current_node_d(me, 2590, "Expression produces a union type that is too complex to represent.");
        acc->any = true;
        return;
    }
    SpreadAcc next = {0};
    for (u32 i = 0; i < acc->alt_count; i++) {
        for (u32 j = 0; j < members.len; j++) {
            SpreadAlt alt = spread_alt_clone(&acc->alts[i]);
            Ty const* member = members.data[j];
            if (!spreads_into_nothing(me, member)) spread_alt_type(me, &alt, member, false, acc->readonly);
            else if (member->flags & (TYF_NULLABLE | TYF_VOID)) {
                alt.nullable_spread = true;
                // (tsc's getSpreadType merges null or undefined like an object, getUnionIndexInfos keeping the index
                // signatures it has too: none. A generic left side is intersected with it instead)
                if (alt.part_count == 1 && !alt.parts[0].generic) {
                    alt.parts[0].object.index_count = 0;
                    alt.parts[0].object.empty_object = false;
                } else if (!alt.part_count) {
                    // (Nothing spread before: tsc's left side is emptyObjectType, so the new object takes the null's index
                    // signatures, none, and is no longer emptyObjectType, which a later spread's index signatures would
                    // be copied into whole: `{ ...(A | B | undefined), ...Record<string, true> }` is `{}` there)
                    spread_alt_push(&alt, (SpreadPart){ .object = { .empty_object = false } });
                } else if (alt.parts[alt.part_count - 1].generic) {
                    // (After a generic object it is intersected with it, tsc's getIntersectionType([left, right]):
                    // `T & undefined` is never for `T extends object`, and that alternative drops out)
                    Ty const* pair[2] = { alt.parts[alt.part_count - 1].generic, member };
                    Ty const* intersection = ty_intersection(me, pair, 2, nullptr, (TyList){0});
                    if (intersection->flags & TYF_NEVER) {
                        spread_alt_drop(&alt);
                        continue;
                    }
                    alt.parts[alt.part_count - 1].generic = intersection;
                }
            }
            spread_acc_add_alt(&next, alt);
        }
        spread_alt_drop(&acc->alts[i]);
    }
    free(acc->alts);
    acc->alts = next.alts;
    acc->alt_count = next.alt_count;
}

// What the literal is. `own`: the properties written after the last spread (the whole literal if nothing was spread)
static Ty const* spread_acc_finish(Checker* me, SpreadAcc* acc, LiteralBuilder* own, ObjectLiteralKind kind, Expr const* expr) {
    Ty const* ret;
    if (acc->any) {
        ret = acc->error ? ty_intrinsic(me, TY_ERROR) : any_type(me);
    } else if (!acc->alt_count) {
        // (`{}` too: a type of its own, see ty_object_literal_of())
        ret = ty_object_literal_of(me, own->props, own->count, own->index, own->index_count, kind, expr);
    } else {
        if (own->count || own->index_count) spread_acc_props(me, acc, own);
        kind |= OBJECT_CONTAINS_SPREAD;
        if (acc->own_widening && !compiler_options()->strict_null_checks) kind |= OBJECT_CONTAINS_WIDENING;
        TyBuf alts = {0};
        TyBuf parts = {0};
        for (u32 i = 0; i < acc->alt_count; i++) {
            SpreadAlt const* alt = &acc->alts[i];
            parts.len = 0;
            for (u32 j = 0; j < alt->part_count; j++) {
                SpreadPart const* part = &alt->parts[j];
                ty_buf_push(&parts, part->generic ? part->generic :
                    ty_object_literal_alternative(me, part->object.props, part->object.count, part->object.index,
                        part->object.index_count, kind, expr, acc->alt_count > 1 ? i + 1 : 0));
            }
            // (A union member null or undefined is still a new object with the literal's symbol: tsc's getSpreadType
            // gets there for it, sorted by that symbol and not last like a symbolless one, `{} | {} | { a: number }`
            // for `{ ...(f | { a: number } | undefined) }`)
            ty_buf_push(&alts, parts.len == 0 ? (acc->alt_count > 1 && alt->nullable_spread ?
                    ty_object_literal_alternative(me, nullptr, 0, nullptr, 0, kind, expr, i + 1) :
                    ty_object_literal(me, nullptr, 0, nullptr, 0, kind)) :
                parts.len == 1 ? parts.data[0] : ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0}));
        }
        ret = alts.len == 1 ? alts.data[0] : ty_union(me, alts.data, alts.len, nullptr, (TyList){0});
        ty_buf_drop(&parts);
        ty_buf_drop(&alts);
    }
    for (u32 i = 0; i < acc->alt_count; i++) spread_alt_drop(&acc->alts[i]);
    free(acc->alts);
    return ret;
}

static Signature const* single_call_signature(Checker* me, Ty const* ty) {
    Members const* members = ty_members(me, ty);
    if (members->call_count != 1) return nullptr;
    signature_resolve(me, members->call[0]);
    return members->call[0];
}

// tsc's isPossiblyDiscriminantValue: what can say which member of a union an object literal is meant to be
static bool is_possibly_discriminant_value(Expr const* expr) {
    switch (expr->tag) {
        case EXPR_LITERAL:
        case EXPR_IDENTIFIER:
            return true;
        case EXPR_PROPERTY:
            return is_possibly_discriminant_value(&expr->property->expr);
        case EXPR_TEMPLATE:
            return !expr->template_->tag;
        default:
            return false;
    }
}

// A property that may pick members of a union contextual type, with the type it has
typedef struct Discriminator {
    Atom const* name;
    Ty const* type;
} Discriminator;

// tsc's discriminateTypeByDiscriminableItems: the members of a union contextual type whose properties the
// discriminators fit (a discriminator that fits none excludes nothing)
static Ty const* discriminate_by_items(Checker* me, Ty const* contextual, Discriminator const* items, u32 count) {
    TyList types = contextual->composite.types;
    // 0: excluded, 1: maybe, 3: included (any number of members: tsc has no limit, slotsmith's DeepKeys-mapped union
    // of a recursive row type has hundreds)
    unsigned char include_small[64];
    unsigned char* include = types.len <= 64 ? include_small : malloc(types.len);
    panic_if(!include, "out of memory");
    u32 const primitive = TYF_STRING | TYF_NUMBER | TYF_BIGINT | TYF_ES_SYMBOL_LIKE | TYF_BOOLEAN | TYF_LITERAL | TYF_ENUM |
        TYF_NULLABLE | TYF_VOID;
    // (Nor a member that reduces to never, tsc 6.0)
    for (u32 i = 0; i < types.len; i++) {
        include[i] = types.data[i]->flags & primitive || (ty_reduced(me, types.data[i])->flags & TYF_NEVER) ? 0 : 3;
    }
    for (u32 d = 0; d < count; d++) {
        Ty const* value = items[d].type;
        Ty const* key = ty_prop_name_literal(me, items[d].name);
        bool matched = false;
        for (u32 i = 0; i < types.len; i++) {
            if (!include[i]) continue;
            Ty const* target = ty_access(me, types.data[i], key);
            // (A member without the property stays in: tsc 6.0)
            if (!target) continue;
            bool related = false;
            {
                Ty const* const* parts = &value;
                u32 part_count = 1;
                if (value->kind == TY_UNION) {
                    parts = value->composite.types.data;
                    part_count = value->composite.types.len;
                }
                for (u32 j = 0; j < part_count && !related; j++) related = ty_is_related(me, parts[j], target, TY_RELATION_ASSIGNABLE);
            }
            if (related) matched = true;
            else include[i] = 1;
        }
        for (u32 i = 0; i < types.len; i++) {
            if (include[i] == 1) include[i] = matched ? 0 : 3;
        }
    }
    Ty const* kept_small[64];
    Ty const** kept = types.len <= 64 ? kept_small : malloc(sizeof(Ty const*) * types.len);
    panic_if(!kept, "out of memory");
    u32 len = 0;
    for (u32 i = 0; i < types.len; i++) {
        if (include[i]) kept[len++] = types.data[i];
    }
    Ty const* ret = len == types.len || !len ? contextual : ty_union_unreduced(me, kept, len); // (tsc's filterType)
    if (include != include_small) free(include);
    if (kept != kept_small) free(kept);
    return ret;
}

// The optional discriminant properties of a union contextual type that a literal doesn't have are undefined in it (the
// second half of tsc's discriminateContextualTypeBy*). `has(name)`: whether the literal has one of that name. `skip`:
// a name not to count (JSX children given as children)
static void push_missing_discriminators(
    Checker* me, Ty const* contextual, bool (*has)(void const*, Checker*, Atom const*), void const* literal,
    Atom const* skip, Discriminator** items, u32* count
) {
    // (The names come from the first member, whose parts an intersection's are)
    Ty const* first = ty_apparent_type(me, contextual->composite.types.data[0]);
    TyList parts = first->kind == TY_INTERSECTION ? first->composite.types : (TyList){ .data = &first, .len = 1 };
    for (u32 p = 0; p < parts.len; p++) {
        Ty const* part = ty_apparent_type(me, parts.data[p]);
        if (!(part->flags & TYF_OBJECT)) continue;
        Members const* members = ty_members(me, part);
        for (u32 i = 0; i < members->prop_count; i++) {
            Atom const* name = members_prop_name(members, i);
            if (name == skip || has(literal, me, name)) continue;
            // A property of the union (every member has it), optional if it is in some member
            bool in_all = true, optional = false;
            for (u32 j = 0; j < contextual->composite.types.len && in_all; j++) {
                FoundProp found = ty_find_prop(me, ty_apparent_type(me, contextual->composite.types.data[j]), name);
                in_all = found.found;
                optional |= found.found && (found.flags & PROP_OPTIONAL);
            }
            if (!in_all || !optional || !ty_is_discriminant_property(me, contextual, name)) continue;
            *items = realloc(*items, sizeof(Discriminator) * (*count + 1));
            panic_if(!*items, "out of memory");
            (*items)[(*count)++] = (Discriminator){ .name = name, .type = ty_intrinsic(me, TY_UNDEFINED) };
        }
    }
}

// (The literal's member names, atomized once)
typedef struct MemberNames { Atom const** data; u32 len; } MemberNames;

static bool object_has_member(void const* literal, Checker* me, Atom const* name) {
    (void)me;
    MemberNames const* names = literal;
    for (u32 i = 0; i < names->len; i++) {
        if (names->data[i] == name) return true;
    }
    return false;
}

// tsc's discriminateContextualTypeByObjectMembers
static Ty const* discriminate_by_object_members(Checker* me, Ty const* contextual, ObjectExpr const* object) {
    Discriminator* items = nullptr;
    u32 count = 0;
    for_each (item, vec_object_expr_item, object) {
        bool candidate = item->tag == OBJECT_ITEM_SHORTHAND ||
            (item->tag == OBJECT_ITEM_PROPERTY && is_possibly_discriminant_value(&item->value));
        if (!candidate || item->computed_key) continue;
        Atom const* name = checker_atom(me, cstr_sv(&item->name_string));
        if (!ty_is_discriminant_property(me, contextual, name)) continue;
        items = realloc(items, sizeof(Discriminator) * (count + 1));
        panic_if(!items, "out of memory");
        items[count++] = (Discriminator){ .name = name, .type = ty_expr_type(me, &item->value) };
    }
    MemberNames names = { .data = malloc(sizeof(Atom const*) * (vec_object_expr_item_size(object) + 1)) };
    panic_if(!names.data, "out of memory");
    for_each (item, vec_object_expr_item, object) {
        if (item->tag != OBJECT_ITEM_SPREAD && !item->computed_key) names.data[names.len++] = checker_atom(me, cstr_sv(&item->name_string));
    }
    push_missing_discriminators(me, contextual, object_has_member, &names, nullptr, &items, &count);
    free(names.data);
    Ty const* ret = discriminate_by_items(me, contextual, items, count);
    free(items);
    return ret;
}

static bool attributes_have(void const* literal, Checker* me, Atom const* name) {
    Expr const* attributes = literal;
    for_each (i, vec_jsx_attribute, &attributes->jsx_attributes) {
        if (i->tag != JSX_ATTRIBUTE_SPREAD && checker_atom(me, span_sv(i->name)) == name) return true;
    }
    return false;
}

// tsc's discriminateContextualTypeByJSXAttributes: an attribute without a value is `true`
static Ty const* discriminate_by_jsx_attributes(Checker* me, Ty const* contextual, Expr const* attributes) {
    Discriminator* items = nullptr;
    u32 count = 0;
    for_each (i, vec_jsx_attribute, &attributes->jsx_attributes) {
        if (i->tag == JSX_ATTRIBUTE_SPREAD) continue;
        bool has_value = i->tag != JSX_ATTRIBUTE_NO_VALUE && i->tag != JSX_ATTRIBUTE_EMPTY_EXPR;
        if (has_value && !is_possibly_discriminant_value(&i->value)) continue;
        Atom const* name = checker_atom(me, span_sv(i->name));
        if (!ty_is_discriminant_property(me, contextual, name)) continue;
        items = realloc(items, sizeof(Discriminator) * (count + 1));
        panic_if(!items, "out of memory");
        items[count++] = (Discriminator){ .name = name, .type = has_value ? ty_expr_type(me, &i->value) : ty_intrinsic(me, TY_TRUE) };
    }
    JsxElement const* element = jsx_attributes_element(attributes);
    Atom const* children = ty_jsx_children_prop_name(me, element);
    u32 child_count = 0;
    for_each (i, vec_jsx_child, &element->children) child_count += ty_jsx_child_is_semantic(i);
    push_missing_discriminators(me, contextual, attributes_have, attributes, child_count ? children : nullptr, &items, &count);
    Ty const* ret = discriminate_by_items(me, contextual, items, count);
    free(items);
    return ret;
}


// tsc's propertiesArray since the last spread, for the index signatures computed names make
// (getObjectLiteralIndexInfo): every member's type and what its name is
typedef struct LiteralMember {
    Ty const* type; // Null for an accessor: typed from it when needed
    FunctionImpl const* accessor;
    bool getter;
    bool numeric_name, symbol_name; // (A computed name of type `any` is both)
    Expr const* computed_key;
    ObjectExprItem const* item;
} LiteralMember;
typedef struct LiteralMembers {
    LiteralMember* items;
    u32 count, cap;
    bool computed[3]; // A computed name made an index signature of this key: string, number, symbol
} LiteralMembers;

static IndexInfo literal_members_index(Checker* me, LiteralMembers const* members, TyIntrinsic key, bool readonly) {
    Ty const** types = malloc(sizeof(Ty const*) * (members->count ? members->count : 1));
    IndexComponent* components = arena_new_n(&my arena, IndexComponent, members->count ? members->count : 1);
    u32 n = 0, component_count = 0;
    for (u32 i = 0; i < members->count; i++) {
        LiteralMember const* m = &members->items[i];
        bool included = key == TY_STRING ? !m->symbol_name : key == TY_NUMBER ? m->numeric_name : m->symbol_name;
        if (!included) continue;
        types[n] = m->type ? m->type : ty_object_literal_accessor_type(me, m->accessor, m->getter);
        if (m->computed_key) components[component_count++] = (IndexComponent){ .name = m->computed_key, .type = types[n], .item = m->item };
        n++;
    }
    IndexInfo ret = {
        .key = ty_intrinsic(me, key),
        .value = n ? ty_union_subtype_reduced(me, types, n) : ty_intrinsic(me, TY_UNDEFINED),
        .param_name = my atoms.x,
        .readonly = readonly,
        .components = components,
        .component_count = component_count,
    };
    free(types);
    return ret;
}

// The index signatures of the members since the last spread, into the builder; then starts over
static void literal_members_flush(Checker* me, LiteralMembers* members, IndexInfo* index, u32* index_count, bool readonly) {
    static TyIntrinsic const keys[3] = { TY_STRING, TY_NUMBER, TY_ES_SYMBOL };
    for (u32 key = 0; key < 3; key++) {
        if (members->computed[key]) index[(*index_count)++] = literal_members_index(me, members, keys[key], readonly);
    }
    members->count = 0;
    members->computed[0] = members->computed[1] = members->computed[2] = false;
}

static void literal_members_push(LiteralMembers* members, LiteralMember member) {
    if (members->count == members->cap) {
        members->cap = members->cap ? members->cap * 2 : 8;
        members->items = realloc(members->items, sizeof(LiteralMember) * members->cap);
    }
    members->items[members->count++] = member;
}

// tsc's getApparentTypeOfContextualType for an object literal: type variables (of a union too) as their constraints,
// which the literal's members then discriminate
// (tsc's getApparentTypeOfContextualType maps getApparentType over a union: a primitive member is its wrapper, whose
// properties say something about the literal's, `string | { normalize?: F }` both String's normalize and F)
static bool is_wrapped_primitive(Ty const* ty) {
    return (ty->flags & (TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_BIGINT | TYF_BIGINT_LITERAL | TYF_BOOLEAN |
        TYF_BOOLEAN_LITERAL | TYF_ES_SYMBOL_LIKE)) && ty->kind != TY_UNION;
}

static Ty const* apparent_literal_context(Checker* me, Ty const* contextual) {
    if (is_contextual_type_variable(contextual)) {
        Ty const* apparent = ty_apparent_type(me, contextual);
        return apparent->kind == TY_INTRINSIC && (apparent->flags & TYF_UNKNOWN) ? contextual : apparent;
    }
    if (contextual->kind == TY_INTERSECTION && (contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        // tsc's getApparentTypeOfIntersectionType is the intersection of each member's apparent type. The properties'
        // contexts already take a type variable member as its constraint (intersection_prop_context()), so only what
        // that intersection absorbs into is left: a member whose constraint is any (an unresolved one) makes it any,
        // `T & { prop: boolean }` with `T extends Missing` widens `prop: true`. (Building the intersection here costs:
        // xstate +2.4% instructions)
        TyList types = contextual->composite.types;
        for (u32 i = 0; i < types.len; i++) {
            if (!is_contextual_type_variable(types.data[i])) continue;
            Ty const* apparent = ty_apparent_type(me, types.data[i]);
            if (apparent->flags & TYF_ANY) return apparent;
        }
        return contextual;
    }
    if (contextual->kind != TY_UNION || (contextual->flags & TYF_BOOLEAN)) return contextual;
    bool primitive = false;
    for (u32 i = 0; i < contextual->composite.types.len && !primitive; i++) {
        primitive = is_wrapped_primitive(contextual->composite.types.data[i]);
    }
    if (!primitive && !(contextual->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) return contextual;
    TyBuf parts = {0};
    bool changed = false;
    for (u32 i = 0; i < contextual->composite.types.len; i++) {
        Ty const* part = contextual->composite.types.data[i];
        Ty const* apparent = is_wrapped_primitive(part) ? ty_apparent_type(me, part) : apparent_literal_context(me, part);
        changed |= apparent != part;
        ty_buf_push(&parts, apparent);
    }
    // (Without reductions, mapType's noReductions: an `any` member doesn't absorb the others' properties)
    Ty const* ret = changed ? ty_union_unreduced(me, parts.data, parts.len) : contextual;
    ty_buf_drop(&parts);
    return ret;
}


// tsgo's binder of an object literal's members (bindPropertyOrMethodOrAccessor, declareSymbolEx): a member joins the
// symbol the first member of its name made unless that symbol has a meaning it excludes, then it is a symbol of its own
// that no later member finds. A property excludes methods, a method everything, an accessor methods and the same
// accessor; an accessor conflicting with a member that isn't the other accessor marks the symbol as both accessors.
// Members with a computed name that is an entity name are late bound after all the others (tsgo's lateBindMember):
// among themselves like the binder, by the name their key's type gives, then each late symbol merges into the early
// one of its name unless the early one excludes it (combineSymbolTables), the early members first. Any other
// computed name is a symbol of its own
enum { LIT_SYM_PROPERTY = 1, LIT_SYM_METHOD = 2, LIT_SYM_GET = 4, LIT_SYM_SET = 8 };
typedef struct LiteralSymbol {
    Atom const* name;
    uint8_t flags;
    bool in_table;
    bool late; // Made by a late-bound member
    u32 first; // Item indices
    i32 getter, setter; // -1: none
    u32 member_start, member_count; // Its members' item indices in LiteralBinding.members, in order
} LiteralSymbol;

#define LITERAL_BINDING_INLINE 16
typedef struct LiteralBinding {
    u32 count;
    u32* symbol_of; // Per item, UINT32_MAX for spreads and members without a bound name
    Atom const** names; // Per item, its bound name (null: none)
    LiteralSymbol* symbols;
    u32* members;
    // The late-bound members that conflicted (see ty_check_object_literal_late_members()): with the late symbol of
    // their name (lateBindMember, `symbol` the one it conflicted with), or whose late symbol the early one of its name
    // excludes (mergeSymbol, `symbol` the early one, `item` the late symbol's first member)
    struct LiteralConflict { u32 item, symbol; bool merge; }* conflicts;
    u32 conflict_count;
    // (Small literals don't allocate)
    u32 symbol_of_buf[LITERAL_BINDING_INLINE];
    Atom const* names_buf[LITERAL_BINDING_INLINE];
    LiteralSymbol symbols_buf[LITERAL_BINDING_INLINE];
    u32 members_buf[LITERAL_BINDING_INLINE];
} LiteralBinding;

static Atom const* literal_item_bound_name(Checker* me, ObjectExprItem const* item) {
    if (item->tag == OBJECT_ITEM_SPREAD) return nullptr;
    // (A bigint name is tsc's __missing, a symbol outside the table)
    if (!item->computed_key && span_is_bigint_name(item->name_span, item->name_is_value)) return nullptr;
    if (!item->computed_key) return checker_atom(me, cstr_sv(&item->name_string));
    // (A literal key is bound by its value, `[-1]` and `[+1]` as written: tsc's getDeclarationName)
    Expr const* key = item->computed_key;
    if (!computed_key_early_name(key, nullptr)) return nullptr;
    bool is_number = false;
    if (key->tag == EXPR_LITERAL) return ty_property_name_of_key(me, ty_expr_type(me, key), &is_number);
    Atom const* text = ty_property_name_of_key(me, ty_expr_type(me, &key->unary_op->expr), &is_number);
    if (!text) return nullptr;
    cstr name = cstr_d(key->unary_op->op == UNARY_OP_MINUS ? "-" : "+", atom_sv(text));
    Atom const* ret = checker_atom(me, cstr_sv(&name));
    cstr_drop(&name);
    return ret;
}

// A member whose computed key is an entity name: late bound (tsc's hasLateBindableName)
static bool literal_item_is_late(ObjectExprItem const* item) {
    return item->tag != OBJECT_ITEM_SPREAD && item->computed_key && !expr_is_perenthisized(item->computed_key) &&
        !computed_key_early_name(item->computed_key, nullptr) && expr_is_entity_name(item->computed_key);
}

static uint8_t literal_item_excludes(uint8_t includes) {
    return includes == LIT_SYM_PROPERTY ? LIT_SYM_METHOD : includes == LIT_SYM_METHOD ? 0xF :
        includes == LIT_SYM_GET ? LIT_SYM_METHOD | LIT_SYM_GET : LIT_SYM_METHOD | LIT_SYM_SET;
}

// What a meaning excludes to late binding and mergeSymbol (tsgo's getExcludedSymbolFlags: the binder's object literal
// methods exclude every value, these only the other meanings)
static uint8_t literal_late_excludes(uint8_t includes) {
    return includes == LIT_SYM_PROPERTY ? LIT_SYM_METHOD : includes == LIT_SYM_METHOD ? LIT_SYM_PROPERTY | LIT_SYM_GET | LIT_SYM_SET :
        includes == LIT_SYM_GET ? LIT_SYM_METHOD | LIT_SYM_GET : LIT_SYM_METHOD | LIT_SYM_SET;
}

static uint8_t literal_item_symbol_flags(ObjectExprItem const* item) {
    switch (item->tag) {
        case OBJECT_ITEM_METHOD: return LIT_SYM_METHOD;
        case OBJECT_ITEM_GETTER: return LIT_SYM_GET;
        case OBJECT_ITEM_SETTER: return LIT_SYM_SET;
        default: return LIT_SYM_PROPERTY;
    }
}

static void literal_binding_conflict(LiteralBinding* me, u32 item, u32 symbol, bool merge) {
    my conflicts = realloc(my conflicts, sizeof(*my conflicts) * (my conflict_count + 1));
    panic_if(!my conflicts, "out of memory");
    my conflicts[my conflict_count++] = (struct LiteralConflict){ item, symbol, merge };
}

static void literal_bind(Checker* me, VecObjectExprItem const* object, LiteralBinding* out) {
    u32 n = (u32)vec_object_expr_item_size(object);
    LiteralBinding* r = out;
    r->count = 0;
    r->conflicts = nullptr;
    r->conflict_count = 0;
    bool small = n <= LITERAL_BINDING_INLINE;
    r->symbol_of = small ? r->symbol_of_buf : malloc(sizeof(u32) * n);
    r->names = small ? r->names_buf : malloc(sizeof(Atom const*) * n);
    r->symbols = small ? r->symbols_buf : malloc(sizeof(LiteralSymbol) * n);
    r->members = small ? r->members_buf : malloc(sizeof(u32) * n);
    #define ret (*r)
    // (Without a name written twice every member is a symbol of its own: none are made)
    // (Nor without a late-bound member among others, whose name its key's type gives)
    bool any_late = false;
    if (small) {
        bool repeated = false;
        for (u32 k = 0; k < n; k++) {
            Atom const* name = literal_item_bound_name(me, &object->data[k]);
            ret.names[k] = name;
            ret.symbol_of[k] = UINT32_MAX;
            for (u32 j = 0; j < k && name && !repeated; j++) repeated = ret.names[j] == name;
            any_late |= literal_item_is_late(&object->data[k]);
        }
        if (!repeated && !(any_late && n > 1)) return;
    }
    // The table: symbol indices by name (open addressing on the atom)
    // (A small literal searches its symbols instead)
    u32 slots = 16;
    while (slots < n * 2) slots <<= 1;
    u32* table = small ? nullptr : malloc(sizeof(u32) * slots);
    if (table) memset(table, 0xFF, sizeof(u32) * slots);
    u32 small_slot = UINT32_MAX;
    for (u32 k = 0; k < n; k++) {
        ObjectExprItem const* item = &object->data[k];
        ret.symbol_of[k] = UINT32_MAX;
        Atom const* name = small ? ret.names[k] : literal_item_bound_name(me, item);
        ret.names[k] = name;
        if (!name && !(item->computed_key && item->tag != OBJECT_ITEM_SPREAD)) continue;
        if (literal_item_is_late(item)) {
            any_late = true;
            continue;
        }
        uint8_t includes = literal_item_symbol_flags(item);
        uint8_t excludes = literal_item_excludes(includes);
        u32* slot = nullptr;
        if (name && !table) {
            small_slot = UINT32_MAX;
            for (u32 j = 0; j < ret.count; j++) {
                if (ret.symbols[j].in_table && ret.symbols[j].name == name) small_slot = j;
            }
            slot = &small_slot;
        } else if (name) {
            u32 h = (u32)(((uintptr_t)name >> 4) * 2654435761u) & (slots - 1);
            while (table[h] != UINT32_MAX && ret.symbols[table[h]].name != name) h = (h + 1) & (slots - 1);
            slot = &table[h];
        }
        LiteralSymbol* existing = slot && *slot != UINT32_MAX ? &ret.symbols[*slot] : nullptr;
        if (existing && !(existing->flags & excludes)) {
            existing->flags |= includes;
            ret.symbol_of[k] = (u32)(existing - ret.symbols);
        } else {
            if (existing && (existing->flags & (LIT_SYM_GET | LIT_SYM_SET)) &&
                (existing->flags & (LIT_SYM_GET | LIT_SYM_SET)) != (includes & (LIT_SYM_GET | LIT_SYM_SET))
            ) {
                existing->flags |= LIT_SYM_GET | LIT_SYM_SET;
            }
            ret.symbols[ret.count] = (LiteralSymbol){
                .name = name, .flags = includes, .in_table = slot && !existing, .first = k, .getter = -1, .setter = -1,
            };
            if (slot && !existing) *slot = ret.count;
            ret.symbol_of[k] = ret.count++;
        }
        LiteralSymbol* symbol = &ret.symbols[ret.symbol_of[k]];
        symbol->member_count++;
        if (item->tag == OBJECT_ITEM_GETTER && symbol->getter < 0) symbol->getter = (i32)k;
        if (item->tag == OBJECT_ITEM_SETTER && symbol->setter < 0) symbol->setter = (i32)k;
    }
    free(table);
    // The late-bound members (names aren't kept: the checker asks the key's type)
    u32 early_count = ret.count;
    for (u32 k = 0; any_late && k < n; k++) {
        ObjectExprItem const* item = &object->data[k];
        if (!literal_item_is_late(item)) continue;
        bool is_number = false;
        Atom const* name = ty_property_name_of_key(me, ty_expr_type(me, item->computed_key), &is_number);
        uint8_t includes = literal_item_symbol_flags(item);
        LiteralSymbol* existing = nullptr;
        for (u32 j = early_count; name && j < ret.count && !existing; j++) {
            if (ret.symbols[j].in_table && ret.symbols[j].name == name) existing = &ret.symbols[j];
        }
        if (existing && !(existing->flags & literal_late_excludes(includes))) {
            existing->flags |= includes;
            ret.symbol_of[k] = (u32)(existing - ret.symbols);
        } else {
            // (A conflicting one is a symbol of its own, tsgo's lateBindMember; an accessor makes the symbol both)
            if (existing) literal_binding_conflict(r, k, (u32)(existing - ret.symbols), false);
            if (existing && (existing->flags & (LIT_SYM_GET | LIT_SYM_SET)) &&
                (existing->flags & (LIT_SYM_GET | LIT_SYM_SET)) != (includes & (LIT_SYM_GET | LIT_SYM_SET))
            ) {
                existing->flags |= LIT_SYM_GET | LIT_SYM_SET;
            }
            ret.symbols[ret.count] = (LiteralSymbol){
                .name = name, .flags = includes, .in_table = name && !existing, .late = true, .first = k, .getter = -1, .setter = -1,
            };
            ret.symbol_of[k] = ret.count++;
        }
        LiteralSymbol* symbol = &ret.symbols[ret.symbol_of[k]];
        symbol->member_count++;
        if (item->tag == OBJECT_ITEM_GETTER && symbol->getter < 0) symbol->getter = (i32)k;
        if (item->tag == OBJECT_ITEM_SETTER && symbol->setter < 0) symbol->setter = (i32)k;
    }
    // Each late symbol merges into the early one of its name, unless that one excludes it (mergeSymbol)
    for (u32 l = early_count; l < ret.count; l++) {
        LiteralSymbol* late = &ret.symbols[l];
        if (!late->in_table || !late->member_count) continue;
        LiteralSymbol* early = nullptr;
        for (u32 j = 0; j < early_count && !early; j++) {
            if (ret.symbols[j].in_table && ret.symbols[j].name == late->name) early = &ret.symbols[j];
        }
        if (!early) continue;
        uint8_t excluded = 0;
        for (uint8_t bit = 1; bit <= LIT_SYM_SET; bit <<= 1) {
            if (late->flags & bit) excluded |= literal_late_excludes(bit);
        }
        if (early->flags & excluded) {
            literal_binding_conflict(r, late->first, (u32)(early - ret.symbols), true);
            continue;
        }
        early->flags |= late->flags;
        if (early->getter < 0) early->getter = late->getter;
        if (early->setter < 0) early->setter = late->setter;
        early->member_count += late->member_count;
        late->member_count = 0;
        for (u32 k = 0; k < n; k++) {
            if (ret.symbol_of[k] == l) ret.symbol_of[k] = (u32)(early - ret.symbols);
        }
    }
    u32 offset = 0;
    for (u32 s = 0; s < ret.count; s++) {
        ret.symbols[s].member_start = offset;
        offset += ret.symbols[s].member_count;
        ret.symbols[s].member_count = 0;
    }
    // (The early members of a symbol first, then the late-bound ones)
    for (u32 pass = 0; pass < (any_late ? 2u : 1u); pass++) {
        for (u32 k = 0; k < n; k++) {
            if (ret.symbol_of[k] == UINT32_MAX || (any_late && literal_item_is_late(&object->data[k]) != (pass == 1))) continue;
            LiteralSymbol* symbol = &ret.symbols[ret.symbol_of[k]];
            ret.members[symbol->member_start + symbol->member_count++] = k;
        }
    }
    #undef ret
}

static void literal_binding_drop(LiteralBinding* me) {
    free(my conflicts);
    if (my symbol_of == my symbol_of_buf) return;
    free(my symbol_of);
    free(my names);
    free(my symbols);
    free(my members);
}

// The type tsgo's getTypeOfAccessors gives an object literal's accessor symbol: the getter's, else the setter's
static Ty const* literal_symbol_accessor_type(Checker* me, VecObjectExprItem const* object, LiteralSymbol const* symbol) {
    if (symbol->getter >= 0) return ty_object_literal_accessor_type(me, object->data[symbol->getter].value.function, true);
    return ty_object_literal_accessor_type(me, object->data[symbol->setter].value.function, false);
}

static Ty const* check_object(Checker* me, Expr const* expr, ExprCtx ctx) {
    ObjectExpr const* object = &expr->object;
    if (expr->expando_container) {
        // `var o = {}; o.x = 1`: the container's type (tsgo's object literal symbol has the expandos)
        SourceFile const* file = span_source(expr->inner_span);
        for (u32 i = 0; i < file->expando_container_count; i++) {
            Symbol const* host = file->expando_containers[i].host;
            if (file->expando_containers[i].literal == expr && host->binder == my binder) return ty_namespace_type(me, host);
        }
    }
    Ty const* written_context = ctx.contextual;
    Ty const* written_sig_context = ctx.sig_contextual;
    if (ctx.contextual) ctx.contextual = apparent_literal_context(me, ctx.contextual);
    if (ctx.contextual && ctx.contextual->kind == TY_UNION) {
        ctx.contextual = discriminate_by_object_members(me, ctx.contextual, object);
    }
    if (ctx.sig_contextual) ctx.sig_contextual = apparent_literal_context(me, ctx.sig_contextual);
    if (ctx.sig_contextual && ctx.sig_contextual->kind == TY_UNION) {
        ctx.sig_contextual = discriminate_by_object_members(me, ctx.sig_contextual, object);
    }
    if (ctx.contextual && !ctx.skip_sensitive) {
        // What `this` in its methods goes by (ty_contextual_this_type()). Not while skipping context-sensitive
        // functions: none that would ask is checked
        ty_link_set(me, KEY_LITERAL_CONTEXT, expr, ctx.contextual);
        // (And the generic call inferring with it, if any, whose type parameters a ThisType<T> marker in it fixes)
        u64 serial = ty_current_inference_serial(me);
        u64 inference_key[2] = { KEY_LITERAL_INFERENCE, (u64)(uintptr_t)expr };
        if (serial || intern_get(&my links, inference_key, 2)) {
            *intern_slot(me, &my links, inference_key, 2) = (void const*)(uintptr_t)serial;
        }
    }
    for_each (i, vec_object_expr_item, object) {
        if (i->tag == OBJECT_ITEM_PROPERTY && i->value.tag == EXPR_OBJECT) {
            u64 parent_key[2] = { KEY_LITERAL_PARENT, (u64)(uintptr_t)&i->value };
            *intern_slot(me, &my links, parent_key, 2) = expr;
        }
    }
    LiteralBuilder builder = {0}; // What was written since the last spread (tsc's propertiesTable)
    SpreadAcc spread = { .readonly = ctx.is_const };
    // The implied type of an object binding pattern (`const { a = 1 } = { a: 2 }`): a property is optional where the
    // pattern's is (tsc's checkObjectLiteral, contextualTypeHasPattern)
    Ty const* pattern_context = ctx.contextual && (ctx.contextual->flags & TYF_BINDING_PATTERN) &&
        (ctx.contextual->flags & TYF_OBJECT) && !ty_is_tuple(ctx.contextual) && !pattern_type_has_rest(me, ctx.contextual) &&
        !intern_get(&my links, (u64[]){ KEY_PATTERN_COMPUTED, ctx.contextual->id }, 2) ? ctx.contextual : nullptr;
    LiteralBinding binding;
    literal_bind(me, object, &binding);

    LiteralMembers members = {0};
    bool reserved = false;
    for_each (i, vec_object_expr_item, object) {
        if (!reserved) {
            literal_builder_reserve(&builder, object_items_before_spread(object, i));
            reserved = true;
        }
        if (i->tag == OBJECT_ITEM_SPREAD) {
            literal_members_flush(me, &members, builder.index, &builder.index_count, ctx.is_const);
            if (builder.count || builder.index_count) spread_acc_props(me, &spread, &builder);
            literal_builder_drop(&builder);
            reserved = false;
            // (Its contextual type is the literal's as given: tsc's getContextualType of a SpreadAssignment)
            // (Reduced: the members of a union that are never, by conflicting discriminants, spread nothing; tsc's
            // getReducedType in checkObjectLiteral)
            Ty const* spread_type = ty_reduced(me, check_with(me, &i->value, (ExprCtx){
                .contextual = written_context, .sig_contextual = written_sig_context, .sig_absent = ctx.sig_absent,
                .skip_sensitive = ctx.skip_sensitive,
            }));
            // (A spread of no object is TS2698 and makes the literal errorType, tsc's checkObjectLiteral)
            if (!ty_is_valid_spread_type(me, spread_type)) spread.any = spread.error = true;
            else spread_acc_type(me, &spread, spread_type);
            continue;
        }

        // The name
        Atom const* name = nullptr;
        bool name_is_value = i->name_is_value;
        Ty const* key_type = nullptr;
        if (i->computed_key) {
            key_type = ty_expr_type(me, i->computed_key);
            bool is_number = false;
            name = ty_property_name_of_key(me, key_type, &is_number);
            if (name) name_is_value = is_number;
        } else {
            name = binding.names[i - object->data]; // (Interned by literal_bind())
        }

        // The type
        Ty const* member_context = nullptr;
        bool is_accessor = i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER;
        // (An accessor has none: asking for it would type the contextual type's same-named accessor, which is this
        // one again when inference rechecks the literal against what it inferred from it, `get sub() { return f(X) }`
        // in X's initializer)
        Ty const* member_sig_context = nullptr;
        if ((ctx.contextual || ctx.sig_contextual) && !is_accessor) {
            // (tsc asks for it from the value's checkExpression: what goes wrong making it is the value's)
            CurrentNode saved_node = ty_enter_expr(me, &i->value);
            if (name) {
                if (ctx.contextual) member_context = contextual_named_prop_type(me, ctx.contextual, name);
                if (ctx.sig_contextual) member_sig_context = contextual_named_prop_type(me, ctx.sig_contextual, name);
            } else if (key_type) {
                if (ctx.contextual) member_context = contextual_prop_type(me, ctx.contextual, key_type);
                if (ctx.sig_contextual) member_sig_context = contextual_prop_type(me, ctx.sig_contextual, key_type);
            }
            ty_leave_node(me, saved_node);
        }
        ExprCtx member_ctx = {
            .contextual = member_context, .sig_contextual = member_sig_context,
            .sig_absent = ctx.sig_absent || (ctx.sig_contextual && !member_sig_context),
            .is_const = ctx.is_const || i->inline_import_attributes, .skip_sensitive = ctx.skip_sensitive,
            .generic_mode = ctx.generic_mode,
        };
        Ty const* type = nullptr;
        switch (i->tag) {
            case OBJECT_ITEM_GETTER:
            case OBJECT_ITEM_SETTER:
                // Typed when asked (PROP_GETTER/PROP_SETTER), unless it's an index signature's
                if (!name) type = ty_object_literal_accessor_type(me, i->value.function, i->tag == OBJECT_ITEM_GETTER);
                break;
            case OBJECT_ITEM_METHOD:
                if (object_item_rare(i)->jsdoc_type) {
                    member_ctx.contextual = ty_of_node(object_item_rare(i)->jsdoc_type);
                    check_with(me, &i->value, member_ctx);
                    type = member_ctx.contextual;
                    break;
                }
                type = check_with(me, &i->value, member_ctx);
                break;
            default:
                if (object_item_rare(i)->jsdoc_type) {
                    // The declared type, which the value is checked against (expr_typecheck())
                    Ty const* declared = ty_of_node(object_item_rare(i)->jsdoc_type);
                    member_ctx.contextual = declared;
                    check_for_mutable_location(me, &i->value, member_ctx);
                    type = declared;
                    break;
                }
                // `{ x = 1 }` outside a destructuring target (TS1312) is typed by its default (tsc's checkObjectLiteral)
                if (i->tag == OBJECT_ITEM_SHORTHAND && object_item_rare(i)->shorthand_default && !expr->assignment_pattern) {
                    type = check_for_mutable_location(me, object_item_rare(i)->shorthand_default, member_ctx);
                    break;
                }
                type = nested_target_type(me, expr, &i->value, check_for_mutable_location(me, &i->value, member_ctx));
                break;
        }

        // (tsc's checkObjectLiteral: a context-sensitive property or method is an intra-expression inference site of the
        // call inferring now, against its contextual type as the literal gives it)
        if ((i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_METHOD) && member_context && !ctx.skip_sensitive &&
            ty_expr_is_context_sensitive(&i->value)
        ) {
            ty_add_intra_expression_site(me, type, member_context);
        }
        // (An optional method's symbol has undefined in its type: tsc's getTypeOfFuncClassEnumModule, addOptionality)
        if (i->tag == OBJECT_ITEM_METHOD && i->optional_method) type = ty_optional_prop_type(me, type);

        // (A private name in a class body is a property of its text, `{ #a: 1 }` is `{ #a: number; }`; outside of one
        // tsc's binder has no class to name it in, and the literal lacks it)
        if (object_item_is_private_name(i) && !i->private_name_in_class) continue;
        // (A bigint name declares no member: tsc's binder names it __missing, which the literal's type skips)
        if (!i->computed_key && span_is_bigint_name(i->name_span, i->name_is_value)) continue;

        // What the member adds to index signatures (tsc's isSymbolWithNumericName, isSymbolWithSymbolName: the
        // computed name's type is assignable to number or symbol)
        bool numeric_name = name ? ty_is_numeric_name(name) : (key_type->flags & (TYF_NUMBER | TYF_NUMBER_LITERAL)) ||
            ty_is_related(me, key_type, ty_intrinsic(me, TY_NUMBER), TY_RELATION_ASSIGNABLE);
        bool symbol_name = key_type && ((key_type->flags & TYF_ES_SYMBOL_LIKE) ||
            (!name && ty_is_related(me, key_type, ty_intrinsic(me, TY_ES_SYMBOL), TY_RELATION_ASSIGNABLE)));
        literal_members_push(&members, (LiteralMember){
            .type = type, .accessor = is_accessor ? i->value.function : nullptr, .getter = i->tag == OBJECT_ITEM_GETTER,
            .numeric_name = numeric_name, .symbol_name = symbol_name, .computed_key = i->computed_key, .item = i,
        });

        if (!name) {
            // A key that's only known to be some string, number or symbol makes an index signature (the one it's
            // assignable to: `any` is a number), of all the members since the last spread. Others make nothing
            Ty const* string_number_symbol = union_of_two(me, union_of_two(me, ty_intrinsic(me, TY_STRING),
                ty_intrinsic(me, TY_NUMBER)), ty_intrinsic(me, TY_ES_SYMBOL));
            if (ty_is_related(me, key_type, string_number_symbol, TY_RELATION_ASSIGNABLE)) {
                members.computed[numeric_name ? 1 : symbol_name ? 2 : 0] = true;
            }
            continue;
        }

        // The member's binder symbol (literal_bind()): a property or method is a property of its own type with the
        // symbol's declarations and meanings (tsgo's checkObjectLiteral makes a transient symbol of them), an accessor
        // is its symbol, typed by its accessors (getTypeOfAccessors). The last member of a name wins
        u32 item_index = (u32)(i - object->data);
        LiteralSymbol const* symbol = binding.symbol_of[item_index] != UINT32_MAX ? &binding.symbols[binding.symbol_of[item_index]] : nullptr;
        TypeNode const* asserted = object_item_rare(i)->jsdoc_type;
        u32 single = item_index;
        u32 const* member_items = symbol ? binding.members + symbol->member_start : &single;
        u32 decl_span_count = symbol ? symbol->member_count : 1;
        uint8_t name_form = ty_prop_name_form(span_sv(i->name_span), i->computed_key, key_type, true);
        Span* decl_span = arena_new_n(&my arena, Span, decl_span_count);
        for (u32 k = 0; k < decl_span_count; k++) {
            ObjectExprItem const* other = &object->data[member_items[k]];
            decl_span[k] = other->span;
            if (decl_span_count == 1) break;
            Ty const* other_key = other->computed_key ? ty_expr_type(me, other->computed_key) : nullptr;
            uint8_t form = ty_prop_name_form(span_sv(other->name_span), other->computed_key, other_key, true);
            name_form = k ? ty_prop_name_form_merge(name_form, form) : form;
        }
        uint8_t symbol_flags = symbol ? symbol->flags : literal_item_symbol_flags(i);
        u32 flags = 0;
        FunctionImpl const* accessor = nullptr;
        FunctionImpl const* setter = nullptr;
        if (is_accessor) {
            bool has_getter = symbol ? symbol->getter >= 0 : i->tag == OBJECT_ITEM_GETTER;
            bool has_setter = symbol ? symbol->setter >= 0 : i->tag == OBJECT_ITEM_SETTER;
            FunctionImpl const* getter_fn = !has_getter ? nullptr : symbol ? object->data[symbol->getter].value.function : i->value.function;
            FunctionImpl const* setter_fn = !has_setter ? nullptr : symbol ? object->data[symbol->setter].value.function : i->value.function;
            accessor = getter_fn ? getter_fn : setter_fn;
            setter = getter_fn ? setter_fn : nullptr;
            flags |= getter_fn ? PROP_GETTER : PROP_SETTER;
            // (One symbol with a property assignment too, `{ a: 2, get a() {...}, set a(v) {} }` (TS1119: the binder
            // merges them), is a property: typed by its accessors (getTypeOfAccessors), and written as that type,
            // getWriteTypeOfSymbol of a SymbolFlags.Property)
            if (symbol && (symbol->flags & LIT_SYM_PROPERTY)) {
                type = ty_object_literal_accessor_type(me, accessor, getter_fn != nullptr);
                flags &= ~(u32)(PROP_GETTER | PROP_SETTER);
                accessor = setter = nullptr;
            }
        }
        if (i->tag == OBJECT_ITEM_METHOD) flags |= PROP_METHOD | (i->optional_method ? PROP_OPTIONAL : 0);
        // (tsgo's isReadonlySymbol: a getter without a setter)
        if ((symbol_flags & LIT_SYM_GET) && !(symbol_flags & LIT_SYM_SET)) flags |= PROP_READONLY;
        if (ctx.is_const) flags |= PROP_READONLY;
        ObjectExprItem const* value_declaration = symbol ? &object->data[symbol->first] : i;
        if (value_declaration->readonly_modifier) flags |= PROP_READONLY;
        if (pattern_context) {
            Prop const* implied = ty_get_prop(me, pattern_context, name);
            if (implied && (implied->flags & PROP_OPTIONAL)) flags |= PROP_OPTIONAL | PROP_PATTERN_OPTIONAL;
        }
        // A destructuring target's member with a default is optional (tsc's checkObjectLiteral inDestructuringPattern)
        if (expr->assignment_pattern && ((i->tag == OBJECT_ITEM_SHORTHAND && object_item_rare(i)->shorthand_default) ||
            (i->tag == OBJECT_ITEM_PROPERTY && i->value.tag == EXPR_BINARY_OP && i->value.binary_op->op == BINARY_OP_EQUAL))
        ) {
            flags |= PROP_OPTIONAL | PROP_PATTERN_OPTIONAL;
        }
        literal_builder_add(&builder, (ObjectLiteralProp){
            .name = name,
            .flags = flags,
            .type = type,
            .accessor = accessor,
            .setter = setter,
            // (Named as its symbol's first declaration writes it: `{ kk: 1, [k]: 2 }` is `kk`)
            // (A name written twice is one symbol, an accessor pair too, declared by its first member: tsc's
            // valueDeclaration, where an excess property is reported, `{ b: 1, a: 1, b: 2 }`)
            .name_span = symbol && member_items[0] != item_index ? object->data[member_items[0]].name_span : i->name_span,
            .decl_spans = decl_span,
            .decl_span_count = decl_span_count,
            .name_is_value = name_is_value,
            .asserted = asserted,
            // (A member written again is declared by the first one: tsc's valueDeclaration)
            .item = value_declaration,
            .own = true,
            .name_form = name_form,
            .name_type = i->computed_key && (key_type->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) ? key_type : nullptr,
        });
    }
    literal_binding_drop(&binding);
    literal_members_flush(me, &members, builder.index, &builder.index_count, ctx.is_const);
    free(members.items);
    // (An object literal with a contextual type isn't open-ended: tsc's isJSObjectLiteral in checkObjectLiteral)
    SourceFile const* file = span_source(expr->inner_span);
    bool js = file && file->is_js && !ctx.contextual;
    Ty const* ret = spread_acc_finish(me, &spread, &builder, OBJECT_LITERAL_FRESH | (js ? OBJECT_JS : 0), expr);
    literal_builder_drop(&builder);
    return ret;
}

// tsc's checkObjectLiteral: a literal contextually typed by the implied type of an object binding pattern (`{ a } =
// { a: 1, b: 2 }`) may only have the pattern's properties, unless the pattern has a rest element or a computed name
void ty_check_object_literal_pattern(Checker* me, Expr const* expr, ErrorCtx* errs) {
    Ty const* contextual = ty_link_get(me, KEY_LITERAL_CONTEXT, expr);
    if (!contextual || !(contextual->flags & TYF_BINDING_PATTERN) || !(contextual->flags & TYF_OBJECT)) return;
    if (ty_is_tuple(contextual) || pattern_type_has_rest(me, contextual)) return;
    if (intern_get(&my links, (u64[]){ KEY_PATTERN_COMPUTED, contextual->id }, 2)) return;
    for_each (i, vec_object_expr_item, &expr->object) {
        if (i->tag != OBJECT_ITEM_PROPERTY && i->tag != OBJECT_ITEM_SHORTHAND && i->tag != OBJECT_ITEM_METHOD) continue;
        if (object_item_is_private_name(i)) continue;
        Atom const* name = nullptr;
        if (i->computed_key) {
            bool is_number = false;
            name = ty_property_name_of_key(me, ty_expr_type(me, i->computed_key), &is_number);
        } else {
            name = checker_atom(me, cstr_sv(&i->name_string));
        }
        if (name && ty_get_prop(me, contextual, name)) continue;
        // (tsc's symbolToString of the member: a name that isn't an identifier as written)
        csview written = span_sv(i->name_span);
        bool as_written = !name || (written.size && (written.buf[0] == '"' || written.buf[0] == '\'' ||
            written.buf[0] == '[' || (written.buf[0] >= '0' && written.buf[0] <= '9')));
        cstr name_str = as_written ? cstr_from_sv(written) : ty_prop_name_cstr(me, name);
        cstr target = ty_to_cstr(me, contextual);
        error_ctx_error_d(errs, i->name_span, 2353, "Object literal may only specify known properties, and '", name_str,
            "' does not exist in type '", target, "'.");
        c_drop(cstr, &name_str, &target);
    }
}

// tsc types an object literal's accessors lazily (checkObjectLiteral only defers their check): one in a literal that is
// (inside) an argument of a call is first typed by that call's resolution, inferring from or relating the literal to the
// parameter, once the argument's pushed contextual type is popped. Its literal's contextual type is then the argument's
// of resolvingSignature, any (getContextualTypeForArgumentAtIndex), and so is a getter's `this`:
// `h({ get value() { return this; } })` has `value: any`. Only a getter the resolution asks for: the literal's contextual
// type has a property of its name. tttt types it while checking the literal, during that resolution
static bool getter_typed_by_resolution(Checker* me, FunctionImpl const* accessor, bool* nested) {
    Expr const* literal = function_impl_rare(accessor)->this_literal;
    if (!literal || function_impl_rare(accessor)->this_literal_parenthesized || literal->tag != EXPR_OBJECT) return false;
    bool in_js = span_source(accessor->full_span) && span_source(accessor->full_span)->is_js;
    if (!compiler_options()->no_implicit_this && !in_js) return false;
    Expr const* outermost = literal;
    for (u32 depth = 0; depth < 32; depth++) {
        Expr const* parent = intern_get(&my links, (u64[]){ KEY_LITERAL_PARENT, (u64)(uintptr_t)outermost }, 2);
        if (!parent) break;
        outermost = parent;
    }
    CallExpr const* call = ty_link_get(me, KEY_LITERAL_ARG_OF, outermost);
    if (!call || !ty_call_resolution_in_flight(me, call)) return false;
    // (The argument's contextual type is any; a literal inside it has none, getTypeOfPropertyOfContextualType of any)
    *nested = outermost != literal;
    Ty const* contextual = ty_link_get(me, KEY_LITERAL_CONTEXT, literal);
    if (!contextual) return false;
    for_each (i, vec_object_expr_item, &literal->object) {
        if (i->tag != OBJECT_ITEM_GETTER || i->value.tag != EXPR_FUNCTION || i->value.function != accessor) continue;
        if (i->computed_key) return false;
        Atom const* name = checker_atom(me, cstr_sv(&i->name_string));
        Ty const* apparent = ty_apparent_type(me, contextual);
        TyList parts = apparent->kind == TY_UNION ? apparent->composite.types : (TyList){ .data = &apparent, .len = 1 };
        for (u32 k = 0; k < parts.len; k++) {
            if (!(parts.data[k]->flags & TYF_OBJECT)) continue;
            if (ty_find_prop(me, parts.data[k], name).found) return true;
        }
        return false;
    }
    return false;
}

Ty const* ty_object_literal_accessor_type(Checker* me, FunctionImpl const* accessor, bool getter) {
    Signature const* sig = single_call_signature(me, ty_function_type(me, accessor));
    if (!sig) return any_type(me);
    // (An annotated getter's type is as written: `get s(): typeof S` stays the unique symbol)
    // (Without one, tsc's getReturnTypeFromBody widens a unit type only, no contextual type for an accessor: a union of
    // literals stays)
    if (getter) {
        // (Typed by a call's resolution: `this` is any, see getter_typed_by_resolution(). Without remembering what the
        // body's expressions are then: tsc's deferred check of the body types them once the call is resolved)
        bool nested = false;
        if (!accessor->ret_explicit && !my lazy_getter && getter_typed_by_resolution(me, accessor, &nested)) {
            my lazy_getter = accessor;
            my lazy_getter_nested = nested;
            TAINT_FLOW();
            my flow_uncached++;
            Ty const* ret = signature_return_type(me, sig);
            TAINT_FLOW();
            my flow_uncached--;
            my lazy_getter = nullptr;
            return !ty_is_unit(ret) ? ret : widen_literals(me, ret);
        }
        Ty const* ret = signature_return_type(me, sig);
        return accessor->ret_explicit || !ty_is_unit(ret) ? ret : widen_literals(me, ret);
    }
    // A setter without a getter: its parameter's annotation, else any (tsc's getTypeOfAccessors, whatever the context
    // or an initializer would make of the parameter)
    for_each (i, vec_function_arg, &accessor->args) {
        Binding const* binding = &i->initializer.binding;
        if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
        if (!binding->has_explicit_type && !initialized_binding_rare(&i->initializer)->getter) return any_type(me);
        break;
    }
    SigResolved const* sig_rs = signature_resolve(me, sig);
    return sig_rs->param_count ? ty_sig_param_declared_type(me, &sig_rs->params[0]) : any_type(me);
}

void ty_check_object_literal_late_members(Checker* me, Expr const* object, ErrorCtx* errs) {
    VecObjectExprItem const* items = &object->object;
    bool late = false;
    for_each (i, vec_object_expr_item, items) late |= literal_item_is_late(i);
    if (!late) return;
    LiteralBinding binding;
    literal_bind(me, items, &binding);
    for (u32 c = 0; c < binding.conflict_count; c++) {
        struct LiteralConflict const* conflict = &binding.conflicts[c];
        ObjectExprItem const* item = &items->data[conflict->item];
        LiteralSymbol const* other = &binding.symbols[conflict->symbol];
        if (!conflict->merge) {
            // tsgo's lateBindMember: at the early declarations of the name, those of the late symbol and this one,
            // named by the property name (a unique symbol's by the declaration's name)
            Ty const* key = ty_regular(me, ty_expr_type(me, item->computed_key));
            csview name = key->flags & TYF_UNIQUE_ES_SYMBOL ? span_sv(item->name_span) : atom_sv(other->name);
            for (u32 s = 0; s < binding.count; s++) {
                LiteralSymbol const* symbol = &binding.symbols[s];
                if (symbol->late || !symbol->in_table || symbol->name != other->name) continue;
                for (u32 k = 0; k < symbol->member_count; k++) {
                    Span at = items->data[binding.members[symbol->member_start + k]].name_span;
                    if (!error_ctx_find(errs, at, 2300)) error_ctx_error_d(errs, at, 2300, "Duplicate identifier '", name, "'.");
                }
            }
            for (u32 k = 0; k < other->member_count; k++) {
                Span at = items->data[binding.members[other->member_start + k]].name_span;
                if (!error_ctx_find(errs, at, 2300)) error_ctx_error_d(errs, at, 2300, "Duplicate identifier '", name, "'.");
            }
            if (!error_ctx_find(errs, item->name_span, 2300)) error_ctx_error_d(errs, item->name_span, 2300, "Duplicate identifier '", name, "'.");
            continue;
        }
        // tsgo's reportMergeSymbolError: at every declaration of both, named as the late symbol prints (`[k]`), each
        // side's pointing at the other's
        LiteralSymbol const* late_symbol = &binding.symbols[binding.symbol_of[conflict->item]];
        csview name = span_sv(item->name_span);
        for (int side = 0; side < 2; side++) {
            LiteralSymbol const* at_symbol = side ? other : late_symbol;
            LiteralSymbol const* to_symbol = side ? late_symbol : other;
            for (u32 k = 0; k < at_symbol->member_count; k++) {
                Span at = items->data[binding.members[at_symbol->member_start + k]].name_span;
                Error* error = error_ctx_find(errs, at, 2300);
                if (!error) error = error_ctx_error_d(errs, at, 2300, "Duplicate identifier '", name, "'.");
                for (u32 j = 0; j < to_symbol->member_count; j++) {
                    add_duplicate_related_named(error, at, items->data[binding.members[to_symbol->member_start + j]].name_span, name);
                }
            }
        }
    }
    literal_binding_drop(&binding);
}

VecSpan ty_object_literal_member_symbol(Checker* me, Expr const* object, ObjectExprItem const* item, ObjectExprItem const** first) {
    VecSpan ret = vec_span_init();
    *first = item;
    if (item->tag == OBJECT_ITEM_SPREAD) return ret;
    // (A bigint name is a symbol of its own outside the table: tsc's binder names it __missing)
    if (!item->computed_key && span_is_bigint_name(item->name_span, item->name_is_value)) {
        vec_span_push(&ret, item->span);
        return ret;
    }
    LiteralBinding binding;
    literal_bind(me, &object->object, &binding);
    u32 index = (u32)(item - object->object.data);
    LiteralSymbol const* symbol = binding.symbol_of[index] != UINT32_MAX ? &binding.symbols[binding.symbol_of[index]] : nullptr;
    if (symbol && symbol->member_count) {
        *first = &object->object.data[binding.members[symbol->member_start]];
        for (u32 k = 0; k < symbol->member_count; k++) vec_span_push(&ret, object->object.data[binding.members[symbol->member_start + k]].span);
    } else {
        vec_span_push(&ret, item->span);
    }
    literal_binding_drop(&binding);
    return ret;
}

Ty const* ty_object_literal_member_type(Checker* me, Expr const* object, ObjectExprItem const* item) {
    Ty const* type = ty_expr_type(me, object);
    // A member of a symbol with accessors is typed by them (tsgo's getTypeOfSymbol: getTypeOfAccessors), whatever the
    // literal's property of the name is
    if (item->tag != OBJECT_ITEM_SPREAD) {
        LiteralBinding binding;
        literal_bind(me, &object->object, &binding);
        u32 index = (u32)(item - object->object.data);
        LiteralSymbol const* symbol = index < (u32)vec_object_expr_item_size(&object->object) && binding.symbol_of[index] != UINT32_MAX ?
            &binding.symbols[binding.symbol_of[index]] : nullptr;
        Ty const* accessor_type = symbol && (symbol->getter >= 0 || symbol->setter >= 0) ?
            literal_symbol_accessor_type(me, &object->object, symbol) : nullptr;
        literal_binding_drop(&binding);
        if (accessor_type) return accessor_type;
    }
    Atom const* name = nullptr;
    if (item->computed_key) {
        bool is_number = false;
        name = ty_property_name_of_key(me, ty_expr_type(me, item->computed_key), &is_number);
    } else {
        name = checker_atom(me, cstr_sv(&item->name_string));
    }
    Prop const* prop = name && (type->flags & TYF_OBJECT) ? ty_get_prop(me, type, name) : nullptr;
    // (Unless a later spread replaced it, or merged its optional property in)
    // (A member written again, `{ a: 1, a: 2 }`, is declared by both but typed by the last: the others aren't it)
    u32 spans = prop ? prop->decl->decl_span_count : 0;
    bool accessor_pair = prop && (prop->flags & (PROP_GETTER | PROP_SETTER)) && spans == 2;
    bool own = prop && prop->decl->literal_own && (!spans || span_eq(prop->decl->decl_spans[spans - 1], item->span) ||
        (accessor_pair && span_eq(prop->decl->decl_spans[0], item->span)));
    // (Methods of one name are one symbol typed by all of them)
    for (u32 i = 0; prop && !own && item->tag == OBJECT_ITEM_METHOD && (prop->flags & PROP_METHOD) && prop->decl->literal_own && i < spans; i++) {
        own = span_eq(prop->decl->decl_spans[i], item->span);
    }
    if (prop && own) {
        return prop_type(me, prop);
    }
    // A computed key that made no property: its part of an index signature, as the literal checked it (tsc's
    // checkPropertyAssignment, whose literal is kept by the contextual type: `[k]: true` for `T` inferred as boolean)
    if (item->computed_key && (type->flags & TYF_OBJECT) && item->tag == OBJECT_ITEM_PROPERTY) {
        Members const* members = ty_members(me, type);
        for (u32 i = 0; i < members->index_count; i++) {
            for (u32 j = 0; j < members->index[i].component_count; j++) {
                if (members->index[i].components[j].name == item->computed_key) return members->index[i].components[j].type;
            }
        }
    }
    Ty const* value = ty_expr_type(me, &item->value);
    if (item->tag == OBJECT_ITEM_GETTER || item->tag == OBJECT_ITEM_SETTER) {
        Signature const* sig = single_call_signature(me, value);
        if (!sig) return any_type(me);
        if (item->tag == OBJECT_ITEM_GETTER) {
            Ty const* ret = signature_return_type(me, sig);
            return item->value.tag == EXPR_FUNCTION && item->value.function->ret_explicit ? ret : widen_literals(me, ret);
        }
        return sig_res(me, sig)->param_count ? ty_sig_param_declared_type(me, &sig_res(me, sig)->params[0]) : any_type(me);
    }
    return widen_literals(me, value);
}

// Array literals

static Ty const* tuple_of(Checker* me, Ty const* const* elements, unsigned char const* flags, u32 len, bool readonly) {
    TupleShape shape = { .arity = len, .flags = flags, .readonly = readonly };
    Ty const* target = ty_tuple_target(me, &shape);
    return len ? ty_reference(me, target, (TyList){ .data = elements, .len = len }) : target;
}

static Ty const* check_array(Checker* me, Expr const* expr, ExprCtx ctx) {
    u32 len = (u32)vec_expr_size(&expr->array);
    // (A destructuring assignment target is a tuple of its targets: tsc's inDestructuringPattern)
    bool as_tuple = ctx.is_const || expr->assignment_pattern || expr->spread_into_call ||
        context_is_tuple_like(me, ctx.contextual, 0);
    Ty const** elements = malloc(sizeof(Ty const*) * (len ? len : 1));
    unsigned char* flags = calloc(len ? len : 1, 1);
    u32 count = 0;
    bool has_omitted = false;
    bool spreads_never = false;
    ElementContextArg context_arg = array_element_context_arg(expr);
    for_each (i, vec_expr, &expr->array) {
        Ty const* element_context = contextual_array_element_type_at(me, ctx.contextual, context_arg, (u32)i_index);
        Ty const* element_sig_context = contextual_array_element_type_at(me, ctx.sig_contextual, context_arg, (u32)i_index);
        ExprCtx element_ctx = {
            .contextual = element_context, .sig_contextual = element_sig_context,
            .sig_absent = ctx.sig_absent || (ctx.sig_contextual && !element_sig_context),
            .is_const = ctx.is_const, .skip_sensitive = ctx.skip_sensitive,
            .generic_mode = ctx.generic_mode,
        };
        if (i->tag == EXPR_SPREAD) {
            // (A spread of a const context is in it too: tsc's isConstContext goes up through SpreadElement)
            Ty const* operand = ctx.is_const ? check_with(me, i->spread, (ExprCtx){ .is_const = true }) : ty_expr_type(me, i->spread);
            if (ty_flow_partial_reports(me, i->inner_span)) expr_spread_errors(i, operand, my errs);
            if (as_tuple) {
                flags[count] = TUPLE_ELEMENT_REST;
                if (expr->assignment_pattern && !ty_is_array(me, operand) && !ty_is_tuple(operand) && !(operand->flags & TYF_ANY) &&
                    !(operand->kind == TY_DECLARED && operand->declared.kind == TY_DECLARED_TUPLE_TARGET)
                ) {
                    // A rest target of a destructuring assignment: its number index signature, else what it iterates,
                    // else unknown (tsc's checkArrayLiteral with inDestructuringPattern)
                    Ty const* apparent = ty_apparent_type(me, operand);
                    IndexInfo const* index = (apparent->flags & TYF_OBJECT) ?
                        ty_applicable_index_info(me, apparent, ty_intrinsic(me, TY_NUMBER)) : nullptr;
                    TyIterationTypes const* iterated = index ? nullptr : ty_iteration_types_of_iterable_unreported(me, operand, ITERATION_DESTRUCTURING);
                    Ty const* element = index ? index->value : iterated ? iterated->yield_type : ty_intrinsic(me, TY_UNKNOWN_);
                    elements[count++] = ty_array(me, element, false);
                    continue;
                }
                // (A spread of never makes the tuple never: tsc's createNormalizedTupleType maps a variadic union or
                // never element over its members)
                spreads_never |= (operand->flags & TYF_NEVER) != 0;
                // (The empty tuple is the tuple target, which spreads nothing)
                bool empty_tuple = operand->kind == TY_DECLARED && operand->declared.kind == TY_DECLARED_TUPLE_TARGET;
                Ty const* like = array_like_element(me, operand);
                elements[count++] = ty_is_array(me, operand) || ty_is_tuple(operand) || empty_tuple ||
                    is_generic_array_like(me, operand) || is_union_of_arrays(me, operand) ?
                    operand : like ? ty_array(me, like, false) :
                    ty_array(me, ty_iterated_type(me, ITERATION_SPREAD, operand, expr_outer_span(i->spread), nullptr), false);
            } else {
                // (never is array-like, its element never: tsc's checkArrayLiteral, getIndexedAccessTypeOrUndefined.
                // The spread expression itself is what iterating gives, any)
                // (A generic array-like one is its `[number]`: tsc's getIndexedAccessTypeOrUndefined of a variadic
                // element, `[...arg]` with arg: T & {} is (T & {})[number][])
                Ty const* like = operand->flags & TYF_NEVER ? nullptr : array_like_element(me, operand);
                elements[count++] = operand->flags & TYF_NEVER ? operand :
                    (is_generic_array_like(me, operand) || (operand->kind == TY_INTERSECTION && is_generic_type(me, operand) &&
                        ty_extends_array(me, operand))) ? ty_indexed(me, operand, ty_intrinsic(me, TY_NUMBER)) :
                    like ? like : ty_expr_type(me, i);
            }
            continue;
        }
        if (compiler_options()->exact_optional_property_types && i->tag == EXPR_LITERAL && expr_outer_span(i).size == 0) {
            // A hole is an optional element that isn't there (tsc's checkArrayLiteral with an OmittedExpression)
            (void)ty_expr_type(me, i);
            has_omitted = true;
            flags[count] = TUPLE_ELEMENT_OPTIONAL;
            elements[count++] = ty_undefined_or_missing(me);
            continue;
        }
        Ty const* element = nested_target_type(me, expr, i, check_for_mutable_location(me, i, element_ctx));
        // (tsc's checkArrayLiteral: in a tuple context, a context-sensitive element is an intra-expression inference site)
        if (element_context && !ctx.skip_sensitive && context_is_tuple_like(me, ctx.contextual, 0) && ty_expr_is_context_sensitive(i)) {
            ty_add_intra_expression_site(me, element, element_context);
        }
        if (has_omitted) flags[count] = TUPLE_ELEMENT_OPTIONAL;
        elements[count++] = element;
    }

    Ty const* ret;
    if (as_tuple) {
        // (A const type parameter constrained to a mutable array gets a mutable tuple. Of the apparent contextual type,
        // tsc's getApparentTypeOfContextualType: under `...cases: X` with `const X extends (string | [string, A])[]`,
        // the context `X[1]` of `['b', a]` is `string | [string, A]`, which has a mutable member)
        ret = spreads_never ? ty_intrinsic(me, TY_NEVER) :
            tuple_of(me, elements, flags, count, ctx.is_const &&
                !ty_some_mutable_array_like(me, ctx.contextual ? apparent_contextual_type(me, ctx.contextual) : nullptr));
    } else {
        // Without strictNullChecks null and undefined elements don't show next to others
        bool strict = compiler_options()->strict_null_checks;
        u32 kept = 0;
        for (u32 i = 0; i < count; i++) {
            if (!strict && (elements[i]->flags & TYF_NULLABLE)) continue;
            elements[kept++] = elements[i];
        }
        Ty const* element;
        if (kept) {
            element = ty_union_subtype_reduced(me, elements, kept);
        } else if (count) {
            element = ty_union(me, elements, count, nullptr, (TyList){0});
        } else {
            // `[]` (an unannotated variable initialized with it is an evolving array in tsc, see binding.c)
            element = compiler_options()->strict_null_checks ? ty_intrinsic(me, TY_IMPLICIT_NEVER) : ty_intrinsic(me, TY_UNDEFINED_WIDENING);
        }
        ret = ty_array(me, element, false);
    }
    free(elements);
    free(flags);
    return ret;
}

// JSX attributes

// The contextual type of one of several children (tsc's getContextualTypeForChildJsxExpression): of an array-like
// children type, its element
static Ty const* jsx_child_context(Checker* me, Ty const* children, u32 index, u32 count) {
    if (!children || count == 1) return children;
    TyList parts = children->kind == TY_UNION ? children->composite.types : (TyList){ .data = &children, .len = 1 };
    TyBuf mapped = {0};
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = parts.data[i];
        if (ty_extends_array(me, part)) {
            Value value = value_number(index);
            part = ty_access(me, part, ty_literal(me, &value));
        }
        if (part) ty_buf_push(&mapped, part);
    }
    Ty const* ret = mapped.len ? ty_union(me, mapped.data, mapped.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&mapped);
    return ret;
}

// tsc's getSemanticJsxChildren: whitespace with a line break and empty `{}` aren't children
bool ty_jsx_child_is_semantic(JsxChild const* child) {
    return child->tag == JSX_CHILD_TEXT ? !child->whitespace_only : child->tag != JSX_CHILD_EMPTY_EXPR;
}

// The type of a child (tsc's checkJsxChildren)
// (`sig_contextual`, `sig_absent`: the view a contextual signature sees while a call infers, as for an attribute)
static Ty const* check_jsx_child(Checker* me, JsxChild const* child, Ty const* contextual, Ty const* sig_contextual, bool sig_absent,
    bool skip_sensitive
) {
    if (child->tag == JSX_CHILD_TEXT) return ty_intrinsic(me, TY_STRING);
    return check_for_mutable_location(me, &child->expr, (ExprCtx){
        .contextual = contextual, .sig_contextual = sig_contextual, .sig_absent = sig_absent, .skip_sensitive = skip_sensitive,
    });
}

// The attributes of an element as an object literal (tsc's createJsxAttributesTypeFromAttributesProperty), with the
// children as one more property, named by JSX.ElementChildrenAttribute
static Ty const* check_jsx_attributes(Checker* me, Expr const* expr, ExprCtx ctx) {
    JsxElement const* element = jsx_attributes_element(expr);
    if (ctx.contextual && ctx.contextual->kind == TY_UNION) {
        ctx.contextual = discriminate_by_jsx_attributes(me, ctx.contextual, expr);
    }
    // (And the view a contextual signature sees while a call infers, which the attributes discriminate as well: a generic
    // component's `Props<H>` instantiated, `onClick={(e) => ...}` typed by the member `asChild` absent picks, wouter's Link)
    if (ctx.sig_contextual && ctx.sig_contextual->kind == TY_UNION) {
        ctx.sig_contextual = discriminate_by_jsx_attributes(me, ctx.sig_contextual, expr);
    }
    LiteralBuilder builder = {0}; // The attributes since the last spread
    SpreadAcc spread = {0};
    bool has_attribute = false, has_spread = false, has_children = false;
    Ty const* to_intersect = nullptr;
    for_each (i, vec_jsx_attribute, &expr->jsx_attributes) {
        has_attribute |= i->tag != JSX_ATTRIBUTE_SPREAD;
        if (i->tag == JSX_ATTRIBUTE_SPREAD) {
            has_spread = true;
            if (builder.count) spread_acc_props(me, &spread, &builder);
            literal_builder_drop(&builder);
            Ty const* spread_type = ty_reduced(me, check_with(me, &i->value, (ExprCtx){
                .contextual = ctx.contextual, .sig_contextual = ctx.sig_contextual, .sig_absent = ctx.sig_absent,
            }));
            // A spread of no object (TS2698) is intersected with the rest instead (tsc's typeToIntersect)
            if (!(spread_type->flags & TYF_ANY) && !ty_is_valid_spread_type(me, spread_type)) {
                to_intersect = to_intersect ?
                    ty_intersection(me, (Ty const*[]){ to_intersect, spread_type }, 2, nullptr, (TyList){0}) : spread_type;
                continue;
            }
            spread_acc_type(me, &spread, spread_type);
            continue;
        }
        Atom const* name = checker_atom(me, span_sv(i->name));
        Ty const* member_context = ctx.contextual ? contextual_named_prop_type(me, ctx.contextual, name) : nullptr;
        // (While a call infers, the view a contextual signature sees, like check_object()'s members)
        Ty const* member_sig_context = ctx.sig_contextual ? contextual_named_prop_type(me, ctx.sig_contextual, name) : nullptr;
        Ty const* type;
        switch (i->tag) {
            case JSX_ATTRIBUTE_NO_VALUE: type = ty_intrinsic(me, TY_TRUE); break; // `<a x />` is `<a x={true} />`
            case JSX_ATTRIBUTE_EMPTY_EXPR: type = any_type(me); break; // (the error type)
            default:
                type = check_for_mutable_location(me, &i->value, (ExprCtx){
                    .contextual = member_context, .sig_contextual = member_sig_context,
                    .sig_absent = ctx.sig_absent || (ctx.sig_contextual && !member_sig_context),
                    .skip_sensitive = ctx.skip_sensitive, .generic_mode = ctx.generic_mode,
                });
                // (A context-sensitive attribute is an intra-expression inference site of the call inferring now, like
                // an object literal's member: tsc's createJsxAttributesTypeFromAttributesProperty)
                if (member_context && !ctx.skip_sensitive && ty_expr_is_context_sensitive(&i->value)) {
                    ty_add_intra_expression_site(me, type, member_context);
                }
                break;
        }
        {
            // (What ty_jsx_attribute_type() shows when a spread of any hides the property)
            u64 key[2] = { KEY_JSX_ATTRIBUTE, (u64)(uintptr_t)i };
            *intern_slot(me, &my links, key, 2) = type;
        }
        Span* decl_span = arena_new(&my arena, Span);
        *decl_span = i->span;
        literal_builder_add(&builder, (ObjectLiteralProp){
            .name = name,
            .type = type,
            .name_span = i->name,
            .decl_spans = decl_span,
            .decl_span_count = 1,
            .own = true,
        });
    }

    // The children
    u32 child_count = 0;
    for_each (i, vec_jsx_child, &element->children) child_count += ty_jsx_child_is_semantic(i);
    if (child_count) {
        Atom const* children_name = ty_jsx_children_prop_name(me, element);
        Ty const* children_context = ctx.contextual && children_name && atom_sv(children_name).size ?
            contextual_named_prop_type(me, ctx.contextual, children_name) : nullptr;
        // (While a call infers, the children's view for a contextual signature too, like an attribute's: a generic
        // component's `{(props) => ...}` child, firecms' Field<T, C> whose props are a conditional type over C)
        Ty const* children_sig_context = ctx.sig_contextual && children_name && atom_sv(children_name).size ?
            contextual_named_prop_type(me, ctx.sig_contextual, children_name) : nullptr;
        bool children_sig_absent = ctx.sig_absent || (ctx.sig_contextual && !children_sig_context);
        TyBuf types = {0};
        u32 index = 0;
        for_each (i, vec_jsx_child, &element->children) {
            if (!ty_jsx_child_is_semantic(i)) continue;
            Ty const* sig_context = children_sig_context ? jsx_child_context(me, children_sig_context, index, child_count) : nullptr;
            ty_buf_push(&types, check_jsx_child(me, i, jsx_child_context(me, children_context, index, child_count), sig_context,
                children_sig_absent || (children_sig_context && !sig_context), ctx.skip_sensitive));
            index++;
        }
        if (!spread.any && children_name && atom_sv(children_name).size) {
            Ty const* type = types.data[0];
            if (types.len > 1) {
                if (context_is_tuple_like(me, children_context, 0)) {
                    unsigned char* flags = calloc(types.len, 1); // all required
                    type = tuple_of(me, types.data, flags, types.len, false);
                    free(flags);
                } else {
                    type = ty_array(me, ty_union(me, types.data, types.len, nullptr, (TyList){0}), false);
                }
            }
            // Spread in after the attributes
            if (builder.count) spread_acc_props(me, &spread, &builder);
            literal_builder_drop(&builder);
            LiteralBuilder children = {0};
            literal_builder_add(&children, (ObjectLiteralProp){ .name = children_name, .type = type, .own = true });
            spread_acc_props(me, &spread, &children);
            literal_builder_drop(&children);
            has_children = true;
        }
        ty_buf_drop(&types);
    }

    // Fresh (excess properties are errors) if tsc made it with createJsxAttributesTypeHelper(): there's an attribute, or
    // nothing was spread into it (children are spread in)
    bool fresh = has_attribute || (!has_spread && !has_children);
    bool spread_nothing = !spread.alt_count && !builder.count && !spread.any;
    Ty const* ret = spread_acc_finish(me, &spread, &builder, fresh ? OBJECT_JSX_ATTRIBUTES_FRESH : OBJECT_JSX_ATTRIBUTES, nullptr);
    literal_builder_drop(&builder);
    if (to_intersect && !(ret->flags & TYF_ANY)) {
        ret = spread_nothing ? to_intersect : ty_intersection(me, (Ty const*[]){ to_intersect, ret }, 2, nullptr, (TyList){0});
    }
    return ret;
}

Ty const* ty_jsx_attribute_type(Checker* me, Expr const* attributes, JsxAttribute const* attribute) {
    if (attribute->tag == JSX_ATTRIBUTE_NO_VALUE) return ty_intrinsic(me, TY_TRUE);
    if (attribute->tag == JSX_ATTRIBUTE_EMPTY_EXPR) return any_type(me);
    Ty const* type = ty_expr_type(me, attributes);
    Prop const* prop = type->flags & TYF_OBJECT ? ty_get_prop(me, type, checker_atom(me, span_sv(attribute->name))) : nullptr;
    // (Unless a later spread replaced it)
    // (Not merged with a spread's optional property, which may have no declaration span of its own: `literal_own`)
    if (prop && prop->decl->decl_span_count == 1 && span_eq(prop->decl->decl_spans[0], attribute->span) &&
        prop->decl->literal_own
    ) {
        return prop_type(me, prop);
    }
    u64 key[2] = { KEY_JSX_ATTRIBUTE, (u64)(uintptr_t)attribute };
    Ty const* checked = intern_get(&my links, key, 2);
    if (checked) return checked;
    return widen_literals(me, ty_expr_type(me, &attribute->value));
}

Ty const* ty_mutable_location_type(Checker* me, Expr const* expr, Ty const* contextual) {
    Ty const* type = ty_expr_type(me, expr);
    if (is_type_assertion(expr)) return type;
    if (contextual && any_literal_of_contextual_type(me, type, contextual)) return ty_regular(me, type);
    return widen_literals(me, type);
}

// An element or spread operand checked again for an elaboration: with tsc's forceTuple check mode (`force`), which
// reaches nested array literals and the array literals of a conditional's branches too: `[...(c ? r : [r])]` spreads
// `R | [r]` (checkConditionalExpression's subtype-reduced union), `[["a", f]]` is `[[string, F]]`; or with only a new
// contextual type (tsc's CheckModeContextual), where an array literal is a tuple when its context is tuple-like
static Ty const* recheck_operand(Checker* me, Expr const* expr, Ty const* contextual, bool force, u32 depth);
static Ty const* array_literal_rechecked(Checker* me, Expr const* expr, Ty const* contextual, bool force, u32 depth);

// An object literal checked again: checkObjectLiteral passes the check mode on to its properties' values, so their
// array literals are tuples with forceTuple (gemini-cli's `options: [{...}, {...}]` in a TS2741 elaboration), and every
// value is checked for a mutable location against its new context (`value: true` where the context, the source's own
// `value: boolean`, keeps it `true`). The literal's type itself when nothing changes
static Ty const* object_literal_rechecked(Checker* me, Expr const* expr, Ty const* contextual, bool force, u32 depth) {
    Ty const* ty = ty_expr_type(me, expr);
    if (ty->kind != TY_ANONYMOUS || !ty->anonymous.object_literal || ty->anonymous.jsx_attributes || depth >= 8) return ty;
    Members const* members = ty_members(me, ty);
    ObjectLiteralProp* props = calloc(members->prop_count ? members->prop_count : 1, sizeof(ObjectLiteralProp));
    bool changed = false;
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members_prop(me, members, i);
        props[i] = (ObjectLiteralProp){ .reuse = prop };
        ObjectExprItem const* item = prop->decl->item;
        if (!item || item->tag != OBJECT_ITEM_PROPERTY || prop->decl->function || !prop->decl->literal_own) continue;
        Expr const* value = &item->value;
        Ty const* element_context = contextual ? contextual_named_prop_type(me, contextual, prop->name) : nullptr;
        Ty const* forced = value->tag == EXPR_ARRAY || value->tag == EXPR_TERNARY || value->tag == EXPR_OBJECT ?
            recheck_operand(me, value, element_context, force, depth + 1) :
            element_context ? ty_mutable_location_type(me, value, element_context) : prop_type(me, prop);
        if (forced == ty_expr_type(me, value) || forced == prop_type(me, prop)) continue;
        changed = true;
        props[i] = (ObjectLiteralProp){
            .name = prop->name,
            .flags = prop->flags,
            .type = forced,
            .name_span = prop->decl->name_span,
            .decl_spans = prop->decl->decl_spans,
            .decl_span_count = prop->decl->decl_span_count,
            .name_is_value = prop->decl->name_is_value,
            .asserted = prop_decl_asserted(prop->decl),
            .item = item,
            .own = true,
            .name_form = ty_prop_name_form_of(prop),
            .name_type = prop_decl_rare(prop->decl)->name_type,
        };
    }
    Ty const* ret = ty;
    if (changed) {
        IndexInfo index[8] = {0};
        u32 index_count = members->index_count < c_arraylen(index) ? members->index_count : c_arraylen(index);
        for (u32 i = 0; i < index_count; i++) index[i] = members->index[i];
        ObjectLiteralKind kind = (ty_is_fresh_object(ty) ? OBJECT_LITERAL_FRESH : OBJECT_LITERAL) |
            (ty->anonymous.js_literal ? OBJECT_JS : 0) | (ty->anonymous.contains_spread ? OBJECT_CONTAINS_SPREAD : 0);
        ret = ty_object_literal_alternative(me, props, members->prop_count, index, index_count, kind, nullptr, 0);
    }
    free(props);
    return ret;
}

static Ty const* recheck_operand(Checker* me, Expr const* expr, Ty const* contextual, bool force, u32 depth) {
    if (expr->tag == EXPR_ARRAY) return array_literal_rechecked(me, expr, contextual, force, depth);
    if (expr->tag == EXPR_OBJECT) return object_literal_rechecked(me, expr, contextual, force, depth);
    if (expr->tag == EXPR_TERNARY && depth < 8) {
        Ty const* yes = recheck_operand(me, &expr->ternary->yes, contextual, force, depth + 1);
        Ty const* no = recheck_operand(me, &expr->ternary->no, contextual, force, depth + 1);
        if (yes != ty_expr_type(me, &expr->ternary->yes) || no != ty_expr_type(me, &expr->ternary->no)) {
            Ty const* both[2] = { yes, no };
            return ty_union_subtype_reduced(me, both, 2);
        }
    }
    return ty_expr_type(me, expr);
}

Ty const* ty_literal_rechecked(Checker* me, Expr const* expr, Ty const* contextual) {
    return recheck_operand(me, expr, contextual, false, 0);
}

static Ty const* array_literal_rechecked(Checker* me, Expr const* expr, Ty const* contextual, bool force, u32 depth) {
    bool as_tuple = force || context_is_tuple_like(me, contextual, 0);
    u32 len = (u32)vec_expr_size(&expr->array);
    Ty const** elements = malloc(sizeof(Ty const*) * (len ? len : 1));
    unsigned char* flags = calloc(len ? len : 1, 1);
    Ty const* ret = nullptr;
    ElementContextArg context_arg = array_element_context_arg(expr);
    for_each (i, vec_expr, &expr->array) {
        if (i->tag == EXPR_SPREAD) {
            if (!as_tuple) {
                // (Left as checked: the spread's element type, as check_array() reads it, unchanged)
                ret = ty_expr_type(me, expr);
                break;
            }
            // As check_array() makes a spread element of a tuple. An array literal is a tuple too (tsc passes
            // forceTuple on to the spread expression)
            Ty const* operand = recheck_operand(me, i->spread, nullptr, force, depth + 1);
            flags[i_index] = TUPLE_ELEMENT_REST;
            elements[i_index] = ty_is_array(me, operand) || ty_is_tuple(operand) || is_generic_array_like(me, operand) ||
                is_union_of_arrays(me, operand) ?
                operand : ty_array(me, ty_iterated_type(me, ITERATION_SPREAD, operand, expr_outer_span(i->spread), nullptr), false);
            continue;
        }
        Ty const* element_context = contextual_array_element_type_at(me, contextual, context_arg, (u32)i_index);
        // (tsc's checkExpressionForMutableLocation(e, checkMode) passes forceTuple on)
        Ty const* forced = i->tag == EXPR_ARRAY || i->tag == EXPR_TERNARY || i->tag == EXPR_OBJECT ?
            recheck_operand(me, i, element_context, force, depth + 1) : nullptr;
        elements[i_index] = forced && forced != ty_expr_type(me, i) ? forced : ty_mutable_location_type(me, i, element_context);
    }
    if (!ret && as_tuple) {
        ret = tuple_of(me, elements, flags, len, false);
    } else if (!ret && (depth >= 8 || !len)) {
        ret = ty_expr_type(me, expr);
    } else if (!ret) {
        // (check_array()'s element type: subtype-reduced, without strictNullChecks null and undefined left out)
        bool strict = compiler_options()->strict_null_checks;
        u32 kept = 0;
        for (u32 i = 0; i < len; i++) {
            if (!strict && (elements[i]->flags & TYF_NULLABLE)) continue;
            elements[kept++] = elements[i];
        }
        ret = kept ? ty_array(me, ty_union_subtype_reduced(me, elements, kept), false) : ty_expr_type(me, expr);
    }
    free(elements);
    free(flags);
    return ret;
}

Ty const* ty_array_literal_as_tuple(Checker* me, Expr const* expr, Ty const* contextual) {
    return array_literal_rechecked(me, expr, contextual, true, 0);
}

// The rest

bool as_type_is_const(AsTypeExpr const* as_type) {
    TypeNode const* type = &as_type->type;
    return type->tag == TYPE_IDENTIFIER && !type->identifier->target && span_equals(type->identifier->name, "const") &&
        !identifier_type_namespace_count(type->identifier);
}

// Whether an instantiation expression keeps a signature (tsc's getInstantiatedSignatures: generic, with the right
// number of type arguments): the type arguments are mapped to the type parameters of exactly those. One whose
// constraints the arguments don't satisfy is kept uninstantiated: its first type parameter is mapped to the unreachable
// never type, which marks it (apply_type_arguments_part()). No type argument is that type, nor does any mapper make it
// (unlike the wildcard: the permissive instantiation of a conditional type maps the outer type parameters to that), so
// an instantiation of the type with an outer mapper keeps the mark
TyInstantiationKeeps ty_instantiation_keeps_signature(Checker* me, Signature const* sig, TyMapper const* mapper) {
    signature_resolve(me, sig);
    SigResolved const* sig_r = sig_res(me, sig);
    if (!sig_r->type_params.len) return TY_INSTANTIATION_DROPS;
    for (u32 k = 0; k < mapper->len; k++) {
        if (mapper->sources[k] != sig_r->type_params.data[0]) continue;
        return mapper->targets[k] == ty_intrinsic(me, TY_UNREACHABLE_NEVER) ? TY_INSTANTIATION_KEEPS_AS_IS : TY_INSTANTIATION_INSTANTIATES;
    }
    return TY_INSTANTIATION_DROPS;
}

// `Bar<string>` as a value: the object type with the type arguments applied to its generic signatures, the others
// dropped (`instantiate_members()`). Returns the type itself if it has no signatures
Ty const* ty_apply_type_arguments_mapper(Checker* me, Ty const* ty, TyMapper const* mapper) {
    return ty_apply_type_arguments_mapper_of(me, ty, mapper, nullptr);
}

// ... as an instantiation of `query`, the type of a `typeof f<T>` node: tsc instantiates that type like any anonymous
// type, keyed by the node's outer type parameters (getObjectTypeInstantiation of an InstantiationExpressionType), so
// it's a type of its own, not the one the expression `f<number>` has
Ty const* ty_apply_type_arguments_mapper_of(Checker* me, Ty const* ty, TyMapper const* mapper, Ty const* query) {
    GRAPH_CHECK_FRAME();
    Members const* members = ty_members(me, ty);
    if (!members->call_count && !members->construct_count) return ty;
    u32 count = 3 + mapper->len * 2;
    u64* key = malloc(sizeof(u64) * count);
    key[0] = KEY_ANONYMOUS_INSTANCE;
    key[1] = ty->id;
    // (Not a type id: tells these apart from ordinary instantiations and ty_realias()'s)
    key[2] = query ? (UINT64_MAX >> 1) + query->id : UINT64_MAX;
    u32 flags = TYF_OBJECT | (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES);
    for (u32 i = 0; i < mapper->len; i++) {
        key[3 + i * 2] = mapper->sources[i]->id;
        key[4 + i * 2] = mapper->targets[i]->id;
        flags |= mapper->targets[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
    }
    Ty const* ret = intern_get(&my types, key, count);
    if (!ret) {
        GRAPH_CHECK_BIND();
        Ty* fresh = ty_alloc(me, TY_ANONYMOUS, flags);
        fresh->anonymous.target = ty;
        fresh->anonymous.mapper = mapper;
        fresh->anonymous.applies_type_args = true;
        fresh->anonymous.free_params_unknown = true;
        fresh->anonymous.state = arena_new(&my arena, TyObjectState);
        ret = intern_set(me, &my types, intern_slot(me, &my types, key, count), fresh);
    }
    free(key);
    return ret;
}

// tsc's getInstantiationExpressionType: object types get their applicable signatures instantiated, unions and
// intersections each member, a type parameter its constraint's
static Ty const* apply_type_arguments_part(Checker* me, Ty const* ty, TyList args, u32 depth) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        u32 len = ty->composite.types.len;
        Ty const** parts = malloc(sizeof(Ty const*) * (len ? len : 1));
        bool changed = false;
        for (u32 i = 0; i < len; i++) {
            parts[i] = apply_type_arguments_part(me, ty->composite.types.data[i], args, depth + 1);
            changed |= parts[i] != ty->composite.types.data[i];
        }
        Ty const* ret = !changed ? ty : ty->kind == TY_UNION ?
            ty_union(me, parts, len, nullptr, (TyList){0}) : ty_intersection(me, parts, len, nullptr, (TyList){0});
        free(parts);
        return ret;
    }
    if (ty->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
        Ty const* constraint = ty_base_constraint(me, ty);
        if (!constraint || constraint == ty) return ty;
        Ty const* instantiated = apply_type_arguments_part(me, constraint, args, depth + 1);
        return instantiated != constraint ? instantiated : ty;
    }
    if (!(ty->flags & TYF_OBJECT)) return ty;
    Members const* members = ty_members(me, ty);
    Ty const* sources[64];
    Ty const* targets[64];
    u32 len = 0;
    u32 total = 0, applicable = 0, fitting = 0;
    for (u32 pass = 0; pass < 2; pass++) {
        u32 sig_count = pass ? members->construct_count : members->call_count;
        Signature* const* sigs = pass ? members->construct : members->call;
        for (u32 i = 0; i < sig_count; i++) {
            signature_resolve(me, sigs[i]);
            TyList params = sig_res(me, sigs[i])->type_params;
            // (tsc's hasCorrectTypeArgumentArity)
            u32 min = 0;
            for (u32 j = 0; j < params.len; j++) {
                if (!ty_param_default(me, params.data[j])) min = j + 1;
            }
            total++;
            if (!params.len || (args.len && (args.len < min || args.len > params.len))) continue;
            applicable++;
            if (len + params.len > c_arraylen(sources)) continue;
            // (Type arguments that don't satisfy the constraints, TS2344, leave the signature as it is: tsc's
            // getInstantiatedSignatures, checkTypeArguments)
            Ty const* own[64];
            if (params.len > c_arraylen(own)) continue;
            // (A default is instantiated with the arguments before it, those not filled in yet the error type: tsc's
            // fillMissingTypeArguments)
            for (u32 j = 0; j < params.len; j++) own[j] = j < args.len ? args.data[j] : ty_intrinsic(me, TY_ERROR);
            TyMapper const* own_mapper = ty_mapper_new(me, params, (TyList){ .data = own, .len = params.len });
            for (u32 j = args.len; j < params.len; j++) {
                Ty const* fallback = ty_param_default(me, params.data[j]);
                own[j] = fallback ? ty_instantiate(me, fallback, own_mapper) : ty_intrinsic(me, TY_UNKNOWN_);
                own_mapper = ty_mapper_new(me, params, (TyList){ .data = own, .len = params.len });
            }
            bool fits = true;
            // (Only the arguments written, each against its constraint with `this` the argument: checkTypeArguments)
            for (u32 j = 0; j < args.len && j < params.len && fits; j++) {
                Ty const* constraint = ty_param_constraint(me, params.data[j]);
                fits = !constraint || ty_is_related(me, own[j],
                    ty_with_this_arg(me, ty_instantiate(me, constraint, own_mapper), own[j]), TY_RELATION_ASSIGNABLE);
            }
            if (!fits) {
                // (Kept as it is, see ty_instantiation_keeps_signature())
                sources[len] = params.data[0];
                targets[len++] = ty_intrinsic(me, TY_UNREACHABLE_NEVER);
                continue;
            }
            fitting++;
            for (u32 j = 0; j < params.len; j++) {
                bool seen = false;
                for (u32 k = 0; k < len; k++) seen |= sources[k] == params.data[j];
                if (seen) continue;
                sources[len] = params.data[j];
                targets[len++] = own[j];
            }
        }
    }
    // (None of them fits and none was dropped: the type as it is)
    if (!fitting && applicable && applicable == total) return ty;
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    return ty_apply_type_arguments_mapper(me, ty, mapper);
}

Ty const* ty_apply_type_arguments(Checker* me, Ty const* ty, TyList args) {
    if (ty->flags & TYF_ANY) return ty;
    return apply_type_arguments_part(me, ty, args, 0);
}

Ty const* ty_optional_expression_type(Checker* me, Ty const* object, bool is_root, bool* optional) {
    bool dummy = false;
    if (!optional) optional = &dummy;
    if (!compiler_options()->strict_null_checks) return is_root ? ty_non_nullable(me, object) : object;
    if (is_root) {
        *optional = ty_can_be_nullish(me, object) && !(object->flags & (TYF_ANY | TYF_UNKNOWN));
        // (tsc's getNonNullableType, which takes void too)
        return ty_non_nullish_part(me, object);
    }
    // (Only the rest of an optional chain sees the marker, see TY_OPTIONAL: no need to ask whether object_expr is one)
    Ty const* ret = ty_remove_optional_marker(me, object);
    *optional = ret != object;
    return ret;
}

static Ty const* chain_object_type(Checker* me, Expr const* object_expr, bool is_root, bool* optional) {
    return ty_optional_expression_type(me, ty_expr_type(me, object_expr), is_root, optional);
}

// `Symbol()` or `Symbol.for()` with the global `Symbol` (tsc's isSymbolOrSymbolForCall)
static bool is_symbol_call(Checker* me, CallExpr const* call) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_PROPERTY && span_equals(property_key_inner_span(callee->property), "for")) callee = &callee->property->expr;
    if (callee->tag != EXPR_IDENTIFIER || !csview_equals(span_sv(callee->inner_span), "Symbol")) return false;
    Symbol const* global = program_get_global(my program, c_sv("Symbol"), SYMBOL_VALUE);
    return global && binder_redirect(my binder, callee->identifier.target) == global;
}

// An argument of a call as tsc's getEffectiveCallArguments has it: the spread of a tuple is its elements (a rest
// element being a spread of its array)
typedef struct EffectiveArg {
    Expr const* expr; // A written argument, else an element of a spread tuple
    Ty const* type; // The element's type (the array's, for a rest element)
    bool spread; // A spread: of `expr` (a spread of something else than a tuple) or of the array `type`
    Atom const* tuple_name; // An element of a spread tuple: its label, if any
} EffectiveArg;

// Null if a spread tuple has generic elements: where the arguments go isn't known then
static EffectiveArg* effective_call_args(Checker* me, CallExpr const* call, u32* count_out) {
    u32 arg_count = (u32)vec_expr_size(&call->args);
    u32 cap = arg_count + 8;
    EffectiveArg* ret = malloc(sizeof(EffectiveArg) * cap);
    panic_if(!ret, "out of memory");
    u32 count = 0;
    for (u32 a = 0; a < arg_count; a++) {
        Expr const* arg = vec_expr_at(&call->args, a);
        if (arg->tag != EXPR_SPREAD) {
            if (count == cap) ret = realloc(ret, sizeof(EffectiveArg) * (cap *= 2));
            ret[count++] = (EffectiveArg){ .expr = arg };
            continue;
        }
        Ty const* spread = ty_expr_type(me, arg->spread);
        if (!ty_tuple_shape(spread)) {
            if (count == cap) ret = realloc(ret, sizeof(EffectiveArg) * (cap *= 2));
            ret[count++] = (EffectiveArg){ .expr = arg, .type = spread, .spread = true };
            continue;
        }
        TupleShape const* shape = ty_tuple_shape(spread);
        TyList elements = shape->arity ? ty_reference_args(me, spread) : (TyList){0};
        for (u32 i = 0; i < shape->arity && i < elements.len; i++) {
            bool rest = shape->flags[i] & TUPLE_ELEMENT_REST;
            if (rest && (elements.data[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
                free(ret);
                return nullptr;
            }
            if (count == cap) ret = realloc(ret, sizeof(EffectiveArg) * (cap *= 2));
            ret[count++] = (EffectiveArg){
                .type = elements.data[i], // (a rest element's is its array)
                .spread = rest,
                .tuple_name = shape->names ? shape->names[i] : nullptr,
            };
        }
    }
    *count_out = count;
    return ret;
}

// How many arguments an immediately invoked function gets (tsc's getEffectiveCallArguments(iife).length): past them its
// unannotated parameters are optional
u32 ty_iife_effective_arg_count(Checker* me, FunctionImpl const* function) {
    CallExpr const* call = function_impl_rare(function)->iife_call;
    if (!call) return function_impl_rare(function)->iife_arg_count;
    bool spread = false;
    for_each (i, vec_expr, &call->args) spread |= i->tag == EXPR_SPREAD;
    if (!spread) return function_impl_rare(function)->iife_arg_count;
    u32 count = 0;
    EffectiveArg* args = effective_call_args(me, call, &count);
    if (!args) return function_impl_rare(function)->iife_arg_count;
    free(args);
    return count;
}

// tsc's getContextuallyTypedParameterType of an immediately invoked function expression: its unannotated parameters
// are the widened types of the (effective) arguments, a rest parameter the tuple of those left (getSpreadArgumentType),
// a missing argument undefined (widening to any without strictNullChecks) unless the parameter has an initializer
static void assign_iife_parameter_types(Checker* me, FunctionImpl const* function, CallExpr const* call) {
    if (ty_function_assigned_contextual_signature(me, function)) return;
    u32 arg_count = 0;
    EffectiveArg* args = effective_call_args(me, call, &arg_count);
    if (!args) return;
    u32 index = 0;
    for_each (i, vec_function_arg, &function->args) {
        bool is_this = i->initializer.binding.tag == BINDING_IDENTIFIER && span_equals(i->initializer.binding.content_span, "this");
        if (is_this) continue;
        u32 at = index++;
        if (i->initializer.binding.has_explicit_type) continue;
        LazyMemo* param_memo = lazy_type_state_peek((LazyType*)&i->initializer.lazy_type);
        if (param_memo && param_memo->evaluation_started) continue;
        Ty const* type = nullptr;
        bool unknown = false;
        if (i->rest) {
            u32 arity = arg_count > at ? arg_count - at : 0;
            if (arg_count && args[arg_count - 1].spread && at >= arg_count - 1) {
                // Both the parameter and the last argument are a rest: its array (tsc's getMutableArrayOrTupleType,
                // which keeps a type variable constrained to a mutable array)
                type = args[arg_count - 1].type;
                if (type->flags & TYF_ANY) {
                    // (tsc's getMutableArrayOrTupleType keeps any)
                } else if (ty_is_tuple(type) && ty_tuple_shape(type)->readonly) {
                    unknown = true; // (TODO: the mutable copy)
                } else if (!ty_extends_array(me, type) && type->kind != TY_TYPE_PARAM) {
                    // An iterable: the array of what it iterates
                    type = ty_array(me, ty_iterated_type(me, ITERATION_SPREAD, type, span_null, nullptr), false);
                } else if (!ty_is_tuple(type) && !ty_is_array(me, type) && type->kind != TY_TYPE_PARAM) {
                    unknown = true;
                }
                if (unknown) continue;
            } else {
                unsigned char* flags = calloc(arity ? arity : 1, 1);
                Ty const** elements = calloc(arity ? arity : 1, sizeof(Ty const*));
                Atom const** names = nullptr; // (The labels of spread tuple elements stay: tsc's getSpreadArgumentType)
                for (u32 a = 0; a < arity; a++) {
                    EffectiveArg const* arg = &args[at + a];
                    if (arg->tuple_name) {
                        if (!names) names = calloc(arity, sizeof(Atom const*));
                        names[a] = arg->tuple_name;
                    }
                    if (arg->spread && arg->expr && !ty_extends_array(me, arg->type)) {
                        // An iterable: a rest element of what it iterates
                        elements[a] = ty_array(me, ty_iterated_type(me, ITERATION_SPREAD, arg->type, span_null, nullptr), false);
                        flags[a] = TUPLE_ELEMENT_REST;
                    } else if (arg->spread) {
                        // (a variadic element when it's a type variable)
                        elements[a] = arg->type;
                        flags[a] = TUPLE_ELEMENT_REST;
                    } else {
                        elements[a] = ty_widened_literal(me, arg->expr ? ty_expr_type(me, arg->expr) : arg->type);
                    }
                }
                TupleShape shape = { .arity = arity, .flags = flags, .names = names };
                Ty const* target = ty_tuple_target(me, &shape);
                type = arity ? ty_reference(me, target, (TyList){ .data = elements, .len = arity }) : target;
                free(flags);
                free(elements);
                free(names);
            }
        } else if (at < arg_count) {
            // (then widened like a declaration's type: null is any without strictNullChecks). A spread element is
            // what it spreads (tsc's checkSyntheticExpression)
            EffectiveArg const* arg = &args[at];
            // (Checked with the contextual type of an argument of a call being resolved, any: tsc's resolvingSignature,
            // so a generic reference with a union constraint is narrowed as its constraint, getNarrowableTypeForReference)
            // (A spread of something else than a tuple: what it iterates, tsc's checkExpression of the SpreadElement)
            Ty const* arg_type = arg->expr && arg->spread ? ty_iterated_type(me, ITERATION_SPREAD, arg->type, span_null, nullptr) :
                arg->expr ? ty_check_expr(me, arg->expr, ty_intrinsic(me, TY_ANY)) :
                arg->spread ? ty_reference_args(me, arg->type).data[0] : arg->type;
            type = ty_widened_for_declaration(me, arg_type, true);
        } else if (initialized_binding_value(&i->initializer)->tag == EXPR_IMPLICIT_ANY) {
            type = ty_intrinsic(me, compiler_options()->strict_null_checks ? TY_UNDEFINED : TY_ANY);
        }
        if (!type) continue;
        if (!ty_link_get(me, KEY_CONTEXT_PARAM, &i->initializer)) ty_link_set(me, KEY_CONTEXT_PARAM, &i->initializer, type);
    }
    free(args);
}

void ty_assign_iife_parameter_types(Checker* me, CallExpr const* call) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    if (callee->tag == EXPR_FUNCTION && callee->function->is_iife && !call->uses_new) {
        assign_iife_parameter_types(me, callee->function, call);
    }
}

// An immediately invoked function returns into the call's contextual type (tsc's getContextualReturnType), which
// only its return expressions see: the return type isn't widened against it (getReturnTypeFromBody). Pushed down by
// whoever checks the call with that context, before anything asks for the callee's return type (a flow query's quick
// type of an assigned `x = (() => ...)()`, tsc's getQuickTypeOfExpression, which finds the context up the tree). The
// first context decides
void ty_seed_iife_return_context(Checker* me, CallExpr const* call, Ty const* contextual) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
    if (callee->tag == EXPR_FUNCTION && callee->function->is_iife && !callee->function->ret_explicit && contextual &&
        !(contextual->flags & (TYF_ANY | TYF_VOID))
    ) {
        u64 key[2] = { KEY_CONTEXT_IIFE_RETURN, (u64)(uintptr_t)callee->function };
        void const** slot = intern_slot(me, &my links, key, 2);
        if (!*slot) *slot = contextual;
    }
}

static void check_function_parameters_first(Checker* me, FunctionImpl const* function) {
    (void)me;
    for_each (i, vec_function_arg, &((FunctionImpl*)function)->args) {
        if (initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY) initialized_binding_settle_value(&i->initializer);
    }
}

static Ty const* check_call(Checker* me, CallExpr const* call, Span call_span, ExprCtx ctx) {
    Expr const* callee = &call->callable;
    if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr; // call.c applies the type arguments
    ty_seed_iife_return_context(me, call, ctx.contextual);
    ty_assign_iife_parameter_types(me, call);
    // Inside an argument of a call inferring now, its body's return expressions are too: their contextual types see the
    // call's inferences (tsc's getInferenceContext walks up through the immediately invoked function)
    // The callee is checked below, without a contextual type (it has none: tsc's getContextualType of a call's
    // expression), which decides it has no contextual signature (KEY_CONTEXT_CHECKED). Decided now: asked for its return
    // type first, it would otherwise look unsettled and have its context root checked from here, out of the context the
    // call is in (inside an argument of a call inferring now, a function next to it would get no contextual signature
    // for good: openclaw's signal event-handler.ts TS7006)
    if (callee->tag == EXPR_FUNCTION && callee->function->is_iife && !function_impl_rare(callee->function)->jsdoc_signature_type &&
        !ty_link_get(me, KEY_CONTEXT_CHECKED, callee->function)
    ) {
        ty_link_set(me, KEY_CONTEXT_CHECKED, callee->function, &context_checked_none);
    }
    if (callee->tag == EXPR_FUNCTION && callee->function->is_iife && !call->uses_new && !callee->function->ret_explicit &&
        ty_outer_inference_active(me) && !ty_function_return_in_flight(me, callee->function)
    ) {
        ty_iife_return_type(me, callee->function);
    }
    bool optional = false;
    // The callee of a call's first resolution is checked behind its barrier (tsc's getResolvedSignature moves
    // resolutionStart, then resolveCallExpression checks the expression): a declaration in flight that the callee reads
    // is computed again, nested, not a cycle. akash's DI container: `const container = createContainer({ a: () =>
    // container.b(...), c: () => container.b(...) })`, the second member's `container.b` in the first re-entry
    // (A request for the same call while its callee is checked is no first resolution: tsc has set resolvingSignature)
    // (The calls whose callee is being checked: a short stack, nested calls in callees are few)
    static _Thread_local CallExpr const* callee_flight[32];
    static _Thread_local u32 callee_flight_len;
    bool first_resolution = !call->uses_new && !call->is_extends && callee_flight_len < c_arraylen(callee_flight);
    for (u32 i = 0; i < callee_flight_len && first_resolution; i++) first_resolution = callee_flight[i] != call;
    if (first_resolution) first_resolution = !ty_link_get(me, KEY_CALL_RESOLUTION, call);
    if (first_resolution) {
        ty_barrier_push(me, BARRIER_CALL);
        callee_flight[callee_flight_len++] = call;
    }
    Ty const* callee_type = chain_object_type(me, callee, call->optional_chain, &optional);
    if (first_resolution) {
        callee_flight_len--;
        ty_barrier_pop(me);
    }
    // An incomplete `never` of control flow calls to nothing (tsc's silentNeverSignature)
    if (callee_type == ty_intrinsic(me, TY_SILENT_NEVER)) return callee_type;
    if ((callee_type->flags & TYF_UNKNOWN) && !call->is_extends && compiler_options()->strict_null_checks) {
        callee_type = ty_intrinsic(me, TY_ERROR); // (See expr_typecheck(): TS18046)
    }
    if (!call->uses_new && !call->is_extends) callee_type = ty_check_non_null_callee(me, callee_type, span_null, nullptr);
    else if (call->uses_new && !call->is_extends) {
        Ty const* non_null = ty_check_non_null_type(me, callee_type, &call->callable, nullptr); // (See expr_typecheck())
        callee_type = non_null ? non_null : ty_intrinsic(me, TY_ERROR);
    }
    // (A call's arguments are checked as its own resolution says, not as members of an outer argument's return)
    u32 saved_argument_return_depth = my argument_return_depth;
    my argument_return_depth = 0;
    Ty const* ret = ty_call_return_type(me, callee_type, call, call_span, ctx.raw_contextual);
    my argument_return_depth = saved_argument_return_depth;
    if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER) {
        return ty_intrinsic(me, TY_VOID); // Whatever it resolved to, and even if it's an error (`new super()` too)
    }
    // The initializer of a `const` (or a `static readonly` property) makes a new unique symbol
    UniqueSymbolDecl const* unique_symbol = call_rare(call)->unique_symbol;
    if (unique_symbol && unique_symbol->key && (ret->flags & TYF_ES_SYMBOL_LIKE) && is_symbol_call(me, call)) {
        ret = ty_unique_symbol(me, unique_symbol);
    }
    return propagate_optional(me, ret, call->continues_chain, optional);
}

// What a `yield` evaluates to: what the generator is sent (its TNext), for `yield*` what the delegate returns (tsc's
// checkYieldExpression). The operand is checked with the yield type as its contextual type, a `yield*` operand with a
// generator that yields that, returns what the `yield*` itself is expected to be and is sent the same
static Ty const* check_yield(Checker* me, UnaryOpExpr const* yield, ExprCtx ctx) {
    FunctionImpl const* generator = yield->generator;
    Expr const* operand = &yield->expr;
    // What the generator is sent (tsc's checkAndAggregateYieldOperandTypes: getContextualType of each yield)
    if (ctx.contextual && yield->op == UNARY_OP_YIELD) {
        u64 key[2] = { KEY_YIELD_CONTEXT, (u64)(uintptr_t)yield };
        // (A call's type parameter, which the resolved signature has as what this any yield inferred: see
        // UnaryOpExpr.no_type_params_in_scope)
        Ty const* sent = ctx.contextual->kind == TY_TYPE_PARAM && yield->no_type_params_in_scope ? ty_intrinsic(me, TY_ANY) : ctx.contextual;
        if (!intern_get(&my links, key, 2)) *intern_slot(me, &my links, key, 2) = sent;
    }
    if (!generator) {
        if (operand->tag != EXPR_IMPLICIT_ANY) ty_expr_type(me, operand);
        return any_type(me);
    }
    bool async = generator->is_async;
    TyIterationTypes const* context = ty_generator_context(me, generator);
    if (yield->op == UNARY_OP_YIELD_STAR) {
        Ty const* operand_context = nullptr;
        if (context) {
            Ty const* silent = ty_intrinsic(me, TY_SILENT_NEVER);
            Ty const* yield_type = context->yield_type;
            Ty const* return_type = ctx.contextual ? ctx.contextual : silent;
            operand_context = ty_generator_type(me, false, yield_type, return_type, context->next_type);
            if (async) {
                Ty const* both[2] = { operand_context, ty_generator_type(me, true, yield_type, return_type, context->next_type) };
                operand_context = ty_union(me, both, 2, nullptr, (TyList){0});
            }
        }
        Ty const* delegate_type = check_with(me, operand, (ExprCtx){ .contextual = operand_context });
        u32 use = async ? ITERATION_ASYNC_YIELD_STAR : ITERATION_YIELD_STAR;
        TyIterationTypes const* delegate = ty_iteration_types_of_iterable(me, delegate_type, use);
        return delegate ? delegate->return_type : any_type(me);
    }
    if (operand->tag != EXPR_IMPLICIT_ANY) check_with(me, operand, (ExprCtx){ .contextual = ty_generator_yield_context(me, generator) });
    return context ? context->next_type : any_type(me);
}

// `t[5]` of tuples (every member of a union) with no element there: undefined after the error
// A numeric name (a number literal, or a string literal that is one) that no tuple of the object has as an element,
// every member being a tuple (tsc's getPropertyTypeForIndexType, isNumericLiteralName(propName)): from index 0 on what
// the tuples have past their leading elements (getTupleElementTypeOutOfStartCount: their rest and trailing elements,
// undefined for one without a rest element), a negative one undefined of one tuple without rest elements and else what
// the index signatures say. Null where the ordinary access decides
static Ty const* tuple_numeric_name_access(Checker* me, Ty const* object, Ty const* key, bool reading) {
    double index;
    Atom const* name;
    if ((key->flags & TYF_NUMBER_LITERAL) && !(key->flags & TYF_ENUM_LITERAL)) {
        index = key->literal.value.number;
        name = ty_literal_name(me, key);
    } else if ((key->flags & TYF_STRING_LITERAL) && !(key->flags & TYF_ENUM_LITERAL) && key->literal.string &&
        ty_is_numeric_name(key->literal.string)
    ) {
        name = key->literal.string;
        char buf[64];
        snprintf(buf, sizeof buf, "%.*s", (int)name->len, name->str);
        index = strtod(buf, nullptr);
    } else {
        return nullptr;
    }
    if (!name) return nullptr;
    TyList types = object->kind == TY_UNION ? object->composite.types : (TyList){ .data = &object, .len = 1 };
    bool variable = false;
    for (u32 i = 0; i < types.len; i++) {
        Ty const* t = types.data[i];
        // (Generic ones are indexed access types: tsc's isGenericObjectType comes first)
        if (!ty_is_tuple(t) || ty_is_generic_tuple(me, t)) return nullptr;
        if (ty_find_prop(me, t, name).found) return nullptr;
        TupleShape const* shape = t->reference.target->declared.tuple;
        for (u32 j = 0; j < shape->arity; j++) variable |= (shape->flags[j] & TUPLE_ELEMENT_REST) != 0;
    }
    if (index < 0) {
        if (types.len == 1 && !variable) return ty_intrinsic(me, TY_UNDEFINED); // TS2514
        return ty_access(me, object, ty_intrinsic(me, TY_NUMBER)); // (TS2339 on a union, then its index signature)
    }
    TyBuf parts = {0};
    for (u32 i = 0; i < types.len; i++) {
        Ty const* t = types.data[i];
        TupleShape const* shape = t->reference.target->declared.tuple;
        TyList args = ty_reference_args(me, t);
        u32 first_rest = shape->arity;
        for (u32 j = 0; j < shape->arity; j++) {
            if (shape->flags[j] & TUPLE_ELEMENT_REST) {
                first_rest = j;
                break;
            }
        }
        if (first_rest == shape->arity) {
            ty_buf_push(&parts, ty_intrinsic(me, TY_UNDEFINED));
            continue;
        }
        for (u32 j = first_rest; j < shape->arity && j < args.len; j++) {
            // (A rest element's argument is its array: the element type is what's read)
            Ty const* arg = args.data[j];
            if (shape->flags[j] & TUPLE_ELEMENT_REST) {
                // (Not an array: a variadic element, generic; the ordinary access decides)
                if (!ty_is_array(me, arg) && !(arg->kind == TY_REFERENCE && arg->reference.target == ty_readonly_array_target(me))) {
                    ty_buf_drop(&parts);
                    return nullptr;
                }
                arg = ty_reference_args(me, arg).data[0];
            }
            ty_buf_push(&parts, arg);
        }
        // (Past as many elements as are fixed, leading and trailing, with noUncheckedIndexedAccess:
        // AccessFlags.IncludeUndefined's missing type, getTotalFixedElementCount)
        u32 fixed = 0;
        for (u32 j = 0; j < shape->arity; j++) fixed += !(shape->flags[j] & TUPLE_ELEMENT_REST); // (ElementFlagsFixed: optional ones too)
        if (reading && compiler_options()->no_unchecked_indexed_access && index >= fixed) {
            ty_buf_push(&parts, ty_intrinsic(me, TY_UNDEFINED));
        }
    }
    Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

bool ty_tuple_past_the_end(Checker* me, Ty const* object, Ty const* key) {
    // (A numeric string literal names an element too: tsc's isNumericLiteralName(propName))
    bool numeric_name = (key->flags & TYF_STRING_LITERAL) && !(key->flags & TYF_ENUM_LITERAL) && key->literal.string &&
        ty_is_numeric_name(key->literal.string);
    if (!(key->flags & TYF_NUMBER_LITERAL) && !numeric_name) return false;
    TyList types = object->kind == TY_UNION ? object->composite.types : (TyList){ .data = &object, .len = 1 };
    // (Null and undefined aside: an element access's object is checkNonNullExpression's, TS18048 says the rest)
    bool tuple = false;
    for (u32 i = 0; i < types.len; i++) {
        if (types.data[i]->flags & TYF_NULLABLE) continue;
        Ty const* apparent = ty_apparent_type(me, types.data[i]);
        if (!ty_is_tuple(apparent) && !ty_tuple_shape(apparent)) return false; // (`[]` is its own target)
        tuple = true;
    }
    return tuple;
}

// `{ a: 1, b: "" }[k]` that doesn't find k (tsc's getPropertyTypeForIndexType on an object literal type): a missing
// literal key is undefined (TS2339 under noImplicitAny), a string or number key any of the properties or undefined
static Ty const* object_literal_access(Checker* me, Ty const* object, Ty const* key) {
    // (A JavaScript object literal's is any first: tsc's isJSLiteralType comes before isObjectLiteralType there)
    if (ty_is_js_literal_type(me, object)) return nullptr;
    key = ty_regular(me, key);
    TyList keys = key->kind == TY_UNION && !(key->flags & TYF_BOOLEAN) ? key->composite.types : (TyList){ .data = &key, .len = 1 };
    TyBuf parts = {0};
    for (u32 i = 0; i < keys.len; i++) {
        Ty const* part = ty_access(me, object, keys.data[i]);
        if (!part && (keys.data[i]->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL))) {
            // (Only with noImplicitAny, where it's TS2339: else tsc's getPropertyTypeForIndexType goes on to any)
            if (!compiler_options()->no_implicit_any) {
                ty_buf_drop(&parts);
                return nullptr;
            }
            part = ty_intrinsic(me, TY_UNDEFINED);
        } else if (!part && (keys.data[i]->flags & (TYF_STRING | TYF_NUMBER))) {
            Members const* members = ty_members(me, object);
            for (u32 j = 0; j < members->prop_count; j++) ty_buf_push(&parts, prop_type(me, members_prop(me, members, j)));
            part = ty_intrinsic(me, TY_UNDEFINED);
        }
        if (!part) {
            ty_buf_drop(&parts);
            return nullptr;
        }
        ty_buf_push(&parts, part);
    }
    Ty const* ret = ty_union(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

// What `o[k] = v` takes for a union of keys: every key's property, the intersection of their (write) types (tsc's
// getIndexedAccessType with AccessFlags.Writing). Null when a key has none (the read reports it). `compound`: `o[k] += v`
// reads it too, an index signature's value includes undefined under noUncheckedIndexedAccess (AccessFlags.Writing |
// ExpressionPosition), still the intersection
static Ty const* union_key_write_type(Checker* me, Ty const* object, Ty const* key, bool compound) {
    // (A generic object with keys that aren't: its apparent type's, like tsc's getIndexedAccessType of an element access
    // expression, which only defers for a generic index or tuple)
    if (ty_is_generic_object(me, object)) object = ty_apparent_type(me, object);
    if (ty_is_generic_object(me, object)) return nullptr;
    TyList keys = key->composite.types;
    TyBuf parts = {0};
    for (u32 i = 0; i < keys.len; i++) {
        Ty const* k = keys.data[i];
        if (!(k->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL))) {
            ty_buf_drop(&parts);
            return nullptr;
        }
        Ty const* part = ty_access_write_type(me, object, k);
        if (!part) part = compound ? ty_access_element(me, object, k, true) : ty_access(me, object, k);
        if (!part) {
            ty_buf_drop(&parts);
            return nullptr;
        }
        // (Each an optional property's write type without the missing type before they're intersected: tsc's
        // getWriteTypeOfSymbol per key, `string & boolean & any` is never where `(string | missing) & ...` wouldn't be)
        ty_buf_push(&parts, ty_remove_missing(me, part, written_prop_is_optional(me, object, k)));
    }
    Ty const* ret = ty_intersection(me, parts.data, parts.len, nullptr, (TyList){0});
    ty_buf_drop(&parts);
    return ret;
}

Ty const* ty_compound_assignee_type(Checker* me, Expr const* target, Ty const* read_type) {
    if (target->tag != EXPR_PROPERTY || target->property->form != PROPERTY_EXPR_DOT) return read_type;
    Ty const* object = ty_expr_type(me, &target->property->expr);
    Ty const* write = ty_access_write_type(me, object, ty_access_key_type(me, target->property));
    return write ? write : read_type;
}

// tsc's isStringIndexSignatureOnlyType: nothing but a string index signature
static bool is_string_index_only(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!is_string_index_only(me, ty->composite.types.data[i])) return false;
        }
        return true;
    }
    if (!(ty->flags & TYF_OBJECT)) return false;
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->call_count && !members->construct_count && !members->mapped &&
        members->index_count == 1 && (members->index[0].key->flags & TYF_STRING);
}

// JavaScript `this.x` in the constructor that declares x, when only `this.x = v` assignments do (tsc's
// isThisPropertyAccessInConstructor with isConstructorDeclaredProperty): x, whose type is being inferred from them.
// Null otherwise
static Symbol const* constructor_declared_member(PropertyExpr const* access) {
    // `this.x`, `this["x"]`
    csview name = span_sv(property_key_inner_span(access));
    if (access->form != PROPERTY_EXPR_DOT) {
        if (access->index.tag != EXPR_LITERAL || access->index.literal.tag != VALUE_STRING || name.size < 2) return nullptr;
        name = csview_slice(name, 1, name.size - 1);
    }
    Expr const* object = &access->expr;
    IdentifierExpr const* this = expr_this_keyword(object);
    if (!this) return nullptr;
    SourceFile const* file = span_source(object->inner_span);
    if (!file || !file->is_js) return nullptr;
    if (this->this_kind != THIS_CLASS_INSTANCE || !property_context(access)->in_constructor_of) return nullptr;
    Symbol const* class_symbol = this->target;
    if (!class_symbol || !symbol_rare(class_symbol)->members) return nullptr;
    Symbol const* member = symbol_table_get(symbol_rare(class_symbol)->members, name);
    if (!member) return nullptr;
    if (!ty_is_constructor_declared(member)) return nullptr;
    // Not when the class body declares it
    ClassDecl const* class_decl = symbol_find_node(class_symbol, SYMBOL_DECL_CLASS);
    return !class_decl || !class_decl_find_member(class_decl, name, false) ? member : nullptr;
}

// `exports.x` in its own file of an export only alias assignments declare, several of them (tsc's getTypeOfAlias:
// autoType, getFlowTypeOfAccessExpression types it by control flow from undefined). Null otherwise
// An assignment to it: any
static bool is_constructor_declared_target(Checker* me, PropertyExpr const* access) {
    (void)me;
    return access->assignment_target == FLOW_TARGET_DEFINITE && constructor_declared_member(access);
}

// A read of it in a class constructor: what control flow says it has been assigned so far (tsc's getFlowTypeOfProperty
// with autoType). Null otherwise
static Ty const* constructor_declared_read(Checker* me, Expr const* expr) {
    PropertyExpr const* access = expr->property;
    if (access->assignment_target || !access->flow) return nullptr;
    Symbol const* member = constructor_declared_member(access);
    if (!member || !symbol_rare(member)->declaring_constructor || !function_impl_return_flow(symbol_rare(member)->declaring_constructor)) return nullptr;
    if (expr_this_keyword(&access->expr)->this_kind != THIS_CLASS_INSTANCE) return nullptr;
    return ty_constructor_declared_flow_type(me, member, expr, access->flow);
}

// `o[k]` with o made of nothing but a string index signature and a key that is a string or a number (each on its own,
// tsc's isTypeAssignableToKind: `K extends string | number` is neither) reads that signature
static bool reads_string_index_only(Checker* me, Ty const* object, Ty const* key) {
    return is_string_index_only(me, object) && !(key->flags & TYF_NULLABLE) &&
        (ty_is_related(me, key, ty_intrinsic(me, TY_STRING), TY_RELATION_ASSIGNABLE) ||
            ty_is_related(me, key, ty_intrinsic(me, TY_NUMBER), TY_RELATION_ASSIGNABLE));
}

// `o[k]` with a generic key that ty_access() couldn't read through a constraint: the deferred
// `O[K]` (tsc's getIndexedAccessType), unless o has nothing but a string index signature, which is what it reads then
static Ty const* generic_element_access(Checker* me, PropertyExpr const* access, Ty const* object, Ty const* key) {
    key = ty_regular(me, key);
    if (!ty_is_generic_index(key)) return nullptr;
    // (An unknown object was TS18046/TS2571 and is the error type then, whatever the key: tsc's checkNonNullExpression)
    if ((object->flags & TYF_UNKNOWN) && compiler_options()->strict_null_checks) return ty_intrinsic(me, TY_ERROR);
    if (object->flags & (TYF_ANY | TYF_UNKNOWN)) return object;
    // (Of the non-nullable object: tsc's checkNonNullExpression has reported null and undefined, TS18048, and removed
    // them; `obj[key]` with `obj: T extends R | undefined` is `R[K]`)
    if (compiler_options()->strict_null_checks && (ty_can_be_null(me, object) || ty_can_be_undefined(me, object))) {
        Ty const* non_null = ty_non_nullable(me, object);
        if (!(non_null->flags & (TYF_NULLABLE | TYF_NEVER))) object = non_null;
    }
    if (reads_string_index_only(me, object, key)) {
        Ty const* string = ty_intrinsic(me, TY_STRING);
        // (A compound assignment reads it too: tsc's AccessFlags.ExpressionPosition unless Definite)
        return access->assignment_target == FLOW_TARGET_DEFINITE ? ty_access(me, object, string) : ty_access_reading(me, object, string);
    }
    // (A read under noUncheckedIndexedAccess: tsc's AccessFlags.IncludeUndefined on the deferred type)
    bool include_undefined = compiler_options()->no_unchecked_indexed_access && access->assignment_target != FLOW_TARGET_DEFINITE;
    // (What tsc's checkIndexedAccessIndexType rejects becomes the error type once the flow type is known, see
    // invalid_generic_access_type())
    return ty_indexed_include_undefined(me, object, key, include_undefined);
}

// tsc's checkIndexedAccessIndexType on the flow type of an element access: an `O[K]` whose index isn't a key of O is
// the error type (reported as TS2536 by the typecheck pass), unless getNarrowableTypeForReference replaced it with its
// constraint (`o[k as T].p` with `o: { [P in keyof T]: A | B }`)
static Ty const* invalid_generic_access_type(Checker* me, Ty const* type) {
    return type->kind == TY_INDEXED && !ty_indexed_access_index_ok(me, type) ? ty_intrinsic(me, TY_ERROR) : type;
}

// Whether an evaluation of the element access found its deferred `O[K]` invalid, the error type, before any flow type
// replaced it: tsc reports TS2536 from every checkElementAccessExpression, so an inference pass's evaluation (the
// argument of a generic call, not narrowed to the constraint there: getNarrowableTypeForReference skips inferential
// checks) says it even when the final one is the constraint
bool ty_generic_access_was_invalid(Checker* me, PropertyExpr const* access) {
    return intern_get(&my links, (u64[]){ KEY_INVALID_GENERIC_ACCESS, (u64)(uintptr_t)access }, 2) != nullptr;
}

// `o[k]` with k generic that tsc's checkIndexedAccessIndexType rejects: the indexed access type, else null
Ty const* ty_invalid_generic_element_access(Checker* me, PropertyExpr const* access, Ty const* object) {
    if (access->form != PROPERTY_EXPR_BRACKET) return nullptr;
    Ty const* key = ty_regular(me, ty_access_key_type(me, access));
    if (!ty_is_generic_index(key) || (object->flags & (TYF_ANY | TYF_UNKNOWN | TYF_ERROR))) return nullptr;
    if (ty_access(me, object, key)) return nullptr;
    if (reads_string_index_only(me, object, key)) {
        return nullptr;
    }
    Ty const* ret = ty_indexed(me, object, key);
    return ret->kind == TY_INDEXED && !ty_indexed_access_index_ok(me, ret) ? ret : nullptr;
}

// A class expression's static fields without a type annotation have the contextual type of the property of the
// same name of the class expression's contextual type (tsc's getContextualTypeForInitializerExpression)
static void contextually_type_static_fields(Checker* me, ClassDecl const* class_decl, Ty const* contextual) {
    for_each (i, vec_class_item, &class_decl->items) {
        if (i->tag != CLASS_ITEM_FIELD || !i->field.is_static || i->field.has_explicit_type) continue;
        if (expr_absent(&i->field.value) || span_valid(class_item_private_name(i))) continue;
        Ty const* key = i->field.computed_key ? ty_regular(me, ty_expr_type(me, i->field.computed_key)) :
            ty_string_literal(me, cstr_sv(&i->field.name_string));
        if (!(key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL | TYF_UNIQUE_ES_SYMBOL))) continue;
        Ty const* prop = ty_contextual_prop_type(me, contextual, key);
        if (prop) ty_check_expr(me, &i->field.value, prop);
    }
}

static Ty const* compute_inner(Checker* me, Expr const* expr, ExprCtx ctx);

static Ty const* compute(Checker* me, Expr const* expr, ExprCtx ctx) {
    // tsc's checkExpression: the current node, and the instantiation limit is per expression, not per program
    CurrentNode saved = ty_enter_expr(me, expr);
    Ty const* ret = compute_inner(me, expr, ctx);
    ty_leave_node(me, saved);
    return ret;
}

static Ty const* compute_inner(Checker* me, Expr const* expr, ExprCtx ctx) {
    switch (expr->tag) {
        case EXPR_IMPLICIT_ANY: return any_type(me);
        case EXPR_LITERAL:
            if (expr->literal.tag == VALUE_NULL) return ty_intrinsic(me, TY_NULL_WIDENING);
            if (expr->undefined_assigned) return ty_intrinsic(me, TY_ERROR); // (tsc's checkIdentifier)
            if (expr->literal.tag == VALUE_UNDEFINED) return ty_intrinsic(me, TY_UNDEFINED_WIDENING);
            return ty_fresh_literal(me, ty_literal(me, &expr->literal));
        case EXPR_IDENTIFIER: {
            IdentifierFlowIssue issue = IDENTIFIER_FLOW_OK; // Reported by the typecheck walk
            return identifier_flow_type(me, expr, ctx.contextual, &issue, false);
        }
        case EXPR_BINARY_OP: {
            Ty const* ret = check_binary(me, expr->binary_op, ctx);
            // (An equality or arithmetic a loop's unfinished flow analysis evaluates is checked on the partial types
            // too: tsc's getTypeOfExpression of an assigned value reports TS2367 for the first iteration's narrowed
            // operands, `a = pick(a === b)` in a loop, and TS18048 for `t = t + n` with `let t;`, whose final type is
            // any, see ty_flow_partial_reports())
            BinaryOp op = expr->binary_op->op;
            // (CheckModeTypeOnly drops an equality's errors alone: checkBinaryLikeExpressionWorker)
            bool equality = op == BINARY_OP_DOUBLE_EQUAL || op == BINARY_OP_TRIPLE_EQUAL || op == BINARY_OP_NOT_EQUAL ||
                op == BINARY_OP_TRIPLE_NOT_EQUAL;
            if (binary_op_checks_operands(op) && ty_flow_partial_reports(me, expr->inner_span) &&
                !(equality && my type_only_root && type_only_reaches(my type_only_root, expr))
            ) {
                expr_binary_operator_errors(expr, ty_expr_type(me, &expr->binary_op->left), ty_expr_type(me, &expr->binary_op->right), my errs);
            }
            return ret;
        }
        case EXPR_UNARY_OP:
            if (expr->unary_op->op == UNARY_OP_YIELD || expr->unary_op->op == UNARY_OP_YIELD_STAR) {
                return check_yield(me, expr->unary_op, ctx);
            }
            // `x!` has the contextual type of its operand
            // (Checked as tsc's checkNonNullAssertion does, checkExpression without the check mode: not inferentially,
            // so a generic reference with a union constraint in a context that isn't generic is its constraint even
            // inside a call's inference, narrowable_type(): effect's `Object.hasOwn(mapping, k) ? mapping[k]! : k`)
            if (expr->unary_op->op == UNARY_OP_RIGHT_BANG && ctx.contextual) {
                u32 inferential_depth = my inferential_depth;
                my inferential_depth = 0;
                Ty const* type = check_with(me, &expr->unary_op->expr, (ExprCtx){ .contextual = ctx.contextual });
                my inferential_depth = inferential_depth;
                return ty_non_nullable(me, type);
            }
            // tsc's getContextualTypeForAwaitOperand: what the context awaits to, or a promise of it
            if (expr->unary_op->op == UNARY_OP_AWAIT && ctx.contextual) {
                // (An operand that asks for its contextual type, a literal or a function, makes tsc compute the context's
                // awaited type silently before the `await` is checked: `const x: U = await []; await u` is no TS1320)
                Expr const* operand_expr = &expr->unary_op->expr;
                __auto_type operand = operand_expr->tag;
                if (operand == EXPR_ARRAY || operand == EXPR_OBJECT || operand == EXPR_FUNCTION || operand == EXPR_LITERAL ||
                    operand == EXPR_TEMPLATE
                ) {
                    ty_await_context_awaited(me, ctx.contextual);
                }
                Ty const* awaited = ty_awaited_no_alias(me, ctx.contextual);
                Ty const* parts[2] = { awaited, ty_promise_like_of(me, awaited) };
                Ty const* context = ty_union(me, parts, 2, nullptr, (TyList){0});
                // (Still what a binding pattern implies, which calls treat differently: tsc's isFromBindingPattern)
                if (ctx.contextual->flags & TYF_BINDING_PATTERN) context = ty_pattern_clone(me, context);
                return ty_awaited(me, check_with(me, &expr->unary_op->expr, (ExprCtx){ .contextual = context }));
            }
            return check_unary(me, expr->unary_op);

        case EXPR_PROPERTY: {
            if (ty_is_import_meta(expr)) return ty_import_meta_expr_type(me, expr);
            if (is_constructor_declared_target(me, expr->property)) return any_type(me);
            Ty const* in_constructor = constructor_declared_read(me, expr);
            if (in_constructor) return in_constructor;
            bool optional = false;
            Ty const* object = chain_object_type(me, &expr->property->expr, expr->property->optional_chain, &optional);
            // (An incomplete never is silent: tsc's getTypeAtFlowCondition)
            if (ty_flow_partial_reports(me, expr->inner_span) && object != ty_intrinsic(me, TY_SILENT_NEVER)) {
                expr_property_access_errors(expr, object, true, my errs);
            }
            // (tsc's getWidenedType: an object literal isn't one any more, a fresh literal widens)
            if (expr->property->method_call || expr->property->assignment_target) {
                object = ty_widened_objects(me, ty_widened_nullable(me, object));
            }
            // An incomplete `never` of control flow is like any (tsc's silentNeverType), a property of the error type is
            // the error type (checkPropertyAccessExpressionOrQualifiedName)
            // (isErrorType: an unresolved type name is one too)
            if (object == ty_intrinsic(me, TY_SILENT_NEVER)) return object;
            // (An element access is the object's own error type: an unresolved name's, which prints as the name,
            // tsc's checkElementAccessExpression)
            if (object->flags & TYF_ERROR) return expr->property->form == PROPERTY_EXPR_BRACKET ? object : ty_intrinsic(me, TY_ERROR);
            // `never` has no properties (TS2339, the error type: `never.a<T>` isn't TS2635 too), `never[k]` is never
            if ((ty_reduced(me, object)->flags & TYF_NEVER) && expr->property->form == PROPERTY_EXPR_DOT) return ty_intrinsic(me, TY_ERROR);
            // A const enum's element by anything but a string literal is TS2476 and the error type (tsc's
            // checkElementAccessExpression), its reverse mapping's index signature notwithstanding
            if (expr->property->form == PROPERTY_EXPR_BRACKET && object->kind == TY_DECLARED &&
                object->declared.kind == TY_DECLARED_ENUM_OBJECT && object->declared.symbol &&
                (object->declared.symbol->flags & SYMBOL_CONST_ENUM) &&
                (expr->property->index.tag != EXPR_LITERAL || expr->property->index.literal.tag != VALUE_STRING)
            ) {
                return ty_intrinsic(me, TY_ERROR);
            }
            Ty const* key = ty_access_key_type(me, expr->property);
            if (expr->property->form == PROPERTY_EXPR_BRACKET) key = ty_string_index_only_key(me, object, key);
            // (By never, what the non-nullable object's index signatures say: tsc's checkNonNullExpression has removed
            // null and undefined, `t[null!]` with `T extends Record<string, any> | undefined` is any)
            if (expr->property->form == PROPERTY_EXPR_BRACKET && (key->flags & TYF_NEVER) && compiler_options()->strict_null_checks) {
                object = ty_non_nullable(me, object);
            }
            // `o[k]` with k any and no index signature that takes it is errorType (tsc's getPropertyTypeForIndexType
            // finds nothing for an access expression)
            // (Of the non-nullable object: tsc's checkElementAccessExpression has reported and removed null and undefined)
            if (expr->property->form == PROPERTY_EXPR_BRACKET && (key->flags & TYF_ANY) && !(object->flags & TYF_ANY)) {
                Ty const* apparent = ty_apparent_type(me, ty_non_nullable(me, object));
                if ((apparent->flags & (TYF_OBJECT | TYF_INTERSECTION)) && !(apparent->flags & TYF_ANY) && !ty_is_js_literal_type(me, apparent) &&
                    !ty_applicable_index_info(me, apparent, key)
                ) {
                    return ty_intrinsic(me, TY_ERROR);
                }
            }
            // (Written to, a generic object's index signatures don't apply: TS2339/TS2862, errorType)
            if (ty_generic_write_index_info(me, expr->property, object, key)) return ty_intrinsic(me, TY_ERROR);
            // (A compound assignment reads it too: tsc's AccessFlags.ExpressionPosition unless Definite)
            // (`never[K]` with a generic K is deferred like any generic access: tsc's getIndexedAccessTypeOrUndefined
            // looks at the index before the object)
            bool deferred_never = (object->flags & TYF_NEVER) && expr->property->form == PROPERTY_EXPR_BRACKET &&
                ty_is_generic_index(ty_regular(me, key));
            // (An element access of a generic tuple past its leading fixed elements is deferred: `args[args.length -
            // 1]` of `[...T, F]` is `[...T, F][number]`, tsc's getIndexedAccessTypeOrUndefined with an access node)
            bool deferred_tuple = expr->property->form == PROPERTY_EXPR_BRACKET && ty_is_tuple(object) &&
                ty_is_generic_tuple(me, object) && !(key->flags & (TYF_ANY | TYF_ERROR));
            Ty const* numeric_tuple = expr->property->form == PROPERTY_EXPR_BRACKET && !deferred_never && !deferred_tuple ?
                tuple_numeric_name_access(me, object, ty_regular(me, key),
                    expr->property->assignment_target != FLOW_TARGET_DEFINITE) : nullptr;
            // (A compound assignment to `o[k]` with a union of keys is written too: the intersection of what each key
            // takes, `s[k] += 1` with one optional property among them isn't possibly undefined, tsc's
            // checkElementAccessExpression takes AccessFlags.Writing for any assignment target)
            Ty const* compound_union = expr->property->form == PROPERTY_EXPR_BRACKET && !numeric_tuple && !deferred_never &&
                !deferred_tuple && expr->property->assignment_target == FLOW_TARGET_COMPOUND && key->kind == TY_UNION &&
                !(key->flags & TYF_BOOLEAN) ? union_key_write_type(me, object, key, true) : nullptr;
            Ty const* ret = compound_union ? compound_union : numeric_tuple ? numeric_tuple : deferred_never ? nullptr :
                deferred_tuple ? ty_indexed_include_undefined(me, object, ty_regular(me, key),
                    compiler_options()->no_unchecked_indexed_access && expr->property->assignment_target != FLOW_TARGET_DEFINITE) :
                expr->property->form == PROPERTY_EXPR_BRACKET ?
                    ty_access_element(me, object, key, expr->property->assignment_target != FLOW_TARGET_DEFINITE) :
                expr->property->assignment_target == FLOW_TARGET_DEFINITE ? ty_access(me, object, key) :
                ty_access_reading(me, object, key);
            // (An element access: a property access of what an object literal lacks is TS2339, checkPropertyAccessExpression)
            if (!ret && ty_is_fresh_object(object) && !expr->property->assignment_target && expr->property->form == PROPERTY_EXPR_BRACKET) {
                ret = object_literal_access(me, object, key);
            }
            Ty const* generic_access = nullptr;
            if (!ret && expr->property->form == PROPERTY_EXPR_BRACKET) ret = generic_access = generic_element_access(me, expr->property, object, key);
            if (!ret && expr->property->qualified_name && (key->flags & TYF_STRING_LITERAL)) {
                Prop const* prop = ty_namespace_type_only_prop(me, object, ty_literal_name(me, key));
                if (prop) ret = prop_type(me, prop);
            }
            if (!ret && expr->property->form == PROPERTY_EXPR_BRACKET && ty_tuple_past_the_end(me, object, key)) {
                return ty_intrinsic(me, TY_UNDEFINED); // TS2493/TS2514 (tsc's getPropertyTypeForIndexType)
            }
            if (!ret) {
                // (Of an element access, the error type is narrowed by control flow like any other: tsc's
                // checkElementAccessExpression calls getFlowTypeOfAccessExpression with errorType, `isObj(c['k'])`)
                Ty const* missing = missing_property_type(me, expr, object);
                if (expr->property->form != PROPERTY_EXPR_BRACKET) return missing;
                return access_flow_type(me, expr, object, key, missing, ctx.contextual);
            }
            // (A generic object whose apparent type is never, `never[keyof T]`, has no properties either: TS2339 and the
            // error type, not never, tsc's checkPropertyAccessExpressionOrQualifiedName)
            if (expr->property->form == PROPERTY_EXPR_DOT && (ret->flags & TYF_NEVER) && !(object->flags & TYF_NEVER) &&
                (ty_reduced(me, ty_apparent_type(me, object))->flags & TYF_NEVER)
            ) {
                return any_type(me);
            }
            if (ty_is_readonly_assignment(me, expr->property, object)) return any_type(me);
            if (expr->property->assignment_target == FLOW_TARGET_DEFINITE) {
                // (Not the write type where a property access is read too, `a.r ??= v`, `a.r! = v`, `[...a.r] = v`:
                // tsc's isWriteOnlyAccess is false there. The assignment checks a logical one against the setter's type,
                // see assignment_target_typecheck_of(). An element access is written wherever it's an assignment target,
                // `o[k] ??= v` with a union key is the intersection: tsc's checkElementAccessExpression takes
                // AccessFlags.Writing for isAssignmentTarget)
                bool read_too = expr->property->read_target && expr->property->form == PROPERTY_EXPR_DOT;
                Ty const* write = read_too ? nullptr : key->kind == TY_UNION ? union_key_write_type(me, object, key, false) :
                    ty_access_write_type(me, object, key);
                if (write) ret = write;
                ret = ty_remove_missing(me, ret, written_prop_is_optional(me, object, key)); // (tsc's getFlowTypeOfAccessExpression)
            }
            // (A property of any isn't narrowed: tsc's checkPropertyAccessExpressionOrQualifiedName returns any before
            // getFlowTypeOfAccessExpression. An element access is)
            if (expr->property->form != PROPERTY_EXPR_DOT || !(ty_apparent_type(me, object)->flags & TYF_ANY)) {
                ret = access_flow_type(me, expr, object, key, ret, ctx.contextual);
            }
            if (generic_access) {
                Ty const* checked = invalid_generic_access_type(me, ret);
                if (checked != ret) {
                    *intern_slot(me, &my links, (u64[]){ KEY_INVALID_GENERIC_ACCESS, (u64)(uintptr_t)expr->property }, 2) = expr->property;
                }
                ret = checked;
            }
            return propagate_optional(me, ret, expr->property->continues_chain, optional);
        }

        case EXPR_GENERIC_PARAMS: {
            // An instantiation expression. As the callee of a call it never gets here, see check_call()
            GenericParamsExpr const* generic = expr->generic_params;
            // (A class's `extends B<T>` is a type reference's type arguments, what the class's base constructor type
            // and super calls make of them: tsc's getBaseConstructorTypeOfClass checks the expression alone)
            if (generic->heritage) return ty_expr_type(me, &generic->expr);
            Ty const* stack_args[32];
            u32 count = (u32)vec_spanned_type_size(&generic->params);
            Ty const** args = count <= c_arraylen(stack_args) ? stack_args : arena_new_n(&my arena, Ty const*, count);
            u32 len = 0;
            for_each (i, vec_spanned_type, &generic->params) args[len++] = ty_from_node(me, &i->type, expr_lower_ctx);
            return ty_apply_type_arguments(me, ty_expr_type(me, &generic->expr), (TyList){ .data = args, .len = len });
        }

        case EXPR_AS_TYPE: {
            AsTypeExpr const* as_type = expr->as_type;
            // (tsc's checkAssertion: the regular literal type, which no longer widens)
            // (Its operand has the contextual type of the assertion: tsc's getContextualType of an AsExpression)
            if (as_type_is_const(as_type)) {
                return ty_regular(me, check_with(me, &as_type->expr, (ExprCtx){ .contextual = ctx.contextual, .is_const = true }));
            }
            // (Resolved as the type node: tsc's checkAssertionWorker gets it under checkSourceElement(type), so what
            // goes wrong inside it, TS2590 of `a as A & B`, is reported there)
            CurrentNode saved_node = {0};
            bool entered = !as_type->angle_bracket_syntax && !as_type->is_jsdoc && span_valid(as_type->keyword_span) &&
                span_valid(expr->inner_span) && span_source(expr->inner_span) == span_source(as_type->keyword_span);
            if (entered) {
                char const* start = span_end_ptr(as_type->keyword_span);
                char const* end = span_end_ptr(expr->inner_span);
                while (start < end && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) start++;
                saved_node = ty_enter_node(me, span_make_in(expr->inner_span, start, end));
            }
            Ty const* type = ty_from_node(me, &as_type->type, expr_lower_ctx);
            if (entered) ty_leave_node(me, saved_node);
            // The type of `e as T` doesn't depend on e, but checking it checks e right away (tsc's checkAssertionWorker:
            // only the comparison is deferred). A declaration's initializer and control flow don't check it
            // (ty_quick_expr_type(), tsc's getQuickTypeOfExpression): `const data = get(id) as Data` in a loop that
            // assigns id from data isn't circular
            if (!as_type->is_satisfies) {
                if (!my flow_uncached) ty_check_expr(me, &as_type->expr, type);
                return type;
            }
            Ty const* checked = ty_check_expr(me, &as_type->expr, type);
            // (tsc's checkSatisfiesExpression relates the two right away, and makes the message of TS1360 there: what
            // printing the target resolves, the return type in flight of `const f = () => 1 satisfies typeof f`, is
            // circular then (TS7023, `() => any`). The diagnostic itself is the walk's)
            if (!(type->flags & TYF_ERROR) && !ty_is_related(me, ty_regular(me, checked), type, TY_RELATION_ASSIGNABLE)) {
                cstr printed = ty_to_cstr(me, type);
                cstr_drop(&printed);
            }
            if (!(type->flags & TYF_ERROR)) {
                // Its elaboration too, which reads the type of every property of the literal the target has
                // (elaborateElement): `get self() { return a }` meets `const a = {...} satisfies T` in flight (TS7022,
                // TS7023). What it reports is the walk's (expr.c)
                ty_satisfies_elaboration_reads(me, checked, type, &as_type->expr);
            }
            return type->flags & TYF_ERROR ? type : checked; // (tsc's checkSatisfiesExpressionWorker)
        }

        case EXPR_TERNARY:
        {
            ty_expr_type(me, &expr->ternary->condition);
            // In source order: union members print in the order their types were made
            ExprCtx branch = passed_on_context(ctx);
            // (Not in a const context for being in one: tsc's isConstContext passes through parentheses, array
            // literals, spreads and property assignments, not a conditional's branches, so `c ? "a" : ["b"]` in `as
            // const` is `"a" | string[]`. A const type parameter's contextual type still makes them one)
            branch.is_const = false;
            Ty const* yes = check_with(me, &expr->ternary->yes, branch);
            Ty const* no = check_with(me, &expr->ternary->no, branch);
            return union_of_two(me, yes, no);
        }

        case EXPR_CALL: return check_call(me, expr->call, expr->inner_span, ctx);

        case EXPR_FUNCTION: {
            // (Its body is checked after the file's statements, from now on if it is in the file being checked: tsc's
            // checkNodeDeferred. The walk does it where it meets it otherwise)
            if (my defer_checks && span_source(expr->function->full_span) == my checking_file && expr->function->has_body) {
                checker_defer_function_body(me, expr->function);
            }
            // Its first check that doesn't skip it decides its contextual signature for good (tsc's ContextChecked, set
            // whatever that check finds): without one then, it has none, `m(() => 1, (z) => () => 2)` with `zero: Z`
            // doesn't take Z's inference (the function itself) as its contextual signature while m is resolved. Its
            // return type only sees the context when it is asked for (ty_contextual_return_type_self()). Not one typed
            // by a JSDoc `@type`, which takes its parameter types from that (tsc's getParameterTypeOfTypeTag)
            bool has_context = (ctx.contextual || ctx.sig_contextual) && !ctx.sig_absent;
            Ty const* context = ctx.sig_contextual ? ctx.sig_contextual : ctx.contextual;
            Signature const* checked = ty_link_get(me, KEY_CONTEXT_CHECKED, expr->function);
            if (!checked && !function_impl_rare(expr->function)->jsdoc_signature_type &&
                (!has_context || !contextual_signature(me, ty_instantiate_contextual_type_for(me, context, true), expr->function, 0))
            ) {
                ty_link_set(me, KEY_CONTEXT_CHECKED, expr->function, &context_checked_none);
            }
            if (has_context) {
                // (A function checked without one still takes its `this` from it, see assign_contextual_signature_as())
                assign_contextual_signature_as(me, expr->function, context, nullptr, nullptr, ctx.raw_contextual);
                if (checked && checked != &context_checked_none) {
                    Signature const* sig = contextual_signature(me, ty_instantiate_contextual_type_for(me, context, true), expr->function, 0);
                    if (sig) set_contextual_return(me, expr->function, sig);
                }
                // Its return type is decided at its first check with a contextual signature, which may be now, while the
                // call it is an argument of infers (tsc's contextuallyCheckFunctionExpressionOrObjectLiteralMethod)
                // (Not again while it is being decided further up: tsc's ContextChecked flag is set by then, so a call
                // re-entered from its own body, `wrap()` returning `new Proxy(t, { get() { return wrap(...) } })`,
                // doesn't make it circular)
                if (!expr->function->ret_explicit &&
                    ty_function_assigned_contextual_signature(me, expr->function) && !ty_function_return_in_flight(me, expr->function)) {
                    if (my outer_inference) ty_argument_return_type(me, expr->function);
                    else ty_contextual_check_return_type(me, expr->function);
                }
            }
            // Then its parameters, each one's type and default (tsc's checkSignatureDeclaration at the end of its
            // first contextual check): a default that reads a variable in flight is circular there, `const pick =
            // (from: Item[] = children) => ...` in the loop that assigns `current = pick()` with `{ children } = current`
            if (!ty_link_get(me, KEY_SIGNATURE_DECL_CHECKED, expr->function)) {
                ty_link_set(me, KEY_SIGNATURE_DECL_CHECKED, expr->function, expr->function);
                check_function_parameters_first(me, expr->function);
            }
            return ty_function_type(me, expr->function);
        }

        case EXPR_OBJECT: return check_object(me, expr, ctx);
        case EXPR_ARRAY: return check_array(me, expr, ctx);
        case EXPR_CLASS_DECL:
            if (ctx.contextual) contextually_type_static_fields(me, expr->class_decl, ctx.contextual);
            return ty_type_of_symbol(me, expr->class_decl->symbol);

        case EXPR_REGEX: return ty_global_regexp_type(me);
        case EXPR_PRIVATE_NAME: return any_type(me);
        case EXPR_JSX_ELEMENT: case EXPR_JSX_FRAGMENT: return ty_jsx_element_type(me, expr);
        case EXPR_JSX_ATTRIBUTES: return check_jsx_attributes(me, expr, ctx);

        case EXPR_TEMPLATE: {
            if (!expr->template_->tag) return check_template(me, expr, ctx);
            Expr const* tag = expr->template_->tag;
            if (tag->tag == EXPR_GENERIC_PARAMS) tag = &tag->generic_params->expr; // call.c applies the type arguments
            return ty_tagged_template_return_type(me, ty_expr_type(me, tag), expr, ctx.raw_contextual);
        }

        case EXPR_SPREAD: {
            // The type of each spread element: of an array its elements, of anything else what iterating it gives
            Ty const* operand = ty_expr_type(me, expr->spread);
            if (ty_is_array(me, operand) || ty_is_tuple(operand)) return ty_rest_element_type(me, operand);
            return ty_iterated_type(me, ITERATION_SPREAD, operand, expr_outer_span(expr->spread), nullptr);
        }
    }
    impossible;
}

// tsc's isTemplateLiteralContextualType
static bool is_template_contextual(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (is_template_contextual(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    if (ty->flags & (TYF_STRING_LITERAL | TYF_TEMPLATE_LITERAL)) return true;
    if (ty->kind == TY_TYPE_PARAM || ty->kind == TY_INDEXED || ty->kind == TY_CONDITIONAL) {
        Ty const* constraint = ty_base_constraint(me, ty);
        if (!constraint) return false;
        TyList parts = constraint->kind == TY_UNION ? constraint->composite.types : (TyList){ .data = &constraint, .len = 1 };
        for (u32 i = 0; i < parts.len; i++) {
            if (parts.data[i]->flags & TYF_STRING_LIKE) return true;
        }
    }
    return false;
}

// tsc's checkTemplateExpression: a constant is a (fresh) string literal, in a const context, an element access or with a
// string literal / template contextual type a template literal type, else string
static Ty const* check_template(Checker* me, Expr const* expr, ExprCtx ctx) {
    TemplateExpr const* template = expr->template_;
    u32 len = (u32)vec_expr_size(&template->substitutions);
    Ty const* allowed = nullptr;
    Ty const** types = malloc(sizeof(Ty const*) * (len ? len : 1));
    panic_if(!types, "out of memory");
    for_each (i, vec_expr, &template->substitutions) {
        // (A substitution of a const context is in it too: tsc's isConstContext goes up through TemplateSpan)
        Ty const* type = ctx.is_const ? check_with(me, i, (ExprCtx){ .is_const = true }) : ty_expr_type(me, i);
        if (!allowed) {
            // tsc's templateConstraintType
            Ty const* parts[6] = {
                ty_intrinsic(me, TY_STRING), ty_intrinsic(me, TY_NUMBER), ty_intrinsic(me, TY_BIGINT),
                ty_intrinsic(me, TY_BOOLEAN), ty_intrinsic(me, TY_NULL), ty_intrinsic(me, TY_UNDEFINED),
            };
            allowed = ty_union(me, parts, 6, nullptr, (TyList){0});
        }
        types[i_index] = ty_is_related(me, type, allowed, TY_RELATION_ASSIGNABLE) ? ty_regular(me, type) : ty_intrinsic(me, TY_STRING);
    }
    Ty const* ret = nullptr;
    cstr evaluated = cstr_null;
    if (!template->is_tag && ty_evaluate_string(me, expr, &evaluated)) {
        ret = ty_fresh_literal(me, ty_string_literal(me, cstr_sv(&evaluated)));
        cstr_drop(&evaluated);
    } else if (ctx.is_const || ctx.template_context || (ctx.contextual && is_template_contextual(me, ctx.contextual))) {
        Atom const** texts = malloc(sizeof(Atom const*) * (len + 1));
        panic_if(!texts, "out of memory");
        for_each (i, ast_cstrs, &template->texts) texts[i_index] = checker_atom(me, cstr_sv(i));
        ret = ty_template_literal(me, texts, types, len);
        free(texts);
    } else {
        ret = ty_intrinsic(me, TY_STRING);
    }
    free(types);
    return ret;
}

// The type of the index of an element access: a template literal there is typed as a template (tsc's
// isTemplateLiteralContext)
Ty const* ty_element_index_type(Checker* me, Expr const* index) {
    if (index->tag == EXPR_TEMPLATE && !index->template_->tag) return check_with(me, index, (ExprCtx){ .template_context = true });
    return ty_expr_type(me, index);
}

// Whether an index signature keyed by `key` is among a type's own (tsc's getIndexInfoOfType)
static bool has_index_key(Members const* members, Ty const* key) {
    for (u32 i = 0; i < members->index_count; i++) {
        if (members->index[i].key == key) return true;
    }
    return false;
}

// tsc's hasNumericPropertyNames: the only index signature of the type is a number one. A union's index signatures are
// the keys every member has (tsc's getUnionIndexInfos)
static bool has_numeric_property_names(Checker* me, Ty const* type) {
    if (type->kind != TY_UNION) {
        Ty const* object = ty_apparent_type(me, type);
        if (!(object->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false; // (getIndexInfosOfType)
        Members const* members = ty_members(me, object);
        return members->index_count == 1 && (members->index[0].key->flags & TYF_NUMBER);
    }
    TyList types = type->composite.types;
    // (null and undefined have no index signatures, so neither does a union with them)
    for (u32 t = 0; t < types.len; t++) {
        if (!(ty_apparent_type(me, types.data[t])->flags & (TYF_OBJECT | TYF_INTERSECTION))) return false;
    }
    Members const* first = ty_members(me, ty_apparent_type(me, types.data[0]));
    u32 count = 0;
    bool numeric = false;
    for (u32 i = 0; i < first->index_count; i++) {
        Ty const* key = first->index[i].key;
        bool everywhere = true;
        for (u32 t = 1; t < types.len && everywhere; t++) {
            everywhere = has_index_key(ty_members(me, ty_apparent_type(me, types.data[t])), key);
        }
        if (!everywhere) continue;
        count++;
        numeric = key->flags & TYF_NUMBER;
    }
    return count == 1 && numeric;
}

// tsc's isForInVariableForNumericPropertyNames: `k` of `for (const k in xs)` where xs has only a number index
// signature (an array) indexes as a number, in the loop's body
static bool is_for_in_numeric_variable(Checker* me, PropertyExpr const* access) {
    ForEachLoopStatement const* loop = property_for_in_loop(access);
    // (Of the iterable's type as it is, null and undefined included: tsc's getTypeOfExpression)
    return loop && has_numeric_property_names(me, ty_expr_type(me, &loop->iterable));
}

// (Out of line: the memo hit below is most calls, ~7 per access, and without the rest its frame is a few instructions:
// vscode's ty_access_key_type() was 1.3% of its instructions, a third of them the prologue and epilogue)
[[gnu::noinline]] static Ty const* access_key_type_slow(Checker* me, PropertyExpr const* access);

Ty const* ty_access_key_type(Checker* me, PropertyExpr const* access) {
    // (A slot is a dot access's, whose memo is set only for a name that is neither private nor `#`: below)
    if (access->name_slot) {
        LazyType memo = { .tag = LAZY_TYPE_EVALUATED, .slot = access->name_slot };
        LazyMemo const* state = lazy_type_state_peek(&memo);
        if (state && state->done) return lazy_memo_evaluated(state);
    }
    return access_key_type_slow(me, access);
}

static Ty const* access_key_type_slow(Checker* me, PropertyExpr const* access) {
    if (property_private_class(access) && property_private_class(access)->symbol) {
        return ty_prop_name_literal(me, ty_private_name(me, property_private_class(access)->symbol, span_sv(property_name(access))));
    }
    if (access->form == PROPERTY_EXPR_DOT) {
        // (A `#x` no class around declares names nothing, not even what an index signature has)
        Span name = property_name(access);
        // (A lone `#` is TS1127's, and the members named so are too)
        if (span_valid(name) && name.size > 1 && name.buf[0] == '#') {
            return ty_prop_name_literal(me, ty_private_name(me, nullptr, span_sv(name)));
        }
        // (Memoized: the string's atom and literal are lookups by content. Synthetic accesses have no slot)
        LazyType memo = { .tag = LAZY_TYPE_EVALUATED, .slot = access->name_slot };
        LazyMemo const* state = memo.slot ? lazy_type_state_peek(&memo) : nullptr;
        if (state && state->done) return lazy_memo_evaluated(state);
        Ty const* ret = ty_fresh_literal(me, ty_string_literal(me, span_sv(name)));
        if (memo.slot) lazy_type_set(&memo, ret, nullptr);
        return ret;
    }
    if (is_for_in_numeric_variable(me, access)) return ty_intrinsic(me, TY_NUMBER);
    return ty_element_index_type(me, &access->index);
}

// What the LazyType of an expression evaluates to: nobody checked it with a contextual type first
Ty const* ty_compute_expr_without_context(Checker* me, Expr const* expr) {
    return compute(me, expr, (ExprCtx){0});
}

// The initializer of a pattern typed again for its rest element (tsc's CheckMode.RestBindingElement, whose contextual
// type skips binding patterns): a reference whose type variable the pattern's contextual type substituted by its
// constraint stays generic, so the rest of a `T` is `Omit<T, ...>`. Null if that's the type it has anyway
Ty const* ty_rest_binding_initializer_type(Checker* me, Expr const* expr) {
    if (expr->tag != EXPR_IDENTIFIER && expr->tag != EXPR_PROPERTY) return nullptr;
    (void)ty_expr_type(me, expr); // (A property access says whether it depends on the context once computed)
    if (!ty_reference_uses_context(me, expr)) return nullptr;
    return compute(me, expr, (ExprCtx){0});
}
