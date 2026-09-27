#include "ty/ty_internal.h"
#include "program.h"
#include "statement.h"
#include "compiler_options.h"

// The types of functions: declarations, function expressions, methods, accessors and constructors. A function's type is
// an anonymous object type with one signature that is read straight off the FunctionImpl (members.c); what's here is
// its identity and its return type.

static LowerCtx const function_lower_ctx = { .generic = true };

Ty const* ty_function_type(Checker* me, FunctionImpl const* function) {
    u64 key[2] = { KEY_FUNCTION, (u64)(uintptr_t)function };
    Ty const* existing = intern_get(&my links, key, 2);
    if (existing) return existing;
    // A function body can mention every type parameter in scope
    Ty* ret = ty_alloc(me, TY_ANONYMOUS, TYF_OBJECT | TYF_COULD_CONTAIN_TYPE_VARIABLES);
    ret->anonymous.function = function;
    ret->anonymous.ctx = function_lower_ctx;
    ret->anonymous.ctx.method = function->is_method;
    ret->anonymous.free_params_unknown = true;
    ret->anonymous.state = arena_new(&my arena, TyObjectState);
    *intern_slot(me, &my links, key, 2) = ret;
    return ret;
}

typedef struct FunctionReturn {
    LazyState state;
    bool circular; // Asked for while being inferred
    Ty const* type;
} FunctionReturn;

static Ty const* promise_of(Checker* me, FunctionImpl const* function, Ty const* type) {
    if (!function->promise_decl) return ty_intrinsic(me, TY_ANY);
    Ty const* promise = ty_declared_type_of(me, function->promise_decl);
    if (promise->kind != TY_DECLARED || promise->declared.type_params.len != 1) return ty_intrinsic(me, TY_ANY);
    return ty_reference(me, promise, (TyList){ .data = &type, .len = 1 });
}

