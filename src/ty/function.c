#include "ty/ty_internal.h"
#include "program.h"
#include "source_file.h"
#include "statement.h"
#include "compiler_options.h"

// The types of functions: declarations, function expressions, methods, accessors and constructors. A function's type is
// an anonymous object type with one signature that is read straight off the FunctionImpl (members.c); what's here is
// its identity and its return type.

static LowerCtx const function_lower_ctx = { .generic = true };

// Whether a function's type mentions no type parameters but its own signature's, so instantiating it is the identity
// (tsc's getOuterTypeParameters is empty): no type parameter list or class encloses it, nor a function expression typed
// by a generic contextual signature, whose type parameters that function takes (assignContextualParameterTypes):
// `(c) => run(() => cw(c))` for `<CF>(c: CF) => ReturnType<CF>`
bool ty_function_closed(Checker* me, FunctionImpl const* function) {
    if (!function->no_outer_type_params && !function->no_enclosing_type_params) return false;
    for (FunctionImpl const* outer = function->contextual_ancestor; outer; outer = outer->contextual_ancestor) {
        Signature const* contextual = ty_function_assigned_contextual_signature(me, outer);
        if (contextual && contextual->type_params.len) return false;
    }
    return true;
}

Ty const* ty_function_type(Checker* me, FunctionImpl const* function) {
    Ty const* existing = ty_link_get(me, KEY_FUNCTION, function);
    if (existing) return existing;
    // A function body can mention every type parameter in scope, if there are any. Where none encloses it (nor a
    // class), instantiating its type is the identity (tsc's getObjectTypeInstantiation: getOuterTypeParameters is
    // empty): an object literal with methods inferred for a type parameter isn't copied by every instantiation that
    // reads it back (the copies relate structurally, asking for the methods' return types too early)
    bool closed = ty_function_closed(me, function);
    Ty* ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT | (closed ? 0 : TYF_COULD_CONTAIN_TYPE_VARIABLES));
    ret->anonymous.function = function;
    ret->anonymous.ctx = function_lower_ctx;
    // (An object literal's method too: a MethodDeclaration, whose parameters tsc's strictVariance compares bivariantly;
    // FunctionImpl.is_method is only a class's)
    ret->anonymous.ctx.method = function->is_method ||
        (!function->accessor && !function->is_constructor && span_valid(function_impl_member_name(function)));
    ret->anonymous.free_params_unknown = !closed;
    ret->anonymous.state = arena_new(&my arena, TyObjectState);
    ty_link_set(me, KEY_FUNCTION, function, ret);
    return ret;
}

typedef struct FunctionReturn {
    LazyState state;
    bool circular; // Asked for while being inferred
    bool reevaluating; // Asked for again from below a call's barrier, see function_return_type()
    // An async function's body returns no value and there's no Promise value (TS2705), not said yet: only a request
    // for the type that tsc would make says it (see ty_function_return_circular())
    bool promise_missing;
    Ty const* type;
} FunctionReturn;

// infer_return_type() found the Promise constructor missing (FunctionReturn.promise_missing)
static thread_local bool inferred_promise_missing;
// The return type is asked for by ty_function_return_circular(), which tsc wouldn't do: no TS2705 from it
static thread_local bool return_type_speculative;

static void report_promise_missing(Checker* me, FunctionImpl const* function, FunctionReturn* memo) {
    if (!memo->promise_missing || return_type_speculative) return;
    memo->promise_missing = false;
    ty_error_at(me, span_null, 2468, cstr_lit("Cannot find global value 'Promise'."));
    ty_error_at(me, function_error_span(function), 2705, cstr_lit("An async function or method in ES5 requires the "
        "'Promise' constructor.  Make sure you have a declaration for the 'Promise' constructor or include 'ES2015' in "
        "your '--lib' option."));
}

