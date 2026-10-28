#include "callable_type.h"
#include "function_impl.h"
#include "error.h"
#include "scope.h"
#include "unused.h"
#include "binding.h"
#include "ty/ty.h"

void callable_type_drop(CallableType* me) {
    // (The function is in the CallableType's block, function_impl_into_type(): type_drop() frees both)
    function_impl_drop(my function);
}

static bool is_type_keyword(csview name) {
    static char const* const keywords[] = {
        "any", "unknown", "number", "bigint", "object", "boolean", "string", "symbol", "void", "undefined", "never",
    };
    for (u32 i = 0; i < c_arraylen(keywords); i++) {
        if (csview_equals(name, keywords[i])) return true;
    }
    return false;
}

void callable_type_resolve_names(CallableType* me, Scope* names, ErrorCtx* errs) {
    // (In a type: no uses of values, and `await`/`yield` aren't operators there, the parser has no such context in a type)
    FlowBuilder* flow = scope_flow(names);
    bool saved_type_query = flow && flow->in_type_query;
    if (flow) flow->in_type_query = true;
    function_impl_lookup_names(my function, false, names, errs);
    if (flow) flow->in_type_query = saved_type_query;
    // (tsc's reportImplicitAny: the name resolves as a type where the signature is, its own type parameters first, or
    // is a type keyword. Resolved as a use, whatever noImplicitAny says: `(Foo) => void` uses an imported type Foo)
    for_each (i, vec_function_arg, &my function->args) {
        Binding const* binding = &i->initializer.binding;
        if (binding->tag != BINDING_IDENTIFIER || binding->has_explicit_type) continue;
        csview name = span_sv(binding->content_span);
        // (Not a construct signature's: tsc asks only for call and method signatures and function types)
        if (my function->is_constructor) {
            i->name_is_type = is_type_keyword(name) || scope_get_type(names, name, nullptr);
            continue;
        }
        Symbol* found = nullptr;
        for_each (t, vec_generic_item, &my function->generic.items) {
            csview type_name = span_sv(t->name);
            if (t->owned_symbol && csview_eq(&type_name, &name)) found = t->owned_symbol;
        }
        Symbol const* as_found = nullptr;
        if (!found && scope_get_type(names, name, &as_found)) found = (Symbol*)as_found;
        i->name_is_type = is_type_keyword(name) || found;
        if (found) unused_mark_referenced(names, found, SYMBOL_TYPE);
    }
}

void callable_type_typecheck(CallableType const* me, ErrorCtx* errs) {
    function_impl_typecheck_signature(my function, true, errs);
    function_impl_check_parameter_decorators(my function, errs);
    function_impl_check_implicit_any_parameters(my function, errs);
    for_each (i, vec_function_arg, &my function->args) {
        Binding* binding = &i->initializer.binding;
        if (!binding->has_explicit_type) continue;
        type_typecheck(binding_type(binding), errs);
        // (tsc's checkParameter -> checkVariableLikeDeclaration: the parameter's type, reportCircularityError for
        // `(x: typeof x) => void`)
        if (binding->tag == BINDING_IDENTIFIER && binding->symbol &&
            ty_binding_type(current_checker, binding, (LowerCtx){ .generic = true }) == ty_intrinsic(current_checker, TY_CIRCULAR)
        ) {
            error_ctx_error_d(errs, i->span, 2502, // (at the whole parameter, its symbol's valueDeclaration)
                "'", binding->content_span, "' is referenced directly or indirectly in its own type annotation.");
        }
    }
    type_typecheck(&my function->ret, errs);
    if (my function->predicate && my function->predicate->has_type) type_typecheck(&my function->predicate->type, errs);
}
