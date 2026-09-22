#include "ty/ty_internal.h"
#include "program.h"
#include "statement.h"

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
    Ty const* type;
} FunctionReturn;

static Ty const* promise_of(Checker* me, FunctionImpl const* function, Ty const* type) {
    // TODO: should be Promise<Awaited<T>>, returning a promise from an async function gives a nested one here
    if (!function->promise_decl) return ty_intrinsic(me, TY_ANY);
    Ty const* promise = ty_declared_type_of(me, function->promise_decl);
    if (promise->kind != TY_DECLARED || promise->declared.type_params.len != 1) return ty_intrinsic(me, TY_ANY);
    return ty_reference(me, promise, (TyList){ .data = &type, .len = 1 });
}

static Ty const* infer_return_type(Checker* me, FunctionImpl const* function) {
    Ty const* outer_context = function_impl_enter_return_context(function);
    TyBuf returned = {0};
    block_collect_return_types(&function->body, &returned);
    my return_context = outer_context;

    Ty const* ret;
    if (function->is_generator) {
        // TODO: Generator<TYield, TReturn, TNext>/AsyncGenerator<...> inferred from the yields and returns
        ret = ty_intrinsic(me, TY_ANY);
    } else {
        // tsc: checkAndAggregateReturnExpressionTypes(). Only what is certain about the end of the body counts until
        // reachability knows about `never` calls and exhaustive switches
        FlowReachability end = ty_function_end_reachability(me, function);
        bool returns_nothing_somewhere = function->has_bare_return || end == FLOW_REACH_YES;
        if (!returned.len && !function->has_bare_return && end == FLOW_REACH_NO && function->is_expression) {
            // Function expressions that can't return don't (declarations are `void`)
            ret = ty_intrinsic(me, TY_NEVER);
        } else if (!returned.len) {
            ret = ty_intrinsic(me, TY_VOID);
        } else {
            if (compiler_options()->strict_null_checks && returns_nothing_somewhere) {
                ty_buf_push(&returned, ty_intrinsic(me, TY_UNDEFINED));
            }
            ret = ty_union_subtype_reduced(me, returned.data, returned.len);
            if (!(ret->flags & TYF_NEVER)) {
                ret = ty_widened_return_type(me, ret, ty_contextual_return_type(me, function));
            }
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

Ty const* ty_function_return_type(Checker* me, FunctionImpl const* function) {
    if (function->constructed_class) return ty_class_instance_type(me, function->constructed_class);
    if (function->ret_explicit || !function->has_body) return ty_from_node(me, &function->ret, function_lower_ctx);
    u64 key[2] = { KEY_FUNCTION_RETURN, (u64)(uintptr_t)function };
    void const** slot = intern_slot(me, &my links, key, 2);
    if (!*slot) *slot = arena_new(&my arena, FunctionReturn);
    FunctionReturn* memo = (FunctionReturn*)*slot;
    switch (memo->state) {
        case LAZY_STATE_DONE: return memo->type;
        case LAZY_STATE_IN_FLIGHT: return ty_intrinsic(me, TY_ERROR); // TODO: TS7023 implicitly any, references itself
        case LAZY_STATE_NOT_STARTED: break;
    }
    memo->state = LAZY_STATE_IN_FLIGHT;
    FlowIsolation isolation = ty_flow_isolate(me);
    struct OuterInference const* outer = my outer_inference; // (Nothing in the body is an argument of that call)
    my outer_inference = nullptr;
    memo->type = infer_return_type(me, function);
    my outer_inference = outer;
    ty_flow_restore(me, isolation);
    memo->state = LAZY_STATE_DONE;
    return memo->type;
}

bool ty_extends_array(Checker* me, Ty const* ty) {
    return ty_is_related(me, ty, ty_array(me, ty_intrinsic(me, TY_ANY), true), TY_RELATION_ASSIGNABLE);
}