Ty const* ty_promise_of(Checker* me, FunctionImpl const* function, Ty const* type) {
    if (!function_impl_rare(function)->promise_decl) return ty_intrinsic(me, TY_ANY);
    Ty const* promise = ty_declared_type_of(me, function_impl_rare(function)->promise_decl);
    if (promise->kind != TY_DECLARED || promise->declared.type_params.len != 1) return ty_intrinsic(me, TY_ANY);
    return ty_reference(me, promise, (TyList){ .data = &type, .len = 1 });
}

// tsc's getReturnTypeFromBody for a generator: Generator<yielded, returned, what the context sends>
static Ty const* infer_generator_type(Checker* me, FunctionImpl const* function, TyBuf* returned) {
    bool async = function->is_async;
    Expr const* const* yield_exprs = function_impl_yields(function);
    u32 yield_count = function_impl_yield_count(function);
    TyIterationTypes const* context = ty_generator_context(me, function);
    TyBuf yields = {0};
    TyBuf nexts = {0};
    for (u32 i = 0; i < yield_count; i++) {
        UnaryOpExpr const* yield = yield_exprs[i]->unary_op;
        ty_expr_type(me, yield_exprs[i]); // Checks the operand with the contextual type it gets from there
        if (yield->op == UNARY_OP_YIELD) {
            Ty const* sent = intern_get(&my links, (u64[]){ KEY_YIELD_CONTEXT, (u64)(uintptr_t)yield }, 2);
            // (Not checked with its context yet: the generator's type is asked before its body is checked, a function
            // expression called right away)
            if (!sent && yield->declared_context) sent = ty_from_node(me, yield->declared_context, (LowerCtx){ .generic = true });
            if (sent) ty_buf_push(&nexts, sent);
        } else if (yield->expr.tag == EXPR_IMPLICIT_ANY) {
            // `yield*` missing its operand delegates to the error type, whose iteration types are any
            ty_buf_push(&nexts, ty_intrinsic(me, TY_ANY));
        } else {
            TyIterationTypes const* delegate = ty_iteration_types_of_iterable(me, ty_expr_type(me, &yield->expr),
                async ? ITERATION_ASYNC_YIELD_STAR : ITERATION_YIELD_STAR);
            if (delegate && delegate->next_type) ty_buf_push(&nexts, delegate->next_type);
        }
        Ty const* type;
        if (yield->expr.tag == EXPR_IMPLICIT_ANY && yield->op == UNARY_OP_YIELD_STAR) {
            type = ty_intrinsic(me, TY_ANY); // (`yield*` missing its operand delegates to the error type)
        } else if (yield->expr.tag == EXPR_IMPLICIT_ANY) {
            type = ty_intrinsic(me, TY_UNDEFINED_WIDENING); // A bare `yield`
        } else if (yield->op == UNARY_OP_YIELD_STAR) {
            u32 use = async ? ITERATION_ASYNC_YIELD_STAR : ITERATION_YIELD_STAR;
            Ty const* delegate = ty_expr_type(me, &yield->expr);
            // (tsgo reports a delegate that isn't iterable from here, deferred: printing it can't find this very return
            // type in progress, getIterationTypesOfIterableWorker's addDeferredDiagnostic)
            type = ty_iterated_type(me, use, delegate, span_null, nullptr);
        } else {
            type = ty_expr_type(me, &yield->expr);
            // In a const context (yielding to a `const` type parameter) it keeps its literals (tsc 6.0's
            // checkAndAggregateYieldOperandTypes)
            if (ty_is_const_context_kind(&yield->expr)) {
                Ty const* yield_context = ty_generator_yield_context(me, function);
                if (yield_context && ty_is_const_type_variable(me, yield_context, 0)) type = ty_regular(me, type);
            }
        }
        if (async) type = ty_awaited(me, type);
        ty_buf_push(&yields, type);
    }
    Ty const* yield_type = yields.len ? ty_union_subtype_reduced(me, yields.data, yields.len) : nullptr;
    ty_buf_drop(&yields);

    // Returns as for other functions (checkAndAggregateReturnExpressionTypes), without them `void`
    bool implicit_return = ty_function_end_reachability(me, function) == FLOW_REACH_YES;
    Ty const* return_type = nullptr;
    Ty const* fallback = ty_intrinsic(me, TY_VOID);
    if (!returned->len && !function_impl_has_bare_return(function) && !implicit_return && function->is_expression) {
        fallback = ty_intrinsic(me, TY_NEVER);
    } else if (returned->len) {
        if (compiler_options()->strict_null_checks && (function_impl_has_bare_return(function) || implicit_return)) {
            ty_buf_push(returned, ty_intrinsic(me, TY_UNDEFINED));
        }
        return_type = ty_union_subtype_reduced(me, returned->data, returned->len);
    }
    if (yield_type && !compiler_options()->strict_null_checks && (yield_type->xflags & TYX_CONTAINS_WIDENING)) {
        // (tsc's reportErrorsFromWidening with WideningKind.GeneratorYield)
        *intern_slot(me, &my links, (u64[]){ KEY_YIELD_WIDENED_ANY, (u64)(uintptr_t)function }, 2) = yield_type;
    }
    if (return_type && !compiler_options()->strict_null_checks && (return_type->xflags & TYX_CONTAINS_WIDENING)) {
        // (WideningKind.FunctionReturn, after the yields: see ty_generator_return_before_widening())
        *intern_slot(me, &my links, (u64[]){ KEY_RETURN_WIDENED_ANY, (u64)(uintptr_t)function }, 2) = return_type;
    }
    if (yield_type) yield_type = ty_widened_return_type(me, yield_type, context ? context->yield_type : nullptr);
    if (return_type) return_type = ty_widened_return_type(me, return_type, context ? context->return_type : nullptr);
    // (tsc: the intersection of what the yields are expected to produce, widened, else the context's)
    Ty const* next_type = nexts.len ? ty_widened_for_declaration(me, ty_intersection(me, nexts.data, nexts.len, nullptr, (TyList){0}), false) :
        context ? context->next_type : ty_intrinsic(me, TY_UNKNOWN_);
    ty_buf_drop(&nexts);
    return ty_generator_type(me, async, yield_type ? yield_type : ty_intrinsic(me, TY_NEVER),
        return_type ? return_type : fallback, next_type);
}