// tsc's getReturnTypeFromBody for a generator: Generator<yielded, returned, what the context sends>
static Ty const* infer_generator_type(Checker* me, FunctionImpl const* function, TyBuf* returned) {
    bool async = function->is_async;
    TyIterationTypes const* context = ty_generator_context(me, function);
    TyBuf yields = {0};
    for (u32 i = 0; i < function->yield_count; i++) {
        UnaryOpExpr const* yield = function->yields[i]->unary_op;
        ty_expr_type(me, function->yields[i]); // Checks the operand with the contextual type it gets from there
        Ty const* type;
        if (yield->expr.tag == EXPR_IMPLICIT_ANY) {
            type = ty_intrinsic(me, TY_UNDEFINED); // A bare `yield`
        } else if (yield->op == UNARY_OP_YIELD_STAR) {
            u32 use = async ? ITERATION_ASYNC_YIELD_STAR : ITERATION_YIELD_STAR;
            Ty const* delegate = ty_expr_type(me, &yield->expr);
            type = ty_iterated_type(me, use, delegate, span_null, nullptr);
            if (!ty_iteration_types_of_iterable(me, delegate, use)) {
                // tsc reports TS2488 from right here, and printing the type in the message can find this very return
                // type in progress (`yield* foo` inside foo makes foo's return type any). HACK: print it too
                cstr printed = ty_to_cstr(me, delegate);
                cstr_drop(&printed);
            }
        } else {
            type = ty_expr_type(me, &yield->expr);
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
    if (!returned->len && !function->has_bare_return && !implicit_return && function->is_expression) {
        fallback = ty_intrinsic(me, TY_NEVER);
    } else if (returned->len) {
        if (compiler_options()->strict_null_checks && (function->has_bare_return || implicit_return)) {
            ty_buf_push(returned, ty_intrinsic(me, TY_UNDEFINED));
        }
        return_type = ty_union_subtype_reduced(me, returned->data, returned->len);
    }
    if (yield_type) yield_type = ty_widened_return_type(me, yield_type, context ? context->yield_type : nullptr);
    if (return_type) return_type = ty_widened_return_type(me, return_type, context ? context->return_type : nullptr);
    Ty const* next_type = context ? context->next_type : ty_intrinsic(me, TY_UNKNOWN_);
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

static Ty const* infer_return_type(Checker* me, FunctionImpl const* function) {
    Ty const* outer_context = function_impl_enter_return_context(function);
    TyBuf returned = {0};
    block_collect_return_types(&function->body, &returned);
    my return_context = outer_context;

    Ty const* ret;
    if (function->is_generator) {
        ret = infer_generator_type(me, function, &returned);
    } else {
        // tsc: checkAndAggregateReturnExpressionTypes(). Only what is certain about the end of the body counts until
        // reachability knows about `never` calls and exhaustive switches
        FlowReachability end = ty_function_end_reachability(me, function);
        bool returns_nothing_somewhere = function->has_bare_return || end == FLOW_REACH_YES;
        if (!returned.len && !function->has_bare_return && end == FLOW_REACH_NO && function->is_expression) {
            // Function expressions that can't return don't (declarations are `void`)
            ret = ty_intrinsic(me, TY_NEVER);
        } else if (!returned.len) {
            // `undefined` if that's what the context wants (tsc's getReturnTypeFromBody, TS 5.1)
            Ty const* contextual = function->is_async ? nullptr : ty_contextual_return_type(me, function);
            bool undefined = contextual && contextual->kind != TY_UNION && (contextual->flags & TYF_UNDEFINED);
            ret = ty_intrinsic(me, undefined ? TY_UNDEFINED : TY_VOID);
        } else {
            // An async function returns what its return expressions await to, and the context is about that too
            // (tsc's getPromisedTypeOfPromise of the contextual return type)
            Ty const* contextual = ty_contextual_return_type(me, function);
            if (function->is_async) {
                for (u32 i = 0; i < returned.len; i++) returned.data[i] = ty_awaited(me, returned.data[i]);
                contextual = contextual ? promised_context(me, contextual) : nullptr;
            }
            if (compiler_options()->strict_null_checks && returns_nothing_somewhere) {
                ty_buf_push(&returned, ty_intrinsic(me, TY_UNDEFINED));
            }
            ret = ty_union_subtype_reduced(me, returned.data, returned.len);
            if (!(ret->flags & TYF_NEVER)) ret = ty_widened_return_type(me, ret, contextual);
        }
    }
    ty_buf_drop(&returned);
    if (function->is_async && !function->is_generator) ret = promise_of(me, function, ret);
    return ret;
}

Ty const* ty_class_instance_type(Checker* me, Symbol const* class_symbol) {
    Ty const* declared = ty_declared_type_of(me, class_symbol);
    if (declared->kind != TY_DECLARED || !declared->declared.type_params.len) return declared;
    return ty_reference(me, declared, declared->declared.type_params);
}

bool ty_function_return_in_flight(Checker* me, FunctionImpl const* function) {
    if (function->constructed_class || function->ret_explicit || !function->has_body) return false;
    u64 key[2] = { KEY_FUNCTION_RETURN, (u64)(uintptr_t)function };
    FunctionReturn const* memo = intern_get(&my links, key, 2);
    return memo && memo->state == LAZY_STATE_IN_FLIGHT;
}

static Ty const* function_return_type(Checker* me, FunctionImpl const* function, bool keep_outer);

Ty const* ty_function_return_type(Checker* me, FunctionImpl const* function) {
    return function_return_type(me, function, false);
}

Ty const* ty_argument_return_type(Checker* me, FunctionImpl const* function) {
    return function_return_type(me, function, true);
}

static Ty const* function_return_type(Checker* me, FunctionImpl const* function, bool keep_outer) {
    if (function->constructed_class) return ty_class_instance_type(me, function->constructed_class);
    if (function->ret_explicit || !function->has_body) return ty_from_node(me, &function->ret, function_lower_ctx);
    u64 key[2] = { KEY_FUNCTION_RETURN, (u64)(uintptr_t)function };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) *slot = arena_new(&my arena, FunctionReturn);
    FunctionReturn* memo = (FunctionReturn*)*slot;
    switch (memo->state) {
        case LAZY_STATE_DONE: return memo->type;
        case LAZY_STATE_IN_FLIGHT:
            memo->circular = true;
            return ty_intrinsic(me, TY_CIRCULAR); // TODO: TS7023 implicitly any, references itself
        case LAZY_STATE_NOT_STARTED: break;
    }
    memo->state = LAZY_STATE_IN_FLIGHT;
    FlowIsolation isolation = ty_flow_isolate(me);
    // Unless the function is an argument of the call inferring now, nothing in its body is an argument of that call
    struct OuterInference const* outer = my outer_inference;
    if (!keep_outer) my outer_inference = nullptr;
    memo->type = infer_return_type(me, function);
    // A return type that depends on itself is any, whatever else the body says (tsc: popTypeResolution() failing)
    if (memo->circular) memo->type = ty_intrinsic(me, TY_ANY);
    my outer_inference = outer;
    ty_flow_restore(me, isolation);
    memo->state = LAZY_STATE_DONE;
    return memo->type;
}

bool ty_extends_array(Checker* me, Ty const* ty) {
    return ty_is_related(me, ty, ty_array(me, ty_intrinsic(me, TY_ANY), true), TY_RELATION_ASSIGNABLE);
}