// What the contextual return type of an async function says about the values it returns: the promised types of its
// members that are promises (tsc filters them in getContextualReturnType, then takes getPromisedTypeOfPromise)
static Ty const* promised_context(Checker* me, Ty const* contextual) {
    TyList members = contextual->kind == TY_UNION ? contextual->composite.types : (TyList){ .data = &contextual, .len = 1 };
    TyBuf promised = {0};
    for (u32 i = 0; i < members.len; i++) {
        Ty const* awaited = ty_awaited(me, members.data[i]);
        if (awaited != members.data[i]) ty_buf_push(&promised, awaited);
    }
    Ty const* ret = promised.len ? ty_union(me, promised.data, promised.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&promised);
    return ret;
}

// The contextual type of an async function's return expressions, before the PromiseLike: the members of the contextual
// return type that are promises (or could be: any, unknown, void, type variables) awaited (tsc's
// getContextualReturnType filter + getContextualTypeForReturnExpression's getAwaitedTypeNoAlias). Null if none
Ty const* ty_async_contextual_awaited(Checker* me, Ty const* contextual) {
    TyList members = contextual->kind == TY_UNION ? contextual->composite.types : (TyList){ .data = &contextual, .len = 1 };
    TyBuf kept = {0};
    for (u32 i = 0; i < members.len; i++) {
        Ty const* member = members.data[i];
        bool keep = (member->flags & (TYF_ANY | TYF_UNKNOWN | TYF_VOID)) || member->kind == TY_TYPE_PARAM ||
            member->kind == TY_INDEXED || member->kind == TY_CONDITIONAL || member->kind == TY_SUBSTITUTION ||
            ty_awaited(me, member) != member;
        if (keep) ty_buf_push(&kept, ty_awaited_no_alias(me, member));
    }
    Ty const* ret = kept.len ? ty_union(me, kept.data, kept.len, nullptr, (TyList){0}) : nullptr;
    ty_buf_drop(&kept);
    return ret;
}

// What an async function's return expressions are contextually typed by, given its contextual return type: the
// awaited type or a PromiseLike of it (tsc's getContextualTypeForReturnExpression), null if none
Ty const* ty_async_return_expressions_context(Checker* me, Ty const* contextual) {
    Ty const* awaited = contextual ? ty_async_contextual_awaited(me, contextual) : nullptr;
    if (!awaited || (awaited->flags & TYF_ANY)) return nullptr;
    Ty const* both[2] = { awaited, ty_promise_like_of(me, awaited) };
    return both[1] ? ty_union(me, both, 2, nullptr, (TyList){0}) : awaited;
}

Ty const* ty_element_before_widening(Checker* me, Binding const* element) {
    return intern_get(&my links, (u64[]){ KEY_ELEMENT_WIDENED_ANY, (u64)(uintptr_t)element }, 2);
}

Ty const* ty_generator_return_before_widening(Checker* me, FunctionImpl const* function) {
    ty_function_return_type(me, function);
    return intern_get(&my links, (u64[]){ KEY_RETURN_WIDENED_ANY, (u64)(uintptr_t)function }, 2);
}

Ty const* ty_function_return_before_widening(Checker* me, FunctionImpl const* function) {
    ty_function_return_type(me, function);
    return intern_get(&my links, (u64[]){ function->is_generator ? KEY_YIELD_WIDENED_ANY : KEY_RETURN_WIDENED_ANY,
        (u64)(uintptr_t)function }, 2);
}

// tsc's checkAndAggregateReturnExpressionTypes: a bare call to the function itself (through a constant reference, for
// a function expression) doesn't contribute to its return type
bool ty_is_recursive_return(Checker* me, Expr const* returned) {
    FunctionImpl const* function = my return_inference_of;
    // (Only return statements: tsc's forEachReturnStatement, an expression body is checked like any expression, `const
    // f = () => f()` is circular, TS7023)
    if (!function || function->expression_body) return false;
    Expr const* expr = returned;
    if (function->is_async && expr->tag == EXPR_UNARY_OP && expr->unary_op->op == UNARY_OP_AWAIT) expr = &expr->unary_op->expr;
    if (expr->tag != EXPR_CALL || expr->call->uses_new || expr->call->optional_chain) return false;
    Expr const* callee = &expr->call->callable;
    // (With type arguments too: tsc's call keeps them apart from its identifier callee, `return reduce<T>(t)`)
    if (callee->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(callee)) callee = &callee->generic_params->expr;
    if (callee->tag != EXPR_IDENTIFIER || callee->identifier.keyword || !callee->identifier.target) return false;
    Symbol const* symbol = callee->identifier.target;
    // (tsc's isConstantReference: a const, or a local `let` never assigned again. Parameters count too, left out)
    // (Or the name of a function expression, valueDeclaration a FunctionExpression: `function f() { return f(); }`)
    // (Not an exported variable: tsc resolves the name to its export symbol, which isConstantVariable doesn't take,
    // `export const g = () => { ...; return g(); }` is TS7023)
    if (function->is_expression && (symbol->exported || (!symbol->is_const &&
        !(symbol->is_local && !symbol_assignments(symbol)->is_assigned && (symbol->flags & SYMBOL_BLOCK_SCOPED_VARIABLE)))) &&
        !(function->symbol && symbol == function->symbol)
    ) {
        return false;
    }
    Ty const* type = ty_expr_type(me, callee);
    bool self = (function->symbol && symbol == function->symbol) ||
        (type->kind == TY_ANONYMOUS && type->anonymous.function == function);
    if (self) my return_inference_skipped = true;
    return self;
}

static Ty const* infer_return_type(Checker* me, FunctionImpl const* function) {
    // (Before the return expressions, like tsc's checkAndAggregateReturnExpressionTypes asks functionHasImplicitReturn
    // first: an exhaustiveness check makes its case expressions' literal types, which then come first in unions)
    FlowReachability end = function->is_generator ? FLOW_REACH_NO : ty_function_end_reachability(me, function);
    Ty const* outer_context = function_impl_enter_return_context(function);
    TyBuf returned = {0};
    FunctionImpl const* outer_inference_of = my return_inference_of;
    bool outer_skipped = my return_inference_skipped;
    my return_inference_of = function;
    my return_inference_skipped = false;
    block_collect_return_types(function_impl_body(function), &returned);
    bool skipped = my return_inference_skipped;
    my return_inference_of = outer_inference_of;
    my return_inference_skipped = outer_skipped;
    my return_context = outer_context;

    Ty const* ret;
    if (function->is_generator) {
        ret = infer_generator_type(me, function, &returned);
    } else {
        // tsc: checkAndAggregateReturnExpressionTypes(). Only what is certain about the end of the body counts until
        // reachability knows about `never` calls and exhaustive switches
        bool returns_nothing_somewhere = function_impl_has_bare_return(function) || end == FLOW_REACH_YES;
        if (!returned.len && !function_impl_has_bare_return(function) && end == FLOW_REACH_NO && (function->is_expression || skipped) &&
            !function->accessor
        ) {
            // Function expressions that can't return don't (declarations are `void`, accessors too: tsc's mayReturnNever)
            ret = ty_intrinsic(me, TY_NEVER);
        } else if (!returned.len) {
            // `undefined` if the context allows it (tsc's getReturnTypeFromBody: some member of the unwrapped contextual
            // return type is undefined, TS 6.0; an async function's is what it awaits to). Not instantiated by what a
            // call has inferred so far: `xs.map(x => {})` returns `void` whatever the map's context
            Ty const* contextual = ty_contextual_return_type(me, function);
            if (contextual && function->is_async) contextual = ty_awaited_no_alias(me, contextual);
            bool undefined = false;
            if (contextual && contextual->kind == TY_UNION) {
                for (u32 i = 0; i < contextual->composite.types.len && !undefined; i++) {
                    undefined = contextual->composite.types.data[i]->flags & TYF_UNDEFINED;
                }
            } else if (contextual) {
                undefined = contextual->flags & TYF_UNDEFINED;
            }
            ret = ty_intrinsic(me, undefined ? TY_UNDEFINED : TY_VOID);
        } else {
            // An async function returns what its return expressions await to, and the context is about that too
            // (tsc's getPromisedTypeOfPromise of the contextual return type)
            ContextSelf self = CONTEXT_OTHER;
            Ty const* contextual = ty_instantiate_contextual_type(me, ty_contextual_return_type_self(me, function, &self));
            // (What TS7024 asks: the return type was inferred with the function's own signature as its context, tsc's
            // getReturnTypeFromBody reporting widening then)
            if (self != CONTEXT_OTHER) *intern_slot(me, &my links, (u64[]){ KEY_CONTEXT_RETURN_SELF, (u64)(uintptr_t)function }, 2) = function;
            if (function->is_async) {
                for (u32 i = 0; i < returned.len; i++) returned.data[i] = ty_unwrap_awaited(me, ty_awaited_no_alias(me, returned.data[i]));
                contextual = contextual ? promised_context(me, contextual) : nullptr;
            }
            // (Each type once, like tsc's AppendIfUnique: a type returned twice is that type, the union of its two copies
            // being the error type when it's an unresolved name's, babel-parser's `N.EnumBody` of a missing module)
            u32 unique = 0;
            for (u32 i = 0; i < returned.len; i++) {
                bool seen = false;
                for (u32 j = 0; j < unique && !seen; j++) seen = returned.data[j] == returned.data[i];
                if (!seen) returned.data[unique++] = returned.data[i];
            }
            returned.len = unique;
            if (compiler_options()->strict_null_checks && returns_nothing_somewhere) {
                bool seen = false;
                for (u32 j = 0; j < returned.len && !seen; j++) seen = returned.data[j] == ty_intrinsic(me, TY_UNDEFINED);
                if (!seen) ty_buf_push(&returned, ty_intrinsic(me, TY_UNDEFINED));
            }
            ret = ty_union_subtype_reduced(me, returned.data, returned.len);
            if (!compiler_options()->strict_null_checks && (ret->xflags & TYX_CONTAINS_WIDENING)) {
                // Its null and undefined widen to any below (tsc's reportErrorsFromWidening with WideningKind.FunctionReturn)
                *intern_slot(me, &my links, (u64[]){ KEY_RETURN_WIDENED_ANY, (u64)(uintptr_t)function }, 2) = ret;
            }
            // (Its context is its own type: see set_contextual_return() in expr.c)
            if (!contextual && self == CONTEXT_OWN) {
                contextual = ret;
            }
            if (!(ret->flags & TYF_NEVER)) ret = ty_widened_return_type(me, ret, contextual);
        }
    }
    ty_function_decide_widening_reportable(me, function);
    if (function->is_async && !function->is_generator && !returned.len && function_impl_rare(function)->promise_decl &&
        !program_get_global(my program, c_sv("Promise"), SYMBOL_VALUE)
    ) {
        // tsc's createPromiseReturnType, which only the bodies that return no value get to: no Promise to make one
        // with, said when the return type is first needed (like tsc: a function nobody asks about isn't reported)
        inferred_promise_missing = true;
    }
    ty_buf_drop(&returned);
    if (function->is_async && !function->is_generator) ret = ty_promise_of(me, function, ret);
    return ret;
}

Ty const* ty_class_instance_type(Checker* me, Symbol const* class_symbol) {
    Ty const* declared = ty_declared_type_of(me, class_symbol);
    if (declared->kind != TY_DECLARED || !declared->declared.type_params.len) return declared;
    return ty_reference(me, declared, declared->declared.type_params);
}

bool ty_function_return_in_flight(Checker* me, FunctionImpl const* function) {
    if (function_impl_rare(function)->constructed_class || function->ret_explicit || !function->has_body) return false;
    FunctionReturn const* memo = ty_link_get(me, KEY_FUNCTION_RETURN, function);
    return memo && memo->state == LAZY_STATE_IN_FLIGHT;
}

static Ty const* function_return_type(Checker* me, FunctionImpl const* function, bool keep_outer);

// The inferred return type if it is known already (tsc's signature.resolvedReturnType), else null
Ty const* ty_function_return_known(Checker* me, FunctionImpl const* function) {
    if (function_impl_rare(function)->constructed_class || function->ret_explicit || !function->has_body) return nullptr;
    FunctionReturn const* memo = ty_link_get(me, KEY_FUNCTION_RETURN, function);
    return memo && memo->state == LAZY_STATE_DONE ? memo->type : nullptr;
}

// The inferred return type depended on itself, so it's any (tsc's getReturnTypeOfSignature failing popTypeResolution)
bool ty_function_return_circular(Checker* me, FunctionImpl const* function) {
    if (function_impl_rare(function)->constructed_class || function->ret_explicit || !function->has_body || function_impl_setter_type(function)) return false;
    bool outer = return_type_speculative;
    return_type_speculative = true;
    ty_function_return_type(me, function);
    return_type_speculative = outer;
    FunctionReturn const* memo = ty_link_get(me, KEY_FUNCTION_RETURN, function);
    return memo && memo->circular;
}

Ty const* ty_function_return_type(Checker* me, FunctionImpl const* function) {
    return function_return_type(me, function, false);
}

Ty const* ty_argument_return_type(Checker* me, FunctionImpl const* function) {
    my argument_return_depth++;
    Ty const* ret = function_return_type(me, function, true);
    my argument_return_depth--;
    return ret;
}

Ty const* ty_iife_return_type(Checker* me, FunctionImpl const* function) {
    return function_return_type(me, function, true);
}

static Ty const* function_return_type(Checker* me, FunctionImpl const* function, bool keep_outer) {
    if (function_impl_rare(function)->constructed_class) return ty_class_instance_type(me, function_impl_rare(function)->constructed_class);
    // (A getter without an annotation is its setter's parameter annotation, body or not: tsc's getTypeOfAccessors)
    if (!function->ret_explicit && function_impl_setter_type(function)) return ty_from_node(me, function_impl_setter_type(function), function_lower_ctx);
    // (A getter whose body the parser lost has an empty one, which tsc's getTypeOfAccessors infers from: void. A
    // signature's return type is any then, getReturnTypeOfSignature checks NodeIsMissing)
    bool body = function->has_body || (function->body_missing && function->accessor == 1);
    if (function->ret_explicit || !body) return ty_from_node(me, &function->ret, function_lower_ctx);
    if (function_impl_rare(function)->jsdoc_signature_type) {
        Ty const* tagged = ty_jsdoc_tag_return_type(me, function);
        if (tagged) return tagged;
    }
    FunctionReturn* memo = (FunctionReturn*)ty_link_get(me, KEY_FUNCTION_RETURN, function);
    if (!memo) {
        memo = arena_new(&my arena, FunctionReturn);
        ty_link_set(me, KEY_FUNCTION_RETURN, function, memo);
    }
    switch (memo->state) {
        case LAZY_STATE_DONE:
            report_promise_missing(me, function, memo);
            return memo->type;
        case LAZY_STATE_IN_FLIGHT:
            // Asked again from below a call's barrier, it isn't circular yet: it's inferred again, once, and the inner
            // result is the one remembered (tsc's getReturnTypeOfSignature, like declarations: lazy_type_evaluate_inner())
            if (!memo->reevaluating && !ty_resolving_find(me, RESOLVING_FUNCTION_RETURN, function)) {
                memo->reevaluating = true;
                break;
            }
            ty_resolving_cycle(me, RESOLVING_FUNCTION_RETURN, function);
            return ty_intrinsic(me, TY_CIRCULAR);
        case LAZY_STATE_NOT_STARTED: break;
    }
    memo->state = LAZY_STATE_IN_FLIGHT;
    ty_resolving_push(me, RESOLVING_FUNCTION_RETURN, function);
    FlowIsolation isolation = ty_flow_isolate(me);
    // (An object literal getter a call's resolution types: nothing in its body is remembered, see
    // ty_object_literal_accessor_type())
    if (my lazy_getter == function) my flow_uncached = isolation.uncached;
    // Unless the function is an argument of the call inferring now, nothing in its body is an argument of that call
    struct OuterInference* outer = my outer_inference;
    if (!keep_outer) my outer_inference = nullptr;
    inferred_promise_missing = false;
    Ty const* type = infer_return_type(me, function);
    bool promise_missing = inferred_promise_missing;
    inferred_promise_missing = false;
    if (memo->state != LAZY_STATE_DONE) {
        memo->type = type;
        memo->promise_missing = promise_missing;
    }
    if (ty_resolving_pop(me)) memo->circular = true;
    // A return type that depends on itself is any, whatever else the body says (tsc: popTypeResolution() failing)
    if (memo->circular) memo->type = ty_intrinsic(me, TY_ANY);
    my outer_inference = outer;
    ty_flow_restore(me, isolation);
    memo->state = LAZY_STATE_DONE;
    report_promise_missing(me, function, memo);
    return memo->type;
}

bool ty_extends_array(Checker* me, Ty const* ty) {
    // (tsc's isArrayLikeType: null and undefined themselves aren't, even where they're assignable to anything)
    if (ty->kind == TY_INTRINSIC && (ty->flags & TYF_NULLABLE)) return false;
    return ty_is_related(me, ty, ty_array(me, ty_intrinsic(me, TY_ANY), true), TY_RELATION_ASSIGNABLE);
}
